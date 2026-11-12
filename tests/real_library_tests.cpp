#include "DiskStreamer.h"
#include "KitEngine.h"
#include "KitLoader.h"
#include "PackCatalog.h"
#include "PatternPalette.h"
#include "PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

#if defined(_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <thread>

using namespace samplit;

namespace
{
    // Заводская установка Studio600 на локальной библиотеке: каталог из манифестов, состав кита из Setup::factory()
    KitSpec factorySpec (const KitLoadOptions& opts)
    {
        const auto catalog = scanPackCatalog (opts, false);
        std::vector<juce::String> warnings;
        return makeKitSpec (Setup::factory(), catalog, false, warnings);
    }

    // До count клавиш кита, равномерно по диапазону
    std::vector<int> sampleNotes (const Kit& kit, size_t count)
    {
        std::vector<int> all;
        for (int n = 0; n < 128; ++n)
            if (kit.notes[(size_t) n].piece >= 0)
                all.push_back (n);
        std::vector<int> out;
        for (size_t i = 0; i < count && ! all.empty(); ++i)
            out.push_back (all[i * all.size() / count]);
        return out;
    }
}

// Проверка на настоящей библиотеке. Запускается только если задана SAMPLIT_REAL_LIB (папка 00-Studio600ru-Drum-LIB),
// остальное берётся из репозитория: манифесты sample_library/packs-src и заводская установка Setup::factory().
// Печатает память и время загрузки и сравнивает потоковый звук с загрузкой целиком на нескольких настоящих ударах.
TEST_CASE ("real library: factory setup loads with heads only and streams like the whole files", "[.reallib]")
{
    const char* libPath = std::getenv ("SAMPLIT_REAL_LIB");
    if (libPath == nullptr)
        SKIP ("SAMPLIT_REAL_LIB не задана");

    const auto repo = juce::File (SAMPLIT_TEST_FIXTURES).getParentDirectory().getParentDirectory();
    KitLoadOptions opts;
    opts.libraryRoot = juce::File (libPath);
    opts.manifestDir = repo.getChildFile ("sample_library/packs-src");
    const auto spec = factorySpec (opts);
    REQUIRE (spec.places.size() == 12);

    const auto t0 = std::chrono::steady_clock::now();
    const auto res = loadKit (spec, opts);
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
    const auto ref = loadKit (spec, full);
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

    for (const int note : sampleNotes (*res.kit, 8))
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
    KitLoadOptions opts;
    opts.libraryRoot = juce::File (libPath);
    opts.manifestDir = repo.getChildFile ("sample_library/packs-src");
    const auto res = loadKit (factorySpec (opts), opts);
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

// Весь путь плагина на настоящей библиотеке: SamplitProcessor, MIDI, реальное время и офлайн-экспорт (setNonRealtime).
// Офлайн-прогон дважды: звук должен совпасть побитово (экспорт не зависит от скорости диска), недогрузок нет.
// Параметры: SAMPLIT_HOST_SECONDS (20), SAMPLIT_HOST_HITS (ударов в секунду, 12).
namespace
{
    struct HostRun
    {
        uint64_t hash = 0;
        double maxBlockUs = 0.0;
        int maxBlockIndex = 0, slowBlocks = 0;      // номер самого долгого блока и сколько блоков дольше 2 мс
        double meanBlockUs = 0.0;
        uint32_t underruns = 0, noSlot = 0, readErrors = 0;
        float peak = 0.0f;
        bool finite = true;
    };

    HostRun runHost (const char* libPath, bool offline, double seconds, double hitsPerSecond, int block)
    {
        constexpr double rate = 44100.0;
        const auto repo = juce::File (SAMPLIT_TEST_FIXTURES).getParentDirectory().getParentDirectory();
        auto dataDir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("samplit-tests").getChildFile ("host");
        dataDir.deleteRecursively();
        dataDir.getChildFile ("manifests").createDirectory();
        for (const auto& f : repo.getChildFile ("sample_library/packs-src").findChildFiles (juce::File::findFiles, false, "*.json"))
            f.copyFileTo (dataDir.getChildFile ("manifests").getChildFile (f.getFileName()));

        auto config = LicenseManager::defaultConfig();
        config.dataDir = dataDir;
        auto manager = std::make_unique<LicenseManager> (config);
        manager->setLibraryRoot (juce::File (libPath));

        SamplitProcessor p;
        p.setLicenseManager (std::move (manager));
        p.setNonRealtime (offline);
        p.setRateAndBufferSizeDetails (rate, block);
        p.prepareToPlay (rate, block);

        p.setSetup (Setup::factory());
        const auto until = juce::Time::getMillisecondCounter() + 180000;
        while ((p.getKit() == nullptr || p.isKitLoading()) && juce::Time::getMillisecondCounter() < until)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        REQUIRE (p.getKit() != nullptr);

        const auto kit = p.getKit();
        REQUIRE (kit != nullptr);
        std::vector<int> notes;
        for (int n = 0; n < 128; ++n)
            if (kit->notes[(size_t) n].piece >= 0)
                notes.push_back (n);

        juce::AudioBuffer<float> buffer (std::max (2, p.getTotalNumOutputChannels()), block);
        // Как аудио-поток в DAW: повышенный приоритет (SAMPLIT_HOST_PRIO=0 отключает, для сравнения)
        if (std::getenv ("SAMPLIT_HOST_PRIO") == nullptr || std::atoi (std::getenv ("SAMPLIT_HOST_PRIO")) != 0)
        {
           #if JUCE_WINDOWS
            ::SetThreadPriority (::GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
           #endif
        }
        uint32_t rng = 777;
        auto next = [&] { rng = rng * 1664525u + 1013904223u; return rng >> 8; };

        HostRun out;
        out.hash = 1469598103934665603ull;
        const int blocks = (int) (seconds * rate / block);
        const double hitsPerBlock = hitsPerSecond * block / rate;
        double debt = 0.0, total = 0.0;
        const auto begin = std::chrono::steady_clock::now();
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            debt += hitsPerBlock;
            while (debt >= 1.0)
            {
                debt -= 1.0;
                midi.addEvent (juce::MidiMessage::noteOn (1, notes[next() % notes.size()], (juce::uint8) (1 + next() % 127)),
                               (int) (next() % (uint32_t) block));
            }

            buffer.clear();
            const auto t0 = std::chrono::steady_clock::now();
            p.processBlock (buffer, midi);
            const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - t0).count();
            if (us > out.maxBlockUs)
                out.maxBlockIndex = b;
            out.maxBlockUs = std::max (out.maxBlockUs, us);
            out.slowBlocks += us > 2000.0 ? 1 : 0;
            total += us;

            for (int ch = 0; ch < std::min (2, buffer.getNumChannels()); ++ch)
                for (int i = 0; i < block; ++i)
                {
                    const float v = buffer.getSample (ch, i);
                    out.finite = out.finite && std::isfinite (v);
                    out.peak = std::max (out.peak, std::abs (v));
                    uint32_t bits;
                    std::memcpy (&bits, &v, 4);
                    out.hash = (out.hash ^ bits) * 1099511628211ull;
                }

            if (! offline)
                std::this_thread::sleep_until (begin + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
                                                   std::chrono::duration<double> ((b + 1) * block / rate)));
        }
        out.meanBlockUs = total / blocks;
        out.underruns = p.getStreamer().underruns.load();
        out.noSlot = p.getStreamer().noSlot.load();
        out.readErrors = p.getStreamer().readErrors.load();
        p.releaseResources();
        return out;
    }
}

