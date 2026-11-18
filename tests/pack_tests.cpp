#include "samplit/core/Pack.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <functional>

using namespace samplit::pack;

namespace
{
    juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }

    juce::File packDir()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("samplit-tests").getChildFile ("pack");
        dir.createDirectory();
        return dir;
    }

    // Детерминированный «звук» с крайними значениями 24 бит
    DecodedAudio makeAudio (int frames, double rate = 44100.0, int seed = 1)
    {
        DecodedAudio a;
        a.sampleRate = rate;
        a.frames = frames;
        a.samples.resize ((size_t) frames);
        uint32_t x = 0x1234567u * (uint32_t) seed + 1u;
        for (int i = 0; i < frames; ++i)
        {
            x = x * 1664525u + 1013904223u;
            a.samples[(size_t) i] = (int32_t) (x >> 8) - 8388608;  // весь диапазон 24 бит
        }
        a.samples[0] = -8388608;
        if (frames > 1)
            a.samples[1] = 8388607;
        return a;
    }

    juce::File writeWav (const juce::String& name, const DecodedAudio& a, int channels = 1, int bits = 24)
    {
        auto file = packDir().getChildFile (name);
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        juce::WavAudioFormat wav;
        auto options = juce::AudioFormatWriterOptions{}.withSampleRate (a.sampleRate).withNumChannels (channels).withBitsPerSample (bits);
        auto writer = wav.createWriterFor (stream, options);
        REQUIRE (writer != nullptr);

        std::vector<int32_t> justified (a.samples.size());
        for (size_t i = 0; i < justified.size(); ++i)
            justified[i] = (int32_t) ((uint32_t) a.samples[i] << 8);
        std::vector<const int*> chans ((size_t) channels, justified.data());
        REQUIRE (writer->write (chans.data(), a.frames));
        return file;
    }

    Manifest makeManifest (const juce::String& id, const std::vector<int>& chunks)
    {
        Manifest m;
        m.id = id;
        m.title = u8 ("Тестовый пакет");
        m.tier = "base";
        m.mics = { { "kick_in", "Kick In", "close", 1 }, { "oh", "Overhead", "overhead", 5 } };

        Piece p;
        p.id = "kick";
        p.name = u8 ("Кик");
        p.keys = { 21 };
        p.layers = { { "all", "center", 1, 127 } };
        for (size_t i = 0; i < chunks.size(); ++i)
        {
            Hit h;
            h.id = "kick_" + juce::String ((int) i);
            h.layer = "all";
            h.key = 21;
            h.velLo = 1 + (int) i * 10;
            h.velHi = h.velLo + 9;
            HitFile f;
            f.chunk = chunks[i];
            f.gainDb = -3.0f;
            f.frames = 1000;
            f.rate = 44100.0;
            h.files["kick_in"] = f;
            p.hits.push_back (h);
        }
        m.pieces.push_back (p);
        return m;
    }

    // Пакет из n блоков по 1000 отсчётов; возвращает исходное аудио блоков
    struct BuiltPack
    {
        juce::File file;
        PackKey key;
        std::vector<DecodedAudio> audio;
        Manifest manifest;
    };

    BuiltPack buildPack (const juce::String& name, int chunks)
    {
        BuiltPack b;
        b.file = packDir().getChildFile (name);
        b.key = generateKey();
        juce::String error;
        auto w = PackWriter::create (b.file, "test-pack", b.key, error);
        REQUIRE (w != nullptr);

        std::vector<int> ids;
        for (int i = 0; i < chunks; ++i)
        {
            b.audio.push_back (makeAudio (1000, 44100.0, i + 1));
            const int id = w->addAudio (b.audio.back(), error);
            REQUIRE (id == i);
            ids.push_back (id);
        }
        b.manifest = makeManifest ("test-pack", ids);
        REQUIRE (w->finish (b.manifest, error));
        return b;
    }

    // Портит байт и сообщает, заметил ли читатель: на открытии, манифесте или любом блоке
    bool tamperDetected (const BuiltPack& b, int64_t position)
    {
        const auto copy = packDir().getChildFile ("tampered.splpack");
        REQUIRE (b.file.copyFileTo (copy));
        {
            juce::FileOutputStream out (copy);
            REQUIRE (out.setPosition (position));
            juce::FileInputStream in (b.file);
            REQUIRE (in.setPosition (position));
            const auto original = in.readByte();
            out.writeByte ((char) (original ^ 0x01));
        }

        juce::String error;
        auto reader = PackReader::open (copy, error);
        if (reader == nullptr)
            return true;

        Manifest m;
        if (! reader->readManifest (b.key, m, error))
            return true;
        for (int c = 0; c < reader->getChunkCount(); ++c)
        {
            std::vector<uint8_t> flac;
            if (! reader->readChunk (b.key, c, flac, error))
                return true;
        }
        return false;
    }
}

TEST_CASE ("flac round trip is bit exact for 24-bit audio", "[pack]")
{
    const auto audio = makeAudio (5000);
    juce::MemoryBlock flac;
    juce::String error;
    REQUIRE (encodeFlac (audio, flac, error));
    REQUIRE (flac.getSize() > 0);

    DecodedAudio back;
    REQUIRE (decodeFlac (flac.getData(), flac.getSize(), back, error));
    REQUIRE (back.frames == audio.frames);
    REQUIRE (back.sampleRate == audio.sampleRate);
    REQUIRE (back.samples == audio.samples);
}

