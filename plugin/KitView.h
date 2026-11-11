#pragma once

#include "PackCatalog.h"
#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace samplit
{
    // Схема барабанной установки: двенадцать мест Studio600. Клик по месту играет его барабан и открывает меню места:
    // включить или выключить, выбрать звук (пак) и как выбирается удар в группах слоёв. Выключенное место рисуется приглушённым
    class KitView final : public juce::Component,
                          public juce::SettableTooltipClient,
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
        // Место на схеме: экранный прямоугольник, подпись и барабан кита, играющий на этом месте (по Piece::slot)
        struct Spot
        {
            std::string placeId;
            int slot = -1;
            int piece = -1;                 // индекс в Kit::pieces, -1: на месте ничего не играет
            bool enabled = true;
            juce::Rectangle<float> bounds;
            bool cymbal = false;
            int order = 2;                  // порядок рисования: бочка, томы, тарелки
            juce::String label;
        };

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;

        void rebuild();
        void layoutSpots();
        void refreshState();
        int spotAt (juce::Point<float>) const;
        void showPlaceMenu (int spot);

        SamplitProcessor& processor;
        std::shared_ptr<const Kit> kit;
        std::vector<Spot> spots;
        std::vector<float> flash;               // по барабанам кита: недавний удар, 1 → 0
        std::vector<std::vector<int>> pieceKeys;
        int hovered = -1;

        juce::Rectangle<int> stageArea, hintArea;
        juce::Rectangle<float> stool;

        juce::Label emptyText;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KitView)
    };
}
