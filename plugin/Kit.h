#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace samplit
{
    // Выходы плагина: 0 = Main (стерео), 1..7 = моно-шины (Kick In, Kick Out, Snare Top, Snare Bottom, Hi-Hat, Ride, Room Mid),
    // 8..10 = стерео-шины (Toms, Over, Room). Микрофоны моно: на стерео-шине их разводит панорама
    constexpr int kNumOutputs = 10;
    constexpr int kFirstStereoOutput = 8;
    constexpr bool isStereoOutput (int out) noexcept { return out >= kFirstStereoOutput && out <= kNumOutputs; }

    // Хвост потокового клипа. Читает только поток DiskStreamer (не audio thread). Источник общий для всех голосов
    // клипа и после публикации не меняется; состояние чтения (файл, кэш куска) живёт в ClipReader, по одному на ячейку
    struct ClipReader
    {
        virtual ~ClipReader() = default;
        // n кадров области клипа с кадра frame в out (float32, 24 бит / 8388608). false: error объясняет причину
        virtual bool read (int64_t frame, int n, float* out, std::string& error) = 0;
    };

    struct ClipSource
    {
        virtual ~ClipSource() = default;
        // Поток чтения: открыть источник под одну ячейку. nullptr: не открылся (error)
        virtual std::unique_ptr<ClipReader> open (std::string& error) const = 0;
        virtual std::string name() const = 0;       // для сообщения об ошибке, без секретов
    };

    // Длинный 24-битный моно WAV: хвост читается с диска. dataOffset: байт файла, с которого начинается область (учтён start)
    struct WavSource final : ClipSource
    {
        WavSource (std::string p, int64_t offset) : path (std::move (p)), dataOffset (offset) {}
        std::unique_ptr<ClipReader> open (std::string& error) const override;
        std::string name() const override;

        std::string path;       // UTF-8
        int64_t dataOffset = 0;
    };

    // Декодированный моно-файл удара: float32, 24 бит без потерь (значение / 8388608).
    // Целиком в f32, либо (потоковый клип, totalFrames > f32.size()) только голова, а остальное движок читает
    // через source (WAV с диска или куски хвоста пакета) через DiskStreamer. После публикации не меняется,
    // поэтому его можно читать из audio thread. source держит всё, что нужно хвосту (читатель пакета), пока клип жив.
    struct DecodedClip
    {
        std::vector<float> f32;         // клип целиком или его голова
        double sampleRate = 44100.0;
        int totalFrames = 0;            // 0: клип целиком в f32; иначе полная длина области (голова + хвост вне памяти)
        std::shared_ptr<const ClipSource> source;   // потоковый клип: откуда читать хвост
        std::string sourceKey;          // откуда клип (пак, блок, обрезка): по нему загрузчик переиспользует клип в новом ките

        int frames() const noexcept { return totalFrames > 0 ? totalFrames : (int) f32.size(); }
        int headFrames() const noexcept { return (int) f32.size(); }
        bool isStreamed() const noexcept { return totalFrames > (int) f32.size(); }

        static std::shared_ptr<const DecodedClip> fromFloat (std::vector<float> data, double rate, std::string sourceKey = {})
        {
            auto c = std::make_shared<DecodedClip>();
            c->sourceKey = std::move (sourceKey);
            c->f32 = std::move (data);
            c->sampleRate = rate;
            return c;
        }

        static std::shared_ptr<const DecodedClip> fromStream (std::vector<float> head, int total, double rate,
                                                              std::shared_ptr<const ClipSource> source,
                                                              std::string sourceKey = {})
        {
            auto c = std::make_shared<DecodedClip>();
            c->sourceKey = std::move (sourceKey);
            c->f32 = std::move (head);
            c->totalFrames = total;
            c->sampleRate = rate;
            c->source = std::move (source);
            return c;
        }

        static std::shared_ptr<const DecodedClip> fromStream (std::vector<float> head, int total, double rate,
                                                              std::string path, int64_t dataOffset,
                                                              std::string sourceKey = {})
        {
            return fromStream (std::move (head), total, rate, std::make_shared<WavSource> (std::move (path), dataOffset),
                               std::move (sourceKey));
        }
    };

    using ClipPtr = std::shared_ptr<const DecodedClip>;

    // Микрофон установки. out: 0 = Main, 1..kNumOutputs = выход плагина (см. kNumOutputs); pan: стартовая панорама
    struct Mic
    {
        std::string id, name, role;
        int out = 0;
        float pan = 0.0f;
    };

    // Файл удара на одном микрофоне. Обрезка (start, end) уже применена к клипу
    struct HitFile
    {
        ClipPtr clip;
        float gainDb = 0.0f;
    };

    // Как в режиме «Живой» выбирается удар внутри слоя
    enum class Pick
    {
        Random,     // случайный внутри слоя velocity, без повтора подряд
        Cycle,      // по кругу: с первого удара слоя (по возрастанию velocity) до последнего и снова
        Exact       // velocity выбирает конкретный удар, как в Kontakt
    };

    // Слой: окно velocity и артикуляция. Необязательные поля приходят из манифеста пакета, pick и choice из установки
    struct Layer
    {
        std::string id, artic;
        int velLo = 1, velHi = 127;
        std::string label;
        int key = -1;                   // слой привязан к одной клавише, -1: ко всем клавишам барабана
        Pick pick = Pick::Random;
        int choice = -1;                // индекс в Kit::choices: режим слоя переключает пользователь
        std::string peerGroup;
        bool unverified = false;
    };

    // Один удар: клавиша, окно velocity, слой и файлы по микрофонам. Играет целиком, с одного сэмпла старта
    struct Hit
    {
        std::string id;
        int layer = 0;                  // индекс в Piece::layers
        int key = 60;
        int velLo = 1, velHi = 127;
        std::string peerGroup;          // признак группы -D из .nki, пока без смысла для движка
        bool unverified = false;        // разметка этого удара не подтверждена
        bool gate = false;              // удар из группы -D (Kontakt): звучит, пока держится нота, на Note Off затухает за ~5 мс
        std::vector<HitFile> files;     // по индексу микрофона в Kit::mics; clip == nullptr: на этом микрофоне нет файла
    };

    // Барабан. choke >= 0: барабан глушит голоса других барабанов с тем же номером группы
    struct Piece
    {
        std::string id, name;
        int slot = -1;                  // место установки (Setup), -1: слот не задан (тестовые киты)
        std::vector<int> keys;
        int choke = -1;
        bool metal = false;             // «железо» (crash, china, splash, hat, ride): его глушит управляющая нота Kit::metalChokeKey
        std::vector<Layer> layers;
        std::vector<Hit> hits;
    };

    // Как громкость зависит от velocity. Для библиотеки Studio600 none: динамика заложена в записи
    enum class VelocityGain { None, Linear };

    // Как выбирается удар
    enum class HitSelect
    {
        Kontakt,  // velocity попадает ровно в окно удара, иначе тишина
        Live      // слой по velocity, удар внутри слоя по правилу pick; сила вне записанных окон молчит (как в Kontakt)
    };

    // Переключатель выбора удара на экране кита для группы слоёв (например, хэт C2–F#4). Режимы задаёт установка
    struct PickChoice
    {
        std::string id;                 // ключ для сохранения в проекте: барабаны и клавиши
        std::string name;
        Pick pick = Pick::Random;       // по умолчанию, из манифеста или установки
        std::vector<int> pieces;        // индексы барабанов в Kit::pieces, на которые действует переключатель
    };

    // Индекс клавиши: какие слои и удары на ней лежат. Строится в Kit::buildIndex()
    struct NoteMap
    {
        struct LayerRef
        {
            int layer = 0;
            int velLo = 1, velHi = 127;
            Pick pick = Pick::Random;
            int choice = -1;
            std::vector<int> hits;      // индексы в Piece::hits, по возрастанию velLo
        };

        int piece = -1;
        std::vector<LayerRef> layers;   // по возрастанию velLo
    };

    // Уникальный номер кита: новый у каждого созданного и скопированного кита, не повторяется за жизнь процесса
    // (в отличие от адреса, который после удаления кита может достаться новому). Ноль не выдаётся
    struct KitSerial
    {
        uint64_t value = next();
        KitSerial() = default;
        KitSerial (const KitSerial&) : value (next()) {}
        KitSerial& operator= (const KitSerial&) { value = next(); return *this; }

    private:
        static uint64_t next() noexcept
        {
            static std::atomic<uint64_t> counter { 0 };
            return counter.fetch_add (1, std::memory_order_relaxed) + 1;
        }
    };

    struct Kit
    {
        KitSerial serial;               // идентичность кита для движка (lastKit), см. KitSerial
        static constexpr int kMaxMics = 24;     // 15 слотов Studio600 (kStudio600Mics) + запас для незнакомых id
        static constexpr int kMaxChoices = 128;

        std::string id, title;
        std::vector<Mic> mics;          // по слотам (см. kStudio600Mics), не больше kMaxMics; слот без микрофона: Mic с пустым id
        std::vector<Piece> pieces;
        VelocityGain velocityGain = VelocityGain::None;
        HitSelect mode = HitSelect::Live;
        std::vector<PickChoice> choices;    // не больше kMaxChoices
        // Управляющая нота: Note On на ней не играет звук, а глушит всё «железо» (Piece::metal). -1: нет. Studio600: C-1 (Cubase C3 = 60)
        int metalChokeKey = -1;
        std::array<NoteMap, 128> notes {};

        // Проверяет связность и строит индекс клавиш. Вызывается один раз перед публикацией кита.
        // Возвращает false, если модель некорректна (индексы слоёв или микрофонов за пределами)
        bool buildIndex()
        {
            for (auto& n : notes)
                n = {};

            if ((int) mics.size() > kMaxMics || (int) choices.size() > kMaxChoices)
                return false;

            for (int p = 0; p < (int) pieces.size(); ++p)
            {
                auto& piece = pieces[(size_t) p];
                for (int h = 0; h < (int) piece.hits.size(); ++h)
                {
                    auto& hit = piece.hits[(size_t) h];
                    if (hit.layer < 0 || hit.layer >= (int) piece.layers.size() || hit.key < 0 || hit.key > 127
                        || piece.layers[(size_t) hit.layer].choice >= (int) choices.size())
                        return false;
                    if (hit.files.size() > mics.size())
                        return false;
                    hit.files.resize (mics.size());

                    auto& map = notes[(size_t) hit.key];
                    if (map.piece >= 0 && map.piece != p)
                        return false;   // клавиша принадлежит одному барабану
                    map.piece = p;

                    NoteMap::LayerRef* ref = nullptr;
                    for (auto& l : map.layers)
                        if (l.layer == hit.layer)
                            ref = &l;
                    if (ref == nullptr)
                    {
                        const auto& layer = piece.layers[(size_t) hit.layer];
                        map.layers.push_back ({ hit.layer, layer.velLo, layer.velHi, layer.pick, layer.choice, {} });
                        ref = &map.layers.back();
                    }
                    ref->hits.push_back (h);
                }
            }

            for (auto& map : notes)
            {
                std::sort (map.layers.begin(), map.layers.end(), [] (const NoteMap::LayerRef& a, const NoteMap::LayerRef& b)
                           { return a.velLo != b.velLo ? a.velLo < b.velLo : a.layer < b.layer; });
                if (map.piece < 0)
                    continue;
                const auto& hits = pieces[(size_t) map.piece].hits;
                for (auto& l : map.layers)
                    std::stable_sort (l.hits.begin(), l.hits.end(), [&] (int a, int b) { return hits[(size_t) a].velLo < hits[(size_t) b].velLo; });
            }
            return true;
        }
    };
}
