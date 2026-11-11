#pragma once

#include "PackCatalog.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace samplit
{
    // Вкладка «Инструменты»: слева двенадцать мест установки, справа карточка выбранного места: включено, звук (пак и барабан),
    // громкость, прослушивание и режимы выбора удара в группах. Все правки идут через процессор и сразу видны на других вкладках
    class InstrumentsPanel final : public juce::Component,
                                   private juce::ChangeListener
    {
    public:
        explicit InstrumentsPanel (SamplitProcessor&);
        ~InstrumentsPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        // Выбрать место по id (например, "snare"); неизвестный id игнорируется
        void selectPlace (const juce::String& placeId);

    private:
        struct PlaceList;
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
        juce::String groupsSignature;

        std::unique_ptr<PlaceList> list;
        juce::Label title, soundLabel, levelLabel, groupsLabel, packWarning, savedHint, noGroups;
        juce::ToggleButton enabledToggle;
        juce::ComboBox packBox, pieceBox;
        juce::Slider level;
        juce::TextButton listen;
        juce::Viewport viewport;
        std::unique_ptr<GroupsContent> groupsContent;

        std::vector<CatalogPack> packs;         // подходящие месту
        juce::Rectangle<int> cardArea;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (InstrumentsPanel)
    };
}
