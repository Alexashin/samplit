#include "PluginEditor.h"
#include "Brand.h"
#include "ui/Fonts.h"

#include <BinaryData.h>

namespace samplit
{
    namespace
    {
        constexpr int kHeaderHeight = 48;
        constexpr int kStatusHeight = 28;
        constexpr int kFooterHeight = 44;
        constexpr int kMargin = 16;
        constexpr int kOverlayWidth = 460;
    }

    juce::AudioProcessorEditor* SamplitProcessor::createEditor()
    {
        return new SamplitEditor (*this);
    }

    SamplitEditor::FirstRunPanel::FirstRunPanel()
    {
        chooseButton.setButtonText (utf8 ("Выбрать папку со звуком…"));
        chooseButton.setColour (juce::TextButton::buttonColourId, brand::accent);
        addAndMakeVisible (chooseButton);
    }

    void SamplitEditor::FirstRunPanel::paint (juce::Graphics& g)
    {
        g.fillAll (brand::bg);
        auto r = getLocalBounds().reduced (24).withSizeKeepingCentre (juce::jmin (560, getWidth() - 48), 150);
        g.setColour (brand::text);
        g.setFont (fonts::strong (22.0f));
        g.drawText (utf8 ("Выберите папку со звуком"), r.removeFromTop (34), juce::Justification::centredLeft);
        g.setColour (brand::textMuted);
        g.setFont (fonts::text (14.0f));
        g.drawFittedText (utf8 ("Укажите папку 00-Studio600ru-Drum-LIB с вашими WAV. Звук читается прямо из неё, без обрезки хвостов. "
                                "Папку можно сменить позже: «Установка ▾», «Папка со звуком…»."),
                          r.removeFromTop (60), juce::Justification::topLeft, 3);
    }

    void SamplitEditor::FirstRunPanel::resized()
    {
        auto r = getLocalBounds().reduced (24).withSizeKeepingCentre (juce::jmin (560, getWidth() - 48), 150);
        r.removeFromTop (100);
        chooseButton.setBounds (r.removeFromTop (34).removeFromLeft (240));
    }

