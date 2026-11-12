#include "KitLoader.h"
#include "Setup.h"
#include "Studio600Names.h"
#include "DiskStreamer.h"
#include "PackSource.h"

#include "samplit/core/Wav.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <optional>
#include <thread>
#include <tuple>

namespace samplit
{
    using pack::operator""_s;

    // ---------------------------------------------------------------- режимы выбора удара

    bool pickFromString (const juce::String& s, Pick& out)
    {
        if (s == "random")      out = Pick::Random;
        else if (s == "cycle")  out = Pick::Cycle;
        else if (s == "exact")  out = Pick::Exact;
        else                    return false;
        return true;
    }

    juce::String pickToString (Pick p)
    {
        return p == Pick::Cycle ? "cycle" : (p == Pick::Exact ? "exact" : "random");
    }

    // ---------------------------------------------------------------- загрузка

    namespace
    {
        struct LoadedPack
        {
            juce::String id;
            std::shared_ptr<pack::PackReader> reader;     // делится с PackSource потоковых клипов
            pack::PackKey key {};
            pack::Manifest manifest;
            juce::File libraryRoot;                 // не пусто: режим локальной библиотеки, reader нет
            std::vector<juce::String> paths;        // локальный режим: «блок» = индекс файла в этом списке

            int chunkCount() const { return reader != nullptr ? reader->getChunkCount() : (int) paths.size(); }
        };

        // Что декодировать: блок пакета с обрезкой. Одинаковые блоки декодируются один раз
        using ClipKey = std::tuple<int, int, int, int, int, int, int, int>;  // пакет, блок, start, end, tailFirstChunk, кадров всего, chunkFrames, headFrames

        struct ClipJob
        {
            ClipKey key;
            std::string sourceKey;
            ClipPtr clip;
            juce::String error;
        };

        struct FileRef
        {
            int piece, hit, mic;
            int job;
            float gainDb;
        };

        bool isCancelled (const KitLoadOptions& o) { return o.cancel != nullptr && o.cancel->load(); }

        // Откуда клип: по этой строке загрузчик узнаёт тот же клип в текущем ките (KitLoadOptions::reuse)
        std::string sourceKeyOf (const LoadedPack& pk, const ClipKey& key, double headSeconds)
        {
            juce::String s;
            if (pk.reader == nullptr)
                s = "w|"_s + pk.libraryRoot.getChildFile (pk.paths[(size_t) std::get<1> (key)]).getFullPathName()
                    + "|"_s + juce::String (headSeconds);
            else
                s = "p|"_s + pk.id + "|v"_s + juce::String (pk.manifest.version) + "|c"_s + juce::String (std::get<1> (key));
            s << "|" << std::get<2> (key) << "|" << std::get<3> (key) << "|t" << std::get<4> (key) << "|n" << std::get<5> (key) << "|k" << std::get<6> (key) << "|h" << std::get<7> (key);
            return s.toStdString();
        }

