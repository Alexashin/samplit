#include "SamplerEngine.h"
#include "test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <map>

using namespace samplit;
using samplit::test::makeSample;

namespace
{
    constexpr double kRate = 48000.0;
    constexpr int kBlock = 1024;

    // Движок с одним снимком инструмента и набором выходных буферов
    struct Rig
    {
        Rig()
        {
            engine.prepare (kRate);
            for (size_t i = 0; i < L.size(); ++i)
            {
                L[i].assign (kBlock, 0.0f);
                R[i].assign (kBlock, 0.0f);
                pl[i] = L[i].data();
                pr[i] = R[i].data();
            }
        }

        Group& addGroup (PlayMode mode = PlayMode::All, int output = 0)
        {
            Group g;
            g.name = "G";
            g.mode = mode;
            g.output = output;
            inst.groups.push_back (g);
            return inst.groups.back();
        }

        // Зона с сэмплом постоянного уровня `level`: по уровню видно, какая зона сыграла
        Zone& addZone (int group, int key, float level, int lowVel = 1, int highVel = 127, int length = 48000)
        {
            Zone z;
            z.sample = makeSample (length, kRate, 1, [level] (int, int) { return level; });
            z.group = group;
            z.lowKey = z.highKey = z.rootKey = key;
            z.lowVel = lowVel;
            z.highVel = highVel;
            inst.zones.push_back (z);
            return inst.zones.back();
        }

        void commit() { engine.setInstrument (&inst); }

        void render (int from = 0, int to = kBlock) { engine.render (pl, pr, from, to); }

        // Тишина и сброс голосов: следующий удар слышен с чистого листа
        void reset()
        {
            engine.prepare (kRate);
            for (size_t i = 0; i < L.size(); ++i)
            {
                std::fill (L[i].begin(), L[i].end(), 0.0f);
                std::fill (R[i].begin(), R[i].end(), 0.0f);
            }
        }

        // Уровень в середине блока (после нарастания в начале)
        float level (int out = 0, int ch = 0) const { return (ch == 0 ? L : R)[(size_t) out][kBlock / 2]; }

        SamplerEngine engine;
        Instrument inst;
        std::array<std::vector<float>, kNumOutputs + 1> L, R;
        std::array<float*, kNumOutputs + 1> pl {}, pr {};
    };
}

TEST_CASE ("engine without instrument is silent")
{
    Rig r;
    REQUIRE (r.engine.noteOn (60, 100) == 0);
    r.render();
    REQUIRE (r.level() == 0.0f);
}

TEST_CASE ("a zone plays only on its key and velocity range")
{
    Rig r;
    r.addGroup();
    r.addZone (0, 60, 0.5f, 10, 100);
    r.commit();

    REQUIRE (r.engine.noteOn (61, 100) == 0);
    REQUIRE (r.engine.noteOn (60, 9) == 0);
    REQUIRE (r.engine.noteOn (60, 101) == 0);
    REQUIRE (r.engine.noteOn (60, 10) == 1);
    REQUIRE (r.engine.noteOn (60, 100) == 1);
}

TEST_CASE ("key range covers several keys")
{
    Rig r;
    r.addGroup();
    r.addZone (0, 60, 0.5f).highKey = 64;
    r.commit();

    for (int key = 60; key <= 64; ++key)
        REQUIRE (r.engine.noteOn (key, 100) == 1);
    REQUIRE (r.engine.noteOn (65, 100) == 0);
}

TEST_CASE ("velocity scales the level linearly")
{
    Rig r;
    r.addGroup();
    r.addZone (0, 60, 1.0f);
    r.commit();

    r.engine.noteOn (60, 127);
    r.render();
    REQUIRE (std::abs (r.level() - 1.0f) < 1e-4f);

    r.reset();
    r.engine.noteOn (60, 64);
    r.render();
    REQUIRE (std::abs (r.level() - 64.0f / 127.0f) < 1e-4f);
}

TEST_CASE ("velocity layers pick the sample for the played velocity")
{
    Rig r;
    r.addGroup (PlayMode::Random);
    r.addZone (0, 60, 0.1f, 1, 63);
    r.addZone (0, 60, 0.4f, 64, 127);
    r.commit();

    r.engine.noteOn (60, 127);
    r.render();
    REQUIRE (std::abs (r.level() - 0.4f) < 1e-4f);

    r.reset();
    r.engine.noteOn (60, 63);
    r.render();
    REQUIRE (std::abs (r.level() - 0.1f * 63.0f / 127.0f) < 1e-4f);
}

