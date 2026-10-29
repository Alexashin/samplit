#include "DiskStreamer.h"
#include "KitEngine.h"
#include "KitLoader.h"

#include "samplit/core/Pack.h"
#include "samplit/core/Wav.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace samplit;

namespace
{
    constexpr int kBlock = 512;
    constexpr int kTotal = 60000;       // длиннее кольца (32768): проверяется и перенос через конец

    juce::File streamDir()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("samplit-tests").getChildFile ("streaming");
        dir.createDirectory();
        return dir;
    }

    // Детерминированные 24-битные отсчёты на весь диапазон
    std::vector<int32_t> noise24 (int frames)
    {
        std::vector<int32_t> v ((size_t) frames);
        for (int i = 0; i < frames; ++i)
            v[(size_t) i] = (int32_t) (((uint32_t) (i + 1) * 2654435761u) >> 8) - 8388608;
        return v;
    }

    juce::File writeWav24 (const juce::File& file, const std::vector<int32_t>& samples, double rate = 44100.0)
    {
        file.deleteFile();
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::OutputStream> os (file.createOutputStream());
        REQUIRE (os != nullptr);
        auto writer = fmt.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (1).withBitsPerSample (24));
        REQUIRE (writer != nullptr);
        std::vector<int> shifted (samples.size());
        for (size_t i = 0; i < samples.size(); ++i)
            shifted[i] = samples[i] * 256;
        const int* ch[1] = { shifted.data() };
        REQUIRE (writer->write (ch, (int) samples.size()));
        return file;
    }

    std::vector<float> toFloats (const std::vector<int32_t>& samples)
    {
        pack::DecodedAudio a;
        a.samples = samples;
        a.frames = (int) samples.size();
        return pack::toFloat24 (a);
    }

    // Потоковый клип так, как его делает загрузчик
    ClipPtr streamedClip (const juce::File& wav, int headFrames, double rate = 44100.0)
    {
        pack::WavInfo info;
        juce::String error;
        REQUIRE (pack::readWavInfo (wav, info, error));
        juce::FileInputStream in (wav);
        std::vector<float> head ((size_t) headFrames);
        std::vector<uint8_t> scratch;
        REQUIRE (pack::readWav24 (in, info.dataOffset, headFrames, head.data(), scratch));
        return DecodedClip::fromStream (std::move (head), (int) info.frames, rate, wav.getFullPathName().toStdString(), info.dataOffset);
    }

    std::shared_ptr<const Kit> oneHitKit (ClipPtr clip)
    {
        auto kit = std::make_shared<Kit>();
        kit->mics.push_back ({ "m", "m", "close", 0 });
        Piece p;
        p.id = "p";
        p.name = "p";
        p.choke = -1;
        p.layers.push_back ({ "l0", "", 1, 127 });
        Hit h;
        h.id = "h0";
        h.layer = 0;
        h.key = 60;
        h.velLo = 1;
        h.velHi = 127;
        h.files.push_back ({ clip, 0.0f });
        p.hits.push_back (h);
        kit->pieces.push_back (p);
        kit->mode = HitSelect::Live;
        REQUIRE (kit->buildIndex());
        return kit;
    }

    // Играет одну ноту и возвращает левый канал Main за заданное число блоков. pumpWith: стример без потока
    // (после каждого блока делается несколько проходов чтения)
    std::vector<float> playBlocks (KitEngine& engine, int blocks, DiskStreamer* pumpWith, int pumpsPerBlock = 3, int block = kBlock)
    {
        std::vector<float> L ((size_t) block), R ((size_t) block);
        std::array<float*, kNumOutputs + 1> pl {}, pr {};
        for (size_t i = 0; i < pl.size(); ++i)
        {
            pl[i] = L.data();
            pr[i] = i == 0 ? R.data() : nullptr;
        }

        std::vector<float> out;
        for (int b = 0; b < blocks; ++b)
        {
            std::fill (L.begin(), L.end(), 0.0f);
            std::fill (R.begin(), R.end(), 0.0f);
            engine.render (pl, pr, 0, block);
            out.insert (out.end(), L.begin(), L.end());
            if (pumpWith != nullptr)
                for (int k = 0; k < pumpsPerBlock; ++k)
                    pumpWith->pump();
        }
        return out;
    }

    // Те же отсчёты, но клип целиком в памяти: эталон
    std::vector<float> referenceRender (const std::vector<int32_t>& samples, double engineRate, int blocks, int block = kBlock)
    {
        KitEngine ref;
        ref.prepare (engineRate);
        const auto kit = oneHitKit (DecodedClip::fromFloat (toFloats (samples), 44100.0));
        ref.setKit (kit.get());
        ref.noteOn (60, 100);
        return playBlocks (ref, blocks, nullptr, 3, block);
    }
}

