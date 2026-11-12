#pragma once

#include "PackCatalog.h"
#include "PluginProcessor.h"
#include "ui/Controls.h"
#include "ui/Knob.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace samplit
{
    // Панель выбранного барабана (справа от схемы): включён, звук, громкость барабана крутилкой, «Прослушать» и
    // раскрывашка «Подробнее» с группами ударов, режимами выбора удара и картой силы. Все правки идут через процессор
    // и сразу видны на других страницах. Новые параметры барабана добавляются в «Подробнее» как строки раскрывашки
    class DrumPanel final : public juce::Component,
                            private juce::ChangeListener
    {
    public:
        explicit DrumPanel (SamplitProcessor&);
        ~DrumPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        // Выбрать барабан по id (например, "snare"); неизвестный id игнорируется
        void selectPlace (const juce::String& placeId);
        void setDetailsOpen (bool open) { groupsOpen = open; disclosure.setToggleState (open, juce::dontSendNotification); resized(); repaint(); }

    private:
        struct GroupRow;
        struct GroupsContent;

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void refresh (bool forceGroups);
        void rebuildGroups (const Setup&, const PlaceDef&, const PlaceSetup&, const std::vector<CatalogPack>& packs);
        void layoutGroups();
        const PlaceDef& currentPlace() const;

        SamplitProcessor& processor;
        int selected = 0;                       // индекс в kStudio600Places
        bool updating = false;
        bool groupsOpen = false;
        juce::String groupsSignature;

        juce::Label title, packWarning, noGroups;
        LedButton enabledLed { utf8 ("Вкл") };
        juce::ComboBox packBox, pieceBox;
        Knob levelKnob { utf8 ("Громкость"), Knob::Size::Large };
        juce::TextButton listen;
        DisclosureHeader disclosure { utf8 ("Подробнее") };
        juce::Viewport viewport;
        std::unique_ptr<GroupsContent> groupsContent;

        std::vector<CatalogPack> packs;         // подходящие месту
        juce::Rectangle<int> soundCaption;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumPanel)
    };
}