TEST_CASE ("wav to pack to decoded audio is bit exact, quiet tail kept", "[pack]")
{
    // Хвост ниже -60 дБ: при старой обрезке он бы пропал, теперь пакет хранит файл целиком
    auto audio = makeAudio (6000, 48000.0, 3);
    for (int i = 3000; i < audio.frames; ++i)
        audio.samples[(size_t) i] = (i % 7) - 3;
    const auto wav = writeWav ("lossless.wav", audio);

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    juce::String error;
    DecodedAudio fromWav;
    REQUIRE (readAudioFile (fm, wav, 0, 0, fromWav, error));

    const auto key = generateKey();
    const auto file = packDir().getChildFile ("lossless.splpack");
    auto w = PackWriter::create (file, "lossless", key, error);
    REQUIRE (w != nullptr);
    const int id = w->addAudio (fromWav, error);
    REQUIRE (id == 0);
    REQUIRE (w->finish (makeManifest ("lossless", { id }), error));

    auto reader = PackReader::open (file, error);
    REQUIRE (reader != nullptr);
    DecodedAudio back;
    REQUIRE (reader->decodeChunk (key, id, back, error));
    REQUIRE (back.frames == 6000);
    REQUIRE (back.samples == audio.samples);
}

TEST_CASE ("toFloat24 keeps every 24-bit value exactly", "[pack]")
{
    DecodedAudio audio;
    audio.sampleRate = 44100.0;
    audio.samples = { -8388608, -8388607, -1, 0, 1, 12345, 8388606, 8388607 };
    audio.frames = (int) audio.samples.size();

    const auto f = toFloat24 (audio);
    REQUIRE (f.size() == audio.samples.size());
    for (size_t i = 0; i < f.size(); ++i)
        CHECK ((int32_t) std::lround ((double) f[i] * 8388608.0) == audio.samples[i]);
}

TEST_CASE ("silence and tiny clips encode", "[pack]")
{
    DecodedAudio audio;
    audio.sampleRate = 48000.0;
    audio.frames = 2;
    audio.samples = { 0, 0 };

    juce::MemoryBlock flac;
    juce::String error;
    REQUIRE (encodeFlac (audio, flac, error));
    DecodedAudio back;
    REQUIRE (decodeFlac (flac.getData(), flac.getSize(), back, error));
    REQUIRE (back.samples == audio.samples);

    audio.frames = 0;
    audio.samples.clear();
    REQUIRE_FALSE (encodeFlac (audio, flac, error));
}

TEST_CASE ("pack hex keys", "[pack]")
{
    const auto key = generateKey();
    PackKey back {};
    REQUIRE (keyFromHex (keyToHex (key), back));
    REQUIRE (back == key);
    REQUIRE (keyToHex (key).length() == 64);
    REQUIRE_FALSE (keyFromHex ("abc", back));
    REQUIRE_FALSE (keyFromHex (juce::String::repeatedString ("zz", 32), back));
    REQUIRE (generateKey() != generateKey());
}

TEST_CASE ("manifest json round trip keeps source and stored forms", "[pack]")
{
    auto m = makeManifest ("round-trip", { 0, 1 });
    m.requiresPacks = { "other" };
    m.pieces[0].choke = "hats";
    m.pieces[0].hits[0].peerGroup = "D";
    m.pieces[0].hits[1].files["oh"].path = "src/oh.wav";  // исходная форма: и path, и chunk допустимы
    m.pieces[0].hits[1].files["oh"].chunk = -1;

    Manifest back;
    juce::String error;
    REQUIRE (Manifest::fromJson (m.toJson(), back, error));
    REQUIRE (back.id == "round-trip");
    REQUIRE (back.title == u8 ("Тестовый пакет"));
    REQUIRE (back.requiresPacks.size() == 1);
    REQUIRE (back.mics.size() == 2);
    REQUIRE (back.mics[1].out == 5);
    REQUIRE (back.pieces[0].choke == "hats");
    REQUIRE (back.pieces[0].keys == std::vector<int> { 21 });
    REQUIRE (back.pieces[0].hits[0].peerGroup == "D");
    REQUIRE (back.pieces[0].hits[0].files.at ("kick_in").chunk == 0);
    REQUIRE (back.pieces[0].hits[0].files.at ("kick_in").gainDb == -3.0f);
    REQUIRE (back.pieces[0].hits[0].files.at ("kick_in").frames == 1000);
    REQUIRE (back.pieces[0].hits[1].files.at ("oh").path == "src/oh.wav");
    REQUIRE (back.pieces[0].hits[1].files.at ("oh").chunk == -1);
    REQUIRE (back.toJson() == m.toJson());
}

TEST_CASE ("manifest json rejects garbage and unknown format", "[pack]")
{
    Manifest m;
    juce::String error;
    REQUIRE_FALSE (Manifest::fromJson ("not json", m, error));
    REQUIRE_FALSE (Manifest::fromJson ("[1,2]", m, error));
    REQUIRE_FALSE (Manifest::fromJson (R"({"format": 7})", m, error));
    REQUIRE (Manifest::fromJson (R"({"format": 1, "id": "x"})", m, error));
}

TEST_CASE ("pack write and read back", "[pack]")
{
    const auto b = buildPack ("roundtrip.splpack", 4);

    juce::String error;
    auto reader = PackReader::open (b.file, error);
    REQUIRE (reader != nullptr);
    REQUIRE (reader->getHeader().packId == "test-pack");
    REQUIRE (reader->getHeader().formatVersion == kFormatVersion);
    REQUIRE (reader->getChunkCount() == 4);

    Manifest m;
    REQUIRE (reader->readManifest (b.key, m, error));
    REQUIRE (m.toJson() == b.manifest.toJson());

    // Чтение в произвольном порядке
    for (int c : { 2, 0, 3, 1, 2 })
    {
        DecodedAudio audio;
        REQUIRE (reader->decodeChunk (b.key, c, audio, error));
        REQUIRE (audio.samples == b.audio[(size_t) c].samples);
        REQUIRE (audio.sampleRate == 44100.0);
    }

    REQUIRE_FALSE (reader->decodeChunk (b.key, 4, *std::make_unique<DecodedAudio>(), error));
    REQUIRE_FALSE (reader->decodeChunk (b.key, -1, *std::make_unique<DecodedAudio>(), error));
    REQUIRE_FALSE (b.file.getFileName().isEmpty());
    REQUIRE_FALSE (b.file.getSiblingFile (b.file.getFileName() + ".data.tmp").existsAsFile());
}

