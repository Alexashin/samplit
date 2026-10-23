#include "samplit/core/License.h"

#include <monocypher-ed25519.h>
#include <monocypher.h>

#include <algorithm>
#include <cstring>
#include <map>

namespace samplit::license
{
    namespace
    {
        // Метки разделения доменов, те же байты в tools/license_server/licensecore.py
        const std::string kIdDomain = "samplit-machine-id-v1";
        const std::string kKeyDomain = "samplit-machine-key-v1";
        const std::string kPackKeyAd = "samplit-packkey-v1:";

        std::string blake2b32hex (const std::string& domain, const juce::String& text)
        {
            const std::string msg = domain + text.toStdString();
            uint8_t hash[32];
            crypto_blake2b (hash, sizeof hash, reinterpret_cast<const uint8_t*> (msg.data()), msg.size());
            return juce::String::toHexString (hash, sizeof hash, 0).toLowerCase().toStdString();
        }

        void appendEscaped (std::string& out, const std::string& s)
        {
            out += '"';
            for (const unsigned char c : s)
            {
                switch (c)
                {
                    case '"':  out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\b': out += "\\b"; break;
                    case '\f': out += "\\f"; break;
                    case '\n': out += "\\n"; break;
                    case '\r': out += "\\r"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (c < 0x20)
                        {
                            static const char* hex = "0123456789abcdef";
                            out += "\\u00";
                            out += hex[c >> 4];
                            out += hex[c & 15];
                        }
                        else
                        {
                            out += (char) c;
                        }
                }
            }
            out += '"';
        }

        void appendCanonical (std::string& out, const juce::var& v)
        {
            if (v.isVoid())
            {
                out += "null";
            }
            else if (v.isBool())
            {
                out += static_cast<bool> (v) ? "true" : "false";
            }
            else if (v.isInt() || v.isInt64())
            {
                out += juce::String (static_cast<juce::int64> (v)).toStdString();
            }
            else if (v.isDouble())
            {
                // Контракт: числа только целые. Целое значение с плавающей точкой пишем как целое
                const double d = static_cast<double> (v);
                out += juce::String (static_cast<juce::int64> (d)).toStdString();
            }
            else if (v.isString())
            {
                appendEscaped (out, v.toString().toStdString());
            }
            else if (v.isArray())
            {
                out += '[';
                for (int i = 0; i < v.size(); ++i)
                {
                    if (i > 0)
                        out += ',';
                    appendCanonical (out, v[i]);
                }
                out += ']';
            }
            else if (auto* obj = v.getDynamicObject())
            {
                std::map<std::string, juce::var> sorted;  // порядок UTF-8 байтов совпадает с порядком кодовых точек
                for (const auto& prop : obj->getProperties())
                    sorted.emplace (prop.name.toString().toStdString(), prop.value);

                out += '{';
                bool first = true;
                for (const auto& [name, value] : sorted)
                {
                    if (! first)
                        out += ',';
                    first = false;
                    appendEscaped (out, name);
                    out += ':';
                    appendCanonical (out, value);
                }
                out += '}';
            }
            else
            {
                out += "null";
            }
        }

        bool fromBase64 (const juce::String& text, juce::MemoryBlock& out)
        {
            juce::MemoryOutputStream stream (out, false);
            return juce::Base64::convertFromBase64 (stream, text);
        }

        bool readInt64 (const juce::var& v, int64_t& out)
        {
            if (! (v.isInt() || v.isInt64()))
                return false;
            out = static_cast<juce::int64> (v);
            return true;
        }

        int64_t loadLastSeen (const juce::File& file)
        {
            if (file == juce::File() || ! file.existsAsFile())
                return 0;

            const auto parsed = juce::JSON::parse (file.loadFileAsString());
            int64_t seen = 0;
            return readInt64 (parsed.getProperty ("lastSeen", {}), seen) ? seen : 0;
        }

