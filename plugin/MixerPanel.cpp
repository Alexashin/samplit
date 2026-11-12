#include "MixerPanel.h"
#include "Brand.h"
#include "Studio600Names.h"

namespace samplit
{
    namespace
    {
        constexpr int kHeaderHeight = 38;
        constexpr int kStripWidth = 86;
        constexpr int kStripGap = 6;
        const juce::Colour kWarning { 0xffffb454 };

        juce::String levelText (double v)
        {
            if (v <= -60.0)
                return utf8 ("−∞ dB");
            return (v > 0.0 ? "+" : "") + juce::String (v, 1) + " dB";
        }

        juce::String panText (double v)
        {
            const int amount = juce::roundToInt (std::abs (v) * 100.0);
            return amount == 0 ? juce::String ("C") : (v < 0.0 ? "L" : "R") + juce::String (amount);
        }
    }

    // Линейка одного микрофона. Всё изменённое сразу уходит в процессор
    class MixerPanel::Strip final : public juce::Component
    {
    public:
        Strip (SamplitProcessor& p, int m, const Mic& mic) : processor (p), index (m)
        {
            const auto micName = juce::String::fromUTF8 (mic.name.c_str());
            name.setText (micName, juce::dontSendNotification);
            name.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            name.setJustificationType (juce::Justification::centred);
            name.setMinimumHorizontalScale (0.65f);
            name.setTooltip (micName);

            output.setTitle (utf8 ("Выход: ") + micName);
            output.setTooltip (utf8 ("Куда идёт микрофон: Main или отдельный моно-выход"));
            output.onChange = [this]
            {
                if (! updating)
                    edit ([v = output.getSelectedId() - 1] (SamplitProcessor::MicState& s) { s.out = v; });
            };

            pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            pan.setRange (-1.0, 1.0, 0.01);
            pan.setDoubleClickReturnValue (true, 0.0);
            pan.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 54, 18);
            pan.textFromValueFunction = panText;
            pan.setTitle (utf8 ("Панорама: ") + micName);
            pan.setTooltip (utf8 ("Панорама в Main. На отдельном выходе микрофон моно. Двойной клик: центр"));
            pan.onValueChange = [this]
            {
                if (! updating)
                    edit ([v = (float) pan.getValue()] (SamplitProcessor::MicState& s) { s.pan = v; });
            };

            level.setSliderStyle (juce::Slider::LinearVertical);
            level.setRange (-60.0, 12.0, 0.1);
            level.setSkewFactorFromMidPoint (-12.0);
            level.setDoubleClickReturnValue (true, 0.0);
            level.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 20);
            level.textFromValueFunction = levelText;
            level.setTitle (utf8 ("Уровень: ") + micName);
            level.setTooltip (utf8 ("Уровень микрофона, дБ. Двойной клик: 0 дБ"));
            level.onValueChange = [this]
            {
                if (! updating)
                    edit ([v = (float) level.getValue()] (SamplitProcessor::MicState& s) { s.levelDb = v; });
            };

            // Жест мыши = gesture параметра хоста (Cubase Write/Touch)
            pan.onDragStart = [this] { processor.beginMicGesture (index, true); };
            pan.onDragEnd = [this] { processor.endMicGesture (index, true); };
            level.onDragStart = [this] { processor.beginMicGesture (index, false); };
            level.onDragEnd = [this] { processor.endMicGesture (index, false); };

            for (auto* s : { &pan, &level })
            {
                s->setColour (juce::Slider::trackColourId, brand::accent);
                s->setColour (juce::Slider::backgroundColourId, brand::line);
                s->setColour (juce::Slider::rotarySliderFillColourId, brand::accent);
                s->setColour (juce::Slider::rotarySliderOutlineColourId, brand::line);
                s->setColour (juce::Slider::thumbColourId, brand::text);
                s->setColour (juce::Slider::textBoxTextColourId, brand::text);
                s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            }

            mute.setButtonText ("M");
            mute.setClickingTogglesState (true);
            mute.setTitle (utf8 ("Mute: ") + micName);
            mute.setTooltip (utf8 ("Заглушить микрофон"));
            mute.setColour (juce::TextButton::buttonOnColourId, kWarning);
            mute.onClick = [this] { edit ([v = mute.getToggleState()] (SamplitProcessor::MicState& s) { s.mute = v; }); };

            solo.setButtonText ("S");
            solo.setClickingTogglesState (true);
            solo.setTitle (utf8 ("Solo: ") + micName);
            solo.setTooltip (utf8 ("Слушать только микрофоны с Solo"));
            solo.onClick = [this] { edit ([v = solo.getToggleState()] (SamplitProcessor::MicState& s) { s.solo = v; }); };

