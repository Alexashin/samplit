#include "Studio600Names.h"
#include "KitEngine.h"
#include "KitLoader.h"
#include "PackCatalog.h"
#include "PluginProcessor.h"
#include "spec_helper.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_audio_formats/juce_audio_formats.h>

using namespace samplit;
using samplit::pack::operator""_s;

namespace
{
    juce::File loaderDir()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("samplit-tests").getChildFile ("loader");
        dir.deleteRecursively();
        dir.createDirectory();
        return dir;
    }

    pack::DecodedAudio tone (int frames, int32_t level)
    {
        pack::DecodedAudio a;
        a.sampleRate = 44100.0;
        a.frames = frames;
        a.samples.assign ((size_t) frames, level);
        return a;
    }

    pack::HitFile fileOf (int chunk, int frames = 1000)
    {
        pack::HitFile f;
        f.chunk = chunk;
        f.frames = frames;
        f.rate = 44100.0;
        f.peak = 1000;
        return f;
    }

    // Пакет с барабаном kick: три удара на клавише 36, два микрофона. Удар 2 использует блок удара 0 (общий клип)
    struct TestPack
    {
        pack::PackKey key = pack::generateKey();
        pack::Manifest manifest;
    };

    TestPack writePack (const juce::File& dir, const juce::String& id, const juce::String& pieceId, int key,
                        bool withUnverified = false, const juce::String& layerPick = {},
                        bool swapMics = false, const std::vector<juce::String>& extraMics = {})
    {
        TestPack t;
        juce::String error;
        auto w = pack::PackWriter::create (dir.getChildFile (id + ".splpack"), id, t.key, error);
        REQUIRE (w != nullptr);

        const int a = w->addAudio (tone (1000, 100000), error);
        const int b = w->addAudio (tone (1000, 200000), error);
        const int c = w->addAudio (tone (1000, 300000), error);
        REQUIRE (a == 0);

        auto& m = t.manifest;
        m.id = id;
        m.mics = { { "kick_in", "Kick In", "close", 1 }, { "oh", "OH", "overhead", 2 } };
        if (swapMics)
            std::swap (m.mics[0], m.mics[1]);
        for (const auto& x : extraMics)
            m.mics.push_back ({ x, x, "close", 0 });

        pack::Piece p;
        p.id = pieceId;
        p.name = pieceId;
        p.keys = { key };
        p.layers = { { "all", "center", 1, 127 } };
        if (layerPick.isNotEmpty())
            p.layers[0].extra.set ("pick", layerPick);

        auto addHit = [&] (const juce::String& hid, int velLo, int velHi, int chunkIn, int chunkOh, bool unverified)
        {
            pack::Hit h;
            h.id = hid;
            h.layer = "all";
            h.key = key;
            h.velLo = velLo;
            h.velHi = velHi;
            h.files["kick_in"] = fileOf (chunkIn);
            h.files["kick_in"].gainDb = -3.0f;
            h.files["oh"] = fileOf (chunkOh);
            if (unverified)
                h.extra.set ("unverified", true);
            p.hits.push_back (h);
        };
        addHit (pieceId + "_0", 1, 60, a, b, false);
        addHit (pieceId + "_1", 61, 127, c, b, false);
        if (withUnverified)
            addHit (pieceId + "_u", 61, 127, c, c, true);

        m.pieces.push_back (p);
        REQUIRE (w->finish (m, error));
        return t;
    }

    KitLoadOptions optionsFor (const juce::File& dir, const std::map<juce::String, pack::PackKey>& keys)
    {
        KitLoadOptions o;
        o.packDir = dir;
        o.threads = 2;
        o.keyFor = [keys] (const juce::String& id, pack::PackKey& out)
        {
            const auto it = keys.find (id);
            if (it == keys.end())
                return false;
            out = it->second;
            return true;
        };
        return o;
    }

    // Состав кита: по одному месту на пару «пак, барабан»; клавиши без сдвига
    struct PackPiece
    {
        juce::String packId, pieceId;
        int key;
    };

    KitSpec specOf (const std::vector<PackPiece>& items)
    {
        KitSpec s;
        static const char* const ids[] = { "kick", "snare", "crash_a", "crash_b" };
        for (size_t i = 0; i < items.size(); ++i)
            s.places.push_back (testspec::placeFor (ids[i % 4], (int) i, items[i].packId, items[i].pieceId, { items[i].key }));
        return s;
    }
}

