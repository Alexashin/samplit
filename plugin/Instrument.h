#pragma once

#include "Sample.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace samplit
{
    constexpr int kMaxGroups = 16;

    // Выходы плагина: 0 = Main (стерео), 1..kNumOutputs = отдельные моно-шины Out 1..Out N
    constexpr int kNumOutputs = 16;

    // Номер ноты в нумерации Cubase: 60 = C3
    inline juce::String noteName (int note)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[note % 12]) + juce::String (note / 12 - 2);
    }

    // Как группа выбирает зону, когда на ноту и velocity подходит несколько
    enum class PlayMode
    {
        All,     // играют все подходящие зоны сразу (слои)
        Cycle,   // по кругу: каждый удар следующая вариация
        Random   // случайная вариация
    };

    // Одна зона: файл, диапазон клавиш и диапазон velocity (1..127)
    struct Zone
    {
        juce::File file;
        SamplePtr sample;  // nullptr, пока файл грузится или не прочитался
        int group = 0;
        int lowKey = 60, highKey = 60;
        int lowVel = 1, highVel = 127;
        int rootKey = 60;
        float gainDb = 0.0f;

        bool matches (int note, int velocity) const noexcept
        {
            return note >= lowKey && note <= highKey && velocity >= lowVel && velocity <= highVel;
        }
    };

    // Группа: общие настройки набора зон и выход, на который он уходит
    struct Group
    {
        juce::String name;
        int output = 0;               // 0 = Main, 1..kNumOutputs = Out N
        PlayMode mode = PlayMode::Random;
        float gainDb = 0.0f;
        float pan = 0.0f;             // −1 лево … 1 право
        bool tracking = false;        // true: высота следует за клавишей относительно rootKey
    };

    // Снимок всего инструмента. После публикации не меняется, поэтому audio thread читает его без блокировок
    struct Instrument
    {
        std::vector<Group> groups;
        std::vector<Zone> zones;
    };
}
