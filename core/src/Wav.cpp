#include "samplit/core/Wav.h"

#include "samplit/core/Manifest.h"

#include <algorithm>

namespace samplit::pack
{
    namespace
    {
        bool fail (juce::String& error, const juce::String& text)
        {
            error = text;
            return false;
        }

        uint16_t u16 (const uint8_t* p) { return (uint16_t) (p[0] | (p[1] << 8)); }
        uint32_t u32 (const uint8_t* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
    }

    bool readWavInfo (const juce::File& file, WavInfo& out, juce::String& error)
    {
        juce::FileInputStream in (file);
        if (in.failedToOpen())
            return fail (error, "не удалось открыть "_s + file.getFullPathName());

        const int64_t fileSize = in.getTotalLength();
        uint8_t head[12];
        if (in.read (head, 12) != 12 || std::memcmp (head, "RIFF", 4) != 0 || std::memcmp (head + 8, "WAVE", 4) != 0)
            return fail (error, file.getFullPathName() + ": не WAV"_s);

        WavInfo info;
        bool haveFmt = false;
        for (;;)
        {
            uint8_t ch[8];
            if (in.read (ch, 8) != 8)
                return fail (error, file.getFullPathName() + ": нет блока data"_s);

            const uint32_t size = u32 (ch + 4);
            const int64_t bodyStart = in.getPosition();

            if (std::memcmp (ch, "fmt ", 4) == 0)
            {
                uint8_t f[40] = {};
                const int n = (int) std::min<uint32_t> (size, 40);
                if (n < 16 || in.read (f, n) != n)
                    return fail (error, file.getFullPathName() + ": битый блок fmt"_s);
                int tag = u16 (f);
                if (tag == 0xFFFE && n >= 26)
                    tag = u16 (f + 24);     // EXTENSIBLE: настоящий формат в первых двух байтах SubFormat
                info.isFloat = tag == 3;
                if (tag != 1 && tag != 3)
                    return fail (error, file.getFullPathName() + ": формат WAV не PCM"_s);
                info.channels = u16 (f + 2);
                info.sampleRate = (double) u32 (f + 4);
                info.bits = u16 (f + 14);
                haveFmt = true;
            }
            else if (std::memcmp (ch, "data", 4) == 0)
            {
                if (! haveFmt)
                    return fail (error, file.getFullPathName() + ": data раньше fmt"_s);
                const int64_t available = std::max<int64_t> (0, fileSize - bodyStart);
                const int64_t bytes = (size == 0xFFFFFFFFu || (int64_t) size > available) ? available : (int64_t) size;
                const int64_t frameBytes = (int64_t) info.channels * (info.bits / 8);
                if (frameBytes <= 0)
                    return fail (error, file.getFullPathName() + ": пустой формат"_s);
                info.dataOffset = bodyStart;
                info.frames = bytes / frameBytes;
                out = info;
                return true;
            }

            const int64_t next = bodyStart + (int64_t) size + (size & 1u);
            if (next >= fileSize || ! in.setPosition (next))
                return fail (error, file.getFullPathName() + ": нет блока data"_s);
        }
    }

    bool readWav24 (juce::InputStream& in, int64_t byteOffset, int n, float* out, std::vector<uint8_t>& scratch)
    {
        if (n <= 0)
            return true;
        const size_t bytes = (size_t) n * 3;
        if (scratch.size() < bytes)
            scratch.resize (bytes);
        if (! in.setPosition (byteOffset) || in.read (scratch.data(), (int) bytes) != (int) bytes)
            return false;

        const uint8_t* p = scratch.data();
        for (int i = 0; i < n; ++i, p += 3)
        {
            const int32_t v = (int32_t) (((uint32_t) p[0] << 8) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 24)) >> 8;
            out[i] = (float) v * (1.0f / 8388608.0f);
        }
        return true;
    }
}
