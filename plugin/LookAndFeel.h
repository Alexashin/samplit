#pragma once

#include "Brand.h"
#include "ui/Fonts.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace samplit
{
    // Вид стандартных компонентов JUCE под визуальную систему (docs/ui-guidelines.md): плоско, тонкие линии, радиус 3–4 px,
    // крутилки и фейдеры как у железа. Новые компоненты (ui/Knob.h, Fader.h, Controls.h) рисуют себя через те же токены
    class SamplitLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        SamplitLookAndFeel()
        {
            using namespace brand;
            setColourScheme ({ bg, surface, pad, line, text, accentSoft, text, pad, text });
            setDefaultSansSerifTypeface (fonts::sansRegular());

            setColour (juce::ResizableWindow::backgroundColourId, bg);
            setColour (juce::TextButton::buttonColourId, pad);
            setColour (juce::TextButton::buttonOnColourId, accent);
            setColour (juce::TextButton::textColourOffId, text);
            setColour (juce::TextButton::textColourOnId, onAccent);
            setColour (juce::ComboBox::backgroundColourId, pad);
            setColour (juce::ComboBox::outlineColourId, lineStrong);
            setColour (juce::ComboBox::textColourId, text);
            setColour (juce::ComboBox::arrowColourId, textMuted);
            setColour (juce::TextEditor::backgroundColourId, bg);
            setColour (juce::TextEditor::textColourId, text);
            setColour (juce::TextEditor::outlineColourId, lineStrong);
            setColour (juce::TextEditor::focusedOutlineColourId, accentHover);
            setColour (juce::TextEditor::highlightColourId, accentSoft);
            setColour (juce::CaretComponent::caretColourId, accentText);
            setColour (juce::ListBox::backgroundColourId, surface);
            setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
            setColour (juce::Label::textColourId, text);
            setColour (juce::ScrollBar::thumbColourId, lineStrong);
            setColour (juce::PopupMenu::backgroundColourId, surface);
            setColour (juce::PopupMenu::textColourId, text);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, accentSoft);
            setColour (juce::PopupMenu::highlightedTextColourId, text);
            setColour (juce::TooltipWindow::backgroundColourId, raised2);
            setColour (juce::TooltipWindow::textColourId, text);
            setColour (juce::TooltipWindow::outlineColourId, lineStrong);

            // Цвета слайдеров по умолчанию; панели могут задать свои
            setColour (juce::Slider::rotarySliderFillColourId, accent);
            setColour (juce::Slider::rotarySliderOutlineColourId, lineStrong);
            setColour (juce::Slider::thumbColourId, text);
            setColour (juce::Slider::trackColourId, accent);
            setColour (juce::Slider::backgroundColourId, line);
        }

        juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override
        {
            return f.isBold() ? fonts::sansMedium() : fonts::sansRegular();
        }

        // Крутилка: тёмный корпус с тонкой кромкой, дуга значения (3 px от края), риска-указатель.
        // У двуполярных ручек (высота, панорама) дуга растёт от центра
        void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                               float startAngle, float endAngle, juce::Slider& slider) override
        {
            const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (2.0f);
            const float outer = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
            const auto centre = bounds.getCentre();
            const float arcThickness = outer >= 20.0f ? 2.5f : 2.0f;
            const float arcRadius = outer - arcThickness * 0.5f;
            const float bodyRadius = arcRadius - arcThickness - 2.5f;

            const auto arc = [&] (float from, float to)
            {
                juce::Path p;
                p.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, from, to, true);
                return p;
            };
            const juce::PathStrokeType stroke (arcThickness, juce::PathStrokeType::curved, juce::PathStrokeType::butt);

            g.setColour (slider.findColour (juce::Slider::rotarySliderOutlineColourId));
            g.strokePath (arc (startAngle, endAngle), stroke);

            const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0
                                 && std::abs (slider.getMinimum() + slider.getMaximum()) < 1e-6;
            const float origin = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
            const float value = startAngle + pos * (endAngle - startAngle);
            if (std::abs (value - origin) > 0.001f)
            {
                g.setColour (slider.isEnabled() ? slider.findColour (juce::Slider::rotarySliderFillColourId) : brand::lineStrong);
                g.strokePath (arc (juce::jmin (origin, value), juce::jmax (origin, value)), stroke);
            }

            // Корпус
            const auto body = juce::Rectangle<float> (bodyRadius * 2.0f, bodyRadius * 2.0f).withCentre (centre);
            g.setColour (brand::pad);
            g.fillEllipse (body);
            g.setColour (brand::lineStrong);
            g.drawEllipse (body, 1.0f);

            // Указатель: короткая риска от края корпуса к центру
            const juce::Point<float> inner (centre.x + bodyRadius * 0.35f * std::sin (value), centre.y - bodyRadius * 0.35f * std::cos (value));
            const juce::Point<float> rim (centre.x + bodyRadius * 0.86f * std::sin (value), centre.y - bodyRadius * 0.86f * std::cos (value));
            g.setColour (slider.isEnabled() ? slider.findColour (juce::Slider::thumbColourId) : brand::textFaint);
            g.drawLine ({ inner, rim }, outer >= 20.0f ? 2.0f : 1.6f);
            drawFocus (g, slider, outer);
        }

        // Фейдер (вертикальный): узкий паз, плоская ручка с риской. Горизонтальный: тонкая дорожка и небольшая ручка
        void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float pos, float /*minPos*/,
                               float /*maxPos*/, juce::Slider::SliderStyle style, juce::Slider& slider) override
        {
            const bool vertical = style == juce::Slider::LinearVertical;
            const auto track = slider.findColour (juce::Slider::trackColourId);
            const auto groove = slider.findColour (juce::Slider::backgroundColourId);

            if (vertical)
            {
                const float cx = (float) x + (float) width * 0.5f;
                const float thumbH = 14.0f, thumbW = juce::jmin ((float) width, 26.0f);
                const float top = (float) y + thumbH * 0.5f, bottom = (float) (y + height) - thumbH * 0.5f;
                g.setColour (groove);
                g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, bottom - top).withCentre ({ cx, (top + bottom) * 0.5f }), 2.0f);
                g.setColour (slider.isEnabled() ? track : brand::lineStrong);
                g.fillRoundedRectangle (juce::Rectangle<float> (4.0f, bottom - pos).withBottomY (bottom).withCentre ({ cx, (pos + bottom) * 0.5f }), 2.0f);

                const auto thumb = juce::Rectangle<float> (thumbW, thumbH).withCentre ({ cx, pos });
                g.setColour (brand::raised2);
                g.fillRoundedRectangle (thumb, brand::radiusS);
                g.setColour (brand::lineStrong);
                g.drawRoundedRectangle (thumb.reduced (0.5f), brand::radiusS, 1.0f);
                g.setColour (slider.isEnabled() ? brand::text : brand::textFaint);
                g.fillRect (juce::Rectangle<float> (thumbW - 8.0f, 1.5f).withCentre (thumb.getCentre()));
            }
            else
            {
                const float cy = (float) y + (float) height * 0.5f;
                const float left = (float) x + 5.0f, right = (float) (x + width) - 5.0f;
                g.setColour (groove);
                g.fillRoundedRectangle (juce::Rectangle<float> (right - left, 3.0f).withCentre ({ (left + right) * 0.5f, cy }), 1.5f);
                g.setColour (slider.isEnabled() ? track : brand::lineStrong);
                g.fillRoundedRectangle (juce::Rectangle<float> (pos - left, 3.0f).withLeft (left).withCentre ({ (left + pos) * 0.5f, cy }), 1.5f);
                const auto thumb = juce::Rectangle<float> (10.0f, 18.0f).withCentre ({ pos, cy });
                g.setColour (brand::raised2);
                g.fillRoundedRectangle (thumb, brand::radiusS);
                g.setColour (brand::lineStrong);
                g.drawRoundedRectangle (thumb.reduced (0.5f), brand::radiusS, 1.0f);
            }
            drawFocus (g, slider, brand::radiusM);
        }

        int getSliderThumbRadius (juce::Slider& s) override { return s.isVertical() ? 7 : 5; }

        juce::Font getComboBoxFont (juce::ComboBox& box) override
        {
            return fonts::text (box.getHeight() <= 24 ? 12.5f : 13.0f);
        }
        juce::Font getTextButtonFont (juce::TextButton&, int) override { return fonts::strong (13.0f); }
        juce::Font getPopupMenuFont() override { return fonts::text (13.5f); }
        juce::Font getLabelFont (juce::Label&) override { return fonts::text (13.0f); }

        void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& background,
                                   bool highlighted, bool down) override
        {
            auto colour = background;
            if (down)
                colour = colour.brighter (0.12f);
            else if (highlighted)
                colour = colour.brighter (0.07f);
            const auto r = button.getLocalBounds().toFloat();
            g.setColour (colour);
            g.fillRoundedRectangle (r, brand::radiusM);
            if (! button.getToggleState() && background == brand::pad)
            {
                g.setColour (highlighted ? brand::textFaint : brand::lineStrong);
                g.drawRoundedRectangle (r.reduced (0.5f), brand::radiusM, 1.0f);
            }
            drawFocus (g, button, brand::radiusM);
        }

        void drawComboBox (juce::Graphics& g, int width, int height, bool /*down*/, int, int, int, int,
                           juce::ComboBox& box) override
        {
            const auto r = juce::Rectangle<float> ((float) width, (float) height);
            g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
            g.fillRoundedRectangle (r, brand::radiusM);
            g.setColour (box.isMouseOver() ? brand::textFaint : box.findColour (juce::ComboBox::outlineColourId));
            g.drawRoundedRectangle (r.reduced (0.5f), brand::radiusM, 1.0f);

            juce::Path arrow;
            const float ax = (float) width - 14.0f, ay = (float) height * 0.5f;
            arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 2.5f);
            g.setColour (box.findColour (juce::ComboBox::arrowColourId));
            g.fillPath (arrow);
            drawFocus (g, box, brand::radiusM);
        }

        void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
        {
            label.setBounds (8, 1, box.getWidth() - 28, box.getHeight() - 2);
            label.setFont (getComboBoxFont (box));
        }

        void drawPopupMenuBackground (juce::Graphics& g, int width, int height) override
        {
            g.fillAll (brand::surface);
            g.setColour (brand::lineStrong);
            g.drawRect (0, 0, width, height, 1);
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
