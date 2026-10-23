#include "samplit/core/License.h"
#include "samplit/core/LicenseClient.h"

#include <catch2/catch_test_macros.hpp>

using namespace samplit::license;

namespace
{
    constexpr int64_t kNow = 1'800'000'000;
    constexpr int64_t kDay = 86400;

    // ТЕСТОВЫЙ сид, тот же, что в tools/license_server/make_fixture.py. Боевого секрета здесь нет
    Key32 testSeed()
    {
        Key32 s {};
        for (size_t i = 0; i < s.size(); ++i)
            s[i] = (uint8_t) i;
        return s;
    }

    Key32 otherSeed()
    {
        Key32 s {};
        for (size_t i = 0; i < s.size(); ++i)
            s[i] = (uint8_t) (200 - i);
        return s;
    }

    struct Keys
    {
        explicit Keys (const Key32& seed) { secret = secretKeyFromSeed (seed, pub); }
        Key32 pub {};
        std::array<uint8_t, 64> secret {};
    };

    const juce::String kFingerprint = "win:unit-test-guid:00c0ffee";

    Key32 packKey (uint8_t base)
    {
        Key32 k {};
        for (size_t i = 0; i < k.size(); ++i)
            k[i] = (uint8_t) (base + i);
        return k;
    }

    std::array<uint8_t, 24> nonce (uint8_t v)
    {
        std::array<uint8_t, 24> n {};
        n.fill (v);
        return n;
    }

    struct PayloadSpec
    {
        int64_t notBefore = kNow - kDay;
        int64_t expires = kNow + 7 * kDay;
        juce::String fingerprint = kFingerprint;
        bool dropPacks = false;
    };

    juce::var makePayload (const PayloadSpec& spec = {})
    {
        auto* packs = new juce::DynamicObject();
        packs->setProperty ("kick", wrapPackKey ("kick", packKey (10), kFingerprint, nonce (1)));
        packs->setProperty ("cymbals", wrapPackKey ("cymbals", packKey (50), kFingerprint, nonce (2)));

        auto* p = new juce::DynamicObject();
        p->setProperty ("licenseId", "L-1");
        p->setProperty ("user", juce::String::fromUTF8 ("Артём"));
        p->setProperty ("machine", machineId (spec.fingerprint));
        p->setProperty ("notBefore", (juce::int64) spec.notBefore);
        p->setProperty ("expires", (juce::int64) spec.expires);
        if (! spec.dropPacks)
            p->setProperty ("packs", juce::var (packs));
        else
            delete packs;
        return juce::var (p);
    }

    Options optionsFor (const Keys& keys, const juce::String& fingerprint = kFingerprint, int64_t now = kNow)
    {
        Options o;
        o.publicKey = keys.pub;
        o.fingerprint = fingerprint;
        o.now = now;
        return o;
    }

    juce::File tempDir()
    {
        auto d = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("samplit-license-tests");
        d.createDirectory();
        return d;
    }

    juce::String hex (const Key32& k)
    {
        return juce::String::toHexString (k.data(), (int) k.size(), 0);
    }
}

