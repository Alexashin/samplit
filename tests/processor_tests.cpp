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
    REQUIRE (processor.getBusCount (false) == 1 + kNumOutputs);
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

// ---------------------------------------------------------------- микшер, очередь нот, хвост, All Sound Off, bypass

namespace
{
    // Кит из 17 микрофонов (слоты 0, 1 и 16: файлы по 0,3; слот 16 лежит за параметрами хоста), все на Main
    std::shared_ptr<const Kit> makeWideKit()
    {
        KitBuilder b;
        for (int i = 0; i < 17; ++i)
            b.mic ("m" + std::to_string (i), 0);
        const int p = b.piece ("kick");
        std::vector<float> levels (17, -1.0f);
        levels[0] = levels[1] = levels[16] = 0.3f;
        b.hit (p, b.layer (p, 1, 127), 36, levels);
        return b.done();
    }

    void neutralMix (SamplitProcessor& pr, int slot, bool mute = false, bool solo = false)
    {
        SamplitProcessor::MicState m;
        m.out = 0;
        m.mute = mute;
        m.solo = solo;
        pr.setMicState (slot, m);
    }

    // Второй блок после смены микшера: рампа первого блока уже доиграна
    float settledPeak (KitFixture& f)
    {
        juce::MidiBuffer none;
        f.run (none);
        return KitFixture::peak (f.run (none), 0, 100);
    }
}

TEST_CASE ("processor: mute and solo reach the audio thread, including slots above the host parameters", "[processor][mixer]")
{
    KitFixture f;
    f.processor.setMultiOut (false);
    f.processor.setKit (makeWideKit());
    for (int slot : { 0, 1, 16 })
        neutralMix (f.processor, slot);
    f.play (36);
    CHECK (std::abs (settledPeak (f) - 0.9f) < 1e-3f);

    neutralMix (f.processor, 16, true);                  // mute слота 16 (mixLevelDb / mixPan)
    CHECK (std::abs (settledPeak (f) - 0.6f) < 1e-3f);

    neutralMix (f.processor, 16);
    neutralMix (f.processor, 1, false, true);            // solo слота 1: остальные молчат
    CHECK (std::abs (settledPeak (f) - 0.3f) < 1e-3f);

    neutralMix (f.processor, 1, true, true);             // mute сильнее solo
    neutralMix (f.processor, 16, false, true);
    CHECK (std::abs (settledPeak (f) - 0.3f) < 1e-3f);   // слышен только solo-слот 16
    neutralMix (f.processor, 16, true, true);
    CHECK (settledPeak (f) == 0.0f);
}

TEST_CASE ("processor: pan of a slot above the host parameters ramps without a jump", "[processor][mixer]")
{
    KitFixture f;
    f.processor.setMultiOut (false);
    f.processor.setKit (makeWideKit());
    for (int slot : { 0, 1 })
        neutralMix (f.processor, slot, true);
    neutralMix (f.processor, 16);
    juce::MidiBuffer none;
    f.play (36);
    f.run (none);

    SamplitProcessor::MicState m;
    m.pan = -1.0f;                                       // правый канал 0,3 -> 0 за блок (рампа 512)
    f.processor.setMicState (16, m);
    const auto out = f.run (none);
    float prev = out.getSample (1, 0);
    CHECK (prev > 0.29f);
    for (int i = 1; i < kFixtureBlock; ++i)
    {
        const float v = out.getSample (1, i);
        REQUIRE (v <= prev + 1e-5f);
        REQUIRE (prev - v < 0.3f / 400.0f);
        prev = v;
    }
    CHECK (prev < 0.01f);
    CHECK (out.getSample (0, kFixtureBlock - 1) > 0.29f);
}

TEST_CASE ("processor: an empty block (numSamples = 0) is harmless", "[processor]")
{
    KitFixture f;
    f.processor.setKit (f.kit);
    f.play (36);
    juce::AudioBuffer<float> empty (f.processor.getTotalNumOutputChannels(), 0);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 36, 1.0f), 0);
    f.processor.processBlock (empty, midi);
    juce::MidiBuffer none;
    CHECK (KitFixture::peak (f.run (none), 0) > 0.0f);   // звук продолжается, состояние не сломано
}

TEST_CASE ("processor: setStateInformation applies the saved output mode to the engine at once", "[processor]")
{
    KitFixture a { 1 };
    a.processor.setKit (a.kit);
    a.processor.setMultiOut (true);
    juce::MemoryBlock state;
    a.processor.getStateInformation (state);

    KitFixture b { 1 };
    b.processor.setMultiOut (false);
    b.processor.setKit (b.kit);
    auto out = b.play (36);
    CHECK (KitFixture::peak (out, 2) == 0.0f);           // «Стерео»: Out 1 молчит
    b.processor.setStateInformation (state.getData(), (int) state.getSize());
    REQUIRE (b.processor.isMultiOut());
    out = b.play (36);
    CHECK (KitFixture::peak (out, 2) > 0.0f);            // applyMixer сработал сразу: kick_in ушёл на Out 1
}

