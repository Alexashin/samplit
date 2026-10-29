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

            if (! joinPacks (e, PackState::NoLibrary).isEmpty())
                return utf8 ("Выберите папку библиотеки («Моя библиотека…»)");

            juce::StringArray parts;
            if (const auto s = joinPacks (e, PackState::NoLicense); s.isNotEmpty())
                parts.add (utf8 ("нет лицензии: ") + s);
            if (const auto s = joinPacks (e, PackState::NotInstalled); s.isNotEmpty())
                parts.add (utf8 ("не установлены: ") + s);
            const auto text = parts.joinIntoString (utf8 ("  ·  "));
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

        intro.setFont (juce::FontOptions (12.0f));
        intro.setColour (juce::Label::textColourId, brand::textMuted);
        intro.setJustificationType (juce::Justification::topLeft);
        intro.setMinimumHorizontalScale (1.0f);

        rescanButton.setButtonText (utf8 ("Обновить"));
        rescanButton.setTooltip (utf8 ("Заново прочитать папки пресетов и пакетов"));
        rescanButton.onClick = [this] { rescan(); };

        folderButton.setButtonText (utf8 ("Папка пресетов"));
        folderButton.setTooltip (utf8 ("Откроется папка с пресетами (.json). Положите сюда чужой пресет и нажмите «Обновить»"));
        folderButton.onClick = [this]
        {
            const auto dir = processor.getLicense().getPresetDir();
            dir.createDirectory();
            dir.startAsProcess();
        };

        packFolderButton.setButtonText (utf8 ("Папка пакетов"));
        packFolderButton.setTooltip (utf8 ("Откроется папка с пакетами звука (.splpack). Новый пакет можно просто положить сюда"));
        packFolderButton.onClick = [this]
        {
            const auto dir = processor.getLicense().getPackDir();
            dir.createDirectory();
            dir.startAsProcess();
        };

        addPackButton.setButtonText (utf8 ("Добавить пакет…"));
        addPackButton.setTooltip (utf8 ("Выбрать файл пакета (.splpack) и скопировать его в папку пакетов"));
        addPackButton.onClick = [this] { addPack(); };

        libraryButton.setButtonText (utf8 ("Моя библиотека…"));
        libraryButton.setTooltip (utf8 ("Проверка на своих сэмплах: выберите папку с WAV (00-Studio600ru-Drum-LIB). Звук берётся из неё целиком, без обрезки хвостов. Отмена выбора сбрасывает папку"));
        libraryButton.onClick = [this] { chooseLibrary(); };

        saveButton.setButtonText (utf8 ("Сохранить как…"));
        saveButton.setTooltip (utf8 ("Сохранить текущий пресет с вашими уровнями микрофонов, панорамой, выходами и режимами выбора удара"));
        saveButton.onClick = [this] { saveAs(); };

        list.setModel (this);
        list.setRowHeight (kRowHeight);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setTitle (utf8 ("Список пресетов"));

        emptyText.setColour (juce::Label::textColourId, brand::textMuted);
        emptyText.setFont (juce::FontOptions (13.0f));
        emptyText.setJustificationType (juce::Justification::topLeft);

        for (auto* c : std::initializer_list<juce::Component*> { &title, &intro, &rescanButton, &folderButton, &packFolderButton, &addPackButton, &saveButton, &libraryButton, &list, &emptyText })
            addAndMakeVisible (c);
        packFolderButton.setVisible (processor.getLicense().isEnforced());     // пакеты нужны только при лицензии
        addPackButton.setVisible (processor.getLicense().isEnforced());

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
        updateIntro();
        updateStatus();
        repaint();
    }

    void PresetPanel::step (int delta)
    {
        const int n = (int) presets.size();
        if (n == 0)
            return;
        int current = -1;
        for (int i = 0; i < n; ++i)
            if (presets[(size_t) i].preset.id == processor.getPresetName())
                current = i;
        load (current < 0 ? 0 : (current + delta + n) % n);
    }

    void PresetPanel::updateIntro()
    {
        if (! processor.getLicense().isEnforced())
        {
            intro.setText (utf8 ("Пресет задаёт, какие барабаны играют и как сведены микрофоны. Звук берётся из папки библиотеки («Моя библиотека…»). "
                                 "Пресетов: ") + juce::String ((int) presets.size())
                               + utf8 (". Свой вариант сохраняется кнопкой «Сохранить как…», правый клик по нему: переименовать или удалить."),
                           juce::dontSendNotification);
            return;
        }
        const auto packCount = processor.getLicense().getPackDir().findChildFiles (juce::File::findFiles, false, "*.splpack").size();
        intro.setText (utf8 ("Пресет задаёт, какие барабаны играют и как сведены микрофоны. Звук он берёт из пакетов (файлы .splpack). "
                             "Пакетов установлено: ") + juce::String (packCount) + utf8 (", пресетов: ") + juce::String ((int) presets.size())
                           + utf8 (". Свой вариант сохраняется кнопкой «Сохранить как…», правый клик по нему: переименовать или удалить."),
                       juce::dontSendNotification);
    }

    // Правый клик по своему пресету (user-*.json): переименовать или удалить
    void PresetPanel::showRowMenu (int row)
    {
        if (! juce::isPositiveAndBelow (row, (int) presets.size()) || ! presets[(size_t) row].file.getFileName().startsWith ("user-"))
            return;

        const auto file = presets[(size_t) row].file;
        const auto name = presets[(size_t) row].preset.name;
        juce::PopupMenu menu;
        menu.addItem (1, utf8 ("Переименовать…"));
        menu.addItem (2, utf8 ("Удалить (в корзину)"));
        menu.showMenuAsync ({}, [this, safe = juce::Component::SafePointer<PresetPanel> (this), file, name] (int result)
        {
            if (safe == nullptr || result == 0)
                return;
            if (result == 2)
            {
                juce::String error;
                const bool ok = processor.deleteUserFile (file, error);
                safe->statusWarns = ! ok;
                safe->statusText = ok ? utf8 ("Удалено: ") + name : error;
                safe->rescan();
                return;
            }

            auto* w = new juce::AlertWindow (utf8 ("Переименовать пресет"), {}, juce::MessageBoxIconType::NoIcon, safe.getComponent());
            w->addTextEditor ("name", name, utf8 ("Название"));
            w->addButton (utf8 ("Сохранить"), 1, juce::KeyPress (juce::KeyPress::returnKey));
            w->addButton (utf8 ("Отмена"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
            w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, file, w] (int r)
            {
                if (safe == nullptr || r != 1)
                    return;
                juce::String error;
                const bool ok = safe->processor.renameUserFile (file, w->getTextEditorContents ("name"), error);
                safe->statusWarns = ! ok;
                safe->statusText = ok ? utf8 ("Переименовано") : error;
                safe->rescan();
            }), true);
        });
    }

    void PresetPanel::saveAs()
    {
        if (processor.getKit() == nullptr)
        {
            statusText = utf8 ("Сначала загрузите пресет, потом сохраняйте его вариант");
            statusWarns = true;
            repaint (statusArea);
            return;
        }

        auto* w = new juce::AlertWindow (utf8 ("Сохранить пресет"),
                                         utf8 ("Название нового пресета. Сохранятся уровни, панорама и выходы микрофонов и режимы выбора удара."),
                                         juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", {}, utf8 ("Название"));
        w->addButton (utf8 ("Сохранить"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (utf8 ("Отмена"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<PresetPanel> (this), w] (int result)
        {
            if (safe == nullptr || result != 1)
                return;
            juce::String error;
            const auto file = safe->processor.saveCurrentPresetAs (w->getTextEditorContents ("name"), error);
            safe->rescan();
            safe->statusWarns = file == juce::File();
            safe->statusText = safe->statusWarns ? error : utf8 ("Сохранено: ") + file.getFileName() + utf8 (" (папка пресетов)");
            safe->repaint (safe->statusArea);
        }), true);
    }

    void PresetPanel::chooseLibrary()
    {
        const auto current = processor.getLicense().getLibraryRoot();
        chooser = std::make_unique<juce::FileChooser> (utf8 ("Папка с библиотекой (00-Studio600ru-Drum-LIB). Отмена: сбросить папку"), current);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safe = juce::Component::SafePointer<PresetPanel> (this)] (const juce::FileChooser& fc)
        {
            if (safe == nullptr)
                return;
            auto& lic = safe->processor.getLicense();
            const auto dir = fc.getResult();
            if (dir == juce::File())
            {
                lic.setLibraryRoot ({});
                const bool enforced = lic.isEnforced();
                safe->statusText = enforced ? utf8 ("Обычный режим: звук из пакетов")
                                            : utf8 ("Папка библиотеки не выбрана: выберите её («Моя библиотека…»)");
                safe->statusWarns = ! enforced;
            }
            else
            {
                lic.setLibraryRoot (dir);
                const int manifests = lic.getManifestDir().findChildFiles (juce::File::findFiles, false, "*.json").size();
                safe->statusWarns = manifests == 0;
                safe->statusText = manifests == 0
                    ? utf8 ("Нет манифестов в ") + lic.getManifestDir().getFullPathName()
                    : utf8 ("Звук из папки ") + dir.getFullPathName() + utf8 (". Выберите пресет заново");
            }
            safe->rescan();
            safe->repaint (safe->statusArea);
        });
    }

    void PresetPanel::addPack()
    {
        chooser = std::make_unique<juce::FileChooser> (utf8 ("Выберите пакет"), juce::File(), "*.splpack");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [safe = juce::Component::SafePointer<PresetPanel> (this)] (const juce::FileChooser& fc)
        {
            if (safe == nullptr || fc.getResults().isEmpty())
                return;
            const auto dir = safe->processor.getLicense().getPackDir();
            dir.createDirectory();
            int copied = 0, existed = 0, failed = 0;
            for (const auto& f : fc.getResults())
            {
                const auto target = dir.getChildFile (f.getFileName());
                if (target.exists())
                    ++existed;
                else if (f.copyFileTo (target))
                    ++copied;
                else
                    ++failed;
            }
            juce::String text = utf8 ("Добавлено пакетов: ") + juce::String (copied);
            if (existed > 0)
                text << utf8 (". Уже были: ") << existed;
            if (failed > 0)
                text << utf8 (". Не скопировались: ") << failed;
            if (copied > 0)
                text << utf8 (". Если пресет молчит, этого пакета нет в вашей лицензии");
            safe->rescan();
            safe->statusText = text;
            safe->statusWarns = failed > 0;
            safe->repaint (safe->statusArea);
        });
    }

    void PresetPanel::load (int row)
    {
        if (! juce::isPositiveAndBelow (row, (int) presets.size()))
            return;

        const auto& e = presets[(size_t) row];
        if (! e.isPlayable())
        {
            if (! processor.getLicense().isEnforced())
                statusText = utf8 ("«") + e.preset.name + utf8 ("» не зазвучит: выберите папку библиотеки («Моя библиотека…»)");
            else
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
        if (onPicked)
            onPicked();
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
            if (processor.getLicense().isEnforced())
                statusText = utf8 ("Не удалось загрузить: ") + last.error
                             + utf8 (". Проверьте лицензию и пакеты, затем выберите пресет снова.");
            else
                statusText = utf8 ("Не удалось загрузить: ") + last.error
                             + utf8 (". Выберите папку библиотеки («Моя библиотека…»), затем выберите пресет снова.");
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
        else if (const auto& lic = processor.getLicense();
                 lic.getLibraryRoot() != juce::File() && lic.getManifestDir().findChildFiles (juce::File::findFiles, false, "*.json").isEmpty())
        {
            statusText = utf8 ("Режим «Моя библиотека»: нет манифестов в ") + lic.getManifestDir().getFullPathName();
            statusWarns = true;
        }
        else if (! processor.getLicense().isEnforced() && processor.getLicense().getLibraryRoot() == juce::File())
        {
            statusText = utf8 ("Выберите папку библиотеки («Моя библиотека…»)");
            statusWarns = true;
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
        else if (e.file.getFileName().startsWith ("user-"))
        {
            g.setColour (brand::textMuted);
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (utf8 ("Мой"), top.removeFromRight (60), juce::Justification::centredRight);
        }

        g.setColour (e.isPlayable() ? brand::text : brand::textMuted);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (e.preset.name.isNotEmpty() ? e.preset.name : e.preset.id, top, juce::Justification::centredLeft, true);

        g.setColour (e.isComplete() ? brand::textMuted : kWarning);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (e.isComplete() && e.preset.comment.isNotEmpty() ? e.preset.comment : packSummary (e), text,
                    juce::Justification::centredLeft, true);
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
        intro.setBounds (area.removeFromTop (48));
        area.removeFromTop (6);

        statusArea = getLocalBounds().removeFromBottom (kStatusHeight);
        area.removeFromBottom (kStatusHeight - 10);

        // Кнопки под списком в три ряда
        libraryButton.setBounds (area.removeFromBottom (30));
        area.removeFromBottom (6);
        auto second = area.removeFromBottom (30);
        area.removeFromBottom (6);
        auto first = area.removeFromBottom (30);
        const int w2 = (first.getWidth() - 6) / 2;
        saveButton.setBounds (first.removeFromLeft (w2));
        first.removeFromLeft (6);
        rescanButton.setBounds (first);
        if (! processor.getLicense().isEnforced())
        {
            folderButton.setBounds (second);
        }
        else
        {
            const int w3 = (second.getWidth() - 12) / 3;
            folderButton.setBounds (second.removeFromLeft (w3));
            second.removeFromLeft (6);
            packFolderButton.setBounds (second.removeFromLeft (w3));
            second.removeFromLeft (6);
            addPackButton.setBounds (second);
        }
        area.removeFromBottom (8);

        list.setBounds (area);
        emptyText.setBounds (area);
    }
}
