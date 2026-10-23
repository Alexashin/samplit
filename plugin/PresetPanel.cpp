#include "PresetPanel.h"
#include "Brand.h"

namespace samplit
{
    namespace
    {
        constexpr int kHeaderHeight = 38;
        constexpr int kRowHeight = 54;
        constexpr int kStatusHeight = 62;
        const juce::Colour kWarning { 0xffffb454 };

        juce::String joinPacks (const PresetEntry& e, PackState state)
        {
            juce::StringArray ids;
            for (const auto& [id, s] : e.packs)
                if (s == state)
                    ids.add (id);
            return ids.joinIntoString (", ");
        }

        // Строка состояния пресета. Смысл несёт текст, цвет только дублирует его
        juce::String packSummary (const PresetEntry& e)
        {
            if (e.isComplete())
                return utf8 ("Все пакеты на месте");

            juce::StringArray parts;
            if (const auto s = joinPacks (e, PackState::NoLicense); s.isNotEmpty())
                parts.add (utf8 ("нет лицензии: ") + s);
            if (const auto s = joinPacks (e, PackState::NotInstalled); s.isNotEmpty())
                parts.add (utf8 ("не установлены: ") + s);
            const auto text = parts.joinIntoString ("  ·  ");
            return (e.isPlayable() ? utf8 ("Часть барабанов молчит, ") : utf8 ("Не зазвучит, ")) + text;
        }

        juce::String sizeText (int64_t bytes)
        {
            const double gb = (double) bytes / (1024.0 * 1024.0 * 1024.0);
            if (gb >= 0.1)
                return juce::String (gb, 1).replaceCharacter ('.', ',') + utf8 (" ГБ");
            const int mb = juce::roundToInt ((double) bytes / (1024.0 * 1024.0));
            return mb < 1 ? utf8 ("меньше 1 МБ") : juce::String (mb) + utf8 (" МБ");
        }
    }

    PresetPanel::PresetPanel (SamplitProcessor& p) : processor (p)
    {
        title.setText (utf8 ("Пресеты"), juce::dontSendNotification);
        title.setFont (juce::FontOptions (15.0f, juce::Font::bold));

        rescanButton.setButtonText (utf8 ("Обновить список"));
        rescanButton.setTooltip (utf8 ("Заново прочитать папки пресетов и пакетов"));
        rescanButton.onClick = [this] { rescan(); };

        folderButton.setButtonText (utf8 ("Открыть папку"));
        folderButton.setTooltip (utf8 ("Папка с пресетами. Пакеты лежат рядом, в папке packs"));
        folderButton.onClick = [this]
        {
            const auto dir = processor.getLicense().getPresetDir();
            dir.createDirectory();
            dir.startAsProcess();
        };

        list.setModel (this);
        list.setRowHeight (kRowHeight);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setTitle (utf8 ("Список пресетов"));

        emptyText.setColour (juce::Label::textColourId, brand::textMuted);
        emptyText.setFont (juce::FontOptions (13.0f));
        emptyText.setJustificationType (juce::Justification::topLeft);

        for (auto* c : std::initializer_list<juce::Component*> { &title, &rescanButton, &folderButton, &list, &emptyText })
            addAndMakeVisible (c);

        processor.addChangeListener (this);
        rescan();
        startTimerHz (10);
    }

    PresetPanel::~PresetPanel()
    {
        processor.removeChangeListener (this);
    }

    void PresetPanel::rescan()
    {
        presets = processor.scanPresets();
        list.updateContent();

        const auto current = processor.getPresetName();
        for (int i = 0; i < (int) presets.size(); ++i)
            if (presets[(size_t) i].preset.id == current)
                list.selectRow (i, true, true);

        emptyText.setText (utf8 ("Пресетов пока нет. Положите файлы пресетов (.json) в папку\n")
                               + processor.getLicense().getPresetDir().getFullPathName()
                               + utf8 ("\nи нажмите «Обновить список»."),
                           juce::dontSendNotification);
        emptyText.setVisible (presets.empty());
        updateStatus();
        repaint();
    }

    void PresetPanel::load (int row)
    {
        if (! juce::isPositiveAndBelow (row, (int) presets.size()))
            return;

        const auto& e = presets[(size_t) row];
        if (! e.isPlayable())
        {
            statusText = utf8 ("«") + e.preset.name + utf8 ("» не зазвучит: ") + packSummary (e).fromFirstOccurrenceOf (", ", false, false)
                         + utf8 (". Установите пакеты или обновите лицензию.");
            statusWarns = true;
            repaint (statusArea);
            return;
        }

        loadingName = e.preset.name;
        processor.selectPreset (e.preset, [safe = juce::Component::SafePointer<PresetPanel> (this)] (const KitLoadResult&)
        {
            if (safe != nullptr)
                safe->rescan();
        });
        updateStatus();
    }

