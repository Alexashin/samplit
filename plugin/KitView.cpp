#include "KitView.h"
#include "Brand.h"

#include <algorithm>

namespace samplit
{
    namespace
    {
        constexpr float kStageWidth = 1.65f, kStageHeight = 1.0f;

        // Бронза тарелок и пластики барабанов приглушены под тёмный интерфейс
        const juce::Colour kBrass { 0xffb48a45 };
        const juce::Colour kBrassDark { 0xff6f5228 };
        const juce::Colour kShell { 0xff1d2331 };
        const juce::Colour kHoop { 0xffaab1bc };
        const juce::Colour kHead { 0xffb9b3a4 };

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
    }

    KitView::KitView (SamplitProcessor& p) : processor (p)
    {
        emptyText.setColour (juce::Label::textColourId, brand::textMuted);
        emptyText.setFont (fonts::text (13.0f));
        emptyText.setJustificationType (juce::Justification::centred);
        emptyText.setText (utf8 ("Звук пока не загружен. Выберите папку со звуком: «Установка ▾», «Папка со звуком…»."),
                           juce::dontSendNotification);
        addChildComponent (emptyText);

        setTooltip (utf8 ("Клик по барабану играет его и выбирает: чем ниже клик, тем сильнее удар. Настройки справа"));

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

        auto stage = stageArea.reduced (12, 10);
        const float scale = juce::jmin ((float) stage.getWidth() / kStageWidth, (float) stage.getHeight() / kStageHeight);
        const auto origin = stage.toFloat().getCentre() - juce::Point<float> (kStageWidth, kStageHeight) * (scale * 0.5f);

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

    // Левый клик играет барабан и выбирает его: сила по высоте клика, ниже = сильнее (одно правило со схемой и клавиатурой).
    // Правый клик только выбирает
    void KitView::mouseDown (const juce::MouseEvent& e)
    {
        const int index = spotAt (e.position);
        if (index < 0)
            return;
        const auto& spot = spots[(size_t) index];
        setSelected (juce::String (spot.placeId));
        if (onSelect)
            onSelect (juce::String (spot.placeId));
        if (e.mods.isPopupMenu())
            return;
        if (const auto* def = findPlace (spot.placeId))
        {
            const float t = juce::jlimit (0.0f, 1.0f, (e.position.y - spot.bounds.getY()) / juce::jmax (1.0f, spot.bounds.getHeight()));
            processor.triggerNote (def->keyLo, juce::jlimit (1, 127, 24 + juce::roundToInt (t * 103.0f)));
        }
    }

    void KitView::setSelected (const juce::String& placeId)
    {
        if (placeId.toStdString() == selectedId)
            return;
        selectedId = placeId.toStdString();
        repaint (stageArea);
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
        stageArea = area;
        layoutSpots();
        emptyText.setBounds (stageArea.reduced (40, 0).withSizeKeepingCentre (stageArea.getWidth() - 80, 60));
        repaint();
    }

    namespace
    {
        // Барабан сверху: корпус, металлический обод с винтами, пластик. Удар подсвечивает пластик акцентом
        void drawDrum (juce::Graphics& g, juce::Rectangle<float> b, float flashing, bool kick)
        {
            const auto c = b.getCentre();
            g.setColour (kShell);
            g.fillEllipse (b);
            g.setColour (kHoop.withAlpha (0.55f));
            g.drawEllipse (b.reduced (1.0f), 2.0f);

            // Винты обода
            const int lugs = kick ? 12 : 8;
            const float lugR = b.getWidth() * 0.5f - 5.0f;
            g.setColour (kHoop.withAlpha (0.5f));
            for (int i = 0; i < lugs; ++i)
            {
                const float a = juce::MathConstants<float>::twoPi * (float) i / (float) lugs;
                g.fillEllipse (juce::Rectangle<float> (2.6f, 2.6f).withCentre ({ c.x + lugR * std::sin (a), c.y - lugR * std::cos (a) }));
            }

            const auto head = b.reduced (b.getWidth() * 0.12f);
            g.setColour (kHead.withMultipliedBrightness (0.62f).interpolatedWith (brand::accentText, flashing * 0.6f));
            g.fillEllipse (head);
            g.setColour (kHoop.withAlpha (0.3f));
            g.drawEllipse (head, 1.0f);
            g.drawEllipse (head.reduced (head.getWidth() * 0.22f), 1.0f);
            if (kick)
            {
                const auto port = juce::Rectangle<float> (head.getWidth() * 0.15f, head.getWidth() * 0.15f).withCentre ({ c.x, c.y + head.getHeight() * 0.3f });
                g.setColour (kShell);
                g.fillEllipse (port);
                g.setColour (kHoop.withAlpha (0.45f));
                g.drawEllipse (port, 1.0f);
            }
        }

        // Тарелка: плоская бронза, кольца проточки, колокол
        void drawCymbal (juce::Graphics& g, juce::Rectangle<float> b, float flashing)
        {
            const auto c = b.getCentre();
            g.setColour (kBrass.interpolatedWith (brand::accentText, flashing * 0.55f));
            g.fillEllipse (b);
            g.setColour (kBrassDark.withAlpha (0.55f));
            g.drawEllipse (b.reduced (1.0f), 1.0f);
            for (float k : { 0.25f, 0.45f, 0.65f })
                g.drawEllipse (b.reduced (b.getHeight() * k * 0.5f, b.getHeight() * k * 0.5f), 0.8f);
            g.setColour (kBrass.brighter (0.45f).interpolatedWith (brand::accentText, flashing * 0.55f));
            g.fillEllipse (juce::Rectangle<float> (b.getHeight() * 0.34f, b.getHeight() * 0.34f).withCentre (c));
        }
    }

    void KitView::paint (juce::Graphics& g)
    {
        const auto stage = stageArea.toFloat();
        g.setColour (brand::surface);
        g.fillRoundedRectangle (stage, brand::radiusM);
        g.setColour (brand::line);
        g.drawRoundedRectangle (stage.reduced (0.5f), brand::radiusM, 1.0f);

        for (int i = 0; i < (int) spots.size(); ++i)
        {
            const auto& s = spots[(size_t) i];
            const bool hot = i == hovered;
            const bool selected = s.placeId == selectedId;
            const float f = juce::isPositiveAndBelow (s.piece, (int) flash.size()) ? flash[(size_t) s.piece] : 0.0f;

            // Выключенный барабан затемнён, но остаётся выбираемым
            g.beginTransparencyLayer (s.enabled ? 1.0f : 0.3f);
            if (s.cymbal)
                drawCymbal (g, s.bounds, f);
            else
                drawDrum (g, s.bounds, f, s.order == 0);
            g.endTransparencyLayer();

            // Выбор: постоянный контур с зазором; наведение: тот же контур бледнее
            if (selected || hot)
            {
                g.setColour (selected ? brand::accent : brand::accentText.withAlpha (0.5f));
                g.drawEllipse (s.bounds.expanded (selected ? 5.0f : 4.0f), selected ? 2.0f : 1.5f);
            }

            // Подпись: у барабанов внутри, у тарелок под ними; выбранный барабан с плашкой акцента
            const auto font = fonts::strong (juce::jlimit (12.0f, 13.0f, s.bounds.getHeight() * 0.3f));
            g.setFont (font);
            auto labelBox = s.cymbal ? s.bounds.withY (s.bounds.getBottom() + 6.0f).withHeight (18.0f).expanded (30.0f, 0.0f)
                                     : s.bounds.withSizeKeepingCentre (s.bounds.getWidth() * 0.9f, 18.0f);
            if (selected)
            {
                const float w = (float) juce::GlyphArrangement::getStringWidthInt (font, s.label) + 14.0f;
                g.setColour (brand::accent);
                g.fillRoundedRectangle (labelBox.withSizeKeepingCentre (juce::jmin (w, labelBox.getWidth()), 18.0f), brand::radiusS);
                g.setColour (brand::onAccent);
            }
            else
                g.setColour (s.cymbal ? (hot ? brand::text : brand::textMuted) : brand::text.withAlpha (s.enabled ? 1.0f : 0.5f));
            g.drawText (s.label, labelBox, juce::Justification::centred, true);
        }
    }
}
