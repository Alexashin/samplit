#include "KitLoader.h"
#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <thread>
#include <tuple>

namespace samplit
{
    using pack::operator""_s;

    // ---------------------------------------------------------------- пресет

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

    // Правила пресета по порядку: позднее правило перекрывает раннее. Каждое правило становится переключателем
    void applyPickRules (const Preset& preset, Kit& kit)
    {
        std::vector<std::vector<bool>> fromManifest;
        for (const auto& piece : kit.pieces)
        {
            auto& exact = fromManifest.emplace_back();
            for (const auto& l : piece.layers)
                exact.push_back (l.pick == Pick::Exact);
        }

        for (const auto& rule : preset.pick)
        {
            const int choice = (int) kit.choices.size();
            bool used = false;
            std::vector<int> usedPieces;
            for (size_t p = 0; p < kit.pieces.size(); ++p)
            {
                auto& piece = kit.pieces[p];
                if (std::find (rule.pieces.begin(), rule.pieces.end(), juce::String (piece.id)) == rule.pieces.end())
                    continue;
                for (size_t i = 0; i < piece.layers.size(); ++i)
                {
                    auto& l = piece.layers[i];
                    const bool allKeys = rule.keyLo == 0 && rule.keyHi == 127;
                    if ((! allKeys && (l.key < rule.keyLo || l.key > rule.keyHi)) || (fromManifest[p][i] && ! rule.includeExact))
                        continue;
                    l.pick = rule.mode;
                    l.choice = choice;
                    used = true;
                    if (std::find (usedPieces.begin(), usedPieces.end(), (int) p) == usedPieces.end())
                        usedPieces.push_back ((int) p);
                }
            }
            if (! used)
                continue;   // барабанов правила нет в ките: переключатель не нужен

            juce::StringArray ids;
            for (const auto& id : rule.pieces)
                ids.add (id);
            const auto id = ids.joinIntoString (",") + ":" + juce::String (rule.keyLo) + "-" + juce::String (rule.keyHi);
            kit.choices.push_back ({ id.toStdString(), rule.name.toStdString(), rule.mode, std::move (usedPieces) });
        }
    }

    namespace
    {
        using MicMap = std::map<juce::String, Preset::MicSetting>;

        void readMics (const juce::DynamicObject& obj, MicMap& out)
        {
            auto* mics = obj.getProperty ("mics").getDynamicObject();
            if (mics == nullptr)
                return;
            for (const auto& prop : mics->getProperties())
            {
                auto* m = prop.value.getDynamicObject();
                if (m == nullptr)
                    continue;
                Preset::MicSetting s;
                s.levelDb = juce::jlimit (-60.0f, 12.0f, (float) (double) m->getProperty ("level"));
                s.pan = juce::jlimit (-1.0f, 1.0f, (float) (double) m->getProperty ("pan"));
                if (m->hasProperty ("out"))
                    s.out = juce::jlimit (0, kNumOutputs, (int) m->getProperty ("out"));
                s.mute = (bool) m->getProperty ("mute");
                out[prop.name.toString()] = s;
            }
        }

        juce::var micsToVar (const MicMap& mics)
        {
            auto* micsObj = new juce::DynamicObject();
            for (const auto& [micId, m] : mics)
            {
                auto* o = new juce::DynamicObject();
                o->setProperty ("level", (double) m.levelDb);
                o->setProperty ("pan", (double) m.pan);
                if (m.out >= 0)
                    o->setProperty ("out", m.out);
                o->setProperty ("mute", m.mute);
                micsObj->setProperty (micId, juce::var (o));
            }
            return juce::var (micsObj);
        }
    }

