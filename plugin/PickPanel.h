#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace samplit
{
    // Выбор удара: по переключателю на каждое правило пресета (бочка, хэт, крэши…). Случайно, по кругу или
    // точно по velocity. Значения по умолчанию из пресета, выбор пользователя сохраняется в проекте
    class PickPanel final : public juce::Component,
                            private juce::ChangeListener
    {
    public:
        explicit PickPanel (SamplitProcessor&);
        ~PickPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        class Row;

        void rebuild();
        void changeListenerCallback (juce::ChangeBroadcaster*) override;

        SamplitProcessor& processor;
        std::shared_ptr<const Kit> kit;

        juce::Label title, hint, emptyText;
        juce::Viewport viewport;
        juce::Component content;
        std::vector<std::unique_ptr<Row>> rows;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PickPanel)
    };
}
