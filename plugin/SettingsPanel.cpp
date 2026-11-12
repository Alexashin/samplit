#include "SettingsPanel.h"

namespace samplit
{
    namespace
    {
        void drawRow (juce::Graphics& g, juce::Rectangle<int> row, const juce::String& title, const juce::String& hint)
        {
            g.setColour (brand::text);
            g.setFont (fonts::strong (13.0f));
            g.drawText (title, row.removeFromTop (18), juce::Justification::centredLeft, true);
            g.setColour (brand::textMuted);
            g.setFont (fonts::text (12.0f));
            g.drawFittedText (hint, row.removeFromTop (30), juce::Justification::topLeft, 2, 1.0f);
        }
    }

    SettingsPanel::SettingsPanel (SamplitProcessor& p) : processor (p)
    {
        strength.onChange = [this] (int i) { processor.setPartMode (i == 1); refresh(); };
        pick.onChange = [this] (int i) { processor.setRepeatable (i == 0); };
        windowSize.onChange = [this] (int i) { if (onWindowSize) onWindowSize (i); };
        windowSize.setSelected (1);
        folderButton.setButtonText (utf8 ("Изменить…"));
        folderButton.onClick = [this] { if (onChooseFolder) onChooseFolder(); };
        rescanButton.setButtonText (utf8 ("Обновить список звуков"));
        rescanButton.onClick = [this] { if (onRescan) onRescan(); };
        for (auto* c : std::initializer_list<juce::Component*> { &strength, &pick, &windowSize, &folderButton, &rescanButton })
            addAndMakeVisible (c);
        refresh();
    }

    void SettingsPanel::refresh()
    {
        strength.setSelected (processor.isPartMode() ? 1 : 0);
        pick.setSelected (processor.isRepeatable() ? 0 : 1);
        pick.setEnabled (! processor.isPartMode());
        repaint();
    }

    void SettingsPanel::paint (juce::Graphics& g)
    {
        g.fillAll (brand::surface);
        drawSectionCaption (g, utf8 ("Настройки"), getLocalBounds().reduced (16, 0).removeFromTop (40));
        drawRow (g, rowArea[0], utf8 ("Что значит velocity"),
                 utf8 ("Сила: запись удара выбирает плагин. Номер записи: velocity указывает конкретную запись, как в Kontakt, нужно для партий под Kontakt."));
        drawRow (g, rowArea[1], utf8 ("Какая запись играет"),
                 utf8 ("Повторяемо: одно место песни всегда даёт одну запись, прослушивание и экспорт совпадают. Живой выбор: каждый раз по-новому."));
        drawRow (g, rowArea[2], utf8 ("Размер окна"), utf8 ("Окно не больше рабочей области экрана. Размер запоминается в проекте."));
        drawRow (g, rowArea[3], utf8 ("Папка со звуком"), processor.getLicense().getLibraryRoot().getFullPathName().isEmpty()
                                                          ? utf8 ("Не выбрана") : processor.getLicense().getLibraryRoot().getFullPathName());
        drawRow (g, rowArea[4], utf8 ("Служебное"), utf8 ("Перечитать манифесты и папку звука, если вы добавили файлы вручную."));
    }

    void SettingsPanel::resized()
    {
        auto r = getLocalBounds().reduced (16, 0);
        r.removeFromTop (44);
        auto take = [&r] (int controlWidth, juce::Component& c, juce::Rectangle<int>& row)
        {
            row = r.removeFromTop (48);
            auto ctl = r.removeFromTop (30);
            c.setBounds (ctl.removeFromLeft (controlWidth));
            r.removeFromTop (10);
        };
        take (juce::jmax (strength.preferredWidth(), 200), strength, rowArea[0]);
        take (juce::jmax (pick.preferredWidth(), 200), pick, rowArea[1]);
        take (windowSize.preferredWidth(), windowSize, rowArea[2]);
        take (110, folderButton, rowArea[3]);
        take (190, rescanButton, rowArea[4]);
    }
}
