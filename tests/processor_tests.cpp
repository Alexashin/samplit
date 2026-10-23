#include "PluginProcessor.h"
#include "test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace samplit;

namespace
{
    constexpr double kRate = 48000.0;
    constexpr int kBlock = 512;

    juce::File testDir()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("samplit-tests");
        dir.createDirectory();
        return dir;
    }

    void writeWav (const juce::File& file, double rate, int channels, int length, std::function<float (int)> value)
    {
        file.getParentDirectory().createDirectory();
        file.deleteFile();

        juce::AudioBuffer<float> audio (channels, length);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < length; ++i)
                audio.setSample (ch, i, value (i));

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        auto options = juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (channels).withBitsPerSample (16);
        auto writer = wav.createWriterFor (stream, options);
        REQUIRE (writer != nullptr);
        writer->writeFromAudioSampleBuffer (audio, 0, length);
    }

    // Секундный файл постоянного уровня
    juce::File constWav (const juce::String& name, float level = 0.5f, int length = 48000)
    {
        const auto file = testDir().getChildFile (name);
        writeWav (file, kRate, 1, length, [level] (int) { return level; });
        return file;
    }

    // Крутит message loop, пока фоновая загрузка не закончится
    bool waitForLoading (SamplitProcessor& p, int timeoutMs = 5000)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (p.getNumLoading() > 0 && juce::Time::getMillisecondCounter() < end)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        return p.getNumLoading() == 0;
    }

    // Процессор в состоянии «хост запустил проигрывание»: включены только шины из списка
    struct Fixture
    {
        explicit Fixture (std::initializer_list<int> enabledAuxBuses = {})
        {
            processor.setRateAndBufferSizeDetails (kRate, kBlock);
            for (int bus : enabledAuxBuses)
                REQUIRE (processor.getBus (false, bus)->enable (true));
            processor.prepareToPlay (kRate, kBlock);
        }

        // Добавляет файл на клавишу группы и ждёт загрузки
        void put (const juce::File& file, int group, int key, Spread spread = Spread::Variations)
        {
            REQUIRE (processor.addFilesToKey ({ file }, group, key, spread) == 1);
            REQUIRE (waitForLoading (processor));
        }

        juce::AudioBuffer<float> run (juce::MidiBuffer& midi)
        {
            juce::AudioBuffer<float> buffer (processor.getTotalNumOutputChannels(), kBlock);
            processor.processBlock (buffer, midi);
            return buffer;
        }

        juce::AudioBuffer<float> play (int note, int at = 0, float velocity = 1.0f)
        {
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, note, velocity), at);
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
}

TEST_CASE ("plugin declares stereo Main plus one mono output per group slot")
{
    SamplitProcessor p;

    REQUIRE (p.getBusCount (false) == 1 + kNumOutputs);
    REQUIRE (p.getBusCount (true) == 0);
    REQUIRE (p.getBus (false, 0)->isEnabled());

    for (int bus = 1; bus <= kNumOutputs; ++bus)
    {
        REQUIRE_FALSE (p.getBus (false, bus)->isEnabled());
        REQUIRE (p.getBus (false, bus)->getDefaultLayout() == juce::AudioChannelSet::mono());
    }
}

TEST_CASE ("bus layouts: stereo Main required, extra outputs mono")
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
    SECTION ("stereo extra output is rejected")
    {
        layout.outputBuses.getReference (1) = juce::AudioChannelSet::stereo();
        REQUIRE_FALSE (p.checkBusesLayoutSupported (layout));
    }
    SECTION ("all extra outputs mono is accepted")
    {
        for (int i = 1; i < layout.outputBuses.size(); ++i)
            layout.outputBuses.getReference (i) = juce::AudioChannelSet::mono();
        REQUIRE (p.checkBusesLayoutSupported (layout));
    }
}