        void storeLastSeen (const juce::File& file, int64_t seen)
        {
            if (file == juce::File())
                return;

            auto* obj = new juce::DynamicObject();
            obj->setProperty ("lastSeen", static_cast<juce::int64> (seen));

            // Запись через временный файл, чтобы обрыв не оставил половинчатое состояние
            file.getParentDirectory().createDirectory();
            const auto tmp = file.getSiblingFile (file.getFileName() + ".tmp");
            if (tmp.replaceWithText (juce::JSON::toString (juce::var (obj), true)))
                tmp.replaceFileIn (file);
        }
    }

    // ---------------------------------------------------------------- машина

    juce::String machineFingerprint()
    {
#if JUCE_WINDOWS
        const auto guid = juce::WindowsRegistry::getValue ("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Cryptography\\MachineGuid",
                                                           {}, juce::WindowsRegistry::WoW64_64bit);
        if (guid.isEmpty())
            return {};

        const auto serial = juce::File::getSpecialLocation (juce::File::windowsSystemDirectory).getVolumeSerialNumber();
        return "win:" + guid.toLowerCase() + ":" + juce::String::toHexString (serial);
#elif JUCE_MAC
        // TODO(macOS): взять IOPlatformUUID через IOKit (IORegistryEntryCreateCFProperty). Пока отпечатка нет,
        // привязать лицензию к машине нельзя
        return {};
#else
        const auto id = juce::File ("/etc/machine-id").loadFileAsString().trim();
        return id.isEmpty() ? juce::String() : "linux:" + id;
#endif
    }

    juce::String machineId (const juce::String& fingerprint)
    {
        return blake2b32hex (kIdDomain, fingerprint);
    }

    Key32 machineKey (const juce::String& fingerprint)
    {
        const std::string msg = kKeyDomain + fingerprint.toStdString();
        Key32 key {};
        crypto_blake2b (key.data(), key.size(), reinterpret_cast<const uint8_t*> (msg.data()), msg.size());
        return key;
    }

    // ---------------------------------------------------------------- JSON и ключи

    std::string canonicalJson (const juce::var& v)
    {
        std::string out;
        appendCanonical (out, v);
        return out;
    }

    juce::String wrapPackKey (const juce::String& packId, const Key32& packKey, const juce::String& fingerprint,
                              const std::array<uint8_t, 24>& nonce)
    {
        const std::string ad = kPackKeyAd + packId.toStdString();
        const auto key = machineKey (fingerprint);

        // nonce | шифртекст | тег
        std::array<uint8_t, 24 + 32 + 16> sealed {};
        std::memcpy (sealed.data(), nonce.data(), nonce.size());
        crypto_aead_lock (sealed.data() + 24, sealed.data() + 24 + 32, key.data(), nonce.data(),
                          reinterpret_cast<const uint8_t*> (ad.data()), ad.size(), packKey.data(), packKey.size());
        return juce::Base64::toBase64 (sealed.data(), sealed.size());
    }

    bool unwrapPackKey (const juce::String& packId, const juce::String& wrappedBase64, const juce::String& fingerprint,
                        Key32& out)
    {
        juce::MemoryBlock raw;
        if (! fromBase64 (wrappedBase64, raw) || raw.getSize() != 24 + 32 + 16)
            return false;

        const auto* bytes = static_cast<const uint8_t*> (raw.getData());
        const std::string ad = kPackKeyAd + packId.toStdString();
        const auto key = machineKey (fingerprint);
        return crypto_aead_unlock (out.data(), bytes + 24 + 32, key.data(), bytes,
                                   reinterpret_cast<const uint8_t*> (ad.data()), ad.size(), bytes + 24, 32) == 0;
    }

    std::array<uint8_t, 64> secretKeyFromSeed (const Key32& seed, Key32& publicKeyOut)
    {
        Key32 seedCopy = seed;  // crypto_ed25519_key_pair затирает сид
        std::array<uint8_t, 64> secret {};
        crypto_ed25519_key_pair (secret.data(), publicKeyOut.data(), seedCopy.data());
        return secret;
    }