TEST_CASE ("real library: whole plugin, realtime and offline export", "[.reallib-host]")
{
    const char* libPath = std::getenv ("SAMPLIT_REAL_LIB");
    if (libPath == nullptr)
        SKIP ("SAMPLIT_REAL_LIB не задана");
    auto env = [] (const char* name, double fallback)
    {
        const char* v = std::getenv (name);
        return v != nullptr ? std::atof (v) : fallback;
    };
    const double seconds = env ("SAMPLIT_HOST_SECONDS", 20);
    const double hits = env ("SAMPLIT_HOST_HITS", 12);

    const auto live = runHost (libPath, false, seconds, hits, 256);
    std::cout << "[host] реальное время: блок среднее " << live.meanBlockUs << " мкс, максимум " << live.maxBlockUs << " мкс в блоке " << live.maxBlockIndex << ", дольше 2 мс: " << live.slowBlocks << " (бюджет 5805), пик "
              << live.peak << ", недогрузок " << live.underruns << ", нет ячейки " << live.noSlot << ", ошибок чтения " << live.readErrors << std::endl;
    CHECK (live.finite);
    CHECK (live.peak > 0.0f);
    CHECK (live.underruns == 0);

    const auto a = runHost (libPath, true, seconds, hits, 4096);
    const auto b = runHost (libPath, true, seconds, hits, 4096);
    std::cout << "[host] офлайн: хеш " << a.hash << " / " << b.hash << ", пик " << a.peak << ", недогрузок " << a.underruns << "/" << b.underruns
              << ", блок максимум " << a.maxBlockUs << " мкс" << std::endl;
    CHECK (a.finite);
    CHECK (a.hash == b.hash);
    CHECK (a.underruns == 0);
    CHECK (b.underruns == 0);
}

