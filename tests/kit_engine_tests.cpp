#include "KitEngine.h"
#include "PluginProcessor.h"
#include "kit_fixture.h"
#include "Studio600Names.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <chrono>
#include <map>
#include <thread>

using namespace samplit;
using namespace kittest;

namespace
{
    constexpr int kBlock = 1024;

    // Движок и выходные буферы: выходы 1..16 моно (правый указатель nullptr), Main стерео
    struct Rig
    {
        Rig()
        {
            engine.prepare (kRate);
            engine.setSeed (12345);
            for (size_t i = 0; i < L.size(); ++i)
            {
                L[i].assign (kBlock, 0.0f);
                R[i].assign (kBlock, 0.0f);
                pl[i] = L[i].data();
                pr[i] = i == 0 ? R[i].data() : nullptr;
            }
        }

        // Как будто DAW не включила этот выход: он ведёт в Main
        void routeToMain (int out)
        {
            pl[(size_t) out] = pl[0];
            pr[(size_t) out] = pr[0];
        }

        void render (int from = 0, int to = kBlock) { engine.render (pl, pr, from, to); }

        void clear()
        {
            for (size_t i = 0; i < L.size(); ++i)
            {
                std::fill (L[i].begin(), L[i].end(), 0.0f);
                std::fill (R[i].begin(), R[i].end(), 0.0f);
            }
        }

        // Одна нота с нуля: чистые голоса и буферы
        KitEngine::StrikeInfo hitOnce (int note, int velocity)
        {
            engine.prepare (kRate);
            clear();
            const auto info = engine.noteOn (note, velocity);
            render();
            return info;
        }

        float level (int out = 0, int ch = 0) const { return (ch == 0 ? L : R)[(size_t) out][kBlock / 2]; }

        KitEngine engine;
        std::array<std::vector<float>, kNumOutputs + 1> L, R;
        std::array<float*, kNumOutputs + 1> pl {}, pr {};
    };

    constexpr float kHalf = 0.5f;  // −6,0206 дБ
}

// ---------------------------------------------------------------- движок

TEST_CASE ("kit: all mics of a hit start together and belong to the same hit", "[kit]")
{
    KitBuilder b;
    const int m0 = b.mic ("kick_in", 1), m1 = b.mic ("kick_out", 2), m2 = b.mic ("room", 3);
    (void) m0; (void) m1; (void) m2;
    const int p = b.piece ("kick");
    const int l = b.layer (p, 1, 127);
    b.hit (p, l, 36, { 0.2f, 0.2f, 0.2f });
    b.hit (p, l, 36, { 0.4f, 0.4f, 0.4f });
    const auto kit = b.done();

    Rig r;
    r.engine.setKit (kit.get());
    for (int i = 0; i < 20; ++i)
    {
        const auto info = r.hitOnce (36, 100);
        REQUIRE (info.voices == 3);
        const float expected = info.hit == 0 ? 0.2f : 0.4f;
        for (int out = 1; out <= 3; ++out)
        {
            REQUIRE (std::abs (r.level (out) - expected) < 1e-4f);
            REQUIRE (r.L[(size_t) out][0] != 0.0f);   // стартуют в одном сэмпле, без нарастания
        }
        REQUIRE (r.level (0) == 0.0f);
    }
}

TEST_CASE ("kit: mics without a file in this hit stay silent", "[kit]")
{
    KitBuilder b;
    b.mic ("a", 1);
    b.mic ("b", 2);
    const int p = b.piece ("p");
    b.hit (p, b.layer (p, 1, 127), 60, { 0.3f, -1.0f });
    const auto kit = b.done();

    Rig r;
    r.engine.setKit (kit.get());
    REQUIRE (r.hitOnce (60, 100).voices == 1);
    REQUIRE (r.level (1) > 0.0f);
    REQUIRE (r.level (2) == 0.0f);
}

TEST_CASE ("kit: an output switched off in the host falls back to Main", "[kit]")
{
    KitBuilder b;
    b.mic ("a", 1);
    b.mic ("b", 2);
    const int p = b.piece ("p");
    b.hit (p, b.layer (p, 1, 127), 60, { 0.3f, 0.3f });
    const auto kit = b.done();

    Rig r;
    r.engine.setKit (kit.get());
    r.routeToMain (2);
    r.hitOnce (60, 100);

    REQUIRE (std::abs (r.level (1) - 0.3f) < 1e-4f);   // свой выход
    REQUIRE (std::abs (r.level (0, 0) - 0.3f) < 1e-4f); // микрофон b пришёл в Main, оба канала
    REQUIRE (std::abs (r.level (0, 1) - 0.3f) < 1e-4f);
}

