#include "KitView.h"
#include "Brand.h"

#include <algorithm>

namespace samplit
{
    namespace
    {
        constexpr int kDetailWidth = 340;
        constexpr int kGap = 14;
        constexpr int kRowHeight = 58;
        constexpr int kLegendHeight = 92;
        constexpr int kLegendMinStage = 330;
        constexpr float kStageWidth = 1.7f, kStageHeight = 1.0f;

        const juce::Colour kBrass { 0xffc9a85c };
        const juce::Colour kBrassDark { 0xff6f5a2c };

        // Место барабана на схеме (вид спереди). Координаты в долях сцены 1,7 × 1,0
        struct Place
        {
            const char* id;
            float x, y, rx, ry;
            bool cymbal;
            const char* label;
        };

        const Place kPlaces[] = {
            { "crash_a",   0.36f, 0.20f, 0.19f, 0.055f, true,  "Crash 18\"" },
            { "splash",    0.60f, 0.10f, 0.08f, 0.030f, true,  "Splash" },
            { "china",     0.80f, 0.11f, 0.13f, 0.040f, true,  "China" },
            { "crash_b",   1.12f, 0.16f, 0.17f, 0.050f, true,  "Crash 16\"" },
            { "ride",      1.50f, 0.27f, 0.21f, 0.062f, true,  "Ride" },
            { "cymbals_x", 1.58f, 0.08f, 0.09f, 0.032f, true,  "C7–G#7" },
            { "hat",       0.17f, 0.57f, 0.14f, 0.045f, true,  "Hi-hat" },
            { "kick",      0.86f, 0.71f, 0.27f, 0.27f,  false, "Бочка" },
            { "snare",     0.47f, 0.66f, 0.15f, 0.15f,  false, "Малый" },
            { "tom_10",    0.69f, 0.38f, 0.115f, 0.115f, false, "Том 10\"" },
            { "tom_14",    1.04f, 0.38f, 0.125f, 0.125f, false, "Том 14\"" },
            { "tom_16m",   1.30f, 0.64f, 0.15f, 0.15f,  false, "16\" ср." },
            { "tom_16l",   1.54f, 0.64f, 0.12f, 0.12f,  false, "16\" низ." },
        };

        const Place* placeFor (const std::string& id)
        {
            for (const auto& p : kPlaces)
                if (id == p.id || (std::string (p.id) == "snare" && id.rfind ("snare", 0) == 0))
                    return &p;
            return nullptr;
        }

        juce::String keyName (int note)
        {
            return juce::MidiMessage::getMidiNoteName (note, true, true, 3);
        }

        struct ModeText
        {
            const char* title;
            const char* text;
        };

        const ModeText kModes[] = {
            { "Случайно", "Velocity выбирает слой громкости, а внутри слоя удар берётся случайно, без повтора подряд." },
            { "По кругу", "Velocity выбирает слой громкости, а записанные в нём удары идут по очереди, потом снова с первого. Повторы звучат чуть по-разному." },
            { "По velocity", "Velocity выбирает конкретный записанный удар, как в Kontakt. Нужно для сбивок, крещендо и закрывания хэта." },
        };
    }

