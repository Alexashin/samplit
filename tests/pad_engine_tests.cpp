#include "PadEngine.h"
#include "test_helpers.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace samplit;
using samplit::test::makeSample;

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

    // Сколько отсчётов голос звучит до конца сэмпла
    int playedLength (PadEngine& engine, int voice, int maxSamples)
    {
        Block b (maxSamples);
        engine.render (voice, b.left.data(), b.right.data(), 0, b.size());
        int last = -1;
        for (int i = 0; i < b.size(); ++i)
            if (b.left[(size_t) i] != 0.0f)
                last = i;
        return last + 1;
    }
}

TEST_CASE ("empty pad stays silent")
{
    PadEngine engine;
    engine.prepare (kRate);
    Block b (512);

    engine.noteOn (0, 1.0f);
    engine.render (0, b.left.data(), b.right.data(), 0, b.size());

    REQUIRE_FALSE (engine.isActive (0));
    REQUIRE (b.peak() == 0.0f);
}

TEST_CASE ("pad plays the sample values at the same sample rate")
{
    PadEngine engine;
    engine.prepare (kRate);
    auto sample = makeSample (1000, kRate, 1, [] (int, int i) { return (float) i / 1000.0f; });
    engine.setSample (2, sample.get());
    Block b (100);

    engine.noteOn (2, 1.0f);
    engine.render (2, b.left.data(), b.right.data(), 0, b.size());

    for (int i = 0; i < b.size(); ++i)
        REQUIRE_THAT (b.left[(size_t) i], Catch::Matchers::WithinAbs ((float) i / 1000.0f, 1e-6));
}

TEST_CASE ("mono sample goes to both channels, stereo keeps its channels")
{
    PadEngine engine;
    engine.prepare (kRate);
    Block b (64);

    SECTION ("mono")
    {
        auto mono = makeSample (1000, kRate, 1);
        engine.setSample (0, mono.get());
        engine.noteOn (0, 1.0f);
        engine.render (0, b.left.data(), b.right.data(), 0, b.size());
        REQUIRE (b.left == b.right);
        REQUIRE (b.left[10] == 0.5f);
    }
    SECTION ("stereo")
    {
        auto stereo = makeSample (1000, kRate, 2, [] (int ch, int) { return ch == 0 ? 0.5f : -0.25f; });
        engine.setSample (0, stereo.get());
        engine.noteOn (0, 1.0f);
        engine.render (0, b.left.data(), b.right.data(), 0, b.size());
        REQUIRE (b.left[10] == 0.5f);
        REQUIRE (b.right[10] == -0.25f);
    }
}

TEST_CASE ("velocity scales the level")
{
    PadEngine engine;
    engine.prepare (kRate);
    auto sample = makeSample (1000);
    engine.setSample (1, sample.get());
    Block b (64);

    engine.noteOn (1, 0.5f);
    engine.render (1, b.left.data(), b.right.data(), 0, b.size());

    REQUIRE_THAT (b.peak(), Catch::Matchers::WithinRel (0.25f, 1e-6f));
}

TEST_CASE ("voice stops at the end of the sample")
{
    PadEngine engine;
    engine.prepare (kRate);
    auto sample = makeSample (300);
    engine.setSample (0, sample.get());

    engine.noteOn (0, 1.0f);
    const int length = playedLength (engine, 0, 1000);

    REQUIRE (length >= 298);
    REQUIRE (length <= 300);
    REQUIRE_FALSE (engine.isActive (0));
}

TEST_CASE ("sample at another rate keeps its duration")
{
    // 300 отсчётов на 24 кГц = 12,5 мс, на 48 кГц это ~600 отсчётов
    PadEngine engine;
    engine.prepare (kRate);
    auto sample = makeSample (300, 24000.0);
    engine.setSample (0, sample.get());

    engine.noteOn (0, 1.0f);
    const int length = playedLength (engine, 0, 2000);

    REQUIRE (length >= 596);
    REQUIRE (length <= 600);
}

TEST_CASE ("stop fades the voice out within 5 ms")
{
    PadEngine engine;
    engine.prepare (kRate);
    auto sample = makeSample (48000);
    engine.setSample (0, sample.get());
    Block b (512);

    engine.noteOn (0, 1.0f);
    engine.render (0, b.left.data(), b.right.data(), 0, 64);
    engine.stop (0);
    engine.render (0, b.left.data(), b.right.data(), 64, b.size());

    REQUIRE_FALSE (engine.isActive (0));
    REQUIRE (b.left[64] > b.left[150]);           // громкость падает
    REQUIRE (b.left[64 + 241] == 0.0f);           // через 5 мс (240 отсчётов) тишина
}

TEST_CASE ("retrigger restarts the sample from the beginning")
{
    PadEngine engine;
    engine.prepare (kRate);
    auto sample = makeSample (1000, kRate, 1, [] (int, int i) { return (float) i / 1000.0f; });
    engine.setSample (0, sample.get());
    Block a (100), b (10);

    engine.noteOn (0, 1.0f);
    engine.render (0, a.left.data(), a.right.data(), 0, a.size());
    engine.noteOn (0, 1.0f);
    engine.render (0, b.left.data(), b.right.data(), 0, b.size());

    REQUIRE (b.left[0] == 0.0f);
    REQUIRE_THAT (b.left[5], Catch::Matchers::WithinAbs (0.005f, 1e-6));
}

TEST_CASE ("isUsing tracks assigned and still playing samples")
{
    PadEngine engine;
    engine.prepare (kRate);
    auto sample = makeSample (48000);
    engine.setSample (3, sample.get());
    REQUIRE (engine.isUsing (sample.get()));

    engine.noteOn (3, 1.0f);
    engine.setSample (3, nullptr);
    REQUIRE (engine.isUsing (sample.get()));  // снят с пэда, но ещё звучит

    Block b (48000);
    engine.render (3, b.left.data(), b.right.data(), 0, b.size());
    REQUIRE_FALSE (engine.isUsing (sample.get()));
}

TEST_CASE ("render adds to the buffer and respects the range")
{
    PadEngine engine;
    engine.prepare (kRate);
    auto sample = makeSample (1000);
    engine.setSample (0, sample.get());
    Block b (512);
    std::fill (b.left.begin(), b.left.end(), 1.0f);

    engine.noteOn (0, 1.0f);
    engine.render (0, b.left.data(), b.right.data(), 200, 300);

    REQUIRE (b.left[199] == 1.0f);
    REQUIRE (b.left[250] == 1.5f);
    REQUIRE (b.left[300] == 1.0f);
}
