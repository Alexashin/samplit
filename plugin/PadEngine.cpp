#include "PadEngine.h"

#include <cmath>

namespace samplit
{
    void PadEngine::prepare (double sampleRate) noexcept
    {
        sr = sampleRate;
        // Затухание до -60 дБ за ~0.45 с
        decayPerSample = std::pow (0.001f, 1.0f / (0.45f * (float) sr));

        // Разные высоты по пэдам: на слух сразу ясно, какой пэд играет
        for (int i = 0; i < kNumPads; ++i)
        {
            voices[(size_t) i] = {};
            voices[(size_t) i].freq = 110.0f * std::pow (2.0f, (float) i * 3.0f / 12.0f);
        }
    }

    void PadEngine::noteOn (int pad, float velocity) noexcept
    {
        auto& v = voices[(size_t) pad];
        v.phase = 0.0f;
        v.level = 1.0f;
        v.velocity = velocity;
    }

    void PadEngine::render (int pad, float* left, float* right, int startSample, int endSample) noexcept
    {
        auto& v = voices[(size_t) pad];
        if (v.level <= 0.0005f)
        {
            v.level = 0.0f;
            return;
        }

        const float step = 6.2831853f * v.freq / (float) sr;
        for (int i = startSample; i < endSample; ++i)
        {
            const float s = std::sin (v.phase) * v.level * v.velocity * 0.4f;
            left[i] += s;
            right[i] += s;
            v.phase += step;
            if (v.phase > 6.2831853f)
                v.phase -= 6.2831853f;
            v.level *= decayPerSample;
        }
    }
}
