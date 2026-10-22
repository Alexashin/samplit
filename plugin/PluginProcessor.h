#pragma once

#include "PadEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

namespace samplit
{
    class SamplitProcessor final : public juce::AudioProcessor
    {
    public:
        SamplitProcessor();
        ~SamplitProcessor() override = default;

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
        double getTailLengthSeconds() const override { return 0.5; }

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock&) override {}
        void setStateInformation (const void*, int) override {}

        // Вызывается из UI. Пэд сыграет в начале следующего блока.
        void triggerPad (int pad) noexcept { pendingTriggers.fetch_or (1u << pad); }

        // Состояние для UI: какие пэды сейчас звучат и какие идут на свой выход, а не в Main
        uint32_t getActiveMask() const noexcept { return activeMask.load(); }
        uint32_t getSeparateOutMask() const noexcept { return separateOutMask.load(); }

    private:
        static BusesProperties makeBuses();

        PadEngine engine;
        std::atomic<uint32_t> pendingTriggers { 0 };
        std::atomic<uint32_t> activeMask { 0 };
        std::atomic<uint32_t> separateOutMask { 0 };

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplitProcessor)
    };
}