// ---------------------------------------------------------------- пакеты v2 (.splpack со стримингом хвоста)
// SAMPLIT_PACKS_V2=<папка с *.splpack и keys\<id>.key> (сборка: tools\install_packs.ps1 -NoRegister -OutDir <папка>).
// Запускать Release: ctest не включает скрытые тесты, поэтому
//   $env:SAMPLIT_PACKS_V2='Z:\repo\samplit\build\packs-v2'; .\SamplitTests.exe "[.reallib-packs]"
//   ... "[.reallib-packs-stress]"   (SAMPLIT_CHORD_NOTES, SAMPLIT_CHORD_SECONDS, SAMPLIT_STRESS_BLOCK)
namespace
{
    bool packKeyFor (const juce::File& dir, const juce::String& packId, pack::PackKey& key)
    {
        const auto f = dir.getChildFile ("keys").getChildFile (packId + ".key");
        return f.existsAsFile() && pack::keyFromHex (f.loadFileAsString().trim(), key);
    }
}

TEST_CASE ("real packs v2: decodeTailChunk time and head RAM", "[.reallib-packs]")
{
    const char* dirPath = std::getenv ("SAMPLIT_PACKS_V2");
    if (dirPath == nullptr)
        SKIP ("SAMPLIT_PACKS_V2 не задана");
    const juce::File dir (dirPath);

    std::vector<double> firstMs, otherMs;
    double totalWhole = 0, totalHead = 0;
    int files = 0, streamed = 0;
    for (const auto& f : dir.findChildFiles (juce::File::findFiles, false, "*.splpack"))
    {
        juce::String err;
        auto reader = pack::PackReader::open (f, err);
        REQUIRE (reader != nullptr);
        pack::PackKey key {};
        REQUIRE (packKeyFor (dir, f.getFileNameWithoutExtension(), key));
        pack::Manifest m;
        REQUIRE (reader->readManifest (key, m, err));

        double whole = 0, head = 0;
        int n = 0, nStreamed = 0, step = 0;
        std::vector<float> buf (200000);
        for (const auto& piece : m.pieces)
            for (const auto& hit : piece.hits)
                for (const auto& [micId, file] : hit.files)
                {
                    ++n;
                    whole += (double) file.frames * 4.0;
                    if (file.tailFirstChunk < 0)
                    {
                        head += (double) file.frames * 4.0;
                        continue;
                    }
                    ++nStreamed;
                    head += (double) file.headFrames * 4.0;
                    if (++step % 4 != 0)        // замер на каждом 4-м файле: достаточно, чтобы не читать весь пак
                        continue;
                    for (int c = 0; c < std::min (2, pack::tailChunkCount (file)); ++c)
                    {
                        int frames = 0;
                        const auto t0 = std::chrono::steady_clock::now();
                        REQUIRE (reader->decodeTailChunk (key, file, c, buf.data(), (int) buf.size(), frames, err));
                        const double ms = std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count();
                        (c == 0 ? firstMs : otherMs).push_back (ms);
                    }
                }
        std::cout << "[packs] " << f.getFileNameWithoutExtension() << ": файлов " << n << " (с хвостом " << nStreamed << "), целиком "
                  << whole / 1048576.0 << " МБ, голова " << head / 1048576.0 << " МБ\n";
        totalWhole += whole;
        totalHead += head;
        files += n;
        streamed += nStreamed;
    }
    REQUIRE (files > 0);
    auto stat = [] (std::vector<double> v, const char* name)
    {
        if (v.empty())
            return;
        std::sort (v.begin(), v.end());
        std::cout << "[packs] " << name << " (n=" << v.size() << "), мс: медиана " << v[v.size() / 2] << ", p95 "
                  << v[std::min (v.size() - 1, (size_t) (0.95 * (double) v.size()))] << ", максимум " << v.back() << "\n";
    };
    stat (firstMs, "decodeTailChunk, 1-й кусок");
    stat (otherMs, "decodeTailChunk, 2-й кусок");
    std::cout << "[packs] ИТОГО: файлов " << files << " (с хвостом " << streamed << "), RAM целиком " << totalWhole / 1048576.0
              << " МБ, только головы " << totalHead / 1048576.0 << " МБ, экономия " << 100.0 * (1.0 - totalHead / totalWhole) << " %" << std::endl;
}