TEST_CASE ("readWavInfo skips LIST and pads odd chunks, reads EXTENSIBLE", "[stream]")
{
    // Руками собранный WAV: fmt EXTENSIBLE (40 байт), нечётный блок LIST с байтом выравнивания, потом data
    std::vector<uint8_t> b;
    auto put32 = [&] (uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back ((uint8_t) (v >> (8 * i))); };
    auto put16 = [&] (uint16_t v) { b.push_back ((uint8_t) v); b.push_back ((uint8_t) (v >> 8)); };
    auto tag = [&] (const char* t) { for (int i = 0; i < 4; ++i) b.push_back ((uint8_t) t[i]); };

    const auto samples = noise24 (100);
    tag ("RIFF"); put32 (0);    // размер допишем
    tag ("WAVE");
    tag ("fmt "); put32 (40);
    put16 (0xFFFE); put16 (1); put32 (48000); put32 (48000 * 3); put16 (3); put16 (24);
    put16 (22); put16 (24); put32 (4);
    put16 (1); for (int i = 0; i < 14; ++i) b.push_back (0);     // SubFormat: PCM
    tag ("LIST"); put32 (5); for (int i = 0; i < 5; ++i) b.push_back ('x'); b.push_back (0);   // 5 байт + выравнивание
    tag ("data"); put32 (100 * 3);
    const size_t dataStart = b.size();
    for (int32_t v : samples)
    {
        b.push_back ((uint8_t) (v & 0xff));
        b.push_back ((uint8_t) ((v >> 8) & 0xff));
        b.push_back ((uint8_t) ((v >> 16) & 0xff));
    }
    const uint32_t riff = (uint32_t) b.size() - 8;
    for (int i = 0; i < 4; ++i)
        b[4 + (size_t) i] = (uint8_t) (riff >> (8 * i));

    const auto file = streamDir().getChildFile ("manual.wav");
    file.replaceWithData (b.data(), b.size());

    pack::WavInfo info;
    juce::String error;
    REQUIRE (pack::readWavInfo (file, info, error));
    CHECK (info.channels == 1);
    CHECK (info.bits == 24);
    CHECK (! info.isFloat);
    CHECK (info.sampleRate == 48000.0);
    CHECK (info.frames == 100);
    CHECK (info.dataOffset == (int64_t) dataStart);

    juce::FileInputStream in (file);
    std::vector<float> got (100);
    std::vector<uint8_t> scratch;
    REQUIRE (pack::readWav24 (in, info.dataOffset, 100, got.data(), scratch));
    CHECK (got == toFloats (samples));
}

TEST_CASE ("readWav24 matches readAudioFile bit for bit", "[stream]")
{
    const auto samples = noise24 (5000);
    const auto wav = writeWav24 (streamDir().getChildFile ("bits.wav"), samples);

    pack::WavInfo info;
    juce::String error;
    REQUIRE (pack::readWavInfo (wav, info, error));
    REQUIRE (info.frames == 5000);

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    pack::DecodedAudio viaJuce;
    REQUIRE (pack::readAudioFile (fm, wav, 100, 4100, viaJuce, error));

    juce::FileInputStream in (wav);
    std::vector<float> mine (4000);
    std::vector<uint8_t> scratch;
    REQUIRE (pack::readWav24 (in, info.dataOffset + 100 * 3, 4000, mine.data(), scratch));
    CHECK (mine == pack::toFloat24 (viaJuce));
}

TEST_CASE ("streamed playback is bit exact with the whole clip in memory", "[stream]")
{
    const auto samples = noise24 (kTotal);
    const auto wav = writeWav24 (streamDir().getChildFile ("exact.wav"), samples);

    for (const double rate : { 44100.0, 48000.0 })
    {
        DiskStreamer streamer (false);
        KitEngine engine;
        engine.setStreamer (&streamer);
        engine.prepare (rate);
        const auto kit = oneHitKit (streamedClip (wav, 4000));
        engine.setKit (kit.get());
        REQUIRE (engine.noteOn (60, 100).voices == 1);

        const int blocks = (int) std::ceil (kTotal / (double) kBlock * (rate / 44100.0)) + 4;
        const auto streamed = playBlocks (engine, blocks, &streamer);
        const auto reference = referenceRender (samples, rate, blocks);

        REQUIRE (streamed.size() == reference.size());
        CHECK (streamed == reference);
        CHECK (streamer.underruns.load() == 0);
        CHECK (engine.getNumActiveStrikes() == 0);
    }
}

