#include "KeyboardView.h"
#include "Brand.h"

#include <algorithm>

namespace samplit
{
    namespace
    {
        constexpr int kHintHeight = 24;
        constexpr int kZoneHeight = 16;     // полоса зон: какое место Studio600 на каких клавишах
        constexpr float kBlackWidth = 0.62f;
        constexpr float kBlackHeight = 0.62f;

        // Номер ноты в нумерации Cubase: 60 = C3
        juce::String noteName (int note)
        {
            static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            return juce::String (names[note % 12]) + juce::String (note / 12 - 2);
        }
    }

    KeyboardView::KeyboardView (SamplitProcessor& p) : processor (p)
    {
        setWantsKeyboardFocus (true);
        kit = processor.getKit();
        rebuildKeyInfo();
        processor.addChangeListener (this);
        startTimerHz (30);
    }

    KeyboardView::~KeyboardView()
    {
        processor.removeChangeListener (this);
    }

    bool KeyboardView::isBlack (int key) noexcept
    {
        const int pc = key % 12;
        return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
    }

    void KeyboardView::resized()
    {
        auto area = getLocalBounds();
        hintArea = area.removeFromTop (kHintHeight);
        zoneArea = area.removeFromTop (kZoneHeight + 2);
        const auto keys = area.toFloat();

        int whites = 0;
        for (int k = kFirstKey; k <= kLastKey; ++k)
            if (! isBlack (k))
                ++whites;

        const float wk = keys.getWidth() / (float) whites;
        const float bw = wk * kBlackWidth;
        const float bh = keys.getHeight() * kBlackHeight;

        keyRect.fill ({});
        int whiteIndex = 0;
        for (int k = kFirstKey; k <= kLastKey; ++k)
        {
            if (! isBlack (k))
            {
                keyRect[(size_t) k] = { keys.getX() + (float) whiteIndex * wk, keys.getY(), wk, keys.getHeight() };
                ++whiteIndex;
            }
            else
            {
                // Чёрная клавиша стоит на границе соседних белых
                keyRect[(size_t) k] = { keys.getX() + (float) whiteIndex * wk - bw * 0.5f, keys.getY(), bw, bh };
            }
        }
    }

    int KeyboardView::keyAt (juce::Point<int> p) const
    {
        const auto pf = p.toFloat();
        for (int k = kFirstKey; k <= kLastKey; ++k)
            if (isBlack (k) && keyRect[(size_t) k].contains (pf))
                return k;
        for (int k = kFirstKey; k <= kLastKey; ++k)
            if (! isBlack (k) && keyRect[(size_t) k].contains (pf))
                return k;
        return -1;
    }

    void KeyboardView::setSelectedKey (int key)
    {
        if (key != selectedKey && key >= kFirstKey && key <= kLastKey)
        {
            selectedKey = key;
            repaint();
        }
    }

    void KeyboardView::select (int key)
    {
        if (key < kFirstKey || key > kLastKey)
            return;
        setSelectedKey (key);
    }

    void KeyboardView::rebuildKeyInfo()
    {
        pieceAt.fill (-1);

        if (kit != nullptr)
        {
            for (int k = 0; k < 128; ++k)
                pieceAt[(size_t) k] = kit->notes[(size_t) k].piece;
            return;
        }
    }

