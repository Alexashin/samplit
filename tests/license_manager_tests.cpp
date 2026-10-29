#include "KitLoader.h"
#include "LicenseManager.h"
#include "PluginProcessor.h"

#include "samplit/core/LicenseSigning.h"
#include "kit_fixture.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <thread>

using namespace samplit;
using samplit::license::Status;
using samplit::pack::operator""_s;

namespace
{
    constexpr int64_t kNow = 1'800'000'000;
    constexpr int64_t kDay = 86400;
    const juce::String kFingerprint = "win:manager-test:0001";

    // Тот же тестовый сид, что в license_tests.cpp и в LicenseKey.cpp
    license::Key32 testSeed()
    {
        license::Key32 s {};
        for (size_t i = 0; i < s.size(); ++i)
            s[i] = (uint8_t) i;
        return s;
    }

    struct Env
    {
        Env()
        {
            dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("samplit-tests").getChildFile ("licmgr");
            dir.deleteRecursively();
            dir.createDirectory();
            secret = license::secretKeyFromSeed (testSeed(), pub);
        }

        // Выпускает токен на пакеты {id → ключ}
        void writeToken (const std::map<juce::String, pack::PackKey>& packs, int64_t expires = kNow + 7 * kDay)
        {
            auto* p = new juce::DynamicObject();
            auto* pk = new juce::DynamicObject();
            std::array<uint8_t, 24> nonce {};
            nonce.fill (7);
            for (const auto& [id, key] : packs)
                pk->setProperty (id, license::wrapPackKey (id, key, kFingerprint, nonce));
            p->setProperty ("licenseId", "L-1");
            p->setProperty ("user", "tester");
            p->setProperty ("machine", license::machineId (kFingerprint));
            p->setProperty ("notBefore", (juce::int64) (kNow - kDay));
            p->setProperty ("expires", (juce::int64) expires);
            p->setProperty ("packs", juce::var (pk));
            dir.getChildFile ("license.token").replaceWithText (license::signToken (juce::var (p), secret));
        }

        LicenseManager::Config config() const
        {
            LicenseManager::Config c;
            c.dataDir = dir;
            c.publicKey = pub;
            c.fingerprint = kFingerprint;
            c.now = [this] { return clockNow; };
            c.enforce = true;       // эти тесты проверяют код лицензии, она в них включена независимо от сборки
            return c;
        }

        juce::File dir;
        license::Key32 pub {};
        std::array<uint8_t, 64> secret {};
        int64_t clockNow = kNow;
    };

    bool pumpUntil (const std::function<bool()>& done, int timeoutMs = 10000)
    {
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (! done() && juce::Time::getMillisecondCounter() < end)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        return done();
    }

    // Менеджер после первой проверки токена
    std::unique_ptr<LicenseManager> started (const Env& env)
    {
        auto m = std::make_unique<LicenseManager> (env.config());
        REQUIRE (pumpUntil ([&] { return ! m->getInfo().busy; }));
        return m;
    }
}

TEST_CASE ("license manager without a token has no keys", "[licmgr]")
{
    Env env;
    auto m = started (env);
    CHECK (m->getInfo().status == Status::NoToken);
    CHECK_FALSE (m->getInfo().isValid());
    pack::PackKey key;
    CHECK_FALSE (m->keyFor ("kick", key));
}

TEST_CASE ("license manager gives keys from a valid token", "[licmgr]")
{
    Env env;
    const auto key = pack::generateKey();
    env.writeToken ({ { "kick", key } });

    auto m = started (env);
    const auto info = m->getInfo();
    CHECK (info.isValid());
    CHECK (info.user == "tester");
    CHECK (info.expires == kNow + 7 * kDay);

    pack::PackKey got;
    REQUIRE (m->keyFor ("kick", got));
    CHECK (got == key);
    CHECK_FALSE (m->keyFor ("cymbals", got));
}

TEST_CASE ("an expired token gives no keys and a refresh picks up a new one", "[licmgr]")
{
    Env env;
    const auto key = pack::generateKey();
    env.writeToken ({ { "kick", key } }, kNow + kDay);
    env.clockNow = kNow + 2 * kDay;

    auto m = started (env);
    CHECK (m->getInfo().status == Status::Expired);
    pack::PackKey got;
    CHECK_FALSE (m->keyFor ("kick", got));

    // Токен заменили файлом (как после продления): повторная проверка его видит
    env.writeToken ({ { "kick", key } }, kNow + 9 * kDay);
    int changes = 0;
    m->onChange = [&] { ++changes; };
    m->check();
    REQUIRE (pumpUntil ([&] { return changes > 0; }));
    CHECK (m->getInfo().isValid());
    CHECK (m->keyFor ("kick", got));
}

