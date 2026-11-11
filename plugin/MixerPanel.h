#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace samplit
{
    // Микшер микрофонов кита: по линейке на микрофон. Выход (в режиме «Мульти-аут»), панорама, уровень, Mute и Solo.
    // Выход, который DAW не включила, подсвечен: микрофон с него играет в Main
    class MixerPanel final : public juce::Component,
                             private juce::ChangeListener,
                             private juce::Timer
    {
    public:
        explicit MixerPanel (SamplitProcessor&);
        ~MixerPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        class Strip;
        friend class Strip;

        void rebuild();
        void updateStrips();
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;

        SamplitProcessor& processor;
        std::shared_ptr<const Kit> kit;
        uint32_t shownOutputs = 0;
        bool shownMulti = false;
        juce::Label modeLabel;
        juce::ComboBox mode;

        juce::Label title, hint, emptyText;
        juce::Viewport viewport;
        juce::Component content;
        std::vector<std::unique_ptr<Strip>> strips;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MixerPanel)
    };
}
