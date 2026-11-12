#pragma once

#include "HeaderBar.h"
#include "InstrumentsPanel.h"
#include "KeyboardView.h"
#include "MidiDragSource.h"
#include "KitView.h"
#include "LicensePanel.h"
#include "LookAndFeel.h"
#include "MixerPanel.h"
#include "PatternPanel.h"
#include "PluginProcessor.h"
#include "ui/Controls.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace samplit
{
    // Окно плагина (docs/ui-guidelines.md): шапка (логотип, «Установка ▾», страницы, лицензия, ⚙, ?), страница,
    // полоса партии, сворачиваемая клавиатура и строка статуса. Страницы заведены списком: новая страница
    // (например «Звуки» для поиска) добавляется одной строкой в конструкторе. Лицензия открывается поверх окна
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

        // Страницы окна. Instruments пока отдельная страница, до переноса в панель барабана (Р4)
        enum class MainTab { Kit, Instruments, Pattern, Mixer };
        void showMainTab (MainTab);

        void selectInstrument (const juce::String& placeId) { instruments.selectPlace (placeId); }

        // Выпадающие панели поверх окна (кнопки шапки, снимки окна)
        void openLicense() { if (license != nullptr) showOverlay (Overlay::License); }

        // Размер окна: компактный 920×560, стандартный 1120×640, большой 1400×800 (с ограничением рабочей областью экрана)
        enum class WindowSize { Compact, Standard, Expanded };
        void setWindowSize (WindowSize);
        void setKeyboardOpen (bool);

    private:
        // Невидимая подложка под выпадающими панелями: клик мимо закрывает их
        struct Scrim final : juce::Component
        {
            void mouseDown (const juce::MouseEvent&) override { if (onClick) onClick(); }
            std::function<void()> onClick;
        };

        // Первый запуск: папка со звуком не выбрана, кита нет. Закрывает страницы понятным приглашением вместо пустых панелей
        struct FirstRunPanel final : juce::Component
        {
            FirstRunPanel();
            void paint (juce::Graphics&) override;
            void resized() override;
            juce::TextButton chooseButton;
        };

        struct Page
        {
            juce::String name;
            juce::Component* content;
            bool hasFooter;     // под страницей полоса партии (перетаскивание в DAW)
        };

        enum class Overlay { None, License };

        void globalFocusChanged (juce::Component*) override;
        void timerCallback() override;
        void showOverlay (Overlay);
        void showPage (int index);
        void updateHeader();
        void showSettingsMenu();
        void showHelp();

        juce::Component::SafePointer<juce::Component> lastFocused;

        SamplitProcessor& processor;
        SamplitLookAndFeel lookAndFeel;
        juce::TooltipWindow tooltips { this, 700 };
        std::unique_ptr<juce::Drawable> logo;
        juce::Rectangle<int> logoArea, headerArea, statusArea;

        HeaderBar headerBar;                       // строка статуса внизу, меню «Установка ▾»
        std::unique_ptr<LicensePanel> license;     // nullptr, пока лицензия отложена
        MixerPanel mixer;
        InstrumentsPanel instruments;
        KitView kitView;
        FirstRunPanel firstRun;
        PatternPanel patternPanel;
        std::vector<Page> pages;
        int currentPage = 0;

        juce::TextButton setupButton, keyboardToggle;
        SegmentedControl pageTabs { { } };
        IconButton settingsButton { IconButton::Icon::Gear, utf8 ("Настройки: как выбирается удар, размер окна") };
        IconButton helpButton { IconButton::Icon::Help, utf8 ("Как пользоваться") };
        juce::TextButton licenseButton;
        MidiDragSource phraseSource;
        Scrim scrim;
        Overlay overlay = Overlay::None;
        float shownProgress = -1.0f;
        bool trackSize = false;                    // запоминать размер окна только после первой раскладки

        KeyboardView keyboard;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SamplitEditor)
    };
}