TEST_CASE ("kit: mic level and pan apply, pan only on Main", "[kit]")
{
    KitBuilder b;
    b.mic ("main_mic", 0);
    b.mic ("out_mic", 1);
    const int p = b.piece ("p");
    b.hit (p, b.layer (p, 1, 127), 60, { 1.0f, 1.0f });
    const auto kit = b.done();

    Rig r;
    r.engine.setKit (kit.get());

    SECTION ("level")
    {
        r.engine.setMicMix (1, -6.0206f, 0.0f);
        r.hitOnce (60, 100);
        REQUIRE (std::abs (r.level (1) - kHalf) < 1e-3f);
        REQUIRE (std::abs (r.level (0, 0) - 1.0f) < 1e-4f);
    }
    SECTION ("pan to the right on Main, no effect on a mono output")
    {
        r.engine.setMicMix (0, 0.0f, 1.0f);
        r.engine.setMicMix (1, 0.0f, 1.0f);
        r.hitOnce (60, 100);
        REQUIRE (r.level (0, 0) == 0.0f);
        REQUIRE (std::abs (r.level (0, 1) - 1.0f) < 1e-4f);
        REQUIRE (std::abs (r.level (1) - 1.0f) < 1e-4f);
    }
}

TEST_CASE ("kit: mic output can be moved without rebuilding the kit", "[kit]")
{
    KitBuilder b;
    b.mic ("a", 1);
    const int p = b.piece ("p");
    b.hit (p, b.layer (p, 1, 127), 60, { 1.0f });
    const auto kit = b.done();

    Rig r;
    r.engine.setKit (kit.get());

    r.engine.setMicOutput (0, 5);
    r.hitOnce (60, 100);
    REQUIRE (r.level (1) == 0.0f);
    REQUIRE (std::abs (r.level (5) - 1.0f) < 1e-4f);

    r.engine.setMicOutput (0, 0);
    r.hitOnce (60, 100);
    REQUIRE (std::abs (r.level (0, 0) - 1.0f) < 1e-4f);
    REQUIRE (r.level (5) == 0.0f);

    r.engine.setMicOutput (0, -1);
    r.hitOnce (60, 100);
    REQUIRE (std::abs (r.level (1) - 1.0f) < 1e-4f);
}

TEST_CASE ("kit: file gain is applied per mic", "[kit]")
{
    KitBuilder b;
    b.mic ("a", 1);
    b.mic ("b", 2);
    const int p = b.piece ("p");
    b.hit (p, b.layer (p, 1, 127), 60, { 1.0f, 1.0f });
    b.kit->pieces[0].hits[0].files[1].gainDb = -6.0206f;
    const auto kit = b.done();

    Rig r;
    r.engine.setKit (kit.get());
    r.hitOnce (60, 100);
    REQUIRE (std::abs (r.level (1) - 1.0f) < 1e-4f);
    REQUIRE (std::abs (r.level (2) - kHalf) < 1e-3f);
}

TEST_CASE ("kit: the layer follows velocity", "[kit]")
{
    KitBuilder b;
    b.mic ("m", 1);
    const int p = b.piece ("p");
    b.hit (p, b.layer (p, 1, 63), 60, { 0.1f });
    b.hit (p, b.layer (p, 64, 127), 60, { 0.4f });
    const auto kit = b.done();

    Rig r;
    r.engine.setKit (kit.get());
    REQUIRE (r.hitOnce (60, 100).layer == 1);
    REQUIRE (std::abs (r.level (1) - 0.4f) < 1e-4f);
    REQUIRE (r.hitOnce (60, 20).layer == 0);
    REQUIRE (std::abs (r.level (1) - 0.1f) < 1e-4f);
    REQUIRE (r.hitOnce (60, 63).layer == 0);
    REQUIRE (r.hitOnce (60, 64).layer == 1);
}

