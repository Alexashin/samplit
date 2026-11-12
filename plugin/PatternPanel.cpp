#include "PatternPanel.h"
#include "Brand.h"
#include "ui/Fonts.h"

#include "samplit/core/Manifest.h"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <optional>
#include <tuple>

namespace samplit
{
    namespace
    {
        constexpr int kControlsHeight = 32;
        constexpr int kGap = 10;
        constexpr int kHeader = 22;
        constexpr int kLabelWidth = 200;
        constexpr int kMinCell = 36;
        constexpr int kMinRow = 22, kMaxRow = 46;
        constexpr int kScrollBar = 10;
        constexpr int kRemoveId = 100000;
        constexpr int kTogetherId = 200000;
        constexpr int kPartnerBaseId = 200100;
        constexpr int kChipWidth = 52;

        // Короткое имя типа места для подсказок пары
        juce::String typeName (const juce::String& type)
        {
            if (type == "tom") return juce::String::fromUTF8 ("том");
            if (type == "snare") return juce::String::fromUTF8 ("малый");
            if (type == "kick") return juce::String::fromUTF8 ("бочка");
            return type;
        }

        juce::Font boldFont (float size) { return fonts::strong (size); }
        juce::Font plainFont (float size) { return fonts::text (size); }

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

        juce::Colour textOn (juce::Colour fill) { return fill.getPerceivedBrightness() > 0.6f ? brand::onAccent : brand::text; }
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
                const bool silent = r.options.empty();
                for (int s = 0; s < steps; ++s)
                {
                    const auto cell = cellBounds (row, s).toFloat().reduced (1.5f);
                    const bool evenBeat = (s / 4) % 2 == 0;
                    g.setColour (silent ? brand::surface : evenBeat ? brand::pad : brand::pad.darker (0.25f));
                    g.fillRoundedRectangle (cell, 2.0f);

                    if (const auto* st = owner.pattern.findStep (s, r.slot))
                    {
                        // Сила удара читается по насыщенности одного акцента, а не по цвету барабана
                        const auto colour = juce::Colour (0xff2a3d8f).interpolatedWith (brand::accentHover, juce::jlimit (0.0f, 1.0f, (float) st->velocity / 127.0f));
                        g.setColour (colour);
                        g.fillRoundedRectangle (cell, 2.0f);
                        g.setColour (textOn (colour));
                        const int opt = owner.optionFor (r, *st);
                        if (rowH >= 30 && cellW >= 40 && opt >= 0)
                        {
                            auto t = cell.toNearestInt().reduced (2, 2);
                            g.setFont (plainFont (10.0f));
                            g.drawText (r.options[(size_t) opt].label, t.removeFromTop (t.getHeight() / 2), juce::Justification::centredBottom, true);
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
                        g.setColour (brand::accentText);
                        g.drawRoundedRectangle (cell, 2.0f, 1.5f);
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
            if (r.options.empty())
                return utf8 ("У этого места нет звука: выберите звук в панели барабана");
            if (const auto* st = owner.pattern.findStep (step, r.slot))
            {
                const int opt = owner.optionFor (r, *st);
                const auto what = opt >= 0 ? describe (r.options[(size_t) opt]) : utf8 ("velocity ") + juce::String (st->velocity);
                return what + utf8 (". Клик снимает удар, правый клик меняет силу");
            }
            return utf8 ("Клик ставит удар: ") + describe (r.options[(size_t) r.current]) + utf8 (". Правый клик: выбрать силу");
        }
    };

    // ---- Подписи строк: место, цвет и сила новых ударов ----

    struct PatternPanel::Labels final : juce::Component,
                                        juce::TooltipClient
    {
        explicit Labels (PatternPanel& o) : owner (o) {}

        PatternPanel& owner;

        // Переключатель «вместе» у строки, где выбранная сила имеет пару; пустой прямоугольник, если пары нет
        juce::Rectangle<int> chipBounds (int row) const
        {
            const auto& r = owner.rows[(size_t) row];
            if (r.current < 0 || r.options[(size_t) r.current].pairGroup.isEmpty())
                return {};
            const int rowH = owner.grid->rowH;
            const auto area = juce::Rectangle<int> (0, kHeader + row * rowH, getWidth() - 8, rowH).reduced (8, 1);
            return area.withTrimmedRight (14).removeFromRight (kChipWidth).withSizeKeepingCentre (kChipWidth, juce::jmin (20, rowH - 4));
        }

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
            g.drawText (utf8 ("Барабан и сила удара"), juce::Rectangle<int> (0, 0, getWidth() - 8, kHeader), juce::Justification::centredLeft);

            const int rowH = owner.grid->rowH;
            for (int row = 0; row < (int) owner.rows.size(); ++row)
            {
                const auto& r = owner.rows[(size_t) row];
                auto area = juce::Rectangle<int> (0, kHeader + row * rowH, getWidth() - 8, rowH).reduced (0, 1);
                g.setColour (brand::surface);
                g.fillRoundedRectangle (area.toFloat(), brand::radiusS);
                area.reduce (10, 0);

                // Треугольник справа: по клику открывается выбор силы
                if (! r.options.empty())
                {
                    const auto tri = area.removeFromRight (10).toFloat().withSizeKeepingCentre (8.0f, 5.0f);
                    juce::Path p;
                    p.addTriangle (tri.getTopLeft(), tri.getTopRight(), { tri.getCentreX(), tri.getBottom() });
                    g.setColour (brand::textMuted);
                    g.fillPath (p);
                    area.removeFromRight (4);
                }

                if (const auto chip = chipBounds (row); ! chip.isEmpty())
                {
                    const bool can = owner.partnerFor (r, r.options[(size_t) r.current]) != nullptr;
                    const bool on = r.together && can;
                    const auto c = chip.toFloat().reduced (0.5f);
                    g.setColour (on ? brand::accent : brand::pad);
                    g.fillRoundedRectangle (c, brand::radiusS);
                    g.setColour (on ? brand::accent : can ? brand::lineStrong : brand::line);
                    g.drawRoundedRectangle (c, brand::radiusS, 1.0f);
                    g.setColour (on ? brand::onAccent : can ? brand::text : brand::textMuted.withAlpha (0.6f));
                    g.setFont (plainFont (11.0f));
                    g.drawText (utf8 ("вместе"), chip, juce::Justification::centred, false);
                    area.removeFromRight (kChipWidth + 6);
                }

                const auto strength = r.options.empty() ? utf8 ("нет звука")
                                                        : r.options[(size_t) r.current].name + " " + juce::String (r.options[(size_t) r.current].velocity);
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
            if (row < 0)
                return;
            if (chipBounds (row).contains (e.getPosition()) && ! e.mods.isPopupMenu())
            {
                auto& r = owner.rows[(size_t) row];
                if (owner.partnerFor (r, r.options[(size_t) r.current]) != nullptr)
                {
                    r.together = ! r.together;
                    repaint();
                }
                return;
            }
            owner.showStrengthMenu (row, -1);
        }

        juce::String getTooltip() override
        {
            const int row = rowAt (getMouseXYRelative());
            if (row < 0)
                return {};
            const auto& r = owner.rows[(size_t) row];
            if (r.options.empty())
                return utf8 ("У этого места нет звука: выберите звук в панели барабана");
            if (chipBounds (row).contains (getMouseXYRelative()))
                return owner.pairHint (r);
            return utf8 ("Сила новых ударов: ") + describe (r.options[(size_t) r.current]) + utf8 (". Клик: выбрать другую");
        }
    };

    // ---- Панель ----

    PatternPanel::PatternPanel (SamplitProcessor& p) : processor (p)
    {
        grid = std::make_unique<Grid> (*this);
        labels = std::make_unique<Labels> (*this);

        barsLabel.setText (utf8 ("Такты"), juce::dontSendNotification);
        tempoLabel.setText (utf8 ("Темп"), juce::dontSendNotification);
        liveLabel.setText (utf8 ("Живость"), juce::dontSendNotification);
        for (auto* l : { &barsLabel, &tempoLabel, &liveLabel })
        {
            l->setFont (plainFont (13.0f));
            l->setColour (juce::Label::textColourId, brand::textMuted);
            l->setJustificationType (juce::Justification::centredRight);
            addAndMakeVisible (l);
        }

        barsSeg.setTooltip (utf8 ("Длина партии, в тактах по 16 шагов"));
        barsSeg.onChange = [this] (int i) { setBars (i + 1); };
        addAndMakeVisible (barsSeg);

        tempo.setSliderStyle (juce::Slider::IncDecButtons);
        tempo.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Horizontal);
        tempo.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 76, kControlsHeight);
        tempo.setRange (30.0, 300.0, 1.0);
        tempo.setTextValueSuffix (" BPM");
        tempo.setTooltip (utf8 ("Темп партии внутри плагина (30–300): кнопки −/+, перетаскивание по ним или ввод числа в поле"));
        tempo.setColour (juce::Slider::textBoxTextColourId, brand::text);
        tempo.setColour (juce::Slider::textBoxBackgroundColourId, brand::bg);
        tempo.setColour (juce::Slider::textBoxOutlineColourId, brand::lineStrong);
        tempo.onValueChange = [this]
        {
            const double bpm = tempo.getValue();
            if (bpm != pattern.bpm)
                edit ([bpm] (Pattern& p) { p.bpm = bpm; });
        };
        addAndMakeVisible (tempo);

