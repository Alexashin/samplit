#include "GroupPanel.h"
#include "Brand.h"

namespace samplit
{
    namespace
    {
        constexpr int kRowHeight = 80;
        constexpr int kHeaderHeight = 38;
        constexpr int kFooterHeight = 48;
        const juce::Colour kWarning { 0xffffb454 };

        juce::String gainText (double v)
        {
            return (v > 0.0 ? "+" : "") + juce::String (v, 1) + " dB";
        }

        juce::String panText (double v)
        {
            const int amount = juce::roundToInt (std::abs (v) * 100.0);
            return amount == 0 ? juce::String ("C") : (v < 0.0 ? "L" : "R") + juce::String (amount);
        }

        void styleSlider (juce::Slider& s, double min, double max, double step)
        {
            s.setSliderStyle (juce::Slider::LinearHorizontal);
            s.setRange (min, max, step);
            s.setDoubleClickReturnValue (true, 0.0);
            s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 58, 22);
            s.setColour (juce::Slider::trackColourId, brand::accent);
            s.setColour (juce::Slider::backgroundColourId, brand::line);
            s.setColour (juce::Slider::thumbColourId, brand::text);
            s.setColour (juce::Slider::textBoxTextColourId, brand::text);
            s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        }

        void styleLabel (juce::Label& l, const juce::String& text)
        {
            l.setText (text, juce::dontSendNotification);
            l.setColour (juce::Label::textColourId, brand::textMuted);
            l.setFont (juce::FontOptions (12.0f));
            l.setInterceptsMouseClicks (false, false);
        }
    }

    // Одна строка списка. Все изменения уходят в процессор через GroupPanel::editGroup
    class GroupPanel::Row final : public juce::Component
    {
    public:
        Row (GroupPanel& o, int i) : owner (o), index (i)
        {
            name.setFont (juce::FontOptions (14.0f));
            name.setIndents (8, 5);
            name.setSelectAllWhenFocused (true);
            name.onReturnKey = [this] { commitName(); name.giveAwayKeyboardFocus(); };
            name.onFocusLost = [this] { commitName(); };
            name.onEscapeKey = [this] { name.giveAwayKeyboardFocus(); };

            output.onChange = [this]
            {
                if (updating)
                    return;
                const int value = output.getSelectedId() - 1;
                owner.editGroup (index, [value] (Group& g) { g.output = value; });
            };

            mode.addItem (utf8 ("Все слои"), 1);
            mode.addItem (utf8 ("По кругу"), 2);
            mode.addItem (utf8 ("Случайно"), 3);
            mode.onChange = [this]
            {
                if (updating)
                    return;
                const auto value = (PlayMode) (mode.getSelectedId() - 1);
                owner.editGroup (index, [value] (Group& g) { g.mode = value; });
            };

            tracking.setButtonText (utf8 ("Высота по клавише"));
            tracking.setColour (juce::ToggleButton::textColourId, brand::text);
            tracking.setColour (juce::ToggleButton::tickColourId, brand::accent);
            tracking.onClick = [this]
            {
                if (updating)
                    return;
                const bool value = tracking.getToggleState();
                owner.editGroup (index, [value] (Group& g) { g.tracking = value; });
            };

            styleSlider (gain, -24.0, 12.0, 0.1);
            gain.textFromValueFunction = gainText;
            gain.onValueChange = [this]
            {
                if (updating)
                    return;
                const auto value = (float) gain.getValue();
                owner.editGroup (index, [value] (Group& g) { g.gainDb = value; });
            };

            styleSlider (pan, -1.0, 1.0, 0.01);
            pan.textFromValueFunction = panText;
            pan.onValueChange = [this]
            {
                if (updating)
                    return;
                const auto value = (float) pan.getValue();
                owner.editGroup (index, [value] (Group& g) { g.pan = value; });
            };

            remove.setButtonText (juce::String::fromUTF8 ("\xC3\x97"));
            remove.onClick = [this] { owner.removeGroup (index); };

            styleLabel (gainLabel, utf8 ("Громкость"));
            styleLabel (panLabel, utf8 ("Панорама"));

            for (auto* c : std::initializer_list<juce::Component*> { &name, &output, &mode, &tracking, &gain, &pan,
                                                                     &remove, &gainLabel, &panLabel })
                addAndMakeVisible (c);

            // Клик по любому элементу строки выбирает группу
            addMouseListener (this, true);
        }

        void update (const Group& g, uint32_t enabledOutputs, bool isSelected, bool canRemove)
        {
            const juce::ScopedValueSetter<bool> guard (updating, true);
            selectedNow = isSelected;

            if (! name.hasKeyboardFocus (true))
                name.setText (g.name, juce::dontSendNotification);

            // Тексты выходов зависят от того, что включил хост, поэтому список пересобираем при смене маски
            if (! outputsBuilt || enabledOutputs != builtFor)
            {
                output.clear (juce::dontSendNotification);
                output.addItem ("Main", 1);
                for (int i = 1; i <= kNumOutputs; ++i)
                {
                    const bool on = (enabledOutputs & (1u << (i - 1))) != 0;
                    output.addItem ("Out " + juce::String (i) + (on ? juce::String() : utf8 ("  ·  выкл. в DAW")), i + 1);
                }
                outputsBuilt = true;
                builtFor = enabledOutputs;
            }
            output.setSelectedId (g.output + 1, juce::dontSendNotification);

            unavailable = g.output > 0 && (enabledOutputs & (1u << (g.output - 1))) == 0;
            output.setColour (juce::ComboBox::textColourId, unavailable ? kWarning : brand::text);
            output.setColour (juce::ComboBox::outlineColourId, unavailable ? kWarning : brand::line);

            mode.setSelectedId ((int) g.mode + 1, juce::dontSendNotification);
            tracking.setToggleState (g.tracking, juce::dontSendNotification);

            if (! gain.isMouseButtonDown())
                gain.setValue (g.gainDb, juce::dontSendNotification);
            if (! pan.isMouseButtonDown())
                pan.setValue (g.pan, juce::dontSendNotification);

            colour = brand::groupColour (index);
            remove.setEnabled (canRemove);
            repaint();
        }

        bool isOutputUnavailable() const noexcept { return unavailable; }

        void setIndex (int i) { index = i; }

        void mouseDown (const juce::MouseEvent&) override { owner.select (index); }

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat().reduced (2.0f, 2.0f);
            g.setColour (brand::pad);
            g.fillRoundedRectangle (r, 10.0f);
            if (selectedNow)
            {
                g.setColour (brand::accent);
                g.drawRoundedRectangle (r.reduced (0.75f), 10.0f, 1.5f);
            }
            g.setColour (colour);
            g.fillRoundedRectangle (r.getX() + 6.0f, r.getY() + 12.0f, 5.0f, 28.0f, 2.5f);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (14, 10);
            area.removeFromLeft (8);  // под цветную метку

            auto line1 = area.removeFromTop (28);
            remove.setBounds (line1.removeFromRight (28));
            line1.removeFromRight (8);
            mode.setBounds (line1.removeFromRight (112));
            line1.removeFromRight (8);
            output.setBounds (line1.removeFromRight (juce::jlimit (130, 190, line1.getWidth() / 2)));
            line1.removeFromRight (8);
            name.setBounds (line1);

            area.removeFromTop (8);
            auto line2 = area.removeFromTop (28);
            tracking.setBounds (line2.removeFromLeft (juce::jmin (170, line2.getWidth() / 3)));
            line2.removeFromLeft (8);
            const int half = line2.getWidth() / 2;
            auto left = line2.removeFromLeft (half);
            gainLabel.setBounds (left.removeFromLeft (64));
            gain.setBounds (left);
            panLabel.setBounds (line2.removeFromLeft (60));
            pan.setBounds (line2);
        }

    private:
        void commitName()
        {
            if (updating)
                return;
            const auto text = name.getText().trim();
            if (text.isEmpty())
                return;
            owner.editGroup (index, [text] (Group& g) { g.name = text; });
        }

        GroupPanel& owner;
        int index;
        bool updating = false;
        bool selectedNow = false;
        bool outputsBuilt = false;
        bool unavailable = false;
        uint32_t builtFor = 0;
        juce::Colour colour;

        juce::TextEditor name;
        juce::ComboBox output, mode;
        juce::ToggleButton tracking;
        juce::Slider gain, pan;
        juce::Label gainLabel, panLabel;
        juce::TextButton remove;
    };

    GroupPanel::GroupPanel (SamplitProcessor& p) : processor (p)
    {
        title.setText (utf8 ("Группы"), juce::dontSendNotification);
        title.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, brand::text);
        addAndMakeVisible (title);

        addButton.setButtonText (utf8 ("+ Группа"));
        addButton.onClick = [this] { addGroup(); };
        addAndMakeVisible (addButton);

        autoButton.setButtonText (utf8 ("Выходы по группам"));
        autoButton.onClick = [this] { autoRoute(); };
        addAndMakeVisible (autoButton);

        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);
        addAndMakeVisible (viewport);

        footer.setFont (juce::FontOptions (12.0f));
        footer.setJustificationType (juce::Justification::topLeft);
        footer.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (footer);

        processor.addChangeListener (this);
        startTimerHz (4);
        refresh();
    }

    GroupPanel::~GroupPanel()
    {
        processor.removeChangeListener (this);
    }

    void GroupPanel::timerCallback()
    {
        // Хост мог включить или выключить выходы: строки и подсказку надо обновить
        if (processor.getEnabledOutputMask() != shownOutputs)
            refresh();
    }

    void GroupPanel::paint (juce::Graphics& g)
    {
        g.setColour (brand::surface);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);
    }

    void GroupPanel::resized()
    {
        auto area = getLocalBounds().reduced (12, 8);
        auto header = area.removeFromTop (kHeaderHeight - 8);
        autoButton.setBounds (header.removeFromRight (150));
        header.removeFromRight (8);
        addButton.setBounds (header.removeFromRight (92));
        title.setBounds (header);

        area.removeFromTop (4);
        footer.setBounds (area.removeFromBottom (kFooterHeight));
        area.removeFromBottom (4);
        viewport.setBounds (area);

        const int width = viewport.getMaximumVisibleWidth();
        content.setSize (width, (int) rows.size() * kRowHeight);
        for (size_t i = 0; i < rows.size(); ++i)
            rows[i]->setBounds (0, (int) i * kRowHeight, width, kRowHeight);
    }

    void GroupPanel::setSelectedGroup (int group)
    {
        if (group != selected)
        {
            selected = group;
            refresh();
        }
    }

    void GroupPanel::select (int group)
    {
        if (group == selected)
            return;
        selected = group;
        refresh();
        if (onSelectGroup)
            onSelectGroup (group);
    }

    void GroupPanel::refresh()
    {
        instrument = processor.getInstrument();
        shownOutputs = processor.getEnabledOutputMask();
        const int n = instrument != nullptr ? (int) instrument->groups.size() : 0;
        const int oldSelected = selected;
        selected = n > 0 ? juce::jlimit (0, n - 1, selected) : 0;

        bool relayout = false;
        while ((int) rows.size() > n)
            rows.pop_back();
        while ((int) rows.size() < n)
        {
            rows.push_back (std::make_unique<Row> (*this, (int) rows.size()));
            content.addAndMakeVisible (*rows.back());
            relayout = true;
        }

        for (int i = 0; i < n; ++i)
            rows[(size_t) i]->update (instrument->groups[(size_t) i], shownOutputs, i == selected, n > 1);

        updateFooter();
        if (relayout || (int) rows.size() * kRowHeight != content.getHeight())
            resized();

        // Выбор сдвинулся из-за удаления группы: сообщаем остальным
        if (selected != oldSelected && onSelectGroup)
            onSelectGroup (selected);
    }

    void GroupPanel::updateFooter()
    {
        footerWarns = false;
        juce::String text = utf8 ("Выход группы: Main или Out N. В DAW Out N это отдельный стерео-канал, "
                                  "на него можно вывести каждую группу на свою дорожку.");

        // Предупреждаем о выбранной группе, а если с ней всё хорошо, о первой проблемной
        int bad = -1;
        if (instrument != nullptr)
        {
            const auto isBad = [&] (int i)
            {
                const int out = instrument->groups[(size_t) i].output;
                return out > 0 && (shownOutputs & (1u << (out - 1))) == 0;
            };
            if (juce::isPositiveAndBelow (selected, (int) instrument->groups.size()) && isBad (selected))
                bad = selected;
            else
                for (int i = 0; i < (int) instrument->groups.size() && bad < 0; ++i)
                    if (isBad (i))
                        bad = i;
        }

        if (bad >= 0)
        {
            const auto& g = instrument->groups[(size_t) bad];
            footerWarns = true;
            text = "Out " + juce::String (g.output) + utf8 (" выключен в DAW, группа «") + g.name
                   + utf8 ("» играет в Main. Cubase: окно VST Instruments, кнопка «Outputs» у Samplit, включите Out ")
                   + juce::String (g.output) + utf8 (". Reaper: кнопка Routing у дорожки.");
        }

        footer.setText (text, juce::dontSendNotification);
        footer.setColour (juce::Label::textColourId, footerWarns ? kWarning : brand::textMuted);
    }

    void GroupPanel::editGroup (int index, const std::function<void (Group&)>& change)
    {
        const auto inst = processor.getInstrument();
        if (inst == nullptr || ! juce::isPositiveAndBelow (index, (int) inst->groups.size()))
            return;
        auto g = inst->groups[(size_t) index];
        change (g);
        processor.setGroup (index, g);
    }

    void GroupPanel::addGroup()
    {
        const auto inst = processor.getInstrument();
        const int count = inst != nullptr ? (int) inst->groups.size() : 0;
        const int index = processor.addGroup (utf8 ("Группа ") + juce::String (count + 1));
        if (index >= 0)
        {
            selected = index;
            refresh();
            if (onSelectGroup)
                onSelectGroup (index);
            viewport.setViewPosition (0, juce::jmax (0, (index + 1) * kRowHeight - viewport.getHeight()));
        }
    }

    void GroupPanel::autoRoute()
    {
        const auto inst = processor.getInstrument();
        if (inst == nullptr)
            return;
        for (int i = 0; i < (int) inst->groups.size(); ++i)
        {
            auto g = inst->groups[(size_t) i];
            g.output = i < kNumOutputs ? i + 1 : 0;
            processor.setGroup (i, g);
        }
    }

    void GroupPanel::removeGroup (int index)
    {
        // Строка с нажатой кнопкой может быть удалена при обновлении списка, поэтому делаем это после клика
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<GroupPanel> (this), index]
        {
            auto* panel = safe.getComponent();
            if (panel == nullptr)
                return;
            panel->processor.removeGroup (index);
            if (panel->selected > index)
                --panel->selected;
            panel->refresh();
        });
    }
}
