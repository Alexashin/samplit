#include "KitView.h"
#include "Brand.h"

#include <algorithm>

namespace samplit
{
    namespace
    {
        constexpr int kHintHeight = 26;
        constexpr float kStageWidth = 1.65f, kStageHeight = 1.0f;

        const juce::Colour kBrass { 0xffc9a85c };
        const juce::Colour kBrassDark { 0xff6f5a2c };

        // Вид сверху: положение инструментов как на схеме Studio600.
        struct Place
        {
            const char* id;
            float x, y, rx, ry;
            bool cymbal;
            const char* label;
        };

        // Координаты в сцене 1,65 x 1,0 (центр и полуоси). Вид сверху: барабанщик снизу, бочка по центру, томы дугой над ней,
        // тарелки по краям. Ничто не пересекается (проверено по снимку окна), подписи тарелок под ними
        const Place kPlaces[] = {
            { "kick",      0.825f, 0.66f, 0.20f, 0.19f, false, "Бочка" },
            { "tom_10",    0.70f,  0.30f, 0.095f, 0.095f, false, "Альт 10" },
            { "tom_14",    0.95f,  0.30f, 0.11f, 0.11f, false, "Альт 12" },
            { "tom_16m",   1.19f,  0.52f, 0.12f, 0.12f, false, "Том 14" },
            { "tom_16l",   1.42f,  0.80f, 0.14f, 0.14f, false, "Флор том 16" },
            { "snare",     0.43f,  0.70f, 0.14f, 0.14f, false, "Малый" },
            { "hat",       0.15f,  0.62f, 0.13f, 0.045f, true, "Hi-hat" },
            { "crash_b",   0.33f,  0.27f, 0.17f, 0.055f, true, "Crash 16\"" },
            { "crash_a",   1.27f,  0.20f, 0.17f, 0.060f, true, "Crash 18\"" },
            { "ride",      1.47f,  0.42f, 0.17f, 0.060f, true, "Ride" },
            { "china",     1.55f,  0.08f, 0.075f, 0.030f, true, "China" },
            { "splash",    0.60f,  0.09f, 0.070f, 0.026f, true, "Splash" },
        };

        // Подписи режимов выбора удара в меню места
        const char* pickLabel (Pick p)
        {
            switch (p)
            {
                case Pick::Cycle:  return "По кругу";
                case Pick::Random: return "Случайно";
                case Pick::Exact:  return "Точно по силе удара";
            }
            return "";
        }
    }

    KitView::KitView (SamplitProcessor& p) : processor (p)
    {
        emptyText.setColour (juce::Label::textColourId, brand::textMuted);
        emptyText.setFont (juce::FontOptions (13.0f));
        emptyText.setJustificationType (juce::Justification::centred);
        emptyText.setText (utf8 ("Звук пока не загружен. Выберите папку библиотеки («Моя библиотека…») в шапке окна."),
                           juce::dontSendNotification);
        addChildComponent (emptyText);

        setTooltip (utf8 ("Левый клик: сыграть барабан. Правый клик: настройки места (включить, выбрать звук, режим ударов)"));

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
            refreshState();
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
        kit = processor.getKit();
        flash.clear();
        pieceKeys.clear();
        hovered = -1;

        if (kit != nullptr)
        {
            flash.assign (kit->pieces.size(), 0.0f);
            pieceKeys.assign (kit->pieces.size(), {});
            for (int k = 0; k < 128; ++k)
                if (const int p = kit->notes[(size_t) k].piece; p >= 0)
                    pieceKeys[(size_t) p].push_back (k);
        }

        layoutSpots();
        emptyText.setVisible (kit == nullptr && ! processor.isKitLoading());
        repaint();
    }

    // Только состояние мест (включено, какой барабан играет): кит прежний
    void KitView::refreshState()
    {
        const auto setup = processor.getSetup();
        for (auto& s : spots)
            if (const auto* ps = setup.find (s.placeId))
                s.enabled = ps->enabled;
        emptyText.setVisible (kit == nullptr && ! processor.isKitLoading());
        repaint (stageArea);
    }

