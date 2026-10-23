#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <vector>

namespace samplit
{
    // Таблица зон выбранной клавиши. Показывает зоны всех групп, зоны выбранной группы ярче.
    // У зоны правятся диапазон velocity, основная нота и громкость; есть прослушивание и удаление.
    class ZoneEditor final : public juce::Component,
                             private juce::ListBoxModel,
                             private juce::ChangeListener
    {
    public:
        explicit ZoneEditor (SamplitProcessor&);
        ~ZoneEditor() override;

        void setKey (int key);
        void setGroup (int group);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        class RowComponent;
        friend class RowComponent;

        void refresh();
        void editZone (int zoneIndex, const std::function<void (Zone&)>&);
        void splitVelocity();
        void clearKey();

        int getNumRows() override { return (int) rows.size(); }
        void paintListBoxItem (int, juce::Graphics&, int, int, bool) override {}
        juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

        // Колонки справа налево, одинаковые у строк и у шапки
        static constexpr int kButton = 28, kGap = 6, kVelWidth = 54, kRootWidth = 58, kGainWidth = 78;

        SamplitProcessor& processor;
        std::shared_ptr<const Instrument> instrument;
        int key = 60;
        int group = 0;
        std::vector<int> rows;  // индексы зон в снимке

        juce::TextButton splitButton, clearButton;
        juce::ListBox list { "Zones", this };
        juce::Rectangle<int> titleArea, headerArea, emptyArea;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZoneEditor)
    };
}
