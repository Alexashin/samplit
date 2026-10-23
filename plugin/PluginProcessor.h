#pragma once

#include "Instrument.h"
#include "KitEngine.h"
#include "KitLoader.h"
#include "LicenseManager.h"
#include "PresetLibrary.h"
#include "SamplerEngine.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <set>
#include <vector>

namespace samplit
{
    // Как разложить несколько файлов на одну клавишу
    enum class Spread
    {
        Variations,     // все на весь диапазон velocity, группа выбирает вариацию (random / cycle)
        VelocityLayers  // диапазон velocity делится поровну между файлами по порядку имён
    };

    class SamplitProcessor final : public juce::AudioProcessor,
                                   public juce::ChangeBroadcaster,
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

        // Состояние в проекте DAW: группы, зоны, последняя папка браузера
        void getStateInformation (juce::MemoryBlock&) override;
        void setStateInformation (const void*, int) override;

        // Читает файл с диска. Медленно, поэтому вызывается из фонового потока. nullptr, если не аудио или длиннее 60 с
        static SamplePtr readSample (juce::AudioFormatManager&, const juce::File&);

        // ---- Инструмент. Всё ниже вызывается из message thread; после изменения шлётся change message ----

        // Текущий снимок: группы и зоны. Не меняется, можно держать и читать сколько угодно
        std::shared_ptr<const Instrument> getInstrument() const;

        // Добавляет группу и возвращает её индекс (-1, если достигнут kMaxGroups). Выход по умолчанию: Out (индекс+1)
        int addGroup (const juce::String& name);
        void removeGroup (int group);                // вместе с зонами
        void setGroup (int group, const Group&);

        // Добавляет файлы на клавишу как зоны группы. Возвращает число добавленных зон
        int addFilesToKey (const juce::Array<juce::File>& files, int group, int key, Spread spread);

        // Папка как кит: файлы по порядку имён ложатся на клавиши подряд начиная с firstKey, по одному на клавишу
        int addFolderAsKit (const juce::File& folder, int group, int firstKey);

        void removeZone (int zone);
        void setZone (int zone, const Zone&);        // sample из аргумента игнорируется
        void clearKey (int group, int key);          // убирает зоны группы, покрывающие только эту клавишу

        // ---- Кит (мультимикрофонные барабаны) ----

        // Задаёт кит: MIDI и ноты с экранной клавиатуры идут в него, а не в сэмплер. nullptr возвращает сэмплер.
        // Кит должен быть собран (Kit::buildIndex()) и после публикации не меняется. Старый кит живёт, пока из него звучит удар
        void setKit (std::shared_ptr<const Kit>);
        std::shared_ptr<const Kit> getKit() const;

        // Уровень (дБ) и панорама микрофона кита. Панорама слышна, когда микрофон идёт в Main
        void setKitMicMix (int mic, float levelDb, float pan) noexcept { kitEngine.setMicMix (mic, levelDb, pan); }
        void setKitSelectMode (HitSelect mode) noexcept { kitEngine.setSelectMode (mode); }
        void setKitSpread (int layers) noexcept { kitEngine.setSpread (layers); }

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

        // ---- Выбор удара (message thread): переключатели Kit::choices, сохраняются в проекте по id ----
        Pick getPickChoice (int choice) const;
        void setPickChoice (int choice, Pick);

        // Служебный режим: играет сэмплер с ручной раскладкой, кит молчит. В релизе переключателя нет
        void setServiceMode (bool);
        bool isServiceMode() const noexcept { return serviceMode.load(); }

        // Имя пресета в состоянии проекта (сам пресет подгружает загрузчик пакетов)
        juce::String getPresetName() const;
        void setPresetName (const juce::String&);

        // Прослушивание из браузера: грузит и играет файл в Main. Новое прослушивание обрывает старое
        void previewFile (const juce::File&);
        void stopPreview() noexcept { pendingPreviewStop.store (true); }

        juce::File getLastFolder() const;
        void setLastFolder (const juce::File&);

        juce::AudioFormatManager& getFormatManager() noexcept { return formats; }
        static juce::String getSupportedWildcard() { return "*.wav;*.aif;*.aiff;*.flac;*.ogg"; }

        // ---- UI ↔ audio thread ----

