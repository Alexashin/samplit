#pragma once

#include "../Brand.h"
#include "Fonts.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace samplit
{
    // Вертикальный фейдер для микшера: паз, ручка с риской, шкала дБ слева, значение сверху.
    // Фейдеры только в микшере, везде вне микшера крутилки (docs/ui-guidelines.md). Жесты как у Knob
    class Fader final : public juce::Component
    {
    public:
        Fader()
        {
            slider.setSliderStyle (juce::Slider::LinearVertical);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
            slider.setVelocityModeParameters (0.25, 1, 0.0, true, juce::ModifierKeys::shiftModifier);
            slider.setScrollWheelEnabled (true);
            slider.setWantsKeyboardFocus (true);
            slider.onValueChange = [this] { updateValueText(); if (onChange) onChange(); };
            addAndMakeVisible (slider);

            valueLabel.setJustificationType (juce::Justification::centred);
            valueLabel.setFont (fonts::mono (12.0f));
            valueLabel.setEditable (false, true, false);
            valueLabel.onTextChange = [this]
            {
                const auto t = valueLabel.getText();
                slider.setValue (parse ? parse (t) : t.getDoubleValue(), juce::sendNotificationSync);
                updateValueText();
            };
            addAndMakeVisible (valueLabel);
        }

        // Диапазон в дБ: например -60…+6, сброс 0 дБ; deadzone нижнего края считается «−∞» через format
        void setRange (double lo, double hi, double resetValue)
        {
            slider.setRange (lo, hi, 0.1);
            slider.setDoubleClickReturnValue (true, resetValue);
            updateValueText();
        }
        void setValue (double v, juce::NotificationType n = juce::dontSendNotification) { slider.setValue (v, n); updateValueText(); }
        double getValue() const { return slider.getValue(); }

        std::function<juce::String (double)> format = [] (double v) { return v <= -59.9 ? juce::String (utf8 ("−∞")) : juce::String (v, 1); };
        std::function<double (const juce::String&)> parse;
        std::function<void()> onChange;

        void paint (juce::Graphics& g) override
        {
            // Шкала: засечки слева от паза
            const auto s = slider.getBounds();
            const float top = (float) s.getY() + 7.0f, bottom = (float) s.getBottom() - 7.0f;
            const double lo = slider.getMinimum(), hi = slider.getMaximum();
            g.setFont (fonts::mono (10.0f));
            for (double db : { 6.0, 0.0, -6.0, -12.0, -24.0, -48.0 })
            {
                if (db < lo || db > hi)
                    continue;
                const float y = bottom - (float) ((db - lo) / (hi - lo)) * (bottom - top);
                g.setColour (db == 0.0 ? brand::textMuted : brand::lineStrong);
                g.fillRect (juce::Rectangle<float> (db == 0.0 ? 5.0f : 3.0f, 1.0f).withCentre ({ (float) s.getX() - 3.0f, y }));
                if (db == 0.0 || db == -12.0 || db == -48.0)
                {
                    g.setColour (brand::textFaint);
                    g.drawText (juce::String ((int) db), juce::Rectangle<float> (20.0f, 12.0f).withCentre ({ (float) s.getX() - 14.0f, y }).toNearestInt(),
                                juce::Justification::centredRight, false);
                }
            }
        }

        void resized() override
        {
            auto r = getLocalBounds();
            valueLabel.setBounds (r.removeFromTop (16));
            r.removeFromTop (2);
            slider.setBounds (r.withTrimmedLeft (28));
        }

        juce::Slider& getSlider() { return slider; }

    private:
        void updateValueText() { valueLabel.setText (format (slider.getValue()), juce::dontSendNotification); }

        juce::Slider slider;
        juce::Label valueLabel;
    };
}
