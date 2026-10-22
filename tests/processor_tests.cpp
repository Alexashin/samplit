#include "PluginProcessor.h"
#include "test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace samplit;
using samplit::test::makeSample;

namespace
{
    constexpr double kRate = 48000.0;
    constexpr int kBlock = 512;

    // Процессор в состоянии «хост запустил проигрывание»: шины включены по списку, prepare вызван,
    // на каждом пэде секундный сэмпл постоянного уровня
    struct Fixture
    {
        explicit Fixture (std::initializer_list<int> enabledAuxBuses = {}, bool loadPads = true)
        {
            processor.setRateAndBufferSizeDetails (kRate, kBlock);
            for (int bus : enabledAuxBuses)
                REQUIRE (processor.getBus (false, bus)->enable (true));
            processor.prepareToPlay (kRate, kBlock);
            if (loadPads)
                for (int pad = 0; pad < kNumPads; ++pad)
                    processor.assignSample (pad, makeSample (48000));
        }

        juce::AudioBuffer<float> run (juce::MidiBuffer& midi)
        {
            juce::AudioBuffer<float> buffer (processor.getTotalNumOutputChannels(), kBlock);
            processor.processBlock (buffer, midi);
            return buffer;
        }

        // Один блок с нотой пэда в сэмпле `at`
        juce::AudioBuffer<float> play (int pad, int at = 0, float velocity = 1.0f)
        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, kFirstPadNote + pad, velocity), at);
            return run (midi);
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

    juce::File writeTestWav (const juce::String& name, double rate, int channels, int length)
    {
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("samplit-tests").getChildFile (name);
        file.getParentDirectory().createDirectory();
        file.deleteFile();

        juce::AudioBuffer<float> audio (channels, length);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < length; ++i)
                audio.setSample (ch, i, 0.25f * std::sin ((float) i * 0.05f));

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto options = juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (channels).withBitsPerSample (16);
        auto writer = wav.createWriterFor (stream, options);
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (audio, 0, length);
        return file;
    }
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

TEST_CASE ("preview always plays into Main, even with outputs enabled")
{
    Fixture f { 1 };
    f.processor.assignSample (kPreviewVoice, makeSample (4800));
    f.processor.triggerPad (kPreviewVoice);

    juce::MidiBuffer midi;
    auto out = f.run (midi);

    REQUIRE (Fixture::peak (out, 0) > 0.0f);
    REQUIRE (Fixture::peak (out, 2) == 0.0f);
}

TEST_CASE ("empty pad is silent")
{
    Fixture f ({}, false);
    auto out = f.play (0);
    REQUIRE (Fixture::peak (out, 0) == 0.0f);
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
        juce::MidiBuffer midi;
        midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);
        auto out = f.run (midi);
        REQUIRE (Fixture::peak (out, 0) == 0.0f);
    }
    REQUIRE (f.processor.getActiveMask() == 0u);
}

TEST_CASE ("pad triggered from the UI plays at the start of the next block")
{
    Fixture f;
    juce::MidiBuffer midi;

    f.processor.triggerPad (4);
    auto out = f.run (midi);

    REQUIRE (Fixture::peak (out, 0, 0, 64) > 0.0f);
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
}

TEST_CASE ("replaced sample is freed only after it stops playing")
{
    Fixture f;
    f.play (0);  // пэд 1 звучит

    f.processor.assignSample (0, makeSample (48000));
    REQUIRE (f.processor.getRetiredCount() == 1);

    f.processor.collectGarbage (true);
    REQUIRE (f.processor.getRetiredCount() == 1);  // старый ещё играет

    juce::MidiBuffer midi;
    for (int i = 0; i < 100; ++i)  // ~1 с: старый сэмпл доиграл
        f.run (midi);
    f.processor.collectGarbage (true);
    REQUIRE (f.processor.getRetiredCount() == 0);
}