        // Сыграть клавишу из интерфейса (velocity 1..127)
        void triggerNote (int note, int velocity = 100) noexcept;

        // Какие клавиши звучат (биты 0..127 в двух словах)
        std::array<uint64_t, 2> getActiveKeys() const noexcept { return { activeLo.load(), activeHi.load() }; }

        // Какие моно-выходы 1..kNumOutputs включены в хосте (бит N-1). Группа или микрофон на выключенный выход играет в Main
        uint32_t getEnabledOutputMask() const noexcept { return enabledOutputs.load(); }

        // Для тестов и сборки мусора: сколько снимков и сэмплов ещё ждут удаления
        size_t getRetiredCount() const noexcept { return retired.size(); }
        void collectGarbage (bool force = false);

        // Сколько файлов ещё читается в фоне
        int getNumLoading() const noexcept { return loadsInFlight.load(); }

    private:
        static BusesProperties makeBuses();
        void timerCallback() override { collectGarbage(); }

        // Модель меняется только в message thread. publish() собирает снимок с загруженными сэмплами и отдаёт движку
        void publish();
        void requestLoad (const juce::File&);
        void startPreviewWith (SamplePtr);
        static juce::Array<juce::File> sortedAudioFiles (const juce::Array<juce::File>&);

        SamplerEngine engine;
        KitEngine kitEngine;
        juce::AudioFormatManager formats;

        // Модель и кэш сэмплов: только message thread
        Instrument model;
        std::map<juce::String, SamplePtr> cache;
        std::set<juce::String> loading, failed;

        struct Retired { std::shared_ptr<const Instrument> snapshot; SamplePtr preview; std::shared_ptr<const Kit> kit; juce::uint32 since; };
        std::vector<Retired> retired;

        // Опубликованный снимок читается и из потока хоста при сохранении проекта
        mutable juce::CriticalSection stateLock;
        std::shared_ptr<const Instrument> published;
        std::shared_ptr<const Kit> kit;
        juce::String presetName;
        juce::File lastFolder;
        SamplePtr previewSample;
        juce::File previewFileLoaded;

        // Очередь нот из UI в audio thread: пишет только message thread, читает только audio thread
        static constexpr int kQueue = 64;
        std::array<std::atomic<uint32_t>, kQueue> queue {};     // note | velocity << 8
        std::atomic<uint32_t> queueWrite { 0 };
        std::atomic<uint32_t> queueRead { 0 };
        std::atomic<bool> kitActive { false };
        std::atomic<uint32_t> previewGeneration { 0 };
        std::atomic<bool> pendingPreview { false };
        std::atomic<bool> pendingPreviewStop { false };
        std::atomic<uint64_t> activeLo { 0 }, activeHi { 0 };
        std::atomic<uint32_t> enabledOutputs { 0 };
        std::atomic<int> loadsInFlight { 0 };
        void applyMixer();
        void applyPicks();

        std::unique_ptr<LicenseManager> license;
        std::vector<MicState> mixer;                        // только message thread
        std::map<juce::String, MicState> savedMix;          // по id микрофона, под stateLock: пишется в проект
        std::vector<Pick> picks;                            // только message thread, по индексу Kit::choices
        std::map<juce::String, Pick> savedPicks;            // по id переключателя, под stateLock: пишется в проект
        KitLoadResult lastLoad;                             // только message thread
        bool restoreMixPending = false;                     // только message thread
        std::atomic<bool> serviceMode { false };
        std::atomic<bool> kitLoading { false };
        std::atomic<float> kitProgress { 0.0f };
        std::shared_ptr<std::atomic<bool>> kitCancel;       // только message thread
        uint32_t kitGeneration = 0;                         // только message thread

        // Пока жив токен, отложенные колбэки загрузки могут обращаться к процессору
        std::shared_ptr<int> lifeToken = std::make_shared<int> (0);
        juce::ThreadPool kitLoaderPool { juce::ThreadPoolOptions{}.withThreadName ("Samplit kit loader").withNumberOfThreads (1) };
        juce::ThreadPool loader { juce::ThreadPoolOptions{}.withThreadName ("Samplit loader").withNumberOfThreads (2) };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplitProcessor)
    };
}
