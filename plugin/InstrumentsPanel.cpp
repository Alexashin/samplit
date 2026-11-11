#include "InstrumentsPanel.h"
#include "Brand.h"

#include <algorithm>
#include <set>

namespace samplit
{
    namespace
    {
        constexpr int kListWidth = 270;
        constexpr int kPad = 20;
        const juce::Colour kWarning { 0xffffb454 };

        juce::String pickText (Pick p)
        {
            switch (p)
            {
                case Pick::Cycle:  return utf8 ("По кругу");
                case Pick::Random: return utf8 ("Случайно");
                case Pick::Exact:  return utf8 ("Точно по силе удара");
            }
            return {};
        }

        // C3 = 60
        juce::String noteName (int key)
        {
            static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            return juce::String (names[key % 12]) + juce::String (key / 12 - 2);
        }

        juce::Font boldFont (float size) { return juce::Font (juce::FontOptions (size, juce::Font::bold)); }
        juce::Font plainFont (float size) { return juce::Font (juce::FontOptions (size)); }

        const CatalogPack* findPack (const std::vector<CatalogPack>& packs, const juce::String& id)
        {
            for (const auto& p : packs)
                if (p.packId == id)
                    return &p;
            return nullptr;
        }

        // Барабан установки в паке: по id или единственный
        const CatalogPiece* findPiece (const CatalogPack& pack, const juce::String& pieceId)
        {
            for (const auto& p : pack.pieces)
                if (p.id == pieceId)
                    return &p;
            return pack.pieces.size() == 1 || pieceId.isEmpty() ? &pack.pieces.front() : nullptr;
        }

        juce::String packTitle (const CatalogPack& p) { return p.title.isNotEmpty() ? p.title : p.packId; }

        void styleSection (juce::Label& l, const juce::String& text)
        {
            l.setText (text, juce::dontSendNotification);
            l.setFont (boldFont (12.0f));
            l.setColour (juce::Label::textColourId, brand::textMuted);
        }
    }

    // ---- Список мест ----

    struct InstrumentsPanel::PlaceList final : juce::ListBox, private juce::ListBoxModel
    {
        struct Row { juce::String label, sub; bool enabled = true, warn = false; };

        explicit PlaceList (InstrumentsPanel& o) : juce::ListBox ({}, nullptr), owner (o)
        {
            setModel (this);
            setRowHeight (54);
            setTitle (utf8 ("Места установки"));
            rows.resize (kStudio600Places.size());
        }

        int getNumRows() override { return (int) rows.size(); }

        void paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool isSelected) override
        {
            if (! juce::isPositiveAndBelow (row, (int) rows.size()))
                return;
            const auto& r = rows[(size_t) row];
            const auto box = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (6.0f, 3.0f);
            if (isSelected)
            {
                g.setColour (brand::accentSoft);
                g.fillRoundedRectangle (box, 8.0f);
                g.setColour (brand::accent);
                g.drawRoundedRectangle (box.reduced (0.5f), 8.0f, 1.0f);
            }
            auto text = box.reduced (12.0f, h < 46 ? 2.0f : 6.0f).toNearestInt();
            g.setColour (r.enabled ? brand::text : brand::textMuted);
            g.setFont (boldFont (14.0f));
            g.drawText (r.label, text.removeFromTop (20), juce::Justification::centredLeft, true);
            g.setColour (r.warn ? kWarning : brand::textMuted);
            g.setFont (plainFont (12.0f));
            g.drawText (r.sub, text, juce::Justification::centredLeft, true);
        }

        void selectedRowsChanged (int row) override
        {
            if (row >= 0 && row != owner.selected)
            {
                owner.selected = row;
                owner.refresh (true);
            }
        }

