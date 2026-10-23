#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace samplit
{
    // Браузер сэмплов: выбор папки, фоновое сканирование, поиск по имени, список с прослушиванием и волна.
    // Из списка сэмпл перетаскивается на клавишу, Enter кладёт его на выбранную клавишу.
    class SampleBrowser final : public juce::Component,
                                private juce::ListBoxModel,
                                private juce::ChangeListener
    {
    public:
        explicit SampleBrowser (SamplitProcessor&);
        ~SampleBrowser() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        std::function<void (const juce::File&)> onPutOnKey;

    private:
        class Scanner;

        void chooseFolder();
        void scanFolder (const juce::File&);
        void scanFinished (const juce::Array<juce::File>&);
        void applyFilter();
        void showSelected (int row, bool forcePreview);
        void updateStatus();

        int getNumRows() override { return visible.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void selectedRowsChanged (int lastRowSelected) override;
        void listBoxItemClicked (int row, const juce::MouseEvent&) override;
        void returnKeyPressed (int lastRowSelected) override;
        juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;

        void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint (waveArea); }

        SamplitProcessor& processor;

        juce::TextButton folderButton;
        juce::Label folderLabel;
        juce::TextEditor search;
        juce::ListBox list { "Samples", this };
        juce::Label status;
        std::unique_ptr<juce::FileChooser> chooser;

        juce::File root;
        juce::Array<juce::File> allFiles;
        juce::Array<int> visible;
        bool scanning = false;
        int lastPreviewedRow = -1;
        juce::uint32 lastPreviewTime = 0;

        juce::AudioThumbnailCache thumbCache { 8 };
        juce::AudioThumbnail thumbnail;
        juce::File shownFile;
        juce::Rectangle<int> waveArea;

        std::unique_ptr<Scanner> scanner;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleBrowser)
    };
}
