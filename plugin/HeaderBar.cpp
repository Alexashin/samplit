#include "HeaderBar.h"
#include "Brand.h"

namespace samplit
{
    namespace
    {
        const juce::Colour kWarning { 0xffffb454 };

        juce::String sizeText (int64_t bytes)
        {
            const double gb = (double) bytes / (1024.0 * 1024.0 * 1024.0);
            if (gb >= 0.1)
                return juce::String (gb, 1).replaceCharacter ('.', ',') + utf8 (" ГБ");
            const int mb = juce::roundToInt ((double) bytes / (1024.0 * 1024.0));
            return mb < 1 ? utf8 ("меньше 1 МБ") : juce::String (mb) + utf8 (" МБ");
        }
    }

    HeaderBar::HeaderBar (SamplitProcessor& p) : processor (p)
    {
        libraryButton.setButtonText (utf8 ("Моя библиотека…"));
        libraryButton.setTooltip (utf8 ("Выберите папку со своими WAV (00-Studio600ru-Drum-LIB): звук берётся из неё целиком, без обрезки хвостов. "
                                        "Отмена выбора сбрасывает папку"));
        libraryButton.onClick = [this] { chooseLibrary(); };

        defaultButton.setButtonText (utf8 ("Сделать стандартной"));
        defaultButton.setTooltip (utf8 ("Запомнить текущие места, паки, громкости и режимы как вашу стандартную установку"));
        defaultButton.onClick = [this]
        {
            if (processor.saveAsDefaultSetup())
                say (utf8 ("Текущая установка сохранена как стандартная"), false);
            else
                say (utf8 ("Не удалось сохранить стандартную установку: нет доступа к папке данных"), true);
        };

        resetButton.setButtonText (utf8 ("Сброс"));
        resetButton.setTooltip (utf8 ("Вернуться к вашей стандартной установке, а если её нет, к заводской"));
        resetButton.onClick = [this]
        {
            processor.resetToDefaultSetup();
            say (utf8 ("Установка сброшена к стандартной"), false);
        };

        moreButton.setButtonText (utf8 ("…"));
        moreButton.setTooltip (utf8 ("Ещё: вернуть заводскую установку, обновить список паков"));
        moreButton.onClick = [this] { showMoreMenu(); };

        for (auto* c : std::initializer_list<juce::Component*> { &libraryButton, &defaultButton, &resetButton, &moreButton })
            addAndMakeVisible (c);

        processor.addChangeListener (this);
        updateStatus();
        startTimerHz (10);
    }

    HeaderBar::~HeaderBar()
    {
        processor.removeChangeListener (this);
    }

    void HeaderBar::say (const juce::String& text, bool warns)
    {
        note = text;
        noteWarns = warns;
        updateStatus();
    }