TEST_CASE ("real packs v2: chords of many cells, one streamer thread", "[.reallib-packs-stress]")
{
    const char* dirPath = std::getenv ("SAMPLIT_PACKS_V2");
    if (dirPath == nullptr)
        SKIP ("SAMPLIT_PACKS_V2 не задана");
    auto env = [] (const char* name, double fallback)
    {
        const char* v = std::getenv (name);
        return v != nullptr ? std::atof (v) : fallback;
    };
    const int chordNotes = (int) env ("SAMPLIT_CHORD_NOTES", 8);
    const double seconds = env ("SAMPLIT_CHORD_SECONDS", 20);
    const int block = (int) env ("SAMPLIT_STRESS_BLOCK", 256);
    constexpr double rate = 44100.0;
    const juce::File dir (dirPath);

    KitLoadOptions opts;
    opts.packDir = dir;
    opts.keyFor = [dir] (const juce::String& id, pack::PackKey& key) { return packKeyFor (dir, id, key); };
    const auto catalog = scanPackCatalog (opts, false);
    std::vector<juce::String> warnings;
    const auto spec = makeKitSpec (Setup::factory(), catalog, false, warnings);
    const auto t0 = std::chrono::steady_clock::now();
    const auto res = loadKit (spec, opts);
    REQUIRE (res.ok());
    std::cout << "[chord] загрузка " << std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() << " с, голов в RAM "
              << (double) res.memoryBytes / 1048576.0 << " МБ" << std::endl;

    std::vector<int> all;
    for (int n = 0; n < 128; ++n)
        if (res.kit->notes[(size_t) n].piece >= 0)
            all.push_back (n);
    REQUIRE (! all.empty());

    DiskStreamer streamer (true);
    KitEngine engine;
    engine.setStreamer (&streamer);
    engine.prepare (rate);
    engine.setKit (res.kit.get());
    // setKit просит 256 ячеек, выделяет их поток стримера: ждём, как плагин успевает за время загрузки пресета
    for (int i = 0; i < 1000 && streamer.getAllocatedSlots() < 256; ++i)
        std::this_thread::sleep_for (std::chrono::milliseconds (2));

    std::vector<float> L ((size_t) block), R ((size_t) block);
    std::array<float*, kNumOutputs + 1> pl {}, pr {};
    for (size_t i = 0; i < pl.size(); ++i)
    {
        pl[i] = L.data();
        pr[i] = i == 0 ? R.data() : nullptr;
    }
    const int blocks = (int) (seconds * rate / block);
    const double blockSeconds = block / rate;
    const int waveEvery = (int) (0.4 / blockSeconds);       // аккорд каждые 0,4 с
    uint32_t rng = 777;
    auto next = [&] { rng = rng * 1664525u + 1013904223u; return rng >> 8; };
    int maxCells = 0, chords = 0;
    std::vector<double> times;
    const auto begin = std::chrono::steady_clock::now();
    for (int b = 0; b < blocks; ++b)
    {
        if (b % waveEvery == 0)
        {
            ++chords;
            for (int i = 0; i < chordNotes; ++i)
                engine.noteOn (all[next() % all.size()], 1 + (int) (next() % 127));
        }
        const auto r0 = std::chrono::steady_clock::now();
        std::fill (L.begin(), L.end(), 0.0f);
        std::fill (R.begin(), R.end(), 0.0f);
        engine.render (pl, pr, 0, block);
        times.push_back (std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - r0).count());
        maxCells = std::max (maxCells, streamer.numActive());
        std::this_thread::sleep_until (begin + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
                                           std::chrono::duration<double> ((b + 1) * blockSeconds)));
    }
    std::sort (times.begin(), times.end());
    std::cout << "[chord] " << seconds << " с, аккордов " << chords << " по " << chordNotes << " ударов, ячеек макс. " << maxCells
              << ", блок мкс: p99 " << times[(size_t) (0.99 * (double) times.size())] << ", макс " << times.back() << "\n"
              << "[chord] недогрузок " << streamer.underruns.load() << ", нет ячейки " << streamer.noSlot.load()
              << ", ошибок чтения " << streamer.readErrors.load() << std::endl;
    CHECK (streamer.readErrors.load() == 0);
    CHECK (streamer.underruns.load() == 0);
    streamer.shutdown();
}