        ClipPtr decodeJob (const LoadedPack& pk, const ClipKey& key, double headSeconds, std::string sourceKey, juce::String& error)
        {
            pack::DecodedAudio audio;
            if (pk.reader == nullptr)
            {
                // Локальная библиотека: WAV целиком, start/end из .nki читает сам readAudioFile
                const auto file = pk.libraryRoot.getChildFile (pk.paths[(size_t) std::get<1> (key)]);
                if (! file.isAChildOf (pk.libraryRoot))
                {
                    error = "путь вне папки библиотеки: "_s + file.getFullPathName();
                    return nullptr;
                }

                // Длинный 24-битный моно WAV: в памяти только голова, хвост потом читает DiskStreamer. Остальное
                // (короткие, не 24 бит, не моно) целиком, как раньше
                pack::WavInfo info;
                juce::String ignored;
                if (headSeconds > 0.0 && pack::readWavInfo (file, info, ignored) && info.channels == 1 && info.bits == 24 && ! info.isFloat)
                {
                    const int64_t from = std::max (0, std::get<2> (key));
                    const int64_t to = std::get<3> (key) > 0 ? std::min<int64_t> (std::get<3> (key), info.frames) : info.frames;
                    const int64_t headFrames = (int64_t) (headSeconds * info.sampleRate);
                    if (from < to && to - from > headFrames + DiskStreamer::kChunkFrames && to - from < (int64_t) INT_MAX)
                    {
                        juce::FileInputStream in (file);
                        std::vector<float> head ((size_t) headFrames);
                        std::vector<uint8_t> scratch;
                        if (in.failedToOpen() || ! pack::readWav24 (in, info.dataOffset + from * 3, (int) headFrames, head.data(), scratch))
                        {
                            error = "ошибка чтения "_s + file.getFullPathName();
                            return nullptr;
                        }
                        return DecodedClip::fromStream (std::move (head), (int) (to - from), info.sampleRate,
                                                        std::make_shared<WavSource> (file.getFullPathName().toStdString(), info.dataOffset + from * 3),
                                                        std::move (sourceKey));
                    }
                }

                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                if (! pack::readAudioFile (formats, file, std::get<2> (key), std::get<3> (key), audio, error))
                    return nullptr;
                return DecodedClip::fromFloat (pack::toFloat24 (audio), audio.sampleRate, std::move (sourceKey));
            }
            // Файл с хвостом (формат v2): в памяти только голова, куски хвоста потом читает DiskStreamer (PackSource)
            if (std::get<4> (key) >= 0)
            {
                pack::HitFile whole;
                whole.chunk = std::get<1> (key);
                whole.tailFirstChunk = std::get<4> (key);
                whole.frames = std::get<5> (key);
                whole.chunkFrames = std::get<6> (key);
                whole.headFrames = std::get<7> (key);

                juce::String tailError;
                if (! pack::checkTailFields (whole, pk.reader->getChunkCount(), tailError))
                {
                    error = tailError;
                    return nullptr;
                }
                if (! pk.reader->decodeChunk (pk.key, whole.chunk, audio, error))
                    return nullptr;
                if (audio.channels != 1 || audio.frames != whole.headFrames)
                {
                    error = "длина головы не совпадает с манифестом"_s;
                    return nullptr;
                }
                return DecodedClip::fromStream (pack::toFloat24 (audio), whole.frames, audio.sampleRate,
                                                std::make_shared<PackSource> (pk.reader, pk.key, whole, pk.id.toStdString()),
                                                std::move (sourceKey));
            }
            if (! pk.reader->decodeChunk (pk.key, std::get<1> (key), audio, error))
                return nullptr;
            if (audio.channels != 1)
            {
                error = "не моно"_s;
                return nullptr;
            }

            const int start = std::get<2> (key), end = std::get<3> (key);
            if (start > 0 || (end > 0 && end < audio.frames))
            {
                const int from = juce::jlimit (0, audio.frames, start);
                const int to = end > 0 ? juce::jlimit (from, audio.frames, end) : audio.frames;
                audio.samples = std::vector<int32_t> (audio.samples.begin() + from, audio.samples.begin() + to);
                audio.frames = to - from;
            }
            if (audio.frames <= 0)
            {
                error = "пустой файл"_s;
                return nullptr;
            }
            return DecodedClip::fromFloat (pack::toFloat24 (audio), audio.sampleRate, std::move (sourceKey));
        }

        juce::String noteName (int key)
        {
            static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            return juce::String (names[(size_t) juce::jlimit (0, 127, key) % 12]) + juce::String (juce::jlimit (0, 127, key) / 12 - 2);
        }

        // Загрузка по установке: пакеты, микрофоны, декодирование с повторным
        // использованием клипов, файлы в удары и индекс клавиш. Модель барабанов собирает вызывающий
        struct Loader
        {
            Loader (const KitLoadOptions& o, KitLoadResult& r) : options (o), res (r), localLibrary (o.libraryRoot != juce::File()) {}