        // Живость: случайность времени и силы, результат запоминается в проекте и попадает в MIDI
        timingKnob.setRange (0.0, 40.0, 1.0, 0.0);
        timingKnob.format = [] (double v) { return juce::String ((int) v) + utf8 (" мс"); };
        timingKnob.setTooltip (utf8 ("Случайный сдвиг каждого удара по времени, до ± этого числа мс. Результат запоминается и попадает в MIDI"));
        timingKnob.onChange = [this]
        {
            const int v = (int) timingKnob.getValue();
            edit ([v] (Pattern& p) { p.humanTimingMs = v; });
        };
        velKnob.setRange (0.0, 20.0, 1.0, 0.0);
        velKnob.format = [] (double v) { return juce::String ((int) v); };
        velKnob.setTooltip (utf8 ("Случайный сдвиг силы каждого удара, до ± этого числа. Результат запоминается и попадает в MIDI"));
        velKnob.onChange = [this]
        {
            const int v = (int) velKnob.getValue();
            edit ([v] (Pattern& p) { p.humanVelocity = v; });
        };
        addAndMakeVisible (timingKnob);
        addAndMakeVisible (velKnob);
        variantButton.setButtonText (utf8 ("Другой вариант"));
        variantButton.setTooltip (utf8 ("Бросить случайность заново. Выбранный вариант запоминается в проекте и совпадает с MIDI при перетаскивании"));
        variantButton.setWantsKeyboardFocus (false);
        variantButton.onClick = [this]
        {
            edit ([] (Pattern& p) { p.humanSeed = p.humanSeed * 1664525u + 1013904223u; });
        };
        addAndMakeVisible (variantButton);