TEST_CASE ("rolling the clock back closes the packs", "[licmgr]")
{
    Env env;
    env.writeToken ({ { "kick", pack::generateKey() } });
    auto m = started (env);
    REQUIRE (m->getInfo().isValid());

    env.clockNow = kNow - 3 * kDay;
    m->check();
    REQUIRE (pumpUntil ([&] { return m->getInfo().status == Status::ClockRollback; }));
    pack::PackKey got;
    CHECK_FALSE (m->keyFor ("kick", got));
}

TEST_CASE ("activation without a server address reports an error", "[licmgr]")
{
    Env env;
    auto m = started (env);
    m->activate ("CODE");
    REQUIRE (pumpUntil ([&] { return ! m->getInfo().busy && m->getInfo().lastError.isNotEmpty(); }));
    CHECK_FALSE (m->getInfo().isValid());
}

TEST_CASE ("the processor opens packs only with a licence", "[licmgr]")
{
    Env env;
    const auto packKey = pack::generateKey();

    // Пакет с одним ударом
    auto packDir = env.dir.getChildFile ("packs");
    packDir.createDirectory();
    {
        juce::String error;
        auto w = pack::PackWriter::create (packDir.getChildFile ("kick.splpack"), "kick", packKey, error);
        REQUIRE (w != nullptr);
        pack::DecodedAudio a;
        a.sampleRate = 44100.0;
        a.frames = 500;
        a.samples.assign (500, 5000);
        REQUIRE (w->addAudio (a, error) == 0);

        pack::Manifest m;
        m.id = "kick";
        m.mics = { { "kick_in", "Kick In", "close", 1 } };
        pack::Piece p;
        p.id = "kick";
        p.keys = { 36 };
        p.layers = { { "all", "center", 1, 127 } };
        pack::Hit h;
        h.id = "k0";
        h.layer = "all";
        h.key = 36;
        h.velLo = 1;
        h.velHi = 127;
        pack::HitFile f;
        f.chunk = 0;
        f.frames = 500;
        f.rate = 44100.0;
        f.peak = 5000;
        h.files["kick_in"] = f;
        p.hits.push_back (h);
        m.pieces.push_back (p);
        REQUIRE (w->finish (m, error));
    }

    Preset preset;
    preset.id = "t";
    preset.packs = { "kick" };

    SamplitProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto runPreset = [&]
    {
        bool called = false;
        bool ok = false;
        processor.loadPreset (preset, [&] (const KitLoadResult& r) { called = true; ok = r.ok(); });
        REQUIRE (pumpUntil ([&] { return called; }));
        return ok;
    };

    SECTION ("без токена кит не собирается")
    {
        processor.setLicenseManager (started (env));
        CHECK_FALSE (runPreset());
        CHECK (processor.getKit() == nullptr);
    }

    SECTION ("с токеном собирается")
    {
        env.writeToken ({ { "kick", packKey } });
        processor.setLicenseManager (started (env));
        CHECK (runPreset());
        REQUIRE (processor.getKit() != nullptr);
        CHECK (processor.getKit()->pieces.size() == 1);
    }

    SECTION ("токен на другой ключ: пакет не открывается")
    {
        env.writeToken ({ { "kick", pack::generateKey() } });
        processor.setLicenseManager (started (env));
        CHECK_FALSE (runPreset());
    }
}

namespace
{
    // Пакет с одним барабаном на клавише key и двумя микрофонами
    void writeKickPack (const juce::File& packDir, const juce::String& id, const pack::PackKey& key, int note)
    {
        packDir.createDirectory();
        juce::String error;
        auto w = pack::PackWriter::create (packDir.getChildFile (id + ".splpack"), id, key, error);
        REQUIRE (w != nullptr);
        pack::DecodedAudio a;
        a.sampleRate = 44100.0;
        a.frames = 500;
        a.samples.assign (500, 5000);
        REQUIRE (w->addAudio (a, error) == 0);

        pack::Manifest m;
        m.id = id;
        m.mics = { { "kick_in", "Kick In", "close", 1 }, { "oh", "OH", "overhead", 2 } };
        pack::Piece p;
        p.id = id;
        p.keys = { note };
        p.layers = { { "all", "center", 1, 127 } };
        pack::Hit h;
        h.id = id + "0";
        h.layer = "all";
        h.key = note;
        pack::HitFile f;
        f.chunk = 0;
        f.frames = 500;
        f.rate = 44100.0;
        f.peak = 5000;
        h.files["kick_in"] = f;
        h.files["oh"] = f;
        p.hits.push_back (h);
        m.pieces.push_back (p);
        REQUIRE (w->finish (m, error));
    }