TEST_CASE ("kit: unassigned velocity between layers is silent", "[kit]")
{
    KitBuilder b;
    b.mic ("m", 1);
    const int p = b.piece ("p");
    b.hit (p, b.layer (p, 1, 40), 60, { 0.1f });
    b.hit (p, b.layer (p, 80, 127), 60, { 0.4f });
    const auto kit = b.done();

    Rig r;
    r.engine.setKit (kit.get());
    for (int v : { 41, 50, 60, 75, 79 })
        REQUIRE (r.hitOnce (60, v).voices == 0);
    REQUIRE (r.hitOnce (60, 1).layer == 0);
    REQUIRE (r.hitOnce (60, 40).layer == 0);
    REQUIRE (r.hitOnce (60, 80).layer == 1);
    REQUIRE (r.hitOnce (60, 127).layer == 1);
}

TEST_CASE ("kit: Kontakt mode needs the exact velocity", "[kit]")
{
    KitBuilder b;
    b.mic ("m", 1);
    const int p = b.piece ("p");
    const int l = b.layer (p, 120, 127);
    for (int v = 120; v <= 127; ++v)
        b.hit (p, l, 60, { 0.01f * (float) (v - 119) }, v, v);
    const auto kit = b.done (HitSelect::Kontakt);

    Rig r;
    r.engine.setKit (kit.get());
    for (int v = 120; v <= 127; ++v)
    {
        const auto info = r.hitOnce (60, v);
        REQUIRE (info.hit == v - 120);
        REQUIRE (std::abs (r.level (1) - 0.01f * (float) (v - 119)) < 1e-4f);
    }
    REQUIRE (r.hitOnce (60, 119).voices == 0);   // щель между окнами: тишина, как в Kontakt
    REQUIRE (r.level (1) == 0.0f);
}

TEST_CASE ("kit: Live mode never repeats the previous hit and uses all hits of a layer", "[kit]")
{
    KitBuilder b;
    b.mic ("m", 1);
    const int p = b.piece ("p");
    const int l = b.layer (p, 1, 127);
    for (int i = 0; i < 4; ++i)
        b.hit (p, l, 60, { 0.1f * (float) (i + 1) });
    const auto kit = b.done();

    Rig r;
    r.engine.setKit (kit.get());

    std::map<int, int> count;
    int previous = -1;
    for (int i = 0; i < 400; ++i)
    {
        const auto info = r.engine.noteOn (60, 100);
        REQUIRE (info.hit != previous);
        previous = info.hit;
        ++count[info.hit];
    }
    REQUIRE (count.size() == 4);
    for (const auto& item : count)
        REQUIRE (item.second > 40);
}

TEST_CASE ("kit: a layer with a single hit may repeat", "[kit]")
{
    KitBuilder b;
    b.mic ("m", 1);
    const int p = b.piece ("p");
    b.hit (p, b.layer (p, 1, 127), 60, { 0.3f });
    const auto kit = b.done();

    Rig r;
    r.engine.setKit (kit.get());
    REQUIRE (r.engine.noteOn (60, 100).hit == 0);
    REQUIRE (r.engine.noteOn (60, 100).hit == 0);
}

TEST_CASE ("kit: random selection stays inside the assigned layer", "[kit]")
{
    KitBuilder b;
    b.mic ("m", 1);
    const int p = b.piece ("p");
    b.hit (p, b.layer (p, 1, 42), 60, { 0.1f });
    b.hit (p, b.layer (p, 43, 84), 60, { 0.2f });
    b.hit (p, b.layer (p, 85, 127), 60, { 0.3f });

    const auto kit = b.done();
    Rig r;
    r.engine.setKit (kit.get());
    for (int i = 0; i < 100; ++i)
    {
        REQUIRE (r.engine.noteOn (60, 10).layer == 0);
        REQUIRE (r.engine.noteOn (60, 60).layer == 1);
        REQUIRE (r.engine.noteOn (60, 100).layer == 2);
    }
}

TEST_CASE ("kit: only keys of a piece play", "[kit]")
{
    KitBuilder b;
    b.mic ("m", 1);
    const int p = b.piece ("p");
    const int l = b.layer (p, 1, 127);
    b.hit (p, l, 60, { 0.3f });
    b.hit (p, l, 61, { 0.5f });
    const auto kit = b.done();

    Rig r;
    r.engine.setKit (kit.get());
    REQUIRE (r.hitOnce (62, 100).voices == 0);
    REQUIRE (r.hitOnce (60, 100).voices == 1);
    REQUIRE (std::abs (r.level (1) - 0.3f) < 1e-4f);
    REQUIRE (r.hitOnce (61, 100).voices == 1);
    REQUIRE (std::abs (r.level (1) - 0.5f) < 1e-4f);
}

