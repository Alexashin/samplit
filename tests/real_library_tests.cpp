#include "DiskStreamer.h"
#include "KitEngine.h"
#include "KitLoader.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <thread>

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

// Стресс в реальном времени: настоящий пресет, настоящий поток чтения, блоки по часам. Плотная игра по всем клавишам кита
// (в том числе по тарелкам с длинными хвостами). Печатает время блока и счётчики. Параметры из окружения:
// SAMPLIT_STRESS_SECONDS (по умолчанию 20), SAMPLIT_STRESS_HITS (ударов в секунду, 30), SAMPLIT_STRESS_BLOCK (кадров, 256).
TEST_CASE ("real library: dense playing in real time without underruns", "[.reallib-stress]")
{
    const char* libPath = std::getenv ("SAMPLIT_REAL_LIB");
    if (libPath == nullptr)
        SKIP ("SAMPLIT_REAL_LIB не задана");
    auto env = [] (const char* name, double fallback)
    {
        const char* v = std::getenv (name);
        return v != nullptr ? std::atof (v) : fallback;
    };
    const double seconds = env ("SAMPLIT_STRESS_SECONDS", 20);
    const double hitsPerSecond = env ("SAMPLIT_STRESS_HITS", 30);
    const int block = (int) env ("SAMPLIT_STRESS_BLOCK", 256);
    constexpr double rate = 44100.0;

    const auto repo = juce::File (SAMPLIT_TEST_FIXTURES).getParentDirectory().getParentDirectory();
    Preset preset;
    juce::String error;
    REQUIRE (Preset::fromFile (repo.getChildFile ("tools/presets/studio600-default.json"), preset, error));
    KitLoadOptions opts;
    opts.libraryRoot = juce::File (libPath);
    opts.manifestDir = repo.getChildFile ("sample_library/packs-src");
    const auto res = loadKit (preset, opts);
    REQUIRE (res.ok());

    std::vector<int> notes;
    for (int n = 0; n < 128; ++n)
        if (res.kit->notes[(size_t) n].piece >= 0)
            notes.push_back (n);
    REQUIRE (! notes.empty());

    DiskStreamer streamer (true);
    KitEngine engine;
    engine.setStreamer (&streamer);
    engine.prepare (rate);
    engine.setKit (res.kit.get());

    std::vector<float> L ((size_t) block), R ((size_t) block);
    std::array<float*, kNumOutputs + 1> pl {}, pr {};
    for (size_t i = 0; i < pl.size(); ++i)
    {
        pl[i] = L.data();
        pr[i] = i == 0 ? R.data() : nullptr;
    }

    const int blocks = (int) (seconds * rate / block);
    const double blockSeconds = block / rate;
    const double hitsPerBlock = hitsPerSecond * blockSeconds;
    uint32_t rng = 12345;
    auto next = [&] { rng = rng * 1664525u + 1013904223u; return rng >> 8; };

    std::vector<double> times, noteOnTimes;
    times.reserve ((size_t) blocks);
    struct Slow { double us; int block; int strikes; int newHits; };
    std::vector<Slow> slow;
    double hitDebt = 0.0;
    int hits = 0, maxActive = 0;
    const auto begin = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b)
    {
        hitDebt += hitsPerBlock;
        int newHits = 0;
        while (hitDebt >= 1.0)
        {
            hitDebt -= 1.0;
            const auto n0 = std::chrono::steady_clock::now();
            engine.noteOn (notes[next() % notes.size()], 1 + (int) (next() % 127));
            noteOnTimes.push_back (std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - n0).count());
            ++hits;
            ++newHits;
        }

        const auto t0 = std::chrono::steady_clock::now();
        std::fill (L.begin(), L.end(), 0.0f);
        std::fill (R.begin(), R.end(), 0.0f);
        engine.render (pl, pr, 0, block);
        times.push_back (std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count());
        maxActive = std::max (maxActive, engine.getNumActiveStrikes());
        slow.push_back ({ times.back(), b, engine.getNumActiveStrikes(), newHits });

        // Ждём до начала следующего блока, как это делает аудио-интерфейс
        std::this_thread::sleep_until (begin + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
                                           std::chrono::duration<double> ((b + 1) * blockSeconds)));
    }

    std::sort (times.begin(), times.end());
    auto pct = [&] (double p) { return times[(size_t) std::min<double> ((double) times.size() - 1, p * (double) times.size())]; };
    std::cout << "[stress] " << seconds << " с, блок " << block << " кадров (" << blockSeconds * 1e6 << " мкс на блок), ударов " << hits
              << ", одновременно ударов макс. " << maxActive << ", ячеек сейчас " << streamer.numActive() << "\n"
              << "[stress] время блока, мкс: среднее " << (std::accumulate (times.begin(), times.end(), 0.0) / (double) times.size())
              << ", p99 " << pct (0.99) << ", p99.9 " << pct (0.999) << ", максимум " << times.back() << "\n"
              << "[stress] недогрузок " << streamer.underruns.load() << ", нет ячейки " << streamer.noSlot.load()
              << ", ошибок чтения " << streamer.readErrors.load() << std::endl;
    std::sort (slow.begin(), slow.end(), [] (const Slow& a, const Slow& b) { return a.us > b.us; });
    for (size_t i = 0; i < std::min<size_t> (6, slow.size()); ++i)
        std::cout << "[stress] медленный блок " << slow[i].block << ": " << slow[i].us << " мкс, ударов звучит " << slow[i].strikes
                  << ", новых в этом блоке " << slow[i].newHits << "\n";
    if (! noteOnTimes.empty())
    {
        std::sort (noteOnTimes.begin(), noteOnTimes.end());
        std::cout << "[stress] noteOn, мкс: медиана " << noteOnTimes[noteOnTimes.size() / 2] << ", максимум " << noteOnTimes.back() << std::endl;
    }
    CHECK (streamer.underruns.load() == 0);
    CHECK (streamer.readErrors.load() == 0);
}
