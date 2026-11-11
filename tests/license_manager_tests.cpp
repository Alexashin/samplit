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

    // Установка с одним местом: бочка из пакета "kick" (writeKickPack), остальные места выключены
    Setup kickSetup (const char* packId = "kick", const char* pieceId = "kick")
    {
        auto s = Setup::factory();
        for (auto& p : s.places)
        {
            p.enabled = p.placeId == "kick";
            if (p.enabled)
            {
                p.packId = packId;
                p.pieceId = pieceId;
            }
        }
        return s;
    }
}


TEST_CASE ("the processor opens packs only with a licence", "[licmgr]")
{
    Env env;
    const auto packKey = pack::generateKey();
    writeKickPack (env.dir.getChildFile ("packs"), "kick", packKey, 36);

    SamplitProcessor processor;
    processor.prepareToPlay (44100.0, 512);

    auto runSetup = [&]
    {
        processor.setSetup (kickSetup());
        REQUIRE (pumpUntil ([&] { return ! processor.isKitLoading(); }));
        return processor.getKit() != nullptr;
    };

    SECTION ("без токена кит не собирается")
    {
        processor.setLicenseManager (started (env));
        CHECK_FALSE (runSetup());
    }

    SECTION ("с токеном собирается")
    {
        env.writeToken ({ { "kick", packKey } });
        processor.setLicenseManager (started (env));
        CHECK (runSetup());
        REQUIRE (processor.getKit() != nullptr);
        CHECK (processor.getKit()->pieces.size() == 1);
    }

    SECTION ("токен на другой ключ: пакет не открывается")
    {
        env.writeToken ({ { "kick", pack::generateKey() } });
        processor.setLicenseManager (started (env));
        CHECK_FALSE (runSetup());
    }
}

