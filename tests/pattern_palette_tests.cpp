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

namespace
{
    StrengthOption opt (int key, const char* group, int velocity, const char* pairType = "", const char* pairGroup = "")
    {
        StrengthOption o;
        o.key = key;
        o.groupId = group;
        o.velocity = velocity;
        o.exact = true;
        o.pairType = pairType;
        o.pairGroup = pairGroup;
        return o;
    }

    // Малый: основной удар без пары и крещендо 42/58 с парой на томах; два тома с крещендо 42/58 и флор без крещендо
    PlacePalette snarePlace()
    {
        return { 1, "snare", "snare", { opt (26, "main", 110), opt (32, "crescendo", 42, "tom", "crescendo"),
                                        opt (32, "crescendo", 58, "tom", "crescendo") } };
    }
    PlacePalette tomPlace (int slot, const char* id, int key)
    {
        return { slot, id, "tom", { opt (key, "main", 100), opt (key + 1, "crescendo", 42, "snare", "crescendo"),
                                    opt (key + 1, "crescendo", 58, "snare", "crescendo") } };
    }
}

TEST_CASE ("pairs: placing and removing a paired hit touches the partner on the same step", "[pattern_palette]")
{
    const auto snare = snarePlace();
    const auto tom = tomPlace (8, "tom_16l", 96);
    Pattern p;

    REQUIRE (canPair (tom, snare.options[1]));
    CHECK_FALSE (canPair (tom, snare.options[0]));          // у основного удара пары нет
    CHECK (pairOption (tom.options, snare.options[2]) == 2);

    REQUIRE (setPairedStep (p, 4, snare, snare.options[2], &tom));
    REQUIRE (p.findStep (4, 1) != nullptr);
    REQUIRE (p.findStep (4, 8) != nullptr);
    CHECK (p.findStep (4, 8)->key == 97);
    CHECK (p.findStep (4, 8)->velocity == 58);              // та же сила

    removePairedStep (p, 4, snare, &tom);
    CHECK (p.findStep (4, 1) == nullptr);
    CHECK (p.findStep (4, 8) == nullptr);

    // На шаге партнёра стоит свой удар (не пара): при снятии малого он остаётся
    setPairedStep (p, 6, snare, snare.options[1], &tom);
    p.setStep (6, 8, 96, 100);
    removePairedStep (p, 6, snare, &tom);
    CHECK (p.findStep (6, 1) == nullptr);
    REQUIRE (p.findStep (6, 8) != nullptr);
    CHECK (p.findStep (6, 8)->key == 96);

    // Без партнёра (переключатель «вместе» выключен) ставится только свой удар
    setPairedStep (p, 8, snare, snare.options[1], nullptr);
    CHECK (p.findStep (8, 1) != nullptr);
    CHECK (p.findStep (8, 8) == nullptr);

    // Шаг вне партии
    CHECK_FALSE (setPairedStep (p, 16, snare, snare.options[1], &tom));
    CHECK (p.findStep (16, 8) == nullptr);
}

TEST_CASE ("pairs: no partner when the place type differs or its pack has no paired group", "[pattern_palette]")
{
    const auto snare = snarePlace();
    auto otherPack = tomPlace (8, "tom_16l", 96);
    otherPack.options.resize (1);                           // смена пака у партнёра: крещендо больше нет
    CHECK_FALSE (canPair (otherPack, snare.options[1]));
    CHECK (pairOption (otherPack.options, snare.options[1]) == -1);

    Pattern p;
    setPairedStep (p, 0, snare, snare.options[1], &otherPack);
    CHECK (p.findStep (0, 1) != nullptr);
    CHECK (p.findStep (0, 8) == nullptr);

    // Тип не подходит: место-бочка с такой же группой
    PlacePalette kick { 0, "kick", "kick", { opt (24, "crescendo", 42) } };
    CHECK_FALSE (canPair (kick, snare.options[1]));

    // Явное место пары важнее типа
    auto byPlace = snare.options[1];
    byPlace.pairPlace = "tom_10";
    CHECK_FALSE (canPair (tomPlace (8, "tom_16l", 96), byPlace));
    CHECK (canPair (tomPlace (11, "tom_10", 102), byPlace));

    // Нет точного числа у партнёра: ближайшее в группе
    auto odd = snare.options[1];
    odd.velocity = 50;
    CHECK (pairOption (tomPlace (8, "tom_16l", 96).options, odd) == 1);
}
