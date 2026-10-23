#include "KitEngine.h"
#include "Interpolation.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>

namespace samplit
{
    namespace
    {
        constexpr float kInt16Scale = 1.0f / 32768.0f;

        // Один микрофонный голос на [start, end). false: голос закончился
        template <bool S16, bool Mono>
        bool renderVoice (const DecodedClip& clip, double& position, double step, int& fadeLeft, int fadeLength,
                          float* left, float* right, float gainL, float gainR, int start, int end) noexcept
        {
            const int frames = clip.frames();
            const int16_t* p16 = clip.s16.data();
            const float* pf = clip.f32.data();
            // Частоты совпадают и высота не сдвинута: позиция целая, интерполировать нечего
            const bool exact = step == 1.0;

            auto at = [&] (int k) noexcept -> float
            {
                if constexpr (S16)
                    return (float) p16[k] * kInt16Scale;
                else
                    return pf[k];
            };

            for (int i = start; i < end; ++i)
            {
                const int idx = (int) position;
                if (idx + 1 >= frames)
                    return false;

                float fade = 1.0f;
                if (fadeLeft >= 0)
                {
                    if (fadeLeft == 0)
                        return false;
                    fade = (float) fadeLeft / (float) fadeLength;
                    --fadeLeft;
                }

                // Кубическая интерполяция Эрмита; на краях клипа недостающие соседи заменяются ближайшими
                float value;
                if (exact)
                {
                    value = at (idx);
                }
                else
                {
                    const float frac = (float) (position - idx);
                    const int im1 = idx > 0 ? idx - 1 : idx;
                    const int i2 = idx + 2 < frames ? idx + 2 : idx + 1;
                    value = hermite (at (im1), at (idx), at (idx + 1), at (i2), frac);
                }

                const float s = value * fade;
                left[i] += s * gainL;
                if constexpr (! Mono)
                    right[i] += s * gainR;
                position += step;
            }
            return true;
        }
    }

    KitEngine::KitEngine()
    {
        for (auto& l : micLevel)
            l.store (1.0f);
        for (auto& p : micPan)
            p.store (0.0f);
        for (auto& o : micOut)
            o.store (-1);
        lastHit.fill (-1);
    }

