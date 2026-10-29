#include "samplit/core/LicenseSigning.h"

#include <monocypher-ed25519.h>

namespace samplit::license
{
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
}
