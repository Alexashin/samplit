#include "PluginProcessor.h"
#include "Studio600Names.h"
#include "kit_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace samplit;
using namespace kittest;

// Процессор с китом (setKit): звук, раскладка по выходам, состояние проекта. Шины и раскладки проверяет kit_engine_tests

TEST_CASE ("processor: Studio600 mono buses use microphone names", "[processor]")
{
    SamplitProcessor processor;
    REQUIRE (processor.getBusCount (false) == 17);
    CHECK (processor.getBus (false, 0)->getName() == "Main");
    for (int i = 1; i <= kNumOutputs; ++i)
    {
        const auto* bus = processor.getBus (false, i);
        REQUIRE (bus != nullptr);
        CHECK (bus->getName() == juce::String::fromUTF8 (studio600OutputName (i)));
    }
}

TEST_CASE ("processor without a kit is silent", "[processor]")
{
    KitFixture f;
    REQUIRE (f.processor.getKit() == nullptr);

    f.processor.triggerNote (36, 100);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 36, 1.0f), 0);
    auto out = f.run (midi);

    for (int ch = 0; ch < out.getNumChannels(); ++ch)
        REQUIRE (KitFixture::peak (out, ch) == 0.0f);
    REQUIRE (f.processor.getActiveKeys()[0] == 0);
    REQUIRE (f.processor.getEnabledOutputMask() == 0u);
}

TEST_CASE ("processor: a MIDI note starts the kit at its sample position inside the block", "[processor]")
{
    KitFixture f;
    f.processor.setKit (f.kit);

    auto out = f.play (36, 1.0f, 200);

    for (int ch = 0; ch < 2; ++ch)
    {
        REQUIRE (KitFixture::peak (out, ch, 0, 200) == 0.0f);
        REQUIRE (KitFixture::peak (out, ch, 200, 400) > 0.0f);
    }
    REQUIRE (f.processor.getEnabledOutputMask() == 0u);
}

TEST_CASE ("processor: two mics on one output are summed there", "[processor]")
{
    KitFixture f { 1 };   // каналы: Main = 0/1, Out 1 = 2
    f.processor.setKit (KitFixture::makeKit ({ 1, 1, 0 }));

    auto out = f.play (36);

    REQUIRE (std::abs (KitFixture::peak (out, 2) - 0.6f) < 1e-3f);   // два микрофона по 0,3
    REQUIRE (std::abs (KitFixture::peak (out, 0) - 0.3f) < 1e-3f);   // room в Main
    REQUIRE (f.processor.getEnabledOutputMask() == 0b1u);
}

TEST_CASE ("processor: a mic with an explicit Main output plays into Main even when Out 1 is enabled", "[processor]")
{
    KitFixture f { 1 };
    f.processor.setKit (KitFixture::makeKit ({ 0, 1, 0 }));

    auto out = f.play (36);

    REQUIRE (std::abs (KitFixture::peak (out, 0) - 0.6f) < 1e-3f);   // kick_in и room
    REQUIRE (std::abs (KitFixture::peak (out, 2) - 0.3f) < 1e-3f);   // kick_out на Out 1
}

TEST_CASE ("processor: a note from the on-screen keyboard plays from the start of the next block", "[processor]")
{
    KitFixture f;
    f.processor.setKit (f.kit);
    juce::MidiBuffer midi;

    f.processor.triggerNote (36, 127);
    auto out = f.run (midi);

    REQUIRE (KitFixture::peak (out, 0, 0, 64) > 0.0f);
    REQUIRE (f.processor.getActiveKeys()[0] == (1ull << 36));
}

TEST_CASE ("processor: the buffer is overwritten, not mixed with what the host left in it", "[processor]")
{
    KitFixture f { 1 };
    f.processor.setKit (f.kit);

    juce::AudioBuffer<float> buffer (f.processor.getTotalNumOutputChannels(), kFixtureBlock);
    juce::MidiBuffer midi;
    const auto fill = [&]
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < kFixtureBlock; ++i)
                buffer.setSample (ch, i, 0.75f);
    };

    fill();
    f.processor.processBlock (buffer, midi);   // нот нет: мусор должен исчезнуть везде
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        REQUIRE (KitFixture::peak (buffer, ch) == 0.0f);

    fill();
    midi.addEvent (juce::MidiMessage::noteOn (1, 36, 1.0f), 0);
    f.processor.processBlock (buffer, midi);   // с нотой: ровно уровни микрофонов, без остатка мусора
    REQUIRE (std::abs (KitFixture::peak (buffer, 0) - 0.6f) < 1e-3f);   // room и kick_out (Out 2 выключен) в Main
    REQUIRE (std::abs (KitFixture::peak (buffer, 2) - 0.3f) < 1e-3f);   // kick_in на Out 1
}

TEST_CASE ("garbage state data does not change the setup", "[processor]")
{
    SamplitProcessor p;
    const auto before = p.getSetup().toJson();

    const char junk[] = "definitely not a plugin state";
    p.setStateInformation (junk, (int) sizeof (junk));

    REQUIRE (p.getSetup().toJson() == before);
}