TEST_CASE ("readSample loads WAV files and rejects the rest")
{
    juce::AudioFormatManager fm;
    fm.registerBasicFormats();

    SECTION ("mono 44.1 kHz")
    {
        auto s = SamplitProcessor::readSample (fm, writeTestWav ("mono.wav", 44100.0, 1, 4410));
        REQUIRE (s != nullptr);
        REQUIRE (s->audio.getNumChannels() == 1);
        REQUIRE (s->audio.getNumSamples() == 4410);
        REQUIRE (s->sampleRate == 44100.0);
    }
    SECTION ("stereo 48 kHz")
    {
        auto s = SamplitProcessor::readSample (fm, writeTestWav ("stereo.wav", 48000.0, 2, 4800));
        REQUIRE (s != nullptr);
        REQUIRE (s->audio.getNumChannels() == 2);
        REQUIRE (s->getLengthSeconds() == 0.1);
    }
    SECTION ("not an audio file")
    {
        auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("samplit-tests/notes.wav");
        file.getParentDirectory().createDirectory();
        file.replaceWithText ("not audio");
        REQUIRE (SamplitProcessor::readSample (fm, file) == nullptr);
    }
    SECTION ("longer than 60 seconds")
    {
        REQUIRE (SamplitProcessor::readSample (fm, writeTestWav ("long.wav", 8000.0, 1, 8000 * 61)) == nullptr);
    }
}

TEST_CASE ("pad files and folder survive save and load of the project")
{
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("samplit-tests");
    juce::MemoryBlock saved;
    {
        SamplitProcessor a;
        a.loadPadSample (0, tmp.getChildFile ("kick.wav"));
        a.loadPadSample (5, tmp.getChildFile ("808.wav"));
        a.setLastFolder (tmp);
        a.getStateInformation (saved);
    }

    SamplitProcessor b;
    b.loadPadSample (3, tmp.getChildFile ("old.wav"));  // в проекте этого пэда нет, он должен очиститься
    b.setStateInformation (saved.getData(), (int) saved.getSize());

    REQUIRE (b.getPadFile (0) == tmp.getChildFile ("kick.wav"));
    REQUIRE (b.getPadFile (5) == tmp.getChildFile ("808.wav"));
    REQUIRE (b.getPadFile (3) == juce::File());
    REQUIRE (b.getLastFolder() == tmp);
}

TEST_CASE ("garbage state data is ignored")
{
    SamplitProcessor p;
    p.loadPadSample (0, juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("kick.wav"));

    const char junk[] = "definitely not a plugin state";
    p.setStateInformation (junk, (int) sizeof (junk));

    REQUIRE (p.getPadFile (0).getFileName() == "kick.wav");
}

TEST_CASE ("pad settings are clamped and survive save and load")
{
    juce::MemoryBlock saved;
    {
        SamplitProcessor a;
        a.setPadParams (2, { -6.0f, 3.0f, -0.5f });
        a.setPadParams (7, { 40.0f, -30.0f, 2.0f });  // за пределами: должно обрезаться
        a.getStateInformation (saved);

        REQUIRE (a.getPadParams (7).gainDb == 6.0f);
        REQUIRE (a.getPadParams (7).pitch == -12.0f);
        REQUIRE (a.getPadParams (7).pan == 1.0f);
    }

    SamplitProcessor b;
    b.setPadParams (4, { -3.0f, 0.0f, 0.0f });  // в проекте не задан, должен сброситься
    b.setStateInformation (saved.getData(), (int) saved.getSize());

    REQUIRE (b.getPadParams (2).gainDb == -6.0f);
    REQUIRE (b.getPadParams (2).pitch == 3.0f);
    REQUIRE (b.getPadParams (2).pan == -0.5f);
    REQUIRE (b.getPadParams (7).gainDb == 6.0f);
    REQUIRE (b.getPadParams (4).isDefault());
}

TEST_CASE ("pad gain in dB reaches the output")
{
    Fixture a, b;
    a.processor.setPadParams (0, { -6.0f, 0.0f, 0.0f });
    auto quiet = a.play (0);
    auto normal = b.play (0);

    const float ratio = Fixture::peak (quiet, 0) / Fixture::peak (normal, 0);
    REQUIRE (std::abs (ratio - 0.501f) < 0.01f);
}
