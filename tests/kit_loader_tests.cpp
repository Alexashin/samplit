#include "KitEngine.h"
#include "KitLoader.h"
#include "PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

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
                        bool withUnverified = false)
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

        pack::Piece p;
        p.id = pieceId;
        p.name = pieceId;
        p.keys = { key };
        p.layers = { { "all", "center", 1, 127 } };

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

    Preset presetOf (std::vector<juce::String> packs)
    {
        Preset p;
        p.id = "test";
        p.name = "Test";
        p.packs = std::move (packs);
        return p;
    }
}

TEST_CASE ("Preset parses the default preset file", "[loader]")
{
    Preset p;
    juce::String error;
    const auto file = juce::File (SAMPLIT_TEST_FIXTURES).getParentDirectory().getParentDirectory()
                          .getChildFile ("tools/presets/studio600-default.json");
    REQUIRE (Preset::fromFile (file, p, error));
    CHECK (p.id == "studio600-default");
    CHECK (p.packs.size() == 5);
    CHECK (p.mode == HitSelect::Live);
    CHECK (p.layerSpread == 1);
    CHECK (p.velocityToGain == 0.0f);
    REQUIRE (p.mics.count ("oh_1") == 1);
    CHECK (p.mics["oh_1"].out == 5);
    CHECK (p.mics["oh_1"].pan == -0.8f);
    CHECK (! p.includeUnverified);
}

TEST_CASE ("Preset rejects broken json", "[loader]")
{
    Preset p;
    juce::String error;
    CHECK_FALSE (Preset::fromJson ("[]", p, error));
    CHECK_FALSE (Preset::fromJson ("{\"packs\":[]}", p, error));
    CHECK_FALSE (Preset::fromJson ("{\"id\":\"x\",\"select\":{\"mode\":\"zzz\"}}", p, error));
    CHECK (Preset::fromJson ("{\"id\":\"x\"}", p, error));
}

TEST_CASE ("loadKit builds a playable kit from a pack", "[loader]")
{
    const auto dir = loaderDir();
    const auto t = writePack (dir, "pk", "kick", 36);

    auto preset = presetOf ({ "pk" });
    preset.mics["oh"].out = 7;
    preset.mics["oh"].levelDb = -6.0f;
    preset.mics["oh"].pan = 0.5f;

    float lastProgress = 0.0f;
    auto opts = optionsFor (dir, { { "pk", t.key } });
    opts.progress = [&] (float f) { lastProgress = std::max (lastProgress, f); };

    const auto res = loadKit (preset, opts);
    REQUIRE (res.ok());
    CHECK (res.error.isEmpty());
    CHECK (lastProgress == 1.0f);

    const auto& kit = *res.kit;
    REQUIRE (kit.mics.size() == 2);
    CHECK (kit.mics[0].out == 1);
    CHECK (kit.mics[1].out == 7);
    REQUIRE (res.mix.size() == 2);
    CHECK (res.mix[1].levelDb == -6.0f);
    CHECK (res.mix[1].pan == 0.5f);

    REQUIRE (kit.pieces.size() == 1);
    REQUIRE (kit.pieces[0].hits.size() == 2);
    const auto& h0 = kit.pieces[0].hits[0];
    REQUIRE (h0.files.size() == 2);
    REQUIRE (h0.files[0].clip != nullptr);
    CHECK (h0.files[0].gainDb == -3.0f);
    CHECK (h0.files[0].clip->frames() == 1000);
    CHECK (h0.files[0].clip->isInt16());
    CHECK (h0.files[0].clip->s16[10] == pack::toInt16 (tone (1000, 100000))[10]);
    CHECK (res.memoryBytes == 3 * 1000 * 2);

    // Блок 1 (oh) общий у двух ударов: декодируется один раз
    CHECK (kit.pieces[0].hits[0].files[1].clip == kit.pieces[0].hits[1].files[1].clip);

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

    auto preset = presetOf ({ "pk" });
    auto res = loadKit (preset, opts);
    REQUIRE (res.ok());
    CHECK (res.kit->pieces[0].hits.size() == 2);

    preset.includeUnverified = true;
    res = loadKit (preset, opts);
    REQUIRE (res.ok());
    REQUIRE (res.kit->pieces[0].hits.size() == 3);
    CHECK (res.kit->pieces[0].hits[2].unverified);
}