TEST_CASE ("streamed voice fades smoothly when the disk is too slow", "[stream]")
{
    const auto wav = writeWav24 (streamDir().getChildFile ("slow.wav"), std::vector<int32_t> ((size_t) kTotal, 4194304));   // 0,5

    DiskStreamer streamer (false);       // pump() не зовём: хвост не приходит
    KitEngine engine;
    engine.setStreamer (&streamer);
    engine.prepare (44100.0);
    const auto kit = oneHitKit (streamedClip (wav, 4000));
    engine.setKit (kit.get());
    REQUIRE (engine.noteOn (60, 100).voices == 1);

    const auto out = playBlocks (engine, 12, nullptr);
    CHECK (streamer.underruns.load() == 1);
    CHECK (out[1000] == 0.5f);
    float peak = 0.0f;
    for (float v : out)
    {
        REQUIRE (std::isfinite (v));
        peak = std::max (peak, std::abs (v));
    }
    CHECK (peak <= 0.5f);
    CHECK (out.back() == 0.0f);
    CHECK (engine.getNumActiveStrikes() == 0);

    // Затухание плавное: соседние отсчёты отличаются не больше чем на шаг спада
    for (size_t i = 1; i < out.size(); ++i)
        REQUIRE (std::abs (out[i] - out[i - 1]) < 0.01f);
}

TEST_CASE ("streams are released with the strike and the kit is held until the disk lets go", "[stream]")
{
    const auto wav = writeWav24 (streamDir().getChildFile ("hold.wav"), noise24 (kTotal));

    DiskStreamer streamer (false);
    KitEngine engine;
    engine.setStreamer (&streamer);
    engine.prepare (44100.0);
    const auto kit = oneHitKit (streamedClip (wav, 4000));
    engine.setKit (kit.get());
    REQUIRE (engine.noteOn (60, 100).voices == 1);
    streamer.pump();
    CHECK (streamer.numActive() == 1);

    engine.setKit (nullptr);            // кит больше не текущий, но удар ещё звучит
    CHECK (engine.isUsing (kit.get()));

    engine.prepare (44100.0);           // все удары отпущены
    CHECK (engine.getNumActiveStrikes() == 0);
    CHECK (streamer.isUsing (kit.get()));       // ячейка ещё закрывает файл
    CHECK (engine.isUsing (kit.get()));
    streamer.pump();
    CHECK (streamer.numActive() == 0);
    CHECK_FALSE (engine.isUsing (kit.get()));
}

TEST_CASE ("streamer slots run out gracefully", "[stream]")
{
    const auto wav = writeWav24 (streamDir().getChildFile ("slots.wav"), noise24 (kTotal));
    const auto clip = streamedClip (wav, 1000);

    DiskStreamer streamer (false);
    std::vector<int> slots;
    for (int i = 0; i < DiskStreamer::kMaxStreams; ++i)
    {
        slots.push_back (streamer.acquire (*clip, nullptr));
        REQUIRE (slots.back() >= 0);
    }
    CHECK (streamer.acquire (*clip, nullptr) == -1);
    CHECK (streamer.noSlot.load() == 1);

    for (int s : slots)
        streamer.release (s);
    streamer.pump();
    CHECK (streamer.numActive() == 0);
    CHECK (streamer.acquire (*clip, nullptr) >= 0);
}

TEST_CASE ("offline render waits for the disk and matches the in-memory render", "[stream]")
{
    constexpr int frames = 3 * 44100;
    const auto samples = noise24 (frames);
    const auto wav = writeWav24 (streamDir().getChildFile ("offline.wav"), samples);

    DiskStreamer streamer (true);       // настоящий поток чтения
    KitEngine engine;
    engine.setStreamer (&streamer);
    engine.setOffline (true);
    engine.prepare (44100.0);
    const auto kit = oneHitKit (streamedClip (wav, 4000));
    engine.setKit (kit.get());
    REQUIRE (engine.noteOn (60, 100).voices == 1);

    const int blocks = frames / kBlock + 4;
    const auto streamed = playBlocks (engine, blocks, nullptr);
    const auto reference = referenceRender (samples, 44100.0, blocks);

    CHECK (streamed == reference);
    CHECK (streamer.underruns.load() == 0);
    CHECK (streamer.readErrors.load() == 0);
}

