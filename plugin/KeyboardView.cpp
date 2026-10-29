#include "KeyboardView.h"
#include "Brand.h"

#include <algorithm>

namespace samplit
{
    namespace
    {
        constexpr int kHintHeight = 24;
        constexpr float kBlackWidth = 0.62f;
        constexpr float kBlackHeight = 0.62f;

        bool isAudioFile (const juce::File& f)
        {
            return f.existsAsFile() && f.hasFileExtension ("wav;aif;aiff;flac;ogg");
        }

        // Файлы как есть, а из папки берутся все аудиофайлы первого уровня по порядку имён
        juce::Array<juce::File> collectAudioFiles (const juce::StringArray& paths)
        {
            juce::Array<juce::File> result;
            for (const auto& path : paths)
            {
                const juce::File f (path);
                if (f.isDirectory())
                {
                    auto inFolder = f.findChildFiles (juce::File::findFiles, false, SamplitProcessor::getSupportedWildcard());
                    std::sort (inFolder.begin(), inFolder.end(), [] (const juce::File& a, const juce::File& b)
                    {
                        return a.getFileName().compareNatural (b.getFileName()) < 0;
                    });
                    result.addArray (inFolder);
                }
                else if (isAudioFile (f))
                {
                    result.add (f);
                }
            }
            return result;
        }

        bool altDown()
        {
            return juce::ModifierKeys::getCurrentModifiersRealtime().isAltDown();
        }
    }

