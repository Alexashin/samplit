#include "SamplerEngine.h"
#include "Interpolation.h"

#include <algorithm>
#include <cmath>

namespace samplit
{
    namespace
    {
        constexpr int kPreviewVoice = kMaxVoices;
    }

    void SamplerEngine::prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        fadeLength = std::max (1, (int) (0.005 * sr));      // 5 мс на глушение
        fadeInStep = 1.0f / (float) std::max (1, (int) (0.002 * sr));  // 2 мс на нарастание
        for (auto& v : voices)
            finish (v);
    }

    void SamplerEngine::finish (Voice& v) noexcept
    {
        v.sample = nullptr;
        v.fadeLeft = -1;
        v.note = -1;
        playing[(size_t) (&v - voices.data())].store (nullptr);
    }

    uint32_t SamplerEngine::nextRandom() noexcept
    {
        rng ^= rng << 13;
        rng ^= rng >> 17;
        rng ^= rng << 5;
        return rng;
    }

    bool SamplerEngine::isUsing (const Sample* sample) const noexcept
    {
        if (previewSlot.load() == sample)
            return true;
        for (const auto& p : playing)
            if (p.load() == sample)
                return true;
        return false;
    }

    bool SamplerEngine::isUsing (const Instrument* instrument) const noexcept
    {
        if (current.load() == instrument)
            return true;
        for (const auto& zone : instrument->zones)
            if (zone.sample != nullptr && isUsing (zone.sample.get()))
                return true;
        return false;
    }

    int SamplerEngine::getNumActiveVoices() const noexcept
    {
        int n = 0;
        for (const auto& p : playing)
            n += p.load() != nullptr ? 1 : 0;
        return n;
    }

    void SamplerEngine::startVoice (const Zone& zone, const Group& group, int note, int velocity) noexcept
    {
        Voice* slot = nullptr;
        for (int i = 0; i < kMaxVoices; ++i)
            if (voices[(size_t) i].sample == nullptr)
            {
                slot = &voices[(size_t) i];
                break;
            }

        // Все заняты: забираем самый старый голос
        if (slot == nullptr)
        {
            slot = &voices[0];
            for (int i = 1; i < kMaxVoices; ++i)
                if (voices[(size_t) i].age < slot->age)
                    slot = &voices[(size_t) i];
        }

        const Sample* s = zone.sample.get();
        const double pitch = group.tracking ? std::exp2 ((note - zone.rootKey) / 12.0) : 1.0;
        const float gain = ((float) velocity / 127.0f)
                         * juce::Decibels::decibelsToGain (zone.gainDb + group.gainDb, -100.0f);
        // Панорама как баланс: центр не меняет уровень, крайнее положение глушит противоположный канал
        const float pan = juce::jlimit (-1.0f, 1.0f, group.pan);

        slot->sample = s;
        slot->position = 0.0;
        slot->step = s->sampleRate / sr * pitch;
        slot->gainL = gain * std::min (1.0f, 1.0f - pan);
        slot->gainR = gain * std::min (1.0f, 1.0f + pan);
        slot->fadeIn = 0.0f;
        slot->fadeLeft = -1;
        slot->output = juce::jlimit (0, kNumOutputs, group.output);
        slot->note = note;
        slot->age = ++ageCounter;
        playing[(size_t) (slot - voices.data())].store (s);
    }

    int SamplerEngine::noteOn (int note, int velocity) noexcept
    {
        const auto* inst = current.load();
        if (inst == nullptr)
            return 0;

        velocity = juce::jlimit (1, 127, velocity);
        const int numGroups = std::min ((int) inst->groups.size(), kMaxGroups);

        // Первый проход: сколько зон каждой группы подходит
        std::array<int, kMaxGroups> count {};
        for (const auto& z : inst->zones)
            if (z.sample != nullptr && z.group >= 0 && z.group < numGroups && z.matches (note, velocity))
                ++count[(size_t) z.group];

        // Какая по счёту подходящая зона играет в каждой группе (-1: все)
        std::array<int, kMaxGroups> pick {};
        for (int g = 0; g < numGroups; ++g)
        {
            const int n = count[(size_t) g];
            switch (inst->groups[(size_t) g].mode)
            {
                case PlayMode::All:    pick[(size_t) g] = -1; break;
                case PlayMode::Cycle:  pick[(size_t) g] = n > 0 ? (int) (cycle[(size_t) g]++ % (uint32_t) n) : 0; break;
                case PlayMode::Random: pick[(size_t) g] = n > 0 ? (int) (nextRandom() % (uint32_t) n) : 0; break;
            }
        }

        // Второй проход: запуск
        std::array<int, kMaxGroups> seen {};
        int started = 0;
        for (const auto& z : inst->zones)
        {
            if (z.sample == nullptr || z.group < 0 || z.group >= numGroups || ! z.matches (note, velocity))
                continue;

            const int order = seen[(size_t) z.group]++;
            if (pick[(size_t) z.group] >= 0 && pick[(size_t) z.group] != order)
                continue;

            startVoice (z, inst->groups[(size_t) z.group], note, velocity);
            ++started;
        }
        return started;
    }

    void SamplerEngine::previewOn() noexcept
    {
        auto& v = voices[kPreviewVoice];
        const Sample* s = previewSlot.load();
        if (s == nullptr || s->audio.getNumSamples() < 2)
        {
            finish (v);
            return;
        }

        v.sample = s;
        v.position = 0.0;
        v.step = s->sampleRate / sr;
        v.gainL = v.gainR = 1.0f;
        v.fadeIn = 1.0f;
        v.fadeLeft = -1;
        v.output = 0;
        v.note = -1;
        v.age = ++ageCounter;
        playing[kPreviewVoice].store (s);
    }

    void SamplerEngine::stopPreview() noexcept
    {
        auto& v = voices[kPreviewVoice];
        if (v.sample != nullptr && v.fadeLeft < 0)
            v.fadeLeft = fadeLength;
    }

    void SamplerEngine::render (const std::array<float*, kNumOutputs + 1>& outL,
                                const std::array<float*, kNumOutputs + 1>& outR, int start, int end) noexcept
    {
        for (auto& v : voices)
        {
            if (v.sample == nullptr)
                continue;

            const auto& audio = v.sample->audio;
            const int length = audio.getNumSamples();
            const float* srcL = audio.getReadPointer (0);
            const float* srcR = audio.getReadPointer (audio.getNumChannels() > 1 ? 1 : 0);
            float* left = outL[(size_t) v.output];
            float* right = outR[(size_t) v.output];
            const bool mono = right == nullptr;  // моно-выход: каналы голоса складываются пополам
            const bool exact = v.step == 1.0;    // позиция целая, интерполировать нечего

            for (int i = start; i < end; ++i)
            {
                const int idx = (int) v.position;
                if (idx + 1 >= length)
                {
                    finish (v);
                    break;
                }

                float gain = 1.0f;
                if (v.fadeIn < 1.0f)
                {
                    v.fadeIn = std::min (1.0f, v.fadeIn + fadeInStep);
                    gain = v.fadeIn;
                }
                if (v.fadeLeft >= 0)
                {
                    if (v.fadeLeft == 0)
                    {
                        finish (v);
                        break;
                    }
                    gain *= (float) v.fadeLeft / (float) fadeLength;
                    --v.fadeLeft;
                }

                // Кубическая интерполяция Эрмита между соседними отсчётами
                float smpL, smpR;
                if (exact)
                {
                    smpL = srcL[idx];
                    smpR = srcR[idx];
                }
                else
                {
                    const float frac = (float) (v.position - idx);
                    const int im1 = idx > 0 ? idx - 1 : idx;
                    const int i2 = idx + 2 < length ? idx + 2 : idx + 1;
                    smpL = hermite (srcL[im1], srcL[idx], srcL[idx + 1], srcL[i2], frac);
                    smpR = hermite (srcR[im1], srcR[idx], srcR[idx + 1], srcR[i2], frac);
                }
                const float l = smpL * gain * v.gainL;
                const float r = smpR * gain * v.gainR;
                if (mono)
                {
                    left[i] += 0.5f * (l + r);
                }
                else
                {
                    left[i] += l;
                    right[i] += r;
                }
                v.position += v.step;
            }
        }
    }

    void SamplerEngine::publishActivity() noexcept
    {
        uint64_t lo = 0, hi = 0;
        for (int i = 0; i < kMaxVoices; ++i)
        {
            const auto& v = voices[(size_t) i];
            if (v.sample == nullptr || v.note < 0)
                continue;
            if (v.note < 64)
                lo |= 1ull << v.note;
            else
                hi |= 1ull << (v.note - 64);
        }
        activeLo.store (lo);
        activeHi.store (hi);
    }
}
