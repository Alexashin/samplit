#pragma once

#include "Kit.h"

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <cstdint>

namespace samplit
{
    // Движок ударов: по ноте и velocity выбирает ОДИН удар барабана и запускает его файлы на всех микрофонах
    // с одного и того же сэмпла старта, каждый микрофон на свой выход. Громкость от velocity не применяется
    // (динамика лежит в самой записи), если кит не просит иного.
    //
    // noteOn, render и publishActivity вызываются только из audio thread: без аллокаций и блокировок.
    // Кит и клипы движку не принадлежат: их держит процессор, пока isUsing() возвращает true.
    class DiskStreamer;

    // Сводка здоровья звука: всё, что обрывает или портит воспроизведение. steals (мягкое вытеснение) в норме не показывается
    struct StreamHealth
    {
        uint32_t underruns = 0;         // настоящий обрыв: диск не успел или ячейка не нашлась до конца головы
        uint32_t steals = 0;            // нормальное вытеснение: старый удар затух (10 мс), ячейка отдана новому
        uint32_t hardSteals = 0;        // крайний случай: удар вытеснен без затухания
        uint32_t noSlot = 0;            // отказов acquire ячейки
        uint32_t readErrors = 0;        // файл не открылся или не читается
        uint32_t offlineTimeouts = 0;   // офлайн-рендер не дождался диска
        juce::String lastError;         // «имя.wav (причина)»

        bool operator== (const StreamHealth&) const = default;
    };

    class KitEngine
    {
    public:
        static constexpr int kMaxPieces = 32;    // барабанов с отдельной громкостью
        static constexpr int kMaxStrikes = 256;  // одновременно звучащих ударов (каждый до kMaxMics голосов)
        static constexpr int kSoftFreeStrikes = 8;   // меньше стольких свободных мест: старейший удар заранее затухает

        // Что запустил последний noteOn (для тестов и индикации)
        struct StrikeInfo
        {
            int piece = -1;
            int hit = -1;
            int layer = -1;
            int voices = 0;     // сколько микрофонных голосов запущено; 0: на клавише ничего не сыграло
        };

        void prepare (double sampleRate) noexcept;

        // Message thread: новый кит. Режим выбора удара и разброс берутся из кита, потом их можно поменять отдельно.
        // Звучащие удары доигрывают из своих клипов
        void setKit (const Kit* kit) noexcept;
        const Kit* getKit() const noexcept { return current.load(); }

        // Message thread
        void setSelectMode (HitSelect mode) noexcept { selectMode.store ((int) mode); }
        void setSpread (int layers) noexcept { spread.store (layers < 0 ? 0 : layers); }

        // Message thread, до первой игры: стример хвостов с диска. nullptr: потоковые клипы играют только голову
        void setStreamer (DiskStreamer* s) noexcept { streamer = s; }

        // Audio thread, раз в блок: офлайн-рендер (экспорт в DAW). Тогда голос ждёт недочитанный хвост вместо затухания
        void setOffline (bool isOffline) noexcept { offline.store (isOffline, std::memory_order_relaxed); }

        // Message thread: сводка здоровья (счётчики движка и стримера, последняя ошибка чтения) и её сброс
        StreamHealth getHealth() const;
        void resetHealth();

        // Message thread: режим слоёв переключателя Kit::choices[choice]. -1: как в ките
        void setPickChoice (int choice, int pick) noexcept;

        // Message thread: уровень (дБ) и панорама микрофона. Панорама действует, когда микрофон идёт в Main
        void setMicMix (int mic, float levelDb, float pan) noexcept;

        // Message thread: громкость барабана кита (дБ), действует на удары, которые начнутся после вызова
        void setPieceLevel (int piece, float levelDb) noexcept;

        // Message thread: выход микрофона поверх выхода из кита. -1: как в ките
        void setMicOutput (int mic, int out) noexcept;

        // Детерминированный ГСЧ для тестов
        void setSeed (uint32_t seed) noexcept { rng = seed != 0 ? seed : 0x9e3779b9u; }

        // Message thread: кит ещё нужен audio thread (он текущий или из него звучит удар)
        bool isUsing (const Kit* kit) const noexcept;

        // velocity 1..127
        StrikeInfo noteOn (int note, int velocity) noexcept;

