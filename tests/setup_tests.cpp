#include "PackCatalog.h"
#include "Setup.h"

#include <catch2/catch_test_macros.hpp>

using namespace samplit;
using samplit::pack::operator""_s;

namespace
{
    const PlaceDef& place (const char* id)
    {
        const auto* p = findPlace (id);
        REQUIRE (p != nullptr);
        return *p;
    }

    juce::File catalogDir (const char* name)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("samplit-tests").getChildFile (name);
        dir.deleteRecursively();
        dir.createDirectory();
        return dir;
    }

    pack::Group groupOf (const juce::String& id, std::vector<juce::String> layers)
    {
        pack::Group g;
        g.id = id;
        g.name = id;
        g.layers = std::move (layers);
        return g;
    }

    void writeManifest (const juce::File& dir, const juce::String& id, bool unverifiedGroup)
    {
        pack::Manifest m;
        m.id = id;
        m.title = "T " + id;
        pack::Piece p;
        p.id = "snare_live";
        p.name = "Snare";
        p.type = "snare";
        p.keys = { 25 };
        p.layers = { { "a", "", 1, 127 }, { "b", "", 1, 127 } };
        p.layers[1].extra.set ("unverified", true);
        p.groups = { groupOf ("center", { "a" }), groupOf ("odd", { "b" }) };
        if (! unverifiedGroup)
            p.groups.pop_back();
        m.pieces.push_back (p);
        pack::Piece k;
        k.id = "kick";
        k.type = "kick";
        m.pieces.push_back (k);
        dir.getChildFile (id + ".json").replaceWithText (m.toJson());
    }
}

TEST_CASE ("Setup factory and JSON round trip", "[setup]")
{
    auto s = Setup::factory();
    REQUIRE (s.places.size() == kStudio600Places.size());
    REQUIRE (s.places[1].placeId == "snare");
    REQUIRE (s.places[1].packId == "studio600-snare-live");

    s.places[1].enabled = false;
    s.places[1].levelDb = -6.5f;
    s.places[1].pieceId = "other";
    s.places[1].picks["center"] = Pick::Cycle;
    s.places[3].picks["x"] = Pick::Exact;

    Setup back;
    juce::String err;
    REQUIRE (Setup::fromJson (s.toJson(), back, err));
    REQUIRE (back.places.size() == s.places.size());
    for (size_t i = 0; i < s.places.size(); ++i)
    {
        REQUIRE (back.places[i].placeId == s.places[i].placeId);
        REQUIRE (back.places[i].enabled == s.places[i].enabled);
        REQUIRE (back.places[i].packId == s.places[i].packId);
        REQUIRE (back.places[i].pieceId == s.places[i].pieceId);
        REQUIRE (back.places[i].levelDb == s.places[i].levelDb);
        REQUIRE (back.places[i].picks == s.places[i].picks);
    }
}

TEST_CASE ("Setup unknown and missing place, broken JSON, level clamp", "[setup]")
{
    Setup s;
    juce::String err;

    REQUIRE (Setup::fromJson (R"({"format":1,"places":[
        {"id":"nope","enabled":false,"pack":"zzz"},
        {"id":"hat","enabled":false,"levelDb":99,"picks":{"g":"cycle","h":"bogus"}},
        {"id":"ride","levelDb":-500}]})", s, err));
    REQUIRE (s.places.size() == kStudio600Places.size());        // недостающие из factory()
    REQUIRE (s.find ("nope") == nullptr);
    REQUIRE (s.find ("kick")->packId == "studio600-kick-rode");
    REQUIRE (s.find ("hat")->enabled == false);
    REQUIRE (s.find ("hat")->levelDb == kPlaceLevelMaxDb);
    REQUIRE (s.find ("hat")->packId == "studio600-hat");
    REQUIRE (s.find ("hat")->picks.size() == 1);
    REQUIRE (s.find ("hat")->picks.at ("g") == Pick::Cycle);
    REQUIRE (s.find ("ride")->levelDb == kPlaceLevelMinDb);

    Setup untouched = Setup::factory();
    untouched.places[0].levelDb = 3.0f;
    REQUIRE_FALSE (Setup::fromJson ("{ broken", untouched, err));
    REQUIRE (err.isNotEmpty());
    REQUIRE (untouched.places[0].levelDb == 3.0f);
    REQUIRE_FALSE (Setup::fromJson ("[1,2]", untouched, err));
}