TEST_CASE ("loadKit reads layer pick from the manifest: random, cycle, exact", "[loader]")
{
    const auto dir = loaderDir();
    const auto cycle = writePack (dir, "pc", "kick", 36, false, "cycle");
    const auto exact = writePack (dir, "pe", "snare", 38, false, "exact");
    const auto plain = writePack (dir, "pr", "tom", 40, false, "zzz");

    const auto res = loadKit (specOf ({ { "pc", "kick", 36 }, { "pe", "snare", 38 }, { "pr", "tom", 40 } }),
                              optionsFor (dir, { { "pc", cycle.key }, { "pe", exact.key }, { "pr", plain.key } }));
    REQUIRE (res.ok());
    REQUIRE (res.kit->pieces.size() == 3);
    for (const auto& piece : res.kit->pieces)
    {
        REQUIRE (piece.layers.size() == 1);
        if (piece.id == "kick")       CHECK (piece.layers[0].pick == Pick::Cycle);
        else if (piece.id == "snare") CHECK (piece.layers[0].pick == Pick::Exact);
        else                          CHECK (piece.layers[0].pick == Pick::Random);
    }
}

TEST_CASE ("loadKit applies the user pick of a group: the layer follows it and becomes a switch", "[loader]")
{
    const auto dir = loaderDir();
    const auto t = writePack (dir, "pk", "kick", 36);

    auto spec = specOf ({ { "pk", "kick", 36 } });
    spec.places[0].picks["all"] = Pick::Cycle;      // группа на слой "all"

    const auto res = loadKit (spec, optionsFor (dir, { { "pk", t.key } }));
    REQUIRE (res.ok());
    REQUIRE (res.kit->choices.size() == 1);
    CHECK (res.kit->choices[0].id == "kick:all");
    CHECK (res.kit->choices[0].pick == Pick::Cycle);
    CHECK (res.kit->choices[0].pieces == std::vector<int> { 0 });
    CHECK (res.kit->pieces[0].layers[0].pick == Pick::Cycle);
    CHECK (res.kit->pieces[0].layers[0].choice == 0);

    KitEngine engine;
    engine.prepare (44100.0);
    engine.setKit (res.kit.get());
    const int a = engine.noteOn (36, 10).hit;
    const int b = engine.noteOn (36, 10).hit;
    const int c = engine.noteOn (36, 10).hit;
    CHECK (a != b);
    CHECK (a == c);
    engine.setKit (nullptr);
}

TEST_CASE ("loadKit builds a playable kit from a pack", "[loader]")
{
    const auto dir = loaderDir();
    const auto t = writePack (dir, "pk", "kick", 36);

    float lastProgress = 0.0f;
    auto opts = optionsFor (dir, { { "pk", t.key } });
    opts.progress = [&] (float f) { lastProgress = std::max (lastProgress, f); };

    const auto res = loadKit (specOf ({ { "pk", "kick", 36 } }), opts);
    REQUIRE (res.ok());
    CHECK (res.error.isEmpty());
    CHECK (lastProgress == 1.0f);

    const auto& kit = *res.kit;
    REQUIRE (kit.mics.size() == 17);        // kick_in: слот 0; "oh" не из Studio600: первый свободный слот kStudio600MicCount (16)
    CHECK (kit.mics[0].out == 1);
    CHECK (kit.mics[kStudio600MicCount].out == 2);
    CHECK (kit.mics[1].id.empty());

    REQUIRE (kit.pieces.size() == 1);
    REQUIRE (kit.pieces[0].hits.size() == 2);
    const auto& h0 = kit.pieces[0].hits[0];
    REQUIRE (h0.files.size() == 17);
    REQUIRE (h0.files[0].clip != nullptr);
    CHECK (h0.files[0].gainDb == -3.0f);
    CHECK (h0.files[0].clip->frames() == 1000);
    CHECK (h0.files[0].clip->f32[10] == pack::toFloat24 (tone (1000, 100000))[10]);
    CHECK (res.memoryBytes == 3 * 1000 * 4);

    // Блок 1 (oh) общий у двух ударов: декодируется один раз
    CHECK (kit.pieces[0].hits[0].files[kStudio600MicCount].clip == kit.pieces[0].hits[1].files[kStudio600MicCount].clip);

    // Кит играет: удар на двух микрофонах
    KitEngine engine;
    engine.prepare (44100.0);
    engine.setKit (res.kit.get());
    CHECK (engine.noteOn (36, 100).voices == 2);
    CHECK (engine.noteOn (37, 100).voices == 0);
    engine.setKit (nullptr);
}

TEST_CASE ("loadKit skips unverified hits unless asked", "[loader]")
{
    const auto dir = loaderDir();
    const auto t = writePack (dir, "pk", "kick", 36, true);
    const auto opts = optionsFor (dir, { { "pk", t.key } });

    auto spec = specOf ({ { "pk", "kick", 36 } });
    auto res = loadKit (spec, opts);
    REQUIRE (res.ok());
    CHECK (res.kit->pieces[0].hits.size() == 2);

    spec.includeUnverified = true;
    res = loadKit (spec, opts);
    REQUIRE (res.ok());
    REQUIRE (res.kit->pieces[0].hits.size() == 3);
    CHECK (res.kit->pieces[0].hits[2].unverified);
}

