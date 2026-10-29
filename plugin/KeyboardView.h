#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>

namespace samplit
{
    // Пиано-клавиатура (ноты 21..108 как у фортепиано, C3 = 60). Клик играет клавишу, чем ниже точка клика, тем громче.
    // С китом клавиши барабанов подкрашены по барабану, имя барабана видно в подсказке над клавиатурой.
    class KeyboardView final : public juce::Component,
                               private juce::ChangeListener,
                               private juce::Timer
    {
    public:
        static constexpr int kFirstKey = 21;   // A-1: с неё начинается бочка Studio600
        static constexpr int kLastKey = 108;

        explicit KeyboardView (SamplitProcessor&);
        ~KeyboardView() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;

        void setSelectedKey (int key);

    private:
        static bool isBlack (int key) noexcept;

        int keyAt (juce::Point<int>) const;
        void select (int key);
        void rebuildKeyInfo();
        juce::String hintText() const;
        juce::String pieceName (int key) const;

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;

        SamplitProcessor& processor;

        std::array<juce::Rectangle<float>, 128> keyRect {};
        juce::Rectangle<int> hintArea;

        // Что стоит на клавишах. Пересчитывается при смене кита, не в paint
        std::shared_ptr<const Kit> kit;             // не nullptr: играет кит, клавиатура показывает барабаны
        std::array<int, 128> pieceAt {};

        std::array<uint64_t, 2> shownActive { 0, 0 };
        int selectedKey = 60;
        juce::String info;
        juce::uint32 infoSince = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyboardView)
    };
}