    void writePreset (const juce::File& dir, const juce::String& json)
    {
        auto presets = dir.getChildFile ("presets");
        presets.createDirectory();
        const auto id = juce::JSON::parse (json).getProperty ("id", {}).toString();
        presets.getChildFile (id + ".json").replaceWithText (json);
    }

    const char* kPresetJson = R"({ "format": 1, "id": "p1", "name": "Preset 1", "packs": ["kick", "snare", "cym"],
        "select": { "mode": "live" },
        "mics": { "oh": { "level": -4.0, "pan": 0.5, "out": 6 } } })";
}

TEST_CASE ("presets show which packs are installed and licensed", "[licmgr]")
{
    Env env;
    const auto kickKey = pack::generateKey();
    writeKickPack (env.dir.getChildFile ("packs"), "kick", kickKey, 36);
    writeKickPack (env.dir.getChildFile ("packs"), "snare", pack::generateKey(), 38);
    writePreset (env.dir, kPresetJson);
    writePreset (env.dir, R"({ "id": "p2", "name": "Only cymbals", "packs": ["cym"] })");
    env.dir.getChildFile ("presets").getChildFile ("broken.json").replaceWithText ("{ nope");
    env.writeToken ({ { "kick", kickKey } });

    SamplitProcessor processor;
    processor.setLicenseManager (started (env));
    const auto presets = processor.scanPresets();
    REQUIRE (presets.size() == 2);

    const auto& p1 = presets[0];
    CHECK (p1.preset.id == "p1");
    REQUIRE (p1.packs.size() == 3);
    CHECK (p1.packs[0].second == PackState::Ready);
    CHECK (p1.packs[1].second == PackState::NoLicense);
    CHECK (p1.packs[2].second == PackState::NotInstalled);
    CHECK (p1.isPlayable());
    CHECK_FALSE (p1.isComplete());
    CHECK_FALSE (presets[1].isPlayable());
}

TEST_CASE ("mixer: preset values, solo and mute, saved with the project", "[licmgr]")
{
    Env env;
    const auto kickKey = pack::generateKey();
    writeKickPack (env.dir.getChildFile ("packs"), "kick", kickKey, 36);
    writePreset (env.dir, kPresetJson);
    env.writeToken ({ { "kick", kickKey } });

    juce::MemoryBlock saved;
    {
        SamplitProcessor a;
        a.prepareToPlay (44100.0, 512);
        a.setLicenseManager (started (env));
        const auto presets = a.scanPresets();
        REQUIRE (presets.size() == 1);

        bool called = false;
        a.selectPreset (presets[0].preset, [&] (const KitLoadResult&) { called = true; });
        REQUIRE (pumpUntil ([&] { return called; }));
        REQUIRE (a.getKit() != nullptr);
        CHECK (a.getPresetName() == "p1");
        CHECK (a.getLastKitLoad().missingPacks.size() == 2);

        // Значения пресета: oh на Out 6, −4 дБ, панорама 0,5; kick_in как в пакете
        CHECK (a.getMicState (0).out == 1);
        CHECK (a.getMicState (1).out == 6);
        CHECK (a.getMicState (1).levelDb == -4.0f);
        CHECK (a.getMicState (1).pan == 0.5f);

        auto m = a.getMicState (0);
        m.solo = true;
        m.out = 3;
        a.setMicState (0, m);
        m = a.getMicState (1);
        m.levelDb = -10.0f;
        m.mute = true;
        a.setMicState (1, m);

        // Пределы
        auto wild = a.getMicState (1);
        wild.levelDb = 100.0f;
        wild.out = 99;
        a.setMicState (1, wild);
        CHECK (a.getMicState (1).levelDb == 12.0f);
        CHECK (a.getMicState (1).out == kNumOutputs);
        a.setMicState (1, m);

        a.getStateInformation (saved);
    }

    // Новый экземпляр: пресет и микшер восстанавливаются из проекта
    SamplitProcessor b;
    b.prepareToPlay (44100.0, 512);
    b.setLicenseManager (started (env));
    b.setStateInformation (saved.getData(), (int) saved.getSize());
    REQUIRE (pumpUntil ([&] { return b.getKit() != nullptr && ! b.isKitLoading(); }));
    CHECK (b.getPresetName() == "p1");
    CHECK (b.getMicState (0).solo);
    CHECK (b.getMicState (0).out == 3);
    CHECK (b.getMicState (1).mute);
    CHECK (b.getMicState (1).levelDb == -10.0f);

    // Выбор пресета заново сбрасывает микшер к значениям пресета
    bool called = false;
    b.selectPreset (b.scanPresets()[0].preset, [&] (const KitLoadResult&) { called = true; });
    REQUIRE (pumpUntil ([&] { return called; }));
    CHECK_FALSE (b.getMicState (0).solo);
    CHECK (b.getMicState (1).levelDb == -4.0f);
}

