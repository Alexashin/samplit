#include "samplit/core/Manifest.h"

#include <algorithm>
#include <set>

namespace samplit::pack
{
    namespace
    {
        juce::var num (int v) { return juce::var (v); }

        bool fail (juce::String& error, const juce::String& text)
        {
            error = text;
            return false;
        }

        int intProp (const juce::var& v, const char* name, int def)
        {
            const auto p = v.getProperty (name, juce::var());
            return p.isVoid() || p.isUndefined() ? def : (int) p;
        }

        juce::String strProp (const juce::var& v, const char* name)
        {
            const auto p = v.getProperty (name, juce::var());
            return p.isString() ? p.toString() : juce::String();
        }

        // Поля объекта, которых нет в списке известных: сохраняем без изменений
        juce::NamedValueSet extraOf (const juce::var& obj, std::initializer_list<const char*> known)
        {
            juce::NamedValueSet extra;
            if (auto* o = obj.getDynamicObject())
                for (const auto& nv : o->getProperties())
                {
                    bool isKnown = false;
                    for (const char* k : known)
                        if (nv.name == juce::Identifier (k))
                        {
                            isKnown = true;
                            break;
                        }
                    if (! isKnown)
                        extra.set (nv.name, nv.value);
                }
            return extra;
        }

        void writeExtra (juce::DynamicObject& o, const juce::NamedValueSet& extra)
        {
            for (const auto& nv : extra)
                o.setProperty (nv.name, nv.value);
        }

        // Список диапазонов целых для сообщений: "74, 83..84"
        juce::String describeRanges (const std::vector<std::pair<int, int>>& ranges)
        {
            juce::StringArray parts;
            for (const auto& [lo, hi] : ranges)
            {
                if (parts.size() >= 8)
                {
                    parts.add ("...");
                    break;
                }
                parts.add (lo == hi ? juce::String (lo) : juce::String (lo) + ".." + juce::String (hi));
            }
            return parts.joinIntoString (", ");
        }
    }

    // ---------------------------------------------------------------- разбор

    bool Manifest::fromJson (const juce::String& json, Manifest& out, juce::String& error)
    {
        juce::var root;
        if (const auto r = juce::JSON::parse (json, root); r.failed())
            return fail (error, "манифест: "_s + r.getErrorMessage());
        if (! root.isObject())
            return fail (error, "манифест: ожидался объект JSON"_s);

        Manifest m;
        m.format = intProp (root, "format", 0);
        if (m.format != 1)
            return fail (error, "манифест: неподдерживаемая версия формата "_s + juce::String (m.format));

        m.id = strProp (root, "id");
        m.title = strProp (root, "title");
        m.tier = strProp (root, "tier");
        m.version = intProp (root, "version", 1);
        if (const auto* req = root.getProperty ("requires", juce::var()).getArray())
            for (const auto& r : *req)
                m.requiresPacks.push_back (r.toString());

        if (const auto* mics = root.getProperty ("mics", juce::var()).getArray())
            for (const auto& v : *mics)
            {
                Mic mic;
                mic.id = strProp (v, "id");
                mic.name = strProp (v, "name");
                mic.role = strProp (v, "role");
                mic.out = intProp (v, "out", 0);
                mic.extra = extraOf (v, { "id", "name", "role", "out" });
                m.mics.push_back (mic);
            }

        if (const auto* pieces = root.getProperty ("pieces", juce::var()).getArray())
            for (const auto& pv : *pieces)
            {
                Piece piece;
                piece.id = strProp (pv, "id");
                piece.name = strProp (pv, "name");
                piece.choke = strProp (pv, "choke");
                piece.extra = extraOf (pv, { "id", "name", "keys", "choke", "layers", "hits" });
                if (const auto* keys = pv.getProperty ("keys", juce::var()).getArray())
                    for (const auto& k : *keys)
                        piece.keys.push_back ((int) k);

                if (const auto* layers = pv.getProperty ("layers", juce::var()).getArray())
                    for (const auto& lv : *layers)
                    {
                        Layer layer;
                        layer.id = strProp (lv, "id");
                        layer.artic = strProp (lv, "artic");
                        layer.velLo = intProp (lv, "velLo", 1);
                        layer.velHi = intProp (lv, "velHi", 127);
                        layer.extra = extraOf (lv, { "id", "artic", "velLo", "velHi" });
                        piece.layers.push_back (layer);
                    }

                if (const auto* hits = pv.getProperty ("hits", juce::var()).getArray())
                    for (const auto& hv : *hits)
                    {
                        Hit hit;
                        hit.id = strProp (hv, "id");
                        hit.layer = strProp (hv, "layer");
                        hit.peerGroup = strProp (hv, "peerGroup");  // копия строки, сам ключ остаётся в extra
                        hit.key = intProp (hv, "key", 60);
                        hit.velLo = intProp (hv, "velLo", 1);
                        hit.velHi = intProp (hv, "velHi", hit.velLo);
                        hit.extra = extraOf (hv, { "id", "layer", "key", "velLo", "velHi", "files" });

                        if (auto* files = hv.getProperty ("files", juce::var()).getDynamicObject())
                            for (const auto& prop : files->getProperties())
                            {
                                HitFile f;
                                f.path = strProp (prop.value, "path");
                                f.chunk = intProp (prop.value, "chunk", -1);
                                f.tailChunk = intProp (prop.value, "tailChunk", -1);
                                f.gainDb = (float) (double) prop.value.getProperty ("gainDb", 0.0);
                                f.start = intProp (prop.value, "start", 0);
                                f.end = intProp (prop.value, "end", 0);
                                f.frames = intProp (prop.value, "frames", 0);
                                f.rate = (double) prop.value.getProperty ("rate", 0.0);
                                f.peak = intProp (prop.value, "peak", -1);
                                f.extra = extraOf (prop.value, { "path", "chunk", "tailChunk", "gainDb", "start", "end",
                                                                 "frames", "rate", "peak" });
                                hit.files[prop.name.toString()] = f;
                            }

                        piece.hits.push_back (std::move (hit));
                    }

                m.pieces.push_back (std::move (piece));
            }

        m.extra = extraOf (root, { "format", "id", "version", "tier", "requires", "title", "mics", "pieces" });
        out = std::move (m);
        return true;
    }