        moreButton.setButtonText (utf8 ("⋯"));
        moreButton.setTooltip (utf8 ("Файлы партии: сохранить, открыть, сохранить как MIDI"));
        moreButton.onClick = [this] { showMoreMenu(); };
        moreButton.setWantsKeyboardFocus (false);
        addAndMakeVisible (moreButton);

        playButton.setTooltip (utf8 ("Сыграть партию с начала или остановить"));
        playButton.onClick = [this]
        {
            if (processor.isPatternPlaying())
                processor.stopPattern();
            else
                processor.playPattern();
            updateControls();
        };
        loopButton.setTooltip (utf8 ("Включено: партия повторяется по кругу. Выключено: играет один раз"));
        loopButton.onClick = [this]
        {
            processor.getPatternPlayer().setLoop (loopButton.getToggleState());
            updateControls();
        };
        clearButton.setButtonText (utf8 ("Очистить"));
        clearButton.setTooltip (utf8 ("Убрать все удары партии"));
        clearButton.onClick = [this]
        {
            trimmed.clear();
            edit ([] (Pattern& p) { p.steps.clear(); });
        };
        for (auto* b : { &playButton, &clearButton })
        {
            b->setWantsKeyboardFocus (false);
            addAndMakeVisible (b);
        }
        addAndMakeVisible (loopButton);

