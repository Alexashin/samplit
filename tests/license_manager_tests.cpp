#include "KitLoader.h"
#include "LicenseManager.h"
#include "PluginProcessor.h"

#include <catch2/catch_test_macros.hpp>

using namespace samplit;
using samplit::license::Status;

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
