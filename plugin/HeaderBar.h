#pragma once

#include "PluginProcessor.h"
#include "Brand.h"
#include "ui/Fonts.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>

namespace samplit
{
    // Строка статуса внизу окна: одна строка про загрузку, звук и последнее действие, плюс меню «Установка ▾»
    // (папка звука, стандартная и заводская установка, файлы установки). Очередь сообщений: новые фоновые задачи
    // (сканирование, установщик) показываются здесь же
    class HeaderBar final : public juce::Component,
                            public juce::TooltipClient,
                            private juce::ChangeListener,
                            private juce::Timer
    {
    public:
        explicit HeaderBar (SamplitProcessor&);
        ~HeaderBar() override;

        // Перечитать состояние (после смены лицензии)
        void refresh() { updateStatus(); }

        // Ход загрузки в долях 0..1; -1: загрузки нет. Для полоски хода в шапке окна
        float getShownProgress() const noexcept { return shownProgress; }

        void paint (juce::Graphics&) override;
        void resized() override;

        void chooseLibrary();
        void showSetupMenu (juce::Component& target);

        // Над строкой статуса: её полный текст (в узком окне он обрезается)
        juce::String getTooltip() override;

    private:
        void addPack();
        void say (const juce::String& text, bool warns);
        void updateStatus();
        void changeListenerCallback (juce::ChangeBroadcaster*) override { updateStatus(); }
        void timerCallback() override;
        static juce::String healthWarning (const StreamHealth&);

        SamplitProcessor& processor;
        std::unique_ptr<juce::FileChooser> chooser;
        juce::Rectangle<int> statusArea;
        juce::String statusText;
        bool statusWarns = false;
        juce::String note;                  // ответ на действие пользователя: показывается, пока не сменится состояние загрузки
        bool noteWarns = false;
        float shownProgress = -1.0f;
        StreamHealth shownHealth;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (HeaderBar)
    };
}
