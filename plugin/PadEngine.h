#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace samplit
{
    constexpr int kNumPads = 8;

    // Пэд N играет по MIDI-ноте kFirstPadNote + N. Нота 36 = C1 в нумерации, где 60 = C3
    constexpr int kFirstPadNote = 36;

    // Движок пэдов. Вся работа в audio thread: без аллокаций, блокировок и ввода-вывода.
    // Пока вместо сэмплов играет тестовый тон на каждом пэде, чтобы проверять маршрутизацию по выходам.
    class PadEngine
    {
    public:
        void prepare (double sampleRate) noexcept;

        void noteOn (int pad, float velocity) noexcept;

        // Дорисовывает пэд в [startSample, endSample) блока в два канала. Не очищает буфер, а суммирует.
        void render (int pad, float* left, float* right, int startSample, int endSample) noexcept;

        bool isActive (int pad) const noexcept { return voices[(size_t) pad].level > 0.0005f; }

    private:
        struct Voice
        {
            float phase = 0.0f;
            float freq = 220.0f;
            float level = 0.0f;
            float velocity = 1.0f;
        };

        std::array<Voice, kNumPads> voices;
        float decayPerSample = 0.9999f;
        double sr = 44100.0;
    };
}