    void HeaderBar::showMoreMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (1, utf8 ("Вернуть заводскую"));
        menu.addItem (2, utf8 ("Обновить список паков"));
        menu.addItem (5, utf8 ("Сохранить установку в файл…"));
        menu.addItem (6, utf8 ("Открыть установку из файла…"));
        if (processor.getLicense().isEnforced())
        {
            menu.addSeparator();
            menu.addItem (3, utf8 ("Добавить пакет…"));
            menu.addItem (4, utf8 ("Открыть папку пакетов"));
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&moreButton),
                            [safe = juce::Component::SafePointer<HeaderBar> (this)] (int result)
        {
            if (safe == nullptr || result == 0)
                return;
            auto& p = safe->processor;
            if (result == 1)
            {
                juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon,
                    utf8 ("Заводская установка"),
                    utf8 ("Вернуть заводскую установку? Ваша стандартная установка будет удалена."),
                    utf8 ("Вернуть"), utf8 ("Отмена"), safe.getComponent(),
                    juce::ModalCallbackFunction::create ([safe] (int r)
                    {
                        if (safe == nullptr || r != 1)
                            return;
                        safe->processor.restoreFactorySetup();
                        safe->say (utf8 ("Возвращена заводская установка"), false);
                    }));
            }
            else if (result == 2)
            {
                p.rescanCatalog();
                safe->say (utf8 ("Список паков обновлён"), false);
            }
            else if (result == 5 || result == 6)
            {
                const bool save = result == 5;
                safe->chooser = std::make_unique<juce::FileChooser> (
                    save ? utf8 ("Сохранить установку") : utf8 ("Открыть установку"),
                    juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Samplit-setup.json"), "*.json");
                safe->chooser->launchAsync (save ? (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting)
                                                 : juce::FileBrowserComponent::openMode,
                    [safe, save] (const juce::FileChooser& fc)
                {
                    const auto f = fc.getResult();
                    if (safe == nullptr || f == juce::File())
                        return;
                    if (save)
                    {
                        const bool ok = f.withFileExtension ("json").replaceWithText (safe->processor.exportSetupFile(), false, false, "\n");
                        safe->say (ok ? utf8 ("Установка сохранена в файл") : utf8 ("Не удалось записать файл"), ! ok);
                    }
                    else
                    {
                        juce::String err;
                        if (safe->processor.importSetupFile (f.loadFileAsString(), err))
                            safe->say (utf8 ("Установка загружена из файла"), false);
                        else
                            safe->say (err.isNotEmpty() ? err : utf8 ("Не удалось прочитать файл установки"), true);
                    }
                });
            }
            else if (result == 3)
                safe->addPack();
            else if (result == 4)
            {
                const auto dir = p.getLicense().getPackDir();
                dir.createDirectory();
                dir.startAsProcess();
            }
        });
    }

    void HeaderBar::chooseLibrary()
    {
        const auto current = processor.getLicense().getLibraryRoot();
        chooser = std::make_unique<juce::FileChooser> (utf8 ("Папка с библиотекой (00-Studio600ru-Drum-LIB). Отмена: сбросить папку"), current);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [safe = juce::Component::SafePointer<HeaderBar> (this)] (const juce::FileChooser& fc)
        {
            if (safe == nullptr)
                return;
            auto& lic = safe->processor.getLicense();
            const auto dir = fc.getResult();
            if (dir == juce::File())
            {
                lic.setLibraryRoot ({});
                const bool enforced = lic.isEnforced();
                safe->say (enforced ? utf8 ("Обычный режим: звук из пакетов")
                                    : utf8 ("Папка библиотеки не выбрана: выберите её («Моя библиотека…»)"),
                           ! enforced);
            }
            else
            {
                lic.setLibraryRoot (dir);
                const int manifests = lic.getManifestDir().findChildFiles (juce::File::findFiles, false, "*.json").size();
                safe->say (manifests == 0 ? utf8 ("Нет манифестов в ") + lic.getManifestDir().getFullPathName()
                                          : utf8 ("Звук из папки ") + dir.getFullPathName(),
                           manifests == 0);
            }
            safe->processor.rescanCatalog();
        });
    }

    void HeaderBar::addPack()
    {
        chooser = std::make_unique<juce::FileChooser> (utf8 ("Выберите пакет"), juce::File(), "*.splpack");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [safe = juce::Component::SafePointer<HeaderBar> (this)] (const juce::FileChooser& fc)
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
                text << utf8 (". Если барабан молчит, этого пака нет в вашей лицензии");
            safe->processor.rescanCatalog();
            safe->say (text, failed > 0);
        });
    }

    // Короткие предупреждения о звуке по приоритету: ошибка чтения, обрывы, жёсткие вытеснения.
    // Мягкие вытеснения (steals) пользователю не показываются: это норма
    juce::String HeaderBar::healthWarning (const StreamHealth& h)
    {
        juce::StringArray parts;
        if (h.readErrors > 0)
            parts.add (utf8 ("Ошибка чтения файла: ") + (h.lastError.isNotEmpty() ? h.lastError : utf8 ("неизвестный файл")));
        if (h.underruns > 0)
            parts.add (utf8 ("Диск не успевает: ") + juce::String (h.underruns) + utf8 (" обрывов"));
        if (h.hardSteals > 0)
            parts.add (utf8 ("Не хватило голосов: ") + juce::String (h.hardSteals));
        return parts.joinIntoString (". ");
    }

    void HeaderBar::updateStatus()
    {
        const auto health = processor.getHealth();
        shownHealth = health;
        const auto& last = processor.getLastKitLoad();
        statusWarns = false;

        if (processor.isKitLoading())
        {
            statusText = utf8 ("Загрузка: ") + juce::String (juce::roundToInt (juce::jmax (0.0f, shownProgress) * 100.0f)) + " %";
        }
        else if (note.isNotEmpty() && last.error.isEmpty())
        {
            statusText = note;
            statusWarns = noteWarns;
        }
        else if (last.cancelled)
        {
            statusText = utf8 ("Загрузка отменена");
        }
        else if (last.error.isNotEmpty())
        {
            statusText = utf8 ("Не удалось загрузить: ") + last.error
                         + (processor.getLicense().isEnforced() ? utf8 (". Проверьте лицензию и пакеты")
                                                                : utf8 (". Выберите папку библиотеки («Моя библиотека…»)"));
            statusWarns = true;
        }
        else if (const auto kit = processor.getKit(); kit != nullptr)
        {
            statusText = utf8 ("Загружено, в памяти ") + sizeText (last.memoryBytes);
            if (! last.missingPacks.empty())
            {
                juce::StringArray ids;
                for (const auto& id : last.missingPacks)
                    ids.add (id);
                statusText << utf8 (". Молчат барабаны паков: ") << ids.joinIntoString (", ");
                statusWarns = true;
            }
            if (const auto warning = healthWarning (health); warning.isNotEmpty())
            {
                statusText << ". " << warning;
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
            statusText = {};
        }
        repaint (statusArea);
    }

    void HeaderBar::timerCallback()
    {
        const float progress = processor.isKitLoading() ? processor.getKitLoadProgress() : -1.0f;
        const auto health = processor.getHealth();      // один снимок на тик
        auto comparable = health;
        comparable.steals = shownHealth.steals;          // мягкие вытеснения не показываются: не повод перерисовывать
        if (progress != shownProgress || comparable != shownHealth)
        {
            if (shownProgress >= 0.0f && progress < 0.0f)
            {
                note = {};                               // загрузка закончилась: ответ на прежнее действие устарел
            }
            shownProgress = progress;
            updateStatus();
        }
    }

    juce::String HeaderBar::getTooltip()
    {
        return statusArea.contains (getMouseXYRelative()) ? statusText : juce::String();
    }

    void HeaderBar::paint (juce::Graphics& g)
    {
        g.setColour (statusWarns ? kWarning : brand::textMuted);
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (statusText, statusArea, juce::Justification::centredLeft, 2, 1.0f);
    }

    void HeaderBar::resized()
    {
        auto area = getLocalBounds();
        moreButton.setBounds (area.removeFromRight (36));
        area.removeFromRight (6);
        resetButton.setBounds (area.removeFromRight (70));
        area.removeFromRight (6);
        defaultButton.setBounds (area.removeFromRight (150));
        area.removeFromRight (12);
        libraryButton.setBounds (area.removeFromLeft (140));
        area.removeFromLeft (12);
        statusArea = area;
    }
}