    SamplitEditor::SamplitEditor (SamplitProcessor& p)
        : AudioProcessorEditor (&p), processor (p), headerBar (p), settingsPanel (p), mixer (p), drumPanel (p), kitView (p), patternPanel (p), strip (p), keyboard (p)
    {
        setLookAndFeel (&lookAndFeel);
        logo = juce::Drawable::createFromImageData (BinaryData::lockuphondark_svg, BinaryData::lockuphondark_svgSize);

        // Лицензия отложена (SAMPLIT_LICENSE=OFF): панели, кнопки и подписки на неё нет
        if (processor.getLicense().isEnforced())
        {
            license = std::make_unique<LicensePanel> (p.getLicense());

            // Лицензия поменялась: другие пакеты открыты, состояние загрузки могло измениться
            processor.getLicense().onChange = [this]
            {
                license->refresh();
                headerBar.refresh();
                if (license->isOk() && overlay == Overlay::License)
                    showOverlay (Overlay::None);
                updateHeader();
                resized();
            };
        }

        licenseButton.setTooltip (utf8 ("Лицензия: срок действия, ввод ключа"));
        licenseButton.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        licenseButton.onClick = [this] { showOverlay (overlay == Overlay::License ? Overlay::None : Overlay::License); };
        scrim.onClick = [this] { showOverlay (Overlay::None); };

        // Страницы окна: порядок = порядок вкладок. Новая страница добавляется сюда одной строкой
        pages = { { utf8 ("Барабаны"), &kitView, &drumPanel, true },
                  { utf8 ("Партия"), &patternPanel, nullptr, true },
                  { utf8 ("Микшер"), &mixer, nullptr, false } };
        std::vector<juce::String> names;
        for (const auto& page : pages)
            names.push_back (page.name);
        pageTabs.setItems (names);
        pageTabs.onChange = [this] (int i) { showPage (i); };

        setupButton.setButtonText (utf8 ("Установка  ▾"));
        setupButton.setTooltip (utf8 ("Папка со звуком, стандартная и заводская установка, файлы установки"));
        setupButton.onClick = [this] { headerBar.showSetupMenu (setupButton); };
        settingsButton.onClick = [this] { showOverlay (overlay == Overlay::Settings ? Overlay::None : Overlay::Settings); };
        settingsPanel.onWindowSize = [this] (int i) { setWindowSize (i == 0 ? WindowSize::Compact : i == 1 ? WindowSize::Standard : WindowSize::Expanded); };
        settingsPanel.onChooseFolder = [this] { showOverlay (Overlay::None); headerBar.chooseLibrary(); };
        settingsPanel.onRescan = [this] { processor.rescanCatalog(); showOverlay (Overlay::None); };
        helpButton.onClick = [this] { showHelp(); };

        keyboardToggle.setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        keyboardToggle.setColour (juce::TextButton::textColourOffId, brand::textMuted);
        keyboardToggle.setTooltip (utf8 ("Показать или скрыть клавиатуру внизу"));
        keyboardToggle.onClick = [this] { setKeyboardOpen (! processor.getUiState().keyboardOpen); };

        kitView.onSelect = [safe = juce::Component::SafePointer<SamplitEditor> (this)] (const juce::String& placeId)
        {
            if (safe != nullptr)
                safe->drumPanel.selectPlace (placeId);
        };
        selectInstrument ("kick");

        // Порядок добавления = порядок наложения: подложка и выпадающие панели поверх всего
        for (auto* c : std::initializer_list<juce::Component*> { &mixer, &drumPanel, &kitView, &patternPanel, &firstRun, &strip, &keyboard,
                                                                  &headerBar, &setupButton, &pageTabs, &settingsButton, &helpButton, &licenseButton,
                                                                  &keyboardToggle, &scrim })
            addChildComponent (c);
        addChildComponent (settingsPanel);
        if (license != nullptr)
            addChildComponent (license.get());
        for (auto* c : std::initializer_list<juce::Component*> { &strip, &keyboard, &headerBar, &setupButton, &pageTabs,
                                                                  &settingsButton, &helpButton, &keyboardToggle })
            c->setVisible (true);
        firstRun.chooseButton.onClick = [this] { headerBar.chooseLibrary(); };
        showPage (0);
        updateHeader();

        juce::Desktop::getInstance().addFocusChangeListener (this);

        // Размер: сохранённый в проекте или стандартный, но не больше рабочей области экрана (с запасом на рамку DAW)
        setResizable (true, true);
        setResizeLimits (880, 520, 2200, 1400);
        const auto ui = processor.getUiState();
        int w = ui.width > 0 ? ui.width : 1120, h = ui.height > 0 ? ui.height : 640;
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            w = juce::jmin (w, display->userArea.getWidth() - 48);
            h = juce::jmin (h, display->userArea.getHeight() - 96);
        }
        setSize (juce::jmax (880, w), juce::jmax (520, h));
        trackSize = true;

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
        const bool first = ! processor.getLicense().isEnforced() && processor.getLicense().getLibraryRoot() == juce::File()
                           && processor.getKit() == nullptr;
        if (first != firstRun.isVisible())
            firstRun.setVisible (first);
    }

    // Подпись кнопки лицензии и ход загрузки
    void SamplitEditor::updateHeader()
    {
        const float progress = processor.isKitLoading() ? processor.getKitLoadProgress() : -1.0f;

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
                licenseButton.setColour (juce::TextButton::textColourOffId, license->isOk() ? brand::text : brand::warn);
                repaint (headerArea);
            }
        }

        if (progress != shownProgress)
        {
            shownProgress = progress;
            repaint (headerArea);
        }
        const bool kbOpen = processor.getUiState().keyboardOpen;
        const auto label = kbOpen ? utf8 ("Клавиатура  ▾") : utf8 ("Клавиатура  ▴");
        if (keyboardToggle.getButtonText() != label)
            keyboardToggle.setButtonText (label);
    }

    void SamplitEditor::showOverlay (Overlay which)
    {
        overlay = which;
        scrim.setVisible (which != Overlay::None);
        if (license != nullptr)
            license->setVisible (which == Overlay::License);
        settingsPanel.setVisible (which == Overlay::Settings);
        if (which == Overlay::Settings)
            settingsPanel.refresh();
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

    void SamplitEditor::showMainTab (MainTab tab)
    {
        showPage (tab == MainTab::Kit ? 0 : tab == MainTab::Pattern ? 1 : 2);
    }

    void SamplitEditor::showPage (int index)
    {
        currentPage = juce::jlimit (0, (int) pages.size() - 1, index);
        pageTabs.setSelected (currentPage);
        for (int i = 0; i < (int) pages.size(); ++i)
            pages[(size_t) i].content->setVisible (i == currentPage);
        drumPanel.setVisible (pages[(size_t) currentPage].side == &drumPanel);
        strip.setVisible (pages[(size_t) currentPage].hasFooter);
        strip.setTransportVisible (currentPage == 0);
        resized();
        repaint();
    }

    void SamplitEditor::setKeyboardOpen (bool open)
    {
        auto ui = processor.getUiState();
        ui.keyboardOpen = open;
        processor.setUiState (ui);
        resized();
        repaint();
    }

    void SamplitEditor::setWindowSize (WindowSize s)
    {
        int w = s == WindowSize::Compact ? 920 : s == WindowSize::Standard ? 1120 : 1400;
        int h = s == WindowSize::Compact ? 560 : s == WindowSize::Standard ? 640 : 800;
        if (const auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            w = juce::jmin (w, display->userArea.getWidth() - 48);
            h = juce::jmin (h, display->userArea.getHeight() - 96);
        }
        setSize (juce::jmax (880, w), juce::jmax (520, h));
    }

    void SamplitEditor::showHelp()
    {
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, utf8 ("Как пользоваться"),
            utf8 ("1. «Установка ▾», «Папка со звуком…»: выберите папку со звуком Studio600.\n"
                  "2. Страница «Барабаны»: нажмите на барабан, чтобы услышать его. Ниже кликнули, значит сильнее.\n"
                  "3. Страница «Партия»: нарисуйте ритм и перетащите его кнопкой «Перетащить в Cubase» на дорожку.\n"
                  "4. Страница «Микшер»: уровень, панорама и выход каждого микрофона."),
            utf8 ("Понятно"));
    }

    void SamplitEditor::resized()
    {
        auto bounds = getLocalBounds();
        headerArea = bounds.removeFromTop (kHeaderHeight);

        // Шапка: логотип и «Установка ▾» слева, страницы по центру, лицензия, ⚙ и ? справа
        auto bar = headerArea.reduced (kMargin - 4, 0);
        logoArea = bar.removeFromLeft (118).withSizeKeepingCentre (118, 22);
        bar.removeFromLeft (16);
        setupButton.setBounds (bar.removeFromLeft (118).withSizeKeepingCentre (118, 30));
        helpButton.setBounds (bar.removeFromRight (32).withSizeKeepingCentre (32, 32));
        settingsButton.setBounds (bar.removeFromRight (32).withSizeKeepingCentre (32, 32));
        bar.removeFromRight (8);
        if (license != nullptr)
        {
            licenseButton.setBounds (bar.removeFromRight (200).withSizeKeepingCentre (200, 30));
            bar.removeFromRight (8);
        }
        const int tabsW = juce::jmin (pageTabs.preferredWidth(), bar.getWidth());
        pageTabs.setBounds (bar.withSizeKeepingCentre (tabsW, 32).withX (bar.getCentreX() - tabsW / 2));

        // Низ: строка статуса, под ней ничего; над ней клавиатура (если открыта)
        statusArea = bounds.removeFromBottom (kStatusHeight);
        keyboardToggle.setBounds (statusArea.removeFromRight (132).reduced (2));
        headerBar.setBounds (statusArea);
        statusArea = statusArea.withTrimmedRight (0);

        const bool kbOpen = processor.getUiState().keyboardOpen;
        keyboard.setVisible (kbOpen);
        if (kbOpen)
            keyboard.setBounds (bounds.removeFromBottom (juce::jlimit (88, 140, bounds.getHeight() / 5)).reduced (kMargin, 0));

        auto area = bounds;
        if (pages[(size_t) currentPage].side != nullptr)
        {
            const int sideWidth = juce::jlimit (268, 340, area.getWidth() / 4);
            drumPanel.setBounds (area.removeFromRight (sideWidth));
        }
        if (pages[(size_t) currentPage].hasFooter)
            strip.setBounds (area.removeFromBottom (kFooterHeight).reduced (kMargin, 6));
        area = area.reduced (kMargin, 8);
        mixer.setBounds (area);
        kitView.setBounds (area);
        patternPanel.setBounds (area);
        firstRun.setBounds (area);

        // Выпадающая панель под шапкой: лицензия у правого края
        scrim.setBounds (getLocalBounds().withTrimmedTop (kHeaderHeight));
        const int width = juce::jmin (kOverlayWidth, getWidth() - 2 * kMargin);
        if (license != nullptr)
            license->setBounds (getWidth() - kMargin - width, kHeaderHeight + 6, width, license->isOk() ? 130 : 180);
        settingsPanel.setBounds (getWidth() - kMargin - width, kHeaderHeight + 6, width, juce::jmin (SettingsPanel::preferredHeight(), getHeight() - kHeaderHeight - 24));

        if (trackSize)
        {
            auto ui = processor.getUiState();
            ui.width = getWidth();
            ui.height = getHeight();
            processor.setUiState (ui);
        }
    }

    void SamplitEditor::paint (juce::Graphics& g)
    {
        g.fillAll (brand::bg);

        // Шапка и строка статуса на уровень светлее фона, разделены тонкой линией; при загрузке тонкая полоска хода
        g.setColour (brand::surface);
        g.fillRect (headerArea);
        g.fillRect (0, getHeight() - kStatusHeight, getWidth(), kStatusHeight);
        g.setColour (brand::line);
        g.fillRect (headerArea.getX(), headerArea.getBottom() - 1, headerArea.getWidth(), 1);
        g.fillRect (0, getHeight() - kStatusHeight, getWidth(), 1);
        if (shownProgress >= 0.0f)
        {
            g.setColour (brand::accent);
            g.fillRect (juce::Rectangle<float> (0.0f, (float) kHeaderHeight - 2.0f,
                                                (float) getWidth() * juce::jlimit (0.0f, 1.0f, shownProgress), 2.0f));
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
            g.setColour (license->isOk() ? brand::ok : brand::warn);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ (float) chip.getX() + 16.0f, (float) chip.getCentreY() }));
        }

        for (auto* c : std::initializer_list<juce::Component*> { license.get(), &settingsPanel })
            if (c != nullptr && c->isVisible())
            {
                g.setColour (brand::lineStrong);
                g.drawRoundedRectangle (c->getBounds().toFloat().expanded (0.5f), brand::radiusM, 1.0f);
            }
    }
}
