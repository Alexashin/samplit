#pragma once

#include "Kit.h"

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <memory>
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
    class DiskStreamer : private juce::Thread
    {
    public:
        static constexpr int kMaxStreams = 512;         // одновременно читаемых хвостов
        static constexpr int kRingFrames = 32768;       // степень двойки: 0,74 с при 44,1 кГц
        static constexpr int kChunkFrames = 8192;       // порция чтения с диска
        static constexpr int kRingMask = kRingFrames - 1;

        // startThread = false: потоков нет, чтение делает pump() (детерминированные тесты)
        explicit DiskStreamer (bool startThread = true);
        ~DiskStreamer() override;

        // Audio thread. Занимает ячейку под хвост клипа. -1: свободных нет (голос доиграет голову и затихнет)
        int acquire (const DecodedClip& clip, const Kit* kit) noexcept;
        void release (int index) noexcept;

        // Audio thread: сколько кадров клипа от начала области уже доступно (голова + прочитанное в кольцо)
        int64_t available (int index) const noexcept { return streams[(size_t) index].written.load (std::memory_order_acquire); }
        const float* ring (int index) const noexcept { return streams[(size_t) index].ring; }

        // Audio thread: голос дочитал до этого кадра, кольцо до него можно перезаписывать
        void setConsumed (int index, int64_t frame) noexcept { streams[(size_t) index].consumed.store (frame, std::memory_order_release); }

        // Audio thread: чтение этого голоса сломалось, ждать хвост бессмысленно
        bool hasFailed (int index) const noexcept { return streams[(size_t) index].failed.load (std::memory_order_acquire); }

        // Останавливает поток чтения. Звать до того, как будут удалены киты: пока поток жив, он читает клипы
        // звучащих голосов. После остановки ячейки не обслуживаются
        void shutdown() { stopThread (3000); }

        // Будит поток чтения (системный вызов: не для обычного audio thread, только для офлайн-рендера)
        void kick() { notify(); }

        // Любой поток: из кита ещё читается хвост (ячейка не свободна)
        bool isUsing (const Kit* kit) const noexcept;
        int numActive() const noexcept;

        // Один проход потока чтения: открыть/закрыть файлы и прочитать одну порцию самому голодному голосу.
        // true: что-то сделано. Вызывать только если поток не запущен (startThread = false)
        bool pump();

        std::atomic<uint32_t> underruns { 0 };      // голос затих, потому что хвост не успел прочитаться
        std::atomic<uint32_t> noSlot { 0 };         // не нашлось свободной ячейки: голос доиграл только голову
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
            float* ring = nullptr;
            std::unique_ptr<juce::FileInputStream> file;    // только поток чтения
        };

        void run() override;
        bool pass();
        void open (Stream&);
        bool readChunk (Stream&);

        std::unique_ptr<float[]> ringStorage;
        std::array<Stream, kMaxStreams> streams;
        std::vector<float> chunk;
        std::vector<uint8_t> scratch;
        int nextFree = 0;                       // подсказка для acquire, только audio thread
    };
}