    // Пересчёт экранных прямоугольников мест по размеру сцены. Барабан кита ищется по месту (Piece::slot)
    void KitView::layoutSpots()
    {
        spots.clear();
        stool = {};

        auto stage = stageArea.reduced (12, 10);
        const float scale = juce::jmin ((float) stage.getWidth() / kStageWidth, (float) stage.getHeight() / kStageHeight);
        const auto origin = stage.toFloat().getCentre() - juce::Point<float> (kStageWidth, kStageHeight) * (scale * 0.5f);
        stool = juce::Rectangle<float> (0.17f * scale, 0.13f * scale)
                    .withCentre (origin + juce::Point<float> (0.825f, 0.935f) * scale);

        const auto setup = processor.getSetup();
        for (const auto& pl : kPlaces)
        {
            const auto* def = findPlace (pl.id);
            if (def == nullptr)
                continue;
            Spot s;
            s.placeId = pl.id;
            s.slot = def->slot;
            s.cymbal = pl.cymbal;
            s.order = std::string (pl.id) == "kick" ? 0 : (pl.cymbal ? 2 : 1);
            s.label = utf8 (pl.label);
            if (const auto* ps = setup.find (s.placeId))
                s.enabled = ps->enabled;
            if (kit != nullptr)
                for (int i = 0; i < (int) kit->pieces.size(); ++i)
                    if (kit->pieces[(size_t) i].slot == def->slot)
                        s.piece = i;
            s.bounds = juce::Rectangle<float> (pl.rx * 2.0f * scale, pl.ry * 2.0f * scale)
                           .withCentre (origin + juce::Point<float> (pl.x, pl.y) * scale);
            spots.push_back (s);
        }
        std::stable_sort (spots.begin(), spots.end(), [] (const Spot& a, const Spot& b) { return a.order < b.order; });
    }

    int KitView::spotAt (juce::Point<float> p) const
    {
        for (int i = (int) spots.size() - 1; i >= 0; --i)
        {
            const auto& b = spots[(size_t) i].bounds;
            if ((std::pow ((p.x - b.getCentreX()) / (b.getWidth() * 0.5f), 2.0f)
                     + std::pow ((p.y - b.getCentreY()) / (b.getHeight() * 0.5f), 2.0f)) <= 1.0f)
                return i;
        }
        return -1;
    }

