#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace samplit
{
    // Сетка 4×2 пэдов. Клик играет пэд, правый клик открывает меню.
    // Сэмпл назначается перетаскиванием из браузера или из проводника. Несколько файлов ложатся на пэды подряд.
    class PadGrid final : public juce::Component,
                          public juce::DragAndDropTarget,
                          public juce::FileDragAndDropTarget,
                          private juce::ChangeListener,
                          private juce::Timer
    {
    public:
        explicit PadGrid (SamplitProcessor&);
        ~PadGrid() override;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

        // Первый пустой пэд или -1
        int firstFreePad() const;

        // Выбранный пэд показывает свои настройки в панели под сеткой
        int getSelectedPad() const noexcept { return selectedPad; }
        std::function<void (int)> onSelect;

        // Высота сетки с подсказкой при заданной ширине: пэды квадратные
        static int preferredHeight (int width);

        bool isInterestedInDragSource (const SourceDetails&) override;
        void itemDragEnter (const SourceDetails& d) override { setHover (padAt (d.localPosition)); }
        void itemDragMove (const SourceDetails& d) override { setHover (padAt (d.localPosition)); }
        void itemDragExit (const SourceDetails&) override { setHover (-1); }
        void itemDropped (const SourceDetails&) override;

        bool isInterestedInFileDrag (const juce::StringArray&) override;
        void fileDragMove (const juce::StringArray&, int x, int y) override { setHover (padAt ({ x, y })); }
        void fileDragExit (const juce::StringArray&) override { setHover (-1); }
        void filesDropped (const juce::StringArray&, int x, int y) override;

    private:
        juce::Rectangle<float> padBounds (int pad) const;
        int padAt (juce::Point<int>) const;
        void setHover (int pad);
        void paintPad (juce::Graphics&, int pad);

        void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
        void timerCallback() override;

        SamplitProcessor& processor;
        uint32_t shownActive = 0, shownSeparate = 0;
        int hoverPad = -1;
        int selectedPad = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PadGrid)
    };
}
