#include "PluginEditor.h"
#include "Brand.h"

#include <BinaryData.h>

namespace samplit
{
    namespace
    {
        constexpr int kHeaderHeight = 64;
        constexpr int kMargin = 20;
        constexpr int kOverlayWidth = 460;
        const juce::Colour kWarning { 0xffffb454 };
        const juce::Colour kOk { 0xff6fd6a1 };
    }

    juce::AudioProcessorEditor* SamplitProcessor::createEditor()
    {
        return new SamplitEditor (*this);
    }

    SamplitEditor::SamplitEditor (SamplitProcessor& p)
        : AudioProcessorEditor (&p), processor (p), presets (p), mixer (p), kitView (p), keyboard (p)
    {
        setLookAndFeel (&lookAndFeel);
        logo = juce::Drawable::createFromImageData (BinaryData::lockuphondark_svg, BinaryData::lockuphondark_svgSize);

        // Лицензия отложена (SAMPLIT_LICENSE=OFF): панели, кнопки и подписки на неё нет
        if (processor.getLicense().isEnforced())
        {
            license = std::make_unique<LicensePanel> (p.getLicense());

            // Лицензия поменялась: другие пакеты открыты, у пресетов другое состояние
            processor.getLicense().onChange = [this]
            {
                license->refresh();
                presets.rescan();
                if (license->isOk() && overlay == Overlay::License)
                    showOverlay (Overlay::None);
                updateHeader();
                resized();
            };
        }

        presetButton.setTooltip (utf8 ("Список пресетов"));
        presetButton.onClick = [this] { showOverlay (overlay == Overlay::Presets ? Overlay::None : Overlay::Presets); };
        prevButton.setButtonText ("<");
        nextButton.setButtonText (">");
        prevButton.setTooltip (utf8 ("Предыдущий пресет"));
        nextButton.setTooltip (utf8 ("Следующий пресет"));
        prevButton.onClick = [this] { presets.step (-1); };
        nextButton.onClick = [this] { presets.step (1); };
        presets.onPicked = [this] { showOverlay (Overlay::None); };

        mixButton.setTooltip (utf8 ("Микс микрофонов: уровни, панорама и выходы. Меняется мгновенно, без перезагрузки кита"));
        mixButton.onClick = [this] { showMixMenu(); };

        licenseButton.setTooltip (utf8 ("Лицензия: срок действия, ввод ключа"));
        licenseButton.onClick = [this] { showOverlay (overlay == Overlay::License ? Overlay::None : Overlay::License); };

        scrim.onClick = [this] { showOverlay (Overlay::None); };

        kitTab.setButtonText (utf8 ("Установка"));
        kitTab.setTooltip (utf8 ("Схема барабанов: клик по барабану играет его и открывает его настройки"));
        mixerTab.setButtonText (utf8 ("Микшер"));
        mixerTab.setTooltip (utf8 ("Уровень, панорама, выход, Mute и Solo каждого микрофона"));
        for (auto* t : { &kitTab, &mixerTab })
        {
            t->setClickingTogglesState (true);
            t->setRadioGroupId (1);
        }
        kitTab.onClick = [this] { showMainTab (false); };
        mixerTab.onClick = [this] { showMainTab (true); };

        // Порядок добавления = порядок наложения: подложка и выпадающие панели поверх всего
        for (auto* c : std::initializer_list<juce::Component*> { &mixer, &kitView, &kitTab, &mixerTab, &keyboard, &prevButton,
                                                                  &nextButton, &presetButton, &licenseButton, &mixButton, &scrim, &presets })
            addChildComponent (c);
        if (license != nullptr)
            addChildComponent (license.get());
        for (auto* c : std::initializer_list<juce::Component*> { &kitTab, &mixerTab, &keyboard, &prevButton, &nextButton,
                                                                  &presetButton, &licenseButton, &mixButton })
            c->setVisible (true);
        showMainTab (false);
        updateHeader();

        juce::Desktop::getInstance().addFocusChangeListener (this);

        setResizable (true, true);
        setResizeLimits (900, 620, 1800, 1200);
        setSize (1180, 780);

        licenseButton.setVisible (license != nullptr);
        if (license != nullptr && ! license->isOk())
            showOverlay (Overlay::License);
        startTimerHz (10);
    }