TEST_CASE ("mixes: a preset takes its mix, a mix applies without reload, edits are flagged and saved", "[licmgr]")
{
    Env env;
    const auto kickKey = pack::generateKey();
    writeKickPack (env.dir.getChildFile ("packs"), "kick", kickKey, 36);
    writePreset (env.dir, R"({ "format": 1, "id": "pm", "name": "PM", "packs": ["kick"], "mix": "m-main" })");
    const auto mixes = env.dir.getChildFile ("presets").getChildFile ("mixes");
    mixes.createDirectory();
    mixes.getChildFile ("m-main.json").replaceWithText (
        R"({ "id": "m-main", "name": "Main", "mics": { "kick_in": { "level": -2.0, "pan": 0.0, "out": 0 }, "oh": { "level": -5.0, "pan": 0.3, "out": 0 } } })");
    mixes.getChildFile ("m-multi.json").replaceWithText (
        R"({ "id": "m-multi", "name": "Multi", "mics": { "kick_in": { "level": -2.0, "out": 1 }, "oh": { "level": -5.0, "pan": 0.3, "out": 2, "mute": true } } })");
    mixes.getChildFile ("broken.json").replaceWithText ("{ nope");
    env.writeToken ({ { "kick", kickKey } });

    SamplitProcessor p;
    p.prepareToPlay (44100.0, 512);
    p.setLicenseManager (started (env));
    REQUIRE (p.scanMixes().size() == 2);   // битый файл пропущен

    bool called = false;
    p.selectPreset (p.scanPresets()[0].preset, [&] (const KitLoadResult&) { called = true; });
    REQUIRE (pumpUntil ([&] { return called; }));
    CHECK (p.getMixName() == "m-main");
    CHECK_FALSE (p.isMixModified());
    CHECK (p.getMicState (0).out == 0);
    CHECK (p.getMicState (0).levelDb == -2.0f);
    CHECK (p.getMicState (1).pan == 0.3f);

    // Другой микс применяется мгновенно, кит тот же
    const auto kit = p.getKit();
    for (const auto& m : p.scanMixes())
        if (m.id == "m-multi")
            p.applyMix (m);
    CHECK (p.getKit() == kit);
    CHECK (p.getMixName() == "m-multi");
    CHECK (p.getMicState (0).out == 1);
    CHECK (p.getMicState (1).out == 2);
    CHECK (p.getMicState (1).mute);

    // Ручная правка помечает микс изменённым
    auto m = p.getMicState (0);
    m.levelDb = -9.0f;
    p.setMicState (0, m);
    CHECK (p.isMixModified());

    // Сохранение: новый файл, повторное имя не перезаписывает
    juce::String error;
    const auto first = p.saveCurrentMixAs ("Мой микс", error);
    REQUIRE (first != juce::File());
    const auto second = p.saveCurrentMixAs ("Мой микс", error);
    REQUIRE (second != juce::File());
    CHECK (first != second);
    CHECK (p.scanMixes().size() == 4);
    CHECK (p.getMixName() == second.getFileNameWithoutExtension());
    CHECK_FALSE (p.isMixModified());
    CHECK (p.saveCurrentMixAs ("   ", error) == juce::File());

    Mix saved;
    REQUIRE (Mix::fromFile (first, saved, error));
    CHECK (saved.mics["kick_in"].levelDb == -9.0f);
    CHECK (saved.mics["kick_in"].out == 1);
    CHECK (saved.mics["oh"].mute);

    // Смена пресета при правках: микшер переносится, а не сбрасывается на микс
    m = p.getMicState (0);
    m.levelDb = -7.0f;
    p.setMicState (0, m);
    REQUIRE (p.isMixModified());
    called = false;
    p.selectPreset (p.scanPresets()[0].preset, [&] (const KitLoadResult&) { called = true; });
    REQUIRE (pumpUntil ([&] { return called; }));
    CHECK (p.getMicState (0).levelDb == -7.0f);
    CHECK (p.isMixModified());
}