TEST_CASE ("kit: a key belongs to one piece", "[kit]")
{
    KitBuilder b;
    b.mic ("m", 1);
    const int p0 = b.piece ("a");
    const int p1 = b.piece ("b");
    b.hit (p0, b.layer (p0, 1, 127), 60, { 0.3f });
    b.hit (p1, b.layer (p1, 1, 127), 60, { 0.3f });
    REQUIRE_FALSE (b.kit->buildIndex());
}

TEST_CASE ("kit: velocity does not change the loudness unless asked", "[kit]")
{
    KitBuilder b;
    b.mic ("m", 1);
    const int p = b.piece ("p");
    b.hit (p, b.layer (p, 1, 127), 60, { 1.0f });

    SECTION ("none")
    {
        const auto kit = b.done (HitSelect::Live, VelocityGain::None);
        Rig r;
        r.engine.setKit (kit.get());
        r.hitOnce (60, 20);
        const float soft = r.level (1);
        r.hitOnce (60, 127);
        REQUIRE (std::abs (soft - 1.0f) < 1e-4f);
        REQUIRE (std::abs (r.level (1) - 1.0f) < 1e-4f);
    }
    SECTION ("linear")
    {
        const auto kit = b.done (HitSelect::Live, VelocityGain::Linear);
        Rig r;
        r.engine.setKit (kit.get());
        r.hitOnce (60, 64);
        REQUIRE (std::abs (r.level (1) - 64.0f / 127.0f) < 1e-4f);
    }
}

TEST_CASE ("kit: choke fades other pieces of the group within 5 ms", "[kit]")
{
    KitBuilder b;
    b.mic ("m", 1);
    const int open = b.piece ("open", 1);
    const int closed = b.piece ("closed", 1);
    const int tom = b.piece ("tom");
    b.hit (open, b.layer (open, 1, 127), 46, { 0.5f });
    b.hit (closed, b.layer (closed, 1, 127), 42, { 0.25f });
    b.hit (tom, b.layer (tom, 1, 127), 48, { 0.125f });
    const auto kit = b.done();

    Rig r;
    r.engine.setKit (kit.get());
    r.engine.noteOn (46, 100);
    r.engine.noteOn (48, 100);
    r.render (0, 256);
    REQUIRE (r.engine.getNumActiveStrikes() == 2);

    r.engine.noteOn (42, 100);   // закрытый хэт глушит открытый
    r.clear();
    r.render (0, kBlock);

    // 5 мс = 240 отсчётов: в конце блока открытый хэт тихий, том и закрытый хэт играют
    REQUIRE (std::abs (r.L[1][kBlock - 1] - (0.25f + 0.125f)) < 1e-4f);
    REQUIRE (r.L[1][10] > 0.25f + 0.125f);   // в начале блока открытый ещё слышен
    REQUIRE (r.engine.getNumActiveStrikes() == 2);
}

TEST_CASE ("kit: a piece does not choke itself", "[kit]")
{
    KitBuilder b;
    b.mic ("m", 1);
    const int hat = b.piece ("hat", 1);
    b.hit (hat, b.layer (hat, 1, 127), 42, { 0.25f });
    const auto kit = b.done();

    Rig r;
    r.engine.setKit (kit.get());
    r.engine.noteOn (42, 100);
    r.engine.noteOn (42, 100);
    r.render();
    REQUIRE (std::abs (r.level (1) - 0.5f) < 1e-4f);
}

TEST_CASE ("kit: clips play at the right level and the rate is converted", "[kit]")
{
    KitBuilder b;
    b.mic ("m", 1);
    const int p = b.piece ("p");
    b.hit (p, b.layer (p, 1, 127), 60, { 0.0f });
    b.kit->pieces[0].hits[0].files[0].clip = DecodedClip::fromFloat (std::vector<float> (44100, 0.5f), 44100.0);
    const auto kit = b.done();

    Rig r;
    r.engine.setKit (kit.get());
    r.hitOnce (60, 100);
    REQUIRE (std::abs (r.level (1) - 0.5f) < 1e-4f);

    // 44100 отсчётов при 44,1 кГц это секунда, то есть 48000 отсчётов при 48 кГц
    r.engine.prepare (kRate);
    r.engine.noteOn (60, 100);
    for (int i = 0; i < 46; ++i)
        r.render();
    REQUIRE (r.engine.getNumActiveStrikes() == 1);
    for (int i = 0; i < 3; ++i)
        r.render();
    REQUIRE (r.engine.getNumActiveStrikes() == 0);
}