TEST_CASE ("pack without chunks is valid", "[pack]")
{
    const auto b = buildPack ("empty.splpack", 0);
    juce::String error;
    auto reader = PackReader::open (b.file, error);
    REQUIRE (reader != nullptr);
    REQUIRE (reader->getChunkCount() == 0);
    Manifest m;
    REQUIRE (reader->readManifest (b.key, m, error));
}

TEST_CASE ("wrong key is rejected", "[pack]")
{
    const auto b = buildPack ("wrongkey.splpack", 2);
    juce::String error;
    auto reader = PackReader::open (b.file, error);
    REQUIRE (reader != nullptr);

    auto wrong = b.key;
    wrong[0] ^= 0x80;
    Manifest m;
    REQUIRE_FALSE (reader->readManifest (wrong, m, error));
    std::vector<uint8_t> flac;
    REQUIRE_FALSE (reader->readChunk (wrong, 0, flac, error));
    REQUIRE (flac.empty());
}

TEST_CASE ("tampering with any part of the file is detected", "[pack]")
{
    const auto b = buildPack ("tamper.splpack", 3);
    const auto size = b.file.getSize();

    juce::String error;
    auto reader = PackReader::open (b.file, error);
    REQUIRE (reader != nullptr);
    juce::int64 dataBytes = 0;
    for (int c = 0; c < reader->getChunkCount(); ++c)
        dataBytes += reader->getChunkLength (c);
    const auto dataStart = size - dataBytes;

    SECTION ("magic") { REQUIRE (tamperDetected (b, 0)); }
    SECTION ("format version") { REQUIRE (tamperDetected (b, 4)); }
    SECTION ("pack id") { REQUIRE (tamperDetected (b, 10)); }
    SECTION ("manifest ciphertext") { REQUIRE (tamperDetected (b, 10 + 9 + 4 + 24 + 20)); }
    SECTION ("first byte of the block table") { REQUIRE (tamperDetected (b, dataStart - 3 * 36 + 2)); }
    SECTION ("block table nonce") { REQUIRE (tamperDetected (b, dataStart - 3 * 36 + 20)); }
    SECTION ("first byte of the first block") { REQUIRE (tamperDetected (b, dataStart)); }
    SECTION ("middle of a block") { REQUIRE (tamperDetected (b, dataStart + (juce::int64) reader->getChunkLength (0) + 100)); }
    SECTION ("last byte of the file (tag of the last block)") { REQUIRE (tamperDetected (b, size - 1)); }
}

TEST_CASE ("truncated and foreign files do not open", "[pack]")
{
    const auto b = buildPack ("truncate.splpack", 2);

    juce::String error;
    REQUIRE (PackReader::open (b.file, error) != nullptr);

    SECTION ("cut in the middle of the data")
    {
        const auto cut = packDir().getChildFile ("cut.splpack");
        juce::MemoryBlock all;
        REQUIRE (b.file.loadFileAsData (all));
        {
            std::ofstream out (cut.getFullPathName().toStdString(), std::ios::binary | std::ios::trunc);
            REQUIRE (out.is_open());
            out.write (static_cast<const char*> (all.getData()), (std::streamsize) all.getSize() - 50);
            REQUIRE (out.good());
        }
        REQUIRE (PackReader::open (cut, error) == nullptr);
    }
    SECTION ("short garbage")
    {
        const auto junk = packDir().getChildFile ("junk.splpack");
        {
            std::ofstream out (junk.getFullPathName().toStdString(), std::ios::binary | std::ios::trunc);
            REQUIRE (out.is_open());
            out.write ("hello", 5);
            REQUIRE (out.good());
        }
        REQUIRE (PackReader::open (junk, error) == nullptr);
    }
    SECTION ("missing file")
    {
        REQUIRE (PackReader::open (packDir().getChildFile ("nope.splpack"), error) == nullptr);
    }
}

TEST_CASE ("verifyManifest finds hits without files and unknown microphones", "[pack]")
{
    auto m = makeManifest ("v", { 0, 1 });
    REQUIRE (verifyManifest (m).ok());

    SECTION ("hit without files")
    {
        m.pieces[0].hits[1].files.clear();
        const auto r = verifyManifest (m);
        REQUIRE_FALSE (r.ok());
        REQUIRE (r.errors.size() == 1);
        REQUIRE (r.errors[0].contains ("kick_1"));
    }
    SECTION ("unknown microphone")
    {
        m.pieces[0].hits[0].files["ghost"] = m.pieces[0].hits[0].files["kick_in"];
        REQUIRE_FALSE (verifyManifest (m).ok());
    }
    SECTION ("hit without path or chunk")
    {
        m.pieces[0].hits[0].files["kick_in"].chunk = -1;
        REQUIRE_FALSE (verifyManifest (m).ok());
    }
    SECTION ("duplicate hit id")
    {
        m.pieces[0].hits[1].id = m.pieces[0].hits[0].id;
        REQUIRE_FALSE (verifyManifest (m).ok());
    }
    SECTION ("bad velocity window")
    {
        m.pieces[0].hits[0].velLo = 100;
        m.pieces[0].hits[0].velHi = 50;
        REQUIRE_FALSE (verifyManifest (m).ok());
    }
    SECTION ("unknown layer")
    {
        m.pieces[0].hits[0].layer = "missing";
        REQUIRE_FALSE (verifyManifest (m).ok());
    }
}

