#pragma once

#include "DiskStreamer.h"
#include "KitEngine.h"
#include "KitLoader.h"
#include "LicenseManager.h"
#include "PackCatalog.h"
#include "Setup.h"

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
                                   private juce::ChangeListener,       // смена лицензии (только при включённой лицензии)
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

        // Состояние в проекте DAW (version 3): установка (Setup, JSON в свойстве setup) и микшер микрофонов (узлы MIC).
        // Проекты version 2 мигрируют: factory-установка, малый по id пресета, MIC переносятся, PICK и микс отбрасываются
        void getStateInformation (juce::MemoryBlock&) override;
        void setStateInformation (const void*, int) override;

        // ---- Установка Studio600 (docs/setup-spec.md): места, паки, громкости, режимы выбора ударов. Только message thread ----

        Setup getSetup() const;
        // Применить установку целиком: громкости сразу, состав кита фоновой загрузкой (старый кит играет до публикации нового)
        void setSetup (const Setup&);
        void setPlaceEnabled (const juce::String& placeId, bool enabled);
        // pieceId пуст: единственный барабан нужного типа. Смена пака сбрасывает выбор ударов места
        void setPlacePack (const juce::String& placeId, const juce::String& packId, const juce::String& pieceId = {});
        // Только громкость: атомик движка и запись в установку, без перезагрузки кита
        void setPlaceLevel (const juce::String& placeId, float levelDb);
        // choiceId "<placeId>:<groupId>" (как Kit::choices[].id): движку сразу, в установку для следующих загрузок
        void setGroupPick (const juce::String& choiceId, Pick);
        // Каталог паков последней применённой установки (для списков в интерфейсе)
        std::vector<CatalogPack> getCatalog() const;
        // Сбросить кэш каталога и применить установку заново
        void rescanCatalog();
        // Применить текущую установку (например, после старта или смены источника библиотеки)
        void applySetup (std::function<void (const KitLoadResult&)> done = {});

        // Стандартная установка пользователя: <данные Samplit>/default-setup.json
        bool saveAsDefaultSetup();          // false: файл не записан
        void resetToDefaultSetup();         // файл, при отсутствии или ошибке заводская
        void restoreFactorySetup();         // удаляет файл стандартной установки и применяет заводскую
        juce::File getDataDir() const { return dataDir; }
        // Для тестов: папка данных вместо папки лицензии (действует для новых экземпляров); пустой File: снять подмену
        static void setDataDirOverride (const juce::File&);
        // SAMPLIT_SERVICE=1 при создании процессора: в каталоге и составе кита есть непроверенные паки и слои
        bool isServiceMode() const noexcept { return serviceMode; }

        // ---- Кит (мультимикрофонные барабаны) ----

        // Задаёт кит: MIDI и ноты с экранной клавиатуры идут в него. nullptr: тишина.
        // Кит должен быть собран (Kit::buildIndex()) и после публикации не меняется. Старый кит живёт, пока из него звучит удар
        void setKit (std::shared_ptr<const Kit>);
        std::shared_ptr<const Kit> getKit() const;

        // Фоновая загрузка кита по составу (KitSpec) с заданными источниками. Новая загрузка отменяет прежнюю. По готовности
        // (message thread) кит ставится через setKit(), микрофонам выставляются уровни и панорамы, потом зовётся done.
        // При ошибке или отмене прежний кит остаётся играть. Звук не блокируется. Основной путь: applySetup()
        void loadKitAsync (const KitSpec&, KitLoadOptions, std::function<void (const KitLoadResult&)> done = {});
        void cancelKitLoad();

        LicenseManager& getLicense() noexcept { return *license; }
        // Для тестов: подмена менеджера лицензий (другая папка данных, ключ, часы)
        void setLicenseManager (std::unique_ptr<LicenseManager>);
        bool isKitLoading() const noexcept { return kitLoading.load(); }
        float getKitLoadProgress() const noexcept { return kitProgress.load(); }

        // Итог последней загрузки (без самого кита): ошибка, недоступные пакеты, предупреждения
        const KitLoadResult& getLastKitLoad() const noexcept { return lastLoad; }

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

        // ---- Автоматизация в DAW: уровень и панорама первых kAutomatedMics микрофонов кита (параметры "micNN_level", "micNN_pan").
        // Хост двигает параметр, микшер следует за ним, и наоборот. Mute, Solo и выход не автоматизируются ----
        static constexpr int kAutomatedMics = Kit::kMaxMics;

        // ---- Выбор удара (message thread): переключатели Kit::choices, сохраняются в проекте по id ----
        Pick getPickChoice (int choice) const;
        void setPickChoice (int choice, Pick);

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

        // Message thread: сводка здоровья звука (обрывы, вытеснения, ошибки чтения) и её сброс; сброс идёт при смене кита
        StreamHealth getHealth() const { return kitEngine.getHealth(); }
        void resetHealth() { kitEngine.resetHealth(); }
        // Message thread: удаляет списанные киты, которые audio thread больше не использует (по факту, без ожидания по времени)
        void collectGarbage();

    private:
        static BusesProperties makeBuses();
        void timerCallback() override;

        DiskStreamer streamer;      // раньше kitEngine: переживает его при разрушении
        KitEngine kitEngine;
        struct Retired { std::shared_ptr<const Kit> kit; };
        std::vector<Retired> retired;

        // Состояние проекта читается и из потока хоста при сохранении проекта
        mutable juce::CriticalSection stateLock;
        std::shared_ptr<const Kit> kit;

        // Очередь нот из UI в audio thread: пишет только message thread, читает только audio thread
        static constexpr int kQueue = 64;
        std::array<std::atomic<uint32_t>, kQueue> queue {};     // note | velocity << 8
        std::atomic<uint32_t> queueWrite { 0 };
        std::atomic<uint32_t> queueRead { 0 };
        std::atomic<uint64_t> activeLo { 0 }, activeHi { 0 };
        std::atomic<uint32_t> enabledOutputs { 0 };
        void applyMixer();
        void pushParam (int mic);
        void pushAllParams();
        // Может вызываться из audio thread хоста: только флаг (triggerAsyncUpdate берёт мьютекс и аллоцирует).
        // Флаг разбирает timerCallback на message thread
        void parameterValueChanged (int, float) override { paramsDirty.store (true, std::memory_order_release); }
        std::atomic<bool> paramsDirty { false };
        int garbageTicks = 0;       // только message thread: уборка китов раз в секунду
        void parameterGestureChanged (int, bool) override {}
        void applyHostParams();
        void changeListenerCallback (juce::ChangeBroadcaster*) override;

        void applyPicks();

        // Общая фоновая загрузка: work выполняется в потоке пула, результат публикуется в message thread.
        // Новая загрузка отменяет прежнюю
        void startLoad (std::function<KitLoadResult (const KitLoadOptions&)> work, KitLoadOptions,
                        std::shared_ptr<std::vector<CatalogPack>> catalogOut, std::function<void (const KitLoadResult&)> done);
        KitLoadOptions makeLoadOptions() const;
        std::vector<CatalogPack> scanCatalogCached (const KitLoadOptions&, bool includeUnverified);
        void pushPlaceLevels (const Setup&);
        void applySetupSoon();      // с любого потока: применить установку в message thread
        juce::File defaultSetupFile() const { return dataDir.getChildFile ("default-setup.json"); }
        Setup loadDefaultSetup() const;

        std::unique_ptr<LicenseManager> license;
        std::vector<MicState> mixer;                        // только message thread
        std::map<juce::String, MicState> savedMix;          // по id микрофона, под stateLock: пишется в проект
        std::vector<Pick> picks;                            // только message thread, по индексу Kit::choices
        KitLoadResult lastLoad;                             // только message thread
        Setup setup;                                        // под stateLock: пишется в проект
        std::vector<CatalogPack> catalog;                   // только message thread: каталог последней загрузки установки
        juce::File dataDir;
        bool serviceMode = false;
        std::atomic<bool> initialApplyPending { true };      // установка ещё ни разу не применялась (пишет и поток хоста)
        bool setupRetryPending = false;                     // только message thread: пакеты не нашлись до проверки лицензии

        // Кэш каталога: пишет поток загрузки, сбрасывает message thread (epoch)
        juce::CriticalSection catalogLock;
        std::vector<CatalogPack> catalogCache;
        juce::String catalogCacheKey;
        uint32_t catalogCacheEpoch = 0;
        bool catalogCacheValid = false;
        std::atomic<uint32_t> catalogEpoch { 1 };
        std::array<juce::AudioParameterFloat*, kAutomatedMics> levelParams {}, panParams {};
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
