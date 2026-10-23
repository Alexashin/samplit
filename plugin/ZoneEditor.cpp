#include "ZoneEditor.h"
#include "Brand.h"

namespace samplit
{
    namespace
    {
        constexpr int kRowHeight = 50;

        juce::String gainText (double v)
        {
            return (v > 0.0 ? "+" : "") + juce::String (v, 1) + " dB";
        }

        void styleBar (juce::Slider& s, double min, double max, double step, double def)
        {
            s.setSliderStyle (juce::Slider::LinearBar);
            s.setRange (min, max, step);
            s.setDoubleClickReturnValue (true, def);
            s.setTextBoxIsEditable (true);
            s.setColour (juce::Slider::trackColourId, brand::accentSoft);
            s.setColour (juce::Slider::backgroundColourId, brand::bg);
            s.setColour (juce::Slider::textBoxTextColourId, brand::text);
            s.setColour (juce::Slider::textBoxOutlineColourId, brand::line);
        }
    }

    // Строка таблицы: одна зона
    class ZoneEditor::RowComponent final : public juce::Component
    {
    public:
        explicit RowComponent (ZoneEditor& o) : owner (o)
        {
            styleBar (lowVel, 1, 127, 1, 1);
            styleBar (highVel, 1, 127, 1, 127);
            styleBar (root, 0, 127, 1, 60);
            root.textFromValueFunction = [] (double v) { return noteName (juce::roundToInt (v)); };
            styleBar (gain, -24, 12, 0.1, 0);
            gain.textFromValueFunction = gainText;

            lowVel.onValueChange = [this]
            {
                if (updating)
                    return;
                const int v = (int) lowVel.getValue();
                owner.editZone (zoneIndex, [v] (Zone& z) { z.lowVel = v; z.highVel = juce::jmax (z.highVel, v); });
            };
            highVel.onValueChange = [this]
            {
                if (updating)
                    return;
                const int v = (int) highVel.getValue();
                owner.editZone (zoneIndex, [v] (Zone& z) { z.highVel = v; z.lowVel = juce::jmin (z.lowVel, v); });
            };
            root.onValueChange = [this]
            {
                if (updating)
                    return;
                const int v = (int) root.getValue();
                owner.editZone (zoneIndex, [v] (Zone& z) { z.rootKey = v; });
            };
            gain.onValueChange = [this]
            {
                if (updating)
                    return;
                const auto v = (float) gain.getValue();
                owner.editZone (zoneIndex, [v] (Zone& z) { z.gainDb = v; });
            };

            play.setButtonText (juce::String::fromUTF8 ("\xE2\x96\xB6"));
            play.onClick = [this] { owner.processor.previewFile (file); };

            remove.setButtonText (juce::String::fromUTF8 ("\xC3\x97"));
            remove.onClick = [this]
            {
                // Строка исчезнет при обновлении списка, поэтому удаляем после клика
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ZoneEditor> (&owner), index = zoneIndex]
                {
                    if (auto* editor = safe.getComponent())
                        editor->processor.removeZone (index);
                });
            };