        emptyState.setText (utf8 ("Нет включённых барабанов. Включите их на странице «Барабаны», и они появятся здесь строками"),
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
        std::map<int, std::pair<bool, int>> pairs;  // слот -> «вместе» и выбранный партнёр
        for (const auto& r : rows)
        {
            if (r.current >= 0)
                previous[r.slot] = r.options[(size_t) r.current];
            pairs[r.slot] = { r.together, r.partnerSlot };
        }

        const auto catalog = kit != nullptr ? processor.getCatalog() : std::vector<CatalogPack> {};
        rows.clear();
        for (const auto& def : kStudio600Places)
        {
            const auto* ps = setup.find (def.id);
            if (ps == nullptr || ! ps->enabled)
                continue;

            Row r;
            r.slot = def.slot;
            r.placeId = def.id;
            r.type = def.type;
            r.label = utf8 (def.label);
            if (kit != nullptr)
            {
                // Роли и пары групп: из описания барабана в каталоге (как во вкладке «Инструменты»)
                pack::Piece meta;
                const bool haveMeta = findPieceMeta (catalog, def, *ps, meta);
                r.options = buildPalette (*kit, def.slot, haveMeta ? &meta : nullptr);
            }

            r.current = defaultOption (r.options);
            if (const auto it = previous.find (r.slot); it != previous.end())
                for (int i = 0; i < (int) r.options.size(); ++i)
                {
                    const auto& o = r.options[(size_t) i];
                    if (o.key == it->second.key && o.velocity == it->second.velocity && o.groupId == it->second.groupId)
                        r.current = i;
                }
            if (const auto it = pairs.find (r.slot); it != pairs.end())
                std::tie (r.together, r.partnerSlot) = it->second;
            rows.push_back (std::move (r));
        }
    }

    int PatternPanel::optionFor (const Row& r, const PatternStep& st) const
    {
        return findOption (r.options, st.key, st.velocity);
    }

    const PatternPanel::Row* PatternPanel::partnerFor (const Row& r, const StrengthOption& o) const
    {
        if (o.pairGroup.isEmpty())
            return nullptr;
        const Row* first = nullptr;
        for (const auto& other : rows)
            if (other.slot != r.slot && canPair (other, o))
            {
                if (other.slot == r.partnerSlot)
                    return &other;
                if (first == nullptr)
                    first = &other;
            }
        return first;       // выбранный партнёр выключен или не подходит: первое подходящее место (решение пользователя 12.11)
    }

    juce::String PatternPanel::pairHint (const Row& r) const
    {
        const auto& o = r.options[(size_t) r.current];
        const auto group = o.exact ? o.name.upToLastOccurrenceOf (" ", false, false) : o.name;
        if (const auto* partner = partnerFor (r, o))
            return utf8 ("Вместе: удар «") + group + utf8 ("» ставится и снимается сразу и здесь, и на месте «") + partner->label
                 + utf8 ("» с той же силой. Партнёра можно сменить в меню строки");
        return utf8 ("Пары нет: включите ") + typeName (o.pairType) + utf8 (" с группой «") + group
             + utf8 ("» на вкладке «Инструменты»");
    }

