#include "PadEngine.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using samplit::PadEngine;
using samplit::kNumPads;

namespace
{
    constexpr double kRate = 48000.0;

    struct Block
    {
        explicit Block (int n) : left ((size_t) n, 0.0f), right ((size_t) n, 0.0f) {}
        std::vector<float> left, right;
        int size() const { return (int) left.size(); }
        float peak() const
        {
            float p = 0.0f;
            for (float v : left)
                p = std::max (p, std::abs (v));
            return p;
        }
    };

    PadEngine makeEngine()
    {
        PadEngine e;
        e.prepare (kRate);
        return e;
    }
}

TEST_CASE ("pad is silent until it is triggered")
{
    auto engine = makeEngine();
    Block b (512);

    for (int pad = 0; pad < kNumPads; ++pad)
    {
        engine.render (pad, b.left.data(), b.right.data(), 0, b.size());
        REQUIRE_FALSE (engine.isActive (pad));
    }
    REQUIRE (b.peak() == 0.0f);
}

TEST_CASE ("triggered pad produces sound in both channels")
{
    auto engine = makeEngine();
    Block b (512);

    engine.noteOn (0, 1.0f);
    engine.render (0, b.left.data(), b.right.data(), 0, b.size());

    REQUIRE (engine.isActive (0));
    REQUIRE (b.peak() > 0.1f);
    REQUIRE (b.left == b.right);
}

TEST_CASE ("velocity scales the level")
{
    auto loud = makeEngine();
    auto soft = makeEngine();
    Block a (2048), b (2048);

    loud.noteOn (3, 1.0f);
    soft.noteOn (3, 0.5f);
    loud.render (3, a.left.data(), a.right.data(), 0, a.size());
    soft.render (3, b.left.data(), b.right.data(), 0, b.size());

    REQUIRE_THAT (b.peak(), Catch::Matchers::WithinRel (a.peak() * 0.5f, 0.01f));
}

TEST_CASE ("pad fades out and becomes inactive")
{
    auto engine = makeEngine();
    Block b (4800);

    engine.noteOn (2, 1.0f);
    for (int i = 0; i < 20; ++i)  // 2 секунды
        engine.render (2, b.left.data(), b.right.data(), 0, b.size());

    REQUIRE_FALSE (engine.isActive (2));
}

TEST_CASE ("pads are independent")
{
    auto engine = makeEngine();
    Block b (512);

    engine.noteOn (1, 1.0f);
    engine.render (4, b.left.data(), b.right.data(), 0, b.size());

    REQUIRE (engine.isActive (1));
    REQUIRE_FALSE (engine.isActive (4));
    REQUIRE (b.peak() == 0.0f);
}

TEST_CASE ("render adds to the buffer instead of overwriting it")
{
    auto engine = makeEngine();
    Block b (256);
    std::fill (b.left.begin(), b.left.end(), 1.0f);
    std::fill (b.right.begin(), b.right.end(), 1.0f);

    engine.noteOn (0, 1.0f);
    engine.render (0, b.left.data(), b.right.data(), 0, b.size());

    // Синус стартует с нуля, дальше значения отличаются от исходной единицы
    REQUIRE (b.left[0] == 1.0f);
    REQUIRE (std::any_of (b.left.begin(), b.left.end(), [] (float v) { return v != 1.0f; }));
}

TEST_CASE ("render respects the sample range")
{
    auto engine = makeEngine();
    Block b (512);

    engine.noteOn (0, 1.0f);
    engine.render (0, b.left.data(), b.right.data(), 200, 300);

    REQUIRE (std::all_of (b.left.begin(), b.left.begin() + 200, [] (float v) { return v == 0.0f; }));
    REQUIRE (std::all_of (b.left.begin() + 300, b.left.end(), [] (float v) { return v == 0.0f; }));
    REQUIRE (std::any_of (b.left.begin() + 200, b.left.begin() + 300, [] (float v) { return v != 0.0f; }));
}