TEST_CASE ("a new plugin has one empty group routed to Out 1")
{
    SamplitProcessor p;
    const auto inst = p.getInstrument();

    REQUIRE (inst->groups.size() == 1);
    REQUIRE (inst->zones.empty());
    REQUIRE (inst->groups[0].output == 1);
}

TEST_CASE ("without extra outputs everything plays into Main")
{
    Fixture f;
    f.processor.addGroup ("Claps");
    f.put (constWav ("a.wav"), 0, 60);
    f.put (constWav ("b.wav"), 1, 61);

    for (int note : { 60, 61 })
    {
        auto out = f.play (note);
        REQUIRE (Fixture::peak (out, 0) > 0.0f);
        REQUIRE (Fixture::peak (out, 1) > 0.0f);
    }
    REQUIRE (f.processor.getEnabledOutputMask() == 0u);
}

TEST_CASE ("a group goes to its own output when the host enables it")
{
    Fixture f { 1, 2 };  // Out 1 и Out 2 включены, остальные нет. Каналы: Main = 0/1, Out 1 = 2, Out 2 = 3 (моно)
    f.processor.addGroup ("G2");
    f.processor.addGroup ("G3");
    f.put (constWav ("a.wav"), 0, 60);
    f.put (constWav ("b.wav"), 1, 61);
    f.put (constWav ("c.wav"), 2, 62);

    SECTION ("group 1 plays only on Out 1")
    {
        auto out = f.play (60);
        REQUIRE (Fixture::peak (out, 0) == 0.0f);
        REQUIRE (Fixture::peak (out, 1) == 0.0f);
        REQUIRE (Fixture::peak (out, 2) > 0.0f);
        REQUIRE (Fixture::peak (out, 3) == 0.0f);
    }
    SECTION ("group 2 plays only on Out 2")
    {
        auto out = f.play (61);
        REQUIRE (Fixture::peak (out, 0) == 0.0f);
        REQUIRE (Fixture::peak (out, 2) == 0.0f);
        REQUIRE (Fixture::peak (out, 3) > 0.0f);
    }
    SECTION ("group 3 has its output switched off and falls back to Main")
    {
        auto out = f.play (62);
        REQUIRE (Fixture::peak (out, 0) > 0.0f);
        REQUIRE (Fixture::peak (out, 2) == 0.0f);
        REQUIRE (Fixture::peak (out, 3) == 0.0f);
    }
    SECTION ("the user can send a group to Main explicitly")
    {
        auto g = f.processor.getInstrument()->groups[0];
        g.output = 0;
        f.processor.setGroup (0, g);
        auto out = f.play (60);
        REQUIRE (Fixture::peak (out, 0) > 0.0f);
        REQUIRE (Fixture::peak (out, 2) == 0.0f);
    }

    REQUIRE (f.processor.getEnabledOutputMask() == 0b11u);
}

TEST_CASE ("several groups can share one output")
{
    Fixture f { 1 };
    f.processor.addGroup ("G2");
    f.put (constWav ("a.wav"), 0, 60);
    f.put (constWav ("b.wav"), 1, 61);
    auto g = f.processor.getInstrument()->groups[1];
    g.output = 1;
    f.processor.setGroup (1, g);

    REQUIRE (Fixture::peak (f.play (60), 2) > 0.0f);
    REQUIRE (Fixture::peak (f.play (61), 2) > 0.0f);
}

TEST_CASE ("MIDI note starts the sound at its sample position")
{
    Fixture f;
    f.put (constWav ("a.wav"), 0, 60);
    auto out = f.play (60, 200);

    REQUIRE (Fixture::peak (out, 0, 0, 200) == 0.0f);
    REQUIRE (Fixture::peak (out, 0, 200, 400) > 0.0f);
}

TEST_CASE ("unmapped notes are silent")
{
    Fixture f;
    f.put (constWav ("a.wav"), 0, 60);

    for (int note : { 0, 59, 61, 127 })
        REQUIRE (Fixture::peak (f.play (note), 0) == 0.0f);
}

