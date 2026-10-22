#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace samplit
{
    // Панель под пэдами: настройки выбранного пэда. Двойной клик по ручке возвращает значение по умолчанию
    class PadSettings final : public juce::Component,
                              private juce::ChangeListener
    {
    public:
        explicit PadSettings (SamplitProcessor&);
        ~PadSettings() override;

        void setPad (int pad);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        struct Knob
        {
            juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxBelow };
            juce::Label label;
        };

        void setupKnob (Knob&, const juce::String& name, double min, double max, double step, double def,
                        std::function<juce::String (double)> format);
        void pushToProcessor();
        void pullFromProcessor();
        void changeListenerCallback (juce::ChangeBroadcaster*) override;

        SamplitProcessor& processor;
        int pad = 0;
        Knob gain, pitch, pan;
        juce::Rectangle<int> titleArea;
        bool pulling = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadSettings)
    };
}