    void PatternPanel::updateControls()
    {
        barsSeg.setSelected (pattern.bars - 1);
        tempo.setValue (pattern.bpm, juce::dontSendNotification);
        timingKnob.setValue (pattern.humanTimingMs);
        velKnob.setValue (pattern.humanVelocity);
        variantButton.setEnabled (pattern.humanTimingMs > 0 || pattern.humanVelocity > 0);

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
        if (r.options.empty())
            return;
        if (pattern.findStep (step, r.slot) != nullptr)
            removeStep (r, step);
        else
            placeStep (r, step, r.options[(size_t) r.current]);
    }

    // Партнёр, которому достанется (или с которого снимется) пара для удара, стоящего на шаге строки; копия, потому что
    // edit() пересобирает строки
    std::optional<PlacePalette> PatternPanel::stepPartner (const Row& r, int step) const
    {
        const auto* st = pattern.findStep (step, r.slot);
        const int opt = st != nullptr ? optionFor (r, *st) : -1;
        const auto* partner = r.together && opt >= 0 ? partnerFor (r, r.options[(size_t) opt]) : nullptr;
        return partner != nullptr ? std::optional<PlacePalette> (*partner) : std::nullopt;
    }

    void PatternPanel::removeStep (const Row& r, int step)
    {
        const PlacePalette own = r;
        const auto partner = stepPartner (r, step);
        edit ([step, own, partner] (Pattern& p) { removePairedStep (p, step, own, partner ? &*partner : nullptr); });
    }

    void PatternPanel::placeStep (const Row& r, int step, StrengthOption o)
    {
        const PlacePalette own = r;
        const auto oldPartner = stepPartner (r, step);      // замена удара: прежняя пара снимается
        const auto* partner = r.together ? partnerFor (r, o) : nullptr;
        const auto partnerCopy = partner != nullptr ? std::optional<PlacePalette> (*partner) : std::nullopt;
        edit ([step, own, o, oldPartner, partnerCopy] (Pattern& p)
        {
            removePairedStep (p, step, own, oldPartner ? &*oldPartner : nullptr);
            setPairedStep (p, step, own, o, partnerCopy ? &*partnerCopy : nullptr);
        });
        processor.triggerNote (o.key, o.velocity);      // слышно, что поставили
        if (partnerCopy)
        {
            const auto& po = partnerCopy->options[(size_t) pairOption (partnerCopy->options, o)];
            processor.triggerNote (po.key, po.velocity);
        }
    }