            const KitLoadOptions& options;
            KitLoadResult& res;
            const bool localLibrary;
            std::vector<LoadedPack> packs;
            std::shared_ptr<Kit> kit = std::make_shared<Kit>();
            std::map<juce::String, int> micIndex, chokeIds;
            std::vector<ClipJob> jobs;
            std::map<ClipKey, int> jobIndex;
            std::vector<FileRef> refs;
            int zeroFiles = 0;

            // false: res.error заполнена
            bool openPacks (const std::vector<juce::String>& ids)
            {
                if (! localLibrary && options.requireLibrary)
                {
                    res.error = "выберите папку библиотеки («Моя библиотека…»)"_s;
                    return false;
                }
                std::map<juce::String, juce::File> files;
                if (! localLibrary)
                    for (const auto& f : options.packDir.findChildFiles (juce::File::findFiles, false, "*.splpack"))
                    {
                        juce::String err;
                        if (auto r = pack::PackReader::open (f, err))
                            files[r->getHeader().packId] = f;
                    }

                for (const auto& id : ids)
                {
                    if (localLibrary)
                    {
                        LoadedPack lp;
                        lp.id = id;
                        lp.libraryRoot = options.libraryRoot;
                        const auto mf = options.manifestDir.getChildFile (id + ".json");
                        juce::String err;
                        if (! mf.existsAsFile() || ! pack::Manifest::fromJson (mf.loadFileAsString(), lp.manifest, err))
                        {
                            res.missingPacks.push_back (id);
                            continue;
                        }

                        // Пути файлов в номера «блоков»: один файл на разных ударах декодируется один раз
                        std::map<juce::String, int> pathIndex;
                        for (auto& mp : lp.manifest.pieces)
                            for (auto& mh : mp.hits)
                                for (auto& [micId, f] : mh.files)
                                    if (f.path.isNotEmpty())
                                    {
                                        const auto it = pathIndex.emplace (f.path, (int) lp.paths.size());
                                        if (it.second)
                                            lp.paths.push_back (f.path);
                                        f.chunk = it.first->second;
                                    }
                        packs.push_back (std::move (lp));
                        continue;
                    }

                    const auto it = files.find (id);
                    LoadedPack lp;
                    juce::String err;
                    if (it == files.end() || ! options.keyFor || ! options.keyFor (id, lp.key))
                    {
                        res.missingPacks.push_back (id);
                        continue;
                    }

                    lp.id = id;
                    lp.reader = pack::PackReader::open (it->second, err);
                    if (lp.reader == nullptr || ! lp.reader->readManifest (lp.key, lp.manifest, err))
                    {
                        res.error = "Пакет "_s + id + ": "_s + err;
                        return false;
                    }

                    for (const auto& req : lp.manifest.requiresPacks)
                        if (std::find (ids.begin(), ids.end(), req) == ids.end())
                            res.warnings.push_back ("Пакету "_s + id + " нужен пакет "_s + req + ", его нет в установке"_s);

                    packs.push_back (std::move (lp));
                }

                if (packs.empty())
                {
                    res.error = localLibrary ? "Для установки нет манифестов в "_s + options.manifestDir.getFullPathName()
                                              : "Ни один пакет установки не доступен (нет файла пакета или лицензии)"_s;
                    return false;
                }

                // Хвост (tailFirstChunk) бывает только у файлов пакета; в локальной библиотеке (путь к WAV) это ошибка манифеста
                for (const auto& pk : packs)
                    if (pk.reader == nullptr)
                        for (const auto& mp : pk.manifest.pieces)
                            for (const auto& mh : mp.hits)
                                for (const auto& [micId, f] : mh.files)
                                    if (f.tailFirstChunk >= 0)
                                    {
                                        res.error = "Манифест "_s + pk.id + ": удар "_s + mh.id + ", микрофон "_s + micId
                                                    + ": tailFirstChunk допустим только в пакете"_s;
                                        return false;
                                    }
                return true;
            }