            routeNote.setFont (juce::FontOptions (11.0f));
            routeNote.setJustificationType (juce::Justification::centred);
            routeNote.setColour (juce::Label::textColourId, kWarning);
            routeNote.setText (utf8 ("→ Main"), juce::dontSendNotification);
            routeNote.setTooltip (utf8 ("Этот выход не включён в DAW. Включите его в настройках выходов инструмента"));

            for (auto* c : std::initializer_list<juce::Component*> { &name, &output, &pan, &level, &mute, &solo, &routeNote })
                addAndMakeVisible (c);

            // Текст полей посчитан до того, как заданы функции форматирования
            pan.updateText();
            level.updateText();
        }

        // anyEnabled: DAW включила хоть один отдельный выход. Если ни одного, предупреждение одно на весь микшер
        void update (uint32_t enabledOutputs, bool multiOut)
        {
            const juce::ScopedValueSetter<bool> guard (updating, true);
            const auto s = processor.getMicState (index);

            // В списке выходы разделены на включённые и выключенные в DAW: так короткое имя влезает в линейку
            if (! outputsBuilt || enabledOutputs != builtFor)
            {
                output.clear (juce::dontSendNotification);
                output.addItem ("Main", 1);
                for (const bool on : { true, false })
                {
                    bool heading = false;
                    for (int i = 1; i <= kNumOutputs; ++i)
                    {
                        if (((enabledOutputs & (1u << (i - 1))) != 0) != on)
                            continue;
                        if (! heading)
                            output.addSectionHeading (on ? utf8 ("Включены в DAW") : utf8 ("Выключены в DAW, играют в Main"));
                        heading = true;
                        output.addItem (juce::String::fromUTF8 (studio600OutputName (i)), i + 1);
                    }
                }
                outputsBuilt = true;
                builtFor = enabledOutputs;
            }
            output.setSelectedId (s.out + 1, juce::dontSendNotification);

            output.setEnabled (multiOut);
            const bool unavailable = multiOut && s.out > 0 && (enabledOutputs & (1u << (s.out - 1))) == 0;
            const bool warn = unavailable && enabledOutputs != 0;   // в режиме «Стерео» выход не используется
            output.setColour (juce::ComboBox::textColourId, warn ? kWarning : brand::text);
            output.setColour (juce::ComboBox::outlineColourId, warn ? kWarning : brand::line);
            routeNote.setColour (juce::Label::textColourId, warn ? kWarning : brand::textMuted);
            routeNote.setVisible (unavailable);

            if (! pan.isMouseButtonDown())
                pan.setValue (s.pan, juce::dontSendNotification);
            if (! level.isMouseButtonDown())
                level.setValue (s.levelDb, juce::dontSendNotification);
            mute.setToggleState (s.mute, juce::dontSendNotification);
            solo.setToggleState (s.solo, juce::dontSendNotification);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (brand::pad);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 10.0f);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (5, 8);
            name.setBounds (area.removeFromTop (18));
            area.removeFromTop (6);
            output.setBounds (area.removeFromTop (24));
            routeNote.setBounds (area.removeFromTop (16));
            area.removeFromTop (2);
            pan.setBounds (area.removeFromTop (70));
            area.removeFromTop (6);

            auto buttons = area.removeFromBottom (26);
            mute.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (2, 0));
            solo.setBounds (buttons.reduced (2, 0));
            area.removeFromBottom (8);
            level.setBounds (area);
        }

    private:
        void edit (const std::function<void (SamplitProcessor::MicState&)>& change)
        {
            auto s = processor.getMicState (index);
            change (s);
            processor.setMicState (index, s, true);
        }

        SamplitProcessor& processor;
        int index;
        bool updating = false;
        bool outputsBuilt = false;
        uint32_t builtFor = 0;

        juce::Label name, routeNote;
        juce::ComboBox output;
        juce::Slider pan, level;
        juce::TextButton mute, solo;
    };

    MixerPanel::MixerPanel (SamplitProcessor& p) : processor (p)
    {
        title.setText (utf8 ("Микрофоны"), juce::dontSendNotification);
        title.setFont (juce::FontOptions (15.0f, juce::Font::bold));

        hint.setFont (juce::FontOptions (12.0f));
        hint.setJustificationType (juce::Justification::centredRight);
        hint.setMinimumHorizontalScale (0.8f);

        emptyText.setText (utf8 ("Выберите пресет в шапке окна, и здесь появятся микрофоны установки."), juce::dontSendNotification);
        emptyText.setColour (juce::Label::textColourId, brand::textMuted);
        emptyText.setFont (juce::FontOptions (13.0f));
        emptyText.setJustificationType (juce::Justification::centred);

        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (false, true);

        modeLabel.setText (utf8 ("Выходы:"), juce::dontSendNotification);
        modeLabel.setFont (juce::FontOptions (12.0f));
        modeLabel.setColour (juce::Label::textColourId, brand::textMuted);
        mode.addItem (utf8 ("Стерео"), 1);
        mode.addItem (utf8 ("Мульти-аут"), 2);
        mode.setTooltip (utf8 ("Стерео: всё играет в Main, панорама на линейках. Мульти-аут: каждый микрофон на свой выход DAW"));
        mode.onChange = [this] { processor.setMultiOut (mode.getSelectedId() == 2); };

        for (auto* c : std::initializer_list<juce::Component*> { &title, &hint, &emptyText, &viewport, &modeLabel, &mode })
            addAndMakeVisible (c);

        processor.addChangeListener (this);
        rebuild();
        startTimerHz (4);
    }

    MixerPanel::~MixerPanel()
    {
        processor.removeChangeListener (this);
    }

    void MixerPanel::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        if (processor.getKit() != kit)
            rebuild();
        else
            updateStrips();
    }

    void MixerPanel::timerCallback()
    {
        if (processor.getEnabledOutputMask() != shownOutputs || processor.isMultiOut() != shownMulti)
            updateStrips();
    }

    void MixerPanel::rebuild()
    {
        kit = processor.getKit();
        strips.clear();
        if (kit != nullptr)
            for (int i = 0; i < (int) kit->mics.size(); ++i)
            {
                if (kit->mics[(size_t) i].id.empty())
                    continue;       // слот без микрофона в этом ките
                strips.push_back (std::make_unique<Strip> (processor, i, kit->mics[(size_t) i]));
                content.addAndMakeVisible (*strips.back());
            }

        if (kit != nullptr)
            title.setText (utf8 ("Микрофоны  ·  ") + juce::String::fromUTF8 (kit->title.c_str()), juce::dontSendNotification);
        else
            title.setText (utf8 ("Микрофоны"), juce::dontSendNotification);

        emptyText.setVisible (strips.empty());
        hint.setVisible (! strips.empty());
        updateStrips();
        resized();
    }

    void MixerPanel::updateStrips()
    {
        shownOutputs = processor.getEnabledOutputMask();
        shownMulti = processor.isMultiOut();
        mode.setSelectedId (shownMulti ? 2 : 1, juce::dontSendNotification);
        if (! shownMulti)
        {
            hint.setText (utf8 ("Всё играет в Main. Для раздельных дорожек в DAW выберите «Мульти-аут»"), juce::dontSendNotification);
            hint.setColour (juce::Label::textColourId, brand::textMuted);
        }
        else if (shownOutputs == 0)
        {
            hint.setText (utf8 ("Отдельные выходы в DAW выключены: все микрофоны играют в Main"), juce::dontSendNotification);
            hint.setColour (juce::Label::textColourId, kWarning);
        }
        else
        {
            hint.setText (utf8 ("Выход, выключенный в DAW, играет в Main"), juce::dontSendNotification);
            hint.setColour (juce::Label::textColourId, brand::textMuted);
        }
        for (auto& s : strips)
            s->update (shownOutputs, shownMulti);
    }

    void MixerPanel::paint (juce::Graphics& g)
    {
        g.setColour (brand::surface);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);
    }

    void MixerPanel::resized()
    {
        auto area = getLocalBounds().reduced (14, 10);
        auto header = area.removeFromTop (kHeaderHeight - 10);
        title.setBounds (header.removeFromLeft (juce::jmax (220, header.getWidth() * 2 / 5)));
        title.setTooltip (title.getText());
        mode.setBounds (header.removeFromRight (120).reduced (0, 2));
        modeLabel.setBounds (header.removeFromRight (62));
        header.removeFromRight (8);
        hint.setBounds (header);
        area.removeFromTop (8);

        emptyText.setBounds (area);
        viewport.setBounds (area);

        // Линейки на всю высоту; не влезают по ширине — появляется горизонтальная прокрутка
        const int n = (int) strips.size();
        const int scrollBar = n * (kStripWidth + kStripGap) > area.getWidth() ? viewport.getScrollBarThickness() : 0;
        const int height = area.getHeight() - scrollBar;
        content.setSize (juce::jmax (area.getWidth(), n * (kStripWidth + kStripGap) - kStripGap), height);
        for (int i = 0; i < n; ++i)
            strips[(size_t) i]->setBounds (i * (kStripWidth + kStripGap), 0, kStripWidth, height);
    }
}