    SamplitEditor::~SamplitEditor()
    {
        stopTimer();
        juce::Desktop::getInstance().removeFocusChangeListener (this);
        if (license != nullptr)
            processor.getLicense().onChange = nullptr;
        presets.onPicked = nullptr;
        setLookAndFeel (nullptr);
    }

    // Обводка фокуса рисуется в LookAndFeel: перерисовать прежний и новый элемент
    void SamplitEditor::globalFocusChanged (juce::Component* focused)
    {
        if (lastFocused != nullptr)
            lastFocused->repaint();
        lastFocused = focused != nullptr && isParentOf (focused) ? focused : nullptr;
        if (lastFocused != nullptr)
            lastFocused->repaint();
    }

    void SamplitEditor::timerCallback()
    {
        updateHeader();
    }

    // Подписи кнопок шапки и ход загрузки
    void SamplitEditor::updateHeader()
    {
        const float progress = processor.isKitLoading() ? processor.getKitLoadProgress() : -1.0f;
        juce::String name;
        if (processor.isKitLoading())
            name = utf8 ("Загрузка… ") + juce::String (juce::roundToInt (juce::jmax (0.0f, progress) * 100.0f)) + " %";
        else if (const auto kit = processor.getKit(); kit != nullptr)
            name = juce::String::fromUTF8 (kit->title.c_str());
        else
            name = utf8 ("Выберите пресет");
        if (presetButton.getButtonText() != name)
            presetButton.setButtonText (name);

        // Кнопка микса: имя берём из файлов миксов, перечитываем только когда id сменился
        const auto mixId = processor.getMixName();
        if (mixId != mixShownId)
        {
            mixShownId = mixId;
            mixes = processor.scanMixes();
        }
        juce::String mixLabel = utf8 ("Свой микс");
        if (mixId.isNotEmpty())
        {
            mixLabel = mixId;
            for (const auto& m : mixes)
                if (m.id == mixId && m.name.isNotEmpty())
                    mixLabel = m.name;
            if (processor.isMixModified())
                mixLabel << utf8 (" (изменён)");
        }
        mixLabel = utf8 ("Микс: ") + mixLabel;
        if (mixButton.getButtonText() != mixLabel)
            mixButton.setButtonText (mixLabel);

        if (license != nullptr)
        {
            const auto& info = license->getInfo();
            juce::String chip;
            if (! license->isOk())
                chip = utf8 ("    Нет лицензии");
            else if (info.expires == 0)
                chip = utf8 ("    Лицензия активна");
            else
                chip = utf8 ("    Лицензия до ") + juce::Time (info.expires * 1000).formatted ("%d.%m.%Y");
            if (licenseButton.getButtonText() != chip)
            {
                licenseButton.setButtonText (chip);
                licenseButton.setColour (juce::TextButton::textColourOffId, license->isOk() ? brand::text : kWarning);
                repaint (headerArea);
            }
        }

        if (progress != shownProgress)
        {
            shownProgress = progress;
            repaint (headerArea);
        }
    }

