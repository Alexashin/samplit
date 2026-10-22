#pragma once

#include "Sample.h"

#include <functional>
#include <memory>

namespace samplit::test
{
    // Сэмпл из формулы: value(channel, index). Удобно проверять, что движок выдаёт ровно эти отсчёты
    inline std::shared_ptr<Sample> makeSample (int length, double rate = 48000.0, int channels = 1,
                                               std::function<float (int, int)> value = {})
    {
        auto s = std::make_shared<Sample>();
        s->sampleRate = rate;
        s->audio.setSize (channels, length);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < length; ++i)
                s->audio.setSample (ch, i, value ? value (ch, i) : 0.5f);
        s->file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("test-sample.wav");
        return s;
    }
}