TEST_CASE ("piece level scales the drum, is saved with the project and renamed or deleted files are guarded", "[licmgr]")
{
    Env env;
    const auto kickKey = pack::generateKey();
    writeKickPack (env.dir.getChildFile ("packs"), "kick", kickKey, 36);
    writePreset (env.dir, kPresetJson);
    env.writeToken ({ { "kick", kickKey } });

    juce::MemoryBlock saved;
    float loud = 0.0f;
    {
        SamplitProcessor p;
        p.prepareToPlay (44100.0, 512);
        p.setLicenseManager (started (env));
        bool called = false;
        p.selectPreset (p.scanPresets()[0].preset, [&] (const KitLoadResult&) { called = true; });
        REQUIRE (pumpUntil ([&] { return called; }));

        auto renderNote = [&]
        {
            juce::AudioBuffer<float> buffer (p.getTotalNumOutputChannels(), 512);
            buffer.clear();
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
            p.processBlock (buffer, midi);
            float peak = 0.0f;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                peak = std::max (peak, buffer.getMagnitude (ch, 0, 512));
            return peak;
        };

        loud = renderNote();
        REQUIRE (loud > 0.0f);
        CHECK (p.getPieceLevel (0) == 0.0f);
        p.setPieceLevel (0, -12.0f);
        CHECK (p.getPieceLevel (0) == -12.0f);
        const float quiet = renderNote();
        CHECK (quiet < loud * 0.3f);
        CHECK (quiet > loud * 0.2f);
        p.setPieceLevel (0, 100.0f);
        CHECK (p.getPieceLevel (0) == 12.0f);
        p.setPieceLevel (0, -12.0f);
        p.getStateInformation (saved);
    }

    SamplitProcessor b;
    b.prepareToPlay (44100.0, 512);
    b.setLicenseManager (started (env));
    b.setStateInformation (saved.getData(), (int) saved.getSize());
    REQUIRE (pumpUntil ([&] { return b.getKit() != nullptr && ! b.isKitLoading(); }));
    CHECK (b.getPieceLevel (0) == -12.0f);

    // Выбор пресета заново возвращает 0 дБ
    bool called = false;
    b.selectPreset (b.scanPresets()[0].preset, [&] (const KitLoadResult&) { called = true; });
    REQUIRE (pumpUntil ([&] { return called; }));
    CHECK (b.getPieceLevel (0) == 0.0f);

    // Свои файлы: переименование меняет только название, чужие трогать нельзя
    juce::String error;
    const auto mine = b.saveCurrentMixAs ("First", error);
    REQUIRE (mine != juce::File());
    REQUIRE (b.renameUserFile (mine, "Second", error));
    Mix m;
    REQUIRE (Mix::fromFile (mine, m, error));
    CHECK (m.name == "Second");
    CHECK (m.id == mine.getFileNameWithoutExtension());
    CHECK_FALSE (b.renameUserFile (mine, "  ", error));

    const auto shipped = env.dir.getChildFile ("presets").getChildFile ("p1.json");
    CHECK_FALSE (b.renameUserFile (shipped, "X", error));
    CHECK_FALSE (b.deleteUserFile (shipped, error));
    CHECK (shipped.existsAsFile());
}

TEST_CASE ("host parameters follow the mixer and the mixer follows the host", "[licmgr]")
{
    Env env;
    const auto kickKey = pack::generateKey();
    writeKickPack (env.dir.getChildFile ("packs"), "kick", kickKey, 36);
    writePreset (env.dir, kPresetJson);
    env.writeToken ({ { "kick", kickKey } });

    SamplitProcessor p;
    p.prepareToPlay (44100.0, 512);
    p.setLicenseManager (started (env));
    CHECK (p.getParameters().size() == 2 * SamplitProcessor::kAutomatedMics);

    bool called = false;
    p.selectPreset (p.scanPresets()[0].preset, [&] (const KitLoadResult&) { called = true; });
    REQUIRE (pumpUntil ([&] { return called; }));

    auto* level1 = dynamic_cast<juce::AudioParameterFloat*> (p.getParameters()[2]);   // mic 2, уровень
    auto* pan1 = dynamic_cast<juce::AudioParameterFloat*> (p.getParameters()[3]);
    REQUIRE (level1 != nullptr);
    REQUIRE (pan1 != nullptr);
    CHECK (level1->paramID == "mic02_level");

    // Предзагруженный микс пресета виден в параметрах (oh: −4 дБ, панорама 0,5)
    CHECK (std::abs (level1->get() - (-4.0f)) < 0.06f);
    CHECK (std::abs (pan1->get() - 0.5f) < 0.006f);

    // Микшер → параметр
    auto m = p.getMicState (1);
    m.levelDb = -9.0f;
    p.setMicState (1, m);
    CHECK (std::abs (level1->get() - (-9.0f)) < 0.06f);

    // Хост → микшер (через очередь message thread)
    level1->setValueNotifyingHost (level1->convertTo0to1 (-20.0f));
    pan1->setValueNotifyingHost (pan1->convertTo0to1 (-0.4f));
    REQUIRE (pumpUntil ([&] { return std::abs (p.getMicState (1).levelDb - (-20.0f)) < 0.06f; }));
    CHECK (std::abs (p.getMicState (1).pan - (-0.4f)) < 0.006f);
    CHECK (p.getMicState (1).out == 6);   // выход и mute автоматизацией не трогаются
}

