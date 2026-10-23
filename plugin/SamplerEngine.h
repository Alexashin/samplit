#pragma once

#include "Instrument.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace samplit
{
    constexpr int kMaxVoices = 64;

    // Движок сэмплера: по ноте и velocity выбирает зоны из текущего снимка инструмента и играет их полифонично.
    // noteOn, stopPreview, previewOn и render вызываются только из audio thread: без аллокаций и блокировок.
    // Снимок и сэмплы движку не принадлежат: их держит процессор, пока isUsing() возвращает true.
    class SamplerEngine
    {
    public:
        SamplerEngine() = default;

        void prepare (double sampleRate) noexcept;

        // Message thread: новый снимок инструмента. Старый звучащие голоса доигрывают из своих сэмплов
        void setInstrument (const Instrument* instrument) noexcept { current.store (instrument); }
        const Instrument* getInstrument() const noexcept { return current.load(); }

        // Message thread: сэмпл для прослушивания из браузера (играет в Main)
        void setPreviewSample (const Sample* sample) noexcept { previewSlot.store (sample); }

        // Message thread: снимок или сэмпл ещё нужны audio thread
        bool isUsing (const Instrument*) const noexcept;
        bool isUsing (const Sample*) const noexcept;

        // velocity 1..127. Возвращает число запущенных голосов
        int noteOn (int note, int velocity) noexcept;

        void previewOn() noexcept;
        void stopPreview() noexcept;

        // Дописывает все голоса в [start, end) блока. outL/outR индексируются выходом группы (0 = Main).
        // Суммирует, буфер не очищает. Указатели выходов, которых нет у хоста, должны вести в Main.
        void render (const std::array<float*, kNumOutputs + 1>& outL, const std::array<float*, kNumOutputs + 1>& outR,
                     int start, int end) noexcept;

        // Какие клавиши сейчас звучат (биты 0..63 и 64..127)
        std::array<uint64_t, 2> getActiveKeys() const noexcept { return { activeLo.load(), activeHi.load() }; }
        void publishActivity() noexcept;

        int getNumActiveVoices() const noexcept;

    private:
        struct Voice
        {
            const Sample* sample = nullptr;
            double position = 0.0;
            double step = 1.0;
            float gainL = 0.0f, gainR = 0.0f;
            float fadeIn = 0.0f;   // 0..1, нарастание в начале
            int fadeLeft = -1;     // -1: не затухает
            int output = 0;
            int note = -1;
            uint64_t age = 0;
        };

        void startVoice (const Zone&, const Group&, int note, int velocity) noexcept;
        void finish (Voice&) noexcept;
        uint32_t nextRandom() noexcept;

        std::atomic<const Instrument*> current { nullptr };
        std::atomic<const Sample*> previewSlot { nullptr };
        std::array<std::atomic<const Sample*>, kMaxVoices + 1> playing {};  // последний = прослушивание
        std::array<Voice, kMaxVoices + 1> voices {};
        std::array<uint32_t, kMaxGroups> cycle {};
        std::atomic<uint64_t> activeLo { 0 }, activeHi { 0 };
        uint64_t ageCounter = 0;
        uint32_t rng = 0x9e3779b9u;
        double sr = 44100.0;
        int fadeLength = 220;
        float fadeInStep = 0.01f;
    };
}
