#pragma once

#include "HeaderBar.h"
#include "KeyboardView.h"
#include "KitView.h"
#include "LicensePanel.h"
#include "LookAndFeel.h"
#include "MixerPanel.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace samplit
{
    // Окно плагина: шапка (библиотека, состояние, стандартная установка, лицензия), схема установки или микшер
    // микрофонов, клавиатура внизу. Лицензия открывается поверх окна по кнопке шапки
    class SamplitEditor final : public juce::AudioProcessorEditor,
                                private juce::FocusChangeListener,
                                private juce::Timer
    {
    public:
        explicit SamplitEditor (SamplitProcessor&);
        ~SamplitEditor() override;

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

        // Основной экран: схема установки (false) или микшер микрофонов (true)
        void showMainTab (bool mixerShown);

        // Выпадающие панели поверх окна (кнопки шапки, снимки окна)
        void openLicense() { if (license != nullptr) showOverlay (Overlay::License); }

    private:
        // Невидимая подложка под выпадающими панелями: клик мимо закрывает их
        struct Scrim final : juce::Component
        {
            void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
            std::function<void()> onClick;
        };

        enum class Overlay { None, License };

        void globalFocusChanged (juce::Component*) override;
        void timerCallback() override;
        void showOverlay (Overlay);
        void updateHeader();

        juce::Component::SafePointer<juce::Component> lastFocused;

        SamplitProcessor& processor;
        SamplitLookAndFeel lookAndFeel;
        juce::TooltipWindow tooltips { this, 700 };
        std::unique_ptr<juce::Drawable> logo;
        juce::Rectangle<int> logoArea, headerArea;

        HeaderBar headerBar;
        std::unique_ptr<LicensePanel> license;     // nullptr, пока лицензия отложена
        MixerPanel mixer;
        KitView kitView;
        juce::TextButton kitTab, mixerTab;
        juce::TextButton licenseButton;
        Scrim scrim;
        Overlay overlay = Overlay::None;
        float shownProgress = -1.0f;

        KeyboardView keyboard;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplitEditor)
    };
}