    // ---------------------------------------------------------------- запись

    juce::String Manifest::toJson() const
    {
        auto* root = new juce::DynamicObject();
        root->setProperty ("format", num (format));
        root->setProperty ("id", id);
        root->setProperty ("version", num (version));
        root->setProperty ("tier", tier);
        juce::Array<juce::var> req;
        for (const auto& r : requiresPacks)
            req.add (r);
        root->setProperty ("requires", req);
        root->setProperty ("title", title);

        juce::Array<juce::var> micArr;
        for (const auto& m : mics)
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("id", m.id);
            o->setProperty ("name", m.name);
            o->setProperty ("role", m.role);
            o->setProperty ("out", num (m.out));
            writeExtra (*o, m.extra);
            micArr.add (juce::var (o));
        }
        root->setProperty ("mics", micArr);

        juce::Array<juce::var> pieceArr;
        for (const auto& p : pieces)
        {
            auto* po = new juce::DynamicObject();
            po->setProperty ("id", p.id);
            po->setProperty ("name", p.name);
            juce::Array<juce::var> keys;
            for (int k : p.keys)
                keys.add (k);
            po->setProperty ("keys", keys);
            po->setProperty ("choke", p.choke.isEmpty() ? juce::var() : juce::var (p.choke));

            juce::Array<juce::var> layerArr;
            for (const auto& l : p.layers)
            {
                auto* lo = new juce::DynamicObject();
                lo->setProperty ("id", l.id);
                lo->setProperty ("artic", l.artic);
                lo->setProperty ("velLo", num (l.velLo));
                lo->setProperty ("velHi", num (l.velHi));
                writeExtra (*lo, l.extra);
                layerArr.add (juce::var (lo));
            }
            po->setProperty ("layers", layerArr);

            juce::Array<juce::var> hitArr;
            for (const auto& h : p.hits)
            {
                auto* ho = new juce::DynamicObject();
                ho->setProperty ("id", h.id);
                ho->setProperty ("layer", h.layer);
                ho->setProperty ("key", num (h.key));
                ho->setProperty ("velLo", num (h.velLo));
                ho->setProperty ("velHi", num (h.velHi));

                auto* fo = new juce::DynamicObject();
                for (const auto& [micId, f] : h.files)
                {
                    auto* e = new juce::DynamicObject();
                    if (f.path.isNotEmpty())
                        e->setProperty ("path", f.path);
                    if (f.chunk >= 0)
                        e->setProperty ("chunk", num (f.chunk));
                    if (f.tailChunk >= 0)
                        e->setProperty ("tailChunk", num (f.tailChunk));
                    e->setProperty ("gainDb", (double) f.gainDb);
                    e->setProperty ("start", num (f.start));
                    e->setProperty ("end", num (f.end));
                    if (f.chunk >= 0)
                    {
                        e->setProperty ("frames", num (f.frames));
                        e->setProperty ("rate", f.rate);
                    }
                    if (f.peak >= 0)
                        e->setProperty ("peak", num (f.peak));
                    writeExtra (*e, f.extra);
                    fo->setProperty (juce::Identifier (micId), juce::var (e));
                }
                ho->setProperty ("files", juce::var (fo));
                if (h.peerGroup.isNotEmpty() && ! h.extra.contains ("peerGroup"))
                    ho->setProperty ("peerGroup", h.peerGroup);
                writeExtra (*ho, h.extra);
                hitArr.add (juce::var (ho));
            }
            po->setProperty ("hits", hitArr);
            writeExtra (*po, p.extra);
            pieceArr.add (juce::var (po));
        }
        root->setProperty ("pieces", pieceArr);
        writeExtra (*root, extra);

