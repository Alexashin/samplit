#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace samplit
{
    class SamplitEditor final : public juce::AudioProcessorEditor,
                                private juce::Timer
    {
    public:
        explicit SamplitEditor (SamplitProcessor&);
        ~SamplitEditor() override = default;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        void timerCallback() override;
        juce::Rectangle<float> padBounds (int pad) const;
        int padAt (juce::Point<float>) const;

        SamplitProcessor& processor;
        std::unique_ptr<juce::Drawable> logo;
        juce::Rectangle<float> logoBounds, gridBounds;
        uint32_t shownActive = 0, shownSeparate = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplitEditor)
    };
}
