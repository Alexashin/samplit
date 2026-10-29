#include "DiskStreamer.h"

#include "samplit/core/Wav.h"

#include <algorithm>

namespace samplit
{
    DiskStreamer::DiskStreamer (bool startThread) : juce::Thread ("Samplit disk streamer")
    {
        // Без инициализации: страницы памяти занимаются при первой записи
        ringStorage.reset (new float[(size_t) kMaxStreams * kRingFrames]);
        for (int i = 0; i < kMaxStreams; ++i)
            streams[(size_t) i].ring = ringStorage.get() + (size_t) i * kRingFrames;
        chunk.resize ((size_t) kChunkFrames);
        scratch.resize ((size_t) kChunkFrames * 3);

        if (startThread)
            Thread::startThread (juce::Thread::Priority::high);
    }

    DiskStreamer::~DiskStreamer()
    {
        stopThread (3000);
    }

    int DiskStreamer::acquire (const DecodedClip& clip, const Kit* kit) noexcept
    {
        for (int n = 0; n < kMaxStreams; ++n)
        {
            const int i = (nextFree + n) % kMaxStreams;
            auto& s = streams[(size_t) i];
            if (s.state.load (std::memory_order_acquire) != Free)
                continue;

            nextFree = (i + 1) % kMaxStreams;
            s.clip = &clip;
            s.kit.store (kit, std::memory_order_relaxed);
            s.written.store (clip.headFrames(), std::memory_order_relaxed);
            s.consumed.store (clip.headFrames(), std::memory_order_relaxed);
            s.state.store (Requested, std::memory_order_release);
            return i;
        }
        noSlot.fetch_add (1, std::memory_order_relaxed);
        return -1;
    }

    void DiskStreamer::release (int index) noexcept
    {
        streams[(size_t) index].state.store (Released, std::memory_order_release);
    }

    bool DiskStreamer::isUsing (const Kit* kit) const noexcept
    {
        for (const auto& s : streams)
            if (s.state.load (std::memory_order_acquire) != Free && s.kit.load (std::memory_order_relaxed) == kit)
                return true;
        return false;
    }

    int DiskStreamer::numActive() const noexcept
    {
        int n = 0;
        for (const auto& s : streams)
            n += s.state.load (std::memory_order_relaxed) != Free ? 1 : 0;
        return n;
    }

    void DiskStreamer::open (Stream& s)
    {
        s.file = std::make_unique<juce::FileInputStream> (juce::File (juce::String::fromUTF8 (s.clip->path.c_str())));
        if (s.file->failedToOpen())
        {
            s.file.reset();
            readErrors.fetch_add (1, std::memory_order_relaxed);     // голос доиграет голову и затихнет как при недогрузке
        }
    }

    bool DiskStreamer::readChunk (Stream& s)
    {
        const int64_t written = s.written.load (std::memory_order_relaxed);
        const int64_t consumed = s.consumed.load (std::memory_order_acquire);
        const int64_t total = s.clip->frames();
        const int64_t n = std::min<int64_t> (kChunkFrames, total - written);
        if (n <= 0 || written + n > consumed + kRingFrames)
            return false;

        if (! pack::readWav24 (*s.file, s.clip->dataOffset + written * 3, (int) n, chunk.data(), scratch))
        {
            s.file.reset();         // дальше этот голос не читается: он затихнет как при недогрузке
            readErrors.fetch_add (1, std::memory_order_relaxed);
            return false;
        }
        for (int64_t j = 0; j < n; ++j)
            s.ring[(written + j) & kRingMask] = chunk[(size_t) j];
        s.written.store (written + n, std::memory_order_release);
        return true;
    }

    bool DiskStreamer::pass()
    {
        bool worked = false;
        Stream* best = nullptr;
        int64_t bestLead = INT64_MAX;

        for (auto& s : streams)
        {
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
                s.file.reset();
                s.state.store (Free, std::memory_order_release);
                worked = true;
            }
            else if (state == Active && s.file != nullptr)
            {
                const int64_t written = s.written.load (std::memory_order_relaxed);
                const int64_t consumed = s.consumed.load (std::memory_order_acquire);
                const int64_t n = std::min<int64_t> (kChunkFrames, (int64_t) s.clip->frames() - written);
                if (n > 0 && written + n <= consumed + kRingFrames && written - consumed < bestLead)
                {
                    bestLead = written - consumed;
                    best = &s;
                }
            }
        }

        if (best != nullptr && readChunk (*best))
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