    // Список миксов: выбрать готовый, сохранить текущий, открыть папку
    void SamplitEditor::showMixMenu()
    {
        mixes = processor.scanMixes();
        const auto current = processor.getMixName();
        juce::PopupMenu menu;
        menu.addSectionHeader (utf8 ("Микс микрофонов"));
        for (int i = 0; i < (int) mixes.size(); ++i)
        {
            const auto& m = mixes[(size_t) i];
            juce::PopupMenu::Item item (m.name.isNotEmpty() ? m.name : m.id);
            item.setID (i + 1).setTicked (m.id == current).setEnabled (processor.getKit() != nullptr);
            if (m.id.startsWith ("user-"))
                item.setTicked (m.id == current);
            menu.addItem (item);
        }
        if (mixes.empty())
            menu.addItem (-1, utf8 ("Миксов пока нет"), false);
        menu.addSeparator();
        juce::PopupMenu mine;
        for (int i = 0; i < (int) mixes.size(); ++i)
            if (mixes[(size_t) i].id.startsWith ("user-"))
            {
                juce::PopupMenu sub;
                sub.addItem (2000 + i, utf8 ("Переименовать…"));
                sub.addItem (3000 + i, utf8 ("Удалить (в корзину)"));
                mine.addSubMenu (mixes[(size_t) i].name.isNotEmpty() ? mixes[(size_t) i].name : mixes[(size_t) i].id, sub);
            }
        if (mine.getNumItems() > 0)
            menu.addSubMenu (utf8 ("Мои миксы"), mine);
        menu.addItem (1000, utf8 ("Сохранить текущий микс…"), processor.getKit() != nullptr);
        menu.addItem (1001, utf8 ("Открыть папку миксов"));

        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&mixButton).withMinimumWidth (mixButton.getWidth()),
                            [safe = juce::Component::SafePointer<SamplitEditor> (this), items = mixes] (int result)
        {
            if (safe == nullptr || result <= 0)
                return;
            if (result == 1000)
                safe->saveMixAs();
            else if (result == 1001)
            {
                const auto dir = safe->processor.getMixDir();
                dir.createDirectory();
                dir.startAsProcess();
            }
            else if (result >= 3000 || result >= 2000)
            {
                const int index = result >= 3000 ? result - 3000 : result - 2000;
                if (juce::isPositiveAndBelow (index, (int) items.size()))
                    safe->editMix (items[(size_t) index], result >= 3000);
            }
            else if (juce::isPositiveAndBelow (result - 1, (int) items.size()))
                safe->processor.applyMix (items[(size_t) (result - 1)]);
            safe->updateHeader();
        });
    }

    // Свой микс: удалить или переименовать
    void SamplitEditor::editMix (const Mix& mix, bool remove)
    {
        const auto file = processor.getMixFile (mix.id);
        if (remove)
        {
            juce::String error;
            if (! processor.deleteUserFile (file, error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, utf8 ("Микс не удалён"), error);
            updateHeader();
            return;
        }

        auto* w = new juce::AlertWindow (utf8 ("Переименовать микс"), {}, juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", mix.name, utf8 ("Название"));
        w->addButton (utf8 ("Сохранить"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (utf8 ("Отмена"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<SamplitEditor> (this), file, w] (int r)
        {
            if (safe == nullptr || r != 1)
                return;
            juce::String error;
            if (! safe->processor.renameUserFile (file, w->getTextEditorContents ("name"), error))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, utf8 ("Микс не переименован"), error);
            safe->mixShownId = "?";    // перечитать названия
            safe->updateHeader();
        }), true);
    }

    void SamplitEditor::saveMixAs()
    {
        auto* w = new juce::AlertWindow (utf8 ("Сохранить микс"),
                                         utf8 ("Название микса. Сохранятся уровни, панорама, выходы и Mute всех микрофонов."),
                                         juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", {}, utf8 ("Название"));
        w->addButton (utf8 ("Сохранить"), 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton (utf8 ("Отмена"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe = juce::Component::SafePointer<SamplitEditor> (this), w] (int result)
        {
            if (safe == nullptr || result != 1)
                return;
            juce::String error;
            const auto file = safe->processor.saveCurrentMixAs (w->getTextEditorContents ("name"), error);
            if (file == juce::File())
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, utf8 ("Микс не сохранён"), error);
            safe->updateHeader();
        }), true);
    }

    void SamplitEditor::showOverlay (Overlay which)
    {
        overlay = which;
        scrim.setVisible (which != Overlay::None);
        presets.setVisible (which == Overlay::Presets);
        if (license != nullptr)
            license->setVisible (which == Overlay::License);
        if (which == Overlay::Presets)
            presets.rescan();
        resized();
        repaint();
    }

    bool SamplitEditor::keyPressed (const juce::KeyPress& k)
    {
        if (k == juce::KeyPress::escapeKey && overlay != Overlay::None)
        {
            showOverlay (Overlay::None);
            return true;
        }
        return false;
    }

    void SamplitEditor::showMainTab (bool mixerShown)
    {
        (mixerShown ? mixerTab : kitTab).setToggleState (true, juce::dontSendNotification);
        mixer.setVisible (mixerShown);
        kitView.setVisible (! mixerShown);
    }

    void SamplitEditor::resized()
    {
        auto bounds = getLocalBounds();
        headerArea = bounds.removeFromTop (kHeaderHeight);

        // Шапка: знак слева, пресет со стрелками по центру, лицензия справа
        auto bar = headerArea.reduced (kMargin, 0);
        logoArea = bar.removeFromLeft (150).withSizeKeepingCentre (150, 30);
        if (license != nullptr)
        {
            licenseButton.setBounds (bar.removeFromRight (230).withSizeKeepingCentre (230, 34));
            bar.removeFromRight (16);
        }

        const int mixWidth = juce::jlimit (170, 250, bar.getWidth() / 4);
        mixButton.setBounds (bar.removeFromRight (mixWidth).withSizeKeepingCentre (mixWidth, 38));
        bar.removeFromRight (12);
        auto selector = bar.withSizeKeepingCentre (juce::jlimit (240, 460, bar.getWidth() - 32), 38);
        prevButton.setBounds (selector.removeFromLeft (38));
        nextButton.setBounds (selector.removeFromRight (38));
        selector.reduce (6, 0);
        presetButton.setBounds (selector);

        auto area = bounds.reduced (kMargin, 0);
        area.removeFromTop (14);
        area.removeFromBottom (kMargin);

        keyboard.setBounds (area.removeFromBottom (juce::jlimit (130, 180, area.getHeight() / 4)));
        area.removeFromBottom (14);

        auto tabs = area.removeFromTop (30);
        kitTab.setBounds (tabs.removeFromLeft (130));
        tabs.removeFromLeft (6);
        mixerTab.setBounds (tabs.removeFromLeft (130));
        area.removeFromTop (8);
        mixer.setBounds (area);
        kitView.setBounds (area);

        // Выпадающие панели под шапкой: пресеты под кнопкой пресета, лицензия у правого края
        scrim.setBounds (getLocalBounds().withTrimmedTop (kHeaderHeight));
        const int width = juce::jmin (kOverlayWidth, getWidth() - 2 * kMargin);
        const int panelHeight = juce::jmin (580, getHeight() - kHeaderHeight - 2 * kMargin);
        const int presetsX = juce::jlimit (kMargin, juce::jmax (kMargin, getWidth() - kMargin - width),
                                           presetButton.getBounds().getCentreX() - width / 2);
        presets.setBounds (presetsX, kHeaderHeight + 6, width, panelHeight);
        if (license != nullptr)
            license->setBounds (getWidth() - kMargin - width, kHeaderHeight + 6, width, license->isOk() ? 130 : 180);
    }

    void SamplitEditor::paint (juce::Graphics& g)
    {
        g.fillAll (brand::bg);

        // Шапка чуть светлее фона; под ней линия, а при загрузке пресета ещё и полоска хода загрузки
        g.setGradientFill (juce::ColourGradient (brand::surface.brighter (0.05f), 0.0f, 0.0f, brand::surface,
                                                 0.0f, (float) kHeaderHeight, false));
        g.fillRect (headerArea);
        g.setColour (brand::line);
        g.fillRect (headerArea.getX(), headerArea.getBottom() - 1, headerArea.getWidth(), 1);
        if (shownProgress >= 0.0f)
        {
            g.setColour (brand::accent);
            g.fillRect (juce::Rectangle<float> (0.0f, (float) kHeaderHeight - 3.0f,
                                                (float) getWidth() * juce::jlimit (0.0f, 1.0f, shownProgress), 3.0f));
        }

        if (logo != nullptr)
            logo->drawWithin (g, logoArea.toFloat(), juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid, 1.0f);
    }

    // Точка состояния лицензии на кнопке и рамка выпадающих панелей
    void SamplitEditor::paintOverChildren (juce::Graphics& g)
    {
        if (license != nullptr)
        {
            const auto chip = licenseButton.getBounds();
            g.setColour (license->isOk() ? kOk : kWarning);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ (float) chip.getX() + 16.0f, (float) chip.getCentreY() }));
        }

        for (auto* c : std::initializer_list<juce::Component*> { &presets, license.get() })
            if (c != nullptr && c->isVisible())
            {
                g.setColour (brand::line);
                g.drawRoundedRectangle (c->getBounds().toFloat().expanded (0.5f), 12.5f, 1.0f);
            }
    }
}
