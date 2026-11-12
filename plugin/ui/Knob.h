#pragma once

#include "../Brand.h"
#include "Fonts.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace samplit
{
    // Крутилка с подписью и значением. Жесты везде одни (docs/ui-guidelines.md §3):
    // тянуть вверх/вниз; Shift: точный шаг; двойной клик по крутилке: сброс; двойной клик по числу: ввод с клавиатуры;
    // колесо: малый шаг; правый клик: «Сбросить»; Tab и стрелки с видимым фокусом (стандарт JUCE Slider)
    class Knob final : public juce::Component,
                       public juce::SettableTooltipClient
    {
    public:
        enum class Size { Large, Small };   // диск 48 или 32 px

        Knob (const juce::String& labelText, Size s = Size::Large)
            : size (s), caption (labelText)
        {
            slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.2f, juce::MathConstants<float>::pi * 2.8f, true);
            slider.setMouseDragSensitivity (180);
            slider.setVelocityModeParameters (0.25, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
            slider.setScrollWheelEnabled (true);
            slider.setWantsKeyboardFocus (true);
            slider.onValueChange = [this] { updateValueText(); if (onChange) onChange(); };
            slider.onResetRequest = [this] { slider.setValue (slider.getDoubleClickReturnValue(), juce::sendNotificationSync); };
            addAndMakeVisible (slider);

            valueLabel.setJustificationType (juce::Justification::centred);
            valueLabel.setFont (fonts::mono (12.0f));
            valueLabel.setColour (juce::Label::textColourId, brand::text);
            valueLabel.setEditable (false, true, false);
            valueLabel.setColour (juce::Label::outlineWhenEditingColourId, brand::accentHover);
            valueLabel.onTextChange = [this]
            {
                const auto t = valueLabel.getText();
                const double v = parse ? parse (t) : t.getDoubleValue();
                slider.setValue (v, juce::sendNotificationSync);
                updateValueText();
            };
            addAndMakeVisible (valueLabel);
        }

        // Диапазон, значение по умолчанию (сброс) и шаг
        void setRange (double lo, double hi, double step, double resetValue)
        {
            slider.setRange (lo, hi, step);
            slider.setDoubleClickReturnValue (true, resetValue);
            updateValueText();
        }
        void setValue (double v, juce::NotificationType n = juce::dontSendNotification) { slider.setValue (v, n); updateValueText(); }
        double getValue() const { return slider.getValue(); }
        void setColour (juce::Colour c) { slider.setColour (juce::Slider::rotarySliderFillColourId, c); }
        void setLabel (const juce::String& t) { caption = t; repaint(); }

        // Как показывать значение («-3,5 дБ», «40 %») и как разбирать введённое число
        std::function<juce::String (double)> format = [] (double v) { return juce::String (v, 1); };
        std::function<double (const juce::String&)> parse;
        std::function<void()> onChange;
        void refresh() { updateValueText(); }

        // Размеры: диск + значение + подпись
        static int widthFor (Size s) { return s == Size::Large ? 72 : 60; }
        static int heightFor (Size s) { return (s == Size::Large ? 48 : 32) + 4 + 15 + 15; }
        int preferredWidth() const { return widthFor (size); }
        int preferredHeight() const { return heightFor (size); }

        void paint (juce::Graphics& g) override
        {
            g.setColour (brand::textMuted);
            g.setFont (fonts::text (12.0f));
            g.drawText (caption, getLocalBounds().removeFromBottom (15), juce::Justification::centred, true);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            const int d = size == Size::Large ? 48 : 32;
            slider.setBounds (r.removeFromTop (d).withSizeKeepingCentre (d, d));
            r.removeFromTop (4);
            valueLabel.setBounds (r.removeFromTop (15));
        }

        juce::Slider& getSlider() { return slider; }

    private:
        // Правый клик: меню «Сбросить»; остальное по умолчанию
        struct KnobSlider final : juce::Slider
        {
            void mouseDown (const juce::MouseEvent& e) override
            {
                if (e.mods.isPopupMenu())
                {
                    juce::PopupMenu m;
                    m.addItem (1, utf8 ("Сбросить"));
                    m.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea ({ e.getScreenX(), e.getScreenY(), 1, 1 }),
                                     [this] (int r) { if (r == 1 && onResetRequest) onResetRequest(); });
                    return;
                }
                juce::Slider::mouseDown (e);
            }
            std::function<void()> onResetRequest;
        };

        void updateValueText() { valueLabel.setText (format (slider.getValue()), juce::dontSendNotification); }

        Size size;
        juce::String caption;
        KnobSlider slider;
        juce::Label valueLabel;
    };
}
