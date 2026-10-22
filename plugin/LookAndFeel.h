#pragma once

#include "Brand.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace samplit
{
    // Цвета стандартных компонентов JUCE (кнопки, поля, списки, меню) под брендбук
    class SamplitLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        SamplitLookAndFeel()
        {
            using namespace brand;
            setColourScheme ({ bg, surface, pad, line, text, accentSoft, text, pad, text });

            setColour (juce::ResizableWindow::backgroundColourId, bg);
            setColour (juce::TextButton::buttonColourId, pad);
            setColour (juce::TextButton::buttonOnColourId, accent);
            setColour (juce::TextButton::textColourOffId, text);
            setColour (juce::TextButton::textColourOnId, onAccent);
            setColour (juce::ComboBox::outlineColourId, line);
            setColour (juce::TextEditor::backgroundColourId, pad);
            setColour (juce::TextEditor::textColourId, text);
            setColour (juce::TextEditor::outlineColourId, line);
            setColour (juce::TextEditor::focusedOutlineColourId, accent);
            setColour (juce::TextEditor::highlightColourId, accentSoft);
            setColour (juce::CaretComponent::caretColourId, accent);
            setColour (juce::ListBox::backgroundColourId, surface);
            setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
            setColour (juce::Label::textColourId, text);
            setColour (juce::ScrollBar::thumbColourId, line);
            setColour (juce::PopupMenu::backgroundColourId, surface);
            setColour (juce::PopupMenu::textColourId, text);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, accentSoft);
            setColour (juce::PopupMenu::highlightedTextColourId, text);
        }

        void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                                   bool highlighted, bool down) override
        {
            auto colour = background;
            if (down)
                colour = colour.brighter (0.15f);
            else if (highlighted)
                colour = colour.brighter (0.08f);
            g.setColour (colour);
            g.fillRoundedRectangle (button.getLocalBounds().toFloat(), 8.0f);
        }
    };
}
