#pragma once

#include "PadEngine.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace samplit
{
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

        // Состояние в проекте DAW: файлы на пэдах и последняя папка браузера
        void getStateInformation (juce::MemoryBlock&) override;
        void setStateInformation (const void*, int) override;

        // ---- Сэмплы. Всё ниже вызывается из message thread ----

        // Читает файл с диска. Медленно, поэтому вызывается из фонового потока. nullptr, если не аудио или длиннее 60 с
        static SamplePtr readSample (juce::AudioFormatManager&, const juce::File&);

        // Загружает файл на пэд в фоне. Пэд сразу запоминает файл, звук появится, когда загрузка закончится
        void loadPadSample (int pad, const juce::File&);
        void clearPad (int pad);

        // Ставит готовый сэмпл на голос (пэд или прослушивание). Старый сэмпл удаляется позже, когда точно не звучит
        void assignSample (int voice, SamplePtr);

        // Прослушивание из браузера: грузит и играет файл в Main. Новое прослушивание обрывает старое
        void previewFile (const juce::File&);
        void stopPreview() noexcept { pendingStops.fetch_or (1u << kPreviewVoice); }

        juce::File getPadFile (int pad) const;
        bool isPadLoaded (int pad) const { return owned[(size_t) pad] != nullptr; }
        SamplePtr getPadSample (int pad) const { return owned[(size_t) pad]; }

        juce::File getLastFolder() const;
        void setLastFolder (const juce::File&);

        juce::AudioFormatManager& getFormatManager() noexcept { return formats; }
        static juce::String getSupportedWildcard() { return "*.wav;*.aif;*.aiff;*.flac;*.ogg"; }

        // ---- UI ↔ audio thread ----

        void triggerPad (int pad) noexcept { pendingTriggers.fetch_or (1u << pad); }

        // Какие пэды сейчас звучат и какие идут на свой выход, а не в Main
        uint32_t getActiveMask() const noexcept { return activeMask.load(); }
        uint32_t getSeparateOutMask() const noexcept { return separateOutMask.load(); }

        // Для тестов и сборки мусора: сколько снятых с пэдов сэмплов ещё ждут удаления
        size_t getRetiredCount() const noexcept { return retired.size(); }
        void collectGarbage (bool force = false);

    private:
        static BusesProperties makeBuses();
        void timerCallback() override { collectGarbage(); }
        void startLoad (int voice, const juce::File&, bool playWhenReady);

        PadEngine engine;
        juce::AudioFormatManager formats;

        // Владение сэмплами: только message thread
        std::array<SamplePtr, kNumVoices> owned {};
        struct Retired { SamplePtr sample; juce::uint32 since; };
        std::vector<Retired> retired;

        // Файлы пэдов и папка браузера: читаются и из потока хоста при сохранении проекта
        mutable juce::CriticalSection stateLock;
        std::array<juce::File, kNumPads> padFiles;
        juce::File lastFolder;

        std::array<std::atomic<uint32_t>, kNumVoices> loadGeneration {};
        std::atomic<uint32_t> pendingTriggers { 0 };
        std::atomic<uint32_t> pendingStops { 0 };
        std::atomic<uint32_t> activeMask { 0 };
        std::atomic<uint32_t> separateOutMask { 0 };

        // Пока жив токен, отложенные колбэки загрузки могут обращаться к процессору
        std::shared_ptr<int> lifeToken = std::make_shared<int> (0);
        juce::ThreadPool loader { juce::ThreadPoolOptions{}.withThreadName ("Samplit loader").withNumberOfThreads (1) };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplitProcessor)
    };
}
