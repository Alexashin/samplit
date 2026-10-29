#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace samplit
{
    // Список пресетов. У каждого видно, всё ли с его пакетами: установлен ли файл и есть ли лицензия.
    // Клик или Enter загружает пресет, стрелки только двигают выбор (загрузка тяжёлая, на каждый шаг её не запускаем).
    // Под списком: ход загрузки, её итог или ошибка
    class PresetPanel final : public juce::Component,
                              public juce::TooltipClient,
                              private juce::ListBoxModel,
                              private juce::ChangeListener,
                              private juce::Timer
    {
    public:
        explicit PresetPanel (SamplitProcessor&);
        ~PresetPanel() override;

        // Пересканировать папку пресетов и пакеты (после смены лицензии, по кнопке)
        void rescan();

        // Выбран пресет, который запущен на загрузку (окно закрывает выпадающий список)
        std::function<void()> onPicked;

        // Соседний пресет в списке по кругу: стрелки в шапке окна
        void step (int delta);

        void paint (juce::Graphics&) override;

        // Над строкой статуса: её полный текст (в узком окне он обрезается)
        juce::String getTooltip() override;
        void resized() override;

    private:
        int getNumRows() override { return (int) presets.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
                showRowMenu (row);
            else
                load (row);
        }
        void showRowMenu (int row);
        void returnKeyPressed (int row) override { load (row); }
        juce::String getTooltipForRow (int row) override;

        void load (int row);
        void updateStatus();
        void updateStatus (const StreamHealth& health);
        void changeListenerCallback (juce::ChangeBroadcaster*) override { updateStatus(); list.repaint(); }
        void timerCallback() override;

        SamplitProcessor& processor;
        std::vector<PresetEntry> presets;
        juce::String loadingName;

        juce::Label title;
        juce::TextButton rescanButton, folderButton, packFolderButton, addPackButton, saveButton, libraryButton;
        std::unique_ptr<juce::FileChooser> chooser;
        juce::Label intro;
        void saveAs();
        void addPack();
        void chooseLibrary();
        void updateIntro();
        juce::ListBox list;
        juce::Label emptyText;
        juce::Rectangle<int> statusArea;
        juce::String statusText;
        bool statusWarns = false;
        float shownProgress = -1.0f;
        StreamHealth shownHealth;
        static juce::String healthWarning (const StreamHealth& health);

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetPanel)
    };
}
