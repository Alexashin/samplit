#include "samplit/core/Library.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace samplit;

namespace
{
    // Временная папка с сэмплами и базой, удаляется вместе с объектом
    struct TempDir
    {
        TempDir()
        {
            dir = juce::File::getSpecialLocation (juce::File::tempDirectory)
                      .getChildFile ("samplit-core-" + juce::String::toHexString (juce::Random::getSystemRandom().nextInt64()));
            dir.createDirectory();
            samples = dir.getChildFile ("samples");
            samples.createDirectory();
        }

        ~TempDir() { dir.deleteRecursively(); }

        juce::File db() const { return dir.getChildFile ("index.db"); }

        juce::File write (const juce::String& relative, double rate, int channels, int length) const
        {
            auto file = samples.getChildFile (relative);
            file.getParentDirectory().createDirectory();
            file.deleteFile();

            juce::AudioBuffer<float> audio (channels, length);
            for (int ch = 0; ch < channels; ++ch)
                for (int i = 0; i < length; ++i)
                    audio.setSample (ch, i, 0.25f * std::sin ((float) i * 0.05f));

            {
                juce::WavAudioFormat wav;
                std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
                auto options = juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (channels).withBitsPerSample (16);
                auto writer = wav.createWriterFor (stream, options);
                REQUIRE (writer != nullptr);
                writer->writeFromAudioSampleBuffer (audio, 0, length);
            }

            return file;
        }

        juce::File dir, samples;
    };

    std::unique_ptr<Library> openLibrary (const TempDir& t)
    {
        juce::String error;
        auto lib = Library::open (t.db(), error);
        REQUIRE (lib != nullptr);
        REQUIRE (error.isEmpty());
        return lib;
    }

    // Три сэмпла, один во вложенной папке
    void fillSamples (const TempDir& t)
    {
        t.write ("kick_808.wav", 44100.0, 1, 4410);
        t.write ("Snare Dry.wav", 48000.0, 2, 24000);
        t.write ("drums/Hat_808_open.wav", 96000.0, 2, 9600);
    }
}

TEST_CASE ("first scan adds everything, second leaves it unchanged")
{
    TempDir t;
    fillSamples (t);
    auto lib = openLibrary (t);

    auto first = lib->scanFolder (t.samples);
    CHECK (first.added == 3);
    CHECK (first.updated == 0);
    CHECK (first.removed == 0);
    CHECK (first.failed == 0);
    CHECK (lib->count() == 3);

    auto second = lib->scanFolder (t.samples);
    CHECK (second.unchanged == 3);
    CHECK (second.added == 0);
    CHECK (second.updated == 0);
    CHECK (second.removed == 0);
}

TEST_CASE ("duration, sample rate and channels are read from the file")
{
    TempDir t;
    fillSamples (t);
    auto lib = openLibrary (t);
    lib->scanFolder (t.samples);

    auto hits = lib->search ("hat");
    REQUIRE (hits.size() == 1);
    CHECK (hits[0].name == "Hat_808_open");
    CHECK (hits[0].file == t.samples.getChildFile ("drums/Hat_808_open.wav"));
    CHECK (hits[0].sampleRate == 96000);
    CHECK (hits[0].channels == 2);
    CHECK (hits[0].duration == Catch::Approx (0.1));

    auto kick = lib->search ("kick");
    REQUIRE (kick.size() == 1);
    CHECK (kick[0].sampleRate == 44100);
    CHECK (kick[0].channels == 1);
    CHECK (kick[0].duration == Catch::Approx (0.1));
}

TEST_CASE ("changed file is updated, deleted file is removed")
{
    TempDir t;
    fillSamples (t);
    auto lib = openLibrary (t);
    lib->scanFolder (t.samples);

    // Другая длина: размер файла меняется, mtime тоже
    t.write ("kick_808.wav", 44100.0, 1, 44100);
    auto changed = lib->scanFolder (t.samples);
    CHECK (changed.updated == 1);
    CHECK (changed.unchanged == 2);
    CHECK (lib->search ("kick")[0].duration == Catch::Approx (1.0));

    REQUIRE (t.samples.getChildFile ("Snare Dry.wav").deleteFile());
    auto removed = lib->scanFolder (t.samples);
    CHECK (removed.removed == 1);
    CHECK (removed.unchanged == 2);
    CHECK (lib->count() == 2);
    CHECK (lib->search ("snare").empty());
}

TEST_CASE ("broken audio file is counted as failed and not indexed")
{
    TempDir t;
    fillSamples (t);
    t.samples.getChildFile ("broken.wav").replaceWithText ("this is not audio");
    t.samples.getChildFile ("notes.txt").replaceWithText ("ignored");
    auto lib = openLibrary (t);

    auto result = lib->scanFolder (t.samples);
    CHECK (result.added == 3);
    CHECK (result.failed == 1);
    CHECK (lib->count() == 3);
    CHECK (lib->search ("broken").empty());
}

TEST_CASE ("search matches every word, ignores case, looks at the path")
{
    TempDir t;
    fillSamples (t);
    auto lib = openLibrary (t);
    lib->scanFolder (t.samples);

    CHECK (lib->search ("").size() == 3);
    CHECK (lib->search ("   ").size() == 3);
    CHECK (lib->search ("808").size() == 2);
    CHECK (lib->search ("808 open").size() == 1);
    CHECK (lib->search ("open 808").size() == 1);
    CHECK (lib->search ("KiCk").size() == 1);
    CHECK (lib->search ("snare 808").empty());
    CHECK (lib->search ("drums").size() == 1); // слово из относительного пути
    CHECK (lib->search ("808", 1).size() == 1); // лимит

    // Пустой запрос: по пути (побайтово, заглавная S идёт раньше строчных)
    auto all = lib->search ("");
    CHECK (all[0].name == "Snare Dry");
    CHECK (all[2].name == "kick_808");
}

TEST_CASE ("index survives reopening")
{
    TempDir t;
    fillSamples (t);
    {
        auto lib = openLibrary (t);
        lib->scanFolder (t.samples);
    }

    auto lib = openLibrary (t);
    CHECK (lib->count() == 3);
    CHECK (lib->scanFolder (t.samples).unchanged == 3);
}

TEST_CASE ("cancelled scan keeps rows that were not visited")
{
    TempDir t;
    fillSamples (t);
    auto lib = openLibrary (t);
    lib->scanFolder (t.samples);

    auto result = lib->scanFolder (t.samples, [] { return true; });
    CHECK (result.removed == 0);
    CHECK (lib->count() == 3);
}
