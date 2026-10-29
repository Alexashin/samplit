#include "samplit/core/Pack.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <monocypher.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

namespace samplit::pack
{
    namespace
    {
        constexpr char kMagic[4] = { 'S', 'P', 'L', 'P' };
        constexpr size_t kMacSize = 16;
        constexpr size_t kNonceSize = 24;
        constexpr uint32_t kMaxManifestSize = 64u * 1024u * 1024u;
        constexpr uint32_t kMaxChunkCount = 4u * 1024u * 1024u;
        constexpr size_t kMaxPackIdSize = 256;
        constexpr int kFlacQuality = 5;

        // ---- порядок байт: little-endian ----
        void put16 (std::vector<uint8_t>& v, uint16_t x) { v.push_back ((uint8_t) x); v.push_back ((uint8_t) (x >> 8)); }
        void put32 (std::vector<uint8_t>& v, uint32_t x) { for (int i = 0; i < 4; ++i) v.push_back ((uint8_t) (x >> (8 * i))); }
        void put64 (std::vector<uint8_t>& v, uint64_t x) { for (int i = 0; i < 8; ++i) v.push_back ((uint8_t) (x >> (8 * i))); }
        uint32_t get32 (const uint8_t* p) { return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24; }
        uint64_t get64 (const uint8_t* p) { return (uint64_t) get32 (p) | (uint64_t) get32 (p + 4) << 32; }
        uint16_t get16 (const uint8_t* p) { return (uint16_t) (p[0] | p[1] << 8); }

        bool fail (juce::String& error, const juce::String& text)
        {
            error = text;
            return false;
        }

        // Дополнительные данные AEAD: заголовок (магия, версия, packId). Для блока ещё его номер,
        // чтобы блоки нельзя было переставить местами
        std::vector<uint8_t> makeHeaderAd (uint32_t version, const juce::String& packId)
        {
            std::vector<uint8_t> ad (kMagic, kMagic + 4);
            put32 (ad, version);
            const auto id = packId.toStdString();
            put16 (ad, (uint16_t) id.size());
            ad.insert (ad.end(), id.begin(), id.end());
            return ad;
        }

        std::vector<uint8_t> chunkAd (const std::vector<uint8_t>& headerAd, uint32_t index)
        {
            auto ad = headerAd;
            put32 (ad, index);
            return ad;
        }

        // Шифрует: результат = шифртекст + тег (16 байт)
        std::vector<uint8_t> seal (const PackKey& key, const uint8_t* nonce, const std::vector<uint8_t>& ad,
                                   const uint8_t* plain, size_t size)
        {
            std::vector<uint8_t> out (size + kMacSize);
            crypto_aead_lock (out.data(), out.data() + size, key.data(), nonce, ad.data(), ad.size(), plain, size);
            return out;
        }

        bool unseal (const PackKey& key, const uint8_t* nonce, const std::vector<uint8_t>& ad,
                   const uint8_t* cipherAndMac, size_t size, std::vector<uint8_t>& plain)
        {
            if (size < kMacSize)
                return false;
            const size_t n = size - kMacSize;
            plain.resize (n);
            const int rc = crypto_aead_unlock (plain.data(), cipherAndMac + n, key.data(), nonce, ad.data(), ad.size(),
                                               cipherAndMac, n);
            if (rc != 0)
            {
                plain.clear();
                return false;
            }
            return true;
        }
    }

    // ---------------------------------------------------------------- ключи и звук

    PackKey generateKey()
    {
        PackKey key {};
        fillRandom (key.data(), key.size());
        return key;
    }

    juce::String keyToHex (const PackKey& key)
    {
        return juce::String::toHexString (key.data(), (int) key.size(), 0);
    }

    bool keyFromHex (const juce::String& hex, PackKey& out)
    {
        const auto clean = hex.trim().toLowerCase();
        if (clean.length() != 64)
            return false;
        for (int i = 0; i < 32; ++i)
        {
            const auto hi = juce::CharacterFunctions::getHexDigitValue (clean[i * 2]);
            const auto lo = juce::CharacterFunctions::getHexDigitValue (clean[i * 2 + 1]);
            if (hi < 0 || lo < 0)
                return false;
            out[(size_t) i] = (uint8_t) (hi * 16 + lo);
        }
        return true;
    }

