#include "DiskStreamer.h"
#include "KitEngine.h"
#include "KitLoader.h"
#include "spec_helper.h"

#include "samplit/core/Pack.h"
#include "samplit/core/Wav.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <fstream>

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

    // Две клавиши (60 и 62) одного барабана, на каждой свой клип
    std::shared_ptr<const Kit> twoNoteKit (ClipPtr a, ClipPtr b)
    {
        auto kit = std::make_shared<Kit>();
        kit->mics.push_back ({ "m", "m", "close", 0 });
        Piece p;
        p.id = "p";
        p.name = "p";
        p.layers.push_back ({ "l0", "", 1, 127 });
        int n = 0;
        for (const auto& clip : { a, b })
        {
            Hit h;
            h.id = "h" + std::to_string (n);
            h.layer = 0;
            h.key = n == 0 ? 60 : 62;
            h.velLo = 1;
            h.velHi = 127;
            h.files.push_back ({ clip, 0.0f });
            p.hits.push_back (h);
            ++n;
        }
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

    DiskStreamer streamer (false, 4, 2);        // максимум 4 ячейки, блоки по 2
    streamer.reserve (4);
    CHECK (streamer.getAllocatedSlots() == 4);
    std::vector<int> slots;
    for (int i = 0; i < 4; ++i)
    {
        slots.push_back (streamer.acquire (*clip, nullptr));
        REQUIRE (slots.back() >= 0);
    }
    CHECK (streamer.acquire (*clip, nullptr) == -1);
    CHECK (streamer.noSlot.load() == 1);
    CHECK (streamer.acquire (*clip, nullptr, false) == -1);      // повторная попытка не считается отказом
    CHECK (streamer.noSlot.load() == 1);
    streamer.pump();
    CHECK (streamer.getAllocatedSlots() == 4);                   // больше максимума не растёт

    for (int s : slots)
        streamer.release (s);
    streamer.pump();
    CHECK (streamer.numActive() == 0);
    CHECK (streamer.acquire (*clip, nullptr) >= 0);
}

TEST_CASE ("ring memory is allocated lazily by blocks up to the maximum", "[stream]")
{
    const auto wav = writeWav24 (streamDir().getChildFile ("lazy.wav"), noise24 (kTotal));
    const auto clip = streamedClip (wav, 1000);

    {
        DiskStreamer fresh (false);
        CHECK (fresh.getAllocatedBlocks() == 0);        // пустой экземпляр: 0 МБ
        CHECK (fresh.getMaxSlots() == 2048);
        CHECK (DiskStreamer::kMaxStreams / DiskStreamer::kBlockSlots == 32);
    }

    DiskStreamer streamer (false, 6, 2);
    CHECK (streamer.getAllocatedBlocks() == 0);
    CHECK (streamer.acquire (*clip, nullptr) == -1);    // нечем: поток чтения получает сигнал и выделит блок
    streamer.pump();
    CHECK (streamer.getAllocatedBlocks() == 1);

    std::vector<int> slots;
    for (int i = 0; i < 2; ++i)
        slots.push_back (streamer.acquire (*clip, nullptr));
    CHECK (slots[0] >= 0);
    CHECK (slots[1] >= 0);
    streamer.pump();                                    // занято больше 3/4: запас выделяется заранее
    CHECK (streamer.getAllocatedBlocks() == 2);
    for (int i = 0; i < 4; ++i)
        slots.push_back (streamer.acquire (*clip, nullptr));
    for (int i = 0; i < 6; ++i)
        streamer.pump();
    CHECK (streamer.getAllocatedBlocks() == 3);
    CHECK (streamer.getAllocatedSlots() == 6);
    CHECK (streamer.acquire (*clip, nullptr) >= 0);     // два оставшихся места третьего блока
    CHECK (streamer.acquire (*clip, nullptr) >= 0);
    CHECK (streamer.acquire (*clip, nullptr) == -1);
    streamer.pump();
    CHECK (streamer.getAllocatedSlots() == 6);          // максимум
}