TEST_CASE ("kit: the oldest strike is taken when the pool is full", "[kit]")
{
    KitBuilder b;
    b.mic ("m", 1);
    const int p = b.piece ("p");
    b.hit (p, b.layer (p, 1, 127), 60, { 0.001f });
    const auto kit = b.done();

    Rig r;
    r.engine.setKit (kit.get());
    for (int i = 0; i < KitEngine::kMaxStrikes + 20; ++i)
        r.engine.noteOn (60, 100);
    REQUIRE (r.engine.getNumActiveStrikes() == KitEngine::kMaxStrikes);
    r.render();
}

TEST_CASE ("kit: isUsing keeps a replaced kit while it sounds, active keys are reported", "[kit]")
{
    KitBuilder b1;
    b1.mic ("m", 1);
    const int p = b1.piece ("p");
    b1.hit (p, b1.layer (p, 1, 127), 100, { 0.3f });
    const auto kit1 = b1.done();
    auto kit2 = std::make_shared<Kit> (*kit1);

    Rig r;
    r.engine.setKit (kit1.get());
    REQUIRE (r.engine.isUsing (kit1.get()));
    REQUIRE_FALSE (r.engine.isUsing (kit2.get()));

    r.engine.noteOn (100, 100);
    r.engine.publishActivity();
    REQUIRE (r.engine.getActiveKeys()[1] == (1ull << (100 - 64)));

    r.engine.setKit (kit2.get());
    REQUIRE (r.engine.isUsing (kit1.get()));   // уже не текущий, но из него звучит удар

    r.engine.prepare (kRate);
    REQUIRE_FALSE (r.engine.isUsing (kit1.get()));
}

TEST_CASE ("kit: without a kit nothing plays", "[kit]")
{
    Rig r;
    REQUIRE (r.engine.noteOn (60, 100).voices == 0);
    r.render();
    REQUIRE (r.level (0) == 0.0f);
}

TEST_CASE ("kit: exact selection plays only its assigned hit", "[kit]")
{
    KitBuilder b;
    b.mic ("m", 1);
    const int p = b.piece ("p");
    const int soft = b.layer (p, 1, 100);
    const int exact = b.layer (p, 120, 127);
    b.kit->pieces[(size_t) p].layers[(size_t) exact].pick = Pick::Exact;
    b.hit (p, soft, 60, { 0.05f });
    for (int v = 120; v <= 127; ++v)
        b.hit (p, exact, 60, { 0.01f * (float) (v - 119) }, v, v);
    const auto kit = b.done();

    Rig r;
    r.engine.setKit (kit.get());
    for (int i = 0; i < 50; ++i)
    {
        const auto info = r.engine.noteOn (60, 123);
        REQUIRE (info.layer == exact);
        REQUIRE (info.hit == 1 + (123 - 120));   // всегда один и тот же, повтор подряд разрешён
    }

    REQUIRE (r.engine.noteOn (60, 112).voices == 0);
    REQUIRE (r.engine.noteOn (60, 50).layer == soft);
}

TEST_CASE ("kit: a layer with pick cycle plays its hits in velocity order and wraps around", "[kit]")
{
    KitBuilder b;
    b.mic ("m", 1);
    const int p = b.piece ("p");
    const int soft = b.layer (p, 1, 100);
    const int loud = b.layer (p, 120, 127);
    b.kit->pieces[(size_t) p].layers[(size_t) loud].pick = Pick::Cycle;
    b.hit (p, soft, 60, { 0.05f });
    // Удары в манифесте не по порядку velocity: круг всё равно идёт от мягкого к сильному
    for (int v : { 124, 120, 127, 122 })
        b.hit (p, loud, 60, { 0.1f }, v, v);
    const auto kit = b.done();

    Rig r;
    r.engine.setKit (kit.get());
    std::vector<int> played;
    for (int v : { 120, 127, 124, 122, 127, 120, 122, 124 })
    {
        REQUIRE (r.engine.noteOn (60, 121).voices == 0); // пустой слот не продвигает круг
        const auto info = r.engine.noteOn (60, v);
        REQUIRE (info.layer == loud);
        played.push_back (info.hit);
    }
    const std::vector<int> order { 2, 4, 1, 3 };    // velocity 120, 122, 124, 127
    for (size_t i = 0; i < played.size(); ++i)
        REQUIRE (played[i] == order[i % order.size()]);

    // Новый кит начинает круг сначала
    r.engine.setKit (nullptr);
    r.engine.setKit (kit.get());
    r.engine.noteOn (0, 1);
    const auto kit2 = b.done();
    r.engine.setKit (kit2.get());
    REQUIRE (r.engine.noteOn (60, 127).hit == 2);
    REQUIRE (r.engine.noteOn (60, 127).hit == 4);
}

