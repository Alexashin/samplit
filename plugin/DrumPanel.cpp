#include "DrumPanel.h"
#include "Brand.h"
#include "PatternPalette.h"

#include <algorithm>
#include <set>

namespace samplit
{
    namespace
    {
        constexpr int kPad = 16;

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

        juce::Font boldFont (float size) { return fonts::strong (size); }
        juce::Font plainFont (float size) { return fonts::text (size); }

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

        // Название звука без служебного префикса библиотеки
        juce::String packTitle (const CatalogPack& p)
        {
            auto t = p.title.isNotEmpty() ? p.title : p.packId;
            if (t.startsWithIgnoreCase ("Studio600 Drum LIB:"))
                t = t.fromFirstOccurrenceOf (":", false, false).trim();
            return t;
        }
    }

    // ---- Строка группы ударов ----

    struct DrumPanel::GroupRow final : juce::Component
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

        // Карта силы: кнопка на каждый вариант удара группы, клик играет клавишу с этим числом
        void setStrengths (const std::vector<StrengthOption>& opts, std::function<void (int, int)> play)
        {
            for (const auto& o : opts)
            {
                auto& b = *strengthButtons.emplace_back (std::make_unique<juce::TextButton> (
                    noteName (o.key) + utf8 (" · ") + (o.exact ? o.name : o.label) + " " + juce::String (o.velocity)));
                b.setTooltip (utf8 ("Сыграть: клавиша ") + noteName (o.key) + utf8 (", сила ") + juce::String (o.velocity));
                b.onClick = [play, key = o.key, vel = o.velocity] { if (play) play (key, vel); };
                addAndMakeVisible (b);
            }
        }

        static int strengthWidth (const juce::TextButton& b)
        {
            return juce::GlyphArrangement::getStringWidthInt (plainFont (12.0f), b.getButtonText()) + 20;
        }

        // Высота блока кнопок силы при ширине width (кнопки переносятся на новые строки)
        int strengthsHeight (int width) const
        {
            if (strengthButtons.empty())
                return 0;
            int x = 0, rowsCount = 1;
            for (auto& b : strengthButtons)
            {
                const int w = strengthWidth (*b);
                if (x > 0 && x + w > width)
                {
                    x = 0;
                    ++rowsCount;
                }
                x += w + 4;
            }
            return rowsCount * 28 + 6;
        }

        bool stacked (int width) const { return width < controlsWidth() + 320; }

        int heightFor (int width) const
        {
            int h = 12 + 20 + (hint.isNotEmpty() ? 18 : 0) + (where.isNotEmpty() ? 18 : 0) + strengthsHeight (width);
            if (! buttons.empty() && stacked (width))
                h += 36;
            return juce::jmax (h, buttons.empty() || stacked (width) ? 0 : 52);
        }

        void resized() override
        {
            const int sh = strengthsHeight (getWidth());
            if (sh > 0)
            {
                int x = 0, y = getHeight() - 1 - sh + 6;
                for (auto& b : strengthButtons)
                {
                    const int w = strengthWidth (*b);
                    if (x > 0 && x + w > getWidth())
                    {
                        x = 0;
                        y += 28;
                    }
                    b->setBounds (x, y, w, 24);
                    x += w + 4;
                }
            }
            if (buttons.empty())
                return;
            auto area = getLocalBounds().reduced (0, 8);
            area.removeFromBottom (sh);
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
            text.removeFromBottom (1 + strengthsHeight (getWidth()));
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
        std::vector<std::unique_ptr<juce::TextButton>> buttons, strengthButtons;
    };

    struct DrumPanel::GroupsContent final : juce::Component
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