TEST_CASE ("loadKit leaves unlicensed packs silent", "[loader]")
{
    const auto dir = loaderDir();
    const auto a = writePack (dir, "pk-a", "kick", 36);
    const auto b = writePack (dir, "pk-b", "snare", 38);

    auto preset = presetOf ({ "pk-a", "pk-b", "pk-missing" });
    auto res = loadKit (preset, optionsFor (dir, { { "pk-a", a.key } }));
    REQUIRE (res.ok());
    CHECK (res.kit->pieces.size() == 1);
    REQUIRE (res.missingPacks.size() == 2);
    CHECK (res.missingPacks[0] == "pk-b");
    CHECK (res.missingPacks[1] == "pk-missing");

    // Оба пакета с ключами: микрофоны общие, барабаны складываются
    res = loadKit (preset, optionsFor (dir, { { "pk-a", a.key }, { "pk-b", b.key } }));
    REQUIRE (res.ok());
    CHECK (res.kit->pieces.size() == 2);
    CHECK (res.kit->mics.size() == 2);
    CHECK (res.kit->notes[36].piece == 0);
    CHECK (res.kit->notes[38].piece == 1);

    // Фильтр барабанов пресета
    preset.pieces = { "snare" };
    res = loadKit (preset, optionsFor (dir, { { "pk-a", a.key }, { "pk-b", b.key } }));
    REQUIRE (res.ok());
    REQUIRE (res.kit->pieces.size() == 1);
    CHECK (res.kit->pieces[0].id == "snare");
}

TEST_CASE ("loadKit reports errors and cancel", "[loader]")
{
    const auto dir = loaderDir();
    const auto t = writePack (dir, "pk", "kick", 36);

    SECTION ("нет ни одного доступного пакета")
    {
        const auto res = loadKit (presetOf ({ "pk" }), optionsFor (dir, {}));
        CHECK_FALSE (res.ok());
        CHECK (res.error.isNotEmpty());
    }

    SECTION ("неверный ключ")
    {
        const auto res = loadKit (presetOf ({ "pk" }), optionsFor (dir, { { "pk", pack::generateKey() } }));
        CHECK_FALSE (res.ok());
        CHECK (res.error.isNotEmpty());
    }

    SECTION ("два барабана на одной клавише")
    {
        const auto b = writePack (dir, "pk-b", "snare", 36);
        const auto res = loadKit (presetOf ({ "pk", "pk-b" }), optionsFor (dir, { { "pk", t.key }, { "pk-b", b.key } }));
        CHECK_FALSE (res.ok());
        CHECK (res.error.isNotEmpty());
    }

    SECTION ("отмена")
    {
        std::atomic<bool> cancel { true };
        auto opts = optionsFor (dir, { { "pk", t.key } });
        opts.cancel = &cancel;
        const auto res = loadKit (presetOf ({ "pk" }), opts);
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

TEST_CASE ("processor loads a kit in the background and applies the mic mix", "[loader]")
{
    const auto dir = loaderDir();
    const auto t = writePack (dir, "pk", "kick", 36);
    auto preset = presetOf ({ "pk" });
    preset.mics["oh"].mute = true;

    SamplitProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    bool called = false;
    KitLoadResult got;
    processor.loadKitAsync (preset, optionsFor (dir, { { "pk", t.key } }), [&] (const KitLoadResult& r)
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

    SamplitProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    bool called = false;
    processor.loadKitAsync (presetOf ({ "pk" }), optionsFor (dir, { { "pk", t.key } }), [&] (const KitLoadResult&) { called = true; });
    REQUIRE (pumpUntil ([&] { return called; }));
    const auto first = processor.getKit();
    REQUIRE (first != nullptr);

    // Нет лицензии: загрузка не удалась, прежний кит на месте
    called = false;
    bool ok = true;
    processor.loadKitAsync (presetOf ({ "pk" }), optionsFor (dir, {}), [&] (const KitLoadResult& r) { called = true; ok = r.ok(); });
    REQUIRE (pumpUntil ([&] { return called; }));
    CHECK_FALSE (ok);
    CHECK (processor.getKit() == first);

    // Отмена: колбэк не приходит, флаг загрузки сброшен
    called = false;
    processor.loadKitAsync (presetOf ({ "pk" }), optionsFor (dir, { { "pk", t.key } }), [&] (const KitLoadResult&) { called = true; });
    processor.cancelKitLoad();
    CHECK_FALSE (processor.isKitLoading());
    pumpUntil ([&] { return called; }, 300);
    CHECK_FALSE (called);
    CHECK (processor.getKit() == first);
}
