#pragma once

// Партия, нарисованная в плагине (docs/phrase-editor-spec.md): шаги 1/16, места установки, явные velocity,
// хуманайз с зерном. Только данные и расчёт событий, без GUI. Один и тот же результат при каждом вызове

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <cstdint>
#include <vector>

namespace samplit
{
    struct PatternStep
    {
        int step = 0;           // 0..bars*16-1
        int slot = 0;           // место установки (PlaceDef::slot)
        int key = 24;           // MIDI-клавиша, которая играет
        int velocity = 100;     // 1..127, явное число
    };

    // Событие воспроизведения: позиция в сэмплах от начала партии (может быть чуть раньше шага из-за хуманайза, но не меньше 0)
    struct PatternEvent
    {
        int64_t sample = 0;
        int key = 0;
        int velocity = 0;
        int slot = 0;
    };

    class Pattern
    {
    public:
        static constexpr int kStepsPerBar = 16;      // 4/4, шаг 1/16
        static constexpr int kMaxBars = 4;
        static constexpr int kPpq = 480;
        static constexpr int kTicksPerStep = kPpq / 4;

        int bars = 1;                   // 1..kMaxBars
        double bpm = 120.0;             // 30..300
        uint32_t humanSeed = 1;
        int humanTimingMs = 0;          // 0..40: сдвиг по времени ±мс
        int humanVelocity = 0;          // 0..20: сдвиг по силе ± единиц
        std::vector<PatternStep> steps;

        int numSteps() const noexcept { return bars * kStepsPerBar; }
        void clampToLimits();

        // Поставить удар (заменяет удар на том же шаге и месте). false: шаг вне партии
        bool setStep (int step, int slot, int key, int velocity);
        bool removeStep (int step, int slot);
        const PatternStep* findStep (int step, int slot) const noexcept;

        // Длина партии в сэмплах (для зацикливания)
        int64_t lengthSamples (double sampleRate) const noexcept;

        // События по возрастанию sample; хуманайз уже применён (зерно + шаг + место, не зависит от порядка вызовов)
        std::vector<PatternEvent> buildEvents (double sampleRate) const;

        // MIDI-файл: PPQ 480, темп, ноты на клавишах мест, velocity явные, хуманайз запечён в тики
        juce::MidiFile toMidiFile() const;

        juce::String toJson() const;
        static bool fromJson (const juce::String&, Pattern& out, juce::String& error);

    private:
        // jitterTime в мс и jitterVel в единицах для шага; детерминированы
        void jitterFor (const PatternStep&, double& timeMs, int& vel) const noexcept;
    };
}