TEST_CASE ("loadKit leaves unlicensed packs silent", "[loader]")
{
    const auto dir = loaderDir();
    const auto a = writePack (dir, "pk-a", "kick", 36);
    const auto b = writePack (dir, "pk-b", "snare", 38);

    const auto spec = specOf ({ { "pk-a", "kick", 36 }, { "pk-b", "snare", 38 }, { "pk-missing", "tom", 40 } });
    auto res = loadKit (spec, optionsFor (dir, { { "pk-a", a.key } }));
    REQUIRE (res.ok());
    CHECK (res.kit->pieces.size() == 1);
    REQUIRE (res.missingPacks.size() == 2);
    CHECK (res.missingPacks[0] == "pk-b");
    CHECK (res.missingPacks[1] == "pk-missing");

    // Оба пакета с ключами: микрофоны общие, барабаны складываются
    res = loadKit (spec, optionsFor (dir, { { "pk-a", a.key }, { "pk-b", b.key } }));
    REQUIRE (res.ok());
    CHECK (res.kit->pieces.size() == 2);
    CHECK (res.kit->mics.size() == 17);
    CHECK (res.kit->notes[36].piece == 0);
    CHECK (res.kit->notes[38].piece == 1);
}

TEST_CASE ("loadKit reports errors and cancel", "[loader]")
{
    const auto dir = loaderDir();
    const auto t = writePack (dir, "pk", "kick", 36);
    const auto one = specOf ({ { "pk", "kick", 36 } });

    SECTION ("нет ни одного доступного пакета")
    {
        const auto res = loadKit (one, optionsFor (dir, {}));
        CHECK_FALSE (res.ok());
        CHECK (res.error.isNotEmpty());
    }

    SECTION ("неверный ключ")
    {
        const auto res = loadKit (one, optionsFor (dir, { { "pk", pack::generateKey() } }));
        CHECK_FALSE (res.ok());
        CHECK (res.error.isNotEmpty());
    }

    SECTION ("два барабана на одной клавише")
    {
        const auto b = writePack (dir, "pk-b", "snare", 36);
        const auto res = loadKit (specOf ({ { "pk", "kick", 36 }, { "pk-b", "snare", 36 } }),
                                  optionsFor (dir, { { "pk", t.key }, { "pk-b", b.key } }));
        CHECK_FALSE (res.ok());
        CHECK (res.error.isNotEmpty());
    }

    SECTION ("отмена")
    {
        std::atomic<bool> cancel { true };
        auto opts = optionsFor (dir, { { "pk", t.key } });
        opts.cancel = &cancel;
        const auto res = loadKit (one, opts);
        CHECK_FALSE (res.ok());
        CHECK (res.cancelled);
    }
}

namespace
{
    template <typename Pred>
    bool pumpUntil (Pred done, int timeoutMs = 10000)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (! done() && juce::Time::getMillisecondCounter() < end)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        return done();
    }
}

TEST_CASE ("processor loads a kit in the background", "[loader]")
{
    const auto dir = loaderDir();
    const auto t = writePack (dir, "pk", "kick", 36);

    SamplitProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    bool called = false;
    KitLoadResult got;
    processor.loadKitAsync (specOf ({ { "pk", "kick", 36 } }), optionsFor (dir, { { "pk", t.key } }), [&] (const KitLoadResult& r)
    {
        called = true;
        got = r;
    });
    CHECK (processor.isKitLoading());
    REQUIRE (pumpUntil ([&] { return called; }));

    CHECK (got.ok());
    CHECK_FALSE (processor.isKitLoading());
    CHECK (processor.getKitLoadProgress() == 1.0f);
    REQUIRE (processor.getKit() != nullptr);
    CHECK (processor.getKit()->pieces.size() == 1);
}

TEST_CASE ("a failed or cancelled load keeps the current kit", "[loader]")
{
    const auto dir = loaderDir();
    const auto t = writePack (dir, "pk", "kick", 36);
    const auto spec = specOf ({ { "pk", "kick", 36 } });

    SamplitProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    bool called = false;
    processor.loadKitAsync (spec, optionsFor (dir, { { "pk", t.key } }), [&] (const KitLoadResult&) { called = true; });
    REQUIRE (pumpUntil ([&] { return called; }));
    const auto first = processor.getKit();
    REQUIRE (first != nullptr);

    // Нет лицензии: загрузка не удалась, прежний кит на месте
    called = false;
    bool ok = true;
    processor.loadKitAsync (spec, optionsFor (dir, {}), [&] (const KitLoadResult& r) { called = true; ok = r.ok(); });
    REQUIRE (pumpUntil ([&] { return called; }));
    CHECK_FALSE (ok);
    CHECK (processor.getKit() == first);

    // Отмена: колбэк приходит с cancelled, кит не меняется, флаг загрузки сброшен
    called = false;
    bool cancelled = false;
    processor.loadKitAsync (spec, optionsFor (dir, { { "pk", t.key } }), [&] (const KitLoadResult& r) { called = true; cancelled = r.cancelled; });
    processor.cancelKitLoad();
    CHECK_FALSE (processor.isKitLoading());
    REQUIRE (pumpUntil ([&] { return called; }));
    CHECK (cancelled);
    CHECK (processor.getKit() == first);
}

