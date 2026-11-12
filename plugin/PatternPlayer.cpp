#include "PatternPlayer.h"

#include <algorithm>

namespace samplit
{
    PatternPlayer::~PatternPlayer() = default;

    void PatternPlayer::publish (const Pattern& p, double sampleRate)
    {
        auto play = std::make_unique<Play>();
        play->events = p.buildEvents (sampleRate);
        play->length = p.lengthSamples (sampleRate);
        const Play* raw = play.get();
        owned.push_back (std::move (play));
        current.store (raw, std::memory_order_seq_cst);
        collectGarbage();
    }

    void PatternPlayer::setPattern (const Pattern& p, double sampleRate)
    {
        pattern = p;
        rate = sampleRate > 0.0 ? sampleRate : rate;
        publish (pattern, rate);
    }

    void PatternPlayer::setSampleRate (double sampleRate)
    {
        if (sampleRate <= 0.0 || sampleRate == rate)
            return;
        rate = sampleRate;
        publish (pattern, rate);
    }

    void PatternPlayer::play() noexcept
    {
        restart.fetch_add (1);          // play() во время игры тоже начинает с начала
        playing.store (true);
    }
    void PatternPlayer::stop() noexcept { playing.store (false); }

    void PatternPlayer::collectGarbage()
    {
        // Снимок живёт, пока он текущий или защищён hazard: порядок проверки как у KitEngine::isUsing
        const Play* cur = current.load();
        const Play* hz = hazard.load();
        std::erase_if (owned, [&] (const std::unique_ptr<Play>& p) { return p.get() != cur && p.get() != hz; });
    }

    int PatternPlayer::process (int numSamples, Hit* out, int maxOut) noexcept
    {
        const bool isPlayingNow = playing.load();
        if (! isPlayingNow)
        {
            wasPlaying = false;
            return 0;
        }
        const int restartNow = restart.load();
        if (! wasPlaying || restartNow != seenRestart)      // запуск или перезапуск: с начала партии
        {
            seenRestart = restartNow;
            wasPlaying = true;
            pos = 0;
            pass = 0;
        }

        const Play* k = current.load();
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            hazard.store (k, std::memory_order_seq_cst);
            const Play* again = current.load();
            if (again == k)
                break;
            k = again;
            if (attempt == 7)
                k = nullptr;
        }
        int count = 0;
        if (k != nullptr && k->length > 0)
        {
            if (pos >= k->length)       // партию укоротили под курсором
                pos = 0;
            const bool doLoop = loop.load();
            int done = 0;
            while (done < numSamples)
            {
                const int64_t segEnd = std::min<int64_t> (k->length, pos + (numSamples - done));
                const auto first = std::lower_bound (k->events.begin(), k->events.end(), pos,
                                                     [] (const PatternEvent& e, int64_t p) { return e.sample < p; });
                for (auto it = first; it != k->events.end() && it->sample < segEnd; ++it)
                    if (count < maxOut)
                        out[count++] = { done + (int) (it->sample - pos), it->key, it->velocity, pass * k->length + it->sample };
                done += (int) (segEnd - pos);
                pos = segEnd;
                if (pos >= k->length)
                {
                    if (doLoop)
                    {
                        pos = 0;
                        ++pass;
                    }
                    else
                    {
                        playing.store (false);
                        wasPlaying = false;
                        break;
                    }
                }
            }
        }
        else
        {
            playing.store (false);          // пустая партия играть нечего
            wasPlaying = false;
        }
        position.store (pos, std::memory_order_relaxed);
        hazard.store (nullptr, std::memory_order_release);
        return count;
    }
}
