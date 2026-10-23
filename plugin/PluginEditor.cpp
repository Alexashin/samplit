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
        : AudioProcessorEditor (&p), processor (p), browser (p), groups (p), zones (p), keyboard (p)
    {
        setLookAndFeel (&lookAndFeel);
        logo = juce::Drawable::createFromImageData (BinaryData::lockuphondark_svg, BinaryData::lockuphondark_svgSize);

        // Enter в браузере кладёт файл на выбранную клавишу в выбранную группу
        browser.onPutOnKey = [this] (const juce::File& file)
        {
            const auto inst = processor.getInstrument();
            int group = groups.getSelectedGroup();
            if (inst == nullptr || inst->groups.empty())
                group = processor.addGroup (utf8 ("Группа 1"));
            if (group >= 0)
                processor.addFilesToKey ({ file }, group, keyboard.getSelectedKey(), Spread::Variations);
        };

        keyboard.onSelectKey = [this] (int key) { zones.setKey (key); };
        groups.onSelectGroup = [this] (int group)
        {
            keyboard.setSelectedGroup (group);
            zones.setGroup (group);
        };

        keyboard.setSelectedGroup (groups.getSelectedGroup());
        zones.setGroup (groups.getSelectedGroup());
        zones.setKey (keyboard.getSelectedKey());

        addAndMakeVisible (browser);
        addAndMakeVisible (groups);
        addAndMakeVisible (zones);
        addAndMakeVisible (keyboard);

        setResizable (true, true);
        setResizeLimits (900, 620, 1800, 1200);
        setSize (1180, 780);
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

        keyboard.setBounds (area.removeFromBottom (juce::jlimit (130, 180, area.getHeight() / 4)));
        area.removeFromBottom (14);

        browser.setBounds (area.removeFromLeft (juce::jlimit (320, 440, area.getWidth() * 3 / 10)));
        area.removeFromLeft (20);

        // Группам нужно место под несколько строк, остальное таблице зон
        groups.setBounds (area.removeFromTop (juce::jlimit (210, 330, area.getHeight() * 11 / 20)));
        area.removeFromTop (12);
        zones.setBounds (area);
    }

    void SamplitEditor::paint (juce::Graphics& g)
    {
        g.fillAll (brand::bg);

        if (logo != nullptr)
            logo->drawWithin (g, logoArea.toFloat(), juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid, 1.0f);
    }
}