    int peakOf (const DecodedAudio& audio)
    {
        int32_t peak = 0;
        for (int32_t v : audio.samples)
            peak = std::max (peak, v < 0 ? -v : v);
        return (int) peak;
    }

    int trimmedLength (const DecodedAudio& audio, double thresholdDb)
    {
        const int block = std::max (1, (int) std::lround (audio.sampleRate * 0.010));
        if (audio.frames <= block || audio.sampleRate <= 0.0)
            return audio.frames;

        const double limit = std::pow (10.0, thresholdDb / 20.0) * 8388608.0;  // порог RMS в единицах отсчёта
        const double limitSq = limit * limit;

        for (int end = audio.frames; end > 0; end -= block)
        {
            const int begin = std::max (0, end - block);
            double sum = 0.0;
            for (int i = begin; i < end; ++i)
                sum += (double) audio.samples[(size_t) i] * (double) audio.samples[(size_t) i];
            if (sum / (double) (end - begin) >= limitSq)
                return std::max (block, end);
        }
        return audio.frames;  // громких блоков нет: не трогаем, это решает проверка на нули
    }

    std::vector<int16_t> toInt16 (const DecodedAudio& audio)
    {
        std::vector<int16_t> out (audio.samples.size());
        for (size_t i = 0; i < out.size(); ++i)
        {
            const int32_t v = (audio.samples[i] + 128) >> 8;
            out[i] = (int16_t) std::clamp (v, -32768, 32767);
        }
        return out;
    }

    std::vector<float> toFloat24 (const DecodedAudio& audio)
    {
        std::vector<float> out (audio.samples.size());
        for (size_t i = 0; i < out.size(); ++i)
            out[i] = (float) audio.samples[i] * (1.0f / 8388608.0f);
        return out;
    }

    bool encodeFlac (const DecodedAudio& audio, juce::MemoryBlock& out, juce::String& error)
    {
        if (audio.channels != 1 || audio.frames < 1 || (int) audio.samples.size() != audio.frames || audio.sampleRate <= 0.0)
            return fail (error, "FLAC: ожидалось моно с положительной длиной и частотой"_s);

        // JUCE принимает целые, прижатые к старшим битам слова
        std::vector<int32_t> justified (audio.samples.size());
        for (size_t i = 0; i < justified.size(); ++i)
            justified[i] = (int32_t) ((uint32_t) audio.samples[i] << 8);

        out.reset();
        {
            juce::FlacAudioFormat flac;
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::MemoryOutputStream> (out, false);
            auto options = juce::AudioFormatWriterOptions{}
                               .withSampleRate (audio.sampleRate)
                               .withNumChannels (1)
                               .withBitsPerSample (24)
                               .withQualityOptionIndex (kFlacQuality);
            auto writer = flac.createWriterFor (stream, options);
            if (writer == nullptr)
                return fail (error, "FLAC: не удалось создать писатель"_s);

            const int* channels[1] = { justified.data() };
            if (! writer->write (channels, audio.frames))
                return fail (error, "FLAC: ошибка записи"_s);
        }  // писатель закрыт, блок дописан до конца

        return out.getSize() > 0 ? true : fail (error, "FLAC: пустой результат"_s);
    }

    bool decodeFlac (const void* data, size_t size, DecodedAudio& out, juce::String& error)
    {
        juce::FlacAudioFormat flac;
        std::unique_ptr<juce::AudioFormatReader> reader (
            flac.createReaderFor (new juce::MemoryInputStream (data, size, false), true));
        if (reader == nullptr)
            return fail (error, "FLAC: не удалось разобрать блок"_s);
        if (reader->numChannels != 1)
            return fail (error, "FLAC: ожидалось моно"_s);

        const auto frames = reader->lengthInSamples;
        if (frames < 1 || frames > 0x7fffffff)
            return fail (error, "FLAC: неверная длина"_s);

        std::vector<int32_t> tmp ((size_t) frames);
        int* dest[1] = { tmp.data() };
        if (! reader->read (dest, 1, 0, (int) frames, false))
            return fail (error, "FLAC: ошибка чтения"_s);

        out.samples.resize ((size_t) frames);
        for (size_t i = 0; i < tmp.size(); ++i)
            out.samples[i] = tmp[i] >> 8;  // из старших битов в 24-битное значение
        out.sampleRate = reader->sampleRate;
        out.channels = 1;
        out.frames = (int) frames;
        return true;
    }