// Палитра ударов на настоящих манифестах: для каждого места заводской установки не пуста, числа попадают в окна
TEST_CASE ("real library: strength palette of every place is not empty", "[.reallib-palette]")
{
    const char* libPath = std::getenv ("SAMPLIT_REAL_LIB");
    if (libPath == nullptr)
        SKIP ("SAMPLIT_REAL_LIB не задана");

    const auto repo = juce::File (SAMPLIT_TEST_FIXTURES).getParentDirectory().getParentDirectory();
    KitLoadOptions opts;
    opts.libraryRoot = juce::File (libPath);
    opts.manifestDir = repo.getChildFile ("sample_library/packs-src");
    const auto res = loadKit (factorySpec (opts), opts);
    REQUIRE (res.ok());

    for (const auto& def : kStudio600Places)
    {
        const auto pal = buildPalette (*res.kit, def.slot);
        INFO ("slot " << def.id);
        CHECK_FALSE (pal.empty());
        for (const auto& o : pal)
        {
            const auto& map = res.kit->notes[(size_t) o.key];
            bool inWindow = false;
            for (const auto& ref : map.layers)
                inWindow = inWindow || (o.velocity >= ref.velLo && o.velocity <= ref.velHi);
            CHECK (inWindow);
        }
    }
}

TEST_CASE ("real library: snare crescendo pairs with every tom at the same velocities", "[.reallib-palette]")
{
    const char* libPath = std::getenv ("SAMPLIT_REAL_LIB");
    if (libPath == nullptr)
        SKIP ("SAMPLIT_REAL_LIB не задана");

    const auto repo = juce::File (SAMPLIT_TEST_FIXTURES).getParentDirectory().getParentDirectory();
    KitLoadOptions opts;
    opts.libraryRoot = juce::File (libPath);
    opts.manifestDir = repo.getChildFile ("sample_library/packs-src");
    const auto res = loadKit (factorySpec (opts), opts);
    REQUIRE (res.ok());
    const auto catalog = scanPackCatalog (opts, false);
    const auto setup = Setup::factory();

    auto place = [&] (const char* id)
    {
        const auto* def = findPlace (id);
        REQUIRE (def != nullptr);
        pack::Piece meta;
        REQUIRE (findPieceMeta (catalog, *def, *setup.find (id), meta));
        return PlacePalette { def->slot, def->id, def->type, buildPalette (*res.kit, def->slot, &meta) };
    };

    const auto snare = place ("snare");
    std::vector<StrengthOption> crescendo;
    for (const auto& o : snare.options)
        if (o.groupId == "crescendo")
            crescendo.push_back (o);
    REQUIRE (crescendo.size() > 1);
    CHECK (crescendo.front().exact);
    CHECK (crescendo.front().pairType == "tom");

    for (const char* id : { "tom_16l", "tom_16m", "tom_14", "tom_10" })
    {
        const auto tom = place (id);
        INFO ("place " << id);
        for (const auto& o : crescendo)
        {
            REQUIRE (canPair (tom, o));
            const auto& po = tom.options[(size_t) pairOption (tom.options, o)];
            // «Сила совпадает»; исключение: у Альта 12 удары 70 и 71 вырезаны (испорченные файлы, open-questions.md §3.1),
            // пара берёт ближайшее число
            if (juce::String (id) == "tom_14" && (o.velocity == 70 || o.velocity == 71))
                CHECK (std::abs (po.velocity - o.velocity) <= 2);
            else
                CHECK (po.velocity == o.velocity);
            CHECK (canPair (snare, po));                    // и обратно: том -> малый
        }
    }
}