    void PatternPanel::showStrengthMenu (int row, int step)
    {
        if (row < 0 || row >= (int) rows.size() || rows[(size_t) row].options.empty())
            return;
        const auto& r = rows[(size_t) row];
        const auto* st = step >= 0 ? pattern.findStep (step, r.slot) : nullptr;
        const int ticked = st != nullptr ? optionFor (r, *st) : r.current;

        // Группы меню: клавиша + группа манифеста; в группе варианты по силе
        struct MenuGroup { juce::String title; std::vector<int> items; };
        std::vector<MenuGroup> groups;
        std::map<juce::String, size_t> index;
        bool manyKeys = false;
        for (int i = 0; i < (int) r.options.size(); ++i)
        {
            const auto& o = r.options[(size_t) i];
            manyKeys = manyKeys || o.key != r.options.front().key;
            const auto id = juce::String (o.key) + "|" + (o.groupId.isNotEmpty() ? o.groupId : o.name);
            auto [it, fresh] = index.emplace (id, groups.size());
            if (fresh)
                groups.push_back ({ o.name, {} });
            groups[it->second].items.push_back (i);
        }
        for (auto& gr : groups)
        {
            const auto& first = r.options[(size_t) gr.items.front()];
            if (first.exact && gr.items.size() > 1)
                gr.title = first.name.upToLastOccurrenceOf (" ", false, false);     // «Дроби 1» -> «Дроби»
            if (manyKeys)
                gr.title << " (" << noteName (first.key) << ")";
        }

        auto itemText = [&] (int i, bool withGroup)
        {
            const auto& o = r.options[(size_t) i];
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
                    menu.addItem (i + 1, gr.title + ": " + r.options[(size_t) i].label + ", " + juce::String (r.options[(size_t) i].velocity),
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
        else if (step < 0 && r.current >= 0 && r.options[(size_t) r.current].pairGroup.isNotEmpty())
        {
            // Пара: переключатель «вместе» и выбор партнёра среди подходящих включённых мест
            const auto& o = r.options[(size_t) r.current];
            const auto* partner = partnerFor (r, o);
            menu.addSectionHeader (utf8 ("Парный удар"));
            menu.addItem (kTogetherId, utf8 ("Играть вместе"), partner != nullptr, r.together && partner != nullptr);
            for (const auto& other : rows)
                if (other.slot != r.slot && canPair (other, o))
                    menu.addItem (kPartnerBaseId + other.slot, utf8 ("Пара на месте «") + other.label + utf8 ("»"),
                                  true, partner == &other);
            if (partner == nullptr)
                menu.addItem (-1, pairHint (r), false);
        }

        const int slot = r.slot;
        const auto sig = rowsSignature;
        const auto kit = lastKit;
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (grid.get()).withMousePosition(),
                            [safe = juce::Component::SafePointer<PatternPanel> (this), slot, step, sig, kit] (int result)
        {
            if (safe == nullptr || result == 0 || safe->rowsSignature != sig || safe->lastKit != kit)
                return;     // строки поменялись, пока меню было открыто
            for (auto& row : safe->rows)
            {
                if (row.slot != slot)
                    continue;
                if (result == kRemoveId)
                    safe->removeStep (row, step);
                else if (result == kTogetherId)
                    row.together = ! row.together;
                else if (result >= kPartnerBaseId)
                {
                    row.partnerSlot = result - kPartnerBaseId;
                    row.together = true;
                }
                else if (result - 1 < (int) row.options.size())
                {
                    row.current = result - 1;
                    if (step >= 0)
                        safe->placeStep (row, step, row.options[(size_t) row.current]);
                }
                safe->labels->repaint();
                return;     // removeStep/placeStep пересобирают rows: дальше по ним не идти
            }
        });
    }

    void PatternPanel::saveJson()
    {
        chooser = std::make_unique<juce::FileChooser> (utf8 ("Сохранить партию"),
                                                       juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Samplit-партия.json"), "*.json");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer<PatternPanel> (this)] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (safe == nullptr || f == juce::File())
                                      return;
                                  f = f.withFileExtension ("json");
                                  if (! f.replaceWithText (safe->processor.getPattern().toJson(), false, false, "\n"))
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, utf8 ("Партия"),
                                                                              utf8 ("Не удалось записать файл: ") + f.getFullPathName());
                              });
    }

