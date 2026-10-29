#include "PluginProcessor.h"
#include "kit_fixture.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace samplit;
using namespace kittest;

// Процессор с китом (setKit): звук, раскладка по выходам, состояние проекта. Шины и раскладки проверяет kit_engine_tests

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

TEST_CASE ("garbage state data does not change the preset name", "[processor]")
{
    SamplitProcessor p;
    p.setPresetName ("Studio600 Rock");

    const char junk[] = "definitely not a plugin state";
    p.setStateInformation (junk, (int) sizeof (junk));

    REQUIRE (p.getPresetName() == "Studio600 Rock");
}

TEST_CASE ("a project saved by the old sampler (GROUP, ZONE, folder) still loads", "[processor]")
{
    // Состояние нынешнего формата: пресет, микрофон, переключатель выбора удара и громкость барабана
    juce::ValueTree modern ("SAMPLIT");
    modern.setProperty ("version", 2, nullptr);
    modern.setProperty ("preset", "legacy-test-preset", nullptr);
    {
        juce::ValueTree mic ("MIC");
        mic.setProperty ("id", "kick_in", nullptr);
        mic.setProperty ("level", -4.0f, nullptr);
        mic.setProperty ("pan", 0.5f, nullptr);
        mic.setProperty ("mute", true, nullptr);
        mic.setProperty ("out", 3, nullptr);
        modern.appendChild (mic, nullptr);

        juce::ValueTree pick ("PICK");
        pick.setProperty ("id", "snare", nullptr);
        pick.setProperty ("mode", pickToString (Pick::Cycle), nullptr);
        modern.appendChild (pick, nullptr);

        juce::ValueTree piece ("PIECE");
        piece.setProperty ("id", "kick", nullptr);
        piece.setProperty ("level", -6.0f, nullptr);
        modern.appendChild (piece, nullptr);
    }

    // Сначала сохраняем состояние самим процессором, как это делает хост
    juce::MemoryBlock saved;
    {
        SamplitProcessor a;
        juce::MemoryBlock seed;
        const auto seedXml = modern.createXml();
        SamplitProcessor::copyXmlToBinary (*seedXml, seed);
        a.setStateInformation (seed.getData(), (int) seed.getSize());
        a.getStateInformation (saved);
    }

    // Старый проект: то же состояние плюс узлы GROUP, ZONE и атрибут folder
    const auto temp = juce::File::getSpecialLocation (juce::File::tempDirectory);
    auto xml = SamplitProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
    REQUIRE (xml != nullptr);
    xml->setAttribute ("folder", temp.getFullPathName());
    auto* group = xml->createNewChildElement ("GROUP");
    group->setAttribute ("name", "Claps");
    group->setAttribute ("output", 5);
    group->setAttribute ("mode", 1);
    auto* zone = xml->createNewChildElement ("ZONE");
    zone->setAttribute ("file", temp.getChildFile ("a.wav").getFullPathName());
    zone->setAttribute ("group", 0);
    zone->setAttribute ("lowKey", 60);

    juce::MemoryBlock legacy;
    SamplitProcessor::copyXmlToBinary (*xml, legacy);

    SamplitProcessor b;
    b.setStateInformation (legacy.getData(), (int) legacy.getSize());
    REQUIRE (b.getPresetName() == "legacy-test-preset");

    // Нужное восстановилось, а старые узлы и атрибут в новом состоянии не появились
    juce::MemoryBlock again;
    b.getStateInformation (again);
    const auto out = SamplitProcessor::getXmlFromBinary (again.getData(), (int) again.getSize());
    REQUIRE (out != nullptr);
    CHECK (out->getStringAttribute ("preset") == "legacy-test-preset");
    CHECK_FALSE (out->hasAttribute ("folder"));
    CHECK (out->getChildByName ("GROUP") == nullptr);
    CHECK (out->getChildByName ("ZONE") == nullptr);

    const auto* mic = out->getChildByName ("MIC");
    REQUIRE (mic != nullptr);
    CHECK (mic->getStringAttribute ("id") == "kick_in");
    CHECK (mic->getDoubleAttribute ("level") == -4.0);
    CHECK (mic->getDoubleAttribute ("pan") == 0.5);
    CHECK (mic->getBoolAttribute ("mute"));
    CHECK (mic->getIntAttribute ("out") == 3);

    const auto* pick = out->getChildByName ("PICK");
    REQUIRE (pick != nullptr);
    CHECK (pick->getStringAttribute ("id") == "snare");
    CHECK (pick->getStringAttribute ("mode") == pickToString (Pick::Cycle));

    const auto* piece = out->getChildByName ("PIECE");
    REQUIRE (piece != nullptr);
    CHECK (piece->getStringAttribute ("id") == "kick");
    CHECK (piece->getDoubleAttribute ("level") == -6.0);
}