    // Меню места: включено, звук (пак) и режимы групп слоёв текущего кита. Все изменения идут через процессор
    void KitView::showPlaceMenu (int spotIndex)
    {
        if (! juce::isPositiveAndBelow (spotIndex, (int) spots.size()))
            return;
        const auto spot = spots[(size_t) spotIndex];
        const auto* def = findPlace (spot.placeId);
        const auto setup = processor.getSetup();
        const auto* ps = setup.find (spot.placeId);
        if (def == nullptr || ps == nullptr)
            return;

        juce::PopupMenu menu;
        menu.addSectionHeader (spot.label);
        menu.addItem (2, utf8 ("Открыть в «Инструментах»"), onOpenInstruments != nullptr);
        menu.addSeparator();
        menu.addItem (1, utf8 ("Включено"), true, ps->enabled);

        // Звук (пак): паки с барабанами нужного типа; барабаны выбираются, если в паке их несколько этого типа
        struct PackChoice { juce::String packId, pieceId; };
        std::vector<PackChoice> packChoices;
        constexpr int kPackBase = 100;
        const auto catalog = processor.getCatalog();
        const auto packs = packsForPlace (catalog, *def);
        const juce::String currentPack (ps->packId), currentPiece (ps->pieceId);

        juce::PopupMenu soundMenu;
        bool found = false;
        for (const auto& pk : packs)
        {
            const auto title = pk.title.isNotEmpty() ? pk.title : pk.packId;
            const bool isCurrent = pk.packId == currentPack;
            found = found || isCurrent;
            if (pk.pieces.size() == 1)
            {
                packChoices.push_back ({ pk.packId, pk.pieces[0].id });
                soundMenu.addItem (kPackBase + (int) packChoices.size() - 1, title, true, isCurrent);
                continue;
            }
            juce::PopupMenu drums;
            for (const auto& piece : pk.pieces)
            {
                packChoices.push_back ({ pk.packId, piece.id });
                const bool tick = isCurrent && (currentPiece.isEmpty() || currentPiece == piece.id);
                drums.addItem (kPackBase + (int) packChoices.size() - 1, piece.name.isNotEmpty() ? piece.name : piece.id, true, tick);
            }
            soundMenu.addSubMenu (title, drums, true, juce::Image(), isCurrent);
        }
        if (! found && currentPack.isNotEmpty())
        {
            if (soundMenu.getNumItems() > 0)
                soundMenu.addSeparator();
            soundMenu.addItem (-1, utf8 ("Пак не найден: ") + currentPack, false);
        }
        if (soundMenu.getNumItems() == 0)
            soundMenu.addItem (-1, utf8 ("Подходящих паков нет"), false);
        menu.addSubMenu (utf8 ("Звук (пак)"), soundMenu);

        // Режимы групп слоёв барабана этого места в текущем ките. Группа с одним допустимым режимом переключателя не имеет
        struct PickChoiceItem { juce::String choiceId; Pick pick; };
        std::vector<PickChoiceItem> pickChoices;
        constexpr int kPickBase = 1000;
        if (kit != nullptr && spot.piece >= 0)
        {
            const CatalogPiece* catalogPiece = nullptr;
            for (const auto& pk : catalog)
                if (pk.packId == currentPack)
                    for (const auto& piece : pk.pieces)
                        if (piece.id == juce::String (kit->pieces[(size_t) spot.piece].id))
                            catalogPiece = &piece;

            juce::PopupMenu groups;
            const auto prefix = juce::String (spot.placeId) + ":";
            for (int c = 0; c < (int) kit->choices.size(); ++c)
            {
                const auto& choice = kit->choices[(size_t) c];
                const juce::String choiceId (choice.id);
                if (! choiceId.startsWith (prefix))
                    continue;

                std::vector<Pick> allowed { Pick::Cycle, Pick::Random, Pick::Exact };
                if (catalogPiece != nullptr)
                    for (const auto& g : catalogPiece->groups)
                        if (g.id == choiceId.substring (prefix.length()) && ! g.picks.empty())
                        {
                            std::vector<Pick> only;
                            for (auto pick : allowed)
                                if (std::find (g.picks.begin(), g.picks.end(), pickToString (pick)) != g.picks.end())
                                    only.push_back (pick);
                            allowed = std::move (only);
                        }
                if (allowed.size() < 2)
                    continue;

                const auto current = processor.getPickChoice (c);
                juce::PopupMenu modes;
                for (auto pick : allowed)
                {
                    pickChoices.push_back ({ choiceId, pick });
                    modes.addItem (kPickBase + (int) pickChoices.size() - 1, utf8 (pickLabel (pick)), true, pick == current);
                }
                groups.addSubMenu (juce::String::fromUTF8 (choice.name.c_str()), modes);
            }
            if (groups.getNumItems() > 0)
            {
                menu.addSeparator();
                menu.addSectionHeader (utf8 ("Как выбирается удар"));
                // Группы идут в одном подменю, если их много, иначе прямо в меню
                if (groups.getNumItems() > 4)
                    menu.addSubMenu (utf8 ("Режимы групп"), groups);
                else
                {
                    juce::PopupMenu::MenuItemIterator it (groups);
                    while (it.next())
                        menu.addItem (it.getItem());
                }
            }
        }

        const auto target = juce::Rectangle<int> (spot.bounds.toNearestInt()).translated (getScreenX(), getScreenY());
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetScreenArea (target),
                            [safe = juce::Component::SafePointer<KitView> (this), placeId = juce::String (spot.placeId),
                             enabled = ps->enabled, packChoices, pickChoices] (int result)
        {
            if (safe == nullptr || result <= 0)
                return;
            auto& p = safe->processor;
            if (result == 2)
            {
                if (safe->onOpenInstruments)
                    safe->onOpenInstruments (placeId);
            }
            else if (result == 1)
            {
                p.setPlaceEnabled (placeId, ! enabled);
                safe->refreshState();
            }
            else if (result >= kPickBase)
            {
                const auto index = (size_t) (result - kPickBase);
                if (index < pickChoices.size())
                    p.setGroupPick (pickChoices[index].choiceId, pickChoices[index].pick);
            }
            else if (result >= kPackBase)
            {
                const auto index = (size_t) (result - kPackBase);
                if (index < packChoices.size())
                    p.setPlacePack (placeId, packChoices[index].packId, packChoices[index].pieceId);
            }
        });
    }

    void KitView::mouseDown (const juce::MouseEvent& e)
    {
        // Левая кнопка играет барабан, правая открывает меню места
        const int index = spotAt (e.position);
        if (index < 0)
            return;
        if (e.mods.isPopupMenu())
            showPlaceMenu (index);
        else if (const auto* def = findPlace (spots[(size_t) index].placeId))
            processor.triggerNote (def->keyLo, 100);
    }

    void KitView::mouseMove (const juce::MouseEvent& e)
    {
        const int index = spotAt (e.position);
        if (index != hovered)
        {
            hovered = index;
            setMouseCursor (index >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
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
        hintArea = area.removeFromBottom (kHintHeight);
        stageArea = area;
        layoutSpots();
        emptyText.setBounds (stageArea.reduced (40, 0).withSizeKeepingCentre (stageArea.getWidth() - 80, 60));
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

        if (! stool.isEmpty())
        {
            g.setColour (brand::pad.brighter (0.25f));
            g.fillEllipse (stool);
            g.setColour (brand::line);
            g.drawEllipse (stool, 1.0f);
        }

        for (int i = 0; i < (int) spots.size(); ++i)
        {
            const auto& s = spots[(size_t) i];
            const bool hot = i == hovered;
            const float f = juce::isPositiveAndBelow (s.piece, (int) flash.size()) ? flash[(size_t) s.piece] : 0.0f;

            // Выключенное место приглушено
            if (! s.enabled)
                g.beginTransparencyLayer (0.35f);
            if (s.cymbal)
                drawCymbal (g, s.bounds, f, false, hot);
            else
                drawDrum (g, s.bounds, f, false, hot, s.order == 1);

            g.setFont (juce::FontOptions (juce::jlimit (11.0f, 13.0f, s.bounds.getHeight() * 0.35f), juce::Font::bold));
            if (s.cymbal)
            {
                g.setColour (hot ? brand::text : brand::textMuted);
                g.drawText (s.label, s.bounds.withY (s.bounds.getBottom() + 1.0f).withHeight (16.0f).expanded (30.0f, 0.0f),
                            juce::Justification::centred, false);
            }
            else
            {
                g.setColour (brand::text);
                g.drawText (s.label, s.bounds.toNearestInt(), juce::Justification::centred, true);
            }
            if (! s.enabled)
                g.endTransparencyLayer();
        }

        g.setColour (brand::textMuted);
        g.setFont (juce::FontOptions (11.5f));
        g.drawText (utf8 ("Левый клик играет барабан, правый открывает настройки: включить или выключить, выбрать звук и режим выбора удара. Сила удара выбирает слой, записи внутри слоя чередуются по режиму."), hintArea.reduced (14, 0),
                    juce::Justification::centredLeft, true);
    }
}
