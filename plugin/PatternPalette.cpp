#include "PatternPalette.h"

#include "samplit/core/Manifest.h"

#include <algorithm>

namespace samplit
{
    namespace
    {
        juce::String strengthLabel (int velocity)
        {
            if (velocity < 43)
                return juce::String::fromUTF8 ("тихо");
            if (velocity < 86)
                return juce::String::fromUTF8 ("средне");
            return juce::String::fromUTF8 ("сильно");
        }
    }

    std::vector<StrengthOption> buildPalette (const Kit& kit, int slot, const pack::Piece* meta)
    {
        std::vector<StrengthOption> out;

        int keyLo = 0, keyHi = 127;
        for (const auto& def : kStudio600Places)
            if (def.slot == slot)
            {
                keyLo = def.keyLo;
                keyHi = def.keyHi;
            }

        for (int key = keyLo; key <= keyHi; ++key)
        {
            const auto& map = kit.notes[(size_t) key];
            if (map.piece < 0 || kit.pieces[(size_t) map.piece].slot != slot)
                continue;
            const auto& piece = kit.pieces[(size_t) map.piece];

            for (const auto& ref : map.layers)
            {
                const auto& layer = piece.layers[(size_t) ref.layer];

                juce::String name;
                juce::String groupId;
                if (ref.choice >= 0 && ref.choice < (int) kit.choices.size())
                {
                    const auto& choice = kit.choices[(size_t) ref.choice];
                    name = juce::String::fromUTF8 (choice.name.c_str());
                    const auto id = juce::String::fromUTF8 (choice.id.c_str());
                    groupId = id.fromFirstOccurrenceOf (":", false, false);
                }
                if (name.isEmpty())
                    name = layer.label.empty() ? juce::String (layer.id) : juce::String::fromUTF8 (layer.label.c_str());

                const pack::Group* group = nullptr;
                if (meta != nullptr && groupId.isNotEmpty())
                    for (const auto& g : meta->groups)
                        if (g.id == groupId)
                            group = &g;

                auto make = [&] (int velocity, bool exact, const juce::String& n)
                {
                    StrengthOption o;
                    o.key = key;
                    o.groupId = groupId;
                    o.name = n;
                    o.role = group != nullptr ? group->role : juce::String ("other");
                    o.velocity = juce::jlimit (1, 127, velocity);
                    o.label = strengthLabel (o.velocity);
                    o.exact = exact;
                    if (group != nullptr)
                    {
                        o.pairType = group->pair.type;
                        o.pairPlace = group->pair.place;
                        o.pairGroup = group->pair.group;
                    }
                    out.push_back (std::move (o));
                };

                if (ref.pick == Pick::Exact && ! ref.hits.empty())
                {
                    int n = 0;
                    for (int h : ref.hits)
                    {
                        ++n;
                        const auto& hit = piece.hits[(size_t) h];
                        make (hit.velLo, true, ref.hits.size() > 1 ? name + " " + juce::String (n) : name);
                    }
                }
                else
                    make ((ref.velLo + ref.velHi) / 2, false, name);
            }
        }

        std::stable_sort (out.begin(), out.end(), [] (const StrengthOption& a, const StrengthOption& b)
                          { return a.key != b.key ? a.key < b.key : a.velocity < b.velocity; });
        return out;
    }
}