TEST_CASE ("kit: a pick choice switches its layers between random and cycle", "[kit]")
{
    KitBuilder b;
    b.mic ("m", 1);
    const int p = b.piece ("p");
    const int l = b.layer (p, 112, 127);
    b.kit->pieces[(size_t) p].layers[(size_t) l].choice = 0;
    b.kit->choices.push_back ({ "p:60-60", "P", Pick::Random });
    for (int v = 112; v <= 127; ++v)
        b.hit (p, l, 60, { 0.1f }, v, v);
    const auto kit = b.done();

    Rig r;
    r.engine.setSeed (7);
    r.engine.setKit (kit.get());

    // По умолчанию случайно: за 16 ударов круг 0, 1, 2… не получается
    bool inOrder = true;
    for (int i = 0; i < 16; ++i)
        inOrder = inOrder && r.engine.noteOn (60, 120).hit == i;
    REQUIRE_FALSE (inOrder);

    r.engine.setPickChoice (0, (int) Pick::Cycle);
    const int first = r.engine.noteOn (60, 120).hit;
    for (int i = 1; i < 20; ++i)
        REQUIRE (r.engine.noteOn (60, 120).hit == (first + i) % 16);

    // Новый кит сбрасывает выбор пользователя к значению из кита
    r.engine.setKit (kit.get());
    r.engine.setPickChoice (0, -1);
    bool cycle = true;
    int prev = r.engine.noteOn (60, 120).hit;
    for (int i = 0; i < 16; ++i)
    {
        const int h = r.engine.noteOn (60, 120).hit;
        cycle = cycle && h == (prev + 1) % 16;
        prev = h;
    }
    REQUIRE_FALSE (cycle);
}

TEST_CASE ("kit: Hermite interpolation follows a sine when the rates differ", "[kit]")
{
    constexpr double clipRate = 44100.0;
    constexpr double freq = 1000.0;
    std::vector<float> sine ((size_t) clipRate);
    for (size_t n = 0; n < sine.size(); ++n)
        sine[n] = 0.5f * (float) std::sin (2.0 * 3.14159265358979323846 * freq * (double) n / clipRate);

    KitBuilder b;
    b.mic ("m", 1);
    const int p = b.piece ("p");
    b.hit (p, b.layer (p, 1, 127), 60, { 0.0f });
    b.kit->pieces[0].hits[0].files[0].clip = DecodedClip::fromFloat (std::move (sine), clipRate);
    const auto kit = b.done();

    Rig r;
    r.engine.setKit (kit.get());
    r.hitOnce (60, 100);

    // Выход на 48 кГц должен совпадать с той же синусоидой в моменты j / 48000; линейная давала бы ошибку около 1e-3
    float worst = 0.0f;
    for (int j = 2; j < kBlock - 2; ++j)
    {
        const float expected = 0.5f * (float) std::sin (2.0 * 3.14159265358979323846 * freq * (double) j / kRate);
        worst = std::max (worst, std::abs (r.L[1][(size_t) j] - expected));
    }
    REQUIRE (worst < 2e-4f);
}

// ---------------------------------------------------------------- процессор: 16 моно-выходов и кит

TEST_CASE ("16 mono outputs: Main is stereo, named outputs are mono and off by default", "[kit]")
{
    SamplitProcessor p;

    REQUIRE (p.getBusCount (false) == 1 + kNumOutputs);
    REQUIRE (p.getBus (false, 0)->getDefaultLayout() == juce::AudioChannelSet::stereo());
    for (int bus = 1; bus <= kNumOutputs; ++bus)
    {
        REQUIRE_FALSE (p.getBus (false, bus)->isEnabled());
        REQUIRE (p.getBus (false, bus)->getDefaultLayout() == juce::AudioChannelSet::mono());
        REQUIRE (p.getBus (false, bus)->getName() == juce::String::fromUTF8 (studio600OutputName (bus)));
    }
}

