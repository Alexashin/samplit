#include "PresetLibrary.h"

#include <algorithm>
#include <set>

namespace samplit
{
    bool PresetEntry::isPlayable() const noexcept
    {
        return std::any_of (packs.begin(), packs.end(), [] (const auto& p) { return p.second == PackState::Ready; });
    }

    bool PresetEntry::isComplete() const noexcept
    {
        return ! packs.empty() && std::all_of (packs.begin(), packs.end(), [] (const auto& p) { return p.second == PackState::Ready; });
    }

    std::vector<Mix> scanMixes (const juce::File& mixDir)
    {
        auto files = mixDir.findChildFiles (juce::File::findFiles, false, "*.json");
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileName().compareNatural (b.getFileName()) < 0; });

        std::vector<Mix> result;
        for (const auto& f : files)
        {
            Mix m;
            juce::String error;
            if (Mix::fromFile (f, m, error))
            {
                m.id = f.getFileNameWithoutExtension();     // id = имя файла: так микс находят и сохраняют в проекте
                result.push_back (std::move (m));
            }
        }
        return result;
    }

    std::vector<PresetEntry> scanPresets (const juce::File& presetDir, const juce::File& packDir,
                                          const std::function<bool (const juce::String&, pack::PackKey&)>& keyFor)
    {
        std::set<juce::String> installed;
        for (const auto& f : packDir.findChildFiles (juce::File::findFiles, false, "*.splpack"))
        {
            juce::String error;
            if (auto r = pack::PackReader::open (f, error))
                installed.insert (r->getHeader().packId);
        }

        auto files = presetDir.findChildFiles (juce::File::findFiles, false, "*.json");
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getFileName().compareNatural (b.getFileName()) < 0; });

        std::vector<PresetEntry> result;
        for (const auto& f : files)
        {
            PresetEntry e;
            juce::String error;
            if (! Preset::fromFile (f, e.preset, error))
                continue;
            e.file = f;
            for (const auto& id : e.preset.packs)
            {
                pack::PackKey key;
                auto state = PackState::NotInstalled;
                if (installed.count (id) > 0)
                    state = keyFor && keyFor (id, key) ? PackState::Ready : PackState::NoLicense;
                e.packs.emplace_back (id, state);
            }
            result.push_back (std::move (e));
        }
        return result;
    }
}
