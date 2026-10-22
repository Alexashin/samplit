#include "PadSettings.h"
#include "Brand.h"

namespace samplit
{
    PadSettings::PadSettings (SamplitProcessor& p) : processor (p)
    {
        setupKnob (gain, utf8 ("Громкость"), -24.0, 6.0, 0.1, 0.0, [] (double v)
        {
            return (v > 0.0 ? "+" : "") + juce::String (v, 1) + " dB";
        });
        setupKnob (pitch, utf8 ("Высота"), -12.0, 12.0, 1.0, 0.0, [] (double v)
        {
            const int st = juce::roundToInt (v);
            return (st > 0 ? "+" : "") + juce::String (st) + " st";
        });
        setupKnob (pan, utf8 ("Панорама"), -1.0, 1.0, 0.01, 0.0, [] (double v)
        {
            const int amount = juce::roundToInt (std::abs (v) * 100.0);
            return amount == 0 ? juce::String ("C") : (v < 0.0 ? "L" : "R") + juce::String (amount);
        });

        processor.addChangeListener (this);
        pullFromProcessor();
    }

    PadSettings::~PadSettings()
    {
        processor.removeChangeListener (this);
    }

    void PadSettings::setupKnob (Knob& k, const juce::String& name, double min, double max, double step, double def,
                                 std::function<juce::String (double)> format)
    {
        k.slider.setRange (min, max, step);
        k.slider.setDoubleClickReturnValue (true, def);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 72, 18);
        k.slider.textFromValueFunction = std::move (format);
        k.slider.setColour (juce::Slider::rotarySliderFillColourId, brand::accent);
        k.slider.setColour (juce::Slider::rotarySliderOutlineColourId, brand::pad);
        k.slider.setColour (juce::Slider::thumbColourId, brand::text);
        k.slider.setColour (juce::Slider::textBoxTextColourId, brand::text);
        k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        k.slider.onValueChange = [this] { pushToProcessor(); };
        addAndMakeVisible (k.slider);

        k.label.setText (name, juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        k.label.setColour (juce::Label::textColourId, brand::textMuted);
        k.label.setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (k.label);
    }

    void PadSettings::setPad (int newPad)
    {
        pad = newPad;
        pullFromProcessor();
        repaint();
    }

    void PadSettings::pushToProcessor()
    {
        if (pulling)
            return;
        processor.setPadParams (pad, { (float) gain.slider.getValue(), (float) pitch.slider.getValue(),
                                       (float) pan.slider.getValue() });
    }

    void PadSettings::pullFromProcessor()
    {
        // Значения пришли из процессора (другой пэд, загрузка проекта): обратно не отправляем
        const juce::ScopedValueSetter<bool> svs (pulling, true);
        const auto params = processor.getPadParams (pad);
        gain.slider.setValue (params.gainDb, juce::dontSendNotification);
        pitch.slider.setValue (params.pitch, juce::dontSendNotification);
        pan.slider.setValue (params.pan, juce::dontSendNotification);
        for (auto* k : { &gain, &pitch, &pan })
            k->slider.updateText();
    }

    void PadSettings::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        if (! gain.slider.isMouseButtonDown() && ! pitch.slider.isMouseButtonDown() && ! pan.slider.isMouseButtonDown())
            pullFromProcessor();
        repaint (titleArea);
    }

    void PadSettings::resized()
    {
        auto area = getLocalBounds().reduced (16, 12);
        titleArea = area.removeFromLeft (juce::jmin (220, area.getWidth() / 3));

        const int w = area.getWidth() / 3;
        for (auto* k : { &gain, &pitch, &pan })
        {
            auto cell = area.removeFromLeft (w);
            k->label.setBounds (cell.removeFromBottom (16));
            k->slider.setBounds (cell);
        }
    }

    void PadSettings::paint (juce::Graphics& g)
    {
        g.setColour (brand::surface);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);

        auto t = titleArea;
        g.setColour (brand::text);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (utf8 ("Пэд ") + juce::String (pad + 1), t.removeFromTop (24), juce::Justification::topLeft);

        const auto file = processor.getPadFile (pad);
        g.setColour (brand::textMuted);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (file == juce::File() ? utf8 ("пусто") : file.getFileNameWithoutExtension(),
                    t.removeFromTop (20), juce::Justification::topLeft);

        g.setFont (juce::FontOptions (11.0f));
        const bool own = (processor.getSeparateOutMask() & (1u << pad)) != 0;
        g.drawText (own ? utf8 ("выход Out ") + juce::String (pad + 1) : utf8 ("выход Main"),
                    t.removeFromTop (18), juce::Justification::topLeft);
    }
}
