#include "PatternPanel.h"
#include "Brand.h"

#include "samplit/core/Manifest.h"

#include <algorithm>
#include <cstdlib>
#include <map>

namespace samplit
{
    namespace
    {
        constexpr int kControlsHeight = 32;
        constexpr int kGap = 10;
        constexpr int kHeader = 22;
        constexpr int kLabelWidth = 210;
        constexpr int kMinCell = 36;
        constexpr int kMinRow = 22, kMaxRow = 46;
        constexpr int kScrollBar = 10;
        constexpr int kRemoveId = 100000;

        juce::Font boldFont (float size) { return juce::Font (juce::FontOptions (size, juce::Font::bold)); }
        juce::Font plainFont (float size) { return juce::Font (juce::FontOptions (size)); }

        // Сила по умолчанию: самый сильный удар основной группы (роль main) на нижней клавише, иначе самый сильный на первой клавише
        int defaultOption (const std::vector<StrengthOption>& p)
        {
            int best = -1;
            for (int i = 0; i < (int) p.size(); ++i)
                if (p[(size_t) i].role == "main"
                    && (best < 0 || p[(size_t) i].key < p[(size_t) best].key
                        || (p[(size_t) i].key == p[(size_t) best].key && p[(size_t) i].velocity > p[(size_t) best].velocity)))
                    best = i;
            if (best >= 0 || p.empty())
                return best;
            best = 0;
            for (int i = 1; i < (int) p.size(); ++i)
                if (p[(size_t) i].key == p[0].key && p[(size_t) i].velocity > p[(size_t) best].velocity)
                    best = i;
            return best;
        }

        // «Основной, сильно, 112»
        juce::String describe (const StrengthOption& o)
        {
            return o.name + ", " + o.label + ", " + juce::String (o.velocity);
        }

        juce::String noteName (int key) { return juce::MidiMessage::getMidiNoteName (key, true, true, 3); }

        juce::Colour textOn (juce::Colour fill) { return fill.getPerceivedBrightness() > 0.55f ? brand::onAccent : brand::text; }
    }

    // ---- Сетка: заголовок с тактами и долями, клетки шагов, курсор проигрывания ----