    // Строка настроек: название группы клавиш барабана и три кнопки режима. Выбор сразу уходит в процессор
    class KitView::ChoiceRow final : public juce::Component
    {
    public:
        ChoiceRow (SamplitProcessor& p, int c, const PickChoice& choice) : processor (p), index (c)
        {
            name.setText (juce::String::fromUTF8 (choice.name.c_str()), juce::dontSendNotification);
            name.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            name.setMinimumHorizontalScale (0.75f);
            addAndMakeVisible (name);

            for (int i = 0; i < 3; ++i)
            {
                auto& b = buttons[(size_t) i];
                b.setButtonText (utf8 (kModes[i].title));
                b.setTooltip (utf8 (kModes[i].text));
                b.setClickingTogglesState (true);
                b.setRadioGroupId (2000 + c);
                b.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < 2 ? juce::Button::ConnectedOnRight : 0));
                b.onClick = [this, i]
                {
                    if (! updating)
                        processor.setPickChoice (index, (Pick) i);
                };
                addAndMakeVisible (b);
            }
            update();
        }

        void update()
        {
            const juce::ScopedValueSetter<bool> guard (updating, true);
            const int current = (int) processor.getPickChoice (index);
            for (int i = 0; i < 3; ++i)
                buttons[(size_t) i].setToggleState (i == current, juce::dontSendNotification);
        }

        void resized() override
        {
            auto area = getLocalBounds();
            name.setBounds (area.removeFromTop (20));
            area.removeFromTop (4);
            const int w = area.getWidth() / 3;
            for (int i = 0; i < 3; ++i)
                buttons[(size_t) i].setBounds (i < 2 ? area.removeFromLeft (w) : area);
        }

    private:
        SamplitProcessor& processor;
        int index;
        bool updating = false;
        juce::Label name;
        juce::TextButton buttons[3];
    };

    KitView::KitView (SamplitProcessor& p) : processor (p)
    {
        const auto muted = [] (juce::Label& l, float size = 12.0f)
        {
            l.setColour (juce::Label::textColourId, brand::textMuted);
            l.setFont (juce::FontOptions (size));
        };

        pieceTitle.setText (utf8 ("Барабан"), juce::dontSendNotification);
        muted (pieceTitle);
        pieceName.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        pieceName.setMinimumHorizontalScale (0.7f);
        muted (pieceKeysLabel);
        choicesTitle.setText (utf8 ("Как выбирается удар"), juce::dontSendNotification);
        muted (choicesTitle);
        noChoices.setText (utf8 ("У этого барабана режим не настраивается: удар выбирает velocity."), juce::dontSendNotification);
        muted (noChoices);
        noChoices.setJustificationType (juce::Justification::topLeft);
        tryTitle.setText (utf8 ("Попробовать: сила удара"), juce::dontSendNotification);
        muted (tryTitle);

        emptyText.setText (utf8 ("Выберите пресет в шапке окна, и здесь появится установка."), juce::dontSendNotification);
        muted (emptyText, 13.0f);
        emptyText.setJustificationType (juce::Justification::centred);

        pieceBox.setTitle (utf8 ("Барабан"));
        pieceBox.setTooltip (utf8 ("Выбрать барабан из списка (то же, что клик по схеме)"));
        pieceBox.onChange = [this]
        {
            if (! updating)
                selectPiece (pieceBox.getSelectedId() - 1);
        };

        levelTitle.setText (utf8 ("Громкость"), juce::dontSendNotification);
        muted (levelTitle);
        levelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
        levelSlider.setRange (-24.0, 12.0, 0.1);
        levelSlider.setDoubleClickReturnValue (true, 0.0);
        levelSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 62, 22);
        levelSlider.setTextValueSuffix (" dB");
        levelSlider.setTitle (utf8 ("Громкость барабана"));
        levelSlider.setTooltip (utf8 ("Громкость этого барабана целиком, на всех микрофонах. Двойной клик: 0 dB. Действует на следующие удары"));
        levelSlider.setColour (juce::Slider::trackColourId, brand::accent);
        levelSlider.setColour (juce::Slider::backgroundColourId, brand::line);
        levelSlider.setColour (juce::Slider::thumbColourId, brand::text);
        levelSlider.setColour (juce::Slider::textBoxTextColourId, brand::text);
        levelSlider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        levelSlider.onValueChange = [this]
        {
            if (! updating && selected >= 0)
                processor.setPieceLevel (selected, (float) levelSlider.getValue());
        };

        trySoft.setButtonText (utf8 ("Тихо"));
        tryMedium.setButtonText (utf8 ("Средне"));
        tryHard.setButtonText (utf8 ("Сильно"));
        trySoft.setTooltip (utf8 ("Сыграть барабан с velocity 40"));
        tryMedium.setTooltip (utf8 ("Сыграть барабан с velocity 80"));
        tryHard.setTooltip (utf8 ("Сыграть барабан с velocity 120"));
        trySoft.onClick = [this] { playPiece (selected, 40); };
        tryMedium.onClick = [this] { playPiece (selected, 80); };
        tryHard.onClick = [this] { playPiece (selected, 120); };

        for (auto* c : std::initializer_list<juce::Component*> { &pieceTitle, &pieceName, &pieceKeysLabel, &choicesTitle, &noChoices,
                                                                  &emptyText, &tryTitle, &pieceBox, &trySoft, &tryMedium, &tryHard,
                                                                  &levelTitle, &levelSlider })
            addChildComponent (c);
        emptyText.setVisible (true);

        processor.addChangeListener (this);
        rebuild();
        startTimerHz (30);
    }

    KitView::~KitView()
    {
        processor.removeChangeListener (this);
    }

    void KitView::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        if (processor.getKit() != kit)
            rebuild();
        else
        {
            for (auto& r : rows)
                r->update();
            if (selected >= 0 && ! levelSlider.isMouseButtonDown())
            {
                const juce::ScopedValueSetter<bool> guard (updating, true);
                levelSlider.setValue (processor.getPieceLevel (selected), juce::dontSendNotification);
            }
        }
    }

    // Подсветка барабанов, которые только что сыграли (с клавиатуры, мышью или из DAW)
    void KitView::timerCallback()
    {
        if (kit == nullptr)
            return;

        const auto active = processor.getActiveKeys();
        bool changed = false;
        for (size_t p = 0; p < flash.size(); ++p)
        {
            bool on = false;
            for (int k : pieceKeys[p])
                on = on || ((active[(size_t) (k >> 6)] >> (k & 63)) & 1u) != 0;
            const float next = on ? 1.0f : juce::jmax (0.0f, flash[p] - 0.09f);
            changed = changed || next != flash[p];
            flash[p] = next;
        }
        if (changed)
            repaint (stageArea);
    }

    void KitView::rebuild()
    {
        const auto previousId = (kit != nullptr && juce::isPositiveAndBelow (selected, (int) kit->pieces.size()))
                                    ? kit->pieces[(size_t) selected].id : std::string();
        kit = processor.getKit();
        flash.clear();
        pieceKeys.clear();
        selected = -1;
        hovered = -1;

        const juce::ScopedValueSetter<bool> guard (updating, true);
        pieceBox.clear (juce::dontSendNotification);
        if (kit != nullptr)
        {
            flash.assign (kit->pieces.size(), 0.0f);
            pieceKeys.assign (kit->pieces.size(), {});
            for (int k = 0; k < 128; ++k)
                if (const int p = kit->notes[(size_t) k].piece; p >= 0)
                    pieceKeys[(size_t) p].push_back (k);
            for (int i = 0; i < (int) kit->pieces.size(); ++i)
            {
                pieceBox.addItem (juce::String::fromUTF8 (kit->pieces[(size_t) i].name.c_str()), i + 1);
                if (kit->pieces[(size_t) i].id == previousId)
                    selected = i;
            }
            if (selected < 0 && ! kit->pieces.empty())
                selected = 0;
        }

        layoutSpots();
        selectPiece (selected);
        repaint();
    }

    // Пересчёт экранных прямоугольников барабанов по размеру сцены
    void KitView::layoutSpots()
    {
        spots.clear();
        if (kit == nullptr)
            return;

        std::vector<int> extra;
        for (int i = 0; i < (int) kit->pieces.size(); ++i)
            if (placeFor (kit->pieces[(size_t) i].id) == nullptr)
                extra.push_back (i);

        auto stage = stageArea.reduced (12, 10);
        auto chips = extra.empty() ? juce::Rectangle<int>() : stage.removeFromBottom (30);
        legendArea = {};
        if (stage.getHeight() >= kLegendMinStage)
            legendArea = stage.removeFromBottom (kLegendHeight).reduced (4, 0);
        const float scale = juce::jmin ((float) stage.getWidth() / kStageWidth, (float) stage.getHeight() / kStageHeight);
        const auto origin = stage.toFloat().getCentre() - juce::Point<float> (kStageWidth, kStageHeight) * (scale * 0.5f);

        for (int i = 0; i < (int) kit->pieces.size(); ++i)
        {
            const auto* pl = placeFor (kit->pieces[(size_t) i].id);
            if (pl == nullptr)
                continue;
            Spot s;
            s.piece = i;
            s.cymbal = pl->cymbal;
            s.order = pl->cymbal ? 0 : (std::string (pl->id) == "kick" ? 1 : 2);
            s.label = utf8 (pl->label);
            s.bounds = juce::Rectangle<float> (pl->rx * 2.0f * scale, pl->ry * 2.0f * scale)
                           .withCentre (origin + juce::Point<float> (pl->x, pl->y) * scale);
            spots.push_back (s);
        }

        // Барабаны, которых нет на схеме, идут рядом кнопками
        float x = (float) chips.getX();
        for (int i : extra)
        {
            Spot s;
            s.piece = i;
            s.chip = true;
            s.label = juce::String::fromUTF8 (kit->pieces[(size_t) i].id.c_str());
            s.bounds = { x, (float) chips.getY(), 120.0f, (float) chips.getHeight() };
            x += 126.0f;
            spots.push_back (s);
        }
        std::stable_sort (spots.begin(), spots.end(), [] (const Spot& a, const Spot& b) { return a.order < b.order; });
    }

    int KitView::pieceAt (juce::Point<float> p) const
    {
        for (auto it = spots.rbegin(); it != spots.rend(); ++it)
        {
            const auto& b = it->bounds;
            if (it->chip ? b.contains (p)
                         : (std::pow ((p.x - b.getCentreX()) / (b.getWidth() * 0.5f), 2.0f)
                                + std::pow ((p.y - b.getCentreY()) / (b.getHeight() * 0.5f), 2.0f)) <= 1.0f)
                return it->piece;
        }
        return -1;
    }

    int KitView::firstKeyOf (int piece) const
    {
        return juce::isPositiveAndBelow (piece, (int) pieceKeys.size()) && ! pieceKeys[(size_t) piece].empty()
                   ? pieceKeys[(size_t) piece].front() : -1;
    }

    juce::String KitView::keysText (int piece) const
    {
        if (firstKeyOf (piece) < 0)
            return {};
        const auto& keys = pieceKeys[(size_t) piece];
        if (keys.size() == 1)
            return utf8 ("Клавиша: ") + keyName (keys.front());
        return utf8 ("Клавиши: ") + keyName (keys.front()) + utf8 (" … ") + keyName (keys.back())
               + utf8 ("  (") + juce::String ((int) keys.size()) + utf8 (")");
    }

    void KitView::playPiece (int piece, int velocity)
    {
        if (const int key = firstKeyOf (piece); key >= 0)
            processor.triggerNote (key, velocity);
    }

    void KitView::selectPiece (int piece)
    {
        selected = kit != nullptr && juce::isPositiveAndBelow (piece, (int) kit->pieces.size()) ? piece : -1;
        {
            const juce::ScopedValueSetter<bool> guard (updating, true);
            pieceBox.setSelectedId (selected + 1, juce::dontSendNotification);
        }
        if (selected >= 0)
        {
            const juce::ScopedValueSetter<bool> guard (updating, true);
            levelSlider.setValue (processor.getPieceLevel (selected), juce::dontSendNotification);
            pieceName.setText (juce::String::fromUTF8 (kit->pieces[(size_t) selected].name.c_str()), juce::dontSendNotification);
            pieceKeysLabel.setText (keysText (selected), juce::dontSendNotification);
        }
        rebuildChoiceRows();
        repaint();
    }

    // Переключатели, которые относятся к выбранному барабану
    void KitView::rebuildChoiceRows()
    {
        rows.clear();
        if (kit != nullptr && selected >= 0)
            for (int c = 0; c < (int) kit->choices.size(); ++c)
            {
                const auto& pieces = kit->choices[(size_t) c].pieces;
                if (std::find (pieces.begin(), pieces.end(), selected) == pieces.end())
                    continue;
                rows.push_back (std::make_unique<ChoiceRow> (processor, c, kit->choices[(size_t) c]));
                addAndMakeVisible (*rows.back());
            }

        const bool has = kit != nullptr && selected >= 0;
        for (auto* c : std::initializer_list<juce::Component*> { &pieceTitle, &pieceName, &pieceKeysLabel, &choicesTitle, &tryTitle,
                                                                  &pieceBox, &trySoft, &tryMedium, &tryHard, &levelTitle, &levelSlider })
            c->setVisible (has);
        noChoices.setVisible (has && rows.empty());
        emptyText.setVisible (kit == nullptr);
        resized();
    }

    void KitView::mouseDown (const juce::MouseEvent& e)
    {
        const int piece = pieceAt (e.position);
        if (piece < 0)
            return;
        selectPiece (piece);
        playPiece (piece, 100);
    }

    void KitView::mouseMove (const juce::MouseEvent& e)
    {
        const int piece = pieceAt (e.position);
        if (piece != hovered)
        {
            hovered = piece;
            setMouseCursor (piece >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint (stageArea);
        }
    }

    void KitView::mouseExit (const juce::MouseEvent&)
    {
        if (hovered >= 0)
        {
            hovered = -1;
            repaint (stageArea);
        }
    }

    void KitView::resized()
    {
        auto area = getLocalBounds();
        detailArea = area.removeFromRight (kDetailWidth);
        area.removeFromRight (kGap);
        stageArea = area;
        layoutSpots();
        emptyText.setBounds (stageArea);

        auto d = detailArea.reduced (16, 14);
        pieceTitle.setBounds (d.removeFromTop (18));
        pieceBox.setBounds (d.removeFromTop (30));
        d.removeFromTop (8);
        pieceName.setBounds (d.removeFromTop (22));
        pieceKeysLabel.setBounds (d.removeFromTop (18));
        d.removeFromTop (12);

        auto levelRow = d.removeFromBottom (26);
        levelTitle.setBounds (levelRow.removeFromLeft (72));
        levelSlider.setBounds (levelRow);
        d.removeFromBottom (10);
        auto bottom = d.removeFromBottom (48);
        tryTitle.setBounds (bottom.removeFromTop (18));
        const int w = (bottom.getWidth() - 12) / 3;
        trySoft.setBounds (bottom.removeFromLeft (w));
        bottom.removeFromLeft (6);
        tryMedium.setBounds (bottom.removeFromLeft (w));
        bottom.removeFromLeft (6);
        tryHard.setBounds (bottom);

        choicesTitle.setBounds (d.removeFromTop (18));
        d.removeFromTop (6);
        noChoices.setBounds (d.removeFromTop (54));
        for (auto& r : rows)
        {
            r->setBounds (d.removeFromTop (kRowHeight));
            d.removeFromTop (8);
        }
        repaint();
    }

    namespace
    {
        void drawDrum (juce::Graphics& g, juce::Rectangle<float> b, float flashing, bool selected, bool hot, bool kick)
        {
            const auto c = b.getCentre();
            g.setGradientFill (juce::ColourGradient (brand::pad.brighter (0.35f), c.x - b.getWidth() * 0.25f, c.y - b.getHeight() * 0.3f,
                                                     brand::pad.darker (0.5f), b.getRight(), b.getBottom(), true));
            g.fillEllipse (b);

            // Головка: от неё при ударе вспыхивает акцентный цвет
            auto head = b.reduced (b.getWidth() * 0.11f);
            g.setColour (juce::Colour (0xff262c38).interpolatedWith (brand::accent, flashing * 0.7f));
            g.fillEllipse (head);
            g.setColour (brand::line.brighter (0.2f));
            g.drawEllipse (head, 1.0f);
            if (kick)
            {
                g.setColour (brand::bg);
                g.fillEllipse (juce::Rectangle<float> (head.getWidth() * 0.22f, head.getHeight() * 0.22f).withCentre (c));
            }

            g.setColour (selected ? brand::accent : (hot ? brand::accentHover : brand::textMuted.withAlpha (0.6f)));
            g.drawEllipse (b.reduced (1.5f), selected ? 3.5f : 2.0f);
        }

        void drawCymbal (juce::Graphics& g, juce::Rectangle<float> b, float flashing, bool selected, bool hot)
        {
            const auto c = b.getCentre();
            g.setGradientFill (juce::ColourGradient (kBrass.brighter (0.2f), b.getX(), c.y, kBrassDark, b.getRight(), c.y, false));
            g.fillEllipse (b);
            g.setColour (kBrassDark.darker (0.3f).withAlpha (0.5f));
            g.drawEllipse (b.reduced (b.getHeight() * 0.18f), 1.0f);
            g.setColour (kBrass.brighter (0.5f).interpolatedWith (juce::Colours::white, 0.3f));
            g.fillEllipse (juce::Rectangle<float> (b.getHeight() * 0.35f, b.getHeight() * 0.35f).withCentre (c));
            if (flashing > 0.0f)
            {
                g.setColour (brand::accent.withAlpha (0.55f * flashing));
                g.fillEllipse (b);
            }
            g.setColour (selected ? brand::accent : (hot ? brand::accentHover : juce::Colours::transparentBlack));
            g.drawEllipse (b.reduced (1.0f), selected ? 3.5f : 2.0f);
        }
    }

    void KitView::paint (juce::Graphics& g)
    {
        g.setColour (brand::surface);
        g.fillRoundedRectangle (stageArea.toFloat(), 12.0f);
        g.fillRoundedRectangle (detailArea.toFloat(), 12.0f);

        if (kit == nullptr)
            return;

        for (const auto& s : spots)
        {
            const bool sel = s.piece == selected, hot = s.piece == hovered;
            const float f = flash[(size_t) s.piece];
            if (s.chip)
            {
                g.setColour (sel ? brand::accentSoft : brand::pad);
                g.fillRoundedRectangle (s.bounds, 8.0f);
                g.setColour (sel ? brand::accent : brand::line);
                g.drawRoundedRectangle (s.bounds.reduced (0.5f), 8.0f, sel ? 2.0f : 1.0f);
                g.setColour (brand::text);
                g.setFont (juce::FontOptions (12.0f));
                g.drawText (s.label, s.bounds.toNearestInt(), juce::Justification::centred, true);
                continue;
            }

            if (s.cymbal)
                drawCymbal (g, s.bounds, f, sel, hot);
            else
                drawDrum (g, s.bounds, f, sel, hot, s.order == 1);

            g.setFont (juce::FontOptions (juce::jlimit (11.0f, 13.0f, s.bounds.getHeight() * 0.35f), juce::Font::bold));
            if (s.cymbal)
            {
                g.setColour (sel || hot ? brand::text : brand::textMuted);
                g.drawText (s.label, s.bounds.withY (s.bounds.getBottom() + 1.0f).withHeight (16.0f).expanded (30.0f, 0.0f),
                            juce::Justification::centred, false);
            }
            else
            {
                g.setColour (brand::text);
                g.drawText (s.label, s.bounds.toNearestInt(), juce::Justification::centred, true);
            }
        }

        // Пояснение режимов под схемой: если места хватает
        if (! legendArea.isEmpty())
        {
            auto legend = legendArea;
            g.setColour (brand::line);
            g.fillRect (legend.removeFromTop (1));
            legend.removeFromTop (10);
            const int w = legend.getWidth() / 3;
            for (int i = 0; i < 3; ++i)
            {
                auto col = legend.removeFromLeft (w).reduced (6, 0);
                g.setColour (brand::text);
                g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
                g.drawText (utf8 (kModes[i].title), col.removeFromTop (16), juce::Justification::centredLeft);
                g.setColour (brand::textMuted);
                g.setFont (juce::FontOptions (11.5f));
                g.drawFittedText (utf8 (kModes[i].text), col, juce::Justification::topLeft, 4, 1.0f);
            }
        }
    }
}