TEST_CASE ("canonical JSON sorts keys, drops spaces and escapes like Python", "[license]")
{
    auto* inner = new juce::DynamicObject();
    inner->setProperty ("z", 1);
    inner->setProperty ("a", juce::var (false));

    auto* o = new juce::DynamicObject();
    o->setProperty (juce::Identifier (juce::String::fromUTF8 ("é")), juce::Array<juce::var> { juce::var (true), juce::var(), (juce::int64) 3'000'000'000 });
    o->setProperty ("b", juce::var (inner));
    o->setProperty ("a", juce::String::fromUTF8 ("\"q\"\\\n\t\x01 я"));

    const auto text = canonicalJson (juce::var (o));
    REQUIRE (text == "{\"a\":\"\\\"q\\\"\\\\\\n\\t\\u0001 \xd1\x8f\",\"b\":{\"a\":false,\"z\":1},\"\xc3\xa9\":[true,null,3000000000]}");
}

TEST_CASE ("a valid token gives its pack keys", "[license]")
{
    Keys keys (testSeed());
    const auto token = signToken (makePayload(), keys.secret);

    const auto r = verifyToken (token, optionsFor (keys));
    REQUIRE (r.status == Status::Valid);
    REQUIRE (r.licenseId == "L-1");
    REQUIRE (r.user == juce::String::fromUTF8 ("Артём"));
    REQUIRE (r.expires == kNow + 7 * kDay);
    REQUIRE (r.packs.size() == 2);
    REQUIRE (r.packs[0].packId == "cymbals");
    REQUIRE (r.packs[0].key == packKey (50));
    REQUIRE (r.packs[1].packId == "kick");
    REQUIRE (r.packs[1].key == packKey (10));
}

TEST_CASE ("whitespace and key order in the token file do not matter", "[license]")
{
    Keys keys (testSeed());
    const auto token = signToken (makePayload(), keys.secret);
    const auto parsed = juce::JSON::parse (token);

    // Переписываем компактно и в другом порядке: подпись считается по каноническому виду
    const auto compact = "{\"sig\":" + juce::JSON::toString (parsed.getProperty ("sig", {}), true) + ",\"payload\":"
                         + juce::JSON::toString (parsed.getProperty ("payload", {}), true) + "}";
    REQUIRE (verifyToken (compact, optionsFor (keys)).status == Status::Valid);
}

TEST_CASE ("no token and malformed tokens", "[license]")
{
    Keys keys (testSeed());
    const auto o = optionsFor (keys);

    REQUIRE (verifyToken ("", o).status == Status::NoToken);
    REQUIRE (verifyToken ("   \n", o).status == Status::NoToken);
    REQUIRE (verifyTokenFile (tempDir().getChildFile ("missing-token.json"), o).status == Status::NoToken);

    REQUIRE (verifyToken ("not json at all", o).status == Status::Malformed);
    REQUIRE (verifyToken ("{\"sig\":\"AAAA\"}", o).status == Status::Malformed);
    REQUIRE (verifyToken ("{\"payload\":{},\"sig\":\"%%%\"}", o).status == Status::Malformed);
    REQUIRE (verifyToken ("{\"payload\":{},\"sig\":\"AAAA\"}", o).status == Status::Malformed);  // подпись не 64 байта

    // Подпись верна, но в payload нет packs
    PayloadSpec spec;
    spec.dropPacks = true;
    REQUIRE (verifyToken (signToken (makePayload (spec), keys.secret), o).status == Status::Malformed);
}

TEST_CASE ("changing a payload byte breaks the signature", "[license]")
{
    Keys keys (testSeed());
    auto token = signToken (makePayload(), keys.secret);
    REQUIRE (token.contains ("\"L-1\""));
    token = token.replace ("\"L-1\"", "\"L-2\"");

    REQUIRE (verifyToken (token, optionsFor (keys)).status == Status::BadSignature);
}

TEST_CASE ("a token signed by another key is rejected", "[license]")
{
    Keys mine (testSeed()), other (otherSeed());
    const auto token = signToken (makePayload(), other.secret);

    REQUIRE (verifyToken (token, optionsFor (mine)).status == Status::BadSignature);
}

TEST_CASE ("a token does not work on another machine", "[license]")
{
    Keys keys (testSeed());
    const auto token = signToken (makePayload(), keys.secret);

    REQUIRE (verifyToken (token, optionsFor (keys, "win:another-guid:11223344")).status == Status::WrongMachine);
    REQUIRE (verifyToken (token, optionsFor (keys, "")).status == Status::WrongMachine);
}

TEST_CASE ("pack keys do not open for someone who knows only the machine id", "[license]")
{
    // machine id лежит в токене открыто, но ключ машины из него не получить
    const auto id = machineId (kFingerprint);
    Key32 out {};
    const auto wrapped = wrapPackKey ("kick", packKey (7), kFingerprint, nonce (9));

    REQUIRE (unwrapPackKey ("kick", wrapped, kFingerprint, out));
    REQUIRE (out == packKey (7));
    REQUIRE_FALSE (unwrapPackKey ("kick", wrapped, id, out));                // отпечаток подменили хешем
    REQUIRE_FALSE (unwrapPackKey ("cymbals", wrapped, kFingerprint, out));   // другой packId
    REQUIRE_FALSE (unwrapPackKey ("kick", "AAAA", kFingerprint, out));       // обрезано
}

TEST_CASE ("expiry, grace and not-before with clock skew", "[license]")
{
    Keys keys (testSeed());

    SECTION ("expired")
    {
        PayloadSpec spec;
        spec.expires = kNow - 10;
        const auto token = signToken (makePayload (spec), keys.secret);
        REQUIRE (verifyToken (token, optionsFor (keys)).status == Status::Expired);

        auto o = optionsFor (keys);
        o.expiryGraceSeconds = 3600;
        REQUIRE (verifyToken (token, o).status == Status::Valid);
    }
    SECTION ("exactly at expiry is still valid")
    {
        PayloadSpec spec;
        spec.expires = kNow;
        REQUIRE (verifyToken (signToken (makePayload (spec), keys.secret), optionsFor (keys)).status == Status::Valid);
    }
    SECTION ("not yet valid")
    {
        PayloadSpec spec;
        spec.notBefore = kNow + kDay;
        spec.expires = kNow + 8 * kDay;
        REQUIRE (verifyToken (signToken (makePayload (spec), keys.secret), optionsFor (keys)).status == Status::NotYetValid);
    }
    SECTION ("clock slightly behind the server is tolerated")
    {
        PayloadSpec spec;
        spec.notBefore = kNow + 120;  // сервер чуть впереди локальных часов
        spec.expires = kNow + 7 * kDay;
        REQUIRE (verifyToken (signToken (makePayload (spec), keys.secret), optionsFor (keys)).status == Status::Valid);
    }
}

TEST_CASE ("rolling the clock back is detected", "[license]")
{
    Keys keys (testSeed());
    const auto token = signToken (makePayload(), keys.secret);
    const auto state = tempDir().getChildFile ("clock-state.json");
    state.deleteFile();

    auto at = [&] (int64_t now)
    {
        auto o = optionsFor (keys, kFingerprint, now);
        o.clockStateFile = state;
        return verifyToken (token, o).status;
    };

    REQUIRE (at (kNow) == Status::Valid);
    REQUIRE (at (kNow + 100) == Status::Valid);
    REQUIRE (at (kNow + 100 - 1800) == Status::Valid);        // небольшое отставание часов в пределах допуска
    REQUIRE (at (kNow + 100 - 2 * 3600) == Status::ClockRollback);
    REQUIRE (at (kNow - kDay) == Status::ClockRollback);
    REQUIRE (at (kNow + 200) == Status::Valid);                // время вернулось вперёд, запись не испорчена

    state.deleteFile();
    REQUIRE (at (kNow - kDay) == Status::Valid);               // без файла состояния откат не виден
}

TEST_CASE ("rollback check does not apply to forged tokens", "[license]")
{
    Keys keys (testSeed());
    const auto state = tempDir().getChildFile ("clock-state-2.json");
    state.replaceWithText ("{\"lastSeen\": 1900000000}");

    auto o = optionsFor (keys);
    o.clockStateFile = state;
    REQUIRE (verifyToken ("garbage", o).status == Status::Malformed);
    REQUIRE (state.loadFileAsString().contains ("1900000000"));
}

TEST_CASE ("token signed by Python verifies in C++", "[license]")
{
    const auto file = juce::File (SAMPLIT_TEST_FIXTURES).getChildFile ("license_fixture.json");
    REQUIRE (file.existsAsFile());
    const auto fixture = juce::JSON::parse (file.loadFileAsString());
    REQUIRE (fixture.isObject());

    juce::MemoryBlock pubBytes;
    pubBytes.loadFromHexString (fixture["publicKeyHex"].toString());
    REQUIRE (pubBytes.getSize() == 32);

    Options o;
    std::memcpy (o.publicKey.data(), pubBytes.getData(), 32);
    o.fingerprint = fixture["fingerprint"].toString();
    o.now = (int64_t) (juce::int64) fixture["now"];

    // Те же байты, что получила подпись на стороне Python
    const auto token = fixture["token"];
    REQUIRE (canonicalJson (token["payload"]) == fixture["canonicalPayload"].toString().toStdString());
    REQUIRE (machineId (o.fingerprint) == fixture["machineId"].toString());
    REQUIRE (o.publicKey != embeddedPublicKey());  // в плагин зашит боевой ключ, тестовый туда не попадает

    const auto r = verifyToken (juce::JSON::toString (token, true), o);
    REQUIRE (r.status == Status::Valid);
    REQUIRE (r.user == juce::String::fromUTF8 ("Тестовый пользователь \"Q\""));
    REQUIRE (r.packs.size() == 2);

    for (const auto& pk : r.packs)
        REQUIRE (hex (pk.key) == fixture["packKeysHex"].getProperty (juce::Identifier (pk.packId), {}).toString());

    SECTION ("tampered fixture is rejected")
    {
        auto text = juce::JSON::toString (token, true).replace ("L-FIXTURE-1", "L-FIXTURE-2");
        REQUIRE (verifyToken (text, o).status == Status::BadSignature);
    }
}

TEST_CASE ("token issued by the license server code verifies in C++", "[license]")
{
    const auto file = juce::File (SAMPLIT_TEST_FIXTURES).getChildFile ("license_server_fixture.json");
    REQUIRE (file.existsAsFile());
    const auto fixture = juce::JSON::parse (file.loadFileAsString());

    juce::MemoryBlock pubBytes;
    pubBytes.loadFromHexString (fixture["publicKeyHex"].toString());
    REQUIRE (pubBytes.getSize() == 32);

    Options o;
    std::memcpy (o.publicKey.data(), pubBytes.getData(), 32);
    o.fingerprint = fixture["fingerprint"].toString();
    const int64_t issuedAt = (juce::int64) fixture["issuedAt"];
    const int64_t valid = (juce::int64) fixture["validSeconds"];
    const auto token = juce::JSON::toString (fixture["token"], true);

    o.now = issuedAt + kDay;
    const auto r = verifyToken (token, o);
    REQUIRE (r.status == Status::Valid);
    REQUIRE (r.notBefore == issuedAt);
    REQUIRE (r.expires == issuedAt + valid);
    REQUIRE (r.packs.size() == 2);
    for (const auto& pk : r.packs)
        REQUIRE (hex (pk.key) == fixture["packKeysHex"].getProperty (juce::Identifier (pk.packId), {}).toString());

    o.now = issuedAt + valid + 1;
    REQUIRE (verifyToken (token, o).status == Status::Expired);

    o.now = issuedAt + kDay;
    o.fingerprint = "win:someone-else:1";
    REQUIRE (verifyToken (token, o).status == Status::WrongMachine);
}

TEST_CASE ("machine fingerprint is stable and hashed to a 64-char id", "[license]")
{
    const auto a = machineFingerprint();
    const auto b = machineFingerprint();
    REQUIRE (a == b);
#if JUCE_WINDOWS
    REQUIRE (a.startsWith ("win:"));
#endif
    REQUIRE (machineId (kFingerprint).length() == 64);
    REQUIRE (machineKey (kFingerprint) != machineKey ("other"));
}

TEST_CASE ("client replaces the token only when the new one verifies", "[license]")
{
    Keys keys (testSeed()), other (otherSeed());
    const auto file = tempDir().getChildFile ("client-token.json");
    auto o = optionsFor (keys);

    const auto oldToken = signToken (makePayload(), keys.secret);
    file.replaceWithText (oldToken);

    auto respond = [] (const juce::String& tokenText)
    {
        return "{\"token\":" + tokenText + "}";
    };

    SECTION ("good token replaces the old one")
    {
        PayloadSpec spec;
        spec.expires = kNow + 30 * kDay;
        const auto fresh = signToken (makePayload (spec), keys.secret);

        const auto r = LicenseClient::applyResponse (respond (fresh), 200, o, file);
        REQUIRE (r.ok);
        REQUIRE (r.status == Status::Valid);
        REQUIRE (verifyTokenFile (file, o).expires == kNow + 30 * kDay);
        REQUIRE_FALSE (file.getSiblingFile (file.getFileName() + ".tmp").exists());
    }
    SECTION ("token with a foreign signature leaves the old file intact")
    {
        const auto forged = signToken (makePayload(), other.secret);
        const auto r = LicenseClient::applyResponse (respond (forged), 200, o, file);
        REQUIRE_FALSE (r.ok);
        REQUIRE (r.status == Status::BadSignature);
        REQUIRE (file.loadFileAsString() == oldToken);
    }
    SECTION ("expired token is not saved")
    {
        PayloadSpec spec;
        spec.expires = kNow - 5;
        const auto stale = signToken (makePayload (spec), keys.secret);
        const auto r = LicenseClient::applyResponse (respond (stale), 200, o, file);
        REQUIRE_FALSE (r.ok);
        REQUIRE (r.status == Status::Expired);
        REQUIRE (file.loadFileAsString() == oldToken);
    }
    SECTION ("server error is reported as a value")
    {
        const auto r = LicenseClient::applyResponse ("{\"error\":\"license revoked\"}", 403, o, file);
        REQUIRE_FALSE (r.ok);
        REQUIRE (r.error == "license revoked");
        REQUIRE (r.httpStatus == 403);
        REQUIRE (file.loadFileAsString() == oldToken);
    }
    SECTION ("garbage response")
    {
        const auto r = LicenseClient::applyResponse ("<html>502</html>", 502, o, file);
        REQUIRE_FALSE (r.ok);
        REQUIRE (file.loadFileAsString() == oldToken);
    }
    SECTION ("a new token is created next to a missing file")
    {
        const auto fresh = tempDir().getChildFile ("new-dir").getChildFile ("token.json");
        fresh.getParentDirectory().deleteRecursively();
        const auto r = LicenseClient::applyResponse (respond (oldToken), 200, o, fresh);
        REQUIRE (r.ok);
        REQUIRE (fresh.existsAsFile());
    }
}

TEST_CASE ("client refuses plain http and works without a network call on bad input", "[license]")
{
    Keys keys (testSeed());
    const auto file = tempDir().getChildFile ("client-token-2.json");
    file.replaceWithText (signToken (makePayload(), keys.secret));
    const auto o = optionsFor (keys);

    ClientConfig config;
    config.serverUrl = "http://license.example.com";
    const auto r = LicenseClient::renew (config, file, o);
    REQUIRE_FALSE (r.ok);
    REQUIRE (r.error.contains ("https"));

    file.deleteFile();
    config.serverUrl = "https://license.example.com";
    REQUIRE (LicenseClient::renew (config, file, o).status == Status::NoToken);

    file.replaceWithText ("{{{");
    REQUIRE (LicenseClient::renew (config, file, o).status == Status::Malformed);

    auto noFingerprint = o;
    noFingerprint.fingerprint = {};
    REQUIRE_FALSE (LicenseClient::activate (config, "CODE", noFingerprint, file).ok);
}
