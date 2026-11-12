#include "MidiDragSource.h"
#include "Brand.h"

namespace samplit
{
    namespace
    {
        constexpr int kPpq = 480;
    }

    MidiDragSource::MidiDragSource()
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip (utf8 ("Пробная фраза: перетащите на MIDI-дорожку Cubase. Ноты придут с заданной силой удара"));
    }

    void MidiDragSource::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (brand::line);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        g.setColour (brand::textMuted);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (utf8 ("Фраза → в Cubase (перетащить)"), getLocalBounds(), juce::Justification::centred);
    }

    juce::MidiFile MidiDragSource::makeTestPhrase()
    {
        juce::MidiMessageSequence seq;
        const auto add = [&] (int note, int beat16, int vel)
        {
            const double t = (double) beat16 * kPpq / 4.0;
            seq.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) vel), t);
            seq.addEvent (juce::MidiMessage::noteOff (10, note), t + kPpq / 8.0);
        };
        for (int i = 0; i < 8; ++i)                  // хэт восьмыми, сила чередуется
            add (48, i * 2, i % 2 == 0 ? 120 : 112);
        add (24, 0, 120); add (24, 8, 111);          // бочка на 1 и 3
        add (26, 4, 120); add (26, 12, 118);         // малый на 2 и 4

        seq.updateMatchedPairs();
        juce::MidiFile file;
        file.setTicksPerQuarterNote (kPpq);
        file.addTrack (seq);
        return file;
    }

    juce::File MidiDragSource::writeTempFile (const juce::MidiFile& midi, const juce::String& name)
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Samplit");
        dir.createDirectory();
        const auto file = dir.getChildFile (name);
        file.deleteFile();
        if (juce::FileOutputStream out (file); out.openedOk())
            midi.writeTo (out);
        return file;
    }

    void MidiDragSource::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragging || e.getDistanceFromDragStart() < 6)
            return;
        dragging = true;
        const auto file = writeTempFile (makeTestPhrase(), "Samplit-phrase.mid");
        if (file.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                         [this] { dragging = false; });
        else
            dragging = false;
    }
}