    bool readAudioFile (juce::AudioFormatManager& fm, const juce::File& file, int start, int end,
                        DecodedAudio& out, juce::String& error)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        if (reader == nullptr || reader->sampleRate <= 0.0)
            return fail (error, "не удалось открыть "_s + file.getFullPathName());
        if (reader->numChannels != 1)
            return fail (error, file.getFullPathName() + ": ожидалось моно, каналов "_s + juce::String ((int) reader->numChannels));

        const auto length = reader->lengthInSamples;
        const auto last = end > 0 ? std::min<juce::int64> (end, length) : length;
        if (start < 0 || start >= last)
            return fail (error, file.getFullPathName() + ": пустая область после обрезки"_s);
        const int n = (int) (last - start);

        out.samples.resize ((size_t) n);
        if (reader->usesFloatingPointData)
        {
            std::vector<float> tmp ((size_t) n);
            float* dest[1] = { tmp.data() };
            if (! reader->read (dest, 1, start, n))
                return fail (error, "ошибка чтения "_s + file.getFullPathName());
            for (size_t i = 0; i < tmp.size(); ++i)
                out.samples[i] = std::clamp ((int32_t) std::lround (tmp[i] * 8388608.0f), -8388608, 8388607);
        }
        else
        {
            std::vector<int32_t> tmp ((size_t) n);
            int* dest[1] = { tmp.data() };
            if (! reader->read (dest, 1, start, n, false))
                return fail (error, "ошибка чтения "_s + file.getFullPathName());
            for (size_t i = 0; i < tmp.size(); ++i)
                out.samples[i] = tmp[i] >> 8;
        }

