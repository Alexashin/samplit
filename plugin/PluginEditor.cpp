#include "PluginEditor.h"
#include "Brand.h"

#include <BinaryData.h>

namespace samplit
{
    namespace
    {
        constexpr int kCols = 4;
        constexpr int kRows = kNumPads / kCols;
        constexpr float kGap = 10.0f;
    }

    juce::AudioProcessorEditor* SamplitProcessor::createEditor()
    {
        return new SamplitEditor (*this);
    }

    SamplitEditor::SamplitEditor (SamplitProcessor& p) : AudioProcessorEditor (&p), processor (p)
    {
        logo = juce::Drawable::createFromImageData (BinaryData::lockuphondark_svg, BinaryData::lockuphondark_svgSize);

        setSize (640, 400);
        startTimerHz (30);
    }

    void SamplitEditor::resized()
    {
        auto area = getLocalBounds().toFloat().reduced (24.0f);
        logoBounds = area.removeFromTop (32.0f);
        area.removeFromTop (20.0f);
        gridBounds = area;
    }

    juce::Rectangle<float> SamplitEditor::padBounds (int pad) const
    {
        const float w = (gridBounds.getWidth() - kGap * (kCols - 1)) / (float) kCols;
        const float h = (gridBounds.getHeight() - kGap * (kRows - 1)) / (float) kRows;
        const int col = pad % kCols;
        const int row = pad / kCols;
        return { gridBounds.getX() + (float) col * (w + kGap), gridBounds.getY() + (float) row * (h + kGap), w, h };
    }

    int SamplitEditor::padAt (juce::Point<float> p) const
    {
        for (int i = 0; i < kNumPads; ++i)
            if (padBounds (i).contains (p))
                return i;
        return -1;
    }

    void SamplitEditor::mouseDown (const juce::MouseEvent& e)
    {
        if (const int pad = padAt (e.position); pad >= 0)
            processor.triggerPad (pad);
    }

    void SamplitEditor::timerCallback()
    {
        const auto active = processor.getActiveMask();
        const auto separate = processor.getSeparateOutMask();
        if (active != shownActive || separate != shownSeparate)
        {
            shownActive = active;
            shownSeparate = separate;
            repaint();
        }
    }

    void SamplitEditor::paint (juce::Graphics& g)
    {
        g.fillAll (brand::bg);

        if (logo != nullptr)
            logo->drawWithin (g, logoBounds, juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid, 1.0f);

        const auto small = juce::Font (juce::FontOptions (12.0f));
        g.setFont (small);
        g.setColour (brand::textMuted);
        g.drawText ("NOTE C1 = PAD 1", logoBounds.toNearestInt(), juce::Justification::centredRight);

        for (int pad = 0; pad < kNumPads; ++pad)
        {
            const auto r = padBounds (pad);
            const bool lit = (shownActive & (1u << pad)) != 0;
            const bool own = (shownSeparate & (1u << pad)) != 0;
            const float radius = r.getHeight() * brand::padRadiusRatio;

            g.setColour (lit ? brand::accent : brand::pad);
            g.fillRoundedRectangle (r, radius);

            const auto inner = r.reduced (14.0f);
            g.setColour (lit ? brand::onAccent : brand::text);
            g.setFont (juce::Font (juce::FontOptions (22.0f, juce::Font::bold)));
            g.drawText (juce::String (pad + 1), inner.toNearestInt(), juce::Justification::bottomLeft);

            g.setColour (lit ? brand::onAccent : brand::textMuted);
            g.setFont (juce::Font (juce::FontOptions (12.0f)));
            g.drawText (own ? "OUT " + juce::String (pad + 1) : juce::String ("MAIN"),
                        inner.toNearestInt(), juce::Justification::topLeft);
        }
    }
}
