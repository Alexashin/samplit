#include "PatternPlayer.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>
#include <tuple>
#include <vector>

using samplit::Pattern;
using samplit::PatternPlayer;

namespace
{
    constexpr double kRate = 48000.0;

    Pattern makePattern (int bars = 1)
    {
        Pattern p;
        p.bars = bars;
        p.bpm = 120.0;                  // шаг 1/16 = 6000 отсчётов, такт = 96000
        p.setStep (0, 0, 24, 120);
        p.setStep (4, 1, 26, 100);
        p.setStep (7, 6, 48, 90);
        p.setStep (15, 6, 48, 80);
        return p;
    }

    // Абсолютные события (позиция от запуска, клавиша, velocity, зерно) за totalSamples при блоках размера blockSize
    std::vector<std::tuple<int64_t, int, int, int64_t>> run (PatternPlayer& pl, int64_t total, int blockSize)
    {
        std::vector<std::tuple<int64_t, int, int, int64_t>> out;
        PatternPlayer::Hit hits[PatternPlayer::kMaxHits];
        for (int64_t at = 0; at < total; at += blockSize)
        {
            const int n = (int) std::min<int64_t> (blockSize, total - at);
            const int c = pl.process (n, hits, PatternPlayer::kMaxHits);
            for (int i = 0; i < c; ++i)
                out.emplace_back (at + hits[i].offset, hits[i].key, hits[i].velocity, hits[i].seedPos);
        }
        return out;
    }
}

TEST_CASE ("pattern player: same events for any block size", "[pattern_player]")
{
    const auto pat = makePattern();
    const int64_t len = pat.lengthSamples (kRate);
    std::vector<decltype (run (*(PatternPlayer*) nullptr, 0, 1))> results;
    for (int bs : { 7, 64, 100, 512 })
    {
        PatternPlayer pl;
        pl.setPattern (pat, kRate);
        pl.play();
        results.push_back (run (pl, len * 3, bs));
    }
    REQUIRE (results[0].size() == 12);
    for (size_t i = 1; i < results.size(); ++i)
        REQUIRE (results[i] == results[0]);
}

TEST_CASE ("pattern player: loop wraps inside a block and seed grows by length per pass", "[pattern_player]")
{
    const auto pat = makePattern();
    const int64_t len = pat.lengthSamples (kRate);
    PatternPlayer pl;
    pl.setPattern (pat, kRate);
    pl.play();
    const auto ev = run (pl, len * 2, (int) len + 1000);        // блок длиннее партии: два кусочка внутри блока
    REQUIRE (ev.size() == 8);
    for (int i = 0; i < 4; ++i)
    {
        REQUIRE (std::get<0> (ev[(size_t) i + 4]) == std::get<0> (ev[(size_t) i]) + len);
        REQUIRE (std::get<3> (ev[(size_t) i + 4]) == std::get<3> (ev[(size_t) i]) + len);
    }
}

TEST_CASE ("pattern player: without loop stops at the end", "[pattern_player]")
{
    const auto pat = makePattern();
    PatternPlayer pl;
    pl.setPattern (pat, kRate);
    pl.setLoop (false);
    pl.play();
    const auto ev = run (pl, pat.lengthSamples (kRate) * 3, 512);
    REQUIRE (ev.size() == 4);
    REQUIRE_FALSE (pl.isPlaying());
    pl.play();                                                  // повторный запуск снова с начала
    REQUIRE (run (pl, pat.lengthSamples (kRate) * 2, 512).size() == 4);
}

TEST_CASE ("pattern player: idle, empty pattern, and new sample rate", "[pattern_player]")
{
    PatternPlayer pl;
    PatternPlayer::Hit hits[PatternPlayer::kMaxHits];
    REQUIRE (pl.process (512, hits, PatternPlayer::kMaxHits) == 0);     // не запущен
    pl.play();
    REQUIRE (pl.process (512, hits, PatternPlayer::kMaxHits) == 0);     // партии нет
    REQUIRE_FALSE (pl.isPlaying());

    pl.setPattern (makePattern(), kRate);
    pl.setSampleRate (96000.0);
    pl.play();
    const auto ev = run (pl, 96000 * 2, 256);                           // такт на 96 кГц = 192000 отсчётов, второй шаг 12000*4
    REQUIRE (std::get<0> (ev[1]) == 48000);
}

TEST_CASE ("pattern player: swapping the pattern from another thread during processing", "[pattern_player]")
{
    PatternPlayer pl;
    pl.setPattern (makePattern (1), kRate);
    pl.play();
    std::atomic<bool> stopFlag { false };
    std::thread swapper ([&]
    {
        int i = 0;
        while (! stopFlag.load())
        {
            pl.setPattern (makePattern (1 + (i++ % 4)), kRate);
            std::this_thread::yield();
        }
    });
    PatternPlayer::Hit hits[PatternPlayer::kMaxHits];
    long total = 0;
    for (int i = 0; i < 20000; ++i)
        total += pl.process (256, hits, PatternPlayer::kMaxHits);
    stopFlag.store (true);
    swapper.join();
    REQUIRE (total > 0);
}
