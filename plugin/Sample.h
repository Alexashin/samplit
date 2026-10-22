#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

#include <memory>

namespace samplit
{
    // Загруженный в память сэмпл. После загрузки не меняется, поэтому его можно читать из audio thread.
    struct Sample
    {
        juce::AudioBuffer<float> audio;  // 1 или 2 канала
        double sampleRate = 44100.0;
        juce::File file;

        juce::String getName() const { return file.getFileNameWithoutExtension(); }
        double getLengthSeconds() const { return audio.getNumSamples() / sampleRate; }
    };

    using SamplePtr = std::shared_ptr<const Sample>;
}
