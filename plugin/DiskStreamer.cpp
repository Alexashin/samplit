#include "DiskStreamer.h"

#include <algorithm>
#include <new>

namespace samplit
{
    DiskStreamer::DiskStreamer (bool startThread, int maxSlotsIn, int blockSlotsIn)
        : juce::Thread ("Samplit disk streamer"), maxSlots (std::max (1, maxSlotsIn)), blockSlots (std::max (1, blockSlotsIn))
    {
        // Только метаданные ячеек; кольца (по 128 КБ) выделяются блоками потоком чтения по мере надобности
        streams.reset (new Stream[(size_t) maxSlots]);
        const int numBlocks = (maxSlots + blockSlots - 1) / blockSlots;
        blockPtr.reset (new std::atomic<float*>[(size_t) numBlocks]);
        for (int i = 0; i < numBlocks; ++i)
            blockPtr[(size_t) i].store (nullptr, std::memory_order_relaxed);
        blocks.reserve ((size_t) numBlocks);
        chunk.resize ((size_t) kChunkFrames);

        if (startThread)
            Thread::startThread (juce::Thread::Priority::high);
    }

    DiskStreamer::~DiskStreamer()
    {
        stopThread (3000);
    }

    int DiskStreamer::acquire (const DecodedClip& clip, const Kit* kit, bool countRefusal) noexcept
    {
        // Только уже выделенные ячейки: audio thread ничего не выделяет и не ждёт
        const int allocated = allocatedSlots.load (std::memory_order_acquire);
        for (int n = 0; n < allocated; ++n)
        {
            const int i = (nextFree + n) % allocated;
            auto& s = streams[(size_t) i];
            if (s.state.load (std::memory_order_acquire) != Free)
                continue;

            nextFree = (i + 1) % allocated;
            s.clip = &clip;
            s.kit.store (kit, std::memory_order_relaxed);
            s.failed.store (false, std::memory_order_relaxed);
            s.written.store (clip.headFrames(), std::memory_order_relaxed);
            s.consumed.store (clip.headFrames(), std::memory_order_relaxed);
            s.state.store (Requested, std::memory_order_release);
            return i;
        }
        needBlock.store (true, std::memory_order_relaxed);      // поток чтения выделит ещё блок, если есть куда
        if (countRefusal)
            noSlot.fetch_add (1, std::memory_order_relaxed);
        return -1;
    }

    void DiskStreamer::release (int index) noexcept
    {
        streams[(size_t) index].state.store (Released, std::memory_order_release);
    }

    void DiskStreamer::reserve (int slots)
    {
        if (slots <= 0)
            return;
        int target = reserveTarget.load();
        while (target < slots && ! reserveTarget.compare_exchange_weak (target, slots)) {}

        if (isThreadRunning())
            notify();
        else
            ensureCapacity();       // потока нет (тесты): выделяем сразу
    }

    bool DiskStreamer::allocateBlock()
    {
        const int n = allocatedSlots.load (std::memory_order_relaxed);
        if (n >= maxSlots || allocFailed.load (std::memory_order_relaxed))
            return false;

        // Без инициализации: страницы памяти занимаются при первой записи
        float* p = new (std::nothrow) float[(size_t) blockSlots * kRingFrames];
        if (p == nullptr)
        {
            allocFailed.store (true, std::memory_order_relaxed);
            readErrors.fetch_add (1, std::memory_order_relaxed);
            const std::lock_guard<std::mutex> lock (errorMutex);
            lastError = juce::String::fromUTF8 ("не хватило памяти под буферы чтения с диска");
            return false;
        }
        blocks.emplace_back (p);
        blockPtr[(size_t) (n / blockSlots)].store (p, std::memory_order_release);
        allocatedSlots.store (std::min (maxSlots, n + blockSlots), std::memory_order_release);
        return true;
    }

    // Только поток чтения (или reserve без потока). Держит запас: новый блок, как только занято больше 3/4 выделенного
    bool DiskStreamer::ensureCapacity()
    {
        bool did = false;
        const bool urgent = needBlock.exchange (false, std::memory_order_relaxed);
        for (;;)
        {
            const int n = allocatedSlots.load (std::memory_order_relaxed);
            if (n >= maxSlots || allocFailed.load (std::memory_order_relaxed))
                break;
            int busy = 0;
            for (int i = 0; i < n; ++i)
                busy += streams[(size_t) i].state.load (std::memory_order_relaxed) != Free ? 1 : 0;

            const bool need = n < reserveTarget.load() || (n == 0 && urgent) || (n > 0 && urgent && busy >= n)
                              || (n > 0 && busy * 4 >= n * 3);
            if (! need || ! allocateBlock())
                break;
            did = true;     // при срочной нехватке цикл выделяет блоки, пока занято не меньше 3/4
        }
        return did;
    }

