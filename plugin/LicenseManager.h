#pragma once

#include "samplit/core/LicenseClient.h"
#include "samplit/core/Pack.h"

#include <juce_events/juce_events.h>

#include <atomic>
#include <functional>
#include <memory>

namespace samplit
{
    // Лицензия в плагине: читает и проверяет токен, продлевает его в фоне, выдаёт ключи пакетов загрузчику.
    // Проверка и сеть идут в отдельном потоке и звук не трогают. Без токена, с просроченным токеном и с токеном
    // другой машины ключей нет: пакеты не открываются (демо без пакетов), сами файлы остаются зашифрованными.
    //
    // Методы check/activate/renew/getInfo/onChange: message thread. keyFor можно звать из любого потока.
    class LicenseManager : private juce::Timer
    {
    public:
        struct Config
        {
            juce::File dataDir;                 // токен, состояние часов, папка пакетов по умолчанию
            license::Key32 publicKey {};
            juce::String fingerprint;           // отпечаток машины
            juce::String serverUrl;             // пусто: продления нет, токен только проверяется
            bool allowInsecureHttp = false;
            std::function<int64_t()> now;       // Unix, секунды; пусто: системные часы
            int renewIntervalHours = 24;        // как часто пробовать продлить, пока плагин открыт
        };

        // Каталог данных пользователя, встроенный ключ и отпечаток этой машины. Адрес сервера берётся из
        // SAMPLIT_LICENSE_URL (определение при сборке), по умолчанию пустой
        static Config defaultConfig();

        explicit LicenseManager (Config);
        ~LicenseManager() override;

        struct Info
        {
            license::Status status = license::Status::NoToken;
            juce::String user, licenseId;
            int64_t expires = 0;
            bool busy = false;                  // идёт проверка или обмен с сервером
            juce::String lastError;             // последняя ошибка сети или сервера, пусто если всё хорошо
            bool isValid() const noexcept { return status == license::Status::Valid; }
        };

        Info getInfo() const;
        juce::File getTokenFile() const { return config.dataDir.getChildFile ("license.token"); }
        juce::File getPackDir() const;
        juce::File getPresetDir() const { return config.dataDir.getChildFile ("presets"); }
        void setPackDir (const juce::File&);

        // Локальная библиотека (проверка на своих WAV без пакетов и лицензии): папка с сэмплами и папка исходных
        // манифестов <id пакета>.json. Путь хранится в library.path рядом с токеном; пусто: обычный режим по пакетам
        juce::File getLibraryRoot() const;
        void setLibraryRoot (const juce::File&);
        juce::File getManifestDir() const { return config.dataDir.getChildFile ("manifests"); }

        // Ключ пакета из действующего токена. false: токена нет, он недействителен или пакета в нём нет
        bool keyFor (const juce::String& packId, pack::PackKey& out) const;

        // Перечитать и проверить токен с диска
        void check();

        // Первая активация по коду лицензии и продление текущего токена. Нужна сеть
        void activate (const juce::String& licenseCode);
        void renew();

        // Код этого компьютера (отпечаток): по нему выпускают файл лицензии без сервера
        juce::String getMachineCode() const { return config.fingerprint; }

        // Локальная лицензия без сервера (временно, до запуска сервера; в проде SAMPLIT_OFFLINE_LICENSE=OFF).
        // Проверяет присланный файл: подпись, этот компьютер, срок. Годный файл заменяет текущий токен,
        // негодный не трогает его, причина попадает в Info::lastError
        void importTokenFile (const juce::File&);

        // То же, но из текста: ключ "SPL1-…" одной строкой или содержимое файла лицензии. Годный ключ
        // сохраняется в папку данных плагина, исходный файл после этого не нужен
        void importTokenText (const juce::String&);

        // Зовётся в message thread после каждого изменения Info (проверка, активация, продление)
        std::function<void()> onChange;

    private:
        void timerCallback() override;
        void run (std::function<license::ClientResult (const license::Options&)> work);
        license::Options makeOptions() const;
        int64_t now() const;

        Config config;
        juce::File packDir;
        juce::File libraryRoot;

        mutable juce::CriticalSection lock;
        license::CheckResult result;
        juce::String lastError;
        std::atomic<int> jobs { 0 };
        int64_t lastRenewTry = 0;           // message thread

        std::shared_ptr<int> lifeToken = std::make_shared<int> (0);
        juce::ThreadPool pool { juce::ThreadPoolOptions{}.withThreadName ("Samplit license").withNumberOfThreads (1) };
    };
}