TEST_CASE ("loadKit keeps only the head of a long local WAV and describes the tail", "[stream][loader]")
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("samplit-tests").getChildFile ("streaming-loader");
    dir.deleteRecursively();
    const auto lib = dir.getChildFile ("lib");
    const auto manifests = dir.getChildFile ("manifests");
    lib.getChildFile ("KICK/in").createDirectory();
    manifests.createDirectory();

    const auto samples = noise24 (kTotal);
    const auto wav = writeWav24 (lib.getChildFile ("KICK/in/0-01.wav"), samples);

    pack::Manifest m;
    m.id = "local";
    m.mics = { { "kick_in", "Kick In", "close", 1 } };
    pack::Piece p;
    p.id = "kick";
    p.name = "Kick";
    p.keys = { 36 };
    p.layers = { { "all", "center", 1, 127 } };
    pack::Hit whole;
    whole.id = "whole";
    whole.layer = "all";
    whole.key = 36;
    whole.files["kick_in"].path = "KICK/in/0-01.wav";
    pack::Hit region = whole;
    region.id = "region";
    region.files["kick_in"].start = 1000;
    region.files["kick_in"].end = 50000;
    p.hits = { whole, region };
    m.pieces.push_back (p);
    manifests.getChildFile ("local.json").replaceWithText (m.toJson());

    KitLoadOptions opts;
    opts.libraryRoot = lib;
    opts.manifestDir = manifests;
    opts.threads = 2;
    opts.headSeconds = 0.1;     // 4410 кадров

    Preset preset;
    preset.id = "t";
    preset.name = "T";
    preset.packs = { "local" };
    const auto res = loadKit (preset, opts);
    REQUIRE (res.ok());

    const auto& hits = res.kit->pieces[0].hits;
    REQUIRE (hits.size() == 2);
    const auto& a = *hits[0].files[0].clip;
    const auto& b = *hits[1].files[0].clip;
    const auto floats = toFloats (samples);

    REQUIRE (a.isStreamed());
    CHECK (a.frames() == kTotal);
    REQUIRE (a.f32.size() == 4410);
    CHECK (std::equal (a.f32.begin(), a.f32.end(), floats.begin()));

    REQUIRE (b.isStreamed());
    CHECK (b.frames() == 49000);
    CHECK (b.dataOffset == a.dataOffset + 1000 * 3);
    CHECK (std::equal (b.f32.begin(), b.f32.end(), floats.begin() + 1000));

    CHECK (res.memoryBytes == 2 * 4410 * 4);

    // Короткий файл (не длиннее головы + порции) остаётся целиком в памяти
    opts.headSeconds = 1.4;
    const auto shortRes = loadKit (preset, opts);
    REQUIRE (shortRes.ok());
    CHECK_FALSE (shortRes.kit->pieces[0].hits[0].files[0].clip->isStreamed());
    CHECK (shortRes.kit->pieces[0].hits[0].files[0].clip->frames() == kTotal);
}

TEST_CASE ("a host block much larger than the ring still renders offline without gaps", "[stream]")
{
    constexpr int frames = 4 * 44100;
    constexpr int bigBlock = 30000;     // больше окна кольца, как бывает в офлайн-рендере некоторых хостов
    const auto samples = noise24 (frames);
    const auto wav = writeWav24 (streamDir().getChildFile ("bigblock.wav"), samples);

    DiskStreamer streamer (true);
    KitEngine engine;
    engine.setStreamer (&streamer);
    engine.setOffline (true);
    engine.prepare (44100.0);
    const auto kit = oneHitKit (streamedClip (wav, 15435));
    engine.setKit (kit.get());
    REQUIRE (engine.noteOn (60, 100).voices == 1);

    const int blocks = frames / bigBlock + 2;
    const auto streamed = playBlocks (engine, blocks, nullptr, 3, bigBlock);
    const auto reference = referenceRender (samples, 44100.0, blocks, bigBlock);

    CHECK (streamed == reference);
    CHECK (streamer.underruns.load() == 0);
}

TEST_CASE ("an unreadable file does not stall the offline render", "[stream]")
{
    const auto wav = writeWav24 (streamDir().getChildFile ("vanish.wav"), noise24 (kTotal));
    const auto clip = streamedClip (wav, 4000);
    REQUIRE (wav.deleteFile());          // файл исчез после загрузки кита

    DiskStreamer streamer (true);
    KitEngine engine;
    engine.setStreamer (&streamer);
    engine.setOffline (true);
    engine.prepare (44100.0);
    const auto kit = oneHitKit (clip);
    engine.setKit (kit.get());
    REQUIRE (engine.noteOn (60, 100).voices == 1);

    const auto start = juce::Time::getMillisecondCounter();
    const auto out = playBlocks (engine, 12, nullptr);
    CHECK (juce::Time::getMillisecondCounter() - start < 2000);     // не 5 с ожидания на голос
    CHECK (streamer.readErrors.load() >= 1);
    CHECK (streamer.underruns.load() == 1);
    for (float v : out)
        REQUIRE (std::isfinite (v));
    CHECK (engine.getNumActiveStrikes() == 0);
}