    bool DiskStreamer::isUsing (const Kit* kit) const noexcept
    {
        const int allocated = allocatedSlots.load (std::memory_order_acquire);
        for (int i = 0; i < allocated; ++i)
        {
            const auto& s = streams[(size_t) i];
            if (s.state.load (std::memory_order_acquire) != Free && s.kit.load (std::memory_order_relaxed) == kit)
                return true;
        }
        return false;
    }

    int DiskStreamer::numActive() const noexcept
    {
        int n = 0;
        const int allocated = allocatedSlots.load (std::memory_order_acquire);
        for (int i = 0; i < allocated; ++i)
            n += streams[(size_t) i].state.load (std::memory_order_relaxed) != Free ? 1 : 0;
        return n;
    }

    juce::String DiskStreamer::getLastError() const
    {
        const std::lock_guard<std::mutex> lock (errorMutex);
        return lastError;
    }

    void DiskStreamer::resetCounters()
    {
        underruns.store (0);
        noSlot.store (0);
        readErrors.store (0);
        const std::lock_guard<std::mutex> lock (errorMutex);
        lastError = {};
    }

    void DiskStreamer::setError (const Stream& s, const juce::String& what)
    {
        const auto name = juce::String::fromUTF8 (s.clip->source != nullptr ? s.clip->source->name().c_str() : "");
        const std::lock_guard<std::mutex> lock (errorMutex);
        lastError = name + " (" + what + ")";
    }

    // Голос доиграет голову и затихнет как при недогрузке
    void DiskStreamer::fail (Stream& s, const juce::String& what)
    {
        setError (s, what);
        s.reader.reset();
        s.failed.store (true, std::memory_order_release);
        readErrors.fetch_add (1, std::memory_order_relaxed);
    }

    void DiskStreamer::open (Stream& s)
    {
        std::string err;
        if (s.clip->source != nullptr)
            s.reader = s.clip->source->open (err);
        if (s.reader == nullptr)
            fail (s, err.empty() ? juce::String::fromUTF8 ("файл не открылся") : juce::String::fromUTF8 (err.c_str()));
    }

    bool DiskStreamer::readChunk (Stream& s, int index)
    {
        const int64_t written = s.written.load (std::memory_order_relaxed);
        const int64_t consumed = s.consumed.load (std::memory_order_acquire);
        const int64_t total = s.clip->frames();
        const int64_t n = std::min<int64_t> (kChunkFrames, total - written);
        if (n <= 0 || written + n > consumed + kRingFrames)
            return false;

        std::string err;
        if (! s.reader->read (written, (int) n, chunk.data(), err))
        {
            fail (s, err.empty() ? juce::String::fromUTF8 ("ошибка чтения") : juce::String::fromUTF8 (err.c_str()));
            return false;
        }
        float* ring = blockPtr[(size_t) (index / blockSlots)].load (std::memory_order_relaxed) + (size_t) (index % blockSlots) * kRingFrames;
        for (int64_t j = 0; j < n; ++j)
            ring[(written + j) & kRingMask] = chunk[(size_t) j];
        s.written.store (written + n, std::memory_order_release);
        return true;
    }

    bool DiskStreamer::pass()
    {
        bool worked = ensureCapacity();
        Stream* best = nullptr;
        int bestIndex = 0;
        int64_t bestLead = INT64_MAX;

        const int allocated = allocatedSlots.load (std::memory_order_relaxed);
        for (int si = 0; si < allocated; ++si)
        {
            auto& s = streams[(size_t) si];
            const int state = s.state.load (std::memory_order_acquire);
            if (state == Requested)
            {
                open (s);
                int expected = Requested;
                // Если голос уже отпущен, состояние осталось Released: файл закроется на следующем проходе
                s.state.compare_exchange_strong (expected, Active, std::memory_order_acq_rel);
                worked = true;
            }
            else if (state == Released)
            {
                s.reader.reset();
                s.state.store (Free, std::memory_order_release);
                worked = true;
            }
            else if (state == Active && s.reader != nullptr)
            {
                const int64_t written = s.written.load (std::memory_order_relaxed);
                const int64_t consumed = s.consumed.load (std::memory_order_acquire);
                const int64_t n = std::min<int64_t> (kChunkFrames, (int64_t) s.clip->frames() - written);
                if (n > 0 && written + n <= consumed + kRingFrames && written - consumed < bestLead)
                {
                    bestLead = written - consumed;
                    best = &s;
                    bestIndex = si;
                }
            }
        }

        if (best != nullptr && readChunk (*best, bestIndex))
            worked = true;
        return worked;
    }

    bool DiskStreamer::pump()
    {
        return pass();
    }

    void DiskStreamer::run()
    {
        while (! threadShouldExit())
            if (! pass())
                wait (2);
    }
}