TEST_CASE ("switching kits while the audio thread keeps playing stays finite and frees the old kits", "[licmgr][stress]")
{
    Env env;
    const auto k1 = pack::generateKey(), k2 = pack::generateKey();
    writeKickPack (env.dir.getChildFile ("packs"), "kick", k1, 36);
    writeKickPack (env.dir.getChildFile ("packs"), "kick2", k2, 36);
    writePreset (env.dir, R"({ "format": 1, "id": "a", "name": "A", "packs": ["kick"] })");
    writePreset (env.dir, R"({ "format": 1, "id": "b", "name": "B", "packs": ["kick2"] })");
    env.writeToken ({ { "kick", k1 }, { "kick2", k2 } });

    SamplitProcessor p;
    p.prepareToPlay (44100.0, 128);
    p.setLicenseManager (started (env));
    const auto presets = p.scanPresets();
    REQUIRE (presets.size() == 2);

    std::atomic<bool> run { true }, bad { false };
    std::atomic<int> blocks { 0 };
    std::thread audio ([&]
    {
        juce::AudioBuffer<float> buffer (p.getTotalNumOutputChannels(), 128);
        while (run.load())
        {
            buffer.clear();
            juce::MidiBuffer midi;
            midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 100), 0);
            p.processBlock (buffer, midi);
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int i = 0; i < 128; ++i)
                    if (! std::isfinite (buffer.getSample (ch, i)))
                        bad.store (true);
            blocks.fetch_add (1);
            std::this_thread::sleep_for (std::chrono::microseconds (200));
        }
    });

    for (int i = 0; i < 12; ++i)
    {
        bool called = false;
        p.selectPreset (presets[(size_t) (i % 2)].preset, [&] (const KitLoadResult&) { called = true; });
        REQUIRE (pumpUntil ([&] { return called; }));
        p.setPieceLevel (0, (float) (i % 5) - 2.0f);
        auto m = p.getMicState (0);
        m.levelDb = -(float) i;
        p.setMicState (0, m);
    }

    run.store (false);
    audio.join();
    CHECK_FALSE (bad.load());
    CHECK (blocks.load() > 0);

    // Старые киты освобождаются, когда из них ничего не звучит
    pumpUntil ([] { return false; }, 300);
    p.collectGarbage();
    CHECK (p.getRetiredCount() <= 2);
}

TEST_CASE ("manual mixer edits survive a kit change without a chosen mix, and a saved preset replays them", "[licmgr]")
{
    Env env;
    const auto kickKey = pack::generateKey();
    writeKickPack (env.dir.getChildFile ("packs"), "kick", kickKey, 36);
    writePreset (env.dir, kPresetJson);   // старый формат: блок mics, без mix
    writePreset (env.dir, R"({ "format": 1, "id": "p3", "name": "P3", "packs": ["kick"], "mix": "m-x" })");
    const auto mixes = env.dir.getChildFile ("presets").getChildFile ("mixes");
    mixes.createDirectory();
    mixes.getChildFile ("m-x.json").replaceWithText (R"({ "id": "m-x", "name": "X", "mics": { "kick_in": { "level": -70.0, "out": 0 } } })");
    env.writeToken ({ { "kick", kickKey } });

    SamplitProcessor p;
    p.prepareToPlay (44100.0, 512);
    p.setLicenseManager (started (env));
    const auto find = [&] (const char* id)
    {
        for (const auto& e : p.scanPresets())
            if (e.preset.id == id)
                return e.preset;
        return Preset();
    };
    const auto load = [&] (const Preset& preset)
    {
        bool called = false;
        p.selectPreset (preset, [&] (const KitLoadResult&) { called = true; });
        REQUIRE (pumpUntil ([&] { return called; }));
    };

    load (find ("p1"));
    CHECK (p.getMixName().isEmpty());
    auto m = p.getMicState (0);
    m.levelDb = -8.0f;
    p.setMicState (0, m);

    // Микс не выбирали, но правки переносятся на другой кит; значение вне диапазона миксa не портит микшер
    load (find ("p3"));
    CHECK (p.getMicState (0).levelDb == -8.0f);

    // Сохранённый пресет проигрывает те же значения, даже если выбран другой микс
    juce::String error;
    const auto saved = p.saveCurrentPresetAs ("Replay", error);
    REQUIRE (saved != juce::File());
    m = p.getMicState (0);
    m.levelDb = -30.0f;
    p.setMicState (0, m);
    Preset replay;
    REQUIRE (Preset::fromFile (saved, replay, error));
    load (replay);
    CHECK (p.getMicState (0).levelDb == -8.0f);

    // Микс с уровнем −70 дБ обрезается до границы параметра и не помечается «изменённым»
    p.applyMix (p.scanMixes()[0]);
    CHECK (p.getMicState (0).levelDb == -60.0f);
    pumpUntil ([] { return false; }, 200);
    CHECK_FALSE (p.isMixModified());

    // Чужие файлы трогать нельзя, id микса — имя файла
    CHECK_FALSE (p.deleteUserFile (mixes.getChildFile ("m-x.json"), error));
}

