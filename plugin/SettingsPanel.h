#pragma once

#include "PluginProcessor.h"
#include "ui/Controls.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace samplit
{
    // Окно «Настройки» (⚙): поведение плагина, которое меняют редко. Раскладка строками «название, пояснение, контроль»;
    // новый раздел добавляется строкой в конструктор и в resized(). Открывается поверх окна под шапкой, как лицензия
    class SettingsPanel final : public juce::Component
    {
    public:
        explicit SettingsPanel (SamplitProcessor&);

        void refresh();          // перечитать состояние процессора (при открытии)
        static int preferredHeight() { return 490; }

        // Действия, которыми владеет окно плагина
        std::function<void (int)> onWindowSize;         // 0 компактный, 1 стандартный, 2 большой
        std::function<void()> onChooseFolder;
        std::function<void()> onRescan;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        SamplitProcessor& processor;
        SegmentedControl strength { { utf8 ("Сила"), utf8 ("Номер записи") } };
        SegmentedControl pick { { utf8 ("Повторяемо"), utf8 ("Живой выбор") } };
        SegmentedControl windowSize { { utf8 ("Компактный"), utf8 ("Стандартный"), utf8 ("Большой") } };
        juce::TextButton folderButton, rescanButton;
        juce::Rectangle<int> rowArea[5];
    };
}
