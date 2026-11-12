#pragma once

#include "../Brand.h"
#include "Fonts.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace samplit
{
    // Заголовок раздела: мелкие прописные с разрядкой, тонкая линия справа
    inline void drawSectionCaption (juce::Graphics& g, const juce::String& title, juce::Rectangle<int> area)
    {
        const auto font = fonts::caption();
        const auto text = title.toUpperCase();
        g.setColour (brand::textMuted);
        g.setFont (font);
        g.drawText (text, area, juce::Justification::centredLeft, false);
        const int w = juce::GlyphArrangement::getStringWidthInt (font, text) + 8;
        if (area.getWidth() > w + 8)
        {
            g.setColour (brand::line);
            g.fillRect (area.getX() + w, area.getCentreY(), area.getWidth() - w, 1);
        }
    }

    // Кнопка-переключатель как у прибора: подпись и светодиод. Включён: LED акцентом, подпись светлая
    class LedButton final : public juce::Button
    {
    public:
        explicit LedButton (const juce::String& label) : juce::Button (label)
        {
            setClickingTogglesState (true);
            setWantsKeyboardFocus (true);
        }

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const auto r = getLocalBounds().toFloat();
            const bool on = getToggleState();
            g.setColour (down ? brand::raised2 : highlighted ? brand::pad.brighter (0.06f) : brand::pad);
            g.fillRoundedRectangle (r, brand::radiusM);
            g.setColour (on ? brand::accent : (highlighted ? brand::textFaint : brand::lineStrong));
            g.drawRoundedRectangle (r.reduced (0.5f), brand::radiusM, 1.0f);

            const float d = 7.0f;
            const auto led = juce::Rectangle<float> (d, d).withCentre ({ 14.0f, r.getCentreY() });
            g.setColour (on ? brand::accentHover : brand::lineStrong);
            g.fillEllipse (led);

            g.setColour (isEnabled() ? (on ? brand::text : brand::textMuted) : brand::textFaint);
            g.setFont (fonts::strong (13.0f));
            g.drawText (getButtonText(), getLocalBounds().withTrimmedLeft (26).withTrimmedRight (8), juce::Justification::centredLeft, true);

            if (hasKeyboardFocus (false))
            {
                g.setColour (brand::accentHover);
                g.drawRoundedRectangle (r.reduced (1.0f), brand::radiusM, 2.0f);
            }
        }
    };

    // Сегменты: один из нескольких вариантов (такты 1–4, Стерео / Мульти-аут). Выбранный: поднятая плашка, без заливки акцентом
    class SegmentedControl final : public juce::Component,
                                   public juce::SettableTooltipClient
    {
    public:
        explicit SegmentedControl (std::vector<juce::String> labels) : items (std::move (labels)) { setWantsKeyboardFocus (true); }

        void setItems (std::vector<juce::String> labels) { items = std::move (labels); selected = 0; repaint(); }
        int getSelected() const { return selected; }
        void setSelected (int i, bool notify = false)
        {
            if (i == selected)
                return;
            selected = juce::jlimit (0, juce::jmax (0, (int) items.size() - 1), i);
            repaint();
            if (notify && onChange)
                onChange (selected);
        }
        std::function<void (int)> onChange;

        // Ячейки одной ширины: самая длинная подпись плюс поля
        int preferredWidth() const
        {
            int widest = 36;
            for (const auto& s : items)
                widest = juce::jmax (widest, juce::GlyphArrangement::getStringWidthInt (fonts::strong (13.0f), s) + 28);
            return 4 + widest * juce::jmax (1, (int) items.size());
        }

        void paint (juce::Graphics& g) override
        {
            g.beginTransparencyLayer (isEnabled() ? 1.0f : 0.45f);
            const auto r = getLocalBounds().toFloat();
            g.setColour (brand::bg);
            g.fillRoundedRectangle (r, brand::radiusM);
            g.setColour (brand::line);
            g.drawRoundedRectangle (r.reduced (0.5f), brand::radiusM, 1.0f);
            for (int i = 0; i < (int) items.size(); ++i)
            {
                const auto cell = cellBounds (i).toFloat();
                if (i == selected)
                {
                    g.setColour (brand::raised2);
                    g.fillRoundedRectangle (cell, brand::radiusS);
                    g.setColour (brand::lineStrong);
                    g.drawRoundedRectangle (cell.reduced (0.5f), brand::radiusS, 1.0f);
                }
                g.setColour (i == selected ? brand::text : brand::textMuted);
                g.setFont (fonts::strong (13.0f));
                g.drawText (items[(size_t) i], cell, juce::Justification::centred, false);
            }
            if (hasKeyboardFocus (false))
            {
                g.setColour (brand::accentHover);
                g.drawRoundedRectangle (r.reduced (1.0f), brand::radiusM, 2.0f);
            }
            g.endTransparencyLayer();
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! isEnabled())
                return;
            for (int i = 0; i < (int) items.size(); ++i)
                if (cellBounds (i).contains (e.getPosition()))
                    setSelected (i, true);
        }

        bool keyPressed (const juce::KeyPress& k) override
        {
            if (k == juce::KeyPress::leftKey && selected > 0)  { setSelected (selected - 1, true); return true; }
            if (k == juce::KeyPress::rightKey && selected + 1 < (int) items.size()) { setSelected (selected + 1, true); return true; }
            return false;
        }

    private:
        juce::Rectangle<int> cellBounds (int i) const
        {
            const int n = juce::jmax (1, (int) items.size());
            const auto inner = getLocalBounds().reduced (2);
            const int w = inner.getWidth() / n;
            return { inner.getX() + i * w, inner.getY(), i == n - 1 ? inner.getRight() - (inner.getX() + i * w) : w, inner.getHeight() };
        }

        std::vector<juce::String> items;
        int selected = 0;
    };

    // Раскрывашка: заголовок со стрелкой («Подробнее»), содержимое показывается по клику. Владелец перекладывает окно в onToggle
    class DisclosureHeader final : public juce::Button
    {
    public:
        explicit DisclosureHeader (const juce::String& title) : juce::Button (title)
        {
            setClickingTogglesState (true);
            setWantsKeyboardFocus (true);
        }

        void paintButton (juce::Graphics& g, bool highlighted, bool) override
        {
            const bool open = getToggleState();
            juce::Path chevron;
            const float cx = 8.0f, cy = (float) getHeight() * 0.5f;
            if (open)
                chevron.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 3.0f);
            else
                chevron.addTriangle (cx - 2.0f, cy - 4.0f, cx - 2.0f, cy + 4.0f, cx + 3.0f, cy);
            g.setColour (highlighted ? brand::text : brand::textMuted);
            g.fillPath (chevron);
            g.setFont (fonts::strong (13.0f));
            g.drawText (getButtonText(), getLocalBounds().withTrimmedLeft (20), juce::Justification::centredLeft, true);
            if (hasKeyboardFocus (false))
            {
                g.setColour (brand::accentHover);
                g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), brand::radiusS, 2.0f);
            }
        }
    };

    // Значок-кнопка в шапке (настройки, справка): рисуется векторно, без внешних иконок. 32×32, подсказка обязательна
    class IconButton final : public juce::Button
    {
    public:
        enum class Icon { Gear, Help };

        IconButton (Icon i, const juce::String& tooltip) : juce::Button (tooltip), icon (i)
        {
            setTooltip (tooltip);
            setWantsKeyboardFocus (true);
        }

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const auto r = getLocalBounds().toFloat();
            if (highlighted || down)
            {
                g.setColour (down ? brand::raised2 : brand::pad);
                g.fillRoundedRectangle (r, brand::radiusM);
            }
            const auto c = r.getCentre();
            g.setColour (highlighted ? brand::text : brand::textMuted);
            if (icon == Icon::Gear)
            {
                g.drawEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre (c), 1.7f);
                for (int i = 0; i < 8; ++i)
                {
                    const float a = juce::MathConstants<float>::twoPi * (float) i / 8.0f;
                    g.drawLine (c.x + 6.6f * std::sin (a), c.y - 6.6f * std::cos (a),
                                c.x + 9.0f * std::sin (a), c.y - 9.0f * std::cos (a), 2.0f);
                }
            }
            else
            {
                g.drawEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre (c), 1.5f);
                g.setFont (fonts::strong (13.0f));
                g.drawText ("?", getLocalBounds(), juce::Justification::centred, false);
            }
            if (hasKeyboardFocus (false))
            {
                g.setColour (brand::accentHover);
                g.drawRoundedRectangle (r.reduced (1.0f), brand::radiusM, 2.0f);
            }
        }

    private:
        Icon icon;
    };
}
