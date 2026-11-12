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
        : AudioProcessorEditor (&p), processor (p), headerBar (p), mixer (p), instruments (p), kitView (p), keyboard (p)
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
        licenseButton.onClick = [this] { showOverlay (overlay == Overlay::License ? Overlay::None : Overlay::License); };

        scrim.onClick = [this] { showOverlay (Overlay::None); };

        kitTab.setButtonText (utf8 ("Установка"));
        kitTab.setTooltip (utf8 ("Схема барабанов: клик по барабану играет его и открывает его настройки"));
        instrumentsTab.setButtonText (utf8 ("Инструменты"));
        instrumentsTab.setTooltip (utf8 ("Для каждого барабана: включить, выбрать звук, громкость и как выбирается удар"));
        mixerTab.setButtonText (utf8 ("Микшер"));
        mixerTab.setTooltip (utf8 ("Уровень, панорама, выход, Mute и Solo каждого микрофона"));
        for (auto* t : { &kitTab, &instrumentsTab, &mixerTab })
        {
            // Состояние вкладок задаёт только showMainTab: радио-группа и автопереключение оставляли подсвеченными две
            // Активна только вкладка с toggle-состоянием: клавиатурный фокус не должен рисовать вторую обводку
            t->setMouseClickGrabsKeyboardFocus (false);
            t->setWantsKeyboardFocus (false);
        }
        kitTab.onClick = [this] { showMainTab (MainTab::Kit); };
        instrumentsTab.onClick = [this] { showMainTab (MainTab::Instruments); };
        mixerTab.onClick = [this] { showMainTab (MainTab::Mixer); };
        kitView.onOpenInstruments = [safe = juce::Component::SafePointer<SamplitEditor> (this)] (const juce::String& placeId)
        {
            if (safe == nullptr)
                return;
            safe->instruments.selectPlace (placeId);
            safe->showMainTab (MainTab::Instruments);
        };

        // Порядок добавления = порядок наложения: подложка и выпадающие панели поверх всего
        for (auto* c : std::initializer_list<juce::Component*> { &mixer, &instruments, &kitView, &kitTab, &instrumentsTab, &mixerTab, &keyboard, &headerBar,
                                                                  &licenseButton, &scrim })
            addChildComponent (c);
        if (license != nullptr)
            addChildComponent (license.get());
        for (auto* c : std::initializer_list<juce::Component*> { &kitTab, &instrumentsTab, &mixerTab, &keyboard, &headerBar, &licenseButton })
            c->setVisible (true);
        showMainTab (MainTab::Kit);
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

    void SamplitEditor::showOverlay (Overlay which)
    {
        overlay = which;
        scrim.setVisible (which != Overlay::None);
        if (license != nullptr)
            license->setVisible (which == Overlay::License);
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
        kitTab.setToggleState (tab == MainTab::Kit, juce::dontSendNotification);
        instrumentsTab.setToggleState (tab == MainTab::Instruments, juce::dontSendNotification);
        mixerTab.setToggleState (tab == MainTab::Mixer, juce::dontSendNotification);
        mixer.setVisible (tab == MainTab::Mixer);
        instruments.setVisible (tab == MainTab::Instruments);
        kitView.setVisible (tab == MainTab::Kit);
    }

    void SamplitEditor::resized()
    {
        auto bounds = getLocalBounds();
        headerArea = bounds.removeFromTop (kHeaderHeight);

        // Шапка: знак слева, лицензия справа, между ними полоса библиотеки и стандартной установки
        auto bar = headerArea.reduced (kMargin, 0);
        logoArea = bar.removeFromLeft (150).withSizeKeepingCentre (150, 30);
        if (license != nullptr)
        {
            licenseButton.setBounds (bar.removeFromRight (230).withSizeKeepingCentre (230, 34));
            bar.removeFromRight (16);
        }
        bar.removeFromLeft (16);
        headerBar.setBounds (bar.withSizeKeepingCentre (bar.getWidth(), 38));

        auto area = bounds.reduced (kMargin, 0);
        area.removeFromTop (14);
        area.removeFromBottom (kMargin);

        keyboard.setBounds (area.removeFromBottom (juce::jlimit (130, 180, area.getHeight() / 4)));
        area.removeFromBottom (14);

        auto tabs = area.removeFromTop (30);
        kitTab.setBounds (tabs.removeFromLeft (130));
        tabs.removeFromLeft (6);
        instrumentsTab.setBounds (tabs.removeFromLeft (130));
        tabs.removeFromLeft (6);
        mixerTab.setBounds (tabs.removeFromLeft (130));
        area.removeFromTop (8);
        mixer.setBounds (area);
        instruments.setBounds (area);
        kitView.setBounds (area);

        // Выпадающая панель под шапкой: лицензия у правого края
        scrim.setBounds (getLocalBounds().withTrimmedTop (kHeaderHeight));
        const int width = juce::jmin (kOverlayWidth, getWidth() - 2 * kMargin);
        if (license != nullptr)
            license->setBounds (getWidth() - kMargin - width, kHeaderHeight + 6, width, license->isOk() ? 130 : 180);
    }

    void SamplitEditor::paint (juce::Graphics& g)
    {
        g.fillAll (brand::bg);

        // Шапка чуть светлее фона; под ней линия, а при загрузке ещё и полоска хода загрузки
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

        for (auto* c : std::initializer_list<juce::Component*> { license.get() })
            if (c != nullptr && c->isVisible())
            {
                g.setColour (brand::line);
                g.drawRoundedRectangle (c->getBounds().toFloat().expanded (0.5f), 12.5f, 1.0f);
            }
    }
}