TEST_CASE ("a new strike plays whole when slots are scarce and the old one fades smoothly", "[stream]")
{
    const auto sa = std::vector<int32_t> ((size_t) kTotal, 4194304);    // 0,5
    const auto sb = noise24 (kTotal);
    const auto wavA = writeWav24 (streamDir().getChildFile ("scarceA.wav"), sa);
    const auto wavB = writeWav24 (streamDir().getChildFile ("scarceB.wav"), sb);

    DiskStreamer streamer (false, 1, 1);        // одна ячейка на всех
    KitEngine engine;
    engine.setStreamer (&streamer);
    engine.prepare (44100.0);
    const auto kit = twoNoteKit (streamedClip (wavA, 4000), streamedClip (wavB, 4000));
    engine.setKit (kit.get());

    REQUIRE (engine.noteOn (60, 100).voices == 1);
    playBlocks (engine, 4, &streamer);
    REQUIRE (engine.noteOn (62, 100).voices == 1);
    const int blocks = kTotal / kBlock + 6;
    const auto second = playBlocks (engine, blocks, &streamer);

    const auto ref = referenceRender (sb, 44100.0, blocks);
    REQUIRE (second.size() == ref.size());

    // Остаток после вычитания эталона удара B это затухание удара A: плавное, 10 мс, потом тишина
    std::vector<float> rest (second.size());
    for (size_t i = 0; i < rest.size(); ++i)
        rest[i] = second[i] - ref[i];
    CHECK (std::abs (rest[0] - 0.5f) < 1e-5f);
    float maxJump = 0.0f;
    for (size_t i = 1; i < 500; ++i)
        maxJump = std::max (maxJump, std::abs (rest[i] - rest[i - 1]));
    CHECK (maxJump < 0.002f);                           // шаг спада 0,5 / 441 = 0,0011
    for (size_t i = 460; i < rest.size(); ++i)
        REQUIRE (std::abs (rest[i]) < 1e-5f);           // B звучит целиком, до последнего отсчёта

    const auto health = engine.getHealth();
    CHECK (health.underruns == 0);
    CHECK (health.steals == 1);
    CHECK (health.hardSteals == 0);
    CHECK (health.readErrors == 0);
    CHECK (engine.getNumActiveStrikes() == 0);
}

TEST_CASE ("a voice fading from a steal near the end of the available data finishes smoothly and counts an underrun", "[stream]")
{
    const auto wavA = writeWav24 (streamDir().getChildFile ("fadeEdgeA.wav"), std::vector<int32_t> ((size_t) kTotal, 4194304));   // 0,5
    const auto wavB = writeWav24 (streamDir().getChildFile ("fadeEdgeB.wav"), std::vector<int32_t> ((size_t) kTotal, 0));

    DiskStreamer streamer (false, 1, 1);        // одна ячейка; pump() не зовём: у A есть только голова 4000
    KitEngine engine;
    engine.setStreamer (&streamer);
    engine.prepare (44100.0);
    const auto kit = twoNoteKit (streamedClip (wavA, 4000), streamedClip (wavB, 4000));
    engine.setKit (kit.get());

    REQUIRE (engine.noteOn (60, 100).voices == 1);
    const auto before = playBlocks (engine, 15, nullptr, 3, 250);       // позиция A: 3750, до конца головы 250
    CHECK (before.back() == 0.5f);
    CHECK (streamer.underruns.load() == 0);

    REQUIRE (engine.noteOn (62, 100).voices == 1);      // A вытесняется: затухание 10 мс (441), а данных только на 250
    const auto after = playBlocks (engine, 4, nullptr, 3, 250);

    CHECK (streamer.underruns.load() == 1);
    CHECK (std::abs (after.front() - 0.5f) < 0.01f);
    CHECK (std::abs (after.front() - before.back()) < 0.01f);
    float maxJump = std::abs (after.front() - before.back());
    for (size_t i = 1; i < after.size(); ++i)
    {
        REQUIRE (std::isfinite (after[i]));
        maxJump = std::max (maxJump, std::abs (after[i] - after[i - 1]));
    }
    CHECK (maxJump < 0.01f);                            // без обрыва огибающей (щелчка 0,3 и больше)
    CHECK (after.back() == 0.0f);
    for (size_t i = 300; i < after.size(); ++i)
        REQUIRE (after[i] == 0.0f);
}