TEST_CASE ("Key mapping rule 1: keys inside the place", "[setup]")
{
    const auto r = mapKeysToPlace ({ 26, 25, 35 }, "snare", place ("snare"));
    REQUIRE (r.keys.size() == 3);
    for (const auto& [from, to] : r.keys)
        REQUIRE (from == to);
    REQUIRE (r.warnings.empty());
}

TEST_CASE ("Key mapping rule 2: native place of the same type", "[setup]")
{
    // Crash 16 (40-43) на месте Crash 18 (36-39)
    auto r = mapKeysToPlace ({ 40, 41, 42, 43 }, "crash", place ("crash_a"));
    REQUIRE (r.keys.at (40) == 36);
    REQUIRE (r.keys.at (43) == 39);
    REQUIRE (r.warnings.empty());

    // Том на соседнем месте: tom_16m (98-99) на tom_14 (100-101)
    r = mapKeysToPlace ({ 98, 99 }, "tom", place ("tom_14"));
    REQUIRE (r.keys.at (98) == 100);
    REQUIRE (r.keys.at (99) == 101);

    // Родное место больше целевого: хвост отбрасывается с предупреждением
    r = mapKeysToPlace ({ 36, 37, 38, 39 }, "crash", place ("crash_b"));
    REQUIRE (r.keys.size() == 4);
    r = mapKeysToPlace ({ 25, 30, 35 }, "snare", place ("kick"));    // не тот тип места, правило 3
    REQUIRE (r.keys.size() == 1);
    REQUIRE (r.keys.at (25) == 24);
    REQUIRE (r.warnings.size() == 2);
}

TEST_CASE ("Key mapping rule 3: foreign pack", "[setup]")
{
    // Клавиши вне любого места: по порядку на клавиши China (44-46), лишние отбрасываются
    const auto r = mapKeysToPlace ({ 10, 5, 7, 9, 5 }, "china", place ("china"));
    REQUIRE (r.keys.size() == 3);
    REQUIRE (r.keys.at (5) == 44);
    REQUIRE (r.keys.at (7) == 45);
    REQUIRE (r.keys.at (9) == 46);
    REQUIRE (r.keys.count (10) == 0);
    REQUIRE (r.warnings.size() == 1);

    // Клавиши лежат в месте другого типа: тоже правило 3
    const auto s = mapKeysToPlace ({ 84, 85 }, "crash", place ("crash_a"));
    REQUIRE (s.keys.at (84) == 36);
    REQUIRE (s.keys.at (85) == 37);
}

TEST_CASE ("Pack catalog: local library, unverified filter, packs for a place", "[setup]")
{
    const auto lib = catalogDir ("catalog-lib");
    const auto manifests = lib.getChildFile ("manifests");
    manifests.createDirectory();
    writeManifest (manifests, "studio600-snare-live", true);
    writeManifest (manifests, "studio600-unverified", true);
    manifests.getChildFile ("junk.json").replaceWithText ("{ not a manifest");

    KitLoadOptions o;
    o.libraryRoot = lib;
    o.manifestDir = manifests;

    auto cat = scanPackCatalog (o, false);
    REQUIRE (cat.size() == 1);
    REQUIRE (cat[0].packId == "studio600-snare-live");
    REQUIRE (cat[0].pieces.size() == 2);
    REQUIRE (cat[0].pieces[0].type == "snare");
    REQUIRE (cat[0].pieces[0].groups.size() == 1);          // группа «odd» целиком unverified скрыта
    REQUIRE (cat[0].pieces[0].groups[0].id == "center");

    cat = scanPackCatalog (o, true);
    REQUIRE (cat.size() == 2);
    REQUIRE (cat[1].packId == "studio600-unverified");
    REQUIRE (cat[0].pieces[0].groups.size() == 2);

    const auto forSnare = packsForPlace (cat, place ("snare"));
    REQUIRE (forSnare.size() == 2);
    REQUIRE (forSnare[0].pieces.size() == 1);
    REQUIRE (forSnare[0].pieces[0].id == "snare_live");
    REQUIRE (packsForPlace (cat, place ("hat")).empty());

    // Нет источников: пусто
    REQUIRE (scanPackCatalog (KitLoadOptions {}, true).empty());
}