    void KitEngine::prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        fadeLength = std::max (1, (int) (0.005 * sr));  // 5 мс на глушение
        for (int i = 0; i < kMaxStrikes; ++i)
            release (i);
        lastHit.fill (-1);
    }

    void KitEngine::setKit (const Kit* kit) noexcept
    {
        if (kit != nullptr)
        {
            selectMode.store ((int) kit->mode);
            spread.store (std::max (0, kit->spread));
        }
        current.store (kit);
    }

    void KitEngine::setMicMix (int mic, float levelDb, float pan) noexcept
    {
        if (mic < 0 || mic >= Kit::kMaxMics)
            return;
        micLevel[(size_t) mic].store (juce::Decibels::decibelsToGain (levelDb, -100.0f));
        micPan[(size_t) mic].store (std::clamp (pan, -1.0f, 1.0f));
    }

    void KitEngine::setMicOutput (int mic, int out) noexcept
    {
        if (mic >= 0 && mic < Kit::kMaxMics)
            micOut[(size_t) mic].store (out < 0 ? -1 : std::min (out, kNumOutputs));
    }

    bool KitEngine::isUsing (const Kit* kit) const noexcept
    {
        if (current.load() == kit)
            return true;
        for (const auto& k : strikeKit)
            if (k.load() == kit)
                return true;
        return false;
    }

    int KitEngine::getNumActiveStrikes() const noexcept
    {
        int n = 0;
        for (const auto& k : strikeKit)
            n += k.load() != nullptr ? 1 : 0;
        return n;
    }

    uint32_t KitEngine::nextRandom() noexcept
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    }

    void KitEngine::release (int index) noexcept
    {
        auto& s = strikes[(size_t) index];
        s.numActive = 0;
        s.piece = -1;
        s.note = -1;
        for (auto& v : s.voices)
            v.clip = nullptr;
        strikeKit[(size_t) index].store (nullptr);
    }

    void KitEngine::fadeStrike (Strike& s) noexcept
    {
        for (auto& v : s.voices)
            if (v.clip != nullptr && v.fadeLeft < 0)
                v.fadeLeft = fadeLength;
    }

    int KitEngine::selectHit (const Kit& kit, const NoteMap& map, int note, int velocity) noexcept
    {
        const auto& piece = kit.pieces[(size_t) map.piece];

        if (selectMode.load() == (int) HitSelect::Kontakt)
        {
            // Точное окно velocity; пересечения окон разрешаются первым подходящим ударом
            for (const auto& l : map.layers)
                for (int h : l.hits)
                {
                    const auto& hit = piece.hits[(size_t) h];
                    if (velocity >= hit.velLo && velocity <= hit.velHi)
                        return h;
                }
            return -1;
        }

        // Live: слой, в который попала velocity, а в щели между окнами ближайший (при равенстве мягче).
        // Слой с pick "exact" играет только точный удар: без случайного выбора и без соседних слоёв
        const int numLayers = (int) map.layers.size();
        int chosen = -1;
        int bestDistance = 1 << 30;
        for (int i = 0; i < numLayers; ++i)
        {
            const auto& l = map.layers[(size_t) i];
            const int distance = velocity < l.velLo ? l.velLo - velocity : (velocity > l.velHi ? velocity - l.velHi : 0);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                chosen = i;
            }
        }
        if (chosen < 0)
            return -1;

        if (map.layers[(size_t) chosen].pickExact)
        {
            int best = -1;
            int bestHitDistance = 1 << 30;
            for (int h : map.layers[(size_t) chosen].hits)
            {
                const auto& hit = piece.hits[(size_t) h];
                const int d = velocity < hit.velLo ? hit.velLo - velocity : (velocity > hit.velHi ? velocity - hit.velHi : 0);
                if (d < bestHitDistance)
                {
                    bestHitDistance = d;
                    best = h;
                }
            }
            return best;
        }

        const int sp = spread.load();
        const int first = std::max (0, chosen - sp);
        const int last = std::min (numLayers - 1, chosen + sp);

        // Случайный удар из выбранных слоёв, но не тот, что играл на этой клавише в прошлый раз
        const int previous = lastHit[(size_t) note];
        int total = 0;
        bool hasPrevious = false;
        for (int i = first; i <= last; ++i)
            for (int h : map.layers[(size_t) i].hits)
            {
                ++total;
                hasPrevious = hasPrevious || h == previous;
            }

        const int candidates = hasPrevious && total > 1 ? total - 1 : total;
        if (candidates <= 0)
            return -1;

        int k = (int) (nextRandom() % (uint32_t) candidates);
        for (int i = first; i <= last; ++i)
            for (int h : map.layers[(size_t) i].hits)
            {
                if (hasPrevious && total > 1 && h == previous)
                    continue;
                if (k-- == 0)
                    return h;
            }
        return -1;
    }

    KitEngine::StrikeInfo KitEngine::noteOn (int note, int velocity) noexcept
    {
        StrikeInfo info;
        const Kit* kit = current.load();
        if (kit == nullptr || note < 0 || note > 127)
            return info;

        if (kit != lastKit)
        {
            lastKit = kit;
            lastHit.fill (-1);
        }

        velocity = std::clamp (velocity, 1, 127);
        const auto& map = kit->notes[(size_t) note];
        if (map.piece < 0)
            return info;

        const int h = selectHit (*kit, map, note, velocity);
        if (h < 0)
            return info;

        const auto& piece = kit->pieces[(size_t) map.piece];
        const auto& hit = piece.hits[(size_t) h];
        lastHit[(size_t) note] = h;
        info.piece = map.piece;
        info.hit = h;
        info.layer = hit.layer;

        // Choke: барабан этой группы заглушает остальные барабаны группы
        if (piece.choke >= 0)
            for (int i = 0; i < kMaxStrikes; ++i)
                if (strikeKit[(size_t) i].load() == kit && strikes[(size_t) i].piece != map.piece
                    && kit->pieces[(size_t) strikes[(size_t) i].piece].choke == piece.choke)
                    fadeStrike (strikes[(size_t) i]);

        // Свободная ячейка или самый старый удар
        int slot = -1;
        for (int i = 0; i < kMaxStrikes; ++i)
            if (strikeKit[(size_t) i].load() == nullptr)
            {
                slot = i;
                break;
            }
        if (slot < 0)
        {
            slot = 0;
            for (int i = 1; i < kMaxStrikes; ++i)
                if (strikes[(size_t) i].age < strikes[(size_t) slot].age)
                    slot = i;
        }

        auto& s = strikes[(size_t) slot];
        const float velGain = kit->velocityGain == VelocityGain::Linear ? (float) velocity / 127.0f : 1.0f;
        int started = 0;
        for (int m = 0; m < (int) hit.files.size(); ++m)
        {
            auto& v = s.voices[(size_t) m];
            v.clip = nullptr;
            const auto& file = hit.files[(size_t) m];
            if (file.clip == nullptr || file.clip->frames() < 2)
                continue;

            v.clip = file.clip.get();
            v.position = 0.0;
            v.step = file.clip->sampleRate / sr;
            v.gain = velGain * juce::Decibels::decibelsToGain (file.gainDb, -100.0f);
            v.mic = m;
            v.fadeLeft = -1;
            ++started;
        }
        for (int m = (int) hit.files.size(); m < Kit::kMaxMics; ++m)
            s.voices[(size_t) m].clip = nullptr;

        info.voices = started;
        if (started == 0)
        {
            release (slot);
            return info;
        }

        s.piece = map.piece;
        s.note = note;
        s.numActive = started;
        s.age = ++ageCounter;
        strikeKit[(size_t) slot].store (kit);
        return info;
    }

    void KitEngine::render (const std::array<float*, kNumOutputs + 1>& outL, const std::array<float*, kNumOutputs + 1>& outR,
                            int start, int end) noexcept
    {
        for (int si = 0; si < kMaxStrikes; ++si)
        {
            const Kit* kit = strikeKit[(size_t) si].load();
            if (kit == nullptr)
                continue;

            auto& s = strikes[(size_t) si];
            for (auto& v : s.voices)
            {
                if (v.clip == nullptr)
                    continue;

                const int over = micOut[(size_t) v.mic].load();
                const int bus = std::clamp (over >= 0 ? over : kit->mics[(size_t) v.mic].out, 0, kNumOutputs);
                float* left = outL[(size_t) bus];
                float* right = outR[(size_t) bus];
                const float level = micLevel[(size_t) v.mic].load();
                const float gain = v.gain * level;
                const bool mono = right == nullptr;

                // Панорама как баланс: центр не меняет уровень, крайнее положение глушит противоположный канал
                const float pan = micPan[(size_t) v.mic].load();
                const float gainL = mono ? gain : gain * std::min (1.0f, 1.0f - pan);
                const float gainR = mono ? 0.0f : gain * std::min (1.0f, 1.0f + pan);

                bool alive;
                if (v.clip->isInt16())
                    alive = mono ? renderVoice<true, true> (*v.clip, v.position, v.step, v.fadeLeft, fadeLength, left, right, gainL, gainR, start, end)
                                 : renderVoice<true, false> (*v.clip, v.position, v.step, v.fadeLeft, fadeLength, left, right, gainL, gainR, start, end);
                else
                    alive = mono ? renderVoice<false, true> (*v.clip, v.position, v.step, v.fadeLeft, fadeLength, left, right, gainL, gainR, start, end)
                                 : renderVoice<false, false> (*v.clip, v.position, v.step, v.fadeLeft, fadeLength, left, right, gainL, gainR, start, end);

                if (! alive)
                {
                    v.clip = nullptr;
                    --s.numActive;
                }
            }

            if (s.numActive <= 0)
                release (si);
        }
    }

    void KitEngine::publishActivity() noexcept
    {
        uint64_t lo = 0, hi = 0;
        for (int i = 0; i < kMaxStrikes; ++i)
        {
            if (strikeKit[(size_t) i].load() == nullptr)
                continue;
            const int note = strikes[(size_t) i].note;
            if (note < 0)
                continue;
            if (note < 64)
                lo |= 1ull << note;
            else
                hi |= 1ull << (note - 64);
        }
        activeLo.store (lo);
        activeHi.store (hi);
    }
}
