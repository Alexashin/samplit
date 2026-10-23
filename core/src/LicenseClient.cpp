#include "samplit/core/LicenseClient.h"

namespace samplit::license
{
    namespace
    {
        ClientResult failure (const juce::String& error, int httpStatus = 0, Status status = Status::NoToken)
        {
            ClientResult r;
            r.error = error;
            r.httpStatus = httpStatus;
            r.status = status;
            return r;
        }

        ClientResult post (const ClientConfig& config, const juce::String& path, const juce::var& body,
                           const Options& options, const juce::File& tokenFile)
        {
            const auto base = config.serverUrl.trimEnd().trimCharactersAtEnd ("/");
            const bool https = base.startsWithIgnoreCase ("https://");
            if (! https && ! (config.allowInsecureHttp && base.startsWithIgnoreCase ("http://")))
                return failure ("Адрес сервера лицензий должен начинаться с https://");

            int httpStatus = 0;
            const auto url = juce::URL (base + path).withPOSTData (juce::JSON::toString (body, true));
            auto stream = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                                                     .withHttpRequestCmd ("POST")
                                                     .withExtraHeaders ("Content-Type: application/json\r\n")
                                                     .withConnectionTimeoutMs (config.timeoutMs)
                                                     .withNumRedirectsToFollow (0)
                                                     .withStatusCode (&httpStatus));
            if (stream == nullptr)
                return failure ("Нет связи с сервером лицензий", httpStatus);

            return LicenseClient::applyResponse (stream->readEntireStreamAsString(), httpStatus, options, tokenFile);
        }
    }

    ClientResult LicenseClient::applyResponse (const juce::String& responseJson, int httpStatus, const Options& options,
                                               const juce::File& tokenFile)
    {
        const auto response = juce::JSON::parse (responseJson);
        if (! response.isObject())
            return failure ("Сервер вернул не JSON", httpStatus, Status::Malformed);

        const auto error = response.getProperty ("error", {});
        if (error.isString())
            return failure (error.toString(), httpStatus);

        const auto token = response.getProperty ("token", {});
        if (! token.isObject())
            return failure ("В ответе сервера нет токена", httpStatus, Status::Malformed);

        const auto tokenText = juce::JSON::toString (token, true);
        const auto check = verifyToken (tokenText, options);
        if (check.status != Status::Valid)
        {
            ClientResult r = failure ("Полученный токен не прошёл проверку", httpStatus, check.status);
            return r;
        }

        // Временный файл рядом с токеном, затем замена: оборванная запись не испортит рабочий токен
        tokenFile.getParentDirectory().createDirectory();
        const auto tmp = tokenFile.getSiblingFile (tokenFile.getFileName() + ".tmp");
        if (! tmp.replaceWithText (tokenText) || ! tmp.replaceFileIn (tokenFile))
        {
            tmp.deleteFile();
            return failure ("Не удалось сохранить токен", httpStatus, check.status);
        }

        ClientResult ok;
        ok.ok = true;
        ok.status = check.status;
        ok.httpStatus = httpStatus;
        return ok;
    }

    ClientResult LicenseClient::activate (const ClientConfig& config, const juce::String& licenseCode,
                                          const Options& options, const juce::File& tokenFile)
    {
        if (options.fingerprint.isEmpty())
            return failure ("Не удалось определить отпечаток машины");

        auto* body = new juce::DynamicObject();
        body->setProperty ("code", licenseCode.trim());
        body->setProperty ("fingerprint", options.fingerprint);
        return post (config, "/activate", juce::var (body), options, tokenFile);
    }

    ClientResult LicenseClient::renew (const ClientConfig& config, const juce::File& tokenFile, const Options& options)
    {
        if (! tokenFile.existsAsFile())
            return failure ("Токена нет, нужна активация", 0, Status::NoToken);

        const auto token = juce::JSON::parse (tokenFile.loadFileAsString());
        if (! token.isObject())
            return failure ("Файл токена повреждён", 0, Status::Malformed);

        auto* body = new juce::DynamicObject();
        body->setProperty ("token", token);
        return post (config, "/renew", juce::var (body), options, tokenFile);
    }
}
