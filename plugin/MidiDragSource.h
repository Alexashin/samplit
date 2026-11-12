#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace samplit
{
    // Пробная «плашка» для перетаскивания MIDI-фразы в DAW: при перетаскивании пишет временный .mid
    // с явной velocity и отдаёт его системе как внешний файл (Cubase кладёт на дорожку).
    class MidiDragSource final : public juce::Component,
                                   public juce::SettableTooltipClient
    {
    public:
        MidiDragSource();

        void paint (juce::Graphics&) override;
        void mouseDrag (const juce::MouseEvent&) override;

        // Откуда брать файл: партия из процессора. Пока `ready` возвращает false, плашка неактивна
        std::function<juce::MidiFile()> makeFile;
        std::function<juce::String()> fileName;
        std::function<bool()> ready;

        void refreshState();        // вызывать из таймера: перерисовать, если готовность изменилась
        static juce::File writeTempFile (const juce::MidiFile&, const juce::String& name);

    private:
        bool dragging = false;
        bool lastReady = false;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiDragSource)
    };
}
