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
        : AudioProcessorEditor (&p), processor (p), presets (p), license (p.getLicense()), mixer (p), picks (p), keyboard (p)
    {
        setLookAndFeel (&lookAndFeel);
        logo = juce::Drawable::createFromImageData (BinaryData::lockuphondark_svg, BinaryData::lockuphondark_svgSize);

        // Лицензия поменялась: другие пакеты открыты, у пресетов другое состояние
        processor.getLicense().onChange = [this]
        {
            license.refresh();
            presets.rescan();
        };

        mixerTab.setButtonText (utf8 ("Микрофоны"));
        picksTab.setButtonText (utf8 ("Выбор удара"));
        picksTab.setTooltip (utf8 ("Как выбирается удар: случайно, по кругу или по velocity"));
        for (auto* t : { &mixerTab, &picksTab })
        {
            t->setClickingTogglesState (true);
            t->setRadioGroupId (1);
        }
        mixerTab.onClick = [this] { showRightTab (false); };
        picksTab.onClick = [this] { showRightTab (true); };
        mixerTab.setToggleState (true, juce::dontSendNotification);

        for (auto* c : std::initializer_list<juce::Component*> { &presets, &license, &mixer, &picks, &mixerTab, &picksTab, &keyboard })
            addChildComponent (c);
        for (auto* c : std::initializer_list<juce::Component*> { &presets, &license, &mixerTab, &picksTab, &keyboard })
            c->setVisible (true);
        showRightTab (false);

        juce::Desktop::getInstance().addFocusChangeListener (this);

        setResizable (true, true);
        setResizeLimits (900, 620, 1800, 1200);
        setSize (1180, 780);
    }

    SamplitEditor::~SamplitEditor()
    {
        juce::Desktop::getInstance().removeFocusChangeListener (this);
        processor.getLicense().onChange = nullptr;
        setLookAndFeel (nullptr);
    }

    // Обводка фокуса рисуется в LookAndFeel: перерисовать прежний и новый элемент
    void SamplitEditor::globalFocusChanged (juce::Component* focused)
    {
        if (lastFocused != nullptr)
            lastFocused->repaint();
        lastFocused = focused != nullptr && isParentOf (focused) ? focused : nullptr;
        if (lastFocused != nullptr)
            lastFocused->repaint();
    }

    void SamplitEditor::showRightTab (bool showPicks)
    {
        (showPicks ? picksTab : mixerTab).setToggleState (true, juce::dontSendNotification);
        mixer.setVisible (! showPicks);
        picks.setVisible (showPicks);
    }

    void SamplitEditor::resized()
    {
        auto area = getLocalBounds().reduced (20);
        logoArea = area.removeFromTop (30);
        area.removeFromTop (16);

        keyboard.setBounds (area.removeFromBottom (juce::jlimit (130, 180, area.getHeight() / 4)));
        area.removeFromBottom (14);

        auto left = area.removeFromLeft (juce::jlimit (300, 400, area.getWidth() * 3 / 10));
        area.removeFromLeft (20);
        license.setBounds (left.removeFromBottom (150));
        left.removeFromBottom (12);
        presets.setBounds (left);
        auto tabs = area.removeFromTop (30);
        mixerTab.setBounds (tabs.removeFromLeft (130));
        tabs.removeFromLeft (6);
        picksTab.setBounds (tabs.removeFromLeft (130));
        area.removeFromTop (8);
        mixer.setBounds (area);
        picks.setBounds (area);
    }

    void SamplitEditor::paint (juce::Graphics& g)
    {
        g.fillAll (brand::bg);

        if (logo != nullptr)
            logo->drawWithin (g, logoArea.toFloat(), juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid, 1.0f);
    }
}