TEST_CASE ("loadKit plays a local WAV library in full length, 24-bit exact, without pack or license", "[loader]")
{
    const auto dir = loaderDir();
    const auto lib = dir.getChildFile ("lib");
    const auto manifests = dir.getChildFile ("manifests");
    lib.getChildFile ("KICK/in").createDirectory();
    manifests.createDirectory();

    // Длинный файл с тихим хвостом: раньше обрезка по -60 дБ отрезала бы его
    pack::DecodedAudio audio = tone (50000, 4000000);
    for (int i = 20000; i < audio.frames; ++i)
        audio.samples[(size_t) i] = (i % 5) - 2;
    const auto wav = lib.getChildFile ("KICK/in/0-01.wav");
    {
        juce::WavAudioFormat fmt;
        std::unique_ptr<juce::OutputStream> os (wav.createOutputStream());
        REQUIRE (os != nullptr);
        auto writer = fmt.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (44100.0).withNumChannels (1).withBitsPerSample (24));
        REQUIRE (writer != nullptr);
        std::vector<int> shifted ((size_t) audio.frames);
        for (size_t i = 0; i < shifted.size(); ++i)
            shifted[i] = audio.samples[i] * 256;
        const int* ch[1] = { shifted.data() };
        REQUIRE (writer->write (ch, audio.frames));
    }

    pack::Manifest m;
    m.id = "local";
    m.mics = { { "kick_in", "Kick In", "close", 1 } };
    pack::Piece p;
    p.id = "kick";
    p.name = "Kick";
    p.keys = { 36 };
    p.layers = { { "all", "center", 1, 127 } };
    pack::Hit h;
    h.id = "kick_0";
    h.layer = "all";
    h.key = 36;
    h.files["kick_in"].path = "KICK/in/0-01.wav";
    p.hits.push_back (h);
    m.pieces.push_back (p);
    manifests.getChildFile ("local.json").replaceWithText (m.toJson());

    KitLoadOptions opts;
    opts.libraryRoot = lib;
    opts.manifestDir = manifests;
    opts.threads = 2;      // keyFor не задан: лицензия не нужна
    opts.headSeconds = 0;  // файл целиком в памяти (стриминг проверяется отдельным тестом)

    const auto res = loadKit (testspec::oneSpec ("local", "kick", 36), opts);
    REQUIRE (res.ok());
    const auto& clip = *res.kit->pieces[0].hits[0].files[0].clip;
    REQUIRE (clip.frames() == 50000);
    CHECK (clip.f32[10] == (float) audio.samples[10] / 8388608.0f);
    CHECK (clip.f32[49999] == (float) audio.samples[49999] / 8388608.0f);

    // Папки с манифестом нет: пакет считается недоступным, а не падением
    opts.manifestDir = dir.getChildFile ("none");
    CHECK_FALSE (loadKit (testspec::oneSpec ("local", "kick", 36), opts).ok());
}

// ---------------------------------------------------------------- KitSpec (установка Studio600)

namespace
{
    struct SpecLayer
    {
        juce::String id;
        int lo = 1, hi = 127, hitKey = 96;
        juce::String pick;
        bool unverified = false;
        int layerKey = -1;
    };

    pack::Group groupDef (const juce::String& id, std::vector<juce::String> layers, const juce::String& pick = {},
                          std::vector<juce::String> picks = {})
    {
        pack::Group g;
        g.id = id;
        g.name = id;
        g.layers = std::move (layers);
        g.pick = pick;
        g.picks = std::move (picks);
        return g;
    }

    // Барабан типа tom: по одному удару на слой; блоки пака 0..2 используются по кругу
    pack::Piece specPiece (const juce::String& id, std::vector<int> keys, const std::vector<SpecLayer>& layers,
                           std::vector<pack::Group> groups = {})
    {
        pack::Piece p;
        p.id = id;
        p.name = id;
        p.type = "tom";
        p.keys = std::move (keys);
        p.groups = std::move (groups);
        int n = 0;
        for (const auto& l : layers)
        {
            pack::Layer ml { l.id, "center", l.lo, l.hi };
            if (l.pick.isNotEmpty())
                ml.extra.set ("pick", l.pick);
            if (l.unverified)
                ml.extra.set ("unverified", true);
            if (l.layerKey >= 0)
                ml.extra.set ("key", l.layerKey);
            p.layers.push_back (ml);

            pack::Hit h;
            h.id = id + "_" + l.id;
            h.layer = l.id;
            h.key = l.hitKey;
            h.velLo = l.lo;
            h.velHi = l.hi;
            h.files["kick_in"] = fileOf (n % 3);
            h.files["oh"] = fileOf ((n + 1) % 3);
            if (l.unverified)
                h.extra.set ("unverified", true);
            p.hits.push_back (h);
            ++n;
        }
        return p;
    }