    struct PatternPanel::Grid final : juce::Component,
                                      juce::TooltipClient
    {
        explicit Grid (PatternPanel& o) : owner (o) {}

        PatternPanel& owner;
        int cellW = kMinCell, rowH = kMinRow;
        int hoverRow = -1, hoverStep = -1;

        bool cellAt (juce::Point<int> p, int& row, int& step) const
        {
            if (p.y < kHeader || cellW <= 0 || rowH <= 0)
                return false;
            row = (p.y - kHeader) / rowH;
            step = p.x / cellW;
            return row >= 0 && row < (int) owner.rows.size() && step >= 0 && step < owner.pattern.numSteps();
        }

        juce::Rectangle<int> cellBounds (int row, int step) const
        {
            return { step * cellW, kHeader + row * rowH, cellW, rowH };
        }

        void paint (juce::Graphics& g) override
        {
            const int steps = owner.pattern.numSteps();
            g.fillAll (brand::bg);

            // Заголовок: номер такта на его первом шаге, номера долей приглушённо
            for (int s = 0; s < steps; s += 4)
            {
                const auto r = juce::Rectangle<int> (s * cellW, 0, cellW * 4, kHeader).reduced (4, 0);
                if (s % Pattern::kStepsPerBar == 0)
                {
                    g.setColour (brand::text);
                    g.setFont (boldFont (12.0f));
                    g.drawText (utf8 ("Такт ") + juce::String (s / Pattern::kStepsPerBar + 1), r, juce::Justification::centredLeft);
                }
                else
                {
                    g.setColour (brand::textMuted);
                    g.setFont (plainFont (11.0f));
                    g.drawText (juce::String (s % Pattern::kStepsPerBar / 4 + 1), r, juce::Justification::centredLeft);
                }
            }

            for (int row = 0; row < (int) owner.rows.size(); ++row)
            {
                const auto& r = owner.rows[(size_t) row];
                const bool silent = r.palette.empty();
                const auto colour = brand::groupColour (r.slot);
                for (int s = 0; s < steps; ++s)
                {
                    const auto cell = cellBounds (row, s).toFloat().reduced (1.5f);
                    const bool evenBeat = (s / 4) % 2 == 0;
                    g.setColour (silent ? brand::surface : evenBeat ? brand::pad : brand::pad.darker (0.25f));
                    g.fillRoundedRectangle (cell, 3.0f);

                    if (const auto* st = owner.pattern.findStep (s, r.slot))
                    {
                        g.setColour (colour);
                        g.fillRoundedRectangle (cell, 3.0f);
                        g.setColour (textOn (colour));
                        const int opt = owner.optionFor (r, *st);
                        if (rowH >= 30 && cellW >= 40 && opt >= 0)
                        {
                            auto t = cell.toNearestInt().reduced (2, 2);
                            g.setFont (plainFont (10.0f));
                            g.drawText (r.palette[(size_t) opt].label, t.removeFromTop (t.getHeight() / 2), juce::Justification::centredBottom, true);
                            g.setFont (boldFont (12.0f));
                            g.drawText (juce::String (st->velocity), t, juce::Justification::centredTop, false);
                        }
                        else
                        {
                            g.setFont (boldFont (cellW >= 30 ? 12.0f : 10.0f));
                            g.drawText (juce::String (st->velocity), cell.toNearestInt(), juce::Justification::centred, false);
                        }
                    }

                    if (row == hoverRow && s == hoverStep && ! silent)
                    {
                        g.setColour (brand::accentHover);
                        g.drawRoundedRectangle (cell, 3.0f, 1.5f);
                    }
                }
            }

            // Границы тактов
            g.setColour (brand::line);
            for (int s = Pattern::kStepsPerBar; s < steps; s += Pattern::kStepsPerBar)
                g.fillRect (s * cellW - 1, 0, 2, getHeight());

            // Курсор проигрывания: подсветка столбца и метка в заголовке
            if (owner.shownPlayStep >= 0 && owner.shownPlayStep < steps)
            {
                const int x = owner.shownPlayStep * cellW;
                g.setColour (brand::accent.withAlpha (0.18f));
                g.fillRect (x, kHeader, cellW, getHeight() - kHeader);
                g.setColour (brand::accent);
                g.fillRect (x + 2, kHeader - 4, cellW - 4, 3);
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            int row = 0, step = 0;
            if (! cellAt (e.getPosition(), row, step))
                return;
            if (e.mods.isPopupMenu())
                owner.showStrengthMenu (row, step);
            else
                owner.toggleStep (row, step);
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            int row = -1, step = -1;
            if (! cellAt (e.getPosition(), row, step))
                row = step = -1;
            if (row != hoverRow || step != hoverStep)
            {
                if (hoverRow >= 0)
                    repaint (cellBounds (hoverRow, hoverStep));
                hoverRow = row;
                hoverStep = step;
                if (hoverRow >= 0)
                    repaint (cellBounds (hoverRow, hoverStep));
            }
        }

        void mouseExit (const juce::MouseEvent&) override
        {
            if (hoverRow >= 0)
                repaint (cellBounds (hoverRow, hoverStep));
            hoverRow = hoverStep = -1;
        }

        juce::String getTooltip() override
        {
            int row = 0, step = 0;
            if (! cellAt (getMouseXYRelative(), row, step))
                return {};
            const auto& r = owner.rows[(size_t) row];
            if (r.palette.empty())
                return utf8 ("У этого места нет звука: проверьте пак на вкладке «Инструменты»");
            if (const auto* st = owner.pattern.findStep (step, r.slot))
            {
                const int opt = owner.optionFor (r, *st);
                const auto what = opt >= 0 ? describe (r.palette[(size_t) opt]) : utf8 ("velocity ") + juce::String (st->velocity);
                return what + utf8 (". Клик снимает удар, правый клик меняет силу");
            }
            return utf8 ("Клик ставит удар: ") + describe (r.palette[(size_t) r.current]) + utf8 (". Правый клик: выбрать силу");
        }
    };

    // ---- Подписи строк: место, цвет и сила новых ударов ----

    struct PatternPanel::Labels final : juce::Component,
                                        juce::TooltipClient
    {
        explicit Labels (PatternPanel& o) : owner (o) {}

        PatternPanel& owner;

        int rowAt (juce::Point<int> p) const
        {
            const int rowH = owner.grid->rowH;
            if (p.y < kHeader || rowH <= 0)
                return -1;
            const int row = (p.y - kHeader) / rowH;
            return row < (int) owner.rows.size() ? row : -1;
        }

        void paint (juce::Graphics& g) override
        {
            g.fillAll (brand::bg);
            g.setColour (brand::textMuted);
            g.setFont (plainFont (11.0f));
            g.drawText (utf8 ("Место и сила удара"), juce::Rectangle<int> (0, 0, getWidth() - 8, kHeader), juce::Justification::centredLeft);

            const int rowH = owner.grid->rowH;
            for (int row = 0; row < (int) owner.rows.size(); ++row)
            {
                const auto& r = owner.rows[(size_t) row];
                auto area = juce::Rectangle<int> (0, kHeader + row * rowH, getWidth() - 8, rowH).reduced (0, 1);
                g.setColour (brand::surface);
                g.fillRoundedRectangle (area.toFloat(), 4.0f);
                area.reduce (8, 0);

                const auto dot = area.removeFromLeft (10);
                g.setColour (brand::groupColour (r.slot));
                g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre (dot.getCentre().toFloat()));
                area.removeFromLeft (6);

                // Треугольник справа: по клику открывается выбор силы
                if (! r.palette.empty())
                {
                    const auto tri = area.removeFromRight (10).toFloat().withSizeKeepingCentre (8.0f, 5.0f);
                    juce::Path p;
                    p.addTriangle (tri.getTopLeft(), tri.getTopRight(), { tri.getCentreX(), tri.getBottom() });
                    g.setColour (brand::textMuted);
                    g.fillPath (p);
                    area.removeFromRight (4);
                }

                const auto strength = r.palette.empty() ? utf8 ("нет звука")
                                                        : r.palette[(size_t) r.current].name + " " + juce::String (r.palette[(size_t) r.current].velocity);
                if (rowH >= 30)
                {
                    auto top = area.removeFromTop (area.getHeight() / 2);
                    g.setColour (brand::text);
                    g.setFont (boldFont (13.0f));
                    g.drawText (r.label, top, juce::Justification::bottomLeft, true);
                    g.setColour (brand::textMuted);
                    g.setFont (plainFont (12.0f));
                    g.drawText (strength, area, juce::Justification::topLeft, true);
                }
                else
                {
                    g.setColour (brand::text);
                    g.setFont (boldFont (13.0f));
                    const int nameW = juce::jmin (area.getWidth() / 2 + 10, juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), r.label) + 8);
                    g.drawText (r.label, area.removeFromLeft (nameW), juce::Justification::centredLeft, true);
                    g.setColour (brand::textMuted);
                    g.setFont (plainFont (11.0f));
                    g.drawText (strength, area, juce::Justification::centredRight, true);
                }
            }
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const int row = rowAt (e.getPosition());
            if (row >= 0)
                owner.showStrengthMenu (row, -1);
        }

        juce::String getTooltip() override
        {
            const int row = rowAt (getMouseXYRelative());
            if (row < 0)
                return {};
            const auto& r = owner.rows[(size_t) row];
            if (r.palette.empty())
                return utf8 ("У этого места нет звука: проверьте пак на вкладке «Инструменты»");
            return utf8 ("Сила новых ударов: ") + describe (r.palette[(size_t) r.current]) + utf8 (". Клик: выбрать другую");
        }
    };

    // ---- Панель ----

    PatternPanel::PatternPanel (SamplitProcessor& p) : processor (p)
    {
        grid = std::make_unique<Grid> (*this);
        labels = std::make_unique<Labels> (*this);

        barsLabel.setText (utf8 ("Такты"), juce::dontSendNotification);
        tempoLabel.setText (utf8 ("Темп"), juce::dontSendNotification);
        for (auto* l : { &barsLabel, &tempoLabel })
        {
            l->setFont (plainFont (13.0f));
            l->setColour (juce::Label::textColourId, brand::textMuted);
            l->setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (l);
        }

        for (int i = 0; i < Pattern::kMaxBars; ++i)
        {
            auto& b = barButtons[i];
            b.setButtonText (juce::String (i + 1));
            b.setTooltip (utf8 ("Длина партии: ") + juce::String (i + 1) + utf8 (i == 0 ? " такт" : " такта") + utf8 (" по 16 шагов"));
            b.setConnectedEdges ((i > 0 ? juce::Button::ConnectedOnLeft : 0) | (i < Pattern::kMaxBars - 1 ? juce::Button::ConnectedOnRight : 0));
            b.setWantsKeyboardFocus (false);
            b.onClick = [this, i] { setBars (i + 1); };
            addAndMakeVisible (b);
        }

        tempo.setSliderStyle (juce::Slider::IncDecButtons);
        tempo.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Horizontal);
        tempo.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 76, kControlsHeight);
        tempo.setRange (30.0, 300.0, 1.0);
        tempo.setTextValueSuffix (" BPM");
        tempo.setTooltip (utf8 ("Темп партии внутри плагина (30–300): кнопки −/+, перетаскивание по ним или ввод числа в поле"));
        tempo.setColour (juce::Slider::textBoxTextColourId, brand::text);
        tempo.setColour (juce::Slider::textBoxBackgroundColourId, brand::pad);
        tempo.setColour (juce::Slider::textBoxOutlineColourId, brand::line);
        tempo.onValueChange = [this]
        {
            const double bpm = tempo.getValue();
            if (bpm != pattern.bpm)
                edit ([bpm] (Pattern& p) { p.bpm = bpm; });
        };
        addAndMakeVisible (tempo);

        playButton.setTooltip (utf8 ("Сыграть партию с начала или остановить"));
        playButton.onClick = [this]
        {
            if (processor.isPatternPlaying())
                processor.stopPattern();
            else
                processor.playPattern();
            updateControls();
        };
        loopButton.setButtonText (utf8 ("Цикл"));
        loopButton.setTooltip (utf8 ("Включено: партия повторяется по кругу. Выключено: играет один раз"));
        loopButton.onClick = [this]
        {
            auto& player = processor.getPatternPlayer();
            player.setLoop (! player.isLooping());
            updateControls();
        };
        clearButton.setButtonText (utf8 ("Очистить"));
        clearButton.setTooltip (utf8 ("Убрать все удары партии"));
        clearButton.onClick = [this]
        {
            trimmed.clear();
            edit ([] (Pattern& p) { p.steps.clear(); });
        };
        for (auto* b : { &playButton, &loopButton, &clearButton })
        {
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (b);
        }

        hint.setText (utf8 ("Клик ставит или снимает удар, правый клик выбирает силу"), juce::dontSendNotification);
        hint.setFont (plainFont (12.0f));
        hint.setColour (juce::Label::textColourId, brand::textMuted);
        hint.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (hint);

        emptyState.setText (utf8 ("Нет включённых мест. Включите барабаны на вкладке «Инструменты», и они появятся здесь строками"),
                            juce::dontSendNotification);
        emptyState.setFont (plainFont (14.0f));
        emptyState.setColour (juce::Label::textColourId, brand::textMuted);
        emptyState.setJustificationType (juce::Justification::centred);
        addChildComponent (emptyState);

        labelsClip.setInterceptsMouseClicks (false, true);
        labelsClip.addAndMakeVisible (*labels);
        addAndMakeVisible (labelsClip);
        viewport.setViewedComponent (grid.get(), false);
        viewport.setScrollBarThickness (kScrollBar);
        viewport.onScroll = [this] { labels->setTopLeftPosition (0, -viewport.getViewPositionY()); };
        addAndMakeVisible (viewport);

        processor.addChangeListener (this);
        refresh();
    }

    PatternPanel::~PatternPanel()
    {
        stopTimer();
        processor.removeChangeListener (this);
    }

    void PatternPanel::visibilityChanged()
    {
        if (isVisible())
        {
            refresh();
            startTimerHz (30);
        }
        else
            stopTimer();
    }

    void PatternPanel::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        if (isVisible())
            refresh();
    }

    void PatternPanel::refresh()
    {
        const int oldSteps = pattern.numSteps();
        const auto oldRows = rows.size();
        pattern = processor.getPattern();
        rebuildRows();
        updateControls();
        if (oldSteps != pattern.numSteps() || oldRows != rows.size())
            layoutGrid();
        grid->repaint();
        labels->repaint();
    }

    void PatternPanel::rebuildRows()
    {
        const auto kit = processor.getKit();
        const auto setup = processor.getSetup();

        juce::String sig;
        for (const auto& def : kStudio600Places)
            if (const auto* ps = setup.find (def.id); ps != nullptr && ps->enabled)
                sig << def.id << ":" << ps->packId << "/" << ps->pieceId << "|";
        if (sig == rowsSignature && kit == lastKit)
            return;
        rowsSignature = sig;
        lastKit = kit;

        std::map<int, StrengthOption> previous;     // слот -> выбранная сила, чтобы пережить перезагрузку кита
        for (const auto& r : rows)
            if (r.current >= 0)
                previous[r.slot] = r.palette[(size_t) r.current];

        const auto catalog = kit != nullptr ? processor.getCatalog() : std::vector<CatalogPack> {};
        rows.clear();
        for (const auto& def : kStudio600Places)
        {
            const auto* ps = setup.find (def.id);
            if (ps == nullptr || ! ps->enabled)
                continue;

            Row r;
            r.slot = def.slot;
            r.label = utf8 (def.label);
            if (kit != nullptr)
            {
                // Роли и пары групп: из описания барабана в каталоге (как во вкладке «Инструменты»)
                pack::Piece meta;
                bool haveMeta = false;
                for (const auto& pk : catalog)
                    if (pk.packId == juce::String (ps->packId))
                        for (const auto& pc : pk.pieces)
                            if (! haveMeta && (ps->pieceId.empty() ? pc.type == def.type : pc.id == juce::String (ps->pieceId)))
                            {
                                meta.id = pc.id;
                                meta.type = pc.type;
                                meta.groups = pc.groups;
                                haveMeta = true;
                            }
                r.palette = buildPalette (*kit, def.slot, haveMeta ? &meta : nullptr);
            }

            r.current = defaultOption (r.palette);
            if (const auto it = previous.find (r.slot); it != previous.end())
                for (int i = 0; i < (int) r.palette.size(); ++i)
                {
                    const auto& o = r.palette[(size_t) i];
                    if (o.key == it->second.key && o.velocity == it->second.velocity && o.groupId == it->second.groupId)
                        r.current = i;
                }
            rows.push_back (std::move (r));
        }
    }

    int PatternPanel::optionFor (const Row& r, const PatternStep& st) const
    {
        int best = -1;
        for (int i = 0; i < (int) r.palette.size(); ++i)
        {
            const auto& o = r.palette[(size_t) i];
            if (o.key != st.key)
                continue;
            if (o.velocity == st.velocity)
                return i;
            // Не точное число (например, из старой партии): ближайший вариант той же клавиши
            if (best < 0 || std::abs (o.velocity - st.velocity) < std::abs (r.palette[(size_t) best].velocity - st.velocity))
                best = i;
        }
        return best;
    }

    void PatternPanel::updateControls()
    {
        for (int i = 0; i < Pattern::kMaxBars; ++i)
            barButtons[i].setToggleState (pattern.bars == i + 1, juce::dontSendNotification);
        tempo.setValue (pattern.bpm, juce::dontSendNotification);

        const bool playing = processor.isPatternPlaying();
        const auto playText = playing ? utf8 ("Стоп") : utf8 ("Играть");
        if (playButton.getButtonText() != playText)
            playButton.setButtonText (playText);
        playButton.setToggleState (playing, juce::dontSendNotification);
        loopButton.setToggleState (processor.getPatternPlayer().isLooping(), juce::dontSendNotification);
        clearButton.setEnabled (! pattern.steps.empty());

        emptyState.setVisible (rows.empty());
        viewport.setVisible (! rows.empty());
        labelsClip.setVisible (! rows.empty());
        hint.setVisible (! rows.empty());
    }

    void PatternPanel::edit (std::function<void (Pattern&)> change)
    {
        Pattern p = processor.getPattern();
        change (p);
        processor.setPattern (p);       // шлёт change message: refresh() обновит копию и сетку
        refresh();
    }

    void PatternPanel::toggleStep (int row, int step)
    {
        if (row < 0 || row >= (int) rows.size())
            return;
        const auto& r = rows[(size_t) row];
        if (r.palette.empty())
            return;
        const int slot = r.slot;
        if (pattern.findStep (step, slot) != nullptr)
        {
            edit ([step, slot] (Pattern& p) { p.removeStep (step, slot); });
            return;
        }
        const auto o = r.palette[(size_t) r.current];
        edit ([step, slot, o] (Pattern& p) { p.setStep (step, slot, o.key, o.velocity); });
        processor.triggerNote (o.key, o.velocity);      // слышно, что поставили
    }

    void PatternPanel::showStrengthMenu (int row, int step)
    {
        if (row < 0 || row >= (int) rows.size() || rows[(size_t) row].palette.empty())
            return;
        const auto& r = rows[(size_t) row];
        const auto* st = step >= 0 ? pattern.findStep (step, r.slot) : nullptr;
        const int ticked = st != nullptr ? optionFor (r, *st) : r.current;

        // Группы меню: клавиша + группа манифеста; в группе варианты по силе
        struct MenuGroup { juce::String title; std::vector<int> items; };
        std::vector<MenuGroup> groups;
        std::map<juce::String, size_t> index;
        bool manyKeys = false;
        for (int i = 0; i < (int) r.palette.size(); ++i)
        {
            const auto& o = r.palette[(size_t) i];
            manyKeys = manyKeys || o.key != r.palette.front().key;
            const auto id = juce::String (o.key) + "|" + (o.groupId.isNotEmpty() ? o.groupId : o.name);
            auto [it, fresh] = index.emplace (id, groups.size());
            if (fresh)
                groups.push_back ({ o.name, {} });
            groups[it->second].items.push_back (i);
        }
        for (auto& gr : groups)
        {
            const auto& first = r.palette[(size_t) gr.items.front()];
            if (first.exact && gr.items.size() > 1)
                gr.title = first.name.upToLastOccurrenceOf (" ", false, false);     // «Дроби 1» -> «Дроби»
            if (manyKeys)
                gr.title << " (" << noteName (first.key) << ")";
        }

        auto itemText = [&] (int i, bool withGroup)
        {
            const auto& o = r.palette[(size_t) i];
            juce::String t;
            if (withGroup)
                t << o.name << ": ";
            else if (o.exact)
                t << o.name.fromLastOccurrenceOf (" ", false, false) << ": ";
            return t + o.label + ", " + juce::String (o.velocity);
        };

        juce::PopupMenu menu;
        menu.addSectionHeader (r.label + (step >= 0 ? utf8 (", шаг ") + juce::String (step + 1) : utf8 (": сила новых ударов")));
        if (groups.size() == 1)
            for (int i : groups.front().items)
                menu.addItem (i + 1, itemText (i, groups.front().items.size() == 1), true, i == ticked);
        else
            for (const auto& gr : groups)
            {
                if (gr.items.size() == 1)
                {
                    const int i = gr.items.front();
                    menu.addItem (i + 1, gr.title + ": " + r.palette[(size_t) i].label + ", " + juce::String (r.palette[(size_t) i].velocity),
                                  true, i == ticked);
                    continue;
                }
                juce::PopupMenu sub;
                bool has = false;
                for (int i : gr.items)
                {
                    sub.addItem (i + 1, itemText (i, false), true, i == ticked);
                    has = has || i == ticked;
                }
                menu.addSubMenu (gr.title, sub, true, nullptr, has);
            }
        if (st != nullptr)
        {
            menu.addSeparator();
            menu.addItem (kRemoveId, utf8 ("Снять удар"));
        }

        const int slot = r.slot;
        const auto sig = rowsSignature;
        const auto kit = lastKit;
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (grid.get()).withMousePosition(),
                            [safe = juce::Component::SafePointer<PatternPanel> (this), slot, step, sig, kit] (int result)
        {
            if (safe == nullptr || result == 0 || safe->rowsSignature != sig || safe->lastKit != kit)
                return;     // строки поменялись, пока меню было открыто
            if (result == kRemoveId)
            {
                safe->edit ([step, slot] (Pattern& p) { p.removeStep (step, slot); });
                return;
            }
            for (auto& row : safe->rows)
                if (row.slot == slot && result - 1 < (int) row.palette.size())
                {
                    row.current = result - 1;
                    const auto o = row.palette[(size_t) row.current];
                    if (step >= 0)
                    {
                        safe->edit ([step, slot, o] (Pattern& p) { p.setStep (step, slot, o.key, o.velocity); });
                        safe->processor.triggerNote (o.key, o.velocity);
                    }
                    safe->labels->repaint();
                }
        });
    }

    void PatternPanel::setBars (int bars)
    {
        bars = juce::jlimit (1, Pattern::kMaxBars, bars);
        if (bars == pattern.bars)
            return;
        edit ([this, bars] (Pattern& p)
        {
            const int n = bars * Pattern::kStepsPerBar;
            if (bars < p.bars)
            {
                // Срезанные удары запоминаются: если такты вернут, удары вернутся
                for (const auto& s : p.steps)
                    if (s.step >= n)
                    {
                        trimmed.erase (std::remove_if (trimmed.begin(), trimmed.end(),
                                                       [&s] (const PatternStep& t) { return t.step == s.step && t.slot == s.slot; }),
                                       trimmed.end());
                        trimmed.push_back (s);
                    }
            }
            else
            {
                for (auto it = trimmed.begin(); it != trimmed.end();)
                    if (it->step < n)
                    {
                        if (p.findStep (it->step, it->slot) == nullptr)
                            p.steps.push_back (*it);
                        it = trimmed.erase (it);
                    }
                    else
                        ++it;
            }
            p.bars = bars;
        });
    }

    int PatternPanel::playStep() const
    {
        if (! processor.isPatternPlaying())
            return -1;
        const double rate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 44100.0;
        const double samplesPerStep = rate * 60.0 / juce::jmax (1.0, pattern.bpm) / 4.0;
        const auto step = (int) ((double) processor.getPlayPositionSamples() / samplesPerStep);
        return juce::jlimit (0, pattern.numSteps() - 1, step);
    }

    void PatternPanel::timerCallback()
    {
        const int s = playStep();
        if (s != shownPlayStep)
        {
            shownPlayStep = s;
            grid->repaint();
            // Курсор ушёл за край видимой части (4 такта в узком окне): прокрутить к нему
            if (s >= 0)
            {
                const int x = s * grid->cellW;
                const auto view = viewport.getViewArea();
                if (x < view.getX() || x + grid->cellW > view.getRight())
                    viewport.setViewPosition (juce::jmax (0, x - grid->cellW), view.getY());
            }
        }
        if (processor.isPatternPlaying() != playButton.getToggleState())
            updateControls();
    }

    void PatternPanel::resized()
    {
        auto area = getLocalBounds();
        auto bar = area.removeFromTop (kControlsHeight);
        area.removeFromTop (kGap);

        barsLabel.setBounds (bar.removeFromLeft (52));
        bar.removeFromLeft (6);
        for (auto& b : barButtons)
            b.setBounds (bar.removeFromLeft (32));
        bar.removeFromLeft (14);
        tempoLabel.setBounds (bar.removeFromLeft (44));
        bar.removeFromLeft (6);
        tempo.setBounds (bar.removeFromLeft (150));
        bar.removeFromLeft (14);
        playButton.setBounds (bar.removeFromLeft (92));
        bar.removeFromLeft (6);
        loopButton.setBounds (bar.removeFromLeft (72));
        clearButton.setBounds (bar.removeFromRight (100));
        bar.removeFromRight (12);
        bar.removeFromLeft (14);
        hint.setBounds (bar);
        hint.setVisible (! rows.empty() && bar.getWidth() >= 220);

        gridArea = area;
        emptyState.setBounds (area.withSizeKeepingCentre (juce::jmin (area.getWidth(), 520), 80));
        layoutGrid();
    }

    void PatternPanel::layoutGrid()
    {
        auto area = gridArea;
        labelsClip.setBounds (area.removeFromLeft (kLabelWidth));
        viewport.setBounds (area);

        const int steps = pattern.numSteps();
        const int viewW = area.getWidth();
        const int cellW = juce::jmax (kMinCell, viewW / juce::jmax (1, steps));
        const bool hScroll = cellW * steps > viewW;
        const int availH = area.getHeight() - kHeader - (hScroll ? kScrollBar : 0);
        const int n = juce::jmax (1, (int) rows.size());
        const int rowH = juce::jlimit (kMinRow, kMaxRow, availH / n);

        grid->cellW = cellW;
        grid->rowH = rowH;
        grid->setSize (cellW * steps, kHeader + rowH * (int) rows.size());
        labels->setBounds (0, -viewport.getViewPositionY(), kLabelWidth, grid->getHeight());
    }

    void PatternPanel::paint (juce::Graphics& g)
    {
        g.fillAll (brand::bg);
    }
}