TEST_CASE ("a perpetual licence has no end date and ignores the clock", "[licmgr]")
{
    Env env;
    const auto key = pack::generateKey();
    env.writeToken ({ { "kick", key } }, 0);
    env.clockNow = kNow + 3650 * kDay;

    auto m = started (env);
    CHECK (m->getInfo().isValid());
    CHECK (m->getInfo().expires == 0);
    pack::PackKey got;
    CHECK (m->keyFor ("kick", got));

    // Часы назад: срочная лицензия закрылась бы, бессрочная работает
    env.clockNow = kNow - 30 * kDay;
    int changes = 0;
    m->onChange = [&] { ++changes; };
    m->check();
    REQUIRE (pumpUntil ([&] { return changes > 0; }));
    CHECK (m->getInfo().isValid());
}

TEST_CASE ("a licence file can be imported without a server", "[licmgr]")
{
    Env env;
    auto m = started (env);
    REQUIRE (m->getInfo().status == Status::NoToken);
    CHECK (m->getMachineCode() == kFingerprint);

    int changes = 0;
    m->onChange = [&] { ++changes; };
    const auto incoming = env.dir.getChildFile ("incoming.token");
    const auto installed = m->getTokenFile();

    SECTION ("годный файл ставится")
    {
        const auto key = pack::generateKey();
        env.writeToken ({ { "kick", key } });
        REQUIRE (installed.moveFileTo (incoming));

        m->importTokenFile (incoming);
        REQUIRE (pumpUntil ([&] { return changes > 0; }));
        CHECK (m->getInfo().isValid());
        CHECK (m->getInfo().lastError.isEmpty());
        CHECK (installed.existsAsFile());
        pack::PackKey got;
        CHECK (m->keyFor ("kick", got));
    }

    SECTION ("чужой файл не ставится и прежнюю лицензию не трогает")
    {
        incoming.replaceWithText ("{ \"payload\": {}, \"sig\": \"AAAA\" }");
        m->importTokenFile (incoming);
        REQUIRE (pumpUntil ([&] { return changes > 0; }));
        CHECK_FALSE (m->getInfo().isValid());
        CHECK (m->getInfo().lastError.isNotEmpty());
        CHECK_FALSE (installed.existsAsFile());
    }
}

// Манифесты режима библиотеки: сначала папка данных, иначе packs-src рядом с папкой библиотеки
TEST_CASE ("library mode manifests fall back to packs-src next to the library", "[license][library]")
{
    Env env;
    LicenseManager manager (env.config());
    const auto lib = env.dir.getChildFile ("lib").getChildFile ("00-Drums");
    const auto source = env.dir.getChildFile ("lib").getChildFile ("packs-src");
    lib.createDirectory();
    source.createDirectory();
    source.getChildFile ("kick.json").replaceWithText ("{}");

    manager.setLibraryRoot (lib);
    CHECK (manager.getManifestDir() == source);

    env.dir.getChildFile ("manifests").createDirectory();
    env.dir.getChildFile ("manifests").getChildFile ("kick.json").replaceWithText ("{}");
    CHECK (manager.getManifestDir() == env.dir.getChildFile ("manifests"));

    manager.setLibraryRoot ({});
}

// ---------------------------------------------------------------- лицензия отложена (enforce == false)

namespace
{
    LicenseManager::Config disabledConfig (const Env& env)
    {
        auto c = env.config();
        c.enforce = false;
        return c;
    }

