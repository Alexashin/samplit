#include "PatternPalette.h"

#include "samplit/core/Manifest.h"

#include <algorithm>
#include <cstdlib>

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

    bool findPieceMeta (const std::vector<CatalogPack>& catalog, const PlaceDef& def, const PlaceSetup& ps, pack::Piece& out)
    {
        for (const auto& pk : catalog)
            if (pk.packId == juce::String (ps.packId))
                for (const auto& pc : pk.pieces)
                    if (ps.pieceId.empty() ? pc.type == juce::String (def.type) : pc.id == juce::String (ps.pieceId))
                    {
                        out = {};
                        out.id = pc.id;
                        out.name = pc.name;
                        out.type = pc.type;
                        out.groups = pc.groups;
                        out.keys = pc.keys;
                        return true;
                    }
        return false;
    }

    int findOption (const std::vector<StrengthOption>& options, int key, int velocity)
    {
        int best = -1;
        for (int i = 0; i < (int) options.size(); ++i)
        {
            const auto& o = options[(size_t) i];
            if (o.key != key)
                continue;
            if (o.velocity == velocity)
                return i;
            if (best < 0 || std::abs (o.velocity - velocity) < std::abs (options[(size_t) best].velocity - velocity))
                best = i;
        }
        return best;
    }

    int pairOption (const std::vector<StrengthOption>& partner, const StrengthOption& o)
    {
        if (o.pairGroup.isEmpty())
            return -1;
        int best = -1;
        for (int i = 0; i < (int) partner.size(); ++i)
        {
            const auto& p = partner[(size_t) i];
            if (p.groupId != o.pairGroup)
                continue;
            if (p.velocity == o.velocity)
                return i;
            if (best < 0 || std::abs (p.velocity - o.velocity) < std::abs (partner[(size_t) best].velocity - o.velocity))
                best = i;
        }
        return best;
    }

    bool canPair (const PlacePalette& partner, const StrengthOption& o)
    {
        if (o.pairGroup.isEmpty())
            return false;
        const bool fits = o.pairPlace.isNotEmpty() ? partner.placeId == o.pairPlace : partner.type == o.pairType;
        return fits && pairOption (partner.options, o) >= 0;
    }

    bool setPairedStep (Pattern& p, int step, const PlacePalette& own, const StrengthOption& o, const PlacePalette* partner)
    {
        if (! p.setStep (step, own.slot, o.key, o.velocity))
            return false;
        if (partner != nullptr && partner->slot != own.slot && canPair (*partner, o))
        {
            const auto& po = partner->options[(size_t) pairOption (partner->options, o)];
            p.setStep (step, partner->slot, po.key, po.velocity);
        }
        return true;
    }

    void removePairedStep (Pattern& p, int step, const PlacePalette& own, const PlacePalette* partner)
    {
        const auto* st = p.findStep (step, own.slot);
        if (st == nullptr)
            return;
        const int opt = findOption (own.options, st->key, st->velocity);
        p.removeStep (step, own.slot);
        if (opt < 0 || partner == nullptr || partner->slot == own.slot)
            return;
        const auto& o = own.options[(size_t) opt];
        if (! canPair (*partner, o))
            return;
        const auto& po = partner->options[(size_t) pairOption (partner->options, o)];
        if (const auto* other = p.findStep (step, partner->slot); other != nullptr && other->key == po.key && other->velocity == po.velocity)
            p.removeStep (step, partner->slot);
    }
}
