#include "PackCatalog.h"

#include <algorithm>

namespace samplit
{
    using pack::operator""_s;

    namespace
    {
        bool layerUnverified (const pack::Piece& piece, const juce::String& layerId)
        {
            for (const auto& l : piece.layers)
                if (l.id == layerId)
                    return (bool) l.extra.getWithDefault ("unverified", false);
            return false;
        }

        CatalogPack toCatalog (const pack::Manifest& m, bool includeUnverified)
        {
            CatalogPack cp;
            cp.packId = m.id;
            cp.title = m.title.isNotEmpty() ? m.title : m.id;
            for (const auto& p : m.pieces)
            {
                CatalogPiece piece;
                piece.id = p.id;
                piece.type = p.type;
                piece.keys = p.keys;
                piece.name = p.name.isNotEmpty() ? p.name : p.id;
                for (const auto& g : p.groups)
                {
                    const bool allUnverified = ! g.layers.empty()
                        && std::all_of (g.layers.begin(), g.layers.end(), [&] (const juce::String& id) { return layerUnverified (p, id); });
                    if (includeUnverified || ! allUnverified)
                        piece.groups.push_back (g);
                }
                cp.pieces.push_back (std::move (piece));
            }
            return cp;
        }
    }

    std::vector<CatalogPack> scanPackCatalog (const KitLoadOptions& options, bool includeUnverified)
    {
        std::vector<CatalogPack> out;
        auto add = [&] (const pack::Manifest& m)
        {
            if (m.id.isEmpty() || (! includeUnverified && m.id == "studio600-unverified"))
                return;
            for (const auto& c : out)
                if (c.packId == m.id)
                    return;
            out.push_back (toCatalog (m, includeUnverified));
        };

        if (options.libraryRoot != juce::File())
        {
            if (options.manifestDir.isDirectory())
                for (const auto& f : options.manifestDir.findChildFiles (juce::File::findFiles, false, "*.json"))
                {
                    pack::Manifest m;
                    juce::String err;
                    if (pack::Manifest::fromJson (f.loadFileAsString(), m, err))
                        add (m);
                }
        }
        else if (! options.requireLibrary && options.keyFor && options.packDir.isDirectory())
        {
            for (const auto& f : options.packDir.findChildFiles (juce::File::findFiles, false, "*.splpack"))
            {
                juce::String err;
                auto reader = pack::PackReader::open (f, err);
                pack::PackKey key {};
                pack::Manifest m;
                if (reader != nullptr && options.keyFor (reader->getHeader().packId, key) && reader->readManifest (key, m, err))
                    add (m);
            }
        }

        std::sort (out.begin(), out.end(), [] (const CatalogPack& a, const CatalogPack& b) { return a.packId < b.packId; });
        return out;
    }

    std::vector<CatalogPack> packsForPlace (const std::vector<CatalogPack>& catalog, const PlaceDef& place)
    {
        std::vector<CatalogPack> out;
        for (const auto& pk : catalog)
        {
            CatalogPack c;
            c.packId = pk.packId;
            c.title = pk.title;
            for (const auto& p : pk.pieces)
                if (p.type == place.type)
                    c.pieces.push_back (p);
            if (! c.pieces.empty())
                out.push_back (std::move (c));
        }
        return out;
    }

    KitSpec makeKitSpec (const Setup& setup, const std::vector<CatalogPack>& catalog, bool includeUnverified,
                         std::vector<juce::String>& warnings)
    {
        KitSpec spec;
        spec.includeUnverified = includeUnverified;

        for (const auto& ps : setup.places)
        {
            const auto* def = findPlace (ps.placeId);
            if (! ps.enabled || def == nullptr)
                continue;

            KitSpec::Place place;
            place.slot = def->slot;
            place.placeId = ps.placeId;
            place.packId = juce::String (ps.packId);
            place.pieceId = juce::String (ps.pieceId);
            place.picks = ps.picks;

            const CatalogPack* pack = nullptr;
            for (const auto& c : catalog)
                if (c.packId == place.packId)
                    pack = &c;

            if (pack != nullptr)
            {
                const CatalogPiece* piece = nullptr;
                if (place.pieceId.isNotEmpty())
                {
                    for (const auto& p : pack->pieces)
                        if (p.id == place.pieceId)
                            piece = &p;
                }
                else
                {
                    int count = 0;
                    for (const auto& p : pack->pieces)
                        if (p.type == juce::String (def->type))
                        {
                            if (piece == nullptr)
                                piece = &p;
                            ++count;
                        }
                    if (count > 1)
                        warnings.push_back ("Место "_s + juce::String::fromUTF8 (def->id) + ": в паке "_s + place.packId
                                            + " несколько барабанов типа "_s + juce::String (def->type) + ", взят "_s + piece->id);
                    if (piece != nullptr)
                        place.pieceId = piece->id;
                }

                if (piece == nullptr)
                    warnings.push_back ("Место "_s + juce::String::fromUTF8 (def->id) + ": в паке "_s + place.packId
                                        + " нет нужного барабана"_s);
                else
                {
                    auto mapping = mapKeysToPlace (piece->keys, piece->type.toStdString(), *def);
                    place.keyMap = std::move (mapping.keys);
                    for (const auto& w : mapping.warnings)
                        warnings.push_back (w);
                }
            }
            spec.places.push_back (std::move (place));
        }
        return spec;
    }
}