TEST_CASE ("strikes are evicted softly before the hard limit", "[stream]")
{
    const std::vector<float> data (200000, 0.01f);
    const auto clip = DecodedClip::fromFloat (data, 44100.0);
    const auto kit = oneHitKit (clip);

    {
        KitEngine engine;
        engine.prepare (44100.0);
        engine.setKit (kit.get());
        float peak = 0.0f;
        for (int i = 0; i < 700; ++i)
        {
            REQUIRE (engine.noteOn (60, 100).voices == 1);
            for (float v : playBlocks (engine, 1, nullptr, 3, 64))
            {
                REQUIRE (std::isfinite (v));
                peak = std::max (peak, std::abs (v));
            }
            CHECK (engine.getNumActiveStrikes() <= KitEngine::kMaxStrikes);
        }
        const auto h = engine.getHealth();
        CHECK (h.steals > 0);
        CHECK (h.hardSteals == 0);
        CHECK (peak < 0.01f * (float) KitEngine::kMaxStrikes + 0.001f);
    }
    {
        // Без рендера между ударами затухание не успевает освободить место: срабатывает жёсткое вытеснение
        KitEngine engine;
        engine.prepare (44100.0);
        engine.setKit (kit.get());
        for (int i = 0; i < 300; ++i)
            REQUIRE (engine.noteOn (60, 100).voices == 1);
        CHECK (engine.getNumActiveStrikes() == KitEngine::kMaxStrikes);
        CHECK (engine.getHealth().hardSteals > 0);
    }
}

TEST_CASE ("the last sample of a clip is played", "[stream]")
{
    const auto samples = noise24 (kTotal);
    const auto floats = toFloats (samples);

    {
        KitEngine engine;
        engine.prepare (44100.0);
        const auto kit = oneHitKit (DecodedClip::fromFloat (floats, 44100.0));
        engine.setKit (kit.get());
        REQUIRE (engine.noteOn (60, 100).voices == 1);
        const auto out = playBlocks (engine, kTotal / kBlock + 2, nullptr);
        CHECK (out[(size_t) kTotal - 1] == floats.back());
        CHECK (out[(size_t) kTotal] == 0.0f);
    }
    {
        const auto wav = writeWav24 (streamDir().getChildFile ("last.wav"), samples);
        DiskStreamer streamer (false);
        KitEngine engine;
        engine.setStreamer (&streamer);
        engine.prepare (44100.0);
        const auto kit = oneHitKit (streamedClip (wav, 4000));
        engine.setKit (kit.get());
        REQUIRE (engine.noteOn (60, 100).voices == 1);
        const auto out = playBlocks (engine, kTotal / kBlock + 2, &streamer);
        CHECK (out[(size_t) kTotal - 1] == floats.back());
        CHECK (out[(size_t) kTotal] == 0.0f);
        CHECK (engine.getHealth().underruns == 0);
    }
}

