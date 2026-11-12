#include "PackSource.h"

#include "samplit/core/Wav.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cstring>

namespace samplit
{
    namespace
    {
        class WavClipReader final : public ClipReader
        {
        public:
            WavClipReader (std::unique_ptr<juce::FileInputStream> f, int64_t offset) : file (std::move (f)), dataOffset (offset) {}

            bool read (int64_t frame, int n, float* out, std::string&) override
            {
                return pack::readWav24 (*file, dataOffset + frame * 3, n, out, scratch);
            }

        private:
            std::unique_ptr<juce::FileInputStream> file;
            int64_t dataOffset;
            std::vector<uint8_t> scratch;
        };
    }

    std::unique_ptr<ClipReader> WavSource::open (std::string& error) const
    {
        auto in = std::make_unique<juce::FileInputStream> (juce::File (juce::String::fromUTF8 (path.c_str())));
        if (in->failedToOpen())
        {
            error = "файл не открылся";
            return nullptr;
        }
        return std::make_unique<WavClipReader> (std::move (in), dataOffset);
    }

    std::string WavSource::name() const
    {
        return juce::File (juce::String::fromUTF8 (path.c_str())).getFileName().toStdString();
    }

    // ---------------------------------------------------------------- пакет

    class PackClipReader final : public ClipReader
    {
    public:
        explicit PackClipReader (const PackSource& s) : src (s), cache ((size_t) s.hit.chunkFrames) {}

        bool read (int64_t frame, int n, float* out, std::string& error) override
        {
            const int64_t head = src.hit.headFrames;
            if (frame < head || n < 0 || frame + n > src.hit.frames)
            {
                error = "чтение вне хвоста";
                return false;
            }
            int done = 0;
            while (done < n)
            {
                const int64_t pos = frame - head + done;
                const int index = (int) (pos / src.hit.chunkFrames);
                if (index != cachedIndex)
                {
                    cachedIndex = -1;
                    juce::String err;
                    int frames = 0;
                    if (! src.reader->decodeTailChunk (src.key, src.hit, index, cache.data(), (int) cache.size(), frames, err))
                    {
                        error = err.toStdString();
                        return false;
                    }
                    cachedFrames = frames;
                    cachedIndex = index;
                }
                const int inChunk = (int) (pos - (int64_t) index * src.hit.chunkFrames);
                const int take = std::min (n - done, cachedFrames - inChunk);
                if (take <= 0)
                {
                    error = "кусок короче ожидаемого";
                    return false;
                }
                std::memcpy (out + done, cache.data() + inChunk, (size_t) take * sizeof (float));
                done += take;
            }
            return true;
        }

    private:
        const PackSource& src;      // живёт, пока жив клип; ячейка читает клип только пока держит кит
        std::vector<float> cache;   // один декодированный кусок
        int cachedIndex = -1;
        int cachedFrames = 0;
    };

    PackSource::PackSource (std::shared_ptr<const pack::PackReader> r, const pack::PackKey& k, const pack::HitFile& f, std::string name)
        : reader (std::move (r)), key (k), packName (std::move (name))
    {
        hit.chunk = f.chunk;
        hit.tailFirstChunk = f.tailFirstChunk;
        hit.headFrames = f.headFrames;
        hit.chunkFrames = f.chunkFrames;
        hit.frames = f.frames;
    }

    std::unique_ptr<ClipReader> PackSource::open (std::string& error) const
    {
        if (reader == nullptr || hit.tailFirstChunk < 0 || hit.chunkFrames <= 0)
        {
            error = "у файла нет хвоста";
            return nullptr;
        }
        return std::make_unique<PackClipReader> (*this);
    }
}