        InstrumentsPanel& owner;
        std::vector<Row> rows;
    };

    // ---- Строка группы ударов ----

    struct InstrumentsPanel::GroupRow final : juce::Component
    {
        GroupRow (juce::String n, juce::String h, juce::String w, std::vector<Pick> allowedModes, Pick cur,
                  std::function<void (Pick)> cb)
            : name (std::move (n)), hint (std::move (h)), where (std::move (w)), allowed (std::move (allowedModes)),
              current (cur), onPick (std::move (cb))
        {
            setTitle (name);
            if (allowed.size() >= 2)
                for (auto pick : allowed)
                {
                    auto& b = *buttons.emplace_back (std::make_unique<juce::TextButton> (pickText (pick)));
                    b.setTooltip (utf8 ("Как выбирается удар в группе «") + name + utf8 ("»: ") + pickText (pick));
                    b.onClick = [this, pick] { setCurrent (pick); if (onPick) onPick (pick); };
                    addAndMakeVisible (b);
                }
            setCurrent (cur);
        }

        void setCurrent (Pick p)
        {
            current = p;
            for (size_t i = 0; i < buttons.size(); ++i)
                buttons[i]->setToggleState (allowed[i] == p, juce::dontSendNotification);
            repaint();
        }

        int controlsWidth() const
        {
            if (buttons.empty())
                return 0;
            int w = 0;
            for (auto& b : buttons)
                w += buttonWidth (*b) + 4;
            return w - 4;
        }

        static int buttonWidth (const juce::TextButton& b)
        {
            return juce::GlyphArrangement::getStringWidthInt (plainFont (13.0f), b.getButtonText()) + 28;
        }

        bool stacked (int width) const { return width < controlsWidth() + 320; }

        int heightFor (int width) const
        {
            int h = 12 + 20 + (hint.isNotEmpty() ? 18 : 0) + (where.isNotEmpty() ? 18 : 0);
            if (! buttons.empty() && stacked (width))
                h += 36;
            return juce::jmax (h, buttons.empty() || stacked (width) ? 0 : 52);
        }

        void resized() override
        {
            if (buttons.empty())
                return;
            auto area = getLocalBounds().reduced (0, 8);
            area = stacked (getWidth()) ? area.removeFromBottom (30) : area.removeFromRight (controlsWidth()).withSizeKeepingCentre (controlsWidth(), 30);
            for (auto& b : buttons)
            {
                b->setBounds (area.removeFromLeft (buttonWidth (*b)));
                area.removeFromLeft (4);
            }
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (brand::line);
            g.fillRect (0, getHeight() - 1, getWidth(), 1);

            auto text = getLocalBounds().reduced (0, 6);
            text.removeFromBottom (1);
            if (! buttons.empty() && stacked (getWidth()))
                text.removeFromBottom (36);
            else if (! buttons.empty())
                text.removeFromRight (controlsWidth() + 16);

            g.setColour (brand::text);
            g.setFont (boldFont (14.0f));
            g.drawText (name, text.removeFromTop (22), juce::Justification::centredLeft, true);
            g.setFont (plainFont (12.0f));
            if (hint.isNotEmpty())
            {
                g.setColour (brand::textMuted);
                g.drawText (hint, text.removeFromTop (18), juce::Justification::centredLeft, true);
            }
            if (where.isNotEmpty())
            {
                g.setColour (brand::textMuted);
                g.drawText (where, text.removeFromTop (18), juce::Justification::centredLeft, true);
            }
            if (buttons.empty())
            {
                g.setColour (brand::textMuted);
                g.setFont (plainFont (13.0f));
                g.drawText (utf8 ("Режим: ") + pickText (current), getLocalBounds().withTrimmedBottom (1).removeFromRight (240),
                            juce::Justification::centredRight, true);
            }
        }

        juce::String name, hint, where;
        std::vector<Pick> allowed;
        Pick current;
        std::function<void (Pick)> onPick;
        std::vector<std::unique_ptr<juce::TextButton>> buttons;
    };

    struct InstrumentsPanel::GroupsContent final : juce::Component
    {
        void layoutRows (int width)
        {
            int y = 0;
            for (auto& r : rows)
            {
                const int h = r->heightFor (width);
                r->setBounds (0, y, width, h);
                y += h;
            }
            setSize (width, y);
        }

        std::vector<std::unique_ptr<GroupRow>> rows;
    };

    // ---- Панель ----

    InstrumentsPanel::InstrumentsPanel (SamplitProcessor& p)
        : processor (p), list (std::make_unique<PlaceList> (*this)), groupsContent (std::make_unique<GroupsContent>())
    {
        setTitle (utf8 ("Инструменты"));
        list->setColour (juce::ListBox::backgroundColourId, brand::surface);
        addAndMakeVisible (*list);

        title.setFont (boldFont (22.0f));
        title.setMinimumHorizontalScale (0.8f);
        styleSection (soundLabel, utf8 ("ЗВУК"));
        styleSection (levelLabel, utf8 ("ГРОМКОСТЬ"));
        styleSection (groupsLabel, utf8 ("ГРУППЫ УДАРОВ"));
        packWarning.setFont (plainFont (13.0f));
        packWarning.setColour (juce::Label::textColourId, kWarning);
        savedHint.setFont (plainFont (12.0f));
        savedHint.setColour (juce::Label::textColourId, brand::textMuted);
        savedHint.setText (utf8 ("Установка сохраняется вместе с проектом. Стандартную можно задать кнопками в шапке."),
                           juce::dontSendNotification);
        noGroups.setFont (plainFont (13.0f));
        noGroups.setColour (juce::Label::textColourId, brand::textMuted);
        noGroups.setText (utf8 ("У этого звука нет настраиваемых групп: удар выбирается автоматически."),
                          juce::dontSendNotification);

        enabledToggle.setButtonText (utf8 ("Включено"));
        enabledToggle.setTooltip (utf8 ("Выключенное место молчит и не загружается в память"));
        enabledToggle.onClick = [this]
        {
            if (! updating)
                processor.setPlaceEnabled (juce::String (currentPlace().id), enabledToggle.getToggleState());
        };

        packBox.setTitle (utf8 ("Пак"));
        packBox.setTooltip (utf8 ("Набор записей, который играет на этом месте"));
        packBox.setTextWhenNothingSelected (utf8 ("Выберите пак"));
        packBox.onChange = [this]
        {
            const auto idx = packBox.getSelectedId() - 1;
            if (updating || ! juce::isPositiveAndBelow (idx, (int) packs.size()))
                return;
            const auto& pk = packs[(size_t) idx];
            const auto& def = currentPlace();
            juce::String piece = pk.pieces.front().id;
            for (const auto& pc : pk.pieces)
                if (pc.id == juce::String (def.defaultPiece))
                    piece = pc.id;
            processor.setPlacePack (juce::String (def.id), pk.packId, piece);
        };
        pieceBox.setTitle (utf8 ("Барабан"));
        pieceBox.setTooltip (utf8 ("В этом паке несколько барабанов такого типа: выберите нужный"));
        pieceBox.onChange = [this]
        {
            const auto idx = packBox.getSelectedId() - 1;
            if (updating || ! juce::isPositiveAndBelow (idx, (int) packs.size()))
                return;
            const auto& pk = packs[(size_t) idx];
            const auto pi = pieceBox.getSelectedId() - 1;
            if (juce::isPositiveAndBelow (pi, (int) pk.pieces.size()))
                processor.setPlacePack (juce::String (currentPlace().id), pk.packId, pk.pieces[(size_t) pi].id);
        };

        level.setSliderStyle (juce::Slider::LinearHorizontal);
        level.setRange (kPlaceLevelMinDb, kPlaceLevelMaxDb, 0.1);
        level.setDoubleClickReturnValue (true, 0.0);
        level.setTextBoxStyle (juce::Slider::TextBoxRight, false, 80, 24);
        level.setTitle (utf8 ("Громкость места"));
        level.setTooltip (utf8 ("Громкость этого барабана, дБ. Двойной клик: 0 дБ"));
        level.textFromValueFunction = [] (double v)
        {
            return (v > 0.05 ? "+" : "") + juce::String (v, 1) + utf8 (" дБ");
        };
        level.valueFromTextFunction = [] (const juce::String& t) { return t.retainCharacters ("0123456789.,-+").replaceCharacter (',', '.').getDoubleValue(); };
        level.updateText();
        level.setColour (juce::Slider::trackColourId, brand::accent);
        level.setColour (juce::Slider::backgroundColourId, brand::line);
        level.setColour (juce::Slider::thumbColourId, brand::text);
        level.setColour (juce::Slider::textBoxTextColourId, brand::text);
        level.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        level.onValueChange = [this]
        {
            if (! updating)
                processor.setPlaceLevel (juce::String (currentPlace().id), (float) level.getValue());
        };

        listen.setButtonText (utf8 ("Прослушать"));
        listen.setTooltip (utf8 ("Сыграть этот барабан один раз"));
        listen.onClick = [this] { processor.triggerNote (currentPlace().keyLo, 100); };

        viewport.setViewedComponent (groupsContent.get(), false);
        viewport.setScrollBarsShown (true, false);
        viewport.setTitle (utf8 ("Группы ударов"));

        for (auto* c : std::initializer_list<juce::Component*> { &title, &enabledToggle, &soundLabel, &packBox, &pieceBox,
                                                                  &packWarning, &levelLabel, &level, &listen, &groupsLabel,
                                                                  &viewport, &noGroups, &savedHint })
            addAndMakeVisible (c);

        processor.addChangeListener (this);
        refresh (true);
        list->selectRow (selected);
    }

    InstrumentsPanel::~InstrumentsPanel()
    {
        processor.removeChangeListener (this);
    }

    const PlaceDef& InstrumentsPanel::currentPlace() const
    {
        return kStudio600Places[(size_t) juce::jlimit (0, (int) kStudio600Places.size() - 1, selected)];
    }

    void InstrumentsPanel::selectPlace (const juce::String& placeId)
    {
        for (int i = 0; i < (int) kStudio600Places.size(); ++i)
            if (placeId == kStudio600Places[(size_t) i].id)
            {
                selected = i;
                list->selectRow (i);
                refresh (true);
                return;
            }
    }

    void InstrumentsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        refresh (false);
    }

    void InstrumentsPanel::refresh (bool forceGroups)
    {
        const juce::ScopedValueSetter<bool> guard (updating, true);
        const auto setup = processor.getSetup();
        const auto catalog = processor.getCatalog();

        // Список слева
        for (size_t i = 0; i < kStudio600Places.size(); ++i)
        {
            const auto& def = kStudio600Places[i];
            auto& row = list->rows[i];
            row.label = juce::String::fromUTF8 (def.label);
            const auto* ps = setup.find (def.id);
            const auto pks = packsForPlace (catalog, def);
            const auto* pk = ps != nullptr ? findPack (pks, juce::String (ps->packId)) : nullptr;
            row.enabled = ps == nullptr || ps->enabled;
            row.warn = false;
            if (ps != nullptr && ! ps->enabled)
                row.sub = utf8 ("выключено");
            else if (ps != nullptr && pk == nullptr)
            {
                row.sub = utf8 ("пак не найден");
                row.warn = true;
            }
            else
                row.sub = pk != nullptr ? packTitle (*pk) : juce::String();
        }
        list->updateContent();
        list->repaint();

        // Карточка
        const auto& def = currentPlace();
        const auto* ps = setup.find (def.id);
        if (ps == nullptr)
            return;
        packs = packsForPlace (catalog, def);

        title.setText (juce::String::fromUTF8 (def.label), juce::dontSendNotification);
        enabledToggle.setToggleState (ps->enabled, juce::dontSendNotification);

        // Список паков перестраивается только при изменении, чтобы не закрывать открытое меню
        juce::String sig;
        for (const auto& pk : packs)
            sig << pk.packId << "|" << pk.title << ";";
        if (sig != packBox.getProperties().getWithDefault ("sig", {}).toString())
        {
            packBox.clear (juce::dontSendNotification);
            for (size_t i = 0; i < packs.size(); ++i)
                packBox.addItem (packTitle (packs[i]), (int) i + 1);
            packBox.getProperties().set ("sig", sig);
        }
        const juce::String packId (ps->packId);
        int packIndex = -1;
        for (size_t i = 0; i < packs.size(); ++i)
            if (packs[i].packId == packId)
                packIndex = (int) i;
        packBox.setSelectedId (packIndex + 1, juce::dontSendNotification);

        const bool missing = packIndex < 0 && packId.isNotEmpty();
        packWarning.setText (missing ? utf8 ("Пак не найден: ") + packId + utf8 (". Установите пак или выберите другой")
                                     : (packs.empty() ? utf8 ("Подходящих паков нет") : juce::String()),
                             juce::dontSendNotification);

        const bool severalPieces = packIndex >= 0 && packs[(size_t) packIndex].pieces.size() > 1;
        if (severalPieces)
        {
            const auto& pk = packs[(size_t) packIndex];
            const juce::String pieceId (ps->pieceId);
            juce::String psig = pk.packId;
            for (const auto& pc : pk.pieces)
                psig << "|" << pc.id << pc.name;
            if (psig != pieceBox.getProperties().getWithDefault ("sig", {}).toString())
            {
                pieceBox.clear (juce::dontSendNotification);
                for (size_t i = 0; i < pk.pieces.size(); ++i)
                    pieceBox.addItem (pk.pieces[i].name.isNotEmpty() ? pk.pieces[i].name : pk.pieces[i].id, (int) i + 1);
                pieceBox.getProperties().set ("sig", psig);
            }
            int pi = 0;
            for (size_t i = 0; i < pk.pieces.size(); ++i)
                if (pk.pieces[i].id == pieceId)
                    pi = (int) i + 1;
            pieceBox.setSelectedId (pi, juce::dontSendNotification);
        }
        pieceBox.setVisible (severalPieces);

        if (! level.isMouseButtonDown() && std::abs (level.getValue() - (double) ps->levelDb) > 0.001)
            level.setValue (ps->levelDb, juce::dontSendNotification);

        const bool on = ps->enabled;
        for (auto* c : std::initializer_list<juce::Component*> { &packBox, &pieceBox, &level, &listen })
            c->setEnabled (on);
        viewport.setAlpha (on ? 1.0f : 0.45f);
        viewport.setEnabled (on);
        viewport.getViewedComponent()->setEnabled (on);

        rebuildGroups (setup, def, *ps, packs);
        (void) forceGroups;
        resized();
        repaint (cardArea);
    }

    // Строки групп: пересоздаются при смене места, пака, барабана или кита; иначе только обновляется режим
    // (строка не удаляется внутри собственного обработчика, прокрутка сохраняется)
    void InstrumentsPanel::rebuildGroups (const Setup&, const PlaceDef& def, const PlaceSetup& ps,
                                          const std::vector<CatalogPack>& pks)
    {
        const auto kit = processor.getKit();
        const auto* pack = findPack (pks, juce::String (ps.packId));
        const auto* piece = pack != nullptr ? findPiece (*pack, juce::String (ps.pieceId)) : nullptr;

        const juce::String structure = juce::String (def.id) + "|" + juce::String (ps.packId) + "|" + juce::String (ps.pieceId)
                                       + "|" + (kit != nullptr ? juce::String ((juce::int64) kit->serial.value) : juce::String())
                                       + "|" + (piece != nullptr ? juce::String ((int) piece->groups.size()) : juce::String ("-"));

        const auto currentMode = [&] (const pack::Group& g)
        {
            const auto it = ps.picks.find ((juce::String (def.id) + ":" + g.id).toStdString());
            if (it != ps.picks.end())
                return it->second;
            Pick p = Pick::Random;
            pickFromString (g.pick, p);
            return p;
        };
        const auto allowedFor = [] (const pack::Group& g)
        {
            std::vector<Pick> allowed;
            for (auto pick : { Pick::Cycle, Pick::Random, Pick::Exact })
                if (g.picks.empty() || std::find (g.picks.begin(), g.picks.end(), pickToString (pick)) != g.picks.end())
                    allowed.push_back (pick);
            if (allowed.empty())
                allowed.push_back (Pick::Random);
            return allowed;
        };

        if (structure == groupsSignature && piece != nullptr)
        {
            for (size_t i = 0; i < groupsContent->rows.size() && i < piece->groups.size(); ++i)
            {
                auto mode = currentMode (piece->groups[i]);
                const auto& al = groupsContent->rows[i]->allowed;
                if (std::find (al.begin(), al.end(), mode) == al.end())
                    mode = al.front();
                if (groupsContent->rows[i]->current != mode)
                    groupsContent->rows[i]->setCurrent (mode);
            }
            return;
        }
        groupsSignature = structure;

        const auto savedPos = viewport.getViewPosition();
        const bool samePlace = groupsContent->getName() == def.id;
        groupsContent->setName (def.id);
        groupsContent->rows.clear();
        groupsContent->removeAllChildren();

        // Клавиши и сила удара группы: из текущего кита, место = Piece::slot
        const Piece* kitPiece = nullptr;
        if (kit != nullptr && piece != nullptr)
            for (const auto& kp : kit->pieces)
                if (kp.slot == def.slot && juce::String (kp.id) == piece->id)
                    kitPiece = &kp;

        const juce::String placeId (def.id);
        if (piece != nullptr)
            for (const auto& g : piece->groups)
            {
                juce::String where;
                if (kitPiece != nullptr)
                {
                    std::set<int> keys;
                    int lo = 127, hi = 1;
                    for (const auto& h : kitPiece->hits)
                    {
                        if (! juce::isPositiveAndBelow (h.layer, (int) kitPiece->layers.size()))
                            continue;
                        const juce::String layerId (kitPiece->layers[(size_t) h.layer].id);
                        if (std::find (g.layers.begin(), g.layers.end(), layerId) == g.layers.end())
                            continue;
                        keys.insert (h.key);
                        lo = juce::jmin (lo, h.velLo);
                        hi = juce::jmax (hi, h.velHi);
                    }
                    if (! keys.empty())
                    {
                        juce::StringArray names;
                        for (int k : keys)
                            names.add (noteName (k));
                        const auto notes = keys.size() <= 4 ? names.joinIntoString (", ")
                                                            : noteName (*keys.begin()) + utf8 ("–") + noteName (*keys.rbegin())
                                                                  + utf8 (", клавиш: ") + juce::String ((int) keys.size());
                        where = utf8 ("Играет: ") + notes + utf8 (" · ")
                                + (lo <= 1 && hi >= 127 ? utf8 ("любая сила удара") : utf8 ("сила ") + juce::String (lo) + utf8 ("–") + juce::String (hi));
                    }
                }

                const auto allowed = allowedFor (g);
                auto mode = currentMode (g);
                if (std::find (allowed.begin(), allowed.end(), mode) == allowed.end())
                    mode = allowed.front();
                const auto choiceId = placeId + ":" + g.id;
                auto row = std::make_unique<GroupRow> (g.name.isNotEmpty() ? g.name : g.id, g.hint, where, allowed, mode,
                                                       [safe = juce::Component::SafePointer<InstrumentsPanel> (this), choiceId] (Pick p)
                                                       {
                                                           if (safe != nullptr)
                                                               safe->processor.setGroupPick (choiceId, p);
                                                       });
                groupsContent->addAndMakeVisible (*row);
                groupsContent->rows.push_back (std::move (row));
            }

        noGroups.setVisible (groupsContent->rows.empty());
        layoutGroups();
        viewport.setViewPosition (samePlace ? savedPos : juce::Point<int> (0, 0));
    }

    void InstrumentsPanel::layoutGroups()
    {
        groupsContent->layoutRows (juce::jmax (100, viewport.getMaximumVisibleWidth()));
    }

    void InstrumentsPanel::paint (juce::Graphics& g)
    {
        g.setColour (brand::surface);
        g.fillRoundedRectangle (cardArea.toFloat(), 10.0f);
        g.setColour (brand::line);
        g.drawRoundedRectangle (cardArea.toFloat().reduced (0.5f), 10.0f, 1.0f);
        const auto l = list->getBounds().toFloat();
        g.drawRoundedRectangle (l.expanded (0.5f), 10.0f, 1.0f);
    }

    void InstrumentsPanel::resized()
    {
        auto area = getLocalBounds();
        list->setBounds (area.removeFromLeft (kListWidth).reduced (1));
        list->setRowHeight (juce::jlimit (36, 54, list->getHeight() / (int) kStudio600Places.size()));
        area.removeFromLeft (16);
        cardArea = area;

        auto card = area.reduced (kPad, 16);
        savedHint.setBounds (card.removeFromBottom (20));
        card.removeFromBottom (6);

        auto head = card.removeFromTop (34);
        enabledToggle.setBounds (head.removeFromRight (120));
        title.setBounds (head);
        card.removeFromTop (10);

        soundLabel.setBounds (card.removeFromTop (18));
        auto sound = card.removeFromTop (30);
        const bool pieces = pieceBox.isVisible();
        packBox.setBounds (sound.removeFromLeft (juce::jmin (pieces ? 300 : 380, sound.getWidth() - (pieces ? 160 : 0))));
        if (pieces)
        {
            sound.removeFromLeft (10);
            pieceBox.setBounds (sound.removeFromLeft (juce::jmin (260, sound.getWidth())));
        }
        const bool warn = packWarning.getText().isNotEmpty();
        packWarning.setBounds (card.removeFromTop (warn ? 24 : 0));
        card.removeFromTop (14);

        levelLabel.setBounds (card.removeFromTop (18));
        auto lv = card.removeFromTop (30);
        listen.setBounds (lv.removeFromRight (120));
        lv.removeFromRight (16);
        level.setBounds (lv.removeFromLeft (juce::jmin (lv.getWidth(), 520)));
        card.removeFromTop (16);

        groupsLabel.setBounds (card.removeFromTop (18));
        card.removeFromTop (4);
        viewport.setBounds (card);
        noGroups.setBounds (card.withHeight (30));
        layoutGroups();
    }
}