    void KeyboardView::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        kit = processor.getKit();
        rebuildKeyInfo();
        repaint();
    }

    juce::String KeyboardView::pieceName (int key) const
    {
        if (kit == nullptr || ! juce::isPositiveAndBelow (key, 128) || pieceAt[(size_t) key] < 0)
            return {};
        return juce::String::fromUTF8 (kit->pieces[(size_t) pieceAt[(size_t) key]].name.c_str());
    }

    void KeyboardView::timerCallback()
    {
        const auto active = processor.getActiveKeys();
        bool dirty = active != shownActive;
        shownActive = active;

        // Подсказка о velocity живёт полторы секунды
        if (info.isNotEmpty() && juce::Time::getMillisecondCounter() - infoSince > 1500)
        {
            info.clear();
            dirty = true;
        }

        if (dirty)
            repaint();
    }

    void KeyboardView::mouseDown (const juce::MouseEvent& e)
    {
        grabKeyboardFocus();
        const int key = keyAt (e.getPosition());
        if (key < 0)
            return;

        // Верх клавиши тихо, низ громко
        const auto r = keyRect[(size_t) key];
        const float t = juce::jlimit (0.0f, 1.0f, (e.position.y - r.getY()) / r.getHeight());
        const int velocity = juce::jlimit (1, 127, 20 + juce::roundToInt (t * 107.0f));

        select (key);
        processor.triggerNote (key, velocity);
        info = noteName (key) + utf8 ("  ·  velocity ") + juce::String (velocity);
        if (kit != nullptr)
            info << utf8 ("  ·  ") << (pieceAt[(size_t) key] >= 0 ? pieceName (key) : utf8 ("пусто"));
        infoSince = juce::Time::getMillisecondCounter();
        repaint();
    }

    bool KeyboardView::keyPressed (const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::leftKey)
            select (selectedKey - 1);
        else if (k == juce::KeyPress::rightKey)
            select (selectedKey + 1);
        else if (k == juce::KeyPress::spaceKey)
            processor.triggerNote (selectedKey, 100);
        else
            return false;
        return true;
    }

    // ---------------------------------------------------------------- рисование

    juce::String KeyboardView::hintText() const
    {
        if (info.isNotEmpty())
            return info;

        if (kit != nullptr)
        {
            const auto name = pieceName (selectedKey);
            return noteName (selectedKey) + (name.isNotEmpty() ? utf8 ("  ·  ") + name : juce::String())
                   + utf8 ("  ·  клик ниже — сильнее удар  ·  ←/→ выбор клавиши, пробел играет");
        }

        return utf8 ("Клик играет клавишу; клик ниже — сильнее удар  ·  ←/→ выбор клавиши");
    }

    void KeyboardView::paint (juce::Graphics& g)
    {
        g.setColour (brand::textMuted);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (hintText(), hintArea, juce::Justification::centredLeft);

        // Полоса зон над клавишами: бочка, малый, тарелки, хэт, райд, томы
        for (const auto& place : kStudio600Places)
        {
            const int lo = juce::jmax (place.keyLo, kFirstKey), hi = juce::jmin (place.keyHi, kLastKey);
            if (lo > hi)
                continue;
            // Подсказка зависит от маппинга: зона видна, только если в загруженном ките на её клавишах есть звук
            bool mapped = false;
            for (int k = lo; k <= hi && ! mapped; ++k)
                mapped = pieceAt[(size_t) k] >= 0;
            if (! mapped)
                continue;
            const float x0 = keyRect[(size_t) lo].getX(), x1 = keyRect[(size_t) hi].getRight();
            const auto zone = juce::Rectangle<float> (x0, (float) zoneArea.getY(), x1 - x0, (float) kZoneHeight).reduced (0.5f, 0.0f);
            g.setColour (brand::groupColour (place.slot).withAlpha (0.85f));
            g.fillRoundedRectangle (zone, 3.0f);
            if (zone.getWidth() >= 56.0f)
            {
                g.setColour (brand::onAccent);
                g.setFont (juce::FontOptions (11.0f));
                g.drawText (utf8 (place.label), zone.reduced (3.0f, 0.0f), juce::Justification::centred, true);
            }
        }

        const auto isLit = [&] (int key)
        {
            const auto& word = shownActive[(size_t) (key / 64)];
            return (word >> (key % 64)) & 1u;
        };

        // Сначала белые, потом чёрные поверх
        for (int pass = 0; pass < 2; ++pass)
        {
            for (int k = kFirstKey; k <= kLastKey; ++k)
            {
                const bool black = isBlack (k);
                if ((pass == 1) != black)
                    continue;

                const auto r = keyRect[(size_t) k].reduced (0.5f, 0.0f);
                const int piece = pieceAt[(size_t) k];
                const bool lit = isLit (k) != 0;

                // Цвет клавиши: звучит, подкрашена по барабану, пусто
                juce::Colour fill = black ? juce::Colour (0xff10131a) : juce::Colour (0xffdfe3ea);
                if (piece >= 0)
                    fill = fill.interpolatedWith (brand::groupColour (piece), black ? 0.55f : 0.6f);
                if (lit)
                    fill = brand::accentHover;

                g.setColour (fill);
                g.fillRoundedRectangle (r, 3.0f);
                g.setColour (brand::line);
                g.drawRoundedRectangle (r, 3.0f, 1.0f);

                if (k == selectedKey)
                {
                    g.setColour (brand::accent);
                    g.drawRoundedRectangle (r.reduced (1.0f), 3.0f, 2.0f);
                }

                const auto ink = (black && ! lit) ? brand::text : brand::onAccent;
                if (! black && k % 12 == 0)
                {
                    g.setColour (brand::onAccent.withAlpha (0.7f));
                    g.setFont (juce::FontOptions (10.0f));
                    g.drawText (noteName (k), r.withTrimmedBottom (4.0f).toNearestInt(), juce::Justification::centredBottom);
                }
            }
        }
    }
}