    bool Preset::fromJson (const juce::String& json, Preset& out, juce::String& error)
    {
        const auto root = juce::JSON::parse (json);
        auto* obj = root.getDynamicObject();
        if (obj == nullptr)
        {
            error = "Пресет: ожидался JSON-объект"_s;
            return false;
        }

        Preset p;
        p.id = obj->getProperty ("id").toString();
        p.name = obj->getProperty ("name").toString();
        p.comment = obj->getProperty ("comment").toString();
        p.mixId = obj->getProperty ("mix").toString();
        if (p.id.isEmpty())
        {
            error = "Пресет: нет id"_s;
            return false;
        }

        if (auto* arr = obj->getProperty ("packs").getArray())
            for (const auto& v : *arr)
                p.packs.push_back (v.toString());
        if (auto* arr = obj->getProperty ("pieces").getArray())
            for (const auto& v : *arr)
                p.pieces.push_back (v.toString());
        p.includeUnverified = (bool) obj->getProperty ("includeUnverified");

        if (auto* sel = obj->getProperty ("select").getDynamicObject())
        {
            const auto mode = sel->getProperty ("mode").toString();
            if (mode == "kontakt")
                p.mode = HitSelect::Kontakt;
            else if (mode != "live" && mode.isNotEmpty())
            {
                error = "Пресет: неизвестный режим выбора удара "_s + mode;
                return false;
            }
            p.layerSpread = juce::jmax (0, (int) sel->getProperty ("layerSpread"));
            p.velocityToGain = (float) (double) sel->getProperty ("velocityToGain");

            if (auto* rules = sel->getProperty ("pick").getArray())
                for (const auto& v : *rules)
                {
                    auto* r = v.getDynamicObject();
                    if (r == nullptr)
                        continue;
                    Preset::PickRule rule;
                    rule.name = r->getProperty ("name").toString();
                    if (auto* ps = r->getProperty ("pieces").getArray())
                        for (const auto& id : *ps)
                            rule.pieces.push_back (id.toString());
                    if (auto* keys = r->getProperty ("keys").getArray(); keys != nullptr && keys->size() == 2)
                    {
                        rule.keyLo = juce::jlimit (0, 127, (int) (*keys)[0]);
                        rule.keyHi = juce::jlimit (rule.keyLo, 127, (int) (*keys)[1]);
                    }
                    const auto m = r->getProperty ("mode").toString();
                    if (! pickFromString (m, rule.mode))
                    {
                        error = "Пресет: неизвестный режим pick "_s + m;
                        return false;
                    }
                    rule.includeExact = (bool) r->getProperty ("includeExact");
                    if (rule.name.isEmpty() || rule.pieces.empty())
                    {
                        error = "Пресет: у правила pick нет name или pieces"_s;
                        return false;
                    }
                    p.pick.push_back (std::move (rule));
                }
            if ((int) p.pick.size() > Kit::kMaxChoices)
            {
                error = "Пресет: правил pick больше "_s + juce::String (Kit::kMaxChoices);
                return false;
            }
        }

        readMics (*obj, p.mics);

        out = std::move (p);
        return true;
    }