TEST_CASE ("16 mono outputs: layout rules", "[kit]")
{
    SamplitProcessor p;
    auto layout = p.getBusesLayout();
    REQUIRE (p.checkBusesLayoutSupported (layout));

    SECTION ("stereo extra output is rejected")
    {
        layout.outputBuses.getReference (3) = juce::AudioChannelSet::stereo();
        REQUIRE_FALSE (p.checkBusesLayoutSupported (layout));
    }
    SECTION ("mono Main is rejected")
    {
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
        REQUIRE_FALSE (p.checkBusesLayoutSupported (layout));
    }
    SECTION ("disabled Main is rejected")
    {
        layout.outputBuses.getReference (0) = juce::AudioChannelSet::disabled();
        REQUIRE_FALSE (p.checkBusesLayoutSupported (layout));
    }
    SECTION ("all extra outputs mono is accepted")
    {
        for (int i = 1; i < layout.outputBuses.size(); ++i)
            layout.outputBuses.getReference (i) = juce::AudioChannelSet::mono();
        REQUIRE (p.checkBusesLayoutSupported (layout));
    }
}

TEST_CASE ("processor with a kit: mics go to their mono outputs", "[kit]")
{
    KitFixture f { 1, 2 };   // каналы: Main = 0/1, Out 1 = 2, Out 2 = 3
    f.processor.setKit (f.kit);

    auto out = f.play (36);
    REQUIRE (KitFixture::peak (out, 2) > 0.0f);   // kick_in
    REQUIRE (KitFixture::peak (out, 3) > 0.0f);   // kick_out
    REQUIRE (KitFixture::peak (out, 0) > 0.0f);   // room идёт в Main
    REQUIRE (KitFixture::peak (out, 1) > 0.0f);
    REQUIRE (f.processor.getEnabledOutputMask() == 0b11u);
}

TEST_CASE ("processor with a kit: outputs the host did not enable play into Main", "[kit]")
{
    KitFixture f;
    f.processor.setKit (f.kit);

    auto out = f.play (36);
    REQUIRE (out.getNumChannels() == 2);
    // три микрофона по 0,3 в Main, в каждом канале
    REQUIRE (std::abs (KitFixture::peak (out, 0) - 0.9f) < 1e-3f);
    REQUIRE (std::abs (KitFixture::peak (out, 1) - 0.9f) < 1e-3f);
    REQUIRE (f.processor.getEnabledOutputMask() == 0u);
}

TEST_CASE ("processor with a kit: velocity does not change the level, unmapped notes are silent", "[kit]")
{
    KitFixture f;
    f.processor.setKit (f.kit);

    const float soft = KitFixture::peak (f.play (36, 20.0f / 127.0f), 0);
    juce::MidiBuffer idle;
    for (int i = 0; i < 200; ++i)
        f.run (idle);
    const float hard = KitFixture::peak (f.play (36, 1.0f), 0);
    REQUIRE (std::abs (soft - hard) < 1e-4f);

    for (int i = 0; i < 200; ++i)   // пусть прошлый удар доиграет
        f.run (idle);
    REQUIRE (KitFixture::peak (f.play (50), 0) == 0.0f);
}

TEST_CASE ("processor with a kit: the kit takes the notes, without a kit the plugin is silent", "[kit]")
{
    KitFixture f;
    f.processor.setKit (f.kit);
    REQUIRE (f.processor.getKit() == f.kit);

    f.processor.triggerNote (36, 100);
    juce::MidiBuffer midi;
    auto out = f.run (midi);
    REQUIRE (KitFixture::peak (out, 0) > 0.0f);
    REQUIRE (f.processor.getActiveKeys()[0] == (1ull << 36));

    f.processor.setKit (nullptr);
    REQUIRE (f.processor.getKit() == nullptr);
    for (int i = 0; i < 200; ++i)
        f.run (midi);
    REQUIRE (KitFixture::peak (f.play (36), 0) == 0.0f);   // без кита ноты никуда не идут
}