TEST_CASE ("verifyManifest warns about velocity holes and overlaps", "[pack]")
{
    auto m = makeManifest ("v", { 0, 1, 2 });
    // окна 1..10, 11..20, 21..30: без дыр и пересечений
    REQUIRE (verifyManifest (m).warnings.empty());

    SECTION ("hole")
    {
        m.pieces[0].hits[2].velLo = 25;
        const auto r = verifyManifest (m);
        REQUIRE (r.ok());
        REQUIRE (r.warnings.size() == 1);
        REQUIRE (r.warnings[0].contains (u8 ("дыры")));
        REQUIRE (r.warnings[0].contains ("21..24"));
    }
    SECTION ("overlap")
    {
        m.pieces[0].hits[1].velLo = 8;
        const auto r = verifyManifest (m);
        REQUIRE (r.ok());
        REQUIRE (r.warnings.size() == 1);
        REQUIRE (r.warnings[0].contains (u8 ("пересекаются")));
    }
    SECTION ("key missing from the piece list")
    {
        m.pieces[0].hits[0].key = 22;
        REQUIRE (verifyManifest (m).warnings.size() >= 1);
    }
}

TEST_CASE ("verifyPack accepts a good pack and finds broken references", "[pack]")
{
    SECTION ("good pack, also deep")
    {
        const auto b = buildPack ("good.splpack", 3);
        juce::String error;
        auto reader = PackReader::open (b.file, error);
        REQUIRE (reader != nullptr);
        REQUIRE (verifyPack (*reader, b.key).ok());
        REQUIRE (verifyPack (*reader, b.key, true).ok());
        REQUIRE (verifyPack (*reader, b.key, true).warnings.empty());
    }
    SECTION ("hit refers to a missing block")
    {
        const auto file = packDir().getChildFile ("dangling.splpack");
        const auto key = generateKey();
        juce::String error;
        auto w = PackWriter::create (file, "dangling", key, error);
        REQUIRE (w != nullptr);
        REQUIRE (w->addAudio (makeAudio (1000), error) == 0);
        REQUIRE (w->finish (makeManifest ("dangling", { 0, 99 }), error));

        auto reader = PackReader::open (file, error);
        REQUIRE (reader != nullptr);
        const auto r = verifyPack (*reader, key);
        REQUIRE_FALSE (r.ok());
        REQUIRE (r.errors[0].contains ("99"));
    }
    SECTION ("unused block gives a warning, pack id mismatch an error")
    {
        const auto file = packDir().getChildFile ("unused.splpack");
        const auto key = generateKey();
        juce::String error;
        auto w = PackWriter::create (file, "unused", key, error);
        REQUIRE (w != nullptr);
        REQUIRE (w->addAudio (makeAudio (1000), error) == 0);
        REQUIRE (w->addAudio (makeAudio (1000, 44100.0, 2), error) == 1);
        REQUIRE (w->finish (makeManifest ("other-id", { 0 }), error));

        auto reader = PackReader::open (file, error);
        REQUIRE (reader != nullptr);
        const auto r = verifyPack (*reader, key);
        REQUIRE_FALSE (r.ok());  // id не совпадает
        REQUIRE (r.warnings.size() == 1);
    }
    SECTION ("deep check catches a length that disagrees with the manifest")
    {
        const auto file = packDir().getChildFile ("badlength.splpack");
        const auto key = generateKey();
        juce::String error;
        auto w = PackWriter::create (file, "badlength", key, error);
        REQUIRE (w != nullptr);
        REQUIRE (w->addAudio (makeAudio (500), error) == 0);  // в манифесте заявлено 1000
        REQUIRE (w->finish (makeManifest ("badlength", { 0 }), error));

        auto reader = PackReader::open (file, error);
        REQUIRE (reader != nullptr);
        REQUIRE (verifyPack (*reader, key).ok());
        REQUIRE_FALSE (verifyPack (*reader, key, true).ok());
    }
}

TEST_CASE ("readAudioFile reads 24-bit mono WAV exactly and trims", "[pack]")
{
    const auto audio = makeAudio (3000, 48000.0, 7);
    const auto wav = writeWav ("read24.wav", audio);

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    juce::String error;

    DecodedAudio back;
    REQUIRE (readAudioFile (fm, wav, 0, 0, back, error));
    REQUIRE (back.frames == 3000);
    REQUIRE (back.sampleRate == 48000.0);
    REQUIRE (back.samples == audio.samples);

    SECTION ("trim start and end")
    {
        REQUIRE (readAudioFile (fm, wav, 100, 2100, back, error));
        REQUIRE (back.frames == 2000);
        REQUIRE (back.samples == std::vector<int32_t> (audio.samples.begin() + 100, audio.samples.begin() + 2100));
    }
    SECTION ("end beyond the file is clamped")
    {
        REQUIRE (readAudioFile (fm, wav, 0, 100000, back, error));
        REQUIRE (back.frames == 3000);
    }
    SECTION ("empty region and missing file")
    {
        REQUIRE_FALSE (readAudioFile (fm, wav, 3000, 0, back, error));
        REQUIRE_FALSE (readAudioFile (fm, packDir().getChildFile ("none.wav"), 0, 0, back, error));
    }
    SECTION ("stereo files are refused")
    {
        const auto stereo = writeWav ("stereo.wav", makeAudio (200), 2);
        REQUIRE_FALSE (readAudioFile (fm, stereo, 0, 0, back, error));
    }
    SECTION ("stereo downmix on request (library SUB-KICK.wav is stereo)")
    {
        const auto src = makeAudio (200);
        const auto stereo = writeWav ("stereo-dm.wav", src, 2);
        REQUIRE (readAudioFile (fm, stereo, 0, 0, back, error, true));
        REQUIRE (back.channels == 1);
        REQUIRE (back.frames == 200);
        // writeWav кладёт один и тот же сигнал в оба канала, поэтому (L+R)/2 совпадает с моно-исходником
        const auto mono = writeWav ("mono-dm.wav", src, 1);
        DecodedAudio ref;
        REQUIRE (readAudioFile (fm, mono, 0, 0, ref, error));
        REQUIRE (back.samples == ref.samples);
        REQUIRE_FALSE (readAudioFile (fm, writeWav ("six.wav", src, 6), 0, 0, back, error, true));
    }
}

