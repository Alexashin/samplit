#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace samplit
{
    // Схема барабанной установки: клик по барабану выбирает его и играет, справа его настройки
    // (как выбирается удар: случайно, по кругу, по velocity). Тот же барабан можно выбрать из списка
    class KitView final : public juce::Component,
                          private juce::ChangeListener,
                          private juce::Timer
    {
    public:
        explicit KitView (SamplitProcessor&);
        ~KitView() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

    private:
        class ChoiceRow;

        // Барабан на схеме: экранный прямоугольник, тип и подпись
        struct Spot
        {
            int piece = -1;
            juce::Rectangle<float> bounds;
            bool cymbal = false, chip = false;
            int order = 2;                  // порядок рисования: тарелки, бочка, остальное
            juce::String label;
        };

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;

        void rebuild();
        void layoutSpots();
        void selectPiece (int piece);
        void rebuildChoiceRows();
        void playPiece (int piece, int velocity);
        int pieceAt (juce::Point<float>) const;
        int firstKeyOf (int piece) const;
        juce::String keysText (int piece) const;

        SamplitProcessor& processor;
        std::shared_ptr<const Kit> kit;
        std::vector<Spot> spots;
        std::vector<float> flash;               // по барабанам кита: недавний удар, 1 → 0
        std::vector<std::vector<int>> pieceKeys;
        int selected = -1, hovered = -1;

        juce::Rectangle<int> stageArea, detailArea, legendArea;

        juce::Label pieceTitle, pieceName, pieceKeysLabel, choicesTitle, noChoices, emptyText, tryTitle;
        juce::ComboBox pieceBox;
        juce::Slider levelSlider;
        juce::Label levelTitle;
        juce::TextButton trySoft, tryMedium, tryHard;
        std::vector<std::unique_ptr<ChoiceRow>> rows;
        bool updating = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KitView)
    };
}
