#pragma once

#include "MidiDragSource.h"
#include "PluginProcessor.h"
#include "ui/Controls.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace samplit
{
    // Мини-полоса партии под схемой установки: Играть/Стоп, Цикл, сколько тактов и главное действие окна
    // «Перетащить в Cubase». Редактирование партии на странице «Партия»; здесь только запуск и вынос в DAW
    class PatternStrip final : public juce::Component,
                               private juce::Timer
    {
    public:
        explicit PatternStrip (SamplitProcessor&);
        ~PatternStrip() override { stopTimer(); }

        MidiDragSource& getDragSource() noexcept { return drag; }
        void setTransportVisible (bool visible);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void timerCallback() override;
        void refreshState();

        SamplitProcessor& processor;
        juce::TextButton playButton;
        LedButton loopButton { utf8 ("Цикл") };
        MidiDragSource drag;
        bool transportVisible = true;
        juce::String barsText;
    };
}