TEST_CASE ("wav through flac through pack is bit exact", "[pack]")
{
    const auto audio = makeAudio (4410, 44100.0, 3);
    const auto wav = writeWav ("endtoend.wav", audio);

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    juce::String error;
    DecodedAudio src;
    REQUIRE (readAudioFile (fm, wav, 0, 0, src, error));

    const auto file = packDir().getChildFile ("endtoend.splpack");
    const auto key = generateKey();
    auto w = PackWriter::create (file, "endtoend", key, error);
    REQUIRE (w != nullptr);
    REQUIRE (w->addAudio (src, error) == 0);
    REQUIRE (w->finish (makeManifest ("endtoend", { 0 }), error));

    auto reader = PackReader::open (file, error);
    REQUIRE (reader != nullptr);
    DecodedAudio out;
    REQUIRE (reader->decodeChunk (key, 0, out, error));
    REQUIRE (out.samples == audio.samples);
}

TEST_CASE ("flac makes loud noise smaller than raw 24-bit but never loses a bit", "[pack]")
{
    // Тихий сигнал сжимается заметно лучше шума: проверка, что кодер вообще сжимает
    DecodedAudio quiet;
    quiet.sampleRate = 44100.0;
    quiet.frames = 20000;
    quiet.samples.resize (20000);
    for (int i = 0; i < quiet.frames; ++i)
        quiet.samples[(size_t) i] = (int32_t) std::lround (2000.0 * std::sin (i * 0.05));

    juce::MemoryBlock flac;
    juce::String error;
    REQUIRE (encodeFlac (quiet, flac, error));
    REQUIRE (flac.getSize() < (size_t) quiet.frames * 3 / 2);

    DecodedAudio back;
    REQUIRE (decodeFlac (flac.getData(), flac.getSize(), back, error));
    REQUIRE (back.samples == quiet.samples);
}

TEST_CASE ("manifest keeps unknown fields of every level as they are", "[pack]")
{
    const auto json = u8 (R"({
      "format": 1, "id": "extra", "version": 1, "tier": "base", "title": "t", "requires": [],
      "futureRootField": {"a": [1, 2]},
      "mics": [ { "id": "m", "name": "M", "role": "close", "out": 1, "color": "red" } ],
      "pieces": [ { "id": "p", "name": "P", "keys": [60], "choke": null, "note": "x",
        "layers": [ { "id": "l", "artic": "center", "velLo": 1, "velHi": 127,
                      "key": 60, "label": "Центр", "pick": "random", "peerGroup": true, "unverified": true } ],
        "hits": [ { "id": "h", "layer": "l", "key": 60, "velLo": 5, "velHi": 5,
                    "peerGroup": "D", "unverified": true,
                    "files": { "m": { "path": "a.wav", "gainDb": 0.0, "start": 0, "end": 0, "origin": "nki" } } } ] } ]
    })");

    Manifest m;
    juce::String error;
    REQUIRE (Manifest::fromJson (json, m, error));

    REQUIRE (m.extra.contains ("futureRootField"));
    REQUIRE (m.mics[0].extra["color"] == "red");
    REQUIRE (m.pieces[0].extra["note"] == "x");
    const auto& layer = m.pieces[0].layers[0];
    REQUIRE ((int) layer.extra["key"] == 60);
    REQUIRE (layer.extra["label"].toString() == u8 ("Центр"));
    REQUIRE (layer.extra["pick"] == "random");
    REQUIRE ((bool) layer.extra["peerGroup"]);
    REQUIRE ((bool) layer.extra["unverified"]);
    const auto& hit = m.pieces[0].hits[0];
    REQUIRE (hit.peerGroup == "D");
    REQUIRE ((bool) hit.extra["unverified"]);
    REQUIRE (hit.files.at ("m").extra["origin"] == "nki");

    // Записанный манифест читается обратно с теми же полями
    const auto written = m.toJson();
    Manifest again;
    REQUIRE (Manifest::fromJson (written, again, error));
    REQUIRE (again.toJson() == written);
    REQUIRE (again.pieces[0].layers[0].extra["pick"] == "random");
    REQUIRE ((bool) again.pieces[0].layers[0].extra["peerGroup"]);
    REQUIRE (again.pieces[0].hits[0].peerGroup == "D");
    REQUIRE (verifyManifest (again).ok());
}

TEST_CASE ("unknown fields survive the pack", "[pack]")
{
    const auto file = packDir().getChildFile ("extras.splpack");
    const auto key = generateKey();
    juce::String error;
    auto w = PackWriter::create (file, "extras", key, error);
    REQUIRE (w != nullptr);
    REQUIRE (w->addAudio (makeAudio (1000), error) == 0);

    auto m = makeManifest ("extras", { 0 });
    m.pieces[0].layers[0].extra.set ("pick", "cycle");
    m.pieces[0].hits[0].extra.set ("unverified", true);
    m.pieces[0].hits[0].files["kick_in"].peak = 123;
    REQUIRE (w->finish (m, error));

    auto reader = PackReader::open (file, error);
    REQUIRE (reader != nullptr);
    Manifest back;
    REQUIRE (reader->readManifest (key, back, error));
    REQUIRE (back.pieces[0].layers[0].extra["pick"] == "cycle");
    REQUIRE ((bool) back.pieces[0].hits[0].extra["unverified"]);
    REQUIRE (back.pieces[0].hits[0].files.at ("kick_in").peak == 123);
}

TEST_CASE ("peakOf finds the loudest sample and zero for silence", "[pack]")
{
    DecodedAudio a;
    a.frames = 4;
    a.samples = { 0, -5000, 300, 4999 };
    REQUIRE (peakOf (a) == 5000);
    a.samples = { 0, 0, 0, 0 };
    REQUIRE (peakOf (a) == 0);
    a.samples = { -8388608 };
    REQUIRE (peakOf (a) == 8388608);
}

