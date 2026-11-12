#pragma once

#include "Kit.h"

#include "samplit/core/Manifest.h"
#include "samplit/core/Pack.h"

#include <memory>

namespace samplit
{
    // Хвост клипа из пакета v2: независимые FLAC-куски по chunkFrames кадров. Держит читателя пакета и ключ в памяти
    // (ключ не логируется и не пишется на диск), поэтому они живут, пока жив клип, то есть пока кит. Читает только поток
    // DiskStreamer: расшифровка и FLAC-декодирование куска, один декодированный кусок на ячейку как кэш.
    class PackSource final : public ClipSource
    {
    public:
        PackSource (std::shared_ptr<const pack::PackReader> reader, const pack::PackKey& key, const pack::HitFile& file, std::string packName);

        std::unique_ptr<ClipReader> open (std::string& error) const override;
        std::string name() const override { return packName; }

        int headFrames() const noexcept { return hit.headFrames; }
        int chunkFrames() const noexcept { return hit.chunkFrames; }

    private:
        friend class PackClipReader;
        std::shared_ptr<const pack::PackReader> reader;
        pack::PackKey key;
        pack::HitFile hit;      // только поля хвоста: chunk, tailFirstChunk, headFrames, chunkFrames, frames
        std::string packName;
    };
}
