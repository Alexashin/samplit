#include "DiskStreamer.h"
#include "KitEngine.h"
#include "KitLoader.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>

using namespace samplit;

// Проверка на настоящей библиотеке. Запускается только если задана SAMPLIT_REAL_LIB (папка 00-Studio600ru-Drum-LIB),
// остальное берётся из репозитория: манифесты sample_library/packs-src и пресет tools/presets/studio600-default.json.
// Печатает память и время загрузки и сравнивает потоковый звук с загрузкой целиком на нескольких настоящих ударах.
TEST_CASE ("real library: preset loads with heads only and streams like the whole files", "[.reallib]")
{
    const char* libPath = std::getenv ("SAMPLIT_REAL_LIB");
    if (libPath == nullptr)
        SKIP ("SAMPLIT_REAL_LIB не задана");

    const auto repo = juce::File (SAMPLIT_TEST_FIXTURES).getParentDirectory().getParentDirectory();
    Preset preset;
    juce::String error;
    REQUIRE (Preset::fromFile (repo.getChildFile ("tools/presets/studio600-default.json"), preset, error));

    KitLoadOptions opts;
    opts.libraryRoot = juce::File (libPath);
    opts.manifestDir = repo.getChildFile ("sample_library/packs-src");

    const auto t0 = std::chrono::steady_clock::now();
    const auto res = loadKit (preset, opts);
    const auto secs = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    REQUIRE (res.ok());

    int streamed = 0, whole = 0;
    for (const auto& p : res.kit->pieces)
        for (const auto& h : p.hits)
            for (const auto& f : h.files)
                if (f.clip != nullptr)
                    (f.clip->isStreamed() ? streamed : whole)++;
    std::cout << "[reallib] загрузка " << secs << " с, память голов " << res.memoryBytes / (1024.0 * 1024.0) << " МБ, потоковых клипов "
              << streamed << ", целиком " << whole << ", предупреждений " << res.warnings.size() << std::endl;

    // Играем по очереди несколько клавиш и сравниваем с эталоном: тот же кит, загруженный целиком
    KitLoadOptions full = opts;
    full.headSeconds = 0;
    const auto ref = loadKit (preset, full);
    REQUIRE (ref.ok());
    std::cout << "[reallib] целиком в памяти: " << ref.memoryBytes / (1024.0 * 1024.0) << " МБ" << std::endl;

    DiskStreamer streamer (true);
    KitEngine a, b;
    a.setStreamer (&streamer);
    a.setOffline (true);
    for (auto* e : { &a, &b })
    {
        e->prepare (44100.0);
        e->setSeed (7);
    }
    a.setKit (res.kit.get());
    b.setKit (ref.kit.get());

    std::vector<float> La (512), Ra (512), Lb (512), Rb (512);
    std::array<float*, kNumOutputs + 1> pla {}, pra {}, plb {}, prb {};
    for (size_t i = 0; i < pla.size(); ++i)
    {
        pla[i] = La.data();
        pra[i] = i == 0 ? Ra.data() : nullptr;
        plb[i] = Lb.data();
        prb[i] = i == 0 ? Rb.data() : nullptr;
    }

    for (const int note : { 36, 38, 42, 51, 49, 96, 100 })
    {
        a.prepare (44100.0);
        b.prepare (44100.0);
        const auto ia = a.noteOn (note, 100);
        const auto ib = b.noteOn (note, 100);
        if (ia.voices == 0)
            continue;
        REQUIRE (ia.hit == ib.hit);

        bool same = true;
        for (int blk = 0; blk < 400 && same; ++blk)
        {
            std::fill (La.begin(), La.end(), 0.0f);
            std::fill (Ra.begin(), Ra.end(), 0.0f);
            std::fill (Lb.begin(), Lb.end(), 0.0f);
            std::fill (Rb.begin(), Rb.end(), 0.0f);
            a.render (pla, pra, 0, 512);
            b.render (plb, prb, 0, 512);
            same = La == Lb && Ra == Rb;
        }
        CHECK (same);
    }
    std::cout << "[reallib] недогрузок " << streamer.underruns.load() << ", ошибок чтения " << streamer.readErrors.load() << std::endl;
    CHECK (streamer.underruns.load() == 0);
}