TEST_CASE ("verifyPack reports files made of zeros", "[pack]")
{
    const auto file = packDir().getChildFile ("zeros.splpack");
    const auto key = generateKey();
    juce::String error;
    auto w = PackWriter::create (file, "zeros", key, error);
    REQUIRE (w != nullptr);

    DecodedAudio zeros;
    zeros.sampleRate = 44100.0;
    zeros.frames = 1000;
    zeros.samples.assign (1000, 0);
    REQUIRE (w->addAudio (makeAudio (1000), error) == 0);
    REQUIRE (w->addAudio (zeros, error) == 1);

    auto m = makeManifest ("zeros", { 0, 1 });
    m.pieces[0].hits[0].files["kick_in"].peak = peakOf (makeAudio (1000));

    SECTION ("the packer recorded the peak")
    {
        m.pieces[0].hits[1].files["kick_in"].peak = 0;
        REQUIRE (w->finish (m, error));
        auto reader = PackReader::open (file, error);
        REQUIRE (reader != nullptr);
        const auto r = verifyPack (*reader, key);  // без deep
        REQUIRE_FALSE (r.ok());
        REQUIRE (r.errors[0].contains ("kick_1"));
        REQUIRE (r.errors[0].contains (u8 ("нул")));
    }
    SECTION ("no peak in the manifest, deep check finds them")
    {
        REQUIRE (w->finish (m, error));
        auto reader = PackReader::open (file, error);
        REQUIRE (reader != nullptr);
        REQUIRE (verifyPack (*reader, key).ok());
        const auto r = verifyPack (*reader, key, true);
        REQUIRE_FALSE (r.ok());
        REQUIRE (r.errors[0].contains (u8 ("нул")));
    }
    SECTION ("a recorded peak that does not match the audio")
    {
        m.pieces[0].hits[0].files["kick_in"].peak = 77;
        m.pieces[0].hits[1].files["kick_in"].peak = 1;
        REQUIRE (w->finish (m, error));
        auto reader = PackReader::open (file, error);
        REQUIRE (reader != nullptr);
        REQUIRE_FALSE (verifyPack (*reader, key, true).ok());
    }
}

TEST_CASE ("manifest reads piece type, groups, pair and picks and writes them back", "[pack]")
{
    const char* json = R"({"format":1,"id":"g","pieces":[
        {"id":"tom1","name":"T","type":"tom","keys":[41],
         "layers":[{"id":"a","artic":"x","velLo":1,"velHi":127},{"id":"b","artic":"x","velLo":1,"velHi":127}],
         "groups":[
           {"id":"center","name":"Centre","hint":"h","role":"main","layers":["a"],"pick":"cycle"},
           {"id":"cres","name":"Cres","role":"crescendo","layers":["b"],"pick":"exact","picks":["exact"],
            "pair":{"type":"tom","group":"cres"}},
           {"id":"odd","name":"Odd","role":"martian","layers":[]}
         ],"hits":[]}]})";
    Manifest m;
    juce::String error;
    REQUIRE (Manifest::fromJson (json, m, error));
    const auto& p = m.pieces[0];
    CHECK (p.type == "tom");
    CHECK_FALSE (p.extra.contains ("type"));
    CHECK_FALSE (p.extra.contains ("groups"));
    REQUIRE (p.groups.size() == 3);
    CHECK (p.groups[0].hint == "h");
    CHECK (p.groups[0].role == "main");
    CHECK (p.groups[0].pick == "cycle");
    CHECK (p.groups[0].layers == std::vector<juce::String> { "a" });
    CHECK (p.groups[1].picks == std::vector<juce::String> { "exact" });
    CHECK (p.groups[1].pair.type == "tom");
    CHECK (p.groups[1].pair.group == "cres");
    CHECK (p.groups[1].pair.place.isEmpty());
    CHECK (p.groups[2].role == "other");

    Manifest back;
    REQUIRE (Manifest::fromJson (m.toJson(), back, error));
    CHECK (back.toJson() == m.toJson());
    CHECK (back.pieces[0].groups[1].pair.group == "cres");
    CHECK (back.pieces[0].groups[2].role == "other");
}

TEST_CASE ("old manifest without type and groups gets the type from the piece id", "[pack]")
{
    Manifest m;
    juce::String error;
    REQUIRE (Manifest::fromJson (R"({"format":1,"id":"o","pieces":[{"id":"snare_main"},{"id":"hat"},{"id":"crash2"},
        {"id":"tom3"},{"id":"ride"},{"id":"china"},{"id":"splash"},{"id":"kick"},{"id":"cowbell"}]})", m, error));
    const std::vector<juce::String> want { "snare", "hat", "crash", "tom", "ride", "china", "splash", "kick", "other" };
    REQUIRE (m.pieces.size() == want.size());
    for (size_t i = 0; i < want.size(); ++i)
    {
        CHECK (m.pieces[i].type == want[i]);
        CHECK (m.pieces[i].groups.empty());
    }
    Manifest back;
    REQUIRE (Manifest::fromJson (m.toJson(), back, error));
    CHECK (back.pieces[8].type == "other");
    CHECK (back.toJson() == m.toJson());
}

