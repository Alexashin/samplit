#include "PluginEditor.h"
#include "Brand.h"

#include <BinaryData.h>

namespace samplit
{
    namespace
    {
       #if SAMPLIT_SERVICE_MODE
        constexpr bool kServiceModeAvailable = true;
       #else
        constexpr bool kServiceModeAvailable = false;
       #endif
    }

    juce::AudioProcessorEditor* SamplitProcessor::createEditor()
    {
        return new SamplitEditor (*this);
    }

    SamplitEditor::SamplitEditor (SamplitProcessor& p)
        : AudioProcessorEditor (&p), processor (p), presets (p), license (p.getLicense()), mixer (p),
          browser (p), groups (p), zones (p), keyboard (p)
    {
        setLookAndFeel (&lookAndFeel);
        logo = juce::Drawable::createFromImageData (BinaryData::lockuphondark_svg, BinaryData::lockuphondark_svgSize);

        // Лицензия поменялась: другие пакеты открыты, у пресетов другое состояние
        processor.getLicense().onChange = [this]
        {
            license.refresh();
            presets.rescan();
        };

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

        serviceButton.setButtonText (utf8 ("Служебный режим"));
        serviceButton.setClickingTogglesState (true);
        serviceButton.setTooltip (utf8 ("Ручная раскладка сэмплов по клавишам. Пока он включён, кит молчит"));
        serviceButton.onClick = [this] { showServiceScreen (serviceButton.getToggleState()); };

        for (auto* c : std::initializer_list<juce::Component*> { &presets, &license, &mixer, &browser, &groups, &zones, &keyboard })
            addChildComponent (c);
        keyboard.setVisible (true);
        addChildComponent (serviceButton);
        serviceButton.setVisible (kServiceModeAvailable);

        showServiceScreen (kServiceModeAvailable && processor.isServiceMode());

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

    void SamplitEditor::showServiceScreen (bool service)
    {
        processor.setServiceMode (service);
        serviceButton.setToggleState (service, juce::dontSendNotification);

        for (auto* c : std::initializer_list<juce::Component*> { &presets, &license, &mixer })
            c->setVisible (! service);
        for (auto* c : std::initializer_list<juce::Component*> { &browser, &groups, &zones })
            c->setVisible (service);
        resized();
    }

    void SamplitEditor::resized()
    {
        auto area = getLocalBounds().reduced (20);
        logoArea = area.removeFromTop (30);
        if (serviceButton.isVisible())
            serviceButton.setBounds (logoArea.removeFromRight (150).reduced (0, 1));
        area.removeFromTop (16);

        keyboard.setBounds (area.removeFromBottom (juce::jlimit (130, 180, area.getHeight() / 4)));
        area.removeFromBottom (14);

        if (! processor.isServiceMode())
        {
            auto left = area.removeFromLeft (juce::jlimit (300, 400, area.getWidth() * 3 / 10));
            area.removeFromLeft (20);
            license.setBounds (left.removeFromBottom (150));
            left.removeFromBottom (12);
            presets.setBounds (left);
            mixer.setBounds (area);
            return;
        }

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
