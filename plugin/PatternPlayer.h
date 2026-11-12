#pragma once

// Проигрыватель партии (Pattern) внутри плагина: независим от транспорта хоста, точность до сэмпла.
// Message thread: setPattern / setSampleRate / play / stop / collectGarbage. Audio thread: только process().
// Снимок событий передаётся как у KitEngine: атомарный указатель + hazard pointer, без блокировок и аллокаций в audio thread

#include "Pattern.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace samplit
{
    class PatternPlayer
    {
    public:
        struct Hit
        {
            int offset = 0;             // отсчёт внутри блока
            int key = 0;
            int velocity = 0;
            int64_t seedPos = 0;        // проход * длина + позиция события: зерно повторяемого выбора записи
        };

        static constexpr int kMaxHits = 256;    // вместимость на блок; лишнее отбрасывается (при нормальных партиях недостижимо)

        PatternPlayer() = default;
        ~PatternPlayer();

        // --- message thread
        void setPattern (const Pattern&, double sampleRate);
        void setSampleRate (double sampleRate);     // пересчитать события партии под новую частоту
        void play() noexcept;                       // с начала партии
        void stop() noexcept;
        void setLoop (bool shouldLoop) noexcept { loop.store (shouldLoop); }
        bool isPlaying() const noexcept { return playing.load(); }
        int64_t getPositionSamples() const noexcept { return position.load (std::memory_order_relaxed); }
        void collectGarbage();                      // освободить снимки, которые audio thread уже не держит

        // --- audio thread: события, попадающие в блок из numSamples отсчётов, по возрастанию offset. Возвращает их число
        int process (int numSamples, Hit* out, int maxOut) noexcept;

    private:
        struct Play
        {
            std::vector<PatternEvent> events;
            int64_t length = 0;
        };

        void publish (const Pattern&, double sampleRate);

        std::atomic<const Play*> current { nullptr };
        std::atomic<const Play*> hazard { nullptr };
        std::vector<std::unique_ptr<Play>> owned;       // message thread: текущий и снятые, но возможно ещё читаемые

        Pattern pattern;                                // message thread: копия для пересчёта при смене частоты
        double rate = 44100.0;

        std::atomic<bool> playing { false };
        std::atomic<bool> loop { true };
        std::atomic<int> restart { 0 };
        std::atomic<int64_t> position { 0 };            // для курсора в UI
        // Только audio thread
        bool wasPlaying = false;
        int seenRestart = 0;
        int64_t pos = 0;
        int64_t pass = 0;
    };
}