TEST_CASE ("note on-screen keyboard plays at the start of the next block")
{
    Fixture f;
    f.put (constWav ("a.wav"), 0, 72);
    juce::MidiBuffer midi;

    f.processor.triggerNote (72, 127);
    auto out = f.run (midi);

    REQUIRE (Fixture::peak (out, 0, 64, 200) > 0.0f);
    REQUIRE (f.processor.getActiveKeys()[1] == (1ull << (72 - 64)));
}

TEST_CASE ("buffer is cleared before rendering")
{
    Fixture f;
    juce::AudioBuffer<float> buffer (f.processor.getTotalNumOutputChannels(), kBlock);
    juce::MidiBuffer midi;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < kBlock; ++i)
            buffer.setSample (ch, i, 0.75f);

    f.processor.processBlock (buffer, midi);

    REQUIRE (Fixture::peak (buffer, 0) == 0.0f);
}

TEST_CASE ("velocity layers split the velocity range evenly")
{
    Fixture f;
    const auto dir = testDir().getChildFile ("layers");
    juce::Array<juce::File> files;
    for (int i = 1; i <= 4; ++i)
        files.add (constWav ("layers/snare " + juce::String (i) + ".wav", 0.1f * (float) i));

    REQUIRE (f.processor.addFilesToKey (files, 0, 38, Spread::VelocityLayers) == 4);
    REQUIRE (waitForLoading (f.processor));

    const auto inst = f.processor.getInstrument();
    REQUIRE (inst->zones.size() == 4);
    const int low[] = { 1, 32, 64, 96 };
    const int high[] = { 31, 63, 95, 127 };
    for (int i = 0; i < 4; ++i)
    {
        REQUIRE (inst->zones[(size_t) i].lowVel == low[i]);
        REQUIRE (inst->zones[(size_t) i].highVel == high[i]);
        REQUIRE (inst->zones[(size_t) i].lowKey == 38);
        REQUIRE (inst->zones[(size_t) i].sample != nullptr);
    }

    // Сильный удар играет самый громкий слой (уровень 0.4), слабый самый тихий (0.1)
    const float hard = Fixture::peak (f.play (38, 0, 1.0f), 0);
    for (int i = 0; i < 100; ++i)
    {
        juce::MidiBuffer midi;
        f.run (midi);
    }
    const float soft = Fixture::peak (f.play (38, 0, 20.0f / 127.0f), 0);
    REQUIRE (hard > soft * 2.0f);
}

TEST_CASE ("variations share the whole velocity range and are sorted by name")
{
    Fixture f;
    juce::Array<juce::File> files;
    for (int i : { 10, 2, 1 })
        files.add (constWav ("vars/clap " + juce::String (i) + ".wav"));
    files.add (testDir().getChildFile ("vars/readme.txt"));

    REQUIRE (f.processor.addFilesToKey (files, 0, 39, Spread::Variations) == 3);
    REQUIRE (waitForLoading (f.processor));

    const auto inst = f.processor.getInstrument();
    REQUIRE (inst->zones.size() == 3);
    REQUIRE (inst->zones[0].file.getFileName() == "clap 1.wav");
    REQUIRE (inst->zones[1].file.getFileName() == "clap 2.wav");
    REQUIRE (inst->zones[2].file.getFileName() == "clap 10.wav");
    for (const auto& z : inst->zones)
    {
        REQUIRE (z.lowVel == 1);
        REQUIRE (z.highVel == 127);
    }
}

TEST_CASE ("a folder becomes a kit: one file per key")
{
    Fixture f;
    const auto dir = testDir().getChildFile ("kit");
    dir.deleteRecursively();
    constWav ("kit/kick.wav");
    constWav ("kit/snare.wav");
    constWav ("kit/hat.wav");
    dir.getChildFile ("notes.txt").replaceWithText ("x");

    REQUIRE (f.processor.addFolderAsKit (dir, 0, 36) == 3);
    REQUIRE (waitForLoading (f.processor));

    const auto inst = f.processor.getInstrument();
    REQUIRE (inst->zones.size() == 3);
    REQUIRE (inst->zones[0].file.getFileName() == "hat.wav");
    REQUIRE (inst->zones[0].lowKey == 36);
    REQUIRE (inst->zones[2].file.getFileName() == "snare.wav");
    REQUIRE (inst->zones[2].lowKey == 38);
}

