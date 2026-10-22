#include "PadEngine.h"

#include <cmath>

namespace samplit
{
    PadEngine::PadEngine()
    {
        for (auto& g : gains)
            g.store (1.0f);
    }

    void PadEngine::setParams (int voice, float gain, float pitchSemitones, float pan) noexcept
    {
        gains[(size_t) voice].store (gain);
        pitches[(size_t) voice].store (pitchSemitones);
        pans[(size_t) voice].store (juce::jlimit (-1.0f, 1.0f, pan));
    }

    void PadEngine::prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        fadeLength = juce::jmax (1, (int) (0.005 * sr));  // 5 мс
        for (int v = 0; v < kNumVoices; ++v)
            finish (v);
    }

    bool PadEngine::isUsing (const Sample* sample) const noexcept
    {
        for (int v = 0; v < kNumVoices; ++v)
            if (slots[(size_t) v].load() == sample || playing[(size_t) v].load() == sample)
                return true;
        return false;
    }

    void PadEngine::noteOn (int voice, float velocity) noexcept
    {
        auto& v = voices[(size_t) voice];
        const Sample* s = slots[(size_t) voice].load();
        if (s == nullptr || s->audio.getNumSamples() < 2)
        {
            finish (voice);
            return;
        }

        playing[(size_t) voice].store (s);
        v.sample = s;
        v.position = 0.0;
        // Сэмпл 44.1 кГц на 48 кГц играет чуть медленнее, высота сохраняется. Сдвиг на 12 полутонов удваивает скорость
        v.step = s->sampleRate / sr * std::exp2 (pitches[(size_t) voice].load() / 12.0);
        v.gain = velocity;
        v.fadeLeft = -1;
    }

    void PadEngine::stop (int voice) noexcept
    {
        auto& v = voices[(size_t) voice];
        if (v.sample != nullptr && v.fadeLeft < 0)
            v.fadeLeft = fadeLength;
    }

    void PadEngine::finish (int voice) noexcept
    {
        voices[(size_t) voice].sample = nullptr;
        voices[(size_t) voice].fadeLeft = -1;
        playing[(size_t) voice].store (nullptr);
    }

    void PadEngine::render (int voice, float* left, float* right, int startSample, int endSample) noexcept
    {
        auto& v = voices[(size_t) voice];
        if (v.sample == nullptr)
            return;

        const auto& audio = v.sample->audio;
        const int length = audio.getNumSamples();
        const float* srcL = audio.getReadPointer (0);
        const float* srcR = audio.getReadPointer (audio.getNumChannels() > 1 ? 1 : 0);

        // Панорама как баланс: центр не меняет уровень, крайнее положение глушит противоположный канал
        const float padGain = gains[(size_t) voice].load();
        const float pan = pans[(size_t) voice].load();
        const float gainL = padGain * juce::jmin (1.0f, 1.0f - pan);
        const float gainR = padGain * juce::jmin (1.0f, 1.0f + pan);

        for (int i = startSample; i < endSample; ++i)
        {
            const int idx = (int) v.position;
            if (idx + 1 >= length)
            {
                finish (voice);
                return;
            }

            float gain = v.gain;
            if (v.fadeLeft >= 0)
            {
                if (v.fadeLeft == 0)
                {
                    finish (voice);
                    return;
                }
                gain *= (float) v.fadeLeft / (float) fadeLength;
                --v.fadeLeft;
            }

            // Линейная интерполяция между соседними отсчётами
            const float frac = (float) (v.position - idx);
            left[i] += (srcL[idx] + (srcL[idx + 1] - srcL[idx]) * frac) * gain * gainL;
            right[i] += (srcR[idx] + (srcR[idx + 1] - srcR[idx]) * frac) * gain * gainR;
            v.position += v.step;
        }
    }
}
