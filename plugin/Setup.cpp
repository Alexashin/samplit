#include "Setup.h"

#include "KitLoader.h"

#include "samplit/core/Manifest.h"

#include <algorithm>
#include <cmath>

namespace samplit
{
    using pack::operator""_s;

    const PlaceDef* findPlace (const std::string& id) noexcept
    {
        for (const auto& p : kStudio600Places)
            if (id == p.id)
                return &p;
        return nullptr;
    }

    Setup Setup::factory()
    {
        Setup s;
        for (const auto& d : kStudio600Places)
        {
            PlaceSetup p;
            p.placeId = d.id;
            p.packId = d.defaultPack;
            p.pieceId = d.defaultPiece;
            s.places.push_back (std::move (p));
        }
        return s;
    }

    const PlaceSetup* Setup::find (const std::string& placeId) const noexcept
    {
        for (const auto& p : places)
            if (p.placeId == placeId)
                return &p;
        return nullptr;
    }

    juce::String Setup::toJson() const
    {
        auto* root = new juce::DynamicObject();
        juce::var rootVar (root);
        root->setProperty ("format", 1);

        juce::Array<juce::var> arr;
        for (const auto& p : places)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("id", juce::String::fromUTF8 (p.placeId.c_str()));
            o->setProperty ("enabled", p.enabled);
            o->setProperty ("pack", juce::String::fromUTF8 (p.packId.c_str()));
            o->setProperty ("piece", juce::String::fromUTF8 (p.pieceId.c_str()));
            o->setProperty ("levelDb", (double) p.levelDb);
            auto* picks = new juce::DynamicObject();
            for (const auto& [group, pick] : p.picks)
                picks->setProperty (juce::Identifier (juce::String::fromUTF8 (group.c_str())), pickToString (pick));
            o->setProperty ("picks", juce::var (picks));
            arr.add (juce::var (o));
        }
        root->setProperty ("places", arr);
        return juce::JSON::toString (rootVar);
    }

    bool Setup::fromJson (const juce::String& json, Setup& out, juce::String& error)
    {
        juce::var v;
        if (juce::JSON::parse (json, v).failed() || v.getDynamicObject() == nullptr)
        {
            error = "Установка: некорректный JSON"_s;
            return false;
        }
        const auto placesVar = v.getProperty ("places", {});
        if (! placesVar.isArray() && ! placesVar.isVoid())
        {
            error = "Установка: places должен быть массивом"_s;
            return false;
        }

        Setup s = factory();
        if (const auto* arr = placesVar.getArray())
            for (const auto& item : *arr)
            {
                if (! item.isObject())
                    continue;
                const auto id = item.getProperty ("id", {}).toString().toStdString();
                auto it = std::find_if (s.places.begin(), s.places.end(), [&] (const PlaceSetup& p) { return p.placeId == id; });
                if (it == s.places.end())
                    continue;       // незнакомое место

                auto& p = *it;
                if (item.hasProperty ("enabled"))
                    p.enabled = (bool) item["enabled"];
                if (item.hasProperty ("pack"))
                    p.packId = item["pack"].toString().toStdString();
                if (item.hasProperty ("piece"))
                    p.pieceId = item["piece"].toString().toStdString();
                if (item.hasProperty ("levelDb"))
                {
                    const auto db = (float) (double) item["levelDb"];
                    p.levelDb = std::isfinite (db) ? juce::jlimit (kPlaceLevelMinDb, kPlaceLevelMaxDb, db) : 0.0f;
                }
                if (auto* picks = item.getProperty ("picks", {}).getDynamicObject())
                    for (const auto& prop : picks->getProperties())
                    {
                        Pick pick;
                        if (pickFromString (prop.value.toString(), pick))
                            p.picks[prop.name.toString().toStdString()] = pick;
                    }
            }

        out = std::move (s);
        return true;
    }

    // ---------------------------------------------------------------- перенос клавиш

    KeyMapping mapKeysToPlace (const std::vector<int>& pieceKeys, const std::string& pieceType, const PlaceDef& target)
    {
        KeyMapping res;
        std::vector<int> keys (pieceKeys);
        std::sort (keys.begin(), keys.end());
        keys.erase (std::unique (keys.begin(), keys.end()), keys.end());
        if (keys.empty())
            return res;

        auto inside = [&] (const PlaceDef& p)
        {
            return std::all_of (keys.begin(), keys.end(), [&] (int k) { return k >= p.keyLo && k <= p.keyHi; });
        };
        const int targetCount = target.keyHi - target.keyLo + 1;

        // 1. Все клавиши в клавишах места
        if (inside (target))
        {
            for (int k : keys)
                res.keys[k] = k;
            return res;
        }

        // 2. Все клавиши в «родном» месте того же типа: по позиции
        for (const auto& home : kStudio600Places)
            if (&home != &target && pieceType == home.type && inside (home))
            {
                for (int k : keys)
                {
                    const int pos = k - home.keyLo;
                    if (pos < targetCount)
                        res.keys[k] = target.keyLo + pos;
                    else
                        res.warnings.push_back ("Клавиша "_s + juce::String (k) + " не помещается на место "_s + juce::String::fromUTF8 (target.id));
                }
                return res;
            }

        // 3. Чужой пак: отсортированные клавиши по порядку
        for (size_t i = 0; i < keys.size(); ++i)
        {
            if ((int) i < targetCount)
                res.keys[keys[i]] = target.keyLo + (int) i;
            else
                res.warnings.push_back ("Клавиша "_s + juce::String (keys[i]) + " отброшена: на месте "_s
                                        + juce::String::fromUTF8 (target.id) + " всего "_s + juce::String (targetCount));
        }
        return res;
    }
}