TEST_CASE ("removing a group removes its zones and renumbers the rest")
{
    Fixture f;
    f.processor.addGroup ("B");
    f.processor.addGroup ("C");
    f.put (constWav ("a.wav"), 0, 60);
    f.put (constWav ("b.wav"), 1, 61);
    f.put (constWav ("c.wav"), 2, 62);

    f.processor.removeGroup (1);

    const auto inst = f.processor.getInstrument();
    REQUIRE (inst->groups.size() == 2);
    REQUIRE (inst->zones.size() == 2);
    REQUIRE (inst->zones[0].group == 0);
    REQUIRE (inst->zones[1].group == 1);
    REQUIRE (inst->zones[1].file.getFileName() == "c.wav");
}

TEST_CASE ("group count is limited")
{
    SamplitProcessor p;
    for (int i = 1; i < kMaxGroups; ++i)
        REQUIRE (p.addGroup ("G") == i);
    REQUIRE (p.addGroup ("one more") == -1);
}

TEST_CASE ("zone edits are clamped")
{
    Fixture f;
    f.put (constWav ("a.wav"), 0, 60);

    auto z = f.processor.getInstrument()->zones[0];
    z.lowVel = 200;
    z.highVel = 3;
    z.lowKey = 70;
    z.highKey = 20;
    f.processor.setZone (0, z);

    const auto fixed = f.processor.getInstrument()->zones[0];
    REQUIRE (fixed.lowVel == 127);
    REQUIRE (fixed.highVel == 127);
    REQUIRE (fixed.lowKey == 70);
    REQUIRE (fixed.highKey == 70);
}

TEST_CASE ("replaced snapshot is freed only after its sample stops playing")
{
    Fixture f;
    f.put (constWav ("a.wav"), 0, 60);
    f.play (60);  // играет

    f.processor.addGroup ("another");  // публикует новый снимок
    f.processor.collectGarbage (true);
    REQUIRE (f.processor.getRetiredCount() > 0);  // старый снимок держит играющий сэмпл

    juce::MidiBuffer midi;
    for (int i = 0; i < 100; ++i)  // ~1 с: сэмпл доиграл
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
        const auto file = testDir().getChildFile ("mono.wav");
        writeWav (file, 44100.0, 1, 4410, [] (int i) { return 0.25f * std::sin ((float) i * 0.05f); });
        auto s = SamplitProcessor::readSample (fm, file);
        REQUIRE (s != nullptr);
        REQUIRE (s->audio.getNumChannels() == 1);
        REQUIRE (s->audio.getNumSamples() == 4410);
        REQUIRE (s->sampleRate == 44100.0);
    }
    SECTION ("stereo 48 kHz")
    {
        const auto file = testDir().getChildFile ("stereo.wav");
        writeWav (file, 48000.0, 2, 4800, [] (int i) { return 0.25f * std::sin ((float) i * 0.05f); });
        auto s = SamplitProcessor::readSample (fm, file);
        REQUIRE (s != nullptr);
        REQUIRE (s->audio.getNumChannels() == 2);
        REQUIRE (s->getLengthSeconds() == 0.1);
    }
    SECTION ("not an audio file")
    {
        const auto file = testDir().getChildFile ("notes.wav");
        file.replaceWithText ("not audio");
        REQUIRE (SamplitProcessor::readSample (fm, file) == nullptr);
    }
    SECTION ("longer than 60 seconds")
    {
        const auto file = testDir().getChildFile ("long.wav");
        writeWav (file, 8000.0, 1, 8000 * 61, [] (int) { return 0.0f; });
        REQUIRE (SamplitProcessor::readSample (fm, file) == nullptr);
    }
}

