#pragma once

#include "PatternPalette.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace samplit
{
    // Вкладка «Партия» (docs/phrase-editor-spec.md): строки = включённые места установки, столбцы = шаги 1/16 (1–4 такта).
    // Левый клик ставит/снимает удар с силой строки, правый выбирает силу словами. Такты, темп, Play/Stop и «Цикл» сверху.
    // Все правки идут через processor.setPattern(); проигрывание делает PatternPlayer процессора
    class PatternPanel final : public juce::Component,
                               private juce::ChangeListener,
                               private juce::Timer
    {
    public:
        explicit PatternPanel (SamplitProcessor&);
        ~PatternPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override;

    private:
        struct Row : PlacePalette
        {
            juce::String label;
            int current = -1;           // индекс выбранной силы в options (-1: палитра пуста)
            bool together = false;      // «вместе»: парный удар ставится и снимается вместе с этим
            int partnerSlot = -1;       // выбранный партнёр пары; -1: первое подходящее место
        };
        struct Grid;
        struct Labels;

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;
        void refresh();
        void rebuildRows();
        void updateControls();
        void layoutGrid();
        void edit (std::function<void (Pattern&)>);
        void toggleStep (int row, int step);
        void placeStep (const Row&, int step, StrengthOption);     // с парой, если у строки «вместе»
        void removeStep (const Row&, int step);
        std::optional<PlacePalette> stepPartner (const Row&, int step) const;
        void showStrengthMenu (int row, int step);      // step < 0: только сила строки
        void setBars (int bars);
        int optionFor (const Row&, const PatternStep&) const;   // индекс в палитре или -1
        const Row* partnerFor (const Row&, const StrengthOption&) const;    // партнёр пары для варианта или nullptr
        juce::String pairHint (const Row&) const;                           // подсказка переключателя «вместе»
        int playStep() const;                           // шаг под курсором, -1 если не играет

        SamplitProcessor& processor;
        Pattern pattern;                                // копия из процессора
        std::vector<Row> rows;
        std::shared_ptr<const Kit> lastKit;
        std::vector<PatternStep> trimmed;               // удары, срезанные уменьшением тактов (вернутся при увеличении)
        juce::String rowsSignature;
        int shownPlayStep = -1;

        juce::Label barsLabel, tempoLabel, timingLabel, velLabel, hint, emptyState;
        juce::TextButton barButtons[Pattern::kMaxBars];
        juce::Slider tempo, humanTiming, humanVel;
        juce::TextButton playButton, loopButton, clearButton, variantButton;
        juce::Component labelsClip;                     // обрезает подписи строк при вертикальной прокрутке
        std::unique_ptr<Labels> labels;
        std::unique_ptr<Grid> grid;
        struct SyncViewport final : juce::Viewport
        {
            void visibleAreaChanged (const juce::Rectangle<int>&) override { if (onScroll) onScroll(); }
            std::function<void()> onScroll;
        } viewport;
        juce::Rectangle<int> gridArea;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PatternPanel)
    };
}