        return juce::JSON::toString (juce::var (root), false, 4);
    }

    const Mic* Manifest::findMic (const juce::String& micId) const
    {
        for (const auto& m : mics)
            if (m.id == micId)
                return &m;
        return nullptr;
    }

    int Manifest::countHits() const
    {
        int n = 0;
        for (const auto& p : pieces)
            n += (int) p.hits.size();
        return n;
    }

    // ---------------------------------------------------------------- проверка

    VerifyReport verifyManifest (const Manifest& m)
    {
        VerifyReport r;
        auto err = [&r] (const juce::String& s) { r.errors.push_back (s); };
        auto warn = [&r] (const juce::String& s) { r.warnings.push_back (s); };

        if (m.id.isEmpty())
            err ("у пакета нет id"_s);

        std::set<juce::String> micIds;
        for (const auto& mic : m.mics)
        {
            if (mic.id.isEmpty())
                err ("микрофон без id"_s);
            else if (! micIds.insert (mic.id).second)
                err ("микрофон "_s + mic.id + " объявлен дважды"_s);
            if (mic.out < 0 || mic.out > 16)
                err ("микрофон "_s + mic.id + ": выход "_s + juce::String (mic.out) + " вне диапазона 0..16"_s);
        }

        std::set<juce::String> pieceIds, hitIds;
        for (const auto& piece : m.pieces)
        {
            const auto where = "барабан "_s + piece.id + ": ";
            if (piece.id.isEmpty())
                err ("барабан без id"_s);
            else if (! pieceIds.insert (piece.id).second)
                err ("барабан "_s + piece.id + " объявлен дважды"_s);

            std::set<juce::String> layerIds;
            for (const auto& l : piece.layers)
            {
                if (! layerIds.insert (l.id).second)
                    err (where + "слой "_s + l.id + " объявлен дважды"_s);
                if (l.velLo < 1 || l.velHi > 127 || l.velLo > l.velHi)
                    err (where + "слой "_s + l.id + ": неверное окно velocity"_s);
            }

            // key -> окна velocity ударов
            std::map<int, std::vector<std::pair<int, int>>> windows;
            for (const auto& hit : piece.hits)
            {
                const auto h = where + "удар "_s + hit.id + ": ";
                if (hit.id.isEmpty())
                    err (where + "удар без id"_s);
                else if (! hitIds.insert (hit.id).second)
                    err (h + "id повторяется"_s);

                if (hit.key < 0 || hit.key > 127)
                    err (h + "клавиша "_s + juce::String (hit.key) + " вне диапазона 0..127"_s);
                else if (std::find (piece.keys.begin(), piece.keys.end(), hit.key) == piece.keys.end())
                    warn (h + "клавиши "_s + juce::String (hit.key) + " нет в списке клавиш барабана"_s);

                if (hit.velLo < 1 || hit.velHi > 127 || hit.velLo > hit.velHi)
                    err (h + "неверное окно velocity "_s + juce::String (hit.velLo) + ".." + juce::String (hit.velHi));
                else
                    windows[hit.key].push_back ({ hit.velLo, hit.velHi });

                if (hit.layer.isNotEmpty() && ! layerIds.count (hit.layer))
                    err (h + "неизвестный слой "_s + hit.layer);

                if (hit.files.empty())
                    err (h + "нет ни одного файла"_s);
                for (const auto& [micId, f] : hit.files)
                {
                    if (! micIds.count (micId))
                        err (h + "файл для неизвестного микрофона "_s + micId);
                    if (f.path.isEmpty() && f.chunk < 0)
                        err (h + "микрофон "_s + micId + ": не задан ни path, ни chunk"_s);
                }
            }

            for (auto& [key, list] : windows)
            {
                std::sort (list.begin(), list.end());
                std::vector<std::pair<int, int>> overlaps, holes;
                int coveredTo = list.front().second;
                for (size_t i = 1; i < list.size(); ++i)
                {
                    const auto [lo, hi] = list[i];
                    if (lo <= coveredTo)
                        overlaps.push_back ({ lo, std::min (hi, coveredTo) });
                    else if (lo > coveredTo + 1)
                        holes.push_back ({ coveredTo + 1, lo - 1 });
                    coveredTo = std::max (coveredTo, hi);
                }
                const auto kname = "барабан "_s + piece.id + ", клавиша "_s + juce::String (key) + ": ";
                if (! overlaps.empty())
                    warn (kname + "окна velocity пересекаются: "_s + describeRanges (overlaps));
                if (! holes.empty())
                    warn (kname + "дыры по velocity: "_s + describeRanges (holes));
            }
        }
        return r;
    }
}
