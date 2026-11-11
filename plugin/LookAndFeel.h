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

        // Ручка: дорожка, дуга значения и точка. У двуполярных ручек (высота, панорама) дуга растёт от центра
        void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                               float startAngle, float endAngle, juce::Slider& slider) override
        {
            const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (6.0f);
            const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
            const auto centre = bounds.getCentre();
            const float thickness = juce::jmax (3.0f, radius * 0.16f);
            const float arcRadius = radius - thickness * 0.5f;

            const auto arc = [&] (float from, float to)
            {
                juce::Path p;
                p.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, from, to, true);
                return p;
            };
            const juce::PathStrokeType stroke (thickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

            g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
            g.strokePath (arc (startAngle, endAngle), stroke);

            const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0
                                 && std::abs (slider.getMinimum() + slider.getMaximum()) < 1e-6;
            const float origin = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
            const float value = startAngle + pos * (endAngle - startAngle);
            if (std::abs (value - origin) > 0.001f)
            {
                g.setColour (slider.findColour (juce::Slider::rotarySliderFillColourId));
                g.strokePath (arc (juce::jmin (origin, value), juce::jmax (origin, value)), stroke);
            }

            const juce::Point<float> dot (centre.x + arcRadius * std::sin (value), centre.y - arcRadius * std::cos (value));
            g.setColour (slider.findColour (juce::Slider::thumbColourId));
            g.fillEllipse (juce::Rectangle<float> (thickness * 1.6f, thickness * 1.6f).withCentre (dot));
            drawFocus (g, slider, 6.0f);
        }

        // Невысокие списки (выход в линейке микшера) мельче, чтобы влезало «Snare Bottom»
        juce::Font getComboBoxFont (juce::ComboBox& box) override
        {
            return juce::FontOptions (box.getHeight() <= 24 ? 13.0f : juce::jmin (16.0f, (float) box.getHeight() * 0.85f));
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
            drawFocus (g, button, 8.0f);
        }

        void drawComboBox (juce::Graphics& g, int width, int height, bool down, int bx, int by, int bw, int bh,
                           juce::ComboBox& box) override
        {
            LookAndFeel_V4::drawComboBox (g, width, height, down, bx, by, bw, bh, box);
            drawFocus (g, box, 5.0f);
        }

        void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float pos, float minPos,
                               float maxPos, juce::Slider::SliderStyle style, juce::Slider& slider) override
        {
            LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, pos, minPos, maxPos, style, slider);
            drawFocus (g, slider, 6.0f);
        }

        // Обводка элемента с фокусом клавиатуры. Слайдеры сами не перерисовываются при смене фокуса,
        // поэтому окно плагина перерисовывает их через FocusChangeListener
        static void drawFocus (juce::Graphics& g, juce::Component& c, float radius)
        {
            if (! c.hasKeyboardFocus (false))
                return;
            g.setColour (brand::accentHover);
            g.drawRoundedRectangle (c.getLocalBounds().toFloat().reduced (1.0f), radius, 2.0f);
        }
    };
}
