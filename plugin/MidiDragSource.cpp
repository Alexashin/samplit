#include "MidiDragSource.h"
#include "Brand.h"
#include "ui/Fonts.h"

namespace samplit
{
    MidiDragSource::MidiDragSource()
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
        setTooltip (utf8 ("Партия: перетащите на MIDI-дорожку Cubase. Ноты придут с силой и сдвигами, как их играет плагин"));
    }

    void MidiDragSource::paint (juce::Graphics& g)
    {
        // Главное действие окна: единственная заливка акцентом на странице. Неактивна, пока партия пуста
        const auto r = getLocalBounds().toFloat();
        g.setColour (lastReady ? brand::accent : brand::pad);
        g.fillRoundedRectangle (r, brand::radiusM);
        if (! lastReady)
        {
            g.setColour (brand::lineStrong);
            g.drawRoundedRectangle (r.reduced (0.5f), brand::radiusM, 1.0f);
        }
        const auto ink = lastReady ? brand::onAccent : brand::textFaint;
        // Значок: стрелка вниз к линии
        const auto c = juce::Point<float> (20.0f, r.getCentreY());
        g.setColour (ink);
        g.drawLine (c.x, c.y - 7.0f, c.x, c.y + 3.0f, 2.0f);
        juce::Path head;
        head.addTriangle (c.x - 5.0f, c.y - 1.0f, c.x + 5.0f, c.y - 1.0f, c.x, c.y + 6.0f);
        g.fillPath (head);
        g.fillRect (c.x - 7.0f, c.y + 8.0f, 14.0f, 2.0f);
        g.setFont (fonts::strong (13.0f));
        g.drawText (utf8 ("Перетащить в Cubase"), getLocalBounds().withTrimmedLeft (38).withTrimmedRight (8), juce::Justification::centredLeft, true);
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
