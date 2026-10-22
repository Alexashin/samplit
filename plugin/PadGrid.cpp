#include "PadGrid.h"
#include "Brand.h"

namespace samplit
{
    namespace
    {
        constexpr int kCols = 4;
        constexpr int kRows = kNumPads / kCols;
        constexpr float kGap = 12.0f;
        constexpr float kHintHeight = 34.0f;

        bool isAudioFile (const juce::File& f)
        {
            return f.existsAsFile() && f.hasFileExtension ("wav;aif;aiff;flac;ogg");
        }

        juce::String noteName (int pad)
        {
            // Нумерация с C3 = 60, как в большинстве DAW
            return juce::MidiMessage::getMidiNoteName (kFirstPadNote + pad, true, true, 3);
        }
    }

    PadGrid::PadGrid (SamplitProcessor& p) : processor (p)
    {
        processor.addChangeListener (this);
        startTimerHz (30);
    }

    PadGrid::~PadGrid()
    {
        processor.removeChangeListener (this);
    }

    juce::Rectangle<float> PadGrid::padBounds (int pad) const
    {
        // Пэды квадратные, как на железном сэмплере: сторона по меньшему из измерений, место под подсказку снизу
        const auto area = getLocalBounds().toFloat().withTrimmedBottom (kHintHeight);
        const float size = juce::jmin ((area.getWidth() - kGap * (kCols - 1)) / (float) kCols,
                                       (area.getHeight() - kGap * (kRows - 1)) / (float) kRows);
        const int col = pad % kCols;
        const int row = pad / kCols;
        return { (float) col * (size + kGap), (float) row * (size + kGap), size, size };
    }

    int PadGrid::padAt (juce::Point<int> p) const
    {
        for (int i = 0; i < kNumPads; ++i)
            if (padBounds (i).contains (p.toFloat()))
                return i;
        return -1;
    }

    int PadGrid::firstFreePad() const
    {
        for (int i = 0; i < kNumPads; ++i)
            if (processor.getPadFile (i) == juce::File())
                return i;
        return -1;
    }

    void PadGrid::setHover (int pad)
    {
        if (pad != hoverPad)
        {
            hoverPad = pad;
            repaint();
        }
    }

    void PadGrid::timerCallback()
    {
        const auto active = processor.getActiveMask();
        const auto separate = processor.getSeparateOutMask();
        if (active != shownActive || separate != shownSeparate)
        {
            shownActive = active;
            shownSeparate = separate;
            repaint();
        }
    }

    void PadGrid::mouseDown (const juce::MouseEvent& e)
    {
        const int pad = padAt (e.getPosition());
        if (pad < 0)
            return;

        if (! e.mods.isPopupMenu())
        {
            processor.triggerPad (pad);
            return;
        }

        juce::PopupMenu menu;
        menu.addSectionHeader (utf8 ("Пэд ") + juce::String (pad + 1) + " · " + noteName (pad));
        menu.addItem (1, utf8 ("Очистить пэд"), processor.getPadFile (pad) != juce::File());
        menu.addItem (2, utf8 ("Показать файл в проводнике"), processor.getPadFile (pad).existsAsFile());
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                            [this, pad] (int result)
                            {
                                if (result == 1)
                                    processor.clearPad (pad);
                                else if (result == 2)
                                    processor.getPadFile (pad).revealToUser();
                            });
    }

    bool PadGrid::isInterestedInDragSource (const SourceDetails& d)
    {
        return d.description.isString() && isAudioFile (juce::File (d.description.toString()));
    }

    void PadGrid::itemDropped (const SourceDetails& d)
    {
        const int pad = padAt (d.localPosition);
        setHover (-1);
        if (pad >= 0)
            processor.loadPadSample (pad, juce::File (d.description.toString()));
    }

    bool PadGrid::isInterestedInFileDrag (const juce::StringArray& files)
    {
        for (const auto& f : files)
            if (isAudioFile (juce::File (f)))
                return true;
        return false;
    }

    void PadGrid::filesDropped (const juce::StringArray& files, int x, int y)
    {
        setHover (-1);
        int pad = padAt ({ x, y });
        if (pad < 0)
            return;

        for (const auto& f : files)
        {
            const juce::File file (f);
            if (! isAudioFile (file))
                continue;
            if (pad >= kNumPads)
                break;
            processor.loadPadSample (pad++, file);
        }
    }

    void PadGrid::paint (juce::Graphics& g)
    {
        for (int pad = 0; pad < kNumPads; ++pad)
            paintPad (g, pad);

        const auto last = padBounds (kNumPads - 1);
        g.setColour (brand::textMuted);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (utf8 ("Клик играет пэд  ·  ноты C1–G1 играют пэды 1–8  ·  правый клик: меню пэда"),
                    juce::Rectangle<float> (0.0f, last.getBottom() + 12.0f, last.getRight(), 18.0f).toNearestInt(),
                    juce::Justification::centredLeft);
    }

    void PadGrid::paintPad (juce::Graphics& g, int pad)
    {
        const auto r = padBounds (pad);
        const bool lit = (shownActive & (1u << pad)) != 0;
        const bool own = (shownSeparate & (1u << pad)) != 0;
        const auto file = processor.getPadFile (pad);
        const auto sample = processor.getPadSample (pad);
        const bool empty = file == juce::File();
        const float radius = juce::jmin (r.getWidth(), r.getHeight()) * brand::padRadiusRatio;

        g.setColour (lit ? brand::accent : brand::pad);
        g.fillRoundedRectangle (r, radius);

        if (hoverPad == pad)
        {
            g.setColour (brand::accent);
            g.drawRoundedRectangle (r.reduced (1.0f), radius, 2.0f);
        }

        const auto ink = lit ? brand::onAccent : brand::text;
        const auto muted = lit ? brand::onAccent.withAlpha (0.75f) : brand::textMuted;
        auto inner = r.reduced (16.0f, 14.0f).toNearestInt();

        // Верх: номер пэда и куда он идёт
        auto top = inner.removeFromTop (22);
        g.setColour (ink);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText (juce::String (pad + 1), top, juce::Justification::centredLeft);
        g.setColour (muted);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ((own ? "OUT " + juce::String (pad + 1) : juce::String ("MAIN")) + "  " + noteName (pad),
                    top, juce::Justification::centredRight);

        // Низ: имя сэмпла и длина
        auto bottom = inner.removeFromBottom (36);
        if (empty)
        {
            g.setColour (muted);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (utf8 ("Пусто"), bottom.removeFromTop (18), juce::Justification::bottomLeft);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (utf8 ("перетащите сэмпл"), bottom, juce::Justification::topLeft);
            return;
        }

        g.setColour (ink);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (file.getFileNameWithoutExtension(), bottom.removeFromTop (18), juce::Justification::bottomLeft);

        g.setColour (muted);
        g.setFont (juce::FontOptions (11.0f));
        if (sample == nullptr)
            g.drawText (file.existsAsFile() ? utf8 ("загрузка…") : utf8 ("файл не найден"), bottom, juce::Justification::topLeft);
        else
            g.drawText (juce::String (sample->getLengthSeconds(), 2) + " s", bottom, juce::Justification::topLeft);
    }
}