            int packIndex (const juce::String& id) const
            {
                for (size_t i = 0; i < packs.size(); ++i)
                    if (packs[i].id == id)
                        return (int) i;
                return -1;
            }

            // Микрофоны всех пакетов, слитые по id. Индекс микрофона фиксирован: известные id Studio600 лежат в своих слотах
            // (kStudio600Mics) независимо от состава установки, незнакомые занимают свободные слоты после них.
            // Слоты без микрофона остаются заглушками с пустым id
            bool collectMics()
            {
                for (const auto& pk : packs)
                    for (const auto& m : pk.manifest.mics)
                    {
                        if (micIndex.count (m.id) > 0)
                            continue;

                        int slot = studio600MicSlot (m.id.toStdString());
                        if (slot < 0)
                        {
                            slot = kStudio600MicCount;
                            while (slot < (int) kit->mics.size() && ! kit->mics[(size_t) slot].id.empty())
                                ++slot;
                        }
                        if (slot >= Kit::kMaxMics)
                        {
                            res.error = "Микрофонов больше "_s + juce::String (Kit::kMaxMics - kStudio600MicCount) + " незнакомых"_s;
                            return false;
                        }
                        if ((int) kit->mics.size() <= slot)
                            kit->mics.resize ((size_t) slot + 1);

                        micIndex[m.id] = slot;
                        const bool studio = pk.id.startsWith ("studio600-");
                        const auto name = studio ? studio600MicName (m.id.toStdString()) : nullptr;
                        kit->mics[(size_t) slot] = { m.id.toStdString(), name != nullptr ? name : m.name.toStdString(), m.role.toStdString(),
                                                     juce::jlimit (0, kNumOutputs, m.out), studio ? studio600MicPan (m.id.toStdString()) : 0.0f };
                    }
                return true;
            }

            // Файлы удара mh (пакет pi) в задания на декодирование. hit уже лежит в kit->pieces[p] или будет туда добавлен под индексом hi
            void addFiles (int pi, int p, int hi, const pack::Hit& mh)
            {
                for (const auto& [micId, f] : mh.files)
                {
                    const auto mic = micIndex.find (micId);
                    if (mic == micIndex.end() || f.chunk < 0 || f.chunk >= packs[(size_t) pi].chunkCount())
                        continue;
                    if (f.peak == 0)
                    {
                        ++zeroFiles;
                        continue;
                    }

                    const ClipKey key { pi, f.chunk, f.start, f.end, f.tailFirstChunk, f.frames, f.chunkFrames, f.headFrames };
                    const auto [it, added] = jobIndex.emplace (key, (int) jobs.size());
                    if (added)
                        jobs.push_back ({ key, {}, nullptr, {} });
                    refs.push_back ({ p, hi, mic->second, it->second, f.gainDb });
                }
            }