    void PatternPanel::openJson()
    {
        chooser = std::make_unique<juce::FileChooser> (utf8 ("Открыть партию"),
                                                       juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.json");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [safe = juce::Component::SafePointer<PatternPanel> (this)] (const juce::FileChooser& fc)
                              {
                                  const auto f = fc.getResult();
                                  if (safe == nullptr || ! f.existsAsFile())
                                      return;
                                  Pattern p;
                                  juce::String error;
                                  if (! Pattern::fromJson (f.loadFileAsString(), p, error))
                                  {
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, utf8 ("Партия"),
                                                                              utf8 ("Файл не похож на партию Samplit: ") + error);
                                      return;
                                  }
                                  safe->trimmed.clear();
                                  safe->processor.setPattern (p);
                                  safe->refresh();
                              });
    }

    void PatternPanel::saveMidi()
    {
        const auto pat = processor.getPattern();
        const auto name = "Samplit-" + juce::String (pat.bars) + "t-" + juce::String (juce::roundToInt (pat.bpm)) + "bpm.mid";
        chooser = std::make_unique<juce::FileChooser> (utf8 ("Сохранить партию как MIDI"),
                                                       juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (name), "*.mid");
        chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles | juce::FileBrowserComponent::warnAboutOverwriting,
                              [safe = juce::Component::SafePointer<PatternPanel> (this)] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (safe == nullptr || f == juce::File())
                                      return;
                                  f = f.withFileExtension ("mid");
                                  f.deleteFile();
                                  juce::FileOutputStream out (f);
                                  if (! out.openedOk() || ! safe->processor.getPattern().toMidiFile().writeTo (out))
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, utf8 ("Партия"),
                                                                              utf8 ("Не удалось записать файл: ") + f.getFullPathName());
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

    // Меню «⋯»: файлы партии
    void PatternPanel::showMoreMenu()
    {
        juce::PopupMenu m;
        m.addItem (1, utf8 ("Сохранить партию…"), ! pattern.steps.empty());
        m.addItem (2, utf8 ("Открыть партию…"));
        m.addSeparator();
        m.addItem (3, utf8 ("Сохранить как MIDI…"), ! pattern.steps.empty());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&moreButton),
                         [safe = juce::Component::SafePointer<PatternPanel> (this)] (int r)
        {
            if (safe == nullptr)
                return;
            if (r == 1)      safe->saveJson();
            else if (r == 2) safe->openJson();
            else if (r == 3) safe->saveMidi();
        });
    }

    // Полоса управления: слева такты, темп, запуск; справа живость (две крутилки и «Другой вариант»), «Очистить» и «⋯».
    // В узком окне живость уходит второй строкой
    void PatternPanel::resized()
    {
        auto area = getLocalBounds();
        const int liveW = 60 + 8 + 60 + 16 + 124;
        const bool wide = area.getWidth() >= 1060;
        const int bandH = 66;

        auto band = area.removeFromTop (wide ? bandH : kControlsHeight);
        if (wide)
        {
            liveRow = band.removeFromRight (liveW + 70);
            transportRow = band.withSizeKeepingCentre (band.getWidth(), kControlsHeight);
        }
        else
        {
            transportRow = band;
            area.removeFromTop (6);
            liveRow = area.removeFromTop (bandH);
        }
        area.removeFromTop (kGap);

        auto bar = transportRow;
        moreButton.setBounds (bar.removeFromRight (36));
        bar.removeFromRight (6);
        clearButton.setBounds (bar.removeFromRight (86));
        bar.removeFromRight (12);
        barsLabel.setBounds (bar.removeFromLeft (52));
        bar.removeFromLeft (6);
        barsSeg.setBounds (bar.removeFromLeft (barsSeg.preferredWidth() > 0 ? juce::jmax (136, barsSeg.preferredWidth()) : 136));
        bar.removeFromLeft (14);
        tempoLabel.setBounds (bar.removeFromLeft (44));
        bar.removeFromLeft (6);
        tempo.setBounds (bar.removeFromLeft (150));
        bar.removeFromLeft (14);
        playButton.setBounds (bar.removeFromLeft (92));
        bar.removeFromLeft (6);
        loopButton.setBounds (bar.removeFromLeft (82));

        auto live = liveRow;
        if (! wide)
            live = live.withTrimmedLeft (0);
        liveLabel.setBounds (live.removeFromLeft (wide ? 62 : 56).withSizeKeepingCentre (wide ? 62 : 56, kControlsHeight));
        live.removeFromLeft (8);
        timingKnob.setBounds (live.removeFromLeft (Knob::widthFor (Knob::Size::Small)).withHeight (Knob::heightFor (Knob::Size::Small)));
        live.removeFromLeft (8);
        velKnob.setBounds (live.removeFromLeft (Knob::widthFor (Knob::Size::Small)).withHeight (Knob::heightFor (Knob::Size::Small)));
        live.removeFromLeft (16);
        variantButton.setBounds (live.removeFromLeft (124).withSizeKeepingCentre (124, kControlsHeight).withY (live.getY() + 15));

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
