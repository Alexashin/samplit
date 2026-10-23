#include "SampleBrowser.h"
#include "Brand.h"

#include <algorithm>

namespace samplit
{
    // Обход папки в фоне: на больших коллекциях это секунды, интерфейс за это время не должен замирать
    class SampleBrowser::Scanner final : public juce::Thread
    {
    public:
        Scanner (juce::File folderToScan, juce::Component::SafePointer<SampleBrowser> ownerToNotify)
            : Thread ("Samplit scan"), folder (std::move (folderToScan)), owner (ownerToNotify) {}

        void run() override
        {
            juce::Array<juce::File> found;
            for (const auto& entry : juce::RangedDirectoryIterator (folder, true, SamplitProcessor::getSupportedWildcard(),
                                                                    juce::File::findFiles))
            {
                if (threadShouldExit())
                    return;
                found.add (entry.getFile());
                if (found.size() >= 50000)
                    break;
            }

            std::sort (found.begin(), found.end(), [] (const juce::File& a, const juce::File& b)
            {
                return a.getFullPathName().compareNatural (b.getFullPathName()) < 0;
            });

            juce::MessageManager::callAsync ([owner = owner, found]
            {
                if (auto* browser = owner.getComponent())
                    browser->scanFinished (found);
            });
        }

    private:
        juce::File folder;
        juce::Component::SafePointer<SampleBrowser> owner;
    };

    SampleBrowser::SampleBrowser (SamplitProcessor& p)
        : processor (p), thumbnail (256, p.getFormatManager(), thumbCache)
    {
        folderButton.setButtonText (utf8 ("Выбрать папку…"));
        folderButton.onClick = [this] { chooseFolder(); };
        addAndMakeVisible (folderButton);

        folderLabel.setColour (juce::Label::textColourId, brand::textMuted);
        folderLabel.setFont (juce::FontOptions (12.0f));
        folderLabel.setMinimumHorizontalScale (1.0f);
        addAndMakeVisible (folderLabel);

        search.setTextToShowWhenEmpty (utf8 ("Поиск по имени: kick dark, 808…"), brand::textMuted);
        search.setFont (juce::FontOptions (14.0f));
        search.setIndents (10, 7);
        search.onTextChange = [this] { applyFilter(); };
        search.onEscapeKey = [this] { search.clear(); applyFilter(); };
        addAndMakeVisible (search);

        list.setRowHeight (30);
        list.setOutlineThickness (0);
        list.setMultipleSelectionEnabled (false);
        addAndMakeVisible (list);

        status.setColour (juce::Label::textColourId, brand::textMuted);
        status.setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (status);

        thumbnail.addChangeListener (this);

        if (const auto folder = processor.getLastFolder(); folder.isDirectory())
            scanFolder (folder);
        else
            updateStatus();
    }

    SampleBrowser::~SampleBrowser()
    {
        thumbnail.removeChangeListener (this);
        if (scanner != nullptr)
            scanner->stopThread (3000);
    }

    void SampleBrowser::resized()
    {
        auto area = getLocalBounds().reduced (16);
        auto top = area.removeFromTop (34);
        folderButton.setBounds (top.removeFromLeft (150));
        top.removeFromLeft (10);
        folderLabel.setBounds (top);

        area.removeFromTop (10);
        search.setBounds (area.removeFromTop (34));
        area.removeFromTop (10);

        status.setBounds (area.removeFromBottom (20));
        area.removeFromBottom (6);
        waveArea = area.removeFromBottom (72);
        area.removeFromBottom (10);
        list.setBounds (area);
    }

    void SampleBrowser::paint (juce::Graphics& g)
    {
        g.setColour (brand::surface);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);

        const auto wave = waveArea.toFloat();
        g.setColour (brand::pad);
        g.fillRoundedRectangle (wave, 8.0f);