    void PresetPanel::updateStatus()
    {
        const auto& last = processor.getLastKitLoad();
        statusWarns = false;

        if (processor.isKitLoading())
        {
            statusText = utf8 ("Загрузка «") + loadingName + utf8 ("»: ")
                         + juce::String (juce::roundToInt (juce::jmax (0.0f, shownProgress) * 100.0f)) + " %";
        }
        else if (last.cancelled)
        {
            statusText = utf8 ("Загрузка отменена");
        }
        else if (last.error.isNotEmpty())
        {
            statusText = utf8 ("Не удалось загрузить: ") + last.error
                         + utf8 (". Проверьте лицензию и пакеты, затем выберите пресет снова.");
            statusWarns = true;
        }
        else if (const auto kit = processor.getKit(); kit != nullptr)
        {
            statusText = utf8 ("Загружено: ") + juce::String::fromUTF8 (kit->title.c_str())
                         + utf8 (", в памяти ") + sizeText (last.memoryBytes);
            if (! last.missingPacks.empty())
            {
                juce::StringArray ids;
                for (const auto& id : last.missingPacks)
                    ids.add (id);
                statusText << utf8 (". Молчат барабаны пакетов: ") << ids.joinIntoString (", ");
                statusWarns = true;
            }
        }
        else
        {
            statusText = presets.empty() ? juce::String() : utf8 ("Выберите пресет: клик или Enter");
        }
        repaint (statusArea);
    }

    void PresetPanel::timerCallback()
    {
        const float progress = processor.isKitLoading() ? processor.getKitLoadProgress() : -1.0f;
        if (progress != shownProgress)
        {
            shownProgress = progress;
            updateStatus();
        }
    }

    juce::String PresetPanel::getTooltip()
    {
        return statusArea.contains (getMouseXYRelative()) ? statusText : juce::String();
    }

    juce::String PresetPanel::getTooltipForRow (int row)
    {
        if (! juce::isPositiveAndBelow (row, (int) presets.size()))
            return {};
        const auto& e = presets[(size_t) row];
        return e.preset.name + "\n" + packSummary (e);
    }

    void PresetPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
    {
        if (! juce::isPositiveAndBelow (row, (int) presets.size()))
            return;

        const auto& e = presets[(size_t) row];
        const bool loaded = processor.getKit() != nullptr && e.preset.id == processor.getPresetName();
        const auto r = juce::Rectangle<int> (width, height).toFloat().reduced (0.0f, 2.0f);

        g.setColour (selected ? brand::accentSoft : brand::pad);
        g.fillRoundedRectangle (r, 8.0f);
        if (selected && list.hasKeyboardFocus (true))
        {
            g.setColour (brand::accent);
            g.drawRoundedRectangle (r.reduced (0.75f), 8.0f, 1.5f);
        }

        auto text = r.reduced (12.0f, 7.0f).toNearestInt();
        auto top = text.removeFromTop (20);

        if (loaded)
        {
            g.setColour (brand::accent);
            g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
            g.drawText (utf8 ("Играет"), top.removeFromRight (60), juce::Justification::centredRight);
        }

        g.setColour (e.isPlayable() ? brand::text : brand::textMuted);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (e.preset.name.isNotEmpty() ? e.preset.name : e.preset.id, top, juce::Justification::centredLeft, true);

        g.setColour (e.isComplete() ? brand::textMuted : kWarning);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (packSummary (e), text, juce::Justification::centredLeft, true);
    }

    void PresetPanel::paint (juce::Graphics& g)
    {
        g.setColour (brand::surface);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);

        auto area = statusArea.reduced (14, 8);
        if (shownProgress >= 0.0f)
        {
            auto bar = area.removeFromBottom (6).toFloat();
            g.setColour (brand::line);
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (brand::accent);
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, shownProgress)), 3.0f);
            area.removeFromBottom (4);
        }

        g.setColour (statusWarns ? kWarning : brand::textMuted);
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (statusText, area, juce::Justification::topLeft, 3, 1.0f);
    }

    void PresetPanel::resized()
    {
        auto area = getLocalBounds().reduced (14, 10);
        title.setBounds (area.removeFromTop (kHeaderHeight - 10));
        area.removeFromTop (8);

        statusArea = getLocalBounds().removeFromBottom (kStatusHeight);
        area.removeFromBottom (kStatusHeight - 10);

        // Кнопки под списком: в узком окне заголовок не теряется
        auto buttons = area.removeFromBottom (30);
        rescanButton.setBounds (buttons.removeFromLeft (juce::jmin (140, buttons.getWidth() / 2 - 3)));
        buttons.removeFromLeft (6);
        folderButton.setBounds (buttons.removeFromLeft (juce::jmin (130, buttons.getWidth())));
        area.removeFromBottom (8);

        list.setBounds (area);
        emptyText.setBounds (area);
    }
}