TEST_CASE ("cycle mode goes through the variations in order")
{
    Rig r;
    r.addGroup (PlayMode::Cycle);
    r.addZone (0, 60, 0.1f);
    r.addZone (0, 60, 0.2f);
    r.addZone (0, 60, 0.3f);
    r.commit();

    const float expected[] = { 0.1f, 0.2f, 0.3f, 0.1f, 0.2f };
    for (float level : expected)
    {
        r.reset();
        r.engine.noteOn (60, 127);
        r.render();
        REQUIRE (std::abs (r.level() - level * 1.0f) < 1e-4f);
    }
}

TEST_CASE ("random mode uses every variation")
{
    Rig r;
    r.addGroup (PlayMode::Random);
    for (int i = 0; i < 4; ++i)
        r.addZone (0, 60, 0.1f * (float) (i + 1));
    r.commit();

    std::map<int, int> hits;
    for (int i = 0; i < 400; ++i)
    {
        r.reset();
        r.engine.noteOn (60, 127);
        r.render();
        ++hits[(int) std::lround (r.level() * 10.0f)];
    }

    REQUIRE (hits.size() == 4);
    for (const auto& item : hits)
        REQUIRE (item.second > 50);
}

TEST_CASE ("all mode layers every matching zone")
{
    Rig r;
    r.addGroup (PlayMode::All);
    r.addZone (0, 60, 0.1f);
    r.addZone (0, 60, 0.2f);
    r.commit();

    REQUIRE (r.engine.noteOn (60, 127) == 2);
    r.render();
    REQUIRE (std::abs (r.level() - 0.3f) < 1e-4f);
}

TEST_CASE ("each group picks on its own")
{
    Rig r;
    r.addGroup (PlayMode::Random, 0);
    r.addGroup (PlayMode::Random, 0);
    r.addZone (0, 60, 0.1f);
    r.addZone (0, 60, 0.1f);
    r.addZone (1, 60, 0.2f);
    r.commit();

    REQUIRE (r.engine.noteOn (60, 127) == 2);  // по одной зоне из каждой группы
}

TEST_CASE ("a group sends its sound to its own output")
{
    Rig r;
    r.addGroup (PlayMode::All, 0);
    r.addGroup (PlayMode::All, 3);
    r.addZone (0, 60, 0.2f);
    r.addZone (1, 61, 0.4f);
    r.commit();

    r.engine.noteOn (61, 127);
    r.render();
    REQUIRE (r.level (0) == 0.0f);
    REQUIRE (std::abs (r.level (3) - 0.4f) < 1e-4f);
    REQUIRE (std::abs (r.level (3, 1) - 0.4f) < 1e-4f);
    REQUIRE (r.level (1) == 0.0f);
}

TEST_CASE ("a mono output receives the stereo voice as (L+R)/2")
{
    Rig r;
    r.addGroup (PlayMode::All, 3);
    auto& z = r.addZone (0, 60, 0.0f);
    z.sample = makeSample (48000, kRate, 2, [] (int ch, int) { return ch == 0 ? 0.6f : 0.2f; });
    r.commit();
    r.pr[3] = nullptr;   // выход 3 моно

    r.engine.noteOn (60, 127);
    r.render();
    REQUIRE (std::abs (r.level (3) - 0.4f) < 1e-4f);
    REQUIRE (r.level (0) == 0.0f);
}

TEST_CASE ("group gain and pan shape the output")
{
    Rig r;
    auto& g = r.addGroup();
    g.gainDb = -6.0206f;
    g.pan = 1.0f;
    r.addZone (0, 60, 1.0f);
    r.commit();

    r.engine.noteOn (60, 127);
    r.render();
    REQUIRE (r.level (0, 0) == 0.0f);
    REQUIRE (std::abs (r.level (0, 1) - 0.5f) < 1e-3f);
}

TEST_CASE ("tracking changes the speed relative to the root key")
{
    Rig r;
    r.addGroup().tracking = true;
    auto& z = r.addZone (0, 60, 0.0f, 1, 127, 8000);
    z.lowKey = 48;
    z.highKey = 84;
    z.rootKey = 60;
    // линейная рампа: значение равно номеру отсчёта / 10000
    z.sample = makeSample (8000, kRate, 1, [] (int, int i) { return (float) i / 10000.0f; });
    r.commit();

    r.engine.noteOn (72, 127);  // октава вверх: вдвое быстрее
    r.render();
    REQUIRE (std::abs (r.L[0][400] - 800.0f / 10000.0f) < 1e-3f);

    r.reset();
    r.engine.noteOn (60, 127);  // корневая клавиша: без сдвига
    r.render();
    REQUIRE (std::abs (r.L[0][400] - 400.0f / 10000.0f) < 1e-3f);
}

