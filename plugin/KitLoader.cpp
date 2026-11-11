#include "KitLoader.h"
#include "Setup.h"
#include "Studio600Names.h"
#include "DiskStreamer.h"

#include "samplit/core/Wav.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <optional>
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
        using ClipKey = std::tuple<int, int, int, int, int>;     // пакет, блок, start, end, tailChunk

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
            s << "|" << std::get<2> (key) << "|" << std::get<3> (key) << "|t" << std::get<4> (key);
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
                                                        file.getFullPathName().toStdString(), info.dataOffset + from * 3,
                                                        std::move (sourceKey));
                    }
                }

                juce::AudioFormatManager formats;
                formats.registerBasicFormats();
                if (! pack::readAudioFile (formats, file, std::get<2> (key), std::get<3> (key), audio, error))
                    return nullptr;
                return DecodedClip::fromFloat (pack::toFloat24 (audio), audio.sampleRate, std::move (sourceKey));
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

        // Общая часть загрузки для пресета и для установки: пакеты, микрофоны, декодирование с повторным
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
                            res.warnings.push_back ("Пакету "_s + id + " нужен пакет "_s + req + ", его нет в пресете"_s);

                    packs.push_back (std::move (lp));
                }

                if (packs.empty())
                {
                    res.error = localLibrary ? "Для пресета нет манифестов в "_s + options.manifestDir.getFullPathName()
                                              : "Ни один пакет пресета не доступен (нет файла пакета или лицензии)"_s;
                    return false;
                }

                // Файл, разрезанный на голову и хвост (tailChunk), загрузчик пока не собирает: молча играть одну голову нельзя
                for (const auto& pk : packs)
                    for (const auto& mp : pk.manifest.pieces)
                        for (const auto& mh : mp.hits)
                            for (const auto& [micId, f] : mh.files)
                                if (f.tailChunk >= 0)
                                {
                                    res.error = "Пакет "_s + pk.id + ": удар "_s + mh.id + ", микрофон "_s + micId
                                                + ": файл разрезан на голову и хвост (tailChunk), такой формат пока не поддерживается"_s;
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

            // Микрофоны всех пакетов, слитые по id. Настройки микса берутся из mics, иначе из манифеста
            bool collectMics (const MicMap& settings)
            {
                for (const auto& pk : packs)
                    for (const auto& m : pk.manifest.mics)
                    {
                        if (micIndex.count (m.id) > 0)
                            continue;
                        if ((int) kit->mics.size() >= Kit::kMaxMics)
                        {
                            res.error = "Микрофонов больше "_s + juce::String (Kit::kMaxMics);
                            return false;
                        }

                        const auto set = settings.find (m.id);
                        const bool hasSet = set != settings.end();
                        const int out = hasSet && set->second.out >= 0 ? set->second.out : m.out;
                        micIndex[m.id] = (int) kit->mics.size();
                        const auto name = pk.id.startsWith ("studio600-") ? studio600MicName (m.id.toStdString()) : nullptr;
                        kit->mics.push_back ({ m.id.toStdString(), name != nullptr ? name : m.name.toStdString(), m.role.toStdString(),
                                               juce::jlimit (0, kNumOutputs, out) });

                        MicMix mix;
                        if (hasSet)
                            mix = { set->second.levelDb, set->second.pan, set->second.mute };
                        res.mix.push_back (mix);
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

                    const ClipKey key { pi, f.chunk, f.start, f.end, f.tailChunk };
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
                    // Кит из установки (у барабанов есть место): называем места, а не пресет
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
                        : "Кит некорректен: одна клавиша у двух барабанов. Проверьте список барабанов в пресете"_s;
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

    KitLoadResult loadKit (const Preset& preset, const KitLoadOptions& options)
    {
        KitLoadResult res;
        Loader L (options, res);

        // 1. Пакеты пресета: файл, ключ, манифест
        if (! L.openPacks (preset.packs))
            return res;

        // 2. Модель: микрофоны, барабаны, слои, удары (пока без звука)
        auto& kit = L.kit;
        auto& packs = L.packs;
        kit->id = preset.id.toStdString();
        kit->title = preset.name.toStdString();
        kit->mode = preset.mode;
        kit->velocityGain = preset.velocityToGain > 0.0f ? VelocityGain::Linear : VelocityGain::None;

        if (! L.collectMics (preset.mics))
            return res;

        std::map<juce::String, int> pieceIndex;
        std::vector<std::map<juce::String, int>> layerIndex;    // по барабану: id слоя → индекс
        std::vector<std::map<juce::String, int>> hitIndex;      // по барабану: id удара → индекс
        int skippedHits = 0;

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
                    const auto name = packs[(size_t) pi].id.startsWith ("studio600-") ? studio600PieceName (np.id) : nullptr;
                    np.name = name != nullptr ? name : mp.name.toStdString();
                    if (mp.choke.isNotEmpty())
                    {
                        const int id = (int) L.chokeIds.size();
                        np.choke = L.chokeIds.emplace (mp.choke, id).first->second;
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
                    if (! pickFromString (ml.extra.getWithDefault ("pick", {}).toString(), l.pick))
                        l.pick = Pick::Random;
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
                    L.addFiles (pi, p, hi, mh);
                    piece.hits.push_back (std::move (h));
                }
            }

        applyPickRules (preset, *kit);

        if (skippedHits > 0)
            res.warnings.push_back (juce::String (skippedHits) + " ударов пропущено (непроверенные, повторы или без слоя)"_s);

        L.finish();
        return res;
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
        if (! L.collectMics ({}))
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