        if (thumbnail.getTotalLength() > 0.0)
        {
            g.setColour (brand::accent);
            thumbnail.drawChannels (g, waveArea.reduced (8, 6), 0.0, thumbnail.getTotalLength(), 1.0f);

            g.setColour (brand::textMuted);
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (juce::String (thumbnail.getTotalLength(), 2) + " s", waveArea.reduced (8, 4),
                        juce::Justification::bottomRight);
        }
        else
        {
            g.setColour (brand::textMuted);
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (utf8 ("Выберите сэмпл, чтобы послушать"), waveArea, juce::Justification::centred);
        }
    }

    void SampleBrowser::chooseFolder()
    {
        chooser = std::make_unique<juce::FileChooser> (utf8 ("Папка с сэмплами"),
                                                       root.isDirectory() ? root : juce::File::getSpecialLocation (juce::File::userMusicDirectory));
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                              [this] (const juce::FileChooser& fc)
                              {
                                  const auto folder = fc.getResult();
                                  if (folder.isDirectory())
                                  {
                                      processor.setLastFolder (folder);
                                      scanFolder (folder);
                                  }
                              });
    }

    void SampleBrowser::scanFolder (const juce::File& folder)
    {
        if (scanner != nullptr)
            scanner->stopThread (3000);

        root = folder;
        folderLabel.setText (folder.getFullPathName(), juce::dontSendNotification);
        allFiles.clear();
        applyFilter();
        scanning = true;
        updateStatus();

        scanner = std::make_unique<Scanner> (folder, juce::Component::SafePointer<SampleBrowser> (this));
        scanner->startThread();
    }

    void SampleBrowser::scanFinished (const juce::Array<juce::File>& files)
    {
        scanning = false;
        allFiles = files;
        applyFilter();
    }

    void SampleBrowser::applyFilter()
    {
        // Все слова запроса должны встретиться в имени или пути файла
        juce::StringArray words;
        words.addTokens (search.getText().trim(), " ", {});
        words.removeEmptyStrings();

        visible.clearQuick();
        for (int i = 0; i < allFiles.size(); ++i)
        {
            const auto path = allFiles.getReference (i).getRelativePathFrom (root);
            const bool matches = std::all_of (words.begin(), words.end(),
                                              [&] (const juce::String& w) { return path.containsIgnoreCase (w); });
            if (matches)
                visible.add (i);
        }

        lastPreviewedRow = -1;
        list.deselectAllRows();
        list.updateContent();
        list.repaint();
        updateStatus();
    }

    void SampleBrowser::updateStatus()
    {
        juce::String s;
        if (scanning)
            s = utf8 ("Сканирую папку…");
        else if (root == juce::File())
            s = utf8 ("Выберите папку со своими сэмплами");
        else if (allFiles.isEmpty())
            s = utf8 ("В папке нет аудиофайлов (WAV, AIFF, FLAC, OGG)");
        else if (visible.size() == allFiles.size())
            s = utf8 ("Найдено файлов: ") + juce::String (allFiles.size());
        else
            s = utf8 ("Показано ") + juce::String (visible.size()) + utf8 (" из ") + juce::String (allFiles.size());

        status.setText (s + utf8 ("   ·   ↑↓ слушать, Enter или перетащить на клавишу"), juce::dontSendNotification);
    }

    void SampleBrowser::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
    {
        if (! juce::isPositiveAndBelow (row, visible.size()))
            return;

        const auto& file = allFiles.getReference (visible[row]);
        auto area = juce::Rectangle<int> (0, 0, width, height).reduced (6, 1);

        if (selected)
        {
            g.setColour (brand::accentSoft);
            g.fillRoundedRectangle (area.toFloat(), 6.0f);
        }

        area.reduce (8, 0);
        const auto folder = file.getParentDirectory().getRelativePathFrom (root);
        auto right = area.removeFromRight (juce::jmin (140, area.getWidth() / 3));

        g.setColour (brand::text);
        g.setFont (juce::FontOptions (14.0f));
        g.drawText (file.getFileNameWithoutExtension(), area, juce::Justification::centredLeft);

        g.setColour (brand::textMuted);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (folder == "." ? juce::String() : folder, right, juce::Justification::centredRight);
    }

    void SampleBrowser::showSelected (int row, bool forcePreview)
    {
        if (! juce::isPositiveAndBelow (row, visible.size()))
            return;

        const auto& file = allFiles.getReference (visible[row]);
        if (file != shownFile)
        {
            shownFile = file;
            thumbnail.setSource (new juce::FileInputSource (file));
            repaint (waveArea);
        }

        // Клик по строке сначала меняет выделение, потом приходит сам клик: второй раз подряд не запускаем
        const auto now = juce::Time::getMillisecondCounter();
        const bool justPlayed = row == lastPreviewedRow && now - lastPreviewTime < 250;
        if ((forcePreview && ! justPlayed) || row != lastPreviewedRow)
        {
            lastPreviewedRow = row;
            lastPreviewTime = now;
            processor.previewFile (file);
        }
    }

    void SampleBrowser::selectedRowsChanged (int lastRowSelected)
    {
        showSelected (lastRowSelected, false);
    }

    void SampleBrowser::listBoxItemClicked (int row, const juce::MouseEvent&)
    {
        // Повторный клик по той же строке снова играет сэмпл
        showSelected (row, true);
    }

    void SampleBrowser::returnKeyPressed (int lastRowSelected)
    {
        if (juce::isPositiveAndBelow (lastRowSelected, visible.size()) && onPutOnKey)
            onPutOnKey (allFiles.getReference (visible[lastRowSelected]));
    }

    juce::var SampleBrowser::getDragSourceDescription (const juce::SparseSet<int>& rows)
    {
        if (rows.isEmpty() || ! juce::isPositiveAndBelow (rows[0], visible.size()))
            return {};
        return allFiles.getReference (visible[rows[0]]).getFullPathName();
    }
}
