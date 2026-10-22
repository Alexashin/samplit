#pragma once

#include "Sample.h"

#include <array>
#include <atomic>

namespace samplit
{
    constexpr int kNumPads = 8;

    // Последний голос играет прослушивание из браузера и всегда идёт в Main
    constexpr int kPreviewVoice = kNumPads;
    constexpr int kNumVoices = kNumPads + 1;

    // Пэд N играет по MIDI-ноте kFirstPadNote + N. Нота 36 = C1 в нумерации, где 60 = C3
    constexpr int kFirstPadNote = 36;

    // Движок пэдов. noteOn, stop и render вызываются только из audio thread: без аллокаций и блокировок.
    // Сэмплами движок не владеет: их держит процессор и не удаляет, пока isUsing() возвращает true.
    class PadEngine
    {
    public:
        PadEngine();

        void prepare (double sampleRate) noexcept;

        // Message thread: громкость (множитель), сдвиг высоты в полутонах, панорама от −1 (лево) до 1 (право).
        // Громкость и панорама применяются сразу, высота со следующего удара
        void setParams (int voice, float gain, float pitchSemitones, float pan) noexcept;

        // Message thread: какой сэмпл играет голос при следующем noteOn. nullptr = пэд пуст
        void setSample (int voice, const Sample* sample) noexcept { slots[(size_t) voice].store (sample); }
        const Sample* getSample (int voice) const noexcept { return slots[(size_t) voice].load(); }

        // Message thread: сэмпл назначен на голос или ещё звучит
        bool isUsing (const Sample* sample) const noexcept;

        void noteOn (int voice, float velocity) noexcept;

        // Плавно глушит голос за несколько миллисекунд, без щелчка
        void stop (int voice) noexcept;

        // Дописывает голос в [startSample, endSample) блока в два канала. Суммирует, не очищает буфер.
        void render (int voice, float* left, float* right, int startSample, int endSample) noexcept;

        bool isActive (int voice) const noexcept { return voices[(size_t) voice].sample != nullptr; }

    private:
        struct Voice
        {
            const Sample* sample = nullptr;
            double position = 0.0;
            double step = 1.0;
            float gain = 0.0f;
            int fadeLeft = -1;  // -1: не затухает
        };

        void finish (int voice) noexcept;

        std::array<std::atomic<const Sample*>, kNumVoices> slots {};
        std::array<std::atomic<const Sample*>, kNumVoices> playing {};
        std::array<Voice, kNumVoices> voices {};
        std::array<std::atomic<float>, kNumVoices> gains {};
        std::array<std::atomic<float>, kNumVoices> pitches {};
        std::array<std::atomic<float>, kNumVoices> pans {};
        double sr = 44100.0;
        int fadeLength = 220;
    };
}