        out.sampleRate = reader->sampleRate;
        out.channels = 1;
        out.frames = n;
        return true;
    }

    // ---------------------------------------------------------------- запись

    std::unique_ptr<PackWriter> PackWriter::create (const juce::File& outFile, const juce::String& packId,
                                                    const PackKey& key, juce::String& error)
    {
        if (packId.isEmpty() || packId.toStdString().size() > kMaxPackIdSize)
        {
            error = "id пакета пуст или слишком длинный"_s;
            return nullptr;
        }

        std::unique_ptr<PackWriter> w (new PackWriter());
        w->outFile = outFile;
        w->dataFile = outFile.getSiblingFile (outFile.getFileName() + ".data.tmp");
        w->dataFile.deleteFile();
        w->data = std::make_unique<juce::FileOutputStream> (w->dataFile);
        if (w->data->failedToOpen())
        {
            error = "не удалось создать "_s + w->dataFile.getFullPathName();
            return nullptr;
        }
        w->headerAd = makeHeaderAd (kFormatVersion, packId);
        w->key = key;
        w->packId = packId;
        return w;
    }

    PackWriter::~PackWriter()
    {
        data.reset();
        if (! finished)
            dataFile.deleteFile();
        crypto_wipe (key.data(), key.size());
    }

    int PackWriter::addFlac (const void* flac, size_t size, juce::String& error)
    {
        if (finished || data == nullptr)
        {
            error = "писатель закрыт"_s;
            return -1;
        }

        ChunkInfo info;
        fillRandom (info.nonce.data(), info.nonce.size());
        const auto index = (uint32_t) chunks.size();
        const auto sealed = seal (key, info.nonce.data(), chunkAd (headerAd, index), static_cast<const uint8_t*> (flac), size);

        if (sealed.size() > 0xffffffffu || ! data->write (sealed.data(), sealed.size()))
        {
            error = "ошибка записи "_s + dataFile.getFullPathName();
            return -1;
        }

        info.offset = dataSize;
        info.length = (uint32_t) sealed.size();
        dataSize += sealed.size();
        chunks.push_back (info);
        return (int) index;
    }

    int PackWriter::addAudio (const DecodedAudio& audio, juce::String& error)
    {
        juce::MemoryBlock flac;
        if (! encodeFlac (audio, flac, error))
            return -1;
        return addFlac (flac.getData(), flac.getSize(), error);
    }

    bool PackWriter::finish (const Manifest& stored, juce::String& error)
    {
        if (finished || data == nullptr)
            return fail (error, "писатель закрыт"_s);

        data->flush();
        data.reset();

        const auto json = stored.toJson().toStdString();
        uint8_t manifestNonce[kNonceSize];
        fillRandom (manifestNonce, sizeof manifestNonce);
        const auto manifestCipher = seal (key, manifestNonce, headerAd,
                                          reinterpret_cast<const uint8_t*> (json.data()), json.size());
        if (manifestCipher.size() > kMaxManifestSize)
            return fail (error, "манифест слишком большой"_s);

        // Заголовок до таблицы блоков, чтобы знать, где начинаются данные
        std::vector<uint8_t> head (kMagic, kMagic + 4);
        put32 (head, kFormatVersion);
        const auto id = packId.toStdString();
        put16 (head, (uint16_t) id.size());
        head.insert (head.end(), id.begin(), id.end());
        put32 (head, (uint32_t) manifestCipher.size());
        head.insert (head.end(), manifestNonce, manifestNonce + kNonceSize);
        head.insert (head.end(), manifestCipher.begin(), manifestCipher.end());
        put32 (head, (uint32_t) chunks.size());

        const uint64_t dataStart = head.size() + chunks.size() * (8 + 4 + kNonceSize);
        for (const auto& c : chunks)
        {
            put64 (head, dataStart + c.offset);
            put32 (head, c.length);
            head.insert (head.end(), c.nonce.begin(), c.nonce.end());
        }

        outFile.deleteFile();
        juce::FileOutputStream out (outFile);
        if (out.failedToOpen())
            return fail (error, "не удалось создать "_s + outFile.getFullPathName());
        out.write (head.data(), head.size());

        {
            juce::FileInputStream in (dataFile);
            if (in.failedToOpen())
                return fail (error, "не удалось прочитать "_s + dataFile.getFullPathName());
            std::vector<char> buffer (1 << 20);
            for (;;)
            {
                const auto n = in.read (buffer.data(), (int) buffer.size());
                if (n <= 0)
                    break;
                if (! out.write (buffer.data(), (size_t) n))
                {
                    out.flush();
                    outFile.deleteFile();
                    return fail (error, "ошибка записи "_s + outFile.getFullPathName());
                }
            }
        }
        out.flush();
        if (out.getStatus().failed())
        {
            outFile.deleteFile();
            return fail (error, "ошибка записи "_s + outFile.getFullPathName());
        }

        finished = true;
        dataFile.deleteFile();
        return true;
    }

    // ---------------------------------------------------------------- чтение

    std::unique_ptr<PackReader> PackReader::open (const juce::File& file, juce::String& error)
    {
        juce::FileInputStream in (file);
        if (in.failedToOpen())
        {
            error = "не удалось открыть "_s + file.getFullPathName();
            return nullptr;
        }
        const auto fileSize = (uint64_t) in.getTotalLength();

        auto readBytes = [&] (void* dest, size_t n) { return in.read (dest, (int) n) == (int) n; };

        std::unique_ptr<PackReader> r (new PackReader());
        r->file = file;
        r->header.fileSize = fileSize;

        uint8_t fixed[4 + 4 + 2];
        if (! readBytes (fixed, sizeof fixed) || std::memcmp (fixed, kMagic, 4) != 0)
        {
            error = "это не пакет .splpack"_s;
            return nullptr;
        }
        r->header.formatVersion = get32 (fixed + 4);
        if (r->header.formatVersion != kFormatVersion)
        {
            error = "версия формата пакета "_s + juce::String ((int) r->header.formatVersion) + " не поддерживается"_s;
            return nullptr;
        }
        const size_t idLen = get16 (fixed + 8);
        if (idLen == 0 || idLen > kMaxPackIdSize)
        {
            error = "повреждён заголовок пакета (id)"_s;
            return nullptr;
        }
        std::string id (idLen, '\0');
        if (! readBytes (id.data(), idLen))
        {
            error = "повреждён заголовок пакета"_s;
            return nullptr;
        }
        r->header.packId = juce::String::fromUTF8 (id.data(), (int) id.size());
        r->headerAd = makeHeaderAd (r->header.formatVersion, r->header.packId);

        uint8_t len[4];
        if (! readBytes (len, 4))
        {
            error = "повреждён заголовок пакета"_s;
            return nullptr;
        }
        const auto manifestLen = get32 (len);
        if (manifestLen < kMacSize || manifestLen > kMaxManifestSize || manifestLen > fileSize)
        {
            error = "повреждён заголовок пакета (длина манифеста)"_s;
            return nullptr;
        }
        r->manifestCipher.resize (manifestLen);
        if (! readBytes (r->manifestNonce.data(), kNonceSize) || ! readBytes (r->manifestCipher.data(), manifestLen)
            || ! readBytes (len, 4))
        {
            error = "повреждён заголовок пакета (манифест)"_s;
            return nullptr;
        }

        const auto count = get32 (len);
        if (count > kMaxChunkCount)
        {
            error = "повреждён заголовок пакета (число блоков)"_s;
            return nullptr;
        }
        r->header.chunkCount = count;

        const uint64_t tableEnd = (uint64_t) in.getPosition() + (uint64_t) count * (8 + 4 + kNonceSize);
        if (tableEnd > fileSize)
        {
            error = "повреждена таблица блоков"_s;
            return nullptr;
        }

        std::vector<uint8_t> table ((size_t) count * (8 + 4 + kNonceSize));
        if (! table.empty() && ! readBytes (table.data(), table.size()))
        {
            error = "повреждена таблица блоков"_s;
            return nullptr;
        }

        r->chunks.resize (count);
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint8_t* p = table.data() + (size_t) i * (8 + 4 + kNonceSize);
            auto& c = r->chunks[i];
            c.offset = get64 (p);
            c.length = get32 (p + 8);
            std::memcpy (c.nonce.data(), p + 12, kNonceSize);
            if (c.length < kMacSize || c.offset < tableEnd || c.offset > fileSize || c.length > fileSize - c.offset)
            {
                error = "блок "_s + juce::String ((int) i) + " выходит за пределы файла"_s;
                return nullptr;
            }
        }
        return r;
    }

    bool PackReader::readManifest (const PackKey& key, Manifest& out, juce::String& error) const
    {
        std::vector<uint8_t> plain;
        if (! unseal (key, manifestNonce.data(), headerAd, manifestCipher.data(), manifestCipher.size(), plain))
            return fail (error, "манифест не расшифровывается: неверный ключ или пакет изменён"_s);

        return Manifest::fromJson (juce::String::fromUTF8 (reinterpret_cast<const char*> (plain.data()), (int) plain.size()),
                                   out, error);
    }

    bool PackReader::readChunk (const PackKey& key, int chunk, std::vector<uint8_t>& flac, juce::String& error) const
    {
        if (chunk < 0 || chunk >= getChunkCount())
            return fail (error, "нет блока "_s + juce::String (chunk));

        const auto& c = chunks[(size_t) chunk];
        juce::FileInputStream in (file);
        if (in.failedToOpen() || ! in.setPosition ((juce::int64) c.offset))
            return fail (error, "не удалось открыть "_s + file.getFullPathName());

        std::vector<uint8_t> cipher (c.length);
        if (in.read (cipher.data(), (int) c.length) != (int) c.length)
            return fail (error, "блок "_s + juce::String (chunk) + ": файл оборван"_s);

        if (! unseal (key, c.nonce.data(), chunkAd (headerAd, (uint32_t) chunk), cipher.data(), cipher.size(), flac))
            return fail (error, "блок "_s + juce::String (chunk) + " не расшифровывается: неверный ключ или данные изменены"_s);
        return true;
    }

    bool PackReader::decodeChunk (const PackKey& key, int chunk, DecodedAudio& out, juce::String& error) const
    {
        std::vector<uint8_t> flac;
        if (! readChunk (key, chunk, flac, error))
            return false;
        return decodeFlac (flac.data(), flac.size(), out, error);
    }

    // ---------------------------------------------------------------- проверка

    VerifyReport verifyPack (const PackReader& reader, const PackKey& key, bool deep)
    {
        VerifyReport report;
        Manifest manifest;
        juce::String error;
        if (! reader.readManifest (key, manifest, error))
        {
            report.errors.push_back (error);
            return report;
        }

        if (manifest.id != reader.getHeader().packId)
            report.errors.push_back ("id в манифесте ("_s + manifest.id + ") не совпадает с id в заголовке ("_s
                                     + reader.getHeader().packId + ")");

        auto structure = verifyManifest (manifest);
        report.errors.insert (report.errors.end(), structure.errors.begin(), structure.errors.end());
        report.warnings.insert (report.warnings.end(), structure.warnings.begin(), structure.warnings.end());

        // Файлы из нулей (повреждённые WAV), которые сборщик отметил при сборке
        for (const auto& piece : manifest.pieces)
            for (const auto& hit : piece.hits)
                for (const auto& [micId, f] : hit.files)
                    if (f.peak == 0)
                        report.errors.push_back ("удар "_s + hit.id + ", " + micId + ": файл состоит из нулей (повреждённый WAV?)"_s);

        // Какие блоки нужны манифесту и что в них должно быть
        std::set<int> referenced;
        std::map<int, const HitFile*> expected;
        for (const auto& piece : manifest.pieces)
            for (const auto& hit : piece.hits)
                for (const auto& [micId, f] : hit.files)
                    for (int c : { f.chunk, f.tailChunk })
                    {
                        if (c < 0)
                            continue;
                        if (c >= reader.getChunkCount())
                            report.errors.push_back ("удар "_s + hit.id + ", " + micId + ": ссылка на несуществующий блок "_s
                                                     + juce::String (c));
                        else
                        {
                            referenced.insert (c);
                            if (c == f.chunk)
                                expected[c] = &f;
                        }
                    }

        for (int c = 0; c < reader.getChunkCount(); ++c)
        {
            std::vector<uint8_t> flac;
            if (! reader.readChunk (key, c, flac, error))
            {
                report.errors.push_back (error);
                continue;
            }
            if (! referenced.count (c))
                report.warnings.push_back ("блок "_s + juce::String (c) + " не используется манифестом"_s);

            if (deep)
            {
                DecodedAudio audio;
                if (! decodeFlac (flac.data(), flac.size(), audio, error))
                    report.errors.push_back ("блок "_s + juce::String (c) + ": " + error);
                else if (const auto it = expected.find (c); it != expected.end() && it->second->tailChunk < 0)
                {
                    if (it->second->frames != 0 && it->second->frames != audio.frames)
                        report.errors.push_back ("блок "_s + juce::String (c) + ": длина "_s
                                                 + juce::String (audio.frames) + " не совпадает с манифестом ("_s
                                                 + juce::String (it->second->frames) + ")");
                    if (it->second->rate != 0.0 && std::abs (it->second->rate - audio.sampleRate) > 0.5)
                        report.errors.push_back ("блок "_s + juce::String (c) + ": частота не совпадает с манифестом"_s);
                }

                // Нули в самом звуке: ловим и тогда, когда сборщик не записал пик
                const int peak = peakOf (audio);
                const auto it = expected.find (c);
                const bool flagged = it != expected.end() && it->second->peak == 0;
                if (peak == 0 && ! flagged)
                    report.errors.push_back ("блок "_s + juce::String (c) + ": звук состоит из нулей (повреждённый WAV?)"_s);
                else if (it != expected.end() && it->second->peak > 0 && it->second->peak != peak)
                    report.errors.push_back ("блок "_s + juce::String (c) + ": пик "_s + juce::String (peak)
                                             + " не совпадает с манифестом ("_s + juce::String (it->second->peak) + ")");
            }
        }
        return report;
    }
}