    DrumPanel::DrumPanel (SamplitProcessor& p)
        : processor (p), groupsContent (std::make_unique<GroupsContent>())
    {
        setTitle (utf8 ("Барабан"));
        title.setFont (fonts::strong (17.0f));
        title.setMinimumHorizontalScale (0.8f);
        packWarning.setFont (fonts::text (12.5f));
        packWarning.setColour (juce::Label::textColourId, brand::warn);
        noGroups.setFont (fonts::text (12.5f));
        noGroups.setColour (juce::Label::textColourId, brand::textMuted);
        noGroups.setText (utf8 ("У этого звука нет настраиваемых групп: удар выбирается автоматически."),
                          juce::dontSendNotification);

        enabledLed.setTooltip (utf8 ("Выключенный барабан молчит и не загружается в память"));
        enabledLed.onClick = [this]
        {
            if (! updating)
                processor.setPlaceEnabled (juce::String (currentPlace().id), enabledLed.getToggleState());
        };

        packBox.setTitle (utf8 ("Звук"));
        packBox.setTooltip (utf8 ("Набор записей, который играет на этом барабане"));
        packBox.setTextWhenNothingSelected (utf8 ("Выберите звук"));
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
        pieceBox.setTooltip (utf8 ("В этом звуке несколько барабанов такого типа: выберите нужный"));
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

        levelKnob.setRange (kPlaceLevelMinDb, kPlaceLevelMaxDb, 0.1, 0.0);
        levelKnob.setTooltip (utf8 ("Громкость этого барабана, дБ. Двойной клик: 0 дБ. Уровень микрофонов настраивается в микшере"));
        levelKnob.format = [] (double v) { return (v > 0.05 ? "+" : "") + juce::String (v, 1) + utf8 (" дБ"); };
        levelKnob.parse = [] (const juce::String& t) { return t.retainCharacters ("0123456789.,-+").replaceCharacter (',', '.').getDoubleValue(); };
        levelKnob.onChange = [this]
        {
            if (! updating)
                processor.setPlaceLevel (juce::String (currentPlace().id), (float) levelKnob.getValue());
        };

        listen.setButtonText (utf8 ("Прослушать"));
        listen.setTooltip (utf8 ("Сыграть этот барабан один раз"));
        listen.onClick = [this] { processor.triggerNote (currentPlace().keyLo, 100); };

        disclosure.setTooltip (utf8 ("Группы ударов, режимы выбора записи и карта силы"));
        disclosure.onClick = [this] { groupsOpen = disclosure.getToggleState(); resized(); repaint(); };

        viewport.setViewedComponent (groupsContent.get(), false);
        viewport.setScrollBarsShown (true, false);
        viewport.setTitle (utf8 ("Группы ударов"));

        for (auto* c : std::initializer_list<juce::Component*> { &title, &enabledLed, &packBox, &pieceBox, &packWarning, &levelKnob,
                                                                  &listen, &disclosure, &viewport, &noGroups })
            addAndMakeVisible (c);

        processor.addChangeListener (this);
        refresh (true);
    }

    DrumPanel::~DrumPanel()
    {
        processor.removeChangeListener (this);
    }

    const PlaceDef& DrumPanel::currentPlace() const
    {
        return kStudio600Places[(size_t) juce::jlimit (0, (int) kStudio600Places.size() - 1, selected)];
    }

    void DrumPanel::selectPlace (const juce::String& placeId)
    {
        for (int i = 0; i < (int) kStudio600Places.size(); ++i)
            if (placeId == kStudio600Places[(size_t) i].id)
            {
                selected = i;
                refresh (true);
                return;
            }
    }