TEST_CASE ("without tracking every key plays at the original speed")
{
    Rig r;
    r.addGroup();
    auto& z = r.addZone (0, 60, 0.0f, 1, 127, 8000);
    z.highKey = 84;
    z.sample = makeSample (8000, kRate, 1, [] (int, int i) { return (float) i / 10000.0f; });
    r.commit();

    r.engine.noteOn (72, 127);
    r.render();
    REQUIRE (std::abs (r.L[0][400] - 400.0f / 10000.0f) < 1e-3f);
}

TEST_CASE ("a zone whose sample is not loaded is skipped")
{
    Rig r;
    r.addGroup();
    r.addZone (0, 60, 0.5f).sample = nullptr;
    r.commit();

    REQUIRE (r.engine.noteOn (60, 100) == 0);
}

TEST_CASE ("voices run out gracefully: the oldest one is taken")
{
    Rig r;
    r.addGroup();
    r.addZone (0, 60, 0.01f);
    r.commit();

    for (int i = 0; i < kMaxVoices + 20; ++i)
        r.engine.noteOn (60, 100);

    REQUIRE (r.engine.getNumActiveVoices() == kMaxVoices);
    r.render();
}

TEST_CASE ("voice stops at the end of the sample")
{
    Rig r;
    r.addGroup();
    r.addZone (0, 60, 0.5f, 1, 127, 300);
    r.commit();

    r.engine.noteOn (60, 127);
    r.render();
    REQUIRE (r.engine.getNumActiveVoices() == 0);
    REQUIRE (r.L[0][250] != 0.0f);
    REQUIRE (r.L[0][400] == 0.0f);
}

TEST_CASE ("sample at another rate keeps its duration")
{
    Rig r;
    r.addGroup();
    r.addZone (0, 60, 0.5f, 1, 127, 100).sample = makeSample (44100, 44100.0);
    r.commit();

    r.engine.noteOn (60, 127);
    // 44100 отсчётов при 44.1 кГц это 1 с, то есть 48000 отсчётов при 48 кГц
    for (int i = 0; i < 45; ++i)
        r.render();
    REQUIRE (r.engine.getNumActiveVoices() == 1);
    for (int i = 0; i < 3; ++i)
        r.render();
    REQUIRE (r.engine.getNumActiveVoices() == 0);
}

TEST_CASE ("isUsing keeps the snapshot while its sample still plays")
{
    Rig r;
    r.addGroup();
    r.addZone (0, 60, 0.5f);
    r.commit();

    Instrument other = r.inst;

    REQUIRE (r.engine.isUsing (&r.inst));        // он текущий
    REQUIRE_FALSE (r.engine.isUsing (&other));   // не текущий и не играет

    r.engine.noteOn (60, 100);
    r.engine.setInstrument (&other);
    REQUIRE (r.engine.isUsing (&r.inst));        // уже не текущий, но его сэмпл звучит

    r.engine.prepare (kRate);                    // голоса сброшены
    REQUIRE_FALSE (r.engine.isUsing (&r.inst));
}

TEST_CASE ("preview plays into Main and can be stopped with a fade")
{
    Rig r;
    auto sample = makeSample (48000, kRate, 1, [] (int, int) { return 0.5f; });
    r.engine.setPreviewSample (sample.get());

    r.engine.previewOn();
    r.render (0, 512);
    REQUIRE (std::abs (r.L[0][400] - 0.5f) < 1e-4f);

    r.engine.stopPreview();
    r.render (0, 512);
    REQUIRE (r.engine.getNumActiveVoices() == 0);
}

TEST_CASE ("active keys report sounding notes")
{
    Rig r;
    r.addGroup();
    r.addZone (0, 36, 0.5f);
    r.addZone (0, 100, 0.5f);
    r.commit();

    r.engine.noteOn (36, 100);
    r.engine.noteOn (100, 100);
    r.engine.publishActivity();
    const auto keys = r.engine.getActiveKeys();

    REQUIRE (keys[0] == (1ull << 36));
    REQUIRE (keys[1] == (1ull << (100 - 64)));
}

TEST_CASE ("note names follow the Cubase convention")
{
    REQUIRE (noteName (60) == "C3");
    REQUIRE (noteName (36) == "C1");
    REQUIRE (noteName (61) == "C#3");
    REQUIRE (noteName (0) == "C-2");
}
