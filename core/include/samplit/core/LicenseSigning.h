#pragma once

#include "samplit/core/License.h"

// Подпись токенов приватным ключом. Только для тестов и инструментов (samplit_signing), в плагин не линкуется
namespace samplit::license
{
    // Подпись токена приватным ключом (64 байта, формат Monocypher/RFC 8032)
    juce::String signToken (const juce::var& payload, const std::array<uint8_t, 64>& secretKey);
    std::array<uint8_t, 64> secretKeyFromSeed (const Key32& seed, Key32& publicKeyOut);
}