    void DrumPanel::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        refresh (false);
    }

    void DrumPanel::refresh (bool forceGroups)
    {
        const juce::ScopedValueSetter<bool> guard (updating, true);
        const auto setup = processor.getSetup();
        const auto catalog = processor.getCatalog();

        // Карточка
        const auto& def = currentPlace();
        const auto* ps = setup.find (def.id);
        if (ps == nullptr)
            return;
        packs = packsForPlace (catalog, def);

        title.setText (juce::String::fromUTF8 (def.label), juce::dontSendNotification);
        enabledLed.setToggleState (ps->enabled, juce::dontSendNotification);

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

        if (! levelKnob.getSlider().isMouseButtonDown() && std::abs (levelKnob.getValue() - (double) ps->levelDb) > 0.001)
            levelKnob.setValue (ps->levelDb);

        const bool on = ps->enabled;
        for (auto* c : std::initializer_list<juce::Component*> { &packBox, &pieceBox, &levelKnob, &listen })
            c->setEnabled (on);
        viewport.setAlpha (on ? 1.0f : 0.45f);
        viewport.setEnabled (on);
        viewport.getViewedComponent()->setEnabled (on);

        rebuildGroups (setup, def, *ps, packs);
        (void) forceGroups;
        resized();
        repaint();
    }

    // Строки групп: пересоздаются при смене места, пака, барабана или кита; иначе только обновляется режим
    // (строка не удаляется внутри собственного обработчика, прокрутка сохраняется)
    void DrumPanel::rebuildGroups (const Setup&, const PlaceDef& def, const PlaceSetup& ps,
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
        std::vector<StrengthOption> palette;
        if (kit != nullptr && piece != nullptr)
        {
            pack::Piece meta;
            const bool haveMeta = findPieceMeta (pks, def, ps, meta);
            palette = buildPalette (*kit, def.slot, haveMeta ? &meta : nullptr);
        }
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
                                                       [safe = juce::Component::SafePointer<DrumPanel> (this), choiceId] (Pick p)
                                                       {
                                                           if (safe != nullptr)
                                                               safe->processor.setGroupPick (choiceId, p);
                                                       });
                std::vector<StrengthOption> mine;
                for (const auto& o : palette)
                    if (o.groupId == g.id)
                        mine.push_back (o);
                row->setStrengths (mine, [safe = juce::Component::SafePointer<DrumPanel> (this)] (int key, int vel)
                                   {
                                       if (safe != nullptr)
                                           safe->processor.triggerNote (key, vel);
                                   });
                groupsContent->addAndMakeVisible (*row);
                groupsContent->rows.push_back (std::move (row));
            }

        noGroups.setVisible (groupsOpen && groupsContent->rows.empty());
        layoutGroups();
        viewport.setViewPosition (samePlace ? savedPos : juce::Point<int> (0, 0));
    }

    void DrumPanel::layoutGroups()
    {
        groupsContent->layoutRows (juce::jmax (100, viewport.getMaximumVisibleWidth()));
    }

    void DrumPanel::paint (juce::Graphics& g)
    {
        g.fillAll (brand::surface);
        g.setColour (brand::line);
        g.fillRect (0, 0, 1, getHeight());
        drawSectionCaption (g, utf8 ("Звук"), soundCaption);
    }

    void DrumPanel::resized()
    {
        auto area = getLocalBounds().reduced (kPad, kPad);

        auto head = area.removeFromTop (30);
        enabledLed.setBounds (head.removeFromRight (68));
        title.setBounds (head);
        area.removeFromTop (12);

        soundCaption = area.removeFromTop (18);
        area.removeFromTop (4);
        packBox.setBounds (area.removeFromTop (30));
        if (pieceBox.isVisible())
        {
            area.removeFromTop (6);
            pieceBox.setBounds (area.removeFromTop (30));
        }
        const bool warn = packWarning.getText().isNotEmpty();
        packWarning.setBounds (area.removeFromTop (warn ? 22 : 0));
        area.removeFromTop (16);

        auto row = area.removeFromTop (levelKnob.preferredHeight());
        levelKnob.setBounds (row.removeFromLeft (levelKnob.preferredWidth()));
        row.removeFromLeft (12);
        listen.setBounds (row.withSizeKeepingCentre (row.getWidth(), 32));
        area.removeFromTop (12);

        disclosure.setBounds (area.removeFromTop (28));
        area.removeFromTop (4);
        viewport.setVisible (groupsOpen);
        noGroups.setVisible (groupsOpen && groupsContent->rows.empty());
        viewport.setBounds (area);
        noGroups.setBounds (area.withHeight (40));
        layoutGroups();
    }
}