    KeyboardView::KeyboardView (SamplitProcessor& p) : processor (p)
    {
        setWantsKeyboardFocus (true);
        instrument = processor.getInstrument();
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

    void KeyboardView::setSelectedGroup (int group)
    {
        selectedGroup = group;
        repaint();
    }

    void KeyboardView::select (int key)
    {
        if (key < kFirstKey || key > kLastKey)
            return;
        setSelectedKey (key);
        if (onSelectKey)
            onSelectKey (key);
    }

    void KeyboardView::rebuildKeyInfo()
    {
        zoneCount.fill (0);
        groupBits.fill (0);
        pieceAt.fill (-1);

        if (kit != nullptr)
        {
            for (int k = 0; k < 128; ++k)
                pieceAt[(size_t) k] = kit->notes[(size_t) k].piece;
            return;
        }
        if (instrument == nullptr)
            return;

        for (const auto& z : instrument->zones)
        {
            for (int k = juce::jmax (0, z.lowKey); k <= juce::jmin (127, z.highKey); ++k)
            {
                ++zoneCount[(size_t) k];
                if (z.group >= 0 && z.group < 16)
                    groupBits[(size_t) k] |= (uint16_t) (1u << z.group);
            }
        }
    }

    void KeyboardView::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        instrument = processor.getInstrument();
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

        // Подсказка о velocity живёт полторы секунды, а при перетаскивании следим за Alt
        if (info.isNotEmpty() && juce::Time::getMillisecondCounter() - infoSince > 1500)
        {
            info.clear();
            dirty = true;
        }
        if (hoverKey >= 0)
            dirty = true;

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

    // ---------------------------------------------------------------- перетаскивание

    void KeyboardView::setHover (int key)
    {
        if (key != hoverKey)
        {
            hoverKey = key;
            repaint();
        }
    }

    void KeyboardView::endDrag()
    {
        hoverKey = -1;
        dragFiles = 0;
        repaint();
    }

    bool KeyboardView::isInterestedInDragSource (const SourceDetails& d)
    {
        return kit == nullptr && d.description.isString() && isAudioFile (juce::File (d.description.toString()));
    }

    void KeyboardView::itemDragEnter (const SourceDetails& d)
    {
        dragFiles = 1;
        setHover (keyAt (d.localPosition));
    }

    void KeyboardView::itemDropped (const SourceDetails& d)
    {
        const int key = keyAt (d.localPosition);
        endDrag();
        if (key >= 0)
            drop ({ juce::File (d.description.toString()) }, key);
    }

    bool KeyboardView::isInterestedInFileDrag (const juce::StringArray& files)
    {
        if (kit != nullptr)
            return false;
        for (const auto& f : files)
            if (juce::File (f).isDirectory() || isAudioFile (juce::File (f)))
                return true;
        return false;
    }

    void KeyboardView::fileDragEnter (const juce::StringArray& files, int x, int y)
    {
        dragFiles = collectAudioFiles (files).size();
        setHover (keyAt ({ x, y }));
    }

    void KeyboardView::filesDropped (const juce::StringArray& files, int x, int y)
    {
        const int key = keyAt ({ x, y });
        endDrag();
        if (key >= 0)
            drop (collectAudioFiles (files), key);
    }

    void KeyboardView::drop (const juce::Array<juce::File>& files, int key)
    {
        if (files.isEmpty())
            return;

        // Без групп класть некуда: заводим первую
        int group = selectedGroup;
        const auto inst = processor.getInstrument();
        if (inst == nullptr || inst->groups.empty())
            group = processor.addGroup (utf8 ("Группа 1"));
        else
            group = juce::jlimit (0, (int) inst->groups.size() - 1, group);
        if (group < 0)
            return;

        const auto spread = altDown() ? Spread::VelocityLayers : Spread::Variations;
        const int added = processor.addFilesToKey (files, group, key, spread);
        select (key);

        info = noteName (key) + utf8 (": добавлено ") + juce::String (added);
        infoSince = juce::Time::getMillisecondCounter();
        repaint();
    }

    // ---------------------------------------------------------------- рисование

    juce::String KeyboardView::hintText() const
    {
        if (hoverKey >= 0 && dragFiles > 0)
        {
            const auto groupName = (instrument != nullptr && juce::isPositiveAndBelow (selectedGroup, (int) instrument->groups.size()))
                                       ? instrument->groups[(size_t) selectedGroup].name
                                       : juce::String();
            const auto how = altDown() ? utf8 ("слои по velocity") : utf8 ("вариации");
            return utf8 ("Отпустите: ") + juce::String (dragFiles) + utf8 (" на ") + noteName (hoverKey)
                   + utf8 (" в группу «") + groupName + utf8 ("», ") + how + utf8 ("  ·  Alt: слои по velocity");
        }

        if (info.isNotEmpty())
            return info;

        if (kit != nullptr)
        {
            const auto name = pieceName (selectedKey);
            return noteName (selectedKey) + (name.isNotEmpty() ? utf8 ("  ·  ") + name : juce::String())
                   + utf8 ("  ·  клик играет барабан, ниже по клавише сильнее удар  ·  ←/→ выбор клавиши, пробел играет");
        }

        return utf8 ("Клик играет клавишу, ниже = громче  ·  перетащите файл или папку на клавишу  ·  Alt: слои по velocity  ·  ←/→ выбор клавиши");
    }

    void KeyboardView::paint (juce::Graphics& g)
    {
        g.setColour (brand::textMuted);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (hintText(), hintArea, juce::Justification::centredLeft);

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
                const int count = zoneCount[(size_t) k];
                const int piece = pieceAt[(size_t) k];
                const bool lit = isLit (k) != 0;

                // Цвет клавиши: звучит, барабан или зоны, пусто
                juce::Colour fill = black ? juce::Colour (0xff10131a) : juce::Colour (0xffdfe3ea);
                if (piece >= 0)
                    fill = fill.interpolatedWith (brand::groupColour (piece), black ? 0.55f : 0.6f);
                else if (count > 0)
                {
                    int group = 0;
                    for (int b = 0; b < 16; ++b)
                        if (groupBits[(size_t) k] & (1u << b)) { group = b; break; }
                    fill = fill.interpolatedWith (brand::groupColour (group), black ? 0.55f : 0.6f);
                }
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
                if (k == hoverKey)
                {
                    g.setColour (brand::text);
                    g.drawRoundedRectangle (r.reduced (1.0f), 3.0f, 2.5f);
                }

                const auto ink = (black && ! lit) ? brand::text : brand::onAccent;
                if (count > 0)
                {
                    g.setColour (ink);
                    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                    g.drawText (juce::String (count), r.withTrimmedBottom (black ? 4.0f : 22.0f).toNearestInt(),
                                juce::Justification::centredBottom);
                }

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
