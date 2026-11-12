#include "PatternStrip.h"

namespace samplit
{
    PatternStrip::PatternStrip (SamplitProcessor& p) : processor (p)
    {
        playButton.setTooltip (utf8 ("Сыграть партию с начала или остановить"));
        playButton.onClick = [this]
        {
            if (processor.isPatternPlaying())
                processor.stopPattern();
            else
                processor.playPattern();
            refreshState();
        };
        loopButton.setTooltip (utf8 ("Включено: партия повторяется по кругу. Выключено: играет один раз"));
        loopButton.onClick = [this]
        {
            processor.getPatternPlayer().setLoop (loopButton.getToggleState());
        };
        for (auto* c : std::initializer_list<juce::Component*> { &playButton, &loopButton, &drag })
            addAndMakeVisible (c);

        drag.makeFile = [this] { return processor.getPattern().toMidiFile(); };
        drag.ready = [this] { return ! processor.getPattern().steps.empty(); };
        drag.fileName = [this]
        {
            const auto pat = processor.getPattern();
            return "Samplit-" + juce::String (pat.bars) + "t-" + juce::String (juce::roundToInt (pat.bpm)) + "bpm.mid";
        };
        refreshState();
        startTimerHz (10);
    }

    void PatternStrip::setTransportVisible (bool visible)
    {
        transportVisible = visible;
        for (auto* c : std::initializer_list<juce::Component*> { &playButton, &loopButton })
            c->setVisible (visible);
        resized();
        repaint();
    }

    void PatternStrip::refreshState()
    {
        const bool playing = processor.isPatternPlaying();
        const auto label = playing ? utf8 ("Стоп") : utf8 ("Играть");
        if (playButton.getButtonText() != label)
            playButton.setButtonText (label);
        playButton.setToggleState (playing, juce::dontSendNotification);
        loopButton.setToggleState (processor.getPatternPlayer().isLooping(), juce::dontSendNotification);

        const auto pat = processor.getPattern();
        const int n = pat.bars;
        const auto text = pat.steps.empty() ? utf8 ("Партия пуста")
                                            : juce::String (n) + (n == 1 ? utf8 (" такт") : n < 5 ? utf8 (" такта") : utf8 (" тактов"))
                                                  + ", " + juce::String (juce::roundToInt (pat.bpm)) + " BPM";
        if (text != barsText)
        {
            barsText = text;
            repaint();
        }
        drag.refreshState();
    }

    void PatternStrip::timerCallback() { refreshState(); }

    void PatternStrip::paint (juce::Graphics& g)
    {
        if (! transportVisible)
            return;
        g.setColour (brand::textMuted);
        g.setFont (fonts::text (12.5f));
        auto r = getLocalBounds();
        r.removeFromLeft (92 + 8 + 82 + 14);
        g.drawText (barsText, r.withTrimmedRight (240), juce::Justification::centredLeft, true);
    }

    void PatternStrip::resized()
    {
        auto r = getLocalBounds();
        drag.setBounds (r.removeFromRight (juce::jmin (232, r.getWidth())));
        if (transportVisible)
        {
            playButton.setBounds (r.removeFromLeft (92));
            r.removeFromLeft (8);
            loopButton.setBounds (r.removeFromLeft (82));
        }
    }
}