TEST_CASE ("processor: UI edits write the host parameter inside a gesture, timer-style edits do not", "[processor][mixer]")
{
    struct Counter final : juce::AudioProcessorListener
    {
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override { ++changes; }
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override { ++begins; }
        void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int) override { ++ends; }
        int changes = 0, begins = 0, ends = 0;
    } c;

    SamplitProcessor p;
    p.addListener (&c);
    SamplitProcessor::MicState m;
    m.levelDb = -6.0f;
    p.setMicState (0, m);                                // как таймер или загрузка проекта
    CHECK (c.changes >= 1);
    CHECK (c.begins == 0);

    m.levelDb = -12.0f;
    p.setMicState (0, m, true);                          // правка мышью без открытого жеста: gesture на одну запись
    CHECK (c.begins == 1);
    CHECK (c.ends == 1);

    p.beginMicGesture (0, false);                        // перетаскивание ползунка: один gesture на весь жест
    m.levelDb = -18.0f;
    p.setMicState (0, m, true);
    m.levelDb = -20.0f;
    p.setMicState (0, m, true);
    p.endMicGesture (0, false);
    CHECK (c.begins == 2);
    CHECK (c.ends == 2);
    p.removeListener (&c);
}

TEST_CASE ("processor: closeAllGestures closes every open gesture (begin/end are paired)", "[processor][mixer]")
{
    struct Counter final : juce::AudioProcessorListener
    {
        void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override {}
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int) override { ++begins; }
        void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int) override { ++ends; }
        int begins = 0, ends = 0;
    } c;

    SamplitProcessor p;
    p.addListener (&c);
    p.beginMicGesture (0, false);
    p.beginMicGesture (3, true);
    CHECK (c.begins == 2);
    CHECK (c.ends == 0);
    p.closeAllGestures();
    CHECK (c.ends == 2);
    p.closeAllGestures();                                // повторно ничего не делает
    p.endMicGesture (0, false);                          // и поздний onDragEnd не даёт лишний end
    CHECK (c.begins == 2);
    CHECK (c.ends == 2);
    p.removeListener (&c);
}

TEST_CASE ("processor: after bypass the first block takes the mix target without a ramp", "[processor][mixer]")
{
    KitFixture f;
    f.processor.setMultiOut (false);
    f.processor.setKit (makeWideKit());
    for (int slot : { 0, 1 })
        neutralMix (f.processor, slot, true);
    neutralMix (f.processor, 16);
    juce::MidiBuffer none;
    f.play (36);
    f.run (none);                                        // движок «прогрет» старой панорамой

    juce::AudioBuffer<float> buffer (f.processor.getTotalNumOutputChannels(), kFixtureBlock);
    juce::MidiBuffer midi;
    f.processor.processBlockBypassed (buffer, midi);
    SamplitProcessor::MicState m;
    m.pan = -1.0f;
    f.processor.setMicState (16, m);
    f.play (36);
    const auto out = f.run (none);
    CHECK (std::abs (out.getSample (1, 0)) < 0.01f);     // сразу цель, не рампа от старого значения
}

TEST_CASE ("processor: notes queued while the audio thread stood still are not played as a volley", "[processor]")
{
    KitFixture f;
    f.processor.setKit (f.kit);
    for (int i = 0; i < 100; ++i)
        f.processor.triggerNote (36, 100);
    juce::MidiBuffer none;
    const auto out = f.run (none);
    // каждый удар даёт 0,9 в левый канал Main (три микрофона по 0,3): играют только последние kQueue = 64
    CHECK (std::abs (KitFixture::peak (out, 0, 0, 16) - 64 * 0.9f) < 0.05f);
}

TEST_CASE ("processor: the tail length is the longest clip of the kit", "[processor]")
{
    KitFixture f;
    CHECK (f.processor.getTailLengthSeconds() == 30.0);  // кита нет: безопасный минимум
    f.processor.setKit (f.kit);
    CHECK (std::abs (f.processor.getTailLengthSeconds() - 1.0) < 1e-6);   // клипы по 48000 кадров при 48 кГц
}

TEST_CASE ("processor: MIDI All Sound Off and All Notes Off fade everything within 10 ms", "[processor]")
{
    for (const bool soundOff : { true, false })
    {
        KitFixture f;
        f.processor.setKit (f.kit);
        f.play (36);

        juce::MidiBuffer midi;
        midi.addEvent (soundOff ? juce::MidiMessage::allSoundOff (1) : juce::MidiMessage::allNotesOff (1), 0);
        auto out = f.run (midi);
        CHECK (KitFixture::peak (out, 0, 0, 8) > 0.1f);              // затухание, а не обрыв
        CHECK (KitFixture::peak (out, 0, 500, kFixtureBlock) == 0.0f);

        juce::MidiBuffer none;
        CHECK (KitFixture::peak (f.run (none), 0) == 0.0f);
        f.processor.collectGarbage();
    }
}

TEST_CASE ("processor: bypass drops frozen voices and queued notes", "[processor]")
{
    KitFixture f;
    f.processor.setKit (f.kit);
    f.play (36);
    f.processor.triggerNote (36, 100);

    juce::AudioBuffer<float> buffer (f.processor.getTotalNumOutputChannels(), kFixtureBlock);
    juce::MidiBuffer midi;
    buffer.setSample (0, 0, 1.0f);
    f.processor.processBlockBypassed (buffer, midi);
    CHECK (KitFixture::peak (buffer, 0) == 0.0f);

    juce::MidiBuffer none;
    CHECK (KitFixture::peak (f.run (none), 0) == 0.0f);              // после снятия bypass хвосты не доигрываются
}

TEST_CASE ("processor: releaseResources releases the strikes so replaced kits can be freed", "[processor]")
{
    KitFixture f;
    f.processor.setKit (f.kit);
    f.play (36);
    f.processor.setKit (KitFixture::makeKit ({ 0, 0, 0 }));
    f.processor.collectGarbage();
    REQUIRE (f.processor.getRetiredCount() == 1);                    // старый кит ещё звучит

    f.processor.releaseResources();
    f.processor.collectGarbage();
    CHECK (f.processor.getRetiredCount() == 0);
}