TEST_CASE ("health counts read errors and names the file", "[stream]")
{
    {
        const auto wav = writeWav24 (streamDir().getChildFile ("gone-health.wav"), noise24 (kTotal));
        const auto clip = streamedClip (wav, 4000);
        REQUIRE (wav.deleteFile());

        DiskStreamer streamer (false);
        KitEngine engine;
        engine.setStreamer (&streamer);
        engine.prepare (44100.0);
        const auto kit = oneHitKit (clip);
        engine.setKit (kit.get());
        REQUIRE (engine.noteOn (60, 100).voices == 1);
        playBlocks (engine, 40, &streamer);
        const auto h = engine.getHealth();
        CHECK (h.readErrors >= 1);
        CHECK (h.lastError.contains ("gone-health.wav"));
        CHECK (h.underruns == 1);

        engine.resetHealth();
        const auto cleared = engine.getHealth();
        CHECK (cleared.readErrors == 0);
        CHECK (cleared.underruns == 0);
        CHECK (cleared.lastError.isEmpty());
    }
    {
        // Файл укоротили после загрузки кита: чтение посреди хвоста не удаётся
        const auto wav = writeWav24 (streamDir().getChildFile ("cut-health.wav"), noise24 (kTotal));
        const auto clip = streamedClip (wav, 4000);
        juce::MemoryBlock all;
        REQUIRE (wav.loadFileAsData (all));
        // Прямое усечение работает и на Windows: replaceWithData пытается заменить открытый файл переименованием.
        {
            std::ofstream cut (wav.getFullPathName().toStdString(), std::ios::binary | std::ios::trunc);
            REQUIRE (cut.is_open());
            cut.write (static_cast<const char*> (all.getData()), 30000);
            REQUIRE (cut.good());
        }

        DiskStreamer streamer (false);
        KitEngine engine;
        engine.setStreamer (&streamer);
        engine.prepare (44100.0);
        const auto kit = oneHitKit (clip);
        engine.setKit (kit.get());
        REQUIRE (engine.noteOn (60, 100).voices == 1);
        const auto out = playBlocks (engine, 60, &streamer);
        for (float v : out)
            REQUIRE (std::isfinite (v));
        const auto h = engine.getHealth();
        CHECK (h.readErrors >= 1);
        CHECK (h.lastError.contains ("cut-health.wav"));
        CHECK (h.underruns == 1);
        CHECK (engine.getNumActiveStrikes() == 0);
    }
}

TEST_CASE ("many strikes with few slots stay clean and finite", "[stream]")
{
    const auto samples = noise24 (kTotal);
    const auto wav = writeWav24 (streamDir().getChildFile ("stress.wav"), samples);

    DiskStreamer streamer (false, 4, 2);
    KitEngine engine;
    engine.setStreamer (&streamer);
    engine.prepare (44100.0);
    const auto kit = oneHitKit (streamedClip (wav, 4000));
    engine.setKit (kit.get());

    float peak = 0.0f;
    for (int i = 0; i < 150; ++i)
    {
        REQUIRE (engine.noteOn (60, 100).voices == 1);
        for (float v : playBlocks (engine, 2, &streamer))
        {
            REQUIRE (std::isfinite (v));
            peak = std::max (peak, std::abs (v));
        }
    }
    const auto h = engine.getHealth();
    CHECK (h.underruns == 0);
    CHECK (h.readErrors == 0);
    CHECK (h.steals > 0);
    CHECK (peak < 32.0f);
    CHECK (streamer.getAllocatedSlots() <= 4);
}

TEST_CASE ("a manifest with a split file (tailChunk) is rejected clearly", "[stream][loader]")
{
    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("samplit-tests").getChildFile ("streaming-tail");
    dir.deleteRecursively();
    const auto lib = dir.getChildFile ("lib");
    const auto manifests = dir.getChildFile ("manifests");
    lib.getChildFile ("KICK/in").createDirectory();
    manifests.createDirectory();
    writeWav24 (lib.getChildFile ("KICK/in/0-01.wav"), noise24 (5000));

    pack::Manifest m;
    m.id = "local";
    m.mics = { { "kick_in", "Kick In", "close", 1 } };
    pack::Piece p;
    p.id = "kick";
    p.name = "Kick";
    p.keys = { 36 };
    p.layers = { { "all", "center", 1, 127 } };
    pack::Hit hit;
    hit.id = "h";
    hit.layer = "all";
    hit.key = 36;
    hit.files["kick_in"].path = "KICK/in/0-01.wav";
    hit.files["kick_in"].tailChunk = 3;
    p.hits = { hit };
    m.pieces.push_back (p);
    manifests.getChildFile ("local.json").replaceWithText (m.toJson());

    KitLoadOptions opts;
    opts.libraryRoot = lib;
    opts.manifestDir = manifests;
    opts.threads = 1;
    const auto res = loadKit (testspec::oneSpec ("local", "kick", 36), opts);
    CHECK_FALSE (res.ok());
    CHECK (res.error.contains ("tailChunk"));
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

    const auto spec = testspec::oneSpec ("local", "kick", 36);
    const auto res = loadKit (spec, opts);
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
    const auto shortRes = loadKit (spec, opts);
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
