#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// Токен лицензии: подписанный файл со сроком действия и ключами пакетов. Формат: docs/pack-format.md.
// Всё здесь вызывается из фонового потока (читает файлы и считает хеши), в audio thread не звать.
namespace samplit::license
{
    using Key32 = std::array<uint8_t, 32>;

    enum class Status
    {
        Valid,
        Expired,
        NotYetValid,
        BadSignature,
        WrongMachine,
        ClockRollback,
        NoToken,
        Malformed
    };

    // Ключ расшифрованного пакета
    struct PackKey
    {
        juce::String packId;
        Key32 key {};
    };

    struct CheckResult
    {
        Status status = Status::NoToken;
        juce::String licenseId;
        juce::String user;
        int64_t notBefore = 0;
        int64_t expires = 0;      // Unix, секунды; 0 = бессрочная лицензия
        bool isPerpetual() const noexcept { return expires == 0; }
        std::vector<PackKey> packs;  // заполнен только при Status::Valid

        bool isValid() const noexcept { return status == Status::Valid; }
    };

    // Особый «отпечаток» служебного токена: принимается на любой машине, только с конечным сроком.
    // Временная мера для передачи демо; ключи пакетов в таком токене не защищены машиной
    inline const char* const kAnyMachineFingerprint = "*";

    struct Options
    {
        Key32 publicKey {};           // публичный ключ издателя
        juce::String fingerprint;     // отпечаток этой машины, см. machineFingerprint()
        int64_t now = 0;              // текущее время, Unix секунды
        juce::File clockStateFile;    // файл с последним увиденным временем; пустой File = защита выключена
        int64_t clockSkewSeconds = 300;      // допуск на notBefore и на небольшое отставание часов
        int64_t expiryGraceSeconds = 0;      // запас после expires
        int64_t rollbackToleranceSeconds = 3600;  // насколько назад можно «откатить» часы без отказа
    };

    // Отпечаток машины. Windows: MachineGuid и серийный номер системного тома. macOS: IOPlatformUUID
    // (пока заглушка). Пустая строка, если получить не удалось
    juce::String machineFingerprint();

    // Публичный идентификатор машины (hex BLAKE2b-256), он лежит в токене открытым текстом
    juce::String machineId (const juce::String& fingerprint);

    // Ключ машины для обёртки ключей пакетов. В токене его нет
    Key32 machineKey (const juce::String& fingerprint);

    // Канонический JSON: ключи по возрастанию, без пробелов, не-ASCII без экранирования. Числа только целые.
    // Те же байты считает Python (tools/license_server/licensecore.py), по ним берётся подпись
    std::string canonicalJson (const juce::var&);

    // Боевой публичный ключ, зашитый в плагин (core/src/LicenseKey.cpp)
    const Key32& embeddedPublicKey();

    // Проверка токена: подпись, машина, часы, срок. Обновляет файл состояния часов
    CheckResult verifyToken (const juce::String& tokenJson, const Options&);
    CheckResult verifyTokenFile (const juce::File& tokenFile, const Options&);

    // Ключ лицензии одной строкой "SPL1-<base64>": тот же токен, что и в файле, только для ввода руками.
    // tokenFromKeyText принимает и ключ, и сырой JSON токена; пробелы и переносы строк игнорирует.
    // Пустая строка, если текст не похож ни на то, ни на другое
    juce::String tokenFromKeyText (const juce::String& text);
    juce::String keyTextFromToken (const juce::String& tokenJson);

    // Обёртка ключа пакета на машину: base64( nonce(24) | шифртекст(32) | тег(16) ).
    // Нужна серверу и тестам, плагин только расшифровывает
    juce::String wrapPackKey (const juce::String& packId, const Key32& packKey, const juce::String& fingerprint,
                              const std::array<uint8_t, 24>& nonce);
    bool unwrapPackKey (const juce::String& packId, const juce::String& wrappedBase64, const juce::String& fingerprint,
                        Key32& out);

    // Подпись токена приватным ключом (64 байта, формат Monocypher/RFC 8032). Для тестов и инструментов, в плагине не нужна
    juce::String signToken (const juce::var& payload, const std::array<uint8_t, 64>& secretKey);
    std::array<uint8_t, 64> secretKeyFromSeed (const Key32& seed, Key32& publicKeyOut);
}
