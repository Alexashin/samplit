#include "LicenseManager.h"

#include "samplit/core/Manifest.h"

namespace samplit
{
    using pack::operator""_s;

    LicenseManager::Config LicenseManager::defaultConfig()
    {
        Config c;
        c.dataDir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Samplit");
        c.publicKey = license::embeddedPublicKey();
        c.fingerprint = license::machineFingerprint();
       #ifdef SAMPLIT_LICENSE_URL
        c.serverUrl = SAMPLIT_LICENSE_URL;
       #endif
        return c;
    }

    LicenseManager::LicenseManager (Config c) : config (std::move (c))
    {
        packDir = config.dataDir.getChildFile ("packs");
        check();
        startTimer (3000);      // первая попытка продления сразу после старта, дальше раз в час
    }

    LicenseManager::~LicenseManager()
    {
        stopTimer();
        lifeToken.reset();
        pool.removeAllJobs (true, 12000);
    }

    int64_t LicenseManager::now() const
    {
        return config.now ? config.now() : juce::Time::currentTimeMillis() / 1000;
    }

    license::Options LicenseManager::makeOptions() const
    {
        license::Options o;
        o.publicKey = config.publicKey;
        o.fingerprint = config.fingerprint;
        o.now = now();
        o.clockStateFile = config.dataDir.getChildFile ("clock.state");
        return o;
    }

    LicenseManager::Info LicenseManager::getInfo() const
    {
        const juce::ScopedLock sl (lock);
        Info i;
        i.status = result.status;
        i.user = result.user;
        i.licenseId = result.licenseId;
        i.expires = result.expires;
        i.busy = jobs.load() > 0;
        i.lastError = lastError;
        return i;
    }

    juce::File LicenseManager::getPackDir() const
    {
        const juce::ScopedLock sl (lock);
        return packDir;
    }

    void LicenseManager::setPackDir (const juce::File& dir)
    {
        const juce::ScopedLock sl (lock);
        packDir = dir;
    }

    bool LicenseManager::keyFor (const juce::String& packId, pack::PackKey& out) const
    {
        const juce::ScopedLock sl (lock);
        if (! result.isValid())
            return false;
        for (const auto& k : result.packs)
            if (k.packId == packId)
            {
                out = k.key;
                return true;
            }
        return false;
    }

    // Работа в фоне: work возвращает результат обмена с сервером (или ok без сети), потом токен перечитывается
    void LicenseManager::run (std::function<license::ClientResult (const license::Options&)> work)
    {
        ++jobs;
        std::weak_ptr<int> alive = lifeToken;
        auto options = makeOptions();

        pool.addJob ([this, alive, work = std::move (work), options]
        {
            license::ClientResult net;
            if (work)
                net = work (options);

            // Токен перечитываем в любом случае: при ошибке сети он остаётся прежним
            auto checked = license::verifyTokenFile (getTokenFile(), options);

            juce::MessageManager::callAsync ([this, alive, net, checked = std::move (checked)]
            {
                if (alive.expired())
                    return;
                {
                    const juce::ScopedLock sl (lock);
                    result = checked;
                    lastError = net.ok ? juce::String() : net.error;
                }
                --jobs;
                if (onChange)
                    onChange();
            });
        });
    }

    void LicenseManager::check()
    {
        run ({});
    }

    void LicenseManager::activate (const juce::String& licenseCode)
    {
        license::ClientConfig cc { config.serverUrl, 8000, config.allowInsecureHttp };
        const auto file = getTokenFile();
        run ([cc, licenseCode, file, empty = config.serverUrl.isEmpty()] (const license::Options& o)
        {
            if (empty)
            {
                license::ClientResult r;
                r.error = "Адрес сервера лицензий не задан"_s;
                return r;
            }
            file.getParentDirectory().createDirectory();
            return license::LicenseClient::activate (cc, licenseCode, o, file);
        });
    }

    void LicenseManager::renew()
    {
        lastRenewTry = now();
        license::ClientConfig cc { config.serverUrl, 8000, config.allowInsecureHttp };
        const auto file = getTokenFile();
        run ([cc, file, empty = config.serverUrl.isEmpty()] (const license::Options& o)
        {
            license::ClientResult r;
            if (empty)
            {
                r.ok = true;    // продления нет: токен проверяется как есть
                return r;
            }
            return license::LicenseClient::renew (cc, file, o);
        });
    }

    void LicenseManager::timerCallback()
    {
        startTimer (60 * 60 * 1000);
        const auto status = getInfo().status;
        const bool hasToken = status != license::Status::NoToken && status != license::Status::Malformed;
        if (config.serverUrl.isNotEmpty() && hasToken && now() - lastRenewTry >= (int64_t) config.renewIntervalHours * 3600)
            renew();
    }
}