TEST_CASE ("groups, zones and folder survive save and load of the project")
{
    juce::MemoryBlock saved;
    {
        Fixture a;
        a.processor.addGroup ("Claps");
        a.put (constWav ("a.wav"), 0, 60);
        a.put (constWav ("b.wav", 0.25f), 1, 64, Spread::VelocityLayers);

        auto g = a.processor.getInstrument()->groups[1];
        g.output = 5;
        g.mode = PlayMode::Cycle;
        g.tracking = true;
        g.gainDb = -3.0f;
        g.pan = 0.25f;
        a.processor.setGroup (1, g);

        auto z = a.processor.getInstrument()->zones[1];
        z.lowVel = 20;
        z.highVel = 90;
        z.rootKey = 62;
        a.processor.setZone (1, z);

        a.processor.setLastFolder (testDir());
        a.processor.getStateInformation (saved);
    }

    Fixture b;
    b.processor.addGroup ("will vanish");
    b.processor.setStateInformation (saved.getData(), (int) saved.getSize());
    REQUIRE (waitForLoading (b.processor));

    const auto inst = b.processor.getInstrument();
    REQUIRE (inst->groups.size() == 2);
    REQUIRE (inst->groups[1].name == "Claps");
    REQUIRE (inst->groups[1].output == 5);
    REQUIRE (inst->groups[1].mode == PlayMode::Cycle);
    REQUIRE (inst->groups[1].tracking);
    REQUIRE (inst->groups[1].gainDb == -3.0f);
    REQUIRE (inst->groups[1].pan == 0.25f);

    REQUIRE (inst->zones.size() == 2);
    REQUIRE (inst->zones[0].lowKey == 60);
    REQUIRE (inst->zones[1].file.getFileName() == "b.wav");
    REQUIRE (inst->zones[1].lowVel == 20);
    REQUIRE (inst->zones[1].highVel == 90);
    REQUIRE (inst->zones[1].rootKey == 62);
    REQUIRE (inst->zones[1].sample != nullptr);
    REQUIRE (b.processor.getLastFolder() == testDir());
}

TEST_CASE ("garbage state data is ignored")
{
    Fixture f;
    f.put (constWav ("a.wav"), 0, 60);

    const char junk[] = "definitely not a plugin state";
    f.processor.setStateInformation (junk, (int) sizeof (junk));

    REQUIRE (f.processor.getInstrument()->zones.size() == 1);
}

TEST_CASE ("a state with bad groups and zones is repaired")
{
    juce::ValueTree state ("SAMPLIT");
    state.setProperty ("version", 2, nullptr);

    juce::ValueTree zone ("ZONE");  // зона на несуществующую группу
    zone.setProperty ("file", testDir().getChildFile ("a.wav").getFullPathName(), nullptr);
    zone.setProperty ("group", 7, nullptr);
    state.appendChild (zone, nullptr);

    juce::MemoryBlock block;
    const auto xml = state.createXml();
    SamplitProcessor::copyXmlToBinary (*xml, block);

    SamplitProcessor p;
    p.setStateInformation (block.getData(), (int) block.getSize());

    const auto inst = p.getInstrument();
    REQUIRE (inst->groups.size() == 1);  // группа по умолчанию
    REQUIRE (inst->zones.empty());
}

TEST_CASE ("preview from the browser plays into Main")
{
    Fixture f { 1 };
    const auto file = constWav ("preview.wav");

    f.processor.previewFile (file);

    float peak = 0.0f;
    float outPeak = 0.0f;
    for (int i = 0; i < 200 && peak == 0.0f; ++i)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        juce::MidiBuffer midi;
        auto out = f.run (midi);
        peak = Fixture::peak (out, 0);
        outPeak = Fixture::peak (out, 2);
    }

    REQUIRE (peak > 0.0f);
    REQUIRE (outPeak == 0.0f);
}