    // Библиотека из одного 24-битного WAV и манифест пакета «lib-kick» на клавише 36
    void writeWavLibrary (const Env& env, const juce::File& lib)
    {
        lib.getChildFile ("KICK").createDirectory();
        {
            juce::WavAudioFormat fmt;
            std::unique_ptr<juce::OutputStream> os (lib.getChildFile ("KICK/k.wav").createOutputStream());
            REQUIRE (os != nullptr);
            auto writer = fmt.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (44100.0).withNumChannels (1).withBitsPerSample (24));
            REQUIRE (writer != nullptr);
            std::vector<int> data (4000, 1000 * 256);
            const int* ch[1] = { data.data() };
            REQUIRE (writer->write (ch, (int) data.size()));
        }

        pack::Manifest m;
        m.id = "lib-kick";
        m.mics = { { "kick_in", "Kick In", "close", 1 } };
        pack::Piece p;
        p.id = "kick";
        p.keys = { 36 };
        p.layers = { { "all", "center", 1, 127 } };
        pack::Hit h;
        h.id = "k0";
        h.layer = "all";
        h.key = 36;
        h.velLo = 1;
        h.velHi = 127;
        h.files["kick_in"].path = "KICK/k.wav";
        p.hits.push_back (h);
        m.pieces.push_back (p);
        const auto manifests = env.dir.getChildFile ("manifests");
        manifests.createDirectory();
        manifests.getChildFile ("lib-kick.json").replaceWithText (m.toJson());
    }
}

TEST_CASE ("disabled licence: no check, no keys, and a WAV library plays", "[licmgr][nolicense]")
{
    Env env;
    env.writeToken ({ { "kick", pack::generateKey() } });   // даже годный токен игнорируется
    writePreset (env.dir, R"({ "format": 1, "id": "lk", "name": "Lib kick", "packs": ["lib-kick"] })");
    const auto lib = env.dir.getChildFile ("lib");
    writeWavLibrary (env, lib);

    kittest::KitFixture f;
    f.processor.setLicenseManager (std::make_unique<LicenseManager> (disabledConfig (env)));
    auto& lic = f.processor.getLicense();

    CHECK_FALSE (lic.isEnforced());
    const auto info = lic.getInfo();
    CHECK (info.disabled);
    CHECK_FALSE (info.busy);
    CHECK_FALSE (info.isValid());
    CHECK (info.statusText() == "отключена"_s);

    pack::PackKey key;
    CHECK_FALSE (lic.keyFor ("kick", key));
    lic.importTokenText ("whatever");       // ничего не делает, фонового обмена нет
    CHECK_FALSE (lic.getInfo().busy);

    lic.setLibraryRoot (lib);
    const auto presets = f.processor.scanPresets();
    REQUIRE (presets.size() == 1);
    CHECK (presets[0].isPlayable());
    CHECK (presets[0].isComplete());

    bool called = false, ok = false;
    f.processor.loadPreset (presets[0].preset, [&] (const KitLoadResult& r) { called = true; ok = r.ok(); });
    REQUIRE (pumpUntil ([&] { return called; }));
    REQUIRE (ok);

    const auto out = f.play (36);
    CHECK (kittest::KitFixture::peak (out, 0) > 0.0f);
    lic.setLibraryRoot ({});
}

TEST_CASE ("disabled licence: a preset without a library folder is not playable", "[licmgr][nolicense]")
{
    Env env;
    writePreset (env.dir, R"({ "format": 1, "id": "lk", "name": "Lib kick", "packs": ["lib-kick"] })");
    writeWavLibrary (env, env.dir.getChildFile ("lib"));       // манифест есть, но папка библиотеки не выбрана
    writeKickPack (env.dir.getChildFile ("packs"), "lib-kick", pack::generateKey(), 36);   // пакет без лицензии не помогает

    SamplitProcessor processor;
    processor.setLicenseManager (std::make_unique<LicenseManager> (disabledConfig (env)));
    REQUIRE (processor.getLicense().getLibraryRoot() == juce::File());

    const auto presets = processor.scanPresets();
    REQUIRE (presets.size() == 1);
    CHECK_FALSE (presets[0].isPlayable());
    REQUIRE (presets[0].packs.size() == 1);
    CHECK (presets[0].packs[0].second == PackState::NoLibrary);

    bool called = false;
    KitLoadResult got;
    processor.loadPreset (presets[0].preset, [&] (const KitLoadResult& r) { called = true; got = r; });
    REQUIRE (pumpUntil ([&] { return called; }));
    CHECK_FALSE (got.ok());
    CHECK (got.error.contains ("папку библиотеки"_s));
    CHECK (processor.getKit() == nullptr);
}
