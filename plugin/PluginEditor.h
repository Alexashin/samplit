#pragma once

#include "GroupPanel.h"
#include "KeyboardView.h"
#include "LookAndFeel.h"
#include "PluginProcessor.h"
#include "SampleBrowser.h"
#include "ZoneEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace samplit
{
    // Окно плагина: браузер сэмплов слева, группы и зоны справа, клавиатура внизу.
    // DragAndDropContainer нужен, чтобы тащить сэмпл из списка на клавишу
    class SamplitEditor final : public juce::AudioProcessorEditor,
                                public juce::DragAndDropContainer
    {
    public:
        explicit SamplitEditor (SamplitProcessor&);
        ~SamplitEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        SamplitProcessor& processor;
        SamplitLookAndFeel lookAndFeel;
        std::unique_ptr<juce::Drawable> logo;
        juce::Rectangle<int> logoArea;

        SampleBrowser browser;
        GroupPanel groups;
        ZoneEditor zones;
        KeyboardView keyboard;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplitEditor)
    };
}