            // Декодирует недостающие клипы (текущий кит даёт готовые), раздаёт файлы ударам, строит индекс клавиш
            void finish()
            {
                if (zeroFiles > 0)
                    res.warnings.push_back (juce::String (zeroFiles) + " файлов из нулей пропущено"_s);

                std::map<std::string, ClipPtr> cache;
                if (options.reuse != nullptr)
                    for (const auto& piece : options.reuse->pieces)
                        for (const auto& hit : piece.hits)
                            for (const auto& f : hit.files)
                                if (f.clip != nullptr && ! f.clip->sourceKey.empty())
                                    cache.emplace (f.clip->sourceKey, f.clip);

                std::vector<int> pending;
                for (int i = 0; i < (int) jobs.size(); ++i)
                {
                    auto& job = jobs[(size_t) i];
                    job.sourceKey = sourceKeyOf (packs[(size_t) std::get<0> (job.key)], job.key, options.headSeconds);
                    const auto it = cache.find (job.sourceKey);
                    if (it != cache.end())
                    {
                        job.clip = it->second;
                        ++res.reusedClips;
                    }
                    else
                        pending.push_back (i);
                }

                // Рабочие потоки берут блоки по очереди
                const int total = (int) pending.size();
                std::atomic<int> next { 0 }, done { 0 };
                std::atomic<bool> failed { false };
                const int workers = juce::jlimit (1, 16, options.threads > 0 ? options.threads : (int) std::thread::hardware_concurrency());

                auto work = [&]
                {
                    for (;;)
                    {
                        const int n = next.fetch_add (1);
                        if (n >= total || failed.load() || isCancelled (options))
                            return;
                        auto& job = jobs[(size_t) pending[(size_t) n]];
                        try
                        {
                            job.clip = decodeJob (packs[(size_t) std::get<0> (job.key)], job.key, options.headSeconds, job.sourceKey, job.error);
                        }
                        catch (const std::bad_alloc&)
                        {
                            job.error = "не хватило памяти"_s;
                        }
                        if (job.clip == nullptr)
                            failed.store (true);
                        if (options.progress)
                            options.progress ((float) (done.fetch_add (1) + 1) / (float) juce::jmax (1, total));
                    }
                };

                std::vector<std::thread> threads;
                for (int i = 1; i < workers; ++i)
                    threads.emplace_back (work);
                work();
                for (auto& t : threads)
                    t.join();

                if (isCancelled (options))
                {
                    res.cancelled = true;
                    return;
                }
                if (failed.load())
                {
                    for (const auto& j : jobs)
                        if (j.clip == nullptr && j.error.isNotEmpty())
                        {
                            res.error = "Блок "_s + juce::String (std::get<1> (j.key)) + " пакета "_s
                                        + packs[(size_t) std::get<0> (j.key)].id + ": "_s + j.error;
                            break;
                        }
                    return;
                }
                res.decodedClips = total;

                for (const auto& r : refs)
                {
                    auto& f = kit->pieces[(size_t) r.piece].hits[(size_t) r.hit].files[(size_t) r.mic];
                    f.clip = jobs[(size_t) r.job].clip;
                    f.gainDb = r.gainDb;
                }
                for (const auto& j : jobs)
                    res.memoryBytes += (int64_t) j.clip->f32.size() * (int64_t) sizeof (float);

                if (! kit->buildIndex())
                {
                                juce::String where;
                    std::map<int, int> owner;       // клавиша -> индекс барабана
                    for (int pi = 0; pi < (int) kit->pieces.size() && where.isEmpty(); ++pi)
                        for (const auto& h : kit->pieces[(size_t) pi].hits)
                        {
                            const auto [it, added] = owner.emplace (h.key, pi);
                            if (! added && it->second != pi)
                            {
                                auto placeName = [&] (int idx) -> juce::String
                                {
                                    const int slot = kit->pieces[(size_t) idx].slot;
                                    for (const auto& d : kStudio600Places)
                                        if (d.slot == slot)
                                            return juce::String::fromUTF8 (d.id);
                                    return {};
                                };
                                const auto a = placeName (it->second), b = placeName (pi);
                                if (a.isNotEmpty() && b.isNotEmpty())
                                    where = " (места "_s + a + " и "_s + b + ", клавиша "_s + juce::String (h.key) + ")"_s;
                                break;
                            }
                        }
                    res.error = where.isNotEmpty()
                        ? "Кит некорректен: одна клавиша у двух мест установки"_s + where + ". Измените установку"_s
                        : "Кит некорректен: одна клавиша у двух барабанов. Проверьте состав установки"_s;
                    return;
                }

                res.kit = std::move (kit);
            }
        };

        // Выбор удара слоя по §3.3: выбор пользователя -> Group::pick -> pick слоя -> Random
        bool allowedPick (const pack::Group& g, Pick pk)
        {
            if (g.picks.empty())
                return true;
            return std::find (g.picks.begin(), g.picks.end(), pickToString (pk)) != g.picks.end();
        }
    }

