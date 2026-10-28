#include "LicensePanel.h"
#include "Brand.h"

namespace samplit
{
    namespace
    {
        const juce::Colour kWarning { 0xffffb454 };
        const juce::Colour kOk { 0xff6fd6a1 };

        juce::String dateText (int64_t unixSeconds)
        {
            return juce::Time (unixSeconds * 1000).formatted ("%d.%m.%Y");
        }
    }

    LicensePanel::LicensePanel (LicenseManager& l) : license (l)
    {
        title.setText (utf8 ("Лицензия"), juce::dontSendNotification);
        title.setFont (juce::FontOptions (15.0f, juce::Font::bold));

        renewButton.setButtonText (utf8 ("Обновить"));
        renewButton.setTooltip (utf8 ("Продлить лицензию через интернет. Без сети плагин работает до окончания срока"));
        renewButton.onClick = [this] { license.renew(); refresh(); };

        code.setTextToShowWhenEmpty (utf8 ("Код лицензии"), brand::textMuted);
        code.setTitle (utf8 ("Код лицензии"));
        code.setIndents (8, 6);
        code.onReturnKey = [this] { activateButton.triggerClick(); };

        activateButton.setButtonText (utf8 ("Активировать"));
        activateButton.onClick = [this]
        {
            const auto text = code.getText().trim();
            if (text.isEmpty())
            {
                code.grabKeyboardFocus();
                return;
            }
           #if SAMPLIT_OFFLINE_LICENSE
            license.importTokenText (text);
           #else
            license.activate (text);
           #endif
            refresh();
        };

       #if SAMPLIT_OFFLINE_LICENSE
        offline = true;
        code.setTextToShowWhenEmpty (utf8 ("Ключ лицензии"), brand::textMuted);
        code.setTitle (utf8 ("Ключ лицензии"));
       #endif

        copyCodeButton.setButtonText (utf8 ("Скопировать код"));
        copyCodeButton.setTooltip (utf8 ("Отправьте этот код владельцу лицензий: по нему выпускают файл лицензии для этого компьютера"));
        copyCodeButton.onClick = [this]
        {
            juce::SystemClipboard::copyTextToClipboard (license.getMachineCode());
            copyCodeButton.setButtonText (utf8 ("Код скопирован"));
            juce::Timer::callAfterDelay (2000, [safe = juce::Component::SafePointer<LicensePanel> (this)]
            {
                if (safe != nullptr)
                    safe->copyCodeButton.setButtonText (utf8 ("Скопировать код"));
            });
        };

        loadFileButton.setButtonText (utf8 ("Выбрать файл"));
        loadFileButton.setTooltip (utf8 ("Если вместо ключа прислали файл license.token"));
        loadFileButton.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> (utf8 ("Файл лицензии Samplit"),
                                                           juce::File::getSpecialLocation (juce::File::userHomeDirectory),
                                                           "*.token;*.json");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      const auto file = fc.getResult();
                                      if (file.existsAsFile())
                                      {
                                          license.importTokenFile (file);
                                          refresh();
                                      }
                                  });
        };

        for (auto* c : std::initializer_list<juce::Component*> { &title, &renewButton, &code, &activateButton,
                                                                 &copyCodeButton, &loadFileButton })
            addChildComponent (c);
        title.setVisible (true);

        refresh();
        startTimerHz (4);
    }

    void LicensePanel::refresh()
    {
        using license::Status;
        info = license.getInfo();
        ok = info.isValid();
        busyShown = info.busy;

        switch (info.status)
        {
            case Status::Valid:
                statusText = info.expires == 0 ? utf8 ("Бессрочная лицензия") : utf8 ("Действует до ") + dateText (info.expires);
                if (info.user.isNotEmpty())
                    statusText << utf8 ("  ·  ") << info.user;
                hintText = {};
                break;
            case Status::Expired:
                statusText = utf8 ("Срок лицензии истёк ") + dateText (info.expires);
                hintText = utf8 ("Подключитесь к интернету и нажмите «Обновить».");
                break;
            case Status::NoToken:
                statusText = utf8 ("Лицензии нет, пакеты закрыты");
                hintText = offline ? utf8 ("Вставьте ключ лицензии и нажмите «Активировать».")
                                   : utf8 ("Введите код лицензии и нажмите «Активировать».");
                break;
            case Status::WrongMachine:
                statusText = utf8 ("Лицензия выписана на другой компьютер");
                hintText = offline ? utf8 ("Этот ключ выписан на другой компьютер. Нужен другой ключ.")
                                   : utf8 ("Активируйте её на этом компьютере своим кодом.");
                break;
            case Status::ClockRollback:
                statusText = utf8 ("Часы компьютера отстают от последнего запуска");
                hintText = utf8 ("Проверьте дату и время, затем нажмите «Обновить».");
                break;
            case Status::NotYetValid:
                statusText = utf8 ("Лицензия ещё не началась");
                hintText = utf8 ("Проверьте дату и время на компьютере.");
                break;
            case Status::BadSignature:
            case Status::Malformed:
                statusText = utf8 ("Ключ лицензии повреждён");
                hintText = offline ? utf8 ("Введите ключ заново.") : utf8 ("Активируйте лицензию заново своим кодом.");
                break;
        }

        if (info.busy)
            hintText = utf8 ("Связь с сервером лицензий…");
        else if (info.lastError.isNotEmpty())
            hintText = info.lastError + (ok ? utf8 (". Плагин работает до окончания срока.") : juce::String());

        const bool perpetual = ok && info.expires == 0;
        renewButton.setEnabled (! info.busy);
        renewButton.setVisible (! perpetual);
        activateButton.setEnabled (! info.busy);
        code.setVisible (! ok);
        activateButton.setVisible (! ok);
        copyCodeButton.setVisible (false);
        loadFileButton.setVisible (! ok && offline);
        loadFileButton.setEnabled (! info.busy);
        resized();
        repaint();
    }

    void LicensePanel::timerCallback()
    {
        if (license.getInfo().busy != busyShown)
            refresh();
    }

    void LicensePanel::paint (juce::Graphics& g)
    {
        g.setColour (brand::surface);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);

        auto area = textArea;
        auto line = area.removeFromTop (20);

        // Точка дублирует текст статуса, сама по себе ничего не значит
        g.setColour (ok ? kOk : kWarning);
        g.fillEllipse (line.removeFromLeft (10).withSizeKeepingCentre (8, 8).toFloat());
        line.removeFromLeft (6);
        g.setColour (brand::text);
        g.setFont (juce::FontOptions (13.0f));
        g.drawText (statusText, line, juce::Justification::centredLeft, true);

        if (hintText.isNotEmpty())
        {
            g.setColour (ok ? brand::textMuted : kWarning);
            g.setFont (juce::FontOptions (12.0f));
            g.drawFittedText (hintText, area.withTrimmedTop (2), juce::Justification::topLeft, 2, 1.0f);
        }
    }

    void LicensePanel::resized()
    {
        auto area = getLocalBounds().reduced (14, 10);
        auto header = area.removeFromTop (28);
        renewButton.setBounds (header.removeFromRight (100).reduced (0, 2));
        title.setBounds (header);

        area.removeFromTop (6);
        if (! ok)
        {
            auto row = area.removeFromBottom (30);
            if (offline)
            {
                loadFileButton.setBounds (row.removeFromRight (110));
                row.removeFromRight (6);
            }
            activateButton.setBounds (row.removeFromRight (120));
            row.removeFromRight (6);
            code.setBounds (row);
            area.removeFromBottom (6);
        }
        textArea = area;
    }
}
