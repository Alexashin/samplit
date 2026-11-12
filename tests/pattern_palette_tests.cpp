#include "PatternPalette.h"
#include "kit_fixture.h"

#include "samplit/core/Manifest.h"

#include <catch2/catch_test_macros.hpp>

using namespace samplit;
using namespace kittest;

namespace
{
    // Малый (слот 1, клавиши 25..35): слой «Основной» cycle 1..127 на 26; слой «Дроби» exact с тремя ударами на 27
    std::shared_ptr<const Kit> makeSnare()
    {
        KitBuilder b;
        b.mic ("snare_top", 0);
        const int p = b.piece ("snare");
        b.kit->pieces[(size_t) p].slot = 1;
        const int main = b.layer (p, 1, 127);
        b.kit->pieces[(size_t) p].layers[(size_t) main].pick = Pick::Cycle;
        b.kit->pieces[(size_t) p].layers[(size_t) main].choice = 0;
        const int drag = b.layer (p, 1, 127);
        b.kit->pieces[(size_t) p].layers[(size_t) drag].pick = Pick::Exact;
        b.kit->pieces[(size_t) p].layers[(size_t) drag].choice = 1;
        b.hit (p, main, 26, { 0.3f });
        b.hit (p, drag, 27, { 0.3f }, 1, 40);
        b.hit (p, drag, 27, { 0.3f }, 41, 80);
        b.hit (p, drag, 27, { 0.3f }, 81, 127);
        b.kit->choices.push_back ({ "snare:main", "Основной", Pick::Cycle, { p } });
        b.kit->choices.push_back ({ "snare:drags", "Дроби", Pick::Exact, { p } });
        return b.done();
    }
}

TEST_CASE ("palette: cycle layer gives one option, exact layer one per hit", "[pattern_palette]")
{
    const auto kit = makeSnare();
    const auto pal = buildPalette (*kit, 1);
    REQUIRE (pal.size() == 4);
    CHECK (pal[0].key == 26);
    CHECK (pal[0].velocity == 64);      // середина окна 1..127
    CHECK_FALSE (pal[0].exact);
    CHECK (pal[0].groupId == "main");
    CHECK (pal[1].key == 27);
    CHECK (pal[1].exact);
    CHECK (pal[1].velocity == 1);
    CHECK (pal[2].velocity == 41);
    CHECK (pal[3].velocity == 81);
    CHECK (pal[1].name == juce::String::fromUTF8 ("Дроби 1"));
}

TEST_CASE ("palette: every velocity falls into the window of its hit", "[pattern_palette]")
{
    const auto kit = makeSnare();
    for (const auto& o : buildPalette (*kit, 1))
    {
        const auto& map = kit->notes[(size_t) o.key];
        bool inWindow = false;
        for (const auto& ref : map.layers)
            inWindow = inWindow || (o.velocity >= ref.velLo && o.velocity <= ref.velHi);
        CHECK (inWindow);
    }
}

TEST_CASE ("palette: other slot gives nothing, meta supplies role and pair", "[pattern_palette]")
{
    const auto kit = makeSnare();
    CHECK (buildPalette (*kit, 0).empty());

    samplit::pack::Piece meta;
    samplit::pack::Group g;
    g.id = "drags";
    g.role = "roll";
    g.pair.type = "snare";
    g.pair.group = "main";
    meta.groups.push_back (g);
    const auto pal = buildPalette (*kit, 1, &meta);
    REQUIRE (pal.size() == 4);
    CHECK (pal[0].role == "other");
    CHECK (pal[1].role == "roll");
    CHECK (pal[1].pairGroup == "main");
}