        // Дописывает все голоса в [start, end) блока. outL/outR индексируются выходом микрофона (0 = Main).
        // outR[i] == nullptr означает моно-выход: панорама не применяется. Указатели выходов, которых нет у хоста,
        // должны вести в Main (и левый, и правый). Суммирует, буфер не очищает.
        void render (const std::array<float*, kNumOutputs + 1>& outL, const std::array<float*, kNumOutputs + 1>& outR,
                     int start, int end) noexcept;

        // Какие клавиши сейчас звучат (биты 0..63 и 64..127). Обновляется publishActivity()
        std::array<uint64_t, 2> getActiveKeys() const noexcept { return { activeLo.load(), activeHi.load() }; }
        void publishActivity() noexcept;

        int getNumActiveStrikes() const noexcept;

    private:
        struct MicVoice
        {
            const DecodedClip* clip = nullptr;
            double position = 0.0;
            double step = 1.0;
            float gain = 0.0f;       // громкость файла и velocity, без уровня микрофона
            int mic = 0;
            int fadeLeft = -1;       // -1: не затухает
            int stream = -1;         // ячейка DiskStreamer с хвостом клипа, -1: хвоста нет
            int fadeTotal = 1;       // длина начатого затухания в кадрах (знаменатель огибающей)
            bool wantStream = false; // потоковый клип ещё без ячейки: повторять acquire на каждом куске
            int stealWait = 0;       // кадров до следующего вытеснения ради этого голоса
        };

        struct Strike
        {
            int piece = -1;
            int note = -1;
            int numActive = 0;
            uint64_t age = 0;
            bool fading = false;     // удар уже глушится (choke, вытеснение): повторно не трогать
            std::array<MicVoice, Kit::kMaxMics> voices {};
        };

        static constexpr int kMaxCycleLayers = 16;     // слоёв на клавише со своим счётчиком круга

        // Кусок диапазона не длиннее kRenderChunk: хвост потокового голоса проверяется и подтверждается на каждом куске,
        // поэтому большой блок хоста (офлайн-рендер) не требует данных больше, чем помещается в кольцо
        static constexpr int kRenderChunk = 2048;
        void renderChunk (const std::array<float*, kNumOutputs + 1>& outL, const std::array<float*, kNumOutputs + 1>& outR,
                          int start, int end) noexcept;

        int selectHit (const Kit&, const NoteMap&, int note, int velocity) noexcept;
        void fadeStrike (Strike&, int frames) noexcept;
        // Глушит старейшие не затухающие удары с ячейками стримера, пока не освободится не меньше needed ячеек.
        // exclude: индекс удара, который не трогать. Возвращает, сколько ячеек освободится
        int stealStreams (int needed, int exclude) noexcept;
        void softEvictStrike() noexcept;
        int waitForStream (MicVoice&, const Kit*, double target) noexcept;
        void dropVoice (MicVoice&) noexcept;     // голос закончился: отпускает ячейку стримера
        void release (int index) noexcept;
        uint32_t nextRandom() noexcept;

        std::atomic<const Kit*> current { nullptr };
        DiskStreamer* streamer = nullptr;
        std::atomic<bool> offline { false };
        std::atomic<int> selectMode { (int) HitSelect::Live };
        std::atomic<int> spread { 0 };
        std::array<std::atomic<float>, kMaxPieces> pieceGain;
        std::array<std::atomic<float>, Kit::kMaxMics> micLevel;
        std::array<std::atomic<float>, Kit::kMaxMics> micPan;
        std::array<std::atomic<int>, Kit::kMaxMics> micOut;
        std::array<std::atomic<int>, Kit::kMaxChoices> choicePick;
        std::array<std::atomic<const Kit*>, kMaxStrikes> strikeKit {};   // != nullptr, пока удар звучит
        std::array<Strike, kMaxStrikes> strikes {};
        std::array<int, 128> lastHit;
        std::array<std::array<uint32_t, kMaxCycleLayers>, 128> cyclePos {};   // следующий удар круга по клавише и слою
        const Kit* lastKit = nullptr;
        std::atomic<uint64_t> activeLo { 0 }, activeHi { 0 };
        uint64_t ageCounter = 0;
        uint32_t rng = 0x9e3779b9u;
        double sr = 44100.0;
        int fadeLength = 220;       // 5 мс: недогрузка диска
        int stealFade = 441;        // 10 мс: choke и вытеснение
        int offlineBudgetMs = 30000;    // общий предел ожидания диска за один вызов render()
        std::atomic<uint32_t> steals { 0 }, hardSteals { 0 }, offlineTimeouts { 0 };

    public:
        KitEngine();
    };
}