TEST_CASE ("verifyManifest checks groups", "[pack]")
{
    auto m = makeManifest ("grp", { 0, 1 });
    auto& p = m.pieces[0];
    p.layers = { { "a", "x", 1, 60 }, { "b", "x", 61, 127 } };
    for (auto& h : p.hits)
        h.layer = "a";
    Group g1; g1.id = "g1"; g1.name = "G1"; g1.layers = { "a" };
    Group g2; g2.id = "g2"; g2.name = "G2"; g2.layers = { "b" };
    p.groups = { g1, g2 };
    REQUIRE (verifyManifest (m).ok());

    SECTION ("layer in two groups") { p.groups[1].layers = { "a" }; REQUIRE_FALSE (verifyManifest (m).ok()); }
    SECTION ("unknown layer")       { p.groups[1].layers = { "zz" }; REQUIRE_FALSE (verifyManifest (m).ok()); }
    SECTION ("duplicate group id")  { p.groups[1].id = "g1"; REQUIRE_FALSE (verifyManifest (m).ok()); }
    SECTION ("empty id")            { p.groups[1].id = ""; REQUIRE_FALSE (verifyManifest (m).ok()); }
    SECTION ("empty name")          { p.groups[1].name = ""; REQUIRE_FALSE (verifyManifest (m).ok()); }
    SECTION ("empty layers")        { p.groups[1].layers.clear(); REQUIRE_FALSE (verifyManifest (m).ok()); }
    SECTION ("pick not in picks")   { p.groups[1].pick = "cycle"; p.groups[1].picks = { "exact" }; REQUIRE_FALSE (verifyManifest (m).ok()); }
    SECTION ("pick in picks")       { p.groups[1].pick = "exact"; p.groups[1].picks = { "exact" }; REQUIRE (verifyManifest (m).ok()); }
    SECTION ("unknown type warns")
    {
        p.type = "cowbell";
        const auto r = verifyManifest (m);
        REQUIRE (r.ok());
        REQUIRE (r.warnings.size() == 1);
    }
}

// ---------------------------------------------------------------- голова + хвост из кусков (формат v2)

namespace
{
    struct TailPack
    {
        juce::File file;
        PackKey key;
        std::vector<DecodedAudio> audio;
        Manifest manifest;
    };

    // По одному удару на длину; голова head, кусок chunk кадров
    TailPack buildTailPack (const juce::String& name, const std::vector<int>& lengths, int head, int chunk)
    {
        TailPack t;
        t.file = packDir().getChildFile (name);
        t.key = generateKey();
        juce::String error;
        auto w = PackWriter::create (t.file, "tail-pack", t.key, error, kFormatVersionTail);
        REQUIRE (w != nullptr);

        t.manifest = makeManifest ("tail-pack", {});
        auto& p = t.manifest.pieces[0];
        for (size_t i = 0; i < lengths.size(); ++i)
        {
            t.audio.push_back (makeAudio (lengths[i], 48000.0, (int) i + 1));
            Hit h;
            h.id = "k" + juce::String ((int) i);
            h.layer = "all";
            h.key = 21;
            h.velLo = 1 + (int) i * 10;
            h.velHi = h.velLo + 9;
            HitFile f;
            REQUIRE (w->addClip (t.audio.back(), head, chunk, f, error));
            h.files["kick_in"] = f;
            p.hits.push_back (h);
        }
        REQUIRE (w->finish (t.manifest, error));
        return t;
    }

    // Куски хвоста, склеенные с головой, должны совпасть с исходником бит в бит (через float32)
    void checkTailPack (const TailPack& t)
    {
        juce::String error;
        auto reader = PackReader::open (t.file, error);
        REQUIRE (reader != nullptr);
        Manifest m;
        REQUIRE (reader->readManifest (t.key, m, error));
        const auto& hits = m.pieces[0].hits;
        REQUIRE (hits.size() == t.audio.size());

        for (size_t i = 0; i < hits.size(); ++i)
        {
            const auto& f = hits[i].files.at ("kick_in");
            const auto& src = t.audio[i];
            REQUIRE (f.frames == src.frames);

            DecodedAudio whole;
            REQUIRE (reader->decodeWhole (t.key, f, whole, error));
            REQUIRE (whole.frames == src.frames);
            REQUIRE (whole.samples == src.samples);

            if (f.tailFirstChunk < 0)
                continue;

            // по кускам: голова + decodeTailChunk
            DecodedAudio head;
            REQUIRE (reader->decodeChunk (t.key, f.chunk, head, error));
            REQUIRE (head.frames == f.headFrames);
            std::vector<float> all = toFloat24 (head);
            std::vector<float> buf ((size_t) f.chunkFrames);
            for (int c = 0; c < tailChunkCount (f); ++c)
            {
                int n = 0;
                REQUIRE (reader->decodeTailChunk (t.key, f, c, buf.data(), (int) buf.size(), n, error));
                REQUIRE (n == std::min (f.chunkFrames, f.frames - f.headFrames - c * f.chunkFrames));
                all.insert (all.end(), buf.begin(), buf.begin() + n);
            }
            REQUIRE (all == toFloat24 (src));
        }
    }
}

TEST_CASE ("pack with head and tail chunks round trips bit exact", "[pack][tail]")
{
    // 600: не длиннее головы + кусок, целый блок; 1500: ровно голова + 2 куска (кратно); 1000: ровно голова + 1 кусок;
    // 1001: хвост 501 (не кратно); 20001: много кусков, последний в один кадр
    const auto t = buildTailPack ("tail-roundtrip.splpack", { 600, 1500, 1000, 1001, 20001 }, 500, 500);
    checkTailPack (t);

    juce::String error;
    auto reader = PackReader::open (t.file, error);
    REQUIRE (reader->getHeader().formatVersion == kFormatVersionTail);
    Manifest m;
    REQUIRE (reader->readManifest (t.key, m, error));
    const auto& hits = m.pieces[0].hits;
    CHECK (hits[0].files.at ("kick_in").tailFirstChunk < 0);
    CHECK (hits[1].files.at ("kick_in").tailFirstChunk >= 0);
    CHECK (tailChunkCount (hits[1].files.at ("kick_in")) == 2);
    CHECK (tailChunkCount (hits[2].files.at ("kick_in")) == 1);
    CHECK (tailChunkCount (hits[3].files.at ("kick_in")) == 2);
    CHECK (tailChunkCount (hits[4].files.at ("kick_in")) == 40);

    // обычная проверка пакета, в том числе глубокая
    const auto report = verifyPack (*reader, t.key, true);
    CHECK (report.errors.empty());
    CHECK (report.warnings.empty());
}