    pack::PackKey writeSpecPack (const juce::File& dir, const juce::String& id, const std::vector<pack::Piece>& pieces)
    {
        const auto key = pack::generateKey();
        juce::String error;
        auto w = pack::PackWriter::create (dir.getChildFile (id + ".splpack"), id, key, error);
        REQUIRE (w != nullptr);
        REQUIRE (w->addAudio (tone (1000, 100000), error) == 0);
        REQUIRE (w->addAudio (tone (1000, 200000), error) == 1);
        REQUIRE (w->addAudio (tone (1000, 300000), error) == 2);
        pack::Manifest m;
        m.id = id;
        m.mics = { { "kick_in", "Kick In", "close", 1 }, { "oh", "OH", "overhead", 2 } };
        m.pieces = pieces;
        REQUIRE (w->finish (m, error));
        return key;
    }

    KitSpec::Place placeOf (const char* placeId, int slot, const juce::String& packId, const juce::String& pieceId,
                            std::vector<int> keys)
    {
        KitSpec::Place p;
        p.slot = slot;
        p.placeId = placeId;
        p.packId = packId;
        p.pieceId = pieceId;
        for (int k : keys)
            p.keyMap[k] = k;
        return p;
    }

    std::vector<SpecLayer> threeLayers (int key)
    {
        return { { "s", 1, 40, key }, { "m", 41, 90, key }, { "h", 91, 127, key } };
    }

    const PickChoice* findChoice (const Kit& kit, const std::string& id)
    {
        for (const auto& c : kit.choices)
            if (c.id == id)
                return &c;
        return nullptr;
    }
}

TEST_CASE ("KitSpec: two places from one pack read it once", "[loader][spec]")
{
    const auto dir = loaderDir();
    const auto key = writeSpecPack (dir, "tp1", {
        specPiece ("a", { 96, 97 }, { { "s", 1, 60, 96 }, { "h", 61, 127, 97 }, { "x", 1, 127, 96, {}, true } },
                   { groupDef ("g1", { "s", "h" }), groupDef ("g2", { "x" }) }),
        specPiece ("b", { 98 }, { { "u", 1, 64, 98 }, { "v", 65, 127, 98 } }) });
    const auto opts = optionsFor (dir, { { "tp1", key } });

    KitSpec spec;
    spec.places.push_back (placeOf ("tom_16l", 8, "tp1", "a", { 96, 97 }));
    spec.places.push_back (placeOf ("tom_16m", 9, "tp1", "b", { 98 }));

    auto res = loadKit (spec, opts);
    REQUIRE (res.ok());
    REQUIRE (res.kit->pieces.size() == 2);
    CHECK (res.kit->pieces[0].slot == 8);
    CHECK (res.kit->pieces[1].slot == 9);
    CHECK (res.kit->pieces[0].id == "a");
    CHECK (res.decodedClips == 3);      // три блока пака, прочитаны один раз
    CHECK (res.reusedClips == 0);
    CHECK (res.kit->pieces[0].hits[0].files[0].clip == res.kit->pieces[1].hits[0].files[0].clip);

    // Непроверенный слой x и его группа g2 не попали; у барабана b групп нет: группа на слой, имя «нота диапазон»
    REQUIRE (res.kit->choices.size() == 3);
    const auto* g1 = findChoice (*res.kit, "tom_16l:g1");
    REQUIRE (g1 != nullptr);
    CHECK (g1->pieces == std::vector<int> { 0 });
    CHECK (res.kit->pieces[0].layers.size() == 2);
    CHECK (res.kit->pieces[0].layers[0].choice == 0);
    const auto* u = findChoice (*res.kit, "tom_16m:u");
    REQUIRE (u != nullptr);
    CHECK (juce::String::fromUTF8 (u->name.c_str()) == "D6 1–64"_s);
    CHECK (findChoice (*res.kit, "tom_16m:v") != nullptr);
    CHECK (res.kit->pieces[1].layers[0].choice == 1);

    spec.includeUnverified = true;
    res = loadKit (spec, opts);
    REQUIRE (res.ok());
    CHECK (res.kit->choices.size() == 4);
    CHECK (res.kit->pieces[0].hits.size() == 3);
    CHECK (findChoice (*res.kit, "tom_16l:g2") != nullptr);
}

