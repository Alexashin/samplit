#pragma once

#include "Instrument.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace samplit
{
    // Декодированный моно-файл удара. Хранится как int16 (вдвое меньше памяти, чем float) или как float.
    // После публикации не меняется, поэтому его можно читать из audio thread.
    struct DecodedClip
    {
        std::vector<int16_t> s16;
        std::vector<float> f32;
        double sampleRate = 44100.0;

        int frames() const noexcept { return s16.empty() ? (int) f32.size() : (int) s16.size(); }
        bool isInt16() const noexcept { return ! s16.empty(); }

        static std::shared_ptr<const DecodedClip> fromFloat (std::vector<float> data, double rate)
        {
            auto c = std::make_shared<DecodedClip>();
            c->f32 = std::move (data);
            c->sampleRate = rate;
            return c;
        }

        static std::shared_ptr<const DecodedClip> fromInt16 (std::vector<int16_t> data, double rate)
        {
            auto c = std::make_shared<DecodedClip>();
            c->s16 = std::move (data);
            c->sampleRate = rate;
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

    // Слой: окно velocity и артикуляция. Необязательные поля приходят из манифеста пакета
    struct Layer
    {
        std::string id, artic;
        int velLo = 1, velHi = 127;
        std::string label;
        int key = -1;                   // слой привязан к одной клавише, -1: ко всем клавишам барабана
        bool pickExact = false;         // pick: "exact": в режиме «Живой» только точный удар, без разброса и соседних слоёв
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

    // Индекс клавиши: какие слои и удары на ней лежат. Строится в Kit::buildIndex()
    struct NoteMap
    {
        struct LayerRef
        {
            int layer = 0;
            int velLo = 1, velHi = 127;
            bool pickExact = false;
            std::vector<int> hits;      // индексы в Piece::hits
        };

        int piece = -1;
        std::vector<LayerRef> layers;   // по возрастанию velLo
    };

    struct Kit
    {
        static constexpr int kMaxMics = kNumOutputs;

        std::string id, title;
        std::vector<Mic> mics;          // не больше kMaxMics
        std::vector<Piece> pieces;
        VelocityGain velocityGain = VelocityGain::None;
        HitSelect mode = HitSelect::Live;
        int spread = 0;                 // Live: брать удар из ±spread соседних слоёв
        std::array<NoteMap, 128> notes {};

        // Проверяет связность и строит индекс клавиш. Вызывается один раз перед публикацией кита.
        // Возвращает false, если модель некорректна (индексы слоёв или микрофонов за пределами)
        bool buildIndex()
        {
            for (auto& n : notes)
                n = {};

            if ((int) mics.size() > kMaxMics)
                return false;

            for (int p = 0; p < (int) pieces.size(); ++p)
            {
                auto& piece = pieces[(size_t) p];
                for (int h = 0; h < (int) piece.hits.size(); ++h)
                {
                    auto& hit = piece.hits[(size_t) h];
                    if (hit.layer < 0 || hit.layer >= (int) piece.layers.size() || hit.key < 0 || hit.key > 127)
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
                        map.layers.push_back ({ hit.layer, layer.velLo, layer.velHi, layer.pickExact, {} });
                        ref = &map.layers.back();
                    }
                    ref->hits.push_back (h);
                }
            }

            for (auto& map : notes)
                std::sort (map.layers.begin(), map.layers.end(), [] (const NoteMap::LayerRef& a, const NoteMap::LayerRef& b)
                           { return a.velLo != b.velLo ? a.velLo < b.velLo : a.layer < b.layer; });
            return true;
        }
    };
}