TEST_CASE ("processor with a kit: a replaced kit is freed only after its strike ends", "[kit]")
{
    KitFixture f;
    f.processor.setKit (f.kit);
    f.play (36);   // звучит секунду

    f.processor.setKit (nullptr);
    f.processor.collectGarbage();
    REQUIRE (f.processor.getRetiredCount() > 0);   // кит ещё звучит

    juce::MidiBuffer midi;
    for (int i = 0; i < 100; ++i)
        f.run (midi);
    f.processor.collectGarbage();
    REQUIRE (f.processor.getRetiredCount() == 0);
}

TEST_CASE ("kit: a kit protected by the block hazard pointer is not released until the block ends", "[kit]")
{
    KitBuilder b1;
    b1.mic ("m", 1);
    const int p = b1.piece ("p");
    b1.hit (p, b1.layer (p, 1, 127), 100, { 0.3f });
    const auto kit1 = b1.done();
    const auto kit2 = std::make_shared<Kit> (*kit1);

    Rig r;
    r.engine.setKit (kit1.get());
    r.engine.beginBlock();
    REQUIRE (r.engine.isProtected (kit1.get()));

    r.engine.setKit (kit2.get());                // кит заменили посреди блока
    REQUIRE (r.engine.isUsing (kit1.get()));     // ударов нет, он уже не текущий, но блок его держит
    REQUIRE (r.engine.noteOn (100, 100).voices == 1);   // и играет именно защищённый кит

    r.engine.endBlock();
    REQUIRE_FALSE (r.engine.isProtected (kit1.get()));
    REQUIRE (r.engine.isUsing (kit1.get()));     // теперь держит звучащий удар
    r.engine.prepare (kRate);
    REQUIRE_FALSE (r.engine.isUsing (kit1.get()));

    // Без блока удержания нет
    r.engine.beginBlock();
    r.engine.endBlock();
    REQUIRE_FALSE (r.engine.isProtected (kit2.get()));
    REQUIRE (r.engine.isUsing (kit2.get()));     // текущий
}

TEST_CASE ("kit: a changed kit resets the cycle and last hit by serial, not by address", "[kit]")
{
    KitBuilder b;
    b.mic ("m", 1);
    const int p = b.piece ("p");
    const int l = b.layer (p, 1, 127);
    b.kit->pieces[(size_t) p].layers[(size_t) l].pick = Pick::Cycle;
    b.hit (p, l, 60, { 0.2f }, 1, 60);
    b.hit (p, l, 60, { 0.4f }, 61, 127);
    const auto a = b.done();
    const auto copy = std::make_shared<Kit> (*a);
    REQUIRE (copy->serial.value != a->serial.value);   // копия это другой кит
    REQUIRE (a->serial.value != 0);

    Rig r;
    r.engine.setKit (a.get());
    const int first = r.engine.noteOn (60, 100).hit;
    REQUIRE (r.engine.getLastKitSerial() == a->serial.value);
    REQUIRE (r.engine.noteOn (60, 100).hit != first);   // круг ушёл вперёд

    r.engine.setKit (copy.get());
    REQUIRE (r.engine.noteOn (60, 100).hit == first);   // новый кит начинает круг заново
    REQUIRE (r.engine.getLastKitSerial() == copy->serial.value);
}

TEST_CASE ("processor with a kit: 200 kit swaps while the audio thread plays do not crash, retired kits are freed", "[kit]")
{
    KitFixture f;
    f.processor.setKit (f.kit);

    std::atomic<bool> run { true };
    std::atomic<int> blocks { 0 };
    std::thread audio ([&]
    {
        juce::MidiBuffer midi;
        while (run.load())
        {
            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOn (1, 36, 1.0f), 0);
            f.run (midi);
            ++blocks;
            std::this_thread::yield();
        }
    });

    for (int i = 0; i < 200; ++i)
    {
        f.processor.setKit (i % 7 == 6 ? nullptr : KitFixture::makeKit ({ 1, 2, 0 }));
        if (i % 10 == 0)
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    run.store (false);
    audio.join();
    REQUIRE (blocks.load() > 0);

    f.processor.setKit (nullptr);
    juce::MidiBuffer idle;
    for (int i = 0; i < 300; ++i)      // удары доигрывают (клип 1 с)
        f.run (idle);
    f.processor.collectGarbage();
    REQUIRE (f.processor.getRetiredCount() == 0);
}
