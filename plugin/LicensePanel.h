#pragma once

#include "LicenseManager.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace samplit
{
    // Состояние лицензии: до какого числа действует, что делать, если не действует. Кнопка «Обновить»
    // продлевает токен (или просто перечитывает его, если сервер не задан), поле с кодом нужно для первой активации
    class LicensePanel final : public juce::Component,
                               private juce::Timer
    {
    public:
        explicit LicensePanel (LicenseManager&);

        // Перечитать состояние из менеджера (зовётся из LicenseManager::onChange)
        void refresh();

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void timerCallback() override;

        LicenseManager& license;
        LicenseManager::Info info;

        juce::Label title;
        juce::TextButton renewButton, activateButton;
        juce::TextEditor code;
        juce::Rectangle<int> textArea;
        juce::String statusText, hintText;
        bool ok = false;
        bool busyShown = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LicensePanel)
    };
}
