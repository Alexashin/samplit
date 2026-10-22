#include "PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using samplit::SamplitProcessor;
using samplit::kFirstPadNote;
using samplit::kNumPads;

namespace
{
    constexpr double kRate = 48000.0;
    constexpr int kBlock = 512;

    // Процессор в состоянии «хост запустил проигрывание»: шины включены по списку, prepare вызван
    struct Fixture
    {
        explicit Fixture (std::initializer_list<int> enabledAuxBuses = {})
        {
            processor.setRateAndBufferSizeDetails (kRate, kBlock);
            for (int bus : enabledAuxBuses)
                REQUIRE (processor.getBus (false, bus)->enable (true));
            processor.prepareToPlay (kRate, kBlock);
        }

        // Один блок с нотой пэда в сэмпле `at`; возвращает буфер со всеми включёнными шинами
        juce::AudioBuffer<float> play (int pad, int at = 0, float velocity = 1.0f)
        {
            juce::AudioBuffer<float> buffer (processor.getTotalNumOutputChannels(), kBlock);
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, kFirstPadNote + pad, velocity), at);
            processor.processBlock (buffer, midi);
            return buffer;
        }

        static float peak (const juce::AudioBuffer<float>& b, int channel, int from = 0, int to = -1)
        {
            if (to < 0)
                to = b.getNumSamples();
            float p = 0.0f;
            for (int i = from; i < to; ++i)
                p = std::max (p, std::abs (b.getSample (channel, i)));
            return p;
        }

        SamplitProcessor processor;
    };
}

TEST_CASE ("plugin declares Main plus one stereo output per pad")
{
    SamplitProcessor p;

    REQUIRE (p.getBusCount (false) == 1 + kNumPads);
    REQUIRE (p.getBusCount (true) == 0);
    REQUIRE (p.getBus (false, 0)->isEnabled());

    for (int bus = 1; bus <= kNumPads; ++bus)
    {
        REQUIRE_FALSE (p.getBus (false, bus)->isEnabled());
        REQUIRE (p.getBus (false, bus)->getDefaultLayout() == juce::AudioChannelSet::stereo());
    }
}

TEST_CASE ("bus layouts: stereo only, Main required")
{
    SamplitProcessor p;
    auto layout = p.getBusesLayout();

    REQUIRE (p.checkBusesLayoutSupported (layout));

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
    SECTION ("mono extra output is rejected")
    {
        layout.outputBuses.getReference (1) = juce::AudioChannelSet::mono();
        REQUIRE_FALSE (p.checkBusesLayoutSupported (layout));
    }
    SECTION ("all stereo outputs are accepted")
    {
        for (auto& out : layout.outputBuses)
            out = juce::AudioChannelSet::stereo();
        REQUIRE (p.checkBusesLayoutSupported (layout));
    }
}

TEST_CASE ("without extra outputs every pad plays into Main")
{
    Fixture f;

    for (int pad = 0; pad < kNumPads; ++pad)
    {
        auto out = f.play (pad);
        REQUIRE (Fixture::peak (out, 0) > 0.0f);
        REQUIRE (Fixture::peak (out, 1) > 0.0f);
    }
    REQUIRE (f.processor.getSeparateOutMask() == 0u);
}

TEST_CASE ("pad goes to its own output when the host enables it")
{
    Fixture f { 1, 2 };  // Out 1 и Out 2 включены, остальные нет
    // каналы: Main = 0/1, Out 1 = 2/3, Out 2 = 4/5

    SECTION ("pad 1 plays only on Out 1")
    {
        auto out = f.play (0);
        REQUIRE (Fixture::peak (out, 0) == 0.0f);
        REQUIRE (Fixture::peak (out, 2) > 0.0f);
        REQUIRE (Fixture::peak (out, 3) > 0.0f);
        REQUIRE (Fixture::peak (out, 4) == 0.0f);
    }
    SECTION ("pad 2 plays only on Out 2")
    {
        auto out = f.play (1);
        REQUIRE (Fixture::peak (out, 0) == 0.0f);
        REQUIRE (Fixture::peak (out, 2) == 0.0f);
        REQUIRE (Fixture::peak (out, 4) > 0.0f);
    }
    SECTION ("pad 3 has no output enabled and falls back to Main")
    {
        auto out = f.play (2);
        REQUIRE (Fixture::peak (out, 0) > 0.0f);
        REQUIRE (Fixture::peak (out, 2) == 0.0f);
        REQUIRE (Fixture::peak (out, 4) == 0.0f);
    }

    REQUIRE (f.processor.getSeparateOutMask() == 0b11u);
}

TEST_CASE ("MIDI note starts the sound at its sample position")
{
    Fixture f;
    auto out = f.play (0, 200);

    REQUIRE (Fixture::peak (out, 0, 0, 200) == 0.0f);
    REQUIRE (Fixture::peak (out, 0, 200, 300) > 0.0f);
}

TEST_CASE ("notes outside the pad range are ignored")
{
    Fixture f;

    for (int note : { kFirstPadNote - 1, kFirstPadNote + kNumPads, 0, 127 })
    {
        juce::AudioBuffer<float> buffer (2, kBlock);
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);
        f.processor.processBlock (buffer, midi);
        REQUIRE (Fixture::peak (buffer, 0) == 0.0f);
    }
    REQUIRE (f.processor.getActiveMask() == 0u);
}

TEST_CASE ("pad triggered from the UI plays at the start of the next block")
{
    Fixture f;
    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;

    f.processor.triggerPad (4);
    f.processor.processBlock (buffer, midi);

    REQUIRE (Fixture::peak (buffer, 0, 0, 64) > 0.0f);
    REQUIRE (f.processor.getActiveMask() == (1u << 4));
}

TEST_CASE ("buffer is cleared before rendering")
{
    Fixture f;
    juce::AudioBuffer<float> buffer (2, kBlock);
    juce::MidiBuffer midi;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < kBlock; ++i)
            buffer.setSample (ch, i, 0.75f);

    f.processor.processBlock (buffer, midi);

    REQUIRE (Fixture::peak (buffer, 0) == 0.0f);
    REQUIRE (Fixture::peak (buffer, 1) == 0.0f);
}
