#include "MidiDragSource.h"
#include "Brand.h"

namespace samplit
{
    MidiDragSource::MidiDragSource()
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip (utf8 ("Партия: перетащите на MIDI-дорожку Cubase. Ноты придут с силой и сдвигами, как их играет плагин"));
    }

    void MidiDragSource::paint (juce::Graphics& g)
    {
        const auto r = getLocalBounds().toFloat().reduced (0.5f);
        g.setColour (brand::line);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        g.setColour (lastReady ? brand::textMuted : brand::textMuted.withAlpha (0.4f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (utf8 ("Партия → Cubase"), getLocalBounds(), juce::Justification::centred);
    }

    void MidiDragSource::refreshState()
    {
        const bool r = ready && ready();
        if (r == lastReady)
            return;
        lastReady = r;
        setMouseCursor (r ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
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
        if (dragging || ! lastReady || ! makeFile || e.getDistanceFromDragStart() < 6)
            return;
        dragging = true;
        const auto file = writeTempFile (makeFile(), fileName ? fileName() : juce::String ("Samplit-pattern.mid"));
        if (file.existsAsFile())
            juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                         [this] { dragging = false; });
        else
            dragging = false;
    }
}
