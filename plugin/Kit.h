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
    // Выходы плагина: 0 = Main (стерео), 1..kNumOutputs = отдельные моно-шины Out 1..Out N
    constexpr int kNumOutputs = 16;

    // Декодированный моно-файл удара: float32, 24 бит без потерь (значение / 8388608).
    // Целиком в f32, либо (потоковый клип, totalFrames > f32.size()) только голова, а остальное движок читает с диска
    // из WAV по path через DiskStreamer. После публикации не меняется, поэтому его можно читать из audio thread.
    struct DecodedClip
    {
        std::vector<float> f32;         // клип целиком или его голова
        double sampleRate = 44100.0;
        int totalFrames = 0;            // 0: клип целиком в f32; иначе полная длина области (голова + хвост на диске)
        std::string path;               // потоковый клип: WAV (UTF-8)
        int64_t dataOffset = 0;         // потоковый клип: байт файла, с которого начинается область (учтён start)

        int frames() const noexcept { return totalFrames > 0 ? totalFrames : (int) f32.size(); }
        int headFrames() const noexcept { return (int) f32.size(); }
        bool isStreamed() const noexcept { return totalFrames > (int) f32.size(); }

        static std::shared_ptr<const DecodedClip> fromFloat (std::vector<float> data, double rate)
        {
            auto c = std::make_shared<DecodedClip>();
            c->f32 = std::move (data);
            c->sampleRate = rate;
            return c;
        }

        static std::shared_ptr<const DecodedClip> fromStream (std::vector<float> head, int total, double rate,
                                                              std::string path, int64_t dataOffset)
        {
            auto c = std::make_shared<DecodedClip>();
            c->f32 = std::move (head);
            c->totalFrames = total;
            c->sampleRate = rate;
            c->path = std::move (path);
            c->dataOffset = dataOffset;
            return c;
        }
    };

    using ClipPtr = std::shared_ptr<const DecodedClip>;

    // Микрофон установки. out: 0 = Main, 1..kNumOutputs = моно-выход Out N
    struct Mic
    {
        std::string id, name, role;
        int out = 0;
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
        Random,     // случайный, без повтора подряд, с разбросом по соседним слоям
        Cycle,      // по кругу: с первого удара слоя (по возрастанию velocity) до последнего и снова
        Exact       // velocity выбирает конкретный удар, как в Kontakt
    };

    // Слой: окно velocity и артикуляция. Необязательные поля приходят из манифеста пакета, pick и choice из пресета
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
        std::vector<HitFile> files;     // по индексу микрофона в Kit::mics; clip == nullptr: на этом микрофоне нет файла
    };

    // Барабан. choke >= 0: барабан глушит голоса других барабанов с тем же номером группы
    struct Piece
    {
        std::string id, name;
        std::vector<int> keys;
        int choke = -1;
        std::vector<Layer> layers;
        std::vector<Hit> hits;
    };

    // Как громкость зависит от velocity. Для библиотеки Studio600 none: динамика заложена в записи
    enum class VelocityGain { None, Linear };

    // Как выбирается удар
    enum class HitSelect
    {
        Kontakt,  // velocity попадает ровно в окно удара, иначе тишина
        Live      // слой по velocity (ближайший, если попали в щель), удар случайный внутри слоя, без повтора подряд
    };

    // Переключатель выбора удара на экране кита для группы слоёв (например, хэт C2–F#4). Правила задаёт пресет
    struct PickChoice
    {
        std::string id;                 // ключ для сохранения в проекте: барабаны и клавиши
        std::string name;
        Pick pick = Pick::Random;       // по умолчанию, из пресета
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
        static constexpr int kMaxMics = kNumOutputs;
        static constexpr int kMaxChoices = 32;

        std::string id, title;
        std::vector<Mic> mics;          // не больше kMaxMics
        std::vector<Piece> pieces;
        VelocityGain velocityGain = VelocityGain::None;
        HitSelect mode = HitSelect::Live;
        int spread = 0;                 // Live: брать удар из ±spread соседних слоёв
        std::vector<PickChoice> choices;    // не больше kMaxChoices
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
