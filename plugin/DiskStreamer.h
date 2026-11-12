#pragma once

#include "Kit.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace samplit
{
    // Потоковое чтение хвостов сэмплов с диска (как DFD в Kontakt). Движок держит в памяти только голову клипа, а то,
    // что дальше головы, читает фоновый поток в кольцо звучащего голоса.
    //
    // Обмен без блокировок: у каждого голоса своя ячейка Stream, состояние ячейки само служит сообщением.
    //   Free → Requested   audio thread (acquire): заполнил ячейку и опубликовал
    //   Requested → Active поток чтения: открыл файл
    //   Requested/Active → Released   audio thread (release): голос закончился, вытеснен или заглушен
    //   Released → Free    поток чтения: закрыл файл
    // Свободную ячейку берёт только audio thread, освобождает только поток чтения, поэтому ABA нет. Кольцо одно
    // на ячейку: пишет только поток чтения, читает только audio thread. Кадры нумеруются от начала области клипа,
    // в кольце лежат кадры от головы (кадр k хранится в слоте k & kRingMask).
    //
    // Память колец выделяется лениво, блоками по kBlockSlots ячеек (8 МБ), и только потоком чтения: пустой
    // экземпляр занимает 0 МБ, максимум kMaxStreams ячеек (256 МБ). Блок публикуется через atomic<float*>, затем
    // allocatedSlots.store(release); audio thread ищет свободную ячейку только среди уже выделенных.
    class DiskStreamer : private juce::Thread
    {
    public:
        static constexpr int kMaxStreams = 2048;        // одновременно читаемых хвостов
        static constexpr int kBlockSlots = 64;          // ячеек в одном блоке памяти (64 * 128 КБ = 8 МБ)
        static constexpr int kRingFrames = 32768;       // степень двойки: 0,74 с при 44,1 кГц
        static constexpr int kChunkFrames = 8192;       // порция чтения с диска
        static constexpr int kRingMask = kRingFrames - 1;

        // startThread = false: потоков нет, чтение делает pump() (детерминированные тесты).
        // maxSlots и blockSlots меняют только тесты: искусственная нехватка ячеек
        explicit DiskStreamer (bool startThread = true, int maxSlots = kMaxStreams, int blockSlots = kBlockSlots);
        ~DiskStreamer() override;

        // Audio thread. Занимает ячейку под хвост клипа. -1: свободных нет (голос повторит попытку позже,
        // иначе доиграет голову и затихнет). countRefusal = false: повторная попытка, отказ не считается в noSlot.
        // Без блокировок и аллокаций: ищет только среди выделенных ячеек, нехватку лишь отмечает флагом для потока чтения
        int acquire (const DecodedClip& clip, const Kit* kit, bool countRefusal = true) noexcept;
        void release (int index) noexcept;

        // Audio thread: сколько кадров клипа от начала области уже доступно (голова + прочитанное в кольцо)
        int64_t available (int index) const noexcept { return streams[(size_t) index].written.load (std::memory_order_acquire); }
        const float* ring (int index) const noexcept
        {
            return blockPtr[(size_t) (index / blockSlots)].load (std::memory_order_acquire) + (size_t) (index % blockSlots) * kRingFrames;
        }

        // Audio thread: голос дочитал до этого кадра, кольцо до него можно перезаписывать
        void setConsumed (int index, int64_t frame) noexcept { streams[(size_t) index].consumed.store (frame, std::memory_order_release); }

        // Audio thread: чтение этого голоса сломалось, ждать хвост бессмысленно
        bool hasFailed (int index) const noexcept { return streams[(size_t) index].failed.load (std::memory_order_acquire); }

        // Останавливает поток чтения. Звать до того, как будут удалены киты: пока поток жив, он читает клипы
        // звучащих голосов. После остановки ячейки не обслуживаются
        void shutdown() { stopThread (3000); }

        // Будит поток чтения (системный вызов: не для обычного audio thread, только для офлайн-рендера)
        void kick() { notify(); }

        // Message thread: подготовить память под хвосты (кит со стримящимися клипами загружен). Выделяет поток чтения,
        // а без потока (тесты) сразу. slots: сколько ячеек должно быть выделено как минимум
        void reserve (int slots);

        // Любой поток: из кита ещё читается хвост (ячейка не свободна)
        bool isUsing (const Kit* kit) const noexcept;
        int numActive() const noexcept;

        int getAllocatedSlots() const noexcept { return allocatedSlots.load (std::memory_order_acquire); }
        int getAllocatedBlocks() const noexcept { return (getAllocatedSlots() + blockSlots - 1) / blockSlots; }
        int getMaxSlots() const noexcept { return maxSlots; }
        // Audio thread: поток чтения ещё может выделить ячейки (не достигнут максимум и выделение не отказывало)
        bool canGrow() const noexcept { return allocatedSlots.load (std::memory_order_acquire) < maxSlots && ! allocFailed.load (std::memory_order_relaxed); }

        // Message thread: последняя ошибка чтения («имя.wav (причина)»). Пусто, если ошибок не было
        juce::String getLastError() const;
        // Message thread: обнуляет счётчики и последнюю ошибку (смена пресета)
        void resetCounters();

        // Один проход потока чтения: открыть/закрыть файлы и прочитать одну порцию самому голодному голосу.
        // true: что-то сделано. Вызывать только если поток не запущен (startThread = false)
        bool pump();

        std::atomic<uint32_t> underruns { 0 };      // настоящий обрыв: хвост не успел прочитаться или ячейка не нашлась до конца головы
        std::atomic<uint32_t> noSlot { 0 };         // факт отказа acquire (голос повторит попытку)
        std::atomic<uint32_t> readErrors { 0 };     // не открылся или не прочитался файл

    private:
        enum State : int { Free = 0, Requested = 1, Active = 2, Released = 3 };

        struct Stream
        {
            std::atomic<int> state { Free };
            std::atomic<const Kit*> kit { nullptr };
            const DecodedClip* clip = nullptr;
            std::atomic<int64_t> written { 0 };
            std::atomic<int64_t> consumed { 0 };
            std::atomic<bool> failed { false };     // файл не открылся или не читается: хвоста не будет
            std::unique_ptr<ClipReader> reader;     // только поток чтения (файл WAV или куски хвоста пакета)
        };

        void run() override;
        bool pass();
        bool ensureCapacity();
        bool allocateBlock();
        void setError (const Stream&, const juce::String& what);
        void fail (Stream&, const juce::String& what);
        void open (Stream&);
        bool readChunk (Stream&, int index);

        const int maxSlots;
        const int blockSlots;
        std::unique_ptr<Stream[]> streams;                          // метаданные всех maxSlots ячеек, заранее
        std::unique_ptr<std::atomic<float*>[]> blockPtr;            // блоки колец, выделяет только поток чтения
        std::vector<std::unique_ptr<float[]>> blocks;               // владение блоками
        std::atomic<int> allocatedSlots { 0 };
        std::atomic<int> reserveTarget { 0 };
        std::atomic<bool> needBlock { false };                      // acquire отказал: поток чтения должен срочно выделить блок
        std::atomic<bool> allocFailed { false };                    // пишет только поток чтения (или reserve без потока), читает кто угодно
        std::vector<float> chunk;
        int nextFree = 0;                       // подсказка для acquire, только audio thread

        mutable std::mutex errorMutex;          // audio thread его не берёт
        juce::String lastError;
    };
}
