#pragma once

#include "samplit/core/Manifest.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace juce { class AudioFormatManager; }

// Контейнер .splpack: манифест и FLAC-блоки, зашифрованные XChaCha20-Poly1305. Формат в docs/pack-format.md.
namespace samplit::pack
{
    constexpr uint32_t kFormatVersion = 1;

    // Ключ пакета. В контейнере не хранится: его выдаёт токен лицензии
    using PackKey = std::array<uint8_t, 32>;

    // Случайные байты от генератора ОС
    void fillRandom (void* dest, size_t size);
    PackKey generateKey();
    juce::String keyToHex (const PackKey&);
    bool keyFromHex (const juce::String& hex, PackKey& out);

    // Моно-звук как в библиотеке: 24-битные целые со знаком (значение в диапазоне −8388608…8388607)
    struct DecodedAudio
    {
        std::vector<int32_t> samples;
        double sampleRate = 0.0;
        int channels = 1;
        int frames = 0;
    };

    // Наибольший |отсчёт| в 24-битных единицах. 0 значит, что файл состоит из нулей (повреждённый WAV)
    int peakOf (const DecodedAudio&);

    // Сколько отсчётов оставить, если отрезать тихий хвост: берутся блоки по 10 мс, и хвост после последнего
    // блока с RMS не ниже thresholdDb (дБ от полной шкалы 24 бит) убирается. Файл без громких блоков и совсем
    // короткий не обрезаются. Остаётся не меньше одного блока
    int trimmedLength (const DecodedAudio&, double thresholdDb);

    // Для памяти в плагине: 24 бит → 16 бит с округлением и ограничением
    std::vector<int16_t> toInt16 (const DecodedAudio&);

    bool encodeFlac (const DecodedAudio&, juce::MemoryBlock& out, juce::String& error);
    bool decodeFlac (const void* data, size_t size, DecodedAudio& out, juce::String& error);

    // Читает звуковой файл (WAV, AIFF, FLAC…), только моно. start и end: обрезка в отсчётах как в HitFile
    bool readAudioFile (juce::AudioFormatManager&, const juce::File&, int start, int end,
                        DecodedAudio& out, juce::String& error);

    // Запись пакета. Блоки шифруются и сразу уходят во временный файл рядом с результатом, в памяти их нет,
    // поэтому большие пакеты собираются без большого расхода памяти. Из нескольких потоков вызывать нельзя
    class PackWriter
    {
    public:
        static std::unique_ptr<PackWriter> create (const juce::File& outFile, const juce::String& packId,
                                                   const PackKey&, juce::String& error);
        ~PackWriter();

        // Кодирует в FLAC, шифрует, дописывает. Возвращает номер блока или −1 (причина в error)
        int addAudio (const DecodedAudio&, juce::String& error);

        // Блок, который уже закодирован в FLAC
        int addFlac (const void* flac, size_t size, juce::String& error);

        // Пишет заголовок, зашифрованный манифест и таблицу блоков, дописывает блоки. После этого писатель закрыт
        bool finish (const Manifest& stored, juce::String& error);

        int getChunkCount() const noexcept { return (int) chunks.size(); }

    private:
        struct ChunkInfo { uint64_t offset = 0; uint32_t length = 0; std::array<uint8_t, 24> nonce {}; };

        PackWriter() = default;

        juce::File outFile, dataFile;
        std::unique_ptr<juce::FileOutputStream> data;
        std::vector<uint8_t> headerAd;  // magic + версия + packId
        PackKey key {};
        juce::String packId;
        std::vector<ChunkInfo> chunks;
        uint64_t dataSize = 0;
        bool finished = false;
    };

    // Чтение пакета. open читает только заголовок и таблицу блоков (ключ не нужен). Все методы чтения
    // открывают файл заново, поэтому могут вызываться из разных фоновых потоков одновременно
    class PackReader
    {
    public:
        struct Header
        {
            juce::String packId;
            uint32_t formatVersion = 0;
            uint32_t chunkCount = 0;
            uint64_t fileSize = 0;
        };

        static std::unique_ptr<PackReader> open (const juce::File&, juce::String& error);

        const Header& getHeader() const noexcept { return header; }
        int getChunkCount() const noexcept { return (int) header.chunkCount; }
        uint32_t getChunkLength (int chunk) const noexcept { return chunk >= 0 && chunk < getChunkCount() ? chunks[(size_t) chunk].length : 0; }

        // Расшифровывает и разбирает манифест. Неверный ключ или изменённый заголовок дают false
        bool readManifest (const PackKey&, Manifest& out, juce::String& error) const;

        // Расшифрованные байты FLAC блока
        bool readChunk (const PackKey&, int chunk, std::vector<uint8_t>& flac, juce::String& error) const;

        // Блок сразу в звук
        bool decodeChunk (const PackKey&, int chunk, DecodedAudio& out, juce::String& error) const;

    private:
        struct ChunkInfo { uint64_t offset = 0; uint32_t length = 0; std::array<uint8_t, 24> nonce {}; };

        PackReader() = default;

        juce::File file;
        Header header;
        std::vector<uint8_t> headerAd;
        std::array<uint8_t, 24> manifestNonce {};
        std::vector<uint8_t> manifestCipher;  // шифртекст и тег в конце
        std::vector<ChunkInfo> chunks;
    };

    // Полная проверка пакета: структура манифеста, ссылки на блоки, расшифровка каждого блока.
    // deep: ещё и разбор каждого FLAC и сверка длины и частоты с манифестом (долго на больших пакетах)
    VerifyReport verifyPack (const PackReader&, const PackKey&, bool deep = false);
}
