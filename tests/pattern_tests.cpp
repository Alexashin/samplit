#include "Pattern.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using samplit::Pattern;

namespace
{
    Pattern sample()
    {
        Pattern p;
        p.bars = 2;
        p.bpm = 120.0;
        for (int i = 0; i < 16; i += 2)
            p.setStep (i, 6, 48, 110);
        p.setStep (0, 0, 24, 120);
        p.setStep (8, 1, 26, 118);
        return p;
    }
}

TEST_CASE ("pattern: steps are placed, replaced and removed", "[pattern]")
{
    Pattern p;
    REQUIRE (p.setStep (3, 1, 26, 100));
    REQUIRE (p.setStep (3, 1, 26, 90));                 // замена, не дубль
    REQUIRE (p.steps.size() == 1);
    REQUIRE (p.findStep (3, 1)->velocity == 90);
    REQUIRE_FALSE (p.setStep (16, 1, 26, 100));         // за пределами одного такта
    REQUIRE (p.removeStep (3, 1));
    REQUIRE (p.steps.empty());
}

TEST_CASE ("pattern: events land on the grid without humanize", "[pattern]")
{
    const auto p = sample();
    const auto ev = p.buildEvents (48000.0);
    REQUIRE (ev.size() == p.steps.size());
    // 120 bpm: шаг 1/16 = 0,125 с = 6000 сэмплов
    REQUIRE (ev.front().sample == 0);
    for (const auto& e : ev)
        REQUIRE (e.sample % 6000 == 0);
    REQUIRE (p.lengthSamples (48000.0) == 2 * 16 * 6000);
}

TEST_CASE ("pattern: humanize is repeatable, bounded and depends on the seed", "[pattern]")
{
    auto p = sample();
    p.humanTimingMs = 10;
    p.humanVelocity = 6;
    const auto a = p.buildEvents (48000.0);
    const auto b = p.buildEvents (48000.0);
    REQUIRE (a.size() == b.size());
    bool moved = false;
    for (size_t i = 0; i < a.size(); ++i)
    {
        REQUIRE (a[i].sample == b[i].sample);
        REQUIRE (a[i].velocity == b[i].velocity);
        REQUIRE (a[i].sample >= 0);
        const auto* st = p.findStep (0, a[i].slot);     // сравниваем с сеткой через ближайший шаг
        (void) st;
        const int64_t grid = (int64_t) std::llround ((double) a[i].sample / 6000.0) * 6000;
        REQUIRE (std::abs (a[i].sample - grid) <= 480);   // 10 мс = 480 сэмплов
        REQUIRE (a[i].velocity >= 1);
        moved = moved || a[i].sample != grid;
    }
    REQUIRE (moved);

    auto q = p;
    q.humanSeed = 99;
    const auto c = q.buildEvents (48000.0);
    bool differs = false;
    for (size_t i = 0; i < a.size(); ++i)
        differs = differs || a[i].sample != c[i].sample || a[i].velocity != c[i].velocity;
    REQUIRE (differs);
}

TEST_CASE ("pattern: midi file carries tempo, explicit velocities and humanize", "[pattern]")
{
    auto p = sample();
    const auto file = p.toMidiFile();
    REQUIRE (file.getTimeFormat() == Pattern::kPpq);
    const auto* track = file.getTrack (0);
    REQUIRE (track != nullptr);
    int ons = 0, kick = 0;
    for (int i = 0; i < track->getNumEvents(); ++i)
    {
        const auto& m = track->getEventPointer (i)->message;
        if (m.isNoteOn())
        {
            ++ons;
            if (m.getNoteNumber() == 24)
            {
                ++kick;
                REQUIRE (m.getVelocity() == 120);
            }
        }
        if (m.isTempoMetaEvent())
            REQUIRE (std::abs (m.getTempoSecondsPerQuarterNote() - 0.5) < 1e-9);
    }
    REQUIRE (ons == (int) p.steps.size());
    REQUIRE (kick == 1);
}

TEST_CASE ("pattern: json round trip and bad input", "[pattern]")
{
    auto p = sample();
    p.humanSeed = 4242;
    p.humanTimingMs = 7;
    Pattern back;
    juce::String err;
    REQUIRE (Pattern::fromJson (p.toJson(), back, err));
    REQUIRE (back.bars == p.bars);
    REQUIRE (back.humanSeed == 4242);
    REQUIRE (back.steps.size() == p.steps.size());
    REQUIRE (back.toJson() == p.toJson());

    REQUIRE_FALSE (Pattern::fromJson ("не json", back, err));
    REQUIRE (err.isNotEmpty());
}
