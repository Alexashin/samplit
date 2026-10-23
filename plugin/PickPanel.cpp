#include "PickPanel.h"
#include "Brand.h"

namespace samplit
{
    namespace
    {
        constexpr int kHeaderHeight = 38;
        constexpr int kRowHeight = 40;
        constexpr int kRowGap = 6;
        constexpr int kColumnGap = 12;
        constexpr int kComboWidth = 150;
        constexpr int kTwoColumnsFrom = 640;

        // Id пунктов списка: Pick + 1
        juce::String pickTooltip (Pick p)
        {
            switch (p)
            {
                case Pick::Random: return utf8 ("Случайный удар из слоя, без повтора подряд");
                case Pick::Cycle:  return utf8 ("Удары слоя по очереди, с первого до последнего, потом сначала");
                case Pick::Exact:  return utf8 ("Velocity выбирает конкретный удар, как в Kontakt");
            }
            return {};
        }
    }

    // Строка: название группы клавиш и список режимов. Изменение сразу уходит в процессор
    class PickPanel::Row final : public juce::Component
    {
    public:
        Row (SamplitProcessor& p, int c, const PickChoice& choice) : processor (p), index (c)
        {
            const auto text = juce::String::fromUTF8 (choice.name.c_str());
            name.setText (text, juce::dontSendNotification);
            name.setFont (juce::FontOptions (13.0f));
            name.setMinimumHorizontalScale (0.75f);
            name.setTooltip (text);

            mode.addItem (utf8 ("Случайно"), (int) Pick::Random + 1);
            mode.addItem (utf8 ("По кругу"), (int) Pick::Cycle + 1);
            mode.addItem (utf8 ("По velocity"), (int) Pick::Exact + 1);
            mode.setTitle (utf8 ("Выбор удара: ") + text);
            mode.onChange = [this]
            {
                const auto pick = (Pick) (mode.getSelectedId() - 1);
                mode.setTooltip (pickTooltip (pick));
                if (! updating)
                    processor.setPickChoice (index, pick);
            };

            addAndMakeVisible (name);
            addAndMakeVisible (mode);
            update();
        }

        void update()
        {
            const juce::ScopedValueSetter<bool> guard (updating, true);
            mode.setSelectedId ((int) processor.getPickChoice (index) + 1, juce::sendNotificationSync);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (brand::pad);
            g.fillRoundedRectangle (getLocalBounds().toFloat(), 8.0f);
        }

        void resized() override
        {
            auto area = getLocalBounds().reduced (10, 6);
            mode.setBounds (area.removeFromRight (kComboWidth));
            area.removeFromRight (8);
            name.setBounds (area);
        }

    private:
        SamplitProcessor& processor;
        int index;
        bool updating = false;

        juce::Label name;
        juce::ComboBox mode;
    };

    PickPanel::PickPanel (SamplitProcessor& p) : processor (p)
    {
        title.setText (utf8 ("Выбор удара"), juce::dontSendNotification);
        title.setFont (juce::FontOptions (15.0f, juce::Font::bold));

        hint.setText (utf8 ("Как velocity выбирает удар на клавишах барабана"), juce::dontSendNotification);
        hint.setFont (juce::FontOptions (12.0f));
        hint.setColour (juce::Label::textColourId, brand::textMuted);
        hint.setJustificationType (juce::Justification::centredRight);
        hint.setMinimumHorizontalScale (0.8f);

        emptyText.setText (utf8 ("Выберите пресет слева, и здесь появятся барабаны установки."), juce::dontSendNotification);
        emptyText.setColour (juce::Label::textColourId, brand::textMuted);
        emptyText.setFont (juce::FontOptions (13.0f));
        emptyText.setJustificationType (juce::Justification::centred);

        viewport.setViewedComponent (&content, false);
        viewport.setScrollBarsShown (true, false);

        for (auto* c : std::initializer_list<juce::Component*> { &title, &hint, &emptyText, &viewport })
            addAndMakeVisible (c);

        processor.addChangeListener (this);
        rebuild();
    }

    PickPanel::~PickPanel()
    {
        processor.removeChangeListener (this);
    }

    void PickPanel::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        if (processor.getKit() != kit)
            rebuild();
        else
            for (auto& r : rows)
                r->update();
    }

    void PickPanel::rebuild()
    {
        kit = processor.getKit();
        rows.clear();
        if (kit != nullptr)
            for (int i = 0; i < (int) kit->choices.size(); ++i)
            {
                rows.push_back (std::make_unique<Row> (processor, i, kit->choices[(size_t) i]));
                content.addAndMakeVisible (*rows.back());
            }

        emptyText.setText (kit == nullptr ? utf8 ("Выберите пресет слева, и здесь появятся барабаны установки.")
                                          : utf8 ("В этом пресете выбор удара не настраивается."),
                           juce::dontSendNotification);
        emptyText.setVisible (rows.empty());
        hint.setVisible (! rows.empty());
        resized();
    }

    void PickPanel::paint (juce::Graphics& g)
    {
        g.setColour (brand::surface);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);
    }

    void PickPanel::resized()
    {
        auto area = getLocalBounds().reduced (14, 10);
        auto header = area.removeFromTop (kHeaderHeight - 10);
        title.setBounds (header.removeFromLeft (juce::jmax (160, header.getWidth() / 3)));
        hint.setBounds (header);
        area.removeFromTop (8);

        emptyText.setBounds (area);
        viewport.setBounds (area);

        // Две колонки, если хватает ширины; порядок как в пресете, сверху вниз по колонкам
        const int n = (int) rows.size();
        const int columns = area.getWidth() >= kTwoColumnsFrom ? 2 : 1;
        const int perColumn = (n + columns - 1) / columns;
        const int contentHeight = perColumn * (kRowHeight + kRowGap) - kRowGap;
        const int scrollBar = contentHeight > area.getHeight() ? viewport.getScrollBarThickness() : 0;
        const int width = area.getWidth() - scrollBar;
        const int columnWidth = (width - (columns - 1) * kColumnGap) / columns;
        content.setSize (width, juce::jmax (0, contentHeight));
        for (int i = 0; i < n; ++i)
        {
            const int col = i / juce::jmax (1, perColumn), row = i % juce::jmax (1, perColumn);
            rows[(size_t) i]->setBounds (col * (columnWidth + kColumnGap), row * (kRowHeight + kRowGap), columnWidth, kRowHeight);
        }
    }
}
