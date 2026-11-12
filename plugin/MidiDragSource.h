#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

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

        // Тестовая фраза: 1 такт 4/4, 120 bpm; бочка C0 (24), малый D0 (26), хэт C2 (48), силы заданы явно
        static juce::MidiFile makeTestPhrase();
        static juce::File writeTempFile (const juce::MidiFile&, const juce::String& name);

    private:
        bool dragging = false;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiDragSource)
    };
}