    juce::String signToken (const juce::var& payload, const std::array<uint8_t, 64>& secretKey)
    {
        const std::string canon = canonicalJson (payload);
        uint8_t sig[64];
        crypto_ed25519_sign (sig, secretKey.data(), reinterpret_cast<const uint8_t*> (canon.data()), canon.size());

        auto* token = new juce::DynamicObject();
        token->setProperty ("payload", payload);
        token->setProperty ("sig", juce::Base64::toBase64 (sig, sizeof sig));
        return juce::JSON::toString (juce::var (token), true);
    }

    // ---------------------------------------------------------------- проверка

    CheckResult verifyToken (const juce::String& tokenJson, const Options& options)
    {
        CheckResult result;
        if (tokenJson.trim().isEmpty())
        {
            result.status = Status::NoToken;
            return result;
        }

        auto fail = [&result] (Status status)
        {
            result.status = status;
            return result;
        };

        const auto token = juce::JSON::parse (tokenJson);
        const auto payload = token.getProperty ("payload", {});
        const auto sigText = token.getProperty ("sig", {});
        if (! payload.isObject() || ! sigText.isString())
            return fail (Status::Malformed);

        juce::MemoryBlock sig;
        if (! fromBase64 (sigText.toString(), sig) || sig.getSize() != 64)
            return fail (Status::Malformed);

        // Подпись считается по каноническому виду payload: пробелы и порядок ключей в файле не важны
        const std::string canon = canonicalJson (payload);
        if (crypto_ed25519_check (static_cast<const uint8_t*> (sig.getData()), options.publicKey.data(),
                                  reinterpret_cast<const uint8_t*> (canon.data()), canon.size()) != 0)
            return fail (Status::BadSignature);

        // Подпись верна, значит поля написали мы. Но на всякий случай проверяем форму
        const auto licenseId = payload.getProperty ("licenseId", {});
        const auto user = payload.getProperty ("user", {});
        const auto machine = payload.getProperty ("machine", {});
        const auto packs = payload.getProperty ("packs", {});
        int64_t notBefore = 0, expires = 0;
        if (! licenseId.isString() || ! user.isString() || ! machine.isString() || ! packs.isObject()
            || ! readInt64 (payload.getProperty ("notBefore", {}), notBefore)
            || ! readInt64 (payload.getProperty ("expires", {}), expires))
            return fail (Status::Malformed);

        result.licenseId = licenseId.toString();
        result.user = user.toString();
        result.notBefore = notBefore;
        result.expires = expires;

        // expires == 0: бессрочная лицензия. Начало, срок и перевод часов для неё не проверяются
        const bool perpetual = expires == 0;

        // Часы: не пускаем назад дальше допуска, иначе обновляем «последнее увиденное»
        const auto lastSeen = loadLastSeen (options.clockStateFile);
        if (! perpetual && options.now + options.rollbackToleranceSeconds < lastSeen)
            return fail (Status::ClockRollback);
        if (options.now > lastSeen)
            storeLastSeen (options.clockStateFile, options.now);

        if (options.fingerprint.isEmpty() || machine.toString() != machineId (options.fingerprint))
            return fail (Status::WrongMachine);

        if (! perpetual && options.now + options.clockSkewSeconds < notBefore)
            return fail (Status::NotYetValid);
        if (! perpetual && options.now > expires + options.expiryGraceSeconds)
            return fail (Status::Expired);

        std::vector<PackKey> keys;
        for (const auto& prop : packs.getDynamicObject()->getProperties())
        {
            PackKey pk;
            pk.packId = prop.name.toString();
            if (! prop.value.isString() || ! unwrapPackKey (pk.packId, prop.value.toString(), options.fingerprint, pk.key))
                return fail (Status::Malformed);
            keys.push_back (pk);
        }
        std::sort (keys.begin(), keys.end(), [] (const PackKey& a, const PackKey& b) { return a.packId < b.packId; });

        result.packs = std::move (keys);
        result.status = Status::Valid;
        return result;
    }

    CheckResult verifyTokenFile (const juce::File& tokenFile, const Options& options)
    {
        if (! tokenFile.existsAsFile())
            return {};  // NoToken
        return verifyToken (tokenFile.loadFileAsString(), options);
    }
}
