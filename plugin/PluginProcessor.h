#pragma once

#include "DiskStreamer.h"
#include "KitEngine.h"
#include "KitLoader.h"
#include "LicenseManager.h"
#include "PresetLibrary.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <vector>

namespace samplit
{
    class SamplitProcessor final : public juce::AudioProcessor,
                                   public juce::ChangeBroadcaster,
                                   private juce::AudioProcessorParameter::Listener,
                                   private juce::AsyncUpdater,
                                   private juce::Timer
    {
    public:
        SamplitProcessor();
        ~SamplitProcessor() override;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override {}
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using juce::AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return JucePlugin_Name; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        // Состояние в проекте DAW: пресет, микшер, режимы выбора удара, громкости барабанов (старые узлы GROUP/ZONE игнорируются)
        void getStateInformation (juce::MemoryBlock&) override;
        void setStateInformation (const void*, int) override;

        // ---- Кит (мультимикрофонные барабаны) ----

        // Задаёт кит: MIDI и ноты с экранной клавиатуры идут в него. nullptr: тишина.
        // Кит должен быть собран (Kit::buildIndex()) и после публикации не меняется. Старый кит живёт, пока из него звучит удар
        void setKit (std::shared_ptr<const Kit>);
        std::shared_ptr<const Kit> getKit() const;

        // Фоновая загрузка кита из пресета и пакетов. Новая загрузка отменяет прежнюю. По готовности (message thread)
        // кит ставится через setKit(), микрофонам выставляются уровни и панорамы, потом зовётся done.
        // При ошибке или отмене прежний кит остаётся играть. Звук не блокируется
        void loadKitAsync (const Preset&, KitLoadOptions, std::function<void (const KitLoadResult&)> done = {});
        void cancelKitLoad();

        // То же, но пакеты берутся из папки лицензии, а ключи из действующего токена
        void loadPreset (const Preset&, std::function<void (const KitLoadResult&)> done = {});

        LicenseManager& getLicense() noexcept { return *license; }
        // Для тестов: подмена менеджера лицензий (другая папка данных, ключ, часы)
        void setLicenseManager (std::unique_ptr<LicenseManager>);
        bool isKitLoading() const noexcept { return kitLoading.load(); }
        float getKitLoadProgress() const noexcept { return kitProgress.load(); }

        // Итог последней загрузки (без самого кита): ошибка, недоступные пакеты, предупреждения
        const KitLoadResult& getLastKitLoad() const noexcept { return lastLoad; }

        // Пресеты из папки данных и состояние их пакетов
        std::vector<PresetEntry> scanPresets() const;

        // Выбор пресета пользователем: запоминается в проекте, микшер берёт значения пресета
        void selectPreset (const Preset&, std::function<void (const KitLoadResult&)> done = {});

        // «Сохранить как…»: текущий пресет с нынешними микшером и режимами выбора удара, новым файлом в папке пресетов.
        // Существующие файлы не перезаписываются. Возвращает файл, при ошибке пустой File и текст в error
        juce::File saveCurrentPresetAs (const juce::String& name, juce::String& error);

        // ---- Миксы микрофонов (файлы в папке mixes рядом с пресетами) ----
        std::vector<Mix> scanMixes() const;
        // Файл микса по id (пустой File, если нет)
        juce::File getMixFile (const juce::String& id) const;
        juce::File getMixDir() const;

        // Применить микс к загруженному киту: уровни, панорамы, выходы, mute. Без перезагрузки, solo не трогает
        void applyMix (const Mix&);

        // Id применённого микса (пусто: микс не выбран) и признак, что после него микшер правили руками
        juce::String getMixName() const;
        bool isMixModified() const;

        // «Сохранить микс…»: текущий микшер новым файлом, существующие не перезаписываются
        juce::File saveCurrentMixAs (const juce::String& name, juce::String& error);

        // Свои пресеты и миксы (файлы user-*.json): переименовать (меняется только название) и удалить в корзину.
        // Чужие и стандартные файлы не трогаются: false и текст в error
        bool renameUserFile (const juce::File&, const juce::String& newName, juce::String& error);
        bool deleteUserFile (const juce::File&, juce::String& error);

        // После загрузки проекта: найти сохранённый пресет и загрузить его с сохранённым микшером.
        // false: пресет в проекте не задан или не найден в папке
        bool restorePreset();

        // ---- Микшер микрофонов кита (message thread). Индекс как в Kit::mics ----
        struct MicState
        {
            float levelDb = 0.0f;
            float pan = 0.0f;
            bool mute = false;
            bool solo = false;
            int out = 0;            // 0 = Main, 1..kNumOutputs = Out N
        };

        MicState getMicState (int mic) const;
        void setMicState (int mic, const MicState&);

        // ---- Громкость барабана (message thread, дБ −24…+12). Индекс как в Kit::pieces; сохраняется в проекте ----
        float getPieceLevel (int piece) const;
        void setPieceLevel (int piece, float levelDb);

        // ---- Автоматизация в DAW: уровень и панорама первых kAutomatedMics микрофонов кита (параметры "micNN_level", "micNN_pan").
        // Хост двигает параметр, микшер следует за ним, и наоборот. Mute, Solo и выход не автоматизируются ----
        static constexpr int kAutomatedMics = Kit::kMaxMics;

        // ---- Выбор удара (message thread): переключатели Kit::choices, сохраняются в проекте по id ----
        Pick getPickChoice (int choice) const;
        void setPickChoice (int choice, Pick);

        // Имя пресета в состоянии проекта (сам пресет подгружает загрузчик пакетов)
        juce::String getPresetName() const;
        void setPresetName (const juce::String&);

        // ---- UI ↔ audio thread ----

        // Сыграть клавишу из интерфейса (velocity 1..127)
        void triggerNote (int note, int velocity = 100) noexcept;

        // Какие клавиши звучат (биты 0..127 в двух словах)
        std::array<uint64_t, 2> getActiveKeys() const noexcept { return { activeLo.load(), activeHi.load() }; }

        // Какие моно-выходы 1..kNumOutputs включены в хосте (бит N-1). Микрофон на выключенный выход играет в Main
        uint32_t getEnabledOutputMask() const noexcept { return enabledOutputs.load(); }

        // Для тестов и сборки мусора: сколько китов ещё ждут удаления
        size_t getRetiredCount() const noexcept { return retired.size(); }

        // Чтение хвостов с диска: счётчики недогрузки (голос затих), нехватки ячеек и ошибок чтения, занятые ячейки
        const DiskStreamer& getStreamer() const noexcept { return streamer; }
        void collectGarbage (bool force = false);

    private:
        static BusesProperties makeBuses();
        void timerCallback() override { collectGarbage(); }

        DiskStreamer streamer;      // раньше kitEngine: переживает его при разрушении
        KitEngine kitEngine;
        struct Retired { std::shared_ptr<const Kit> kit; juce::uint32 since; };
        std::vector<Retired> retired;

        // Состояние проекта читается и из потока хоста при сохранении проекта
        mutable juce::CriticalSection stateLock;
        std::shared_ptr<const Kit> kit;
        juce::String presetName, mixName;
        bool mixModified = false, applyingMix = false;

        // Очередь нот из UI в audio thread: пишет только message thread, читает только audio thread
        static constexpr int kQueue = 64;
        std::array<std::atomic<uint32_t>, kQueue> queue {};     // note | velocity << 8
        std::atomic<uint32_t> queueWrite { 0 };
        std::atomic<uint32_t> queueRead { 0 };
        std::atomic<uint64_t> activeLo { 0 }, activeHi { 0 };
        std::atomic<uint32_t> enabledOutputs { 0 };
        void applyMixer();
        void applyPieceLevels();
        void pushParam (int mic);
        void pushAllParams();
        void parameterValueChanged (int, float) override { triggerAsyncUpdate(); }
        void parameterGestureChanged (int, bool) override {}
        void handleAsyncUpdate() override;

        void applyPicks();

        std::unique_ptr<LicenseManager> license;
        std::vector<MicState> mixer;                        // только message thread
        std::map<juce::String, MicState> savedMix;          // по id микрофона, под stateLock: пишется в проект
        std::vector<Pick> picks;                            // только message thread, по индексу Kit::choices
        std::map<juce::String, Pick> savedPicks;            // по id переключателя, под stateLock: пишется в проект
        KitLoadResult lastLoad;                             // только message thread
        std::vector<float> pieceLevels;                     // дБ, только message thread
        std::map<juce::String, float> savedPieces;          // по id барабана, под stateLock
        std::array<juce::AudioParameterFloat*, kAutomatedMics> levelParams {}, panParams {};
        bool restoreMixPending = false;                     // только message thread
        std::atomic<bool> kitLoading { false };
        std::atomic<float> kitProgress { 0.0f };
        std::shared_ptr<std::atomic<bool>> kitCancel;       // только message thread
        uint32_t kitGeneration = 0;                         // только message thread

        // Пока жив токен, отложенные колбэки загрузки могут обращаться к процессору
        std::shared_ptr<int> lifeToken = std::make_shared<int> (0);
        juce::ThreadPool kitLoaderPool { juce::ThreadPoolOptions{}.withThreadName ("Samplit kit loader").withNumberOfThreads (1) };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplitProcessor)
    };
}
