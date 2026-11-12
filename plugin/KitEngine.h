#pragma once

#include "Kit.h"
#include "Setup.h"

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

        // Message thread: новый кит. Режим выбора удара берётся из кита.
        // Звучащие удары доигрывают из своих клипов
        void setKit (const Kit* kit) noexcept;
        const Kit* getKit() const noexcept { return current.load(); }

        // Message thread
        void setSelectMode (HitSelect mode) noexcept { selectMode.store ((int) mode); }

        // «Удар выбирает»: партия (число velocity = конкретная запись, как в Kontakt) или плагин (velocity = сила, запись
        // выбирает плагин). partMode перекрывает режим кита
        void setPartMode (bool on) noexcept { partMode.store (on); }
        // Плагин выбирает повторяемо: запись зависит от позиции в песне (сэмпл транспорта), клавиши и слоя, поэтому
        // прослушивание, экспорт и заморозка звучат одинаково. false: «живой» выбор по кругу/случайно
        void setRepeatable (bool on) noexcept { repeatable.store (on); }
        // Audio thread: позиция следующего noteOn в сэмплах транспорта; valid=false (нет транспорта, ноты с экрана): живой выбор
        void setStrikePosition (int64_t samples, bool valid) noexcept { strikePos = samples; strikePosValid = valid; }

        // Message thread, до первой игры: стример хвостов с диска. nullptr: потоковые клипы играют только голову
        void setStreamer (DiskStreamer* s) noexcept { streamer = s; }

        // Audio thread, раз в блок: офлайн-рендер (экспорт в DAW). Тогда голос ждёт недочитанный хвост вместо затухания
        // Выход из офлайна сбрасывает «сдачу» (offlineGaveUp): после исчерпания бюджета диск до конца экспорта не ждём
        void setOffline (bool isOffline) noexcept
        {
            if (! isOffline)
                offlineGaveUp = false;
            offline.store (isOffline, std::memory_order_relaxed);
        }
        bool hasGivenUpOffline() const noexcept { return offlineGaveUp; }
        // Для тестов: бюджет ожидания диска на блок в офлайне (по умолчанию 30 с)
        void setOfflineBudget (int ms) noexcept { offlineBudgetMax = std::max (0, ms); offlineBudgetMs = offlineBudgetMax; }

        // Audio thread (или остановленный движок): все звучащие удары затухают за ~10 мс (MIDI All Sound Off / All Notes Off)
        void fadeAll() noexcept;
        // Audio thread (bypass) или остановленный движок (releaseResources): сбросить все удары, освободить ячейки стримера. Без аллокаций
        void releaseAll() noexcept;
        // Максимальная длина рампы микрофонов в отсчётах: огромный офлайн-блок не даёт ноте после размьюта атаку в десятки мс
        static constexpr int kMaxRampSamples = 512;

        // Message thread: сводка здоровья (счётчики движка и стримера, последняя ошибка чтения) и её сброс
        StreamHealth getHealth() const;
        void resetHealth();

        // Message thread: режим слоёв переключателя Kit::choices[choice]. -1: как в ките
        void setPickChoice (int choice, int pick) noexcept;

        // Message thread: уровень (дБ) и панорама микрофона. Панорама действует и на стерео-шинах (Toms, Over, Room), и в Main
        void setMicMix (int mic, float levelDb, float pan) noexcept;

        // Message thread: выход микрофона поверх выхода из кита. -1: как в ките
        void setMicOutput (int mic, int out) noexcept;

        // Message thread: громкость места установки (дБ, по Piece::slot). Читается при старте удара и не зависит от смены кита;
        // уже звучащий удар не меняется. slot вне 0..kMaxPlaces-1 игнорируется
        void setPlaceLevel (int slot, float levelDb) noexcept;

        // Детерминированный ГСЧ для тестов
        void setSeed (uint32_t seed) noexcept { rng = seed != 0 ? seed : 0x9e3779b9u; }

        // Message thread: кит ещё нужен audio thread (текущий, защищён блоком, из него звучит удар или читает стример).
        //
        // Протокол защиты (hazard pointer). Audio thread в начале каждого блока вызывает beginBlock():
        //   k = current.load(); hazard.store(k, seq_cst); повторять, пока current.load() != k (число повторов ограничено).
        // Весь блок (noteOn, render) работает только с этим k. В конце блока endBlock() сбрасывает hazard (release).
        // Message thread: setKit() сначала меняет current (seq_cst), и только потом списанный кит проверяется isUsing().
        // Порядок проверки в isUsing: current, hazard, затем strikeKit и ячейки стримера. Удар и ячейка записываются
        // раньше, чем endBlock() снимает hazard, поэтому кит не может «проскочить» между проверками.
        // Если повторная проверка current в beginBlock() увидела старый кит, то hazard.store стоял в общем порядке
        // seq_cst до смены current, а значит isUsing() увидит hazard.
        bool isUsing (const Kit* kit) const noexcept;

        // Audio thread: границы блока (см. протокол выше). Без пары noteOn читает current напрямую (только тесты)
        // rampSamples > 0: уровень и панорама микрофонов в этом блоке линейно идут от значения прошлого блока к целевому
        // (setMicMix) за rampSamples отсчётов (длина блока); 0: целевое значение действует сразу
        void beginBlock (int rampSamples = 0) noexcept;
        void endBlock() noexcept;
        // Для тестов: кит сейчас защищён hazard pointer
        bool isProtected (const Kit* kit) const noexcept { return kit != nullptr && hazard.load() == kit; }
        // Для тестов: serial кита, с которым связаны lastHit и cyclePos (0: ещё ни с каким)
        uint64_t getLastKitSerial() const noexcept { return lastKitSerial; }

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
            int bus = 0;             // выход фиксируется при старте удара: смена «Стерео / Мульти-аут» не двигает звучащий голос
            int fadeLeft = -1;       // -1: не затухает
            int stream = -1;         // ячейка DiskStreamer с хвостом клипа, -1: хвоста нет
            int fadeTotal = 1;       // длина начатого затухания в кадрах (знаменатель огибающей)
            bool wantStream = false; // потоковый клип ещё без ячейки: повторять acquire на каждом куске
            int stealWait = 0;       // кадров до следующего вытеснения ради этого голоса
            bool underrunCounted = false;   // обрыв этого голоса уже учтён в underruns (считаем один раз на голос)
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

        // Live-режим: сила вне записанных окон клавиши сдвигается к ближайшему окну; ниже самого нижнего окна
        // файл прежний, а уровень снижается (quietGain, не больше -18 дБ). Kontakt-режим силу не меняет
        int fitVelocity (const Kit&, const NoteMap&, int velocity, float& quietGain) const noexcept;
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
        std::atomic<const Kit*> hazard { nullptr };     // кит, с которым audio thread работает в текущем блоке
        const Kit* blockKit = nullptr;                  // он же, только audio thread; действует, пока inBlock
        bool inBlock = false;
        DiskStreamer* streamer = nullptr;
        std::atomic<bool> offline { false };
        std::atomic<int> selectMode { (int) HitSelect::Live };
        std::atomic<bool> partMode { false };
        std::atomic<bool> repeatable { true };
        int64_t strikePos = 0;              // только audio thread
        bool strikePosValid = false;
        bool kontaktNow = false;            // режим на время одного удара: читается один раз в noteOn, чтобы fitVelocity и selectHit не разошлись
        int64_t lastStrikePos = -1;         // для разведения одинаковых нот в одном сэмпле
        uint32_t samePosCount = 0;
        bool isKontakt() const noexcept { return kontaktNow; }
        std::array<std::atomic<float>, Kit::kMaxMics> micLevel;
        std::array<std::atomic<float>, Kit::kMaxMics> micPan;
        std::array<std::atomic<int>, Kit::kMaxMics> micOut;
        // Только audio thread: гейн и панорама микрофона на данный момент (рампа к micLevel/micPan), общие для всех голосов
        std::array<float, Kit::kMaxMics> curLevel {}, curPan {}, stepLevel {}, stepPan {};
        int rampLen = 0;
        bool mixPrimed = false;
        std::array<std::atomic<int>, Kit::kMaxChoices> choicePick;
        std::array<std::atomic<float>, kMaxPlaces> placeGain;     // линейный гейн мест, по Piece::slot
        std::array<std::atomic<const Kit*>, kMaxStrikes> strikeKit {};   // != nullptr, пока удар звучит
        std::array<Strike, kMaxStrikes> strikes {};
        std::array<int, 128> lastHit;
        std::array<std::array<uint32_t, kMaxCycleLayers>, 128> cyclePos {};   // следующий удар круга по клавише и слою
        uint64_t lastKitSerial = 0;     // serial кита, для которого накоплены lastHit и cyclePos
        std::atomic<uint64_t> activeLo { 0 }, activeHi { 0 };
        uint64_t ageCounter = 0;
        uint32_t rng = 0x9e3779b9u;
        double sr = 44100.0;
        int fadeLength = 220;       // 5 мс: недогрузка диска
        int stealFade = 441;        // 10 мс: choke и вытеснение
        int offlineBudgetMax = 30000;
        int offlineBudgetMs = 30000;    // общий предел ожидания диска на блок (сбрасывается в beginBlock)
        bool offlineGaveUp = false;     // только audio thread: бюджет исчерпан, до конца экспорта диск не ждём
        std::atomic<uint32_t> steals { 0 }, hardSteals { 0 }, offlineTimeouts { 0 };

    public:
        KitEngine();
    };
}