TEST_CASE ("mixer: solo and mute are saved with the project", "[licmgr]")
{
    Env env;
    const auto kickKey = pack::generateKey();
    writeKickPack (env.dir.getChildFile ("packs"), "kick", kickKey, 36);
    env.writeToken ({ { "kick", kickKey } });

    juce::MemoryBlock saved;
    {
        SamplitProcessor a;
        a.prepareToPlay (44100.0, 512);
        a.setLicenseManager (started (env));
        a.setSetup (kickSetup());
        REQUIRE (pumpUntil ([&] { return a.getKit() != nullptr && ! a.isKitLoading(); }));

        // Значения микрофонов из манифеста пака
        CHECK (a.getMicState (0).out == 1);
        CHECK (a.getMicState (1).out == 2);
        CHECK (a.getMicState (1).levelDb == 0.0f);

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

    // Новый экземпляр: установка и микшер восстанавливаются из проекта
    SamplitProcessor b;
    b.prepareToPlay (44100.0, 512);
    b.setLicenseManager (started (env));
    b.setStateInformation (saved.getData(), (int) saved.getSize());
    REQUIRE (pumpUntil ([&] { return b.getKit() != nullptr && ! b.isKitLoading(); }));
    CHECK (b.getMicState (0).solo);
    CHECK (b.getMicState (0).out == 3);
    CHECK (b.getMicState (1).mute);
    CHECK (b.getMicState (1).levelDb == -10.0f);
    CHECK (b.getSetup().find ("kick")->packId == "kick");
}

TEST_CASE ("host parameters follow the mixer and the mixer follows the host", "[licmgr]")
{
    Env env;
    const auto kickKey = pack::generateKey();
    writeKickPack (env.dir.getChildFile ("packs"), "kick", kickKey, 36);
    env.writeToken ({ { "kick", kickKey } });

    SamplitProcessor p;
    p.prepareToPlay (44100.0, 512);
    p.setLicenseManager (started (env));
    CHECK (p.getParameters().size() == 2 * SamplitProcessor::kAutomatedMics);

    p.setSetup (kickSetup());
    REQUIRE (pumpUntil ([&] { return p.getKit() != nullptr && ! p.isKitLoading(); }));

    auto* level1 = dynamic_cast<juce::AudioParameterFloat*> (p.getParameters()[2]);   // mic 2, уровень
    auto* pan1 = dynamic_cast<juce::AudioParameterFloat*> (p.getParameters()[3]);
    REQUIRE (level1 != nullptr);
    REQUIRE (pan1 != nullptr);
    CHECK (level1->paramID == "mic02_level");

    // Микшер → параметр (oh: −4 дБ, панорама 0,5, выход 6)
    auto m = p.getMicState (1);
    m.levelDb = -4.0f;
    m.pan = 0.5f;
    m.out = 6;
    p.setMicState (1, m);
    CHECK (std::abs (level1->get() - (-4.0f)) < 0.06f);
    CHECK (std::abs (pan1->get() - 0.5f) < 0.006f);

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
    env.writeToken ({ { "kick", k1 }, { "kick2", k2 } });

    SamplitProcessor p;
    p.prepareToPlay (44100.0, 128);
    p.setLicenseManager (started (env));
    const Setup setups[2] = { kickSetup ("kick", "kick"), kickSetup ("kick2", "kick2") };

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
        p.setSetup (setups[i % 2]);
        REQUIRE (pumpUntil ([&] { return ! p.isKitLoading(); }));
        REQUIRE (p.getKit() != nullptr);
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

TEST_CASE ("manual mixer edits survive a kit change", "[licmgr]")
{
    Env env;
    const auto k1 = pack::generateKey(), k2 = pack::generateKey();
    writeKickPack (env.dir.getChildFile ("packs"), "kick", k1, 36);
    writeKickPack (env.dir.getChildFile ("packs"), "kick2", k2, 36);
    env.writeToken ({ { "kick", k1 }, { "kick2", k2 } });

    SamplitProcessor p;
    p.prepareToPlay (44100.0, 512);
    p.setLicenseManager (started (env));
    const auto load = [&] (const Setup& setup)
    {
        p.setSetup (setup);
        REQUIRE (pumpUntil ([&] { return ! p.isKitLoading(); }));
        REQUIRE (p.getKit() != nullptr);
    };

    load (kickSetup ("kick", "kick"));
    auto m = p.getMicState (0);
    m.levelDb = -8.0f;
    p.setMicState (0, m);

    // Микрофоны пака kick2 те же по id: правки переносятся на другой кит
    load (kickSetup ("kick2", "kick2"));
    CHECK (p.getMicState (0).levelDb == -8.0f);

    // Значение вне диапазона обрезается до границы параметра
    m = p.getMicState (0);
    m.levelDb = -70.0f;
    p.setMicState (0, m);
    CHECK (p.getMicState (0).levelDb == -60.0f);
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
    f.processor.setSetup (kickSetup ("lib-kick", "kick"));
    REQUIRE (pumpUntil ([&] { return f.processor.getKit() != nullptr && ! f.processor.isKitLoading(); }));

    const auto out = f.play (24);   // клавиша места «kick»: барабан со своей 36 перенесён на неё
    CHECK (kittest::KitFixture::peak (out, 0) > 0.0f);
    lic.setLibraryRoot ({});
}

TEST_CASE ("disabled licence: a setup without a library folder is not playable", "[licmgr][nolicense]")
{
    Env env;
    writeWavLibrary (env, env.dir.getChildFile ("lib"));       // манифест есть, но папка библиотеки не выбрана
    writeKickPack (env.dir.getChildFile ("packs"), "lib-kick", pack::generateKey(), 36);   // пакет без лицензии не помогает

    SamplitProcessor processor;
    processor.setLicenseManager (std::make_unique<LicenseManager> (disabledConfig (env)));
    REQUIRE (processor.getLicense().getLibraryRoot() == juce::File());

    processor.setSetup (kickSetup ("lib-kick", "kick"));
    REQUIRE (pumpUntil ([&] { return ! processor.isKitLoading(); }));
    CHECK (processor.getLastKitLoad().error.contains ("папку библиотеки"_s));
    CHECK (processor.getKit() == nullptr);
}

// ---------------------------------------------------------------- установка Studio600 в процессоре

namespace
{
    // Пак с одним барабаном типа tom на клавишах 96..97 (удар на 96), клип постоянной величины заданной длины
    void writeTomPack (const juce::File& packDir, const juce::String& id, const juce::String& pieceId, int frames,
                       const pack::PackKey& key)
    {
        packDir.createDirectory();
        juce::String error;
        auto w = pack::PackWriter::create (packDir.getChildFile (id + ".splpack"), id, key, error);
        REQUIRE (w != nullptr);
        pack::DecodedAudio a;
        a.sampleRate = 44100.0;
        a.frames = frames;
        a.samples.assign ((size_t) frames, 2'000'000);
        REQUIRE (w->addAudio (a, error) == 0);

        pack::Manifest m;
        m.id = id;
        m.mics = { { "kick_in", "Kick In", "close", 1 }, { "oh", "OH", "overhead", 2 } };
        pack::Piece p;
        p.id = pieceId;
        p.type = "tom";
        p.keys = { 96, 97 };
        p.layers = { { "all", "center", 1, 127 } };
        pack::Hit h;
        h.id = pieceId + "0";
        h.layer = "all";
        h.key = 96;
        pack::HitFile f;
        f.chunk = 0;
        f.frames = frames;
        f.rate = 44100.0;
        f.peak = 2'000'000;
        h.files["kick_in"] = f;
        h.files["oh"] = f;
        p.hits.push_back (h);
        m.pieces.push_back (p);
        REQUIRE (w->finish (m, error));
    }

    // Включены только tom_16l (пак pa) и tom_16m (пак pb)
    Setup twoTomSetup()
    {
        auto s = Setup::factory();
        for (auto& p : s.places)
        {
            p.enabled = p.placeId == "tom_16l" || p.placeId == "tom_16m";
            if (p.placeId == "tom_16l") { p.packId = "pa"; p.pieceId = "a"; }
            if (p.placeId == "tom_16m") { p.packId = "pb"; p.pieceId = "b"; }
        }
        return s;
    }

    struct SetupEnv
    {
        explicit SetupEnv (int frames = 400)
        {
            const auto ka = pack::generateKey(), kb = pack::generateKey(), kc = pack::generateKey();
            writeTomPack (env.dir.getChildFile ("packs"), "pa", "a", frames, ka);
            writeTomPack (env.dir.getChildFile ("packs"), "pb", "b", frames, kb);
            writeTomPack (env.dir.getChildFile ("packs"), "pc", "c", frames, kc);
            env.writeToken ({ { "pa", ka }, { "pb", kb }, { "pc", kc } });
            processor.setRateAndBufferSizeDetails (44100.0, 512);
            processor.prepareToPlay (44100.0, 512);
            processor.setLicenseManager (started (env));
        }

        bool loaded() { return pumpUntil ([&] { return processor.getKit() != nullptr && ! processor.isKitLoading(); }); }

        juce::AudioBuffer<float> block (int note = -1)
        {
            juce::AudioBuffer<float> buffer (processor.getTotalNumOutputChannels(), 512);
            juce::MidiBuffer midi;
            if (note >= 0)
                midi.addEvent (juce::MidiMessage::noteOn (1, note, 1.0f), 0);
            processor.processBlock (buffer, midi);
            return buffer;
        }

        Env env;
        SamplitProcessor processor;
    };

    juce::MemoryBlock stateOf (const juce::ValueTree& tree)
    {
        juce::MemoryBlock mb;
        const auto xml = tree.createXml();
        SamplitProcessor::copyXmlToBinary (*xml, mb);
        return mb;
    }
}

TEST_CASE ("setup state v3 round trip keeps setup and mixer", "[licmgr][setup]")
{
    SetupEnv e;
    auto s = twoTomSetup();
    s.places[8].levelDb = -3.5f;                     // tom_16l
    s.places[9].picks["g"] = Pick::Cycle;            // tom_16m
    e.processor.setSetup (s);
    REQUIRE (e.loaded());
    auto m = e.processor.getMicState (1);
    m.levelDb = -7.0f;
    m.mute = true;
    e.processor.setMicState (1, m);

    juce::MemoryBlock saved;
    e.processor.getStateInformation (saved);
    const auto xml = SamplitProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
    REQUIRE (xml != nullptr);
    CHECK (xml->getIntAttribute ("version") == 3);
    CHECK (xml->hasAttribute ("setup"));
    CHECK_FALSE (xml->hasAttribute ("preset"));
    CHECK (xml->getChildByName ("PICK") == nullptr);

    SetupEnv f;
    f.processor.setStateInformation (saved.getData(), (int) saved.getSize());
    REQUIRE (f.loaded());
    const auto got = f.processor.getSetup();
    CHECK (got.find ("tom_16l")->levelDb == -3.5f);
    CHECK (got.find ("tom_16l")->packId == "pa");
    CHECK (got.find ("tom_16m")->picks.at ("g") == Pick::Cycle);
    CHECK_FALSE (got.find ("kick")->enabled);
    CHECK (f.processor.getMicState (1).levelDb == -7.0f);
    CHECK (f.processor.getMicState (1).mute);
}

TEST_CASE ("setup state v2 migrates: snare by preset id, MIC kept, PICK dropped", "[licmgr][setup]")
{
    auto makeV2 = [] (const char* presetId)
    {
        juce::ValueTree t ("SAMPLIT");
        t.setProperty ("version", 2, nullptr);
        t.setProperty ("preset", presetId, nullptr);
        t.setProperty ("mix", "some-mix", nullptr);
        juce::ValueTree mic ("MIC");
        mic.setProperty ("id", "sn_top", nullptr);
        mic.setProperty ("level", -4.0f, nullptr);
        mic.setProperty ("out", 3, nullptr);
        t.appendChild (mic, nullptr);
        juce::ValueTree pick ("PICK");
        pick.setProperty ("id", "snare", nullptr);
        pick.setProperty ("mode", pickToString (Pick::Cycle), nullptr);
        t.appendChild (pick, nullptr);
        return stateOf (t);
    };

    struct Case { const char* preset; const char* pack; const char* piece; };
    for (const auto& c : { Case { "studio600-ahead-low", "studio600-snare-low", "snare_low" },
                           Case { "studio600-ambasador", "studio600-snare-hi", "snare_hi" },
                           Case { "something-else", "studio600-snare-live", "snare_live" } })
    {
        SetupEnv e;
        const auto v2 = makeV2 (c.preset);
        e.processor.setStateInformation (v2.getData(), (int) v2.getSize());
        const auto s = e.processor.getSetup();
        CHECK (s.find ("snare")->packId == c.pack);
        CHECK (s.find ("snare")->pieceId == c.piece);
        CHECK (s.find ("kick")->packId == "studio600-kick-rode");      // остальное заводское
        CHECK (s.find ("snare")->picks.empty());

        juce::MemoryBlock again;
        e.processor.getStateInformation (again);
        const auto xml = SamplitProcessor::getXmlFromBinary (again.getData(), (int) again.getSize());
        REQUIRE (xml != nullptr);
        CHECK (xml->getIntAttribute ("version") == 3);
        CHECK (xml->getChildByName ("PICK") == nullptr);
        CHECK_FALSE (xml->hasAttribute ("mix"));
        const auto* mic = xml->getChildByName ("MIC");
        REQUIRE (mic != nullptr);
        CHECK (mic->getStringAttribute ("id") == "sn_top");
        CHECK (mic->getDoubleAttribute ("level") == -4.0);
        CHECK (mic->getIntAttribute ("out") == 3);
    }
}

TEST_CASE ("place level changes without reloading the kit", "[licmgr][setup]")
{
    SetupEnv e;
    e.processor.setSetup (twoTomSetup());
    REQUIRE (e.loaded());
    const auto kit = e.processor.getKit();
    const auto serial = kit->serial.value;

    const auto first = kittest::KitFixture::peak (e.block (96), 0);
    REQUIRE (first > 0.1f);
    e.block();      // клип кончился

    e.processor.setPlaceLevel ("tom_16l", -6.0206f);
    CHECK (e.processor.getSetup().find ("tom_16l")->levelDb == -6.0206f);
    CHECK_FALSE (e.processor.isKitLoading());
    CHECK (e.processor.getKit() == kit);
    CHECK (e.processor.getKit()->serial.value == serial);

    const auto second = kittest::KitFixture::peak (e.block (96), 0);
    CHECK (std::abs (second - first * 0.5f) < 0.01f);

    // Второе место (клавиша 98) уровнем первого не затронуто
    e.block();
    const auto other = kittest::KitFixture::peak (e.block (98), 0);
    CHECK (std::abs (other - first) < 0.01f);
}

TEST_CASE ("changing the pack of one place does not interrupt a sounding strike of another", "[licmgr][setup]")
{
    SetupEnv e (300000);        // ~6.8 с: удар звучит дольше всей смены пака
    e.processor.setSetup (twoTomSetup());
    REQUIRE (e.loaded());
    const auto firstSerial = e.processor.getKit()->serial.value;

    auto minOf = [] (const juce::AudioBuffer<float>& b)
    {
        float m = 1e9f;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < b.getNumSamples(); ++i)
                m = std::min (m, std::abs (b.getSample (ch, i)));
        return m;
    };

    CHECK (minOf (e.block (96)) > 0.1f);        // tom_16l играет
    e.processor.setPlacePack ("tom_16m", "pc", "c");
    bool swapped = false;
    int blocks = 0;
    for (int i = 0; i < 600 && ! swapped; ++i, ++blocks)
    {
        CHECK (minOf (e.block()) > 0.1f);       // хвост звучит без разрывов во время загрузки и после публикации
        juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        swapped = e.processor.getKit()->serial.value != firstSerial;
    }
    REQUIRE (swapped);
    for (int i = 0; i < 20; ++i)
        CHECK (minOf (e.block()) > 0.1f);
    CHECK (e.processor.getSetup().find ("tom_16m")->packId == "pc");
}

TEST_CASE ("second setup apply reuses clips and decodes nothing", "[licmgr][setup]")
{
    SetupEnv e;
    e.processor.setSetup (twoTomSetup());
    REQUIRE (e.loaded());

    bool called = false;
    KitLoadResult got;
    e.processor.applySetup ([&] (const KitLoadResult& r) { called = true; got = r; });
    REQUIRE (pumpUntil ([&] { return called; }));
    REQUIRE (got.ok());
    CHECK (got.decodedClips == 0);
    CHECK (got.reusedClips > 0);
    CHECK_FALSE (e.processor.getCatalog().empty());
}

TEST_CASE ("setup apply without any pack reports an error and keeps the old kit", "[licmgr][setup]")
{
    SetupEnv e;
    e.processor.setSetup (twoTomSetup());
    REQUIRE (e.loaded());
    const auto kit = e.processor.getKit();

    auto s = twoTomSetup();
    for (auto& p : s.places)
        if (p.enabled)
            p.packId = "missing";
    e.processor.setSetup (s);
    REQUIRE (pumpUntil ([&] { return ! e.processor.isKitLoading(); }));
    CHECK (e.processor.getLastKitLoad().error.isNotEmpty());
    CHECK (e.processor.getKit() == kit);
}

TEST_CASE ("group pick goes to the engine and the setup", "[licmgr][setup]")
{
    SetupEnv e;
    e.processor.setSetup (twoTomSetup());
    REQUIRE (e.loaded());
    const auto kit = e.processor.getKit();
    REQUIRE_FALSE (kit->choices.empty());
    const auto id = juce::String (kit->choices[0].id);
    e.processor.setGroupPick (id, Pick::Cycle);
    CHECK (e.processor.getPickChoice (0) == Pick::Cycle);
    const auto colon = id.indexOfChar (':');
    CHECK (e.processor.getSetup().find (id.substring (0, colon).toStdString())->picks.at (id.substring (colon + 1).toStdString()) == Pick::Cycle);
}

TEST_CASE ("group pick made during a kit load survives the publish", "[licmgr][setup]")
{
    SetupEnv e;
    e.processor.setSetup (twoTomSetup());
    REQUIRE (e.loaded());
    const auto kit = e.processor.getKit();
    int index = -1;
    for (int i = 0; i < (int) kit->choices.size(); ++i)
        if (kit->choices[(size_t) i].id.rfind ("tom_16l:", 0) == 0)
            index = i;
    REQUIRE (index >= 0);
    const auto id = juce::String (kit->choices[(size_t) index].id);
    const auto wanted = kit->choices[(size_t) index].pick == Pick::Cycle ? Pick::Random : Pick::Cycle;

    auto s = twoTomSetup();
    for (auto& p : s.places)
        if (p.placeId == "tom_16m")
            p.packId = "pc", p.pieceId = "c";
    e.processor.setSetup (s);                   // фоновая загрузка нового кита
    REQUIRE (e.processor.isKitLoading());
    e.processor.setGroupPick (id, wanted);      // выбор во время загрузки
    REQUIRE (pumpUntil ([&] { return ! e.processor.isKitLoading(); }));
    const auto now = e.processor.getKit();
    REQUIRE (now != kit);
    for (int i = 0; i < (int) now->choices.size(); ++i)
        if (juce::String (now->choices[(size_t) i].id) == id)
            CHECK (e.processor.getPickChoice (i) == wanted);
}

TEST_CASE ("superseded setup load still calls done with cancelled", "[licmgr][setup]")
{
    SetupEnv e;
    e.processor.setSetup (twoTomSetup());
    REQUIRE (e.loaded());

    bool firstCalled = false, secondCalled = false;
    KitLoadResult first, second;
    e.processor.applySetup ([&] (const KitLoadResult& r) { firstCalled = true; first = r; });
    e.processor.applySetup ([&] (const KitLoadResult& r) { secondCalled = true; second = r; });
    REQUIRE (pumpUntil ([&] { return firstCalled && secondCalled; }));
    CHECK (first.cancelled);
    CHECK_FALSE (first.ok());
    CHECK (second.ok());
}

TEST_CASE ("default setup: save, reset and factory restore", "[licmgr][setup]")
{
    SetupEnv e;
    const auto file = e.processor.getDataDir().getChildFile ("default-setup.json");
    REQUIRE (e.processor.getDataDir() == e.env.dir);
    file.deleteFile();

    e.processor.setPlaceLevel ("kick", -5.0f);
    REQUIRE (e.processor.saveAsDefaultSetup());
    CHECK (file.existsAsFile());

    e.processor.setPlaceLevel ("kick", 3.0f);
    e.processor.resetToDefaultSetup();
    CHECK (e.processor.getSetup().find ("kick")->levelDb == -5.0f);

    e.processor.restoreFactorySetup();
    CHECK_FALSE (file.existsAsFile());
    CHECK (e.processor.getSetup().find ("kick")->levelDb == 0.0f);
    CHECK (e.processor.getSetup().toJson() == Setup::factory().toJson());
}

TEST_CASE ("broken default setup falls back to factory; a new instance takes the saved default", "[licmgr][setup]")
{
    Env env;
    SamplitProcessor::setDataDirOverride (env.dir);
    struct Reset { ~Reset() { SamplitProcessor::setDataDirOverride ({}); } } reset;
    const auto file = env.dir.getChildFile ("default-setup.json");

    {
        auto s = Setup::factory();
        s.places[0].levelDb = -2.0f;
        file.replaceWithText (s.toJson());
        SamplitProcessor p;
        CHECK (p.getSetup().find ("kick")->levelDb == -2.0f);
    }
    {
        file.replaceWithText ("{ not json");
        SamplitProcessor p;
        CHECK (p.getSetup().toJson() == Setup::factory().toJson());
        p.resetToDefaultSetup();
        CHECK (p.getSetup().toJson() == Setup::factory().toJson());
    }
}
