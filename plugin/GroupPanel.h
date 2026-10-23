#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace samplit
{
    // Список групп. У каждой группы: имя, выход (Main или Out N), режим выбора вариации, высота по клавише,
    // громкость и панорама. Выбранная группа получает файлы, которые роняют на клавиатуру.
    class GroupPanel final : public juce::Component,
                             private juce::ChangeListener,
                             private juce::Timer
    {
    public:
        explicit GroupPanel (SamplitProcessor&);
        ~GroupPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        int getSelectedGroup() const noexcept { return selected; }
        void setSelectedGroup (int group);
        std::function<void (int)> onSelectGroup;

    private:
        class Row;
        friend class Row;

        void refresh();
        void select (int group);
        void editGroup (int index, const std::function<void (Group&)>&);
        void addGroup();
        void autoRoute();
        void removeGroup (int index);
        void updateFooter();

        void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
        void timerCallback() override;

        SamplitProcessor& processor;
        std::shared_ptr<const Instrument> instrument;
        int selected = 0;
        uint32_t shownOutputs = 0;

        juce::Label title;
        juce::TextButton addButton, autoButton;
        juce::Viewport viewport;
        juce::Component content;
        std::vector<std::unique_ptr<Row>> rows;
        juce::Label footer;
        bool footerWarns = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GroupPanel)
    };
}
