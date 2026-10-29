#pragma once

#include "PluginProcessor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>

namespace samplit
{
    // Пиано-клавиатура (ноты 21..108 как у фортепиано, C3 = 60). Клик играет клавишу, чем ниже точка клика, тем громче.
    // С китом клавиши барабанов подкрашены по барабану, имя барабана видно в подсказке над клавиатурой.
    // Без кита (устаревший сэмплерный путь) клавиши с зонами подкрашены цветом группы и подписаны числом вариаций.
    // Сюда же роняют файлы и папки: из проводника. Все файлы ложатся на клавишу под курсором
    // в выбранную группу как вариации, с зажатым Alt как слои по velocity.
    class KeyboardView final : public juce::Component,
                               public juce::DragAndDropTarget,
                               public juce::FileDragAndDropTarget,
                               private juce::ChangeListener,
                               private juce::Timer
    {
    public:
        static constexpr int kFirstKey = 21;   // A-1: с неё начинается бочка Studio600
        static constexpr int kLastKey = 108;

        explicit KeyboardView (SamplitProcessor&);
        ~KeyboardView() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;

        void setSelectedKey (int key);
        int getSelectedKey() const noexcept { return selectedKey; }
        void setSelectedGroup (int group);

        std::function<void (int)> onSelectKey;

        bool isInterestedInDragSource (const SourceDetails&) override;
        void itemDragEnter (const SourceDetails& d) override;
        void itemDragMove (const SourceDetails& d) override { setHover (keyAt (d.localPosition)); }
        void itemDragExit (const SourceDetails&) override { endDrag(); }
        void itemDropped (const SourceDetails&) override;

        bool isInterestedInFileDrag (const juce::StringArray&) override;
        void fileDragEnter (const juce::StringArray&, int x, int y) override;
        void fileDragMove (const juce::StringArray&, int x, int y) override { setHover (keyAt ({ x, y })); }
        void fileDragExit (const juce::StringArray&) override { endDrag(); }
        void filesDropped (const juce::StringArray&, int x, int y) override;

    private:
        static bool isBlack (int key) noexcept;

        int keyAt (juce::Point<int>) const;
        void setHover (int key);
        void endDrag();
        void drop (const juce::Array<juce::File>&, int key);
        void select (int key);
        void rebuildKeyInfo();
        juce::String hintText() const;
        juce::String pieceName (int key) const;

        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;

        SamplitProcessor& processor;

        std::array<juce::Rectangle<float>, 128> keyRect {};
        juce::Rectangle<int> hintArea;

        // Что стоит на клавишах: число зон и биты групп. Пересчитывается при смене инструмента, не в paint
        std::shared_ptr<const Instrument> instrument;
        std::shared_ptr<const Kit> kit;             // не nullptr: играет кит, клавиатура показывает барабаны
        std::array<int, 128> pieceAt {};
        std::array<int, 128> zoneCount {};
        std::array<uint16_t, 128> groupBits {};

        std::array<uint64_t, 2> shownActive { 0, 0 };
        int selectedKey = 60;
        int selectedGroup = 0;
        int hoverKey = -1;
        int dragFiles = 0;
        juce::String info;
        juce::uint32 infoSince = 0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KeyboardView)
    };
}