TEST_CASE ("KitSpec: key shift reaches notes, dropped keys warn, disabled place is silent", "[loader][spec]")
{
    const auto dir = loaderDir();
    const auto key = writeSpecPack (dir, "tp1", { specPiece ("a", { 96, 97 }, { { "s", 1, 60, 96 }, { "h", 61, 127, 97, {}, false, 97 } }) });
    const auto opts = optionsFor (dir, { { "tp1", key } });

    // Место tom_16m (98-99): клавиши барабана 96,97 уезжают на 98,99
    KitSpec spec;
    KitSpec::Place pl;
    pl.slot = 9;
    pl.placeId = "tom_16m";
    pl.packId = "tp1";
    pl.pieceId = "a";
    const auto mapping = mapKeysToPlace ({ 96, 97 }, "tom", *findPlace ("tom_16m"));
    pl.keyMap = mapping.keys;
    spec.places.push_back (pl);

    auto res = loadKit (spec, opts);
    REQUIRE (res.ok());
    CHECK (res.kit->pieces[0].keys == std::vector<int> { 98, 99 });
    CHECK (res.kit->pieces[0].hits[1].key == 99);
    CHECK (res.kit->pieces[0].layers[1].key == 99);     // клавиша слоя тоже перенесена
    CHECK (res.kit->notes[98].piece == 0);
    CHECK (res.kit->notes[99].piece == 0);
    CHECK (res.kit->notes[96].piece < 0);               // тут теперь место tom_16l, которого в spec нет: молчит
    CHECK (res.kit->notes[97].piece < 0);

    // Клавиши нет в переносе: удар и слой отбрасываются с предупреждением, кит собирается
    spec.places[0].keyMap = { { 96, 98 } };
    res = loadKit (spec, opts);
    REQUIRE (res.ok());
    CHECK (res.kit->pieces[0].hits.size() == 1);
    CHECK (res.kit->pieces[0].layers.size() == 1);
    CHECK_FALSE (res.warnings.empty());
}

TEST_CASE ("KitSpec: missing pack silences its place, the rest plays", "[loader][spec]")
{
    const auto dir = loaderDir();
    const auto key = writeSpecPack (dir, "tp1", { specPiece ("a", { 96 }, { { "s", 1, 127, 96 } }) });
    const auto opts = optionsFor (dir, { { "tp1", key } });

    KitSpec spec;
    spec.places.push_back (placeOf ("tom_16l", 8, "tp1", "a", { 96 }));
    spec.places.push_back (placeOf ("tom_16m", 9, "nopack", "z", { 98 }));
    auto res = loadKit (spec, opts);
    REQUIRE (res.ok());
    REQUIRE (res.kit->pieces.size() == 1);
    CHECK (res.kit->notes[96].piece == 0);
    CHECK (res.kit->notes[98].piece < 0);
    REQUIRE (res.missingPacks.size() == 1);
    CHECK (res.missingPacks[0] == "nopack");

    // Ни одного доступного пака: ошибка, как у пресета
    KitSpec none;
    none.places.push_back (placeOf ("tom_16m", 9, "nopack", "z", { 98 }));
    CHECK_FALSE (loadKit (none, opts).ok());
}

TEST_CASE ("KitSpec: reuse takes clips from the current kit and decodes only what changed", "[loader][spec]")
{
    const auto dir = loaderDir();
    const auto k1 = writeSpecPack (dir, "tp1", { specPiece ("a", { 96 }, threeLayers (96)) });
    const auto k2 = writeSpecPack (dir, "tp2", { specPiece ("c", { 98 }, threeLayers (98)) });
    const auto k3 = writeSpecPack (dir, "tp3", { specPiece ("d", { 98 }, threeLayers (98)) });
    const auto opts = optionsFor (dir, { { "tp1", k1 }, { "tp2", k2 }, { "tp3", k3 } });

    KitSpec spec;
    spec.places.push_back (placeOf ("tom_16l", 8, "tp1", "a", { 96 }));
    spec.places.push_back (placeOf ("tom_16m", 9, "tp2", "c", { 98 }));

    const auto first = loadKit (spec, opts);
    REQUIRE (first.ok());
    CHECK (first.decodedClips == 6);

    auto o = opts;
    o.reuse = first.kit;
    const auto second = loadKit (spec, o);
    REQUIRE (second.ok());
    CHECK (second.decodedClips == 0);
    CHECK (second.reusedClips == 6);
    CHECK (second.kit.get() != first.kit.get());
    CHECK (second.kit->serial.value != first.kit->serial.value);
    CHECK (second.kit->pieces[1].hits[2].files[1].clip == first.kit->pieces[1].hits[2].files[1].clip);
    CHECK (second.memoryBytes == first.memoryBytes);

    // Другой пак на втором месте: декодируется только он
    spec.places[1] = placeOf ("tom_16m", 9, "tp3", "d", { 98 });
    o.reuse = second.kit;
    const auto third = loadKit (spec, o);
    REQUIRE (third.ok());
    CHECK (third.decodedClips == 3);
    CHECK (third.reusedClips == 3);
    CHECK (third.kit->pieces[0].hits[0].files[0].clip == second.kit->pieces[0].hits[0].files[0].clip);
    CHECK (third.kit->pieces[1].hits[0].files[0].clip != second.kit->pieces[1].hits[0].files[0].clip);
}