TEST_CASE ("tail chunk boundaries: head plus one frame", "[pack][tail]")
{
    // chunk 1 кадр: длина головы + 1 уже даёт хвост из одного кадра
    const auto t = buildTailPack ("tail-edge.splpack", { 501, 502, 1000 }, 500, 1);
    checkTailPack (t);
}

TEST_CASE ("a damaged tail chunk is reported", "[pack][tail]")
{
    const auto t = buildTailPack ("tail-damage.splpack", { 3000 }, 500, 1000);
    const auto copy = packDir().getChildFile ("tail-damage-copy.splpack");
    REQUIRE (t.file.copyFileTo (copy));
    {
        juce::FileOutputStream out (copy);  // портим последний байт файла: тег последнего куска
        REQUIRE (! out.failedToOpen());
        REQUIRE (out.setPosition (copy.getSize() - 1));
        out.writeByte ((char) 0x5A);
    }

    juce::String error;
    auto reader = PackReader::open (copy, error);
    REQUIRE (reader != nullptr);
    Manifest m;
    REQUIRE (reader->readManifest (t.key, m, error));
    const auto& f = m.pieces[0].hits[0].files.at ("kick_in");
    REQUIRE (tailChunkCount (f) == 3);

    std::vector<float> buf (1000);
    int n = 0;
    CHECK (reader->decodeTailChunk (t.key, f, 0, buf.data(), 1000, n, error));      // чужие куски целы
    CHECK_FALSE (reader->decodeTailChunk (t.key, f, 2, buf.data(), 1000, n, error));
    CHECK (error.isNotEmpty());
    DecodedAudio whole;
    CHECK_FALSE (reader->decodeWhole (t.key, f, whole, error));
    CHECK_FALSE (verifyPack (*reader, t.key).ok());

    // неверные аргументы
    CHECK_FALSE (reader->decodeTailChunk (t.key, f, 3, buf.data(), 1000, n, error));
    CHECK_FALSE (reader->decodeTailChunk (t.key, f, -1, buf.data(), 1000, n, error));
    CHECK_FALSE (reader->decodeTailChunk (t.key, f, 0, buf.data(), 10, n, error));
}

TEST_CASE ("an old version 1 pack still reads, decodeWhole and no tail", "[pack][tail]")
{
    const auto b = buildPack ("tail-old.splpack", 2);
    juce::String error;
    auto reader = PackReader::open (b.file, error);
    REQUIRE (reader != nullptr);
    REQUIRE (reader->getHeader().formatVersion == kFormatVersion);
    Manifest m;
    REQUIRE (reader->readManifest (b.key, m, error));
    DecodedAudio whole;
    REQUIRE (reader->decodeWhole (b.key, m.pieces[0].hits[1].files.at ("kick_in"), whole, error));
    CHECK (whole.samples == b.audio[1].samples);

    // писатель версии 1 не пишет хвосты и не принимает манифест с ними
    auto w = PackWriter::create (packDir().getChildFile ("tail-v1.splpack"), "x", b.key, error);
    REQUIRE (w != nullptr);
    HitFile f;
    CHECK_FALSE (w->addClip (makeAudio (3000), 500, 500, f, error));
    HitFile g;
    CHECK (w->addClip (makeAudio (600), 500, 500, g, error));  // короткий клип остаётся целым блоком
    CHECK (g.tailFirstChunk < 0);
}

TEST_CASE ("inconsistent tail fields are rejected", "[pack][tail]")
{
    auto jsonWith = [] (const std::function<void (HitFile&)>& edit)
    {
        auto m = makeManifest ("x", { 0 });
        auto& f = m.pieces[0].hits[0].files["kick_in"];
        f.frames = 3000;
        f.tailFirstChunk = 1;
        f.headFrames = 500;
        f.chunkFrames = 1000;
        edit (f);
        return m.toJson();
    };
    Manifest out;
    juce::String error;

    CHECK (Manifest::fromJson (jsonWith ([] (HitFile&) {}), out, error));
    CHECK_FALSE (Manifest::fromJson (jsonWith ([] (HitFile& f) { f.chunkFrames = 0; }), out, error));
    CHECK_FALSE (Manifest::fromJson (jsonWith ([] (HitFile& f) { f.headFrames = 0; }), out, error));
    CHECK_FALSE (Manifest::fromJson (jsonWith ([] (HitFile& f) { f.frames = 500; }), out, error));
    CHECK_FALSE (Manifest::fromJson (jsonWith ([] (HitFile& f) { f.chunk = -1; f.path = "a.wav"; }), out, error));
    CHECK_FALSE (Manifest::fromJson (jsonWith ([] (HitFile& f) { f.tailFirstChunk = 0; }), out, error));
    CHECK_FALSE (Manifest::fromJson (jsonWith ([] (HitFile& f) { f.start = 10; }), out, error));

    // куски хвоста за таблицей блоков: писатель не запишет такой манифест
    const auto key = generateKey();
    auto w = PackWriter::create (packDir().getChildFile ("tail-bad.splpack"), "x", key, error, kFormatVersionTail);
    REQUIRE (w != nullptr);
    REQUIRE (w->addAudio (makeAudio (500), error) == 0);
    REQUIRE (w->addAudio (makeAudio (1000), error) == 1);  // нужно 3 куска: блоки 1..3, есть только 1
    auto m = makeManifest ("x", { 0 });
    auto& f = m.pieces[0].hits[0].files["kick_in"];
    f.frames = 3500;
    f.tailFirstChunk = 1;
    f.headFrames = 500;
    f.chunkFrames = 1000;
    CHECK_FALSE (w->finish (m, error));
    CHECK (error.contains ("tailFirstChunk"));
}
