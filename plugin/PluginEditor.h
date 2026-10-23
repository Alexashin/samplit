#pragma once

#include "GroupPanel.h"
#include "KeyboardView.h"
#include "LicensePanel.h"
#include "LookAndFeel.h"
#include "MixerPanel.h"
#include "PickPanel.h"
#include "PluginProcessor.h"
#include "PresetPanel.h"
#include "SampleBrowser.h"
#include "ZoneEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace samplit
{
    // Окно плагина. Основной экран: пресеты и лицензия слева, микшер микрофонов и выбор удара справа (вкладки), клавиатура внизу.
    // Служебный экран (только в сборках с SAMPLIT_SERVICE_MODE): браузер сэмплов, группы и зоны для ручной раскладки.
    // DragAndDropContainer нужен, чтобы тащить сэмпл из списка на клавишу
    class SamplitEditor final : public juce::AudioProcessorEditor,
                                public juce::DragAndDropContainer,
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
        void showServiceScreen (bool);
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

        SampleBrowser browser;
        GroupPanel groups;
        ZoneEditor zones;
        KeyboardView keyboard;

        juce::TextButton serviceButton;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplitEditor)
    };
}