    KitLoadResult loadKit (const KitSpec& spec, const KitLoadOptions& options)
    {
        KitLoadResult res;
        Loader L (options, res);

        std::vector<juce::String> ids;
        for (const auto& pl : spec.places)
            if (std::find (ids.begin(), ids.end(), pl.packId) == ids.end())
                ids.push_back (pl.packId);
        if (! L.openPacks (ids))
            return res;

        auto& kit = L.kit;
        kit->id = "setup";
        kit->title = "Studio600";
        kit->mode = spec.mode;
        kit->velocityGain = VelocityGain::None;
        if (! L.collectMics())
            return res;

        bool choicesFull = false;

        for (const auto& place : spec.places)
        {
            const auto where = "Место "_s + juce::String::fromUTF8 (place.placeId.c_str()) + ": "_s;
            const int pi = L.packIndex (place.packId);
            if (pi < 0)
                continue;       // пака нет: место молчит, packId уже в missingPacks
            const auto& pk = L.packs[(size_t) pi];

            const pack::Piece* mp = nullptr;
            if (place.pieceId.isNotEmpty())
            {
                for (const auto& c : pk.manifest.pieces)
                    if (c.id == place.pieceId)
                        mp = &c;
            }
            else
            {
                const auto* def = findPlace (place.placeId);
                for (const auto& c : pk.manifest.pieces)
                    if (def != nullptr && c.type == juce::String (def->type))
                    {
                        if (mp == nullptr)
                            mp = &c;
                        else
                        {
                            res.warnings.push_back (where + "в паке несколько барабанов этого типа, взят "_s + mp->id);
                            break;
                        }
                    }
            }
            if (mp == nullptr)
            {
                res.warnings.push_back (where + "в паке "_s + place.packId + " нет нужного барабана"_s);
                continue;
            }

            const int p = (int) kit->pieces.size();
            Piece piece;
            piece.id = mp->id.toStdString();
            const auto pname = pk.id.startsWith ("studio600-") ? studio600PieceName (piece.id) : nullptr;
            piece.name = pname != nullptr ? pname : mp->name.toStdString();
            piece.slot = place.slot;
            if (mp->choke.isNotEmpty())
            {
                const int id = (int) L.chokeIds.size();
                piece.choke = L.chokeIds.emplace (mp->choke, id).first->second;
            }
            for (int k : mp->keys)
            {
                const auto m = place.keyMap.find (k);
                if (m != place.keyMap.end() && std::find (piece.keys.begin(), piece.keys.end(), m->second) == piece.keys.end())
                    piece.keys.push_back (m->second);
            }

            int droppedLayers = 0, droppedHits = 0;

            // Слои: без непроверенных (если не просили) и без клавиш, которых нет в переносе
            std::map<juce::String, int> layerIndex;
            for (const auto& ml : mp->layers)
            {
                if (layerIndex.count (ml.id) > 0 || ((bool) ml.extra.getWithDefault ("unverified", false) && ! spec.includeUnverified))
                    continue;
                Layer l;
                l.id = ml.id.toStdString();
                l.artic = ml.artic.toStdString();
                l.velLo = ml.velLo;
                l.velHi = ml.velHi;
                l.label = ml.extra.getWithDefault ("label", {}).toString().toStdString();
                l.key = (int) ml.extra.getWithDefault ("key", -1);
                if (l.key >= 0)
                {
                    const auto m = place.keyMap.find (l.key);
                    if (m == place.keyMap.end())
                    {
                        ++droppedLayers;
                        continue;
                    }
                    l.key = m->second;
                }
                if (! pickFromString (ml.extra.getWithDefault ("pick", {}).toString(), l.pick))
                    l.pick = Pick::Random;
                l.peerGroup = ml.extra.getWithDefault ("peerGroup", {}).toString().toStdString();
                l.unverified = (bool) ml.extra.getWithDefault ("unverified", false);
                layerIndex[ml.id] = (int) piece.layers.size();
                piece.layers.push_back (std::move (l));
            }

            // Удары
            std::map<juce::String, int> hitIndex;
            std::map<juce::String, int> firstKey;       // слой -> первая (перенесённая) клавиша его ударов
            for (const auto& mh : mp->hits)
            {
                const bool unverified = (bool) mh.extra.getWithDefault ("unverified", false);
                const auto layer = layerIndex.find (mh.layer);
                if ((unverified && ! spec.includeUnverified) || layer == layerIndex.end() || hitIndex.count (mh.id) > 0)
                    continue;
                const auto key = place.keyMap.find (mh.key);
                if (key == place.keyMap.end())
                {
                    ++droppedHits;
                    continue;
                }

                Hit h;
                h.id = mh.id.toStdString();
                h.layer = layer->second;
                h.key = key->second;
                h.velLo = mh.velLo;
                h.velHi = mh.velHi;
                h.peerGroup = mh.peerGroup.toStdString();
                h.unverified = unverified;
                h.files.resize (kit->mics.size());
                firstKey.emplace (mh.layer, h.key);

                const int hi = (int) piece.hits.size();
                hitIndex[mh.id] = hi;
                L.addFiles (pi, p, hi, mh);
                piece.hits.push_back (std::move (h));
            }

            // Группы -> переключатели. Нет групп (старый пак): группа на слой
            std::vector<pack::Group> groups = mp->groups;
            if (groups.empty())
                for (const auto& l : piece.layers)
                {
                    pack::Group g;
                    g.id = juce::String (l.id);
                    const auto fk = firstKey.find (g.id);
                    const int note = l.key >= 0 ? l.key : (fk != firstKey.end() ? fk->second : -1);
                    g.name = (note >= 0 ? noteName (note) + " " : juce::String()) + juce::String (l.velLo) + "–"_s + juce::String (l.velHi);
                    g.layers = { g.id };
                    groups.push_back (std::move (g));
                }

            std::vector<bool> grouped (piece.layers.size(), false);
            for (const auto& g : groups)
            {
                std::vector<int> members;
                for (const auto& lid : g.layers)
                    if (const auto it = layerIndex.find (lid); it != layerIndex.end() && ! grouped[(size_t) it->second])
                        members.push_back (it->second);
                if (members.empty())
                    continue;       // у группы не осталось слоёв
                for (int m : members)
                    grouped[(size_t) m] = true;

                std::optional<Pick> groupPick;
                Pick parsed;
                if (const auto sp = place.picks.find (g.id.toStdString()); sp != place.picks.end() && allowedPick (g, sp->second))
                    groupPick = sp->second;
                else if (pickFromString (g.pick, parsed))
                    groupPick = parsed;

                int choice = -1;
                if ((int) kit->choices.size() < Kit::kMaxChoices)
                    choice = (int) kit->choices.size();
                else if (! choicesFull)
                {
                    choicesFull = true;
                    res.warnings.push_back ("Переключателей больше "_s + juce::String (Kit::kMaxChoices) + ": лишние не созданы"_s);
                }

                for (int m : members)
                {
                    auto& l = piece.layers[(size_t) m];
                    if (groupPick)
                        l.pick = *groupPick;
                    l.choice = choice;
                }
                if (choice >= 0)
                    kit->choices.push_back ({ (juce::String (place.placeId.c_str()) + ":" + g.id).toStdString(),
                                              (g.name.isNotEmpty() ? g.name : g.id).toStdString(),
                                              piece.layers[(size_t) members.front()].pick, { p } });
            }

            if (droppedLayers > 0 || droppedHits > 0)
                res.warnings.push_back (where + "отброшено слоёв "_s + juce::String (droppedLayers) + ", ударов "_s
                                        + juce::String (droppedHits) + ": их клавиш нет в переносе на место"_s);

            kit->pieces.push_back (std::move (piece));
        }

        L.finish();
        return res;
    }
}
