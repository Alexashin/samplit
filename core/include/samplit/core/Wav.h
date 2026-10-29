#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>
#include <vector>

namespace samplit::pack
{
    // Заголовок WAV (RIFF/WAVE): формат из блока fmt (PCM или EXTENSIBLE) и положение данных. Нужен для потокового
    // чтения: JUCE-ридер держит файл открытым и не даёт читать произвольный кусок так, как нужно движку
    struct WavInfo
    {
        int channels = 0;
        int bits = 0;
        bool isFloat = false;
        double sampleRate = 0.0;
        int64_t dataOffset = 0;     // байт от начала файла до первого кадра
        int64_t frames = 0;         // кадров в блоке data (по размеру файла, если размер в заголовке неверен)
    };

    // Блоки кроме fmt и data (LIST, bext, junk…) пропускаются, нечётный блок дополняется байтом
    bool readWavInfo (const juce::File&, WavInfo& out, juce::String& error);

    // Читает n кадров моно 24-битного PCM с байтового смещения byteOffset и переводит в float: значение / 8388608.
    // Это то же число, что даёт pack::toFloat24 для отсчёта из readAudioFile, поэтому потоковый звук совпадает с
    // загруженным целиком бит в бит. scratch: рабочий буфер вызывающего (растёт при необходимости)
    bool readWav24 (juce::InputStream&, int64_t byteOffset, int n, float* out, std::vector<uint8_t>& scratch);
}