TEST_CASE ("KitSpec: two kits of one process share decoded clips (second plugin instance)", "[loader][spec]")
{
    const auto dir = loaderDir();
    const auto k1 = writeSpecPack (dir, "tsh1", { specPiece ("a", { 96 }, threeLayers (96)) });
    const auto opts = optionsFor (dir, { { "tsh1", k1 } });

    KitSpec spec;
    spec.places.push_back (placeOf ("tom_16l", 8, "tsh1", "a", { 96 }));

    const auto first = loadKit (spec, opts);
    REQUIRE (first.ok());
    CHECK (first.decodedClips == 3);

    // Без reuse (другой экземпляр плагина): клипы берутся из общего реестра процесса, копий в памяти нет
    const auto second = loadKit (spec, opts);
    REQUIRE (second.ok());
    CHECK (second.decodedClips == 0);
    CHECK (second.reusedClips == 3);
    CHECK (second.kit->pieces[0].hits[0].files[0].clip == first.kit->pieces[0].hits[0].files[0].clip);
}

TEST_CASE ("KitSpec: pick priority is user, group, layer, random; a pick outside picks is ignored", "[loader][spec]")
{
    const auto dir = loaderDir();
    const auto key = writeSpecPack (dir, "tp1", { specPiece ("pr", { 96, 97, 98, 99, 100 }, {
            { "lu", 1, 127, 96, "exact" },
            { "lg", 1, 127, 97, "random" },
            { "ll", 1, 127, 98, "exact" },
            { "ld", 1, 127, 99 },
            { "lx", 1, 127, 100 } },
        { groupDef ("gU", { "lu" }, "random"),
          groupDef ("gG", { "lg" }, "exact"),
          groupDef ("gL", { "ll" }),
          groupDef ("gD", { "ld" }),
          groupDef ("gX", { "lx" }, "cycle", { "random", "cycle" }) }) });
    const auto opts = optionsFor (dir, { { "tp1", key } });

    KitSpec spec;
    spec.places.push_back (placeOf ("tom_14", 10, "tp1", "pr", { 96, 97, 98, 99, 100 }));
    spec.places[0].picks["gU"] = Pick::Cycle;
    spec.places[0].picks["gX"] = Pick::Exact;       // gX не разрешает exact: берётся Group::pick

    const auto res = loadKit (spec, opts);
    REQUIRE (res.ok());
    const auto& layers = res.kit->pieces[0].layers;
    CHECK (layers[0].pick == Pick::Cycle);      // выбор пользователя
    CHECK (layers[1].pick == Pick::Exact);      // Group::pick поверх pick слоя
    CHECK (layers[2].pick == Pick::Exact);      // pick слоя
    CHECK (layers[3].pick == Pick::Random);     // умолчание
    CHECK (layers[4].pick == Pick::Cycle);      // exact вне picks игнорируется
    CHECK (findChoice (*res.kit, "tom_14:gU")->pick == Pick::Cycle);
    CHECK (findChoice (*res.kit, "tom_14:gG")->pick == Pick::Exact);
    CHECK (findChoice (*res.kit, "tom_14:gX")->pick == Pick::Cycle);
    CHECK (res.kit->notes[96].layers[0].pick == Pick::Cycle);
}

TEST_CASE ("KitSpec: choices over kMaxChoices are skipped with a warning", "[loader][spec]")
{
    const auto dir = loaderDir();
    std::vector<SpecLayer> layers;
    for (int i = 0; i < Kit::kMaxChoices + 3; ++i)
        layers.push_back ({ "l" + juce::String (i), 1, 127, 96 });
    const auto key = writeSpecPack (dir, "tp1", { specPiece ("a", { 96 }, layers) });
    KitSpec spec;
    spec.places.push_back (placeOf ("tom_16l", 8, "tp1", "a", { 96 }));
    const auto res = loadKit (spec, optionsFor (dir, { { "tp1", key } }));
    REQUIRE (res.ok());
    CHECK ((int) res.kit->choices.size() == Kit::kMaxChoices);
    CHECK (res.kit->pieces[0].layers.back().choice == -1);
    CHECK_FALSE (res.warnings.empty());
}