    juce::String Preset::toJson() const
    {
        const auto strings = [] (const std::vector<juce::String>& v)
        {
            juce::Array<juce::var> a;
            for (const auto& x : v)
                a.add (x);
            return juce::var (a);
        };

        auto* root = new juce::DynamicObject();
        juce::var result (root);
        root->setProperty ("format", 1);
        root->setProperty ("id", id);
        root->setProperty ("name", name);
        if (comment.isNotEmpty())
            root->setProperty ("comment", comment);
        if (mixId.isNotEmpty())
            root->setProperty ("mix", mixId);
        root->setProperty ("packs", strings (packs));
        if (! pieces.empty())
            root->setProperty ("pieces", strings (pieces));
        root->setProperty ("includeUnverified", includeUnverified);

        auto* sel = new juce::DynamicObject();
        sel->setProperty ("mode", mode == HitSelect::Kontakt ? "kontakt" : "live");
        sel->setProperty ("layerSpread", layerSpread);
        sel->setProperty ("noRepeat", true);
        sel->setProperty ("velocityToGain", (double) velocityToGain);
        juce::Array<juce::var> rules;
        for (const auto& r : pick)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("name", r.name);
            o->setProperty ("pieces", strings (r.pieces));
            if (r.keyLo != 0 || r.keyHi != 127)
                o->setProperty ("keys", juce::Array<juce::var> { r.keyLo, r.keyHi });
            o->setProperty ("mode", pickToString (r.mode));
            if (r.includeExact)
                o->setProperty ("includeExact", true);
            rules.add (juce::var (o));
        }
        sel->setProperty ("pick", rules);
        root->setProperty ("select", juce::var (sel));
        root->setProperty ("mics", micsToVar (mics));
        return juce::JSON::toString (result);
    }

    bool Mix::fromJson (const juce::String& json, Mix& out, juce::String& error)
    {
        const auto root = juce::JSON::parse (json);
        auto* obj = root.getDynamicObject();
        if (obj == nullptr)
        {
            error = "Микс: ожидался JSON-объект"_s;
            return false;
        }
        Mix m;
        m.id = obj->getProperty ("id").toString();
        m.name = obj->getProperty ("name").toString();
        m.comment = obj->getProperty ("comment").toString();
        if (m.id.isEmpty())
        {
            error = "Микс: нет id"_s;
            return false;
        }
        readMics (*obj, m.mics);
        if (m.mics.empty())
        {
            error = "Микс: нет микрофонов"_s;
            return false;
        }
        out = std::move (m);
        return true;
    }

    bool Mix::fromFile (const juce::File& file, Mix& out, juce::String& error)
    {
        if (! file.existsAsFile())
        {
            error = "Нет файла микса: "_s + file.getFullPathName();
            return false;
        }
        return fromJson (file.loadFileAsString(), out, error);
    }

    juce::String Mix::toJson() const
    {
        auto* root = new juce::DynamicObject();
        juce::var result (root);
        root->setProperty ("format", 1);
        root->setProperty ("id", id);
        root->setProperty ("name", name);
        if (comment.isNotEmpty())
            root->setProperty ("comment", comment);
        root->setProperty ("mics", micsToVar (mics));
        return juce::JSON::toString (result);
    }

    bool Preset::fromFile (const juce::File& file, Preset& out, juce::String& error)
    {
        if (! file.existsAsFile())
        {
            error = "Нет файла пресета: "_s + file.getFullPathName();
            return false;
        }
        return fromJson (file.loadFileAsString(), out, error);
    }

    // ---------------------------------------------------------------- загрузка

    namespace
    {
        struct LoadedPack
        {
            juce::String id;
            std::unique_ptr<pack::PackReader> reader;
            pack::PackKey key {};
            pack::Manifest manifest;
            juce::File libraryRoot;                 // не пусто: режим локальной библиотеки, reader нет
            std::vector<juce::String> paths;        // локальный режим: «блок» = индекс файла в этом списке

            int chunkCount() const { return reader != nullptr ? reader->getChunkCount() : (int) paths.size(); }
        };

        // Что декодировать: блок пакета с обрезкой. Одинаковые блоки декодируются один раз
        using ClipKey = std::tuple<int, int, int, int>;     // пакет, блок, start, end

        struct ClipJob
        {
            ClipKey key;
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

        ClipPtr decodeJob (const LoadedPack& pk, const ClipKey& key, juce::String& error)
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
                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                if (! pack::readAudioFile (formats, file, std::get<2> (key), std::get<3> (key), audio, error))
                    return nullptr;
                return DecodedClip::fromFloat (pack::toFloat24 (audio), audio.sampleRate);
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
            return DecodedClip::fromFloat (pack::toFloat24 (audio), audio.sampleRate);
        }
    }

    KitLoadResult loadKit (const Preset& preset, const KitLoadOptions& options)
    {
        KitLoadResult res;

        // 1. Пакеты пресета: файл, ключ, манифест
        const bool localLibrary = options.libraryRoot != juce::File();
        std::map<juce::String, juce::File> files;
        if (! localLibrary)
            for (const auto& f : options.packDir.findChildFiles (juce::File::findFiles, false, "*.splpack"))
            {
                juce::String err;
                if (auto r = pack::PackReader::open (f, err))
                    files[r->getHeader().packId] = f;
            }

        std::vector<LoadedPack> packs;
        for (const auto& id : preset.packs)
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
                return res;
            }

            for (const auto& req : lp.manifest.requiresPacks)
                if (std::find (preset.packs.begin(), preset.packs.end(), req) == preset.packs.end())
                    res.warnings.push_back ("Пакету "_s + id + " нужен пакет "_s + req + ", его нет в пресете"_s);

            packs.push_back (std::move (lp));
        }

        if (packs.empty())
        {
            res.error = localLibrary ? "Для пресета нет манифестов в "_s + options.manifestDir.getFullPathName()
                                      : "Ни один пакет пресета не доступен (нет файла или лицензии)"_s;
            return res;
        }

        // 2. Модель: микрофоны, барабаны, слои, удары (пока без звука)
        auto kit = std::make_shared<Kit>();
        kit->id = preset.id.toStdString();
        kit->title = preset.name.toStdString();
        kit->mode = preset.mode;
        kit->spread = preset.layerSpread;
        kit->velocityGain = preset.velocityToGain > 0.0f ? VelocityGain::Linear : VelocityGain::None;

        std::map<juce::String, int> micIndex;
        for (const auto& pk : packs)
            for (const auto& m : pk.manifest.mics)
            {
                if (micIndex.count (m.id) > 0)
                    continue;
                if ((int) kit->mics.size() >= Kit::kMaxMics)
                {
                    res.error = "Микрофонов больше "_s + juce::String (Kit::kMaxMics);
                    return res;
                }

                const auto set = preset.mics.find (m.id);
                const bool hasSet = set != preset.mics.end();
                const int out = hasSet && set->second.out >= 0 ? set->second.out : m.out;
                micIndex[m.id] = (int) kit->mics.size();
                kit->mics.push_back ({ m.id.toStdString(), m.name.toStdString(), m.role.toStdString(),
                                       juce::jlimit (0, kNumOutputs, out) });

                MicMix mix;
                if (hasSet)
                    mix = { set->second.levelDb, set->second.pan, set->second.mute };
                res.mix.push_back (mix);
            }

        std::map<juce::String, int> pieceIndex, chokeIds;
        std::vector<std::map<juce::String, int>> layerIndex;    // по барабану: id слоя → индекс
        std::vector<std::map<juce::String, int>> hitIndex;      // по барабану: id удара → индекс
        std::vector<ClipJob> jobs;
        std::map<ClipKey, int> jobIndex;
        std::vector<FileRef> refs;
        int zeroFiles = 0, skippedHits = 0;

        for (int pi = 0; pi < (int) packs.size(); ++pi)
            for (const auto& mp : packs[(size_t) pi].manifest.pieces)
            {
                if (! preset.pieces.empty() && std::find (preset.pieces.begin(), preset.pieces.end(), mp.id) == preset.pieces.end())
                    continue;

                int p;
                if (const auto it = pieceIndex.find (mp.id); it != pieceIndex.end())
                    p = it->second;
                else
                {
                    p = (int) kit->pieces.size();
                    pieceIndex[mp.id] = p;
                    Piece np;
                    np.id = mp.id.toStdString();
                    np.name = mp.name.toStdString();
                    if (mp.choke.isNotEmpty())
                    {
                        const int id = (int) chokeIds.size();
                        np.choke = chokeIds.emplace (mp.choke, id).first->second;
                    }
                    kit->pieces.push_back (std::move (np));
                    layerIndex.emplace_back();
                    hitIndex.emplace_back();
                }
                auto& piece = kit->pieces[(size_t) p];
                for (int k : mp.keys)
                    if (std::find (piece.keys.begin(), piece.keys.end(), k) == piece.keys.end())
                        piece.keys.push_back (k);

                for (const auto& ml : mp.layers)
                {
                    if (layerIndex[(size_t) p].count (ml.id) > 0)
                        continue;
                    Layer l;
                    l.id = ml.id.toStdString();
                    l.artic = ml.artic.toStdString();
                    l.velLo = ml.velLo;
                    l.velHi = ml.velHi;
                    l.label = ml.extra.getWithDefault ("label", {}).toString().toStdString();
                    l.key = (int) ml.extra.getWithDefault ("key", -1);
                    l.pick = ml.extra.getWithDefault ("pick", {}).toString() == "exact" ? Pick::Exact : Pick::Random;
                    l.peerGroup = ml.extra.getWithDefault ("peerGroup", {}).toString().toStdString();
                    l.unverified = (bool) ml.extra.getWithDefault ("unverified", false);
                    layerIndex[(size_t) p][ml.id] = (int) piece.layers.size();
                    piece.layers.push_back (std::move (l));
                }

                for (const auto& mh : mp.hits)
                {
                    const bool unverified = (bool) mh.extra.getWithDefault ("unverified", false);
                    const auto layer = layerIndex[(size_t) p].find (mh.layer);
                    if ((unverified && ! preset.includeUnverified) || hitIndex[(size_t) p].count (mh.id) > 0
                        || layer == layerIndex[(size_t) p].end())
                    {
                        ++skippedHits;
                        continue;
                    }

                    Hit h;
                    h.id = mh.id.toStdString();
                    h.layer = layer->second;
                    h.key = mh.key;
                    h.velLo = mh.velLo;
                    h.velHi = mh.velHi;
                    h.peerGroup = mh.peerGroup.toStdString();
                    h.unverified = unverified;
                    h.files.resize (kit->mics.size());

                    const int hi = (int) piece.hits.size();
                    hitIndex[(size_t) p][mh.id] = hi;
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

                        const ClipKey key { pi, f.chunk, f.start, f.end };
                        const auto [it, added] = jobIndex.emplace (key, (int) jobs.size());
                        if (added)
                            jobs.push_back ({ key, nullptr, {} });
                        refs.push_back ({ p, hi, mic->second, it->second, f.gainDb });
                    }
                    piece.hits.push_back (std::move (h));
                }
            }

        applyPickRules (preset, *kit);

        if (zeroFiles > 0)
            res.warnings.push_back (juce::String (zeroFiles) + " файлов из нулей пропущено"_s);
        if (skippedHits > 0)
            res.warnings.push_back (juce::String (skippedHits) + " ударов пропущено (непроверенные, повторы или без слоя)"_s);

        // 3. Декодирование: рабочие потоки берут блоки по очереди
        const int total = (int) jobs.size();
        std::atomic<int> next { 0 }, done { 0 };
        std::atomic<bool> failed { false };
        const int workers = juce::jlimit (1, 16, options.threads > 0 ? options.threads : (int) std::thread::hardware_concurrency());

        auto work = [&]
        {
            for (;;)
            {
                const int i = next.fetch_add (1);
                if (i >= total || failed.load() || isCancelled (options))
                    return;
                auto& job = jobs[(size_t) i];
                try
                {
                    job.clip = decodeJob (packs[(size_t) std::get<0> (job.key)], job.key, job.error);
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
            return res;
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
            return res;
        }

        // 4. Файлы в удары, индекс клавиш
        for (const auto& r : refs)
        {
            auto& f = kit->pieces[(size_t) r.piece].hits[(size_t) r.hit].files[(size_t) r.mic];
            f.clip = jobs[(size_t) r.job].clip;
            f.gainDb = r.gainDb;
        }
        for (const auto& j : jobs)
            res.memoryBytes += (int64_t) j.clip->frames() * (int64_t) sizeof (float);

        if (! kit->buildIndex())
        {
            res.error = "Кит некорректен: одна клавиша у двух барабанов. Проверьте список барабанов в пресете"_s;
            return res;
        }

        res.kit = std::move (kit);
        return res;
    }
}
