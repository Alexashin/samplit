#pragma once

#include "KeyboardView.h"
#include "LicensePanel.h"
#include "LookAndFeel.h"
#include "MixerPanel.h"
#include "PickPanel.h"
#include "PluginProcessor.h"
#include "PresetPanel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace samplit
{
    // Окно плагина: пресеты и лицензия слева, микшер микрофонов и выбор удара справа (вкладки), клавиатура внизу
    class SamplitEditor final : public juce::AudioProcessorEditor,
                                private juce::FocusChangeListener
    {
    public:
        explicit SamplitEditor (SamplitProcessor&);
        ~SamplitEditor() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        // Правая вкладка основного экрана: микшер микрофонов или выбор удара
        void showRightTab (bool picks);

    private:
        void globalFocusChanged (juce::Component*) override;

        juce::Component::SafePointer<juce::Component> lastFocused;

        SamplitProcessor& processor;
        SamplitLookAndFeel lookAndFeel;
        juce::TooltipWindow tooltips { this, 700 };
        std::unique_ptr<juce::Drawable> logo;
        juce::Rectangle<int> logoArea;

        PresetPanel presets;
        LicensePanel license;
        MixerPanel mixer;
        PickPanel picks;
        juce::TextButton mixerTab, picksTab;

        KeyboardView keyboard;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplitEditor)
    };
}