            for (auto* c : std::initializer_list<juce::Component*> { &lowVel, &highVel, &root, &gain, &play, &remove })
                addAndMakeVisible (c);
        }

        void update (int index, const Zone& z, const juce::String& groupName, bool inSelectedGroup)
        {
            const juce::ScopedValueSetter<bool> guard (updating, true);
            zoneIndex = index;
            file = z.file;
            loaded = z.sample != nullptr;
            group = z.group;
            groupLabel = groupName;
            bright = inSelectedGroup;
            missing = ! z.file.existsAsFile();

            if (! lowVel.isMouseButtonDown())
                lowVel.setValue (z.lowVel, juce::dontSendNotification);
            if (! highVel.isMouseButtonDown())
                highVel.setValue (z.highVel, juce::dontSendNotification);
            if (! root.isMouseButtonDown())
                root.setValue (z.rootKey, juce::dontSendNotification);
            if (! gain.isMouseButtonDown())
                gain.setValue (z.gainDb, juce::dontSendNotification);

            setAlpha (inSelectedGroup ? 1.0f : 0.6f);
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat().reduced (2.0f, 2.0f);
            g.setColour (brand::pad);
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (brand::groupColour (group));
            g.fillRoundedRectangle (r.getX() + 5.0f, r.getY() + 8.0f, 4.0f, r.getHeight() - 16.0f, 2.0f);

            auto text = getLocalBounds().reduced (10, 6);
            text.removeFromLeft (10);
            text.setRight (lowVel.getX() - 8);

            g.setColour (brand::text);
            g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
            g.drawText (file.getFileNameWithoutExtension(), text.removeFromTop (text.getHeight() / 2 + 2),
                        juce::Justification::bottomLeft);

            const bool warn = ! loaded;
            g.setColour (warn ? juce::Colour (0xffffb454) : brand::textMuted);
            g.setFont (juce::FontOptions (11.0f));
            const auto status = warn ? (missing ? utf8 ("файл не найден") : utf8 ("не загружен")) : groupLabel;
            g.drawText (status, text, juce::Justification::topLeft);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (10, 9);
            remove.setBounds (area.removeFromRight (kButton));
            area.removeFromRight (kGap);
            play.setBounds (area.removeFromRight (kButton));
            area.removeFromRight (kGap);
            gain.setBounds (area.removeFromRight (kGainWidth));
            area.removeFromRight (kGap);
            root.setBounds (area.removeFromRight (kRootWidth));
            area.removeFromRight (kGap);
            highVel.setBounds (area.removeFromRight (kVelWidth));
            area.removeFromRight (kGap);
            lowVel.setBounds (area.removeFromRight (kVelWidth));
        }

    private:
        ZoneEditor& owner;
        int zoneIndex = -1;
        int group = 0;
        bool updating = false, loaded = false, bright = true, missing = false;
        juce::File file;
        juce::String groupLabel;

        juce::Slider lowVel, highVel, root, gain;
        juce::TextButton play, remove;
    };

    ZoneEditor::ZoneEditor (SamplitProcessor& p) : processor (p)
    {
        splitButton.setButtonText (utf8 ("Разделить velocity поровну"));
        splitButton.onClick = [this] { splitVelocity(); };
        addAndMakeVisible (splitButton);

        clearButton.setButtonText (utf8 ("Очистить клавишу"));
        clearButton.onClick = [this] { clearKey(); };
        addAndMakeVisible (clearButton);

        list.setRowHeight (kRowHeight);
        list.setOutlineThickness (0);
        list.getVerticalScrollBar().setAutoHide (false);
        addAndMakeVisible (list);

        processor.addChangeListener (this);
        refresh();
    }

    ZoneEditor::~ZoneEditor()
    {
        processor.removeChangeListener (this);
    }

    void ZoneEditor::setKey (int newKey)
    {
        key = newKey;
        refresh();
    }

    void ZoneEditor::setGroup (int newGroup)
    {
        group = newGroup;
        refresh();
    }

    void ZoneEditor::refresh()
    {
        instrument = processor.getInstrument();
        rows.clear();
        if (instrument != nullptr)
            for (int i = 0; i < (int) instrument->zones.size(); ++i)
                if (key >= instrument->zones[(size_t) i].lowKey && key <= instrument->zones[(size_t) i].highKey)
                    rows.push_back (i);

        // Выбранная группа первой: вариации, куда сейчас кладём файлы, всегда сверху
        std::stable_sort (rows.begin(), rows.end(), [this] (int a, int b)
        {
            return (instrument->zones[(size_t) a].group == group) > (instrument->zones[(size_t) b].group == group);
        });

        int inGroup = 0;
        for (int i : rows)
            if (instrument->zones[(size_t) i].group == group)
                ++inGroup;
        splitButton.setEnabled (inGroup > 1);
        clearButton.setEnabled (inGroup > 0);

        list.updateContent();
        list.setVisible (! rows.empty());
        repaint();
    }

    juce::Component* ZoneEditor::refreshComponentForRow (int row, bool, juce::Component* existing)
    {
        if (! juce::isPositiveAndBelow (row, (int) rows.size()) || instrument == nullptr)
        {
            delete existing;
            return nullptr;
        }

        auto* comp = dynamic_cast<RowComponent*> (existing);
        if (comp == nullptr)
        {
            delete existing;
            comp = new RowComponent (*this);
        }

        const int index = rows[(size_t) row];
        const auto& zone = instrument->zones[(size_t) index];
        const auto groupName = juce::isPositiveAndBelow (zone.group, (int) instrument->groups.size())
                                   ? instrument->groups[(size_t) zone.group].name
                                   : juce::String();
        comp->update (index, zone, groupName, zone.group == group);
        return comp;
    }

    void ZoneEditor::editZone (int zoneIndex, const std::function<void (Zone&)>& change)
    {
        const auto inst = processor.getInstrument();
        if (inst == nullptr || ! juce::isPositiveAndBelow (zoneIndex, (int) inst->zones.size()))
            return;
        auto z = inst->zones[(size_t) zoneIndex];
        change (z);
        processor.setZone (zoneIndex, z);
    }

    void ZoneEditor::splitVelocity()
    {
        // Зоны выбранной группы на этой клавише делят 1..127 на равные полосы по порядку в таблице
        std::vector<int> mine;
        for (int i : rows)
            if (instrument->zones[(size_t) i].group == group)
                mine.push_back (i);

        const int n = (int) mine.size();
        for (int i = 0; i < n; ++i)
        {
            const int low = 1 + (i * 127) / n;
            const int high = ((i + 1) * 127) / n;
            editZone (mine[(size_t) i], [low, high] (Zone& z) { z.lowVel = low; z.highVel = high; });
        }
    }

    void ZoneEditor::clearKey()
    {
        processor.clearKey (group, key);
    }

    void ZoneEditor::resized()
    {
        auto area = getLocalBounds().reduced (12, 8);
        auto top = area.removeFromTop (30);
        clearButton.setBounds (top.removeFromRight (140));
        top.removeFromRight (8);
        splitButton.setBounds (top.removeFromRight (190));
        titleArea = top;

        area.removeFromTop (6);
        headerArea = area.removeFromTop (18);
        emptyArea = area;
        list.setBounds (area);
    }

    void ZoneEditor::paint (juce::Graphics& g)
    {
        g.setColour (brand::surface);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);

        const int count = (int) rows.size();
        g.setColour (brand::text);
        g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
        g.drawText (utf8 ("Клавиша ") + noteName (key) + juce::String ("  ·  ") + juce::String (count) + utf8 (" зон"),
                    titleArea, juce::Justification::centredLeft);

        if (rows.empty())
        {
            g.setColour (brand::textMuted);
            g.setFont (juce::FontOptions (13.0f));
            g.drawFittedText (utf8 ("На клавише пусто. Перетащите на неё файл или папку: вариации выбираются по velocity "
                                    "и режиму группы. С Alt файлы ложатся слоями по velocity."),
                              emptyArea.reduced (24, 12), juce::Justification::centred, 3);
            return;
        }

        // Шапка колонок: тот же расчёт справа налево, что и в строках (отступ строки 12 + 10)
        auto h = headerArea;
        h.removeFromRight (list.getViewport()->getScrollBarThickness() + 10);
        g.setColour (brand::textMuted);
        g.setFont (juce::FontOptions (11.0f));
        const auto column = [&] (int width, const juce::String& text)
        {
            g.drawText (text, h.removeFromRight (width), juce::Justification::centred);
            h.removeFromRight (kGap);
        };
        column (kButton, {});
        column (kButton, {});
        column (kGainWidth, utf8 ("Громкость"));
        column (kRootWidth, utf8 ("Нота"));
        column (kVelWidth, utf8 ("Vel до"));
        column (kVelWidth, utf8 ("Vel от"));
        g.drawText (utf8 ("Сэмпл"), h.withTrimmedLeft (22), juce::Justification::centredLeft);
    }
}