TEST_CASE ("makeKitSpec builds the spec from Setup and the catalog", "[loader][spec]")
{
    auto drum = [] (const char* id, const char* type, std::vector<int> keys)
    {
        CatalogPiece p;
        p.id = id;
        p.type = type;
        p.name = id;
        p.keys = std::move (keys);
        return p;
    };
    std::vector<CatalogPack> catalog (3);
    catalog[0].packId = "studio600-kick-rode";
    catalog[0].pieces = { drum ("kick", "kick", { 24 }) };
    catalog[1].packId = "studio600-toms";
    catalog[1].pieces = { drum ("tom_16l", "tom", { 96, 97 }), drum ("tom_16m", "tom", { 96, 97 }) };
    catalog[2].packId = "studio600-snare-live";
    catalog[2].pieces = { drum ("snare_a", "snare", { 25, 26 }), drum ("snare_b", "snare", { 25 }) };

    auto setup = Setup::factory();
    for (auto& p : setup.places)
    {
        if (p.placeId == "hat")
            p.enabled = false;
        if (p.placeId == "snare")
            p.pieceId.clear();          // единственный барабан типа; тут их два
    }

    std::vector<juce::String> warnings;
    const auto spec = makeKitSpec (setup, catalog, true, warnings);
    CHECK (spec.includeUnverified);
    CHECK (spec.places.size() == 11);   // hat выключен
    for (const auto& p : spec.places)
        CHECK (p.placeId != "hat");

    auto find = [&] (const char* id) -> const KitSpec::Place&
    {
        for (const auto& p : spec.places)
            if (p.placeId == id)
                return p;
        FAIL ("no place " << id);
        return spec.places[0];
    };
    CHECK (find ("kick").keyMap == std::map<int, int> { { 24, 24 } });
    CHECK (find ("tom_16l").keyMap == std::map<int, int> { { 96, 96 }, { 97, 97 } });
    CHECK (find ("tom_16m").keyMap == std::map<int, int> { { 96, 98 }, { 97, 99 } });
    CHECK (find ("tom_16m").slot == 9);
    CHECK (find ("snare").pieceId == "snare_a");
    CHECK_FALSE (warnings.empty());             // два барабана типа snare
    // Пака нет в каталоге: место осталось, выбор не стёрт, клавиш нет
    CHECK (find ("crash_a").packId == "studio600-crash18");
    CHECK (find ("crash_a").keyMap.empty());
}

TEST_CASE ("loadKit: mic index is a fixed slot, independent of pack order and of disabled places", "[loader][mics]")
{
    const auto dir = loaderDir();
    // Паки перечисляют микрофоны в разном порядке и добавляют незнакомые id
    const auto a = writePack (dir, "pk-a", "kick", 36, false, {}, false, { "x1" });
    const auto b = writePack (dir, "pk-b", "snare", 38, false, {}, true, { "x2" });
    const auto keys = std::map<juce::String, pack::PackKey> { { "pk-a", a.key }, { "pk-b", b.key } };

    const auto both = loadKit (specOf ({ { "pk-a", "kick", 36 }, { "pk-b", "snare", 38 } }), optionsFor (dir, keys));
    const auto onlyB = loadKit (specOf ({ { "pk-b", "snare", 38 } }), optionsFor (dir, keys));
    const auto onlyA = loadKit (specOf ({ { "pk-a", "kick", 36 } }), optionsFor (dir, keys));
    REQUIRE (both.ok());
    REQUIRE (onlyB.ok());
    REQUIRE (onlyA.ok());

    // Известный id (Studio600) всегда в своём слоте
    for (const auto* r : { &both, &onlyB, &onlyA })
    {
        REQUIRE (r->kit->mics.size() >= 17);
        CHECK (r->kit->mics[0].id == "kick_in");
        CHECK (r->kit->mics[1].id.empty());     // слот без микрофона: заглушка
    }

    // Незнакомый id получает свободный слот после слотов Studio600 и в этом ките не пересекается с другими
    const auto& m = both.kit->mics;
    REQUIRE (m.size() == 19);
    CHECK (m[kStudio600MicCount].id == "oh");
    CHECK (m[kStudio600MicCount + 1].id == "x1");
    CHECK (m[kStudio600MicCount + 2].id == "x2");
    CHECK (both.kit->pieces[0].hits[0].files.size() == m.size());
    CHECK (both.kit->pieces[0].hits[0].files[0].clip != nullptr);
}

TEST_CASE ("loadKit: more unknown mics than spare slots is an error", "[loader][mics]")
{
    const auto dir = loaderDir();
    std::vector<juce::String> extra;
    for (int i = 0; i < Kit::kMaxMics; ++i)
        extra.push_back ("u" + juce::String (i));
    const auto a = writePack (dir, "pk-a", "kick", 36, false, {}, false, extra);
    const auto res = loadKit (specOf ({ { "pk-a", "kick", 36 } }), optionsFor (dir, { { "pk-a", a.key } }));
    CHECK_FALSE (res.ok());
}

TEST_CASE ("sub kick occupies the 16th Studio600 slot after the old 15, out 1", "[studio600]")
{
    using namespace samplit;
    REQUIRE (kStudio600MicCount == 16);
    CHECK (studio600MicSlot ("room_2") == 14);      // старые слоты не сдвинулись (идентификаторы параметров привязаны к id)
    CHECK (studio600MicSlot ("sub_kick") == 15);
    CHECK (kStudio600Mics[15].defaultOut == 1);
    CHECK (std::string (studio600MicName ("sub_kick")) == "Sub Kick");
}
