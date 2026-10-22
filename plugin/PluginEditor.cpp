#include "PluginEditor.h"
#include "Brand.h"

#include <BinaryData.h>

namespace samplit
{
    juce::AudioProcessorEditor* SamplitProcessor::createEditor()
    {
        return new SamplitEditor (*this);
    }

    SamplitEditor::SamplitEditor (SamplitProcessor& p)
        : AudioProcessorEditor (&p), processor (p), browser (p), pads (p), settings (p)
    {
        setLookAndFeel (&lookAndFeel);
        logo = juce::Drawable::createFromImageData (BinaryData::lockuphondark_svg, BinaryData::lockuphondark_svgSize);

        browser.onPutOnFreePad = [this] (const juce::File& file)
        {
            const int pad = pads.firstFreePad();
            processor.loadPadSample (pad >= 0 ? pad : 0, file);
        };

        pads.onSelect = [this] (int pad) { settings.setPad (pad); };

        addAndMakeVisible (browser);
        addAndMakeVisible (pads);
        addAndMakeVisible (settings);

        setResizable (true, true);
        setResizeLimits (860, 520, 1800, 1200);
        setSize (1080, 680);
    }

    SamplitEditor::~SamplitEditor()
    {
        setLookAndFeel (nullptr);
    }

    void SamplitEditor::resized()
    {
        auto area = getLocalBounds().reduced (20);
        logoArea = area.removeFromTop (30);
        area.removeFromTop (16);

        browser.setBounds (area.removeFromLeft (juce::jlimit (320, 460, area.getWidth() * 2 / 5)));
        area.removeFromLeft (20);

        // Пэды квадратные по ширине колонки, остаток высоты отдаём панели настроек
        pads.setBounds (area.removeFromTop (juce::jmin (PadGrid::preferredHeight (area.getWidth()), area.getHeight() - 150)));
        area.removeFromTop (6);
        settings.setBounds (area.withHeight (juce::jmin (area.getHeight(), 150)));
    }

    void SamplitEditor::paint (juce::Graphics& g)
    {
        g.fillAll (brand::bg);

        if (logo != nullptr)
            logo->drawWithin (g, logoArea.toFloat(), juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid, 1.0f);

        g.setColour (brand::textMuted);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (utf8 ("Офлайн-библиотека сэмплов · мульти-аут"), logoArea, juce::Justification::centredRight);
    }
}
