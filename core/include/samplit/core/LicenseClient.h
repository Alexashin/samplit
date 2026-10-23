#pragma once

#include "samplit/core/License.h"

// Получение и продление токена у сервера. Вызывать только из фонового потока: функции блокируют до таймаута.
// Ошибки сети не бросаются, они возвращаются в ClientResult. Старый токен заменяется только после того, как
// подпись и срок нового проверены.
namespace samplit::license
{
    struct ClientConfig
    {
        juce::String serverUrl;           // без завершающего слэша, например https://license.example.com
        int timeoutMs = 8000;             // на соединение и на ответ
        bool allowInsecureHttp = false;   // только для локальной отладки сервера
    };

    struct ClientResult
    {
        bool ok = false;
        juce::String error;               // человекочитаемо, пусто при ok
        Status status = Status::NoToken;  // результат проверки полученного токена
        int httpStatus = 0;               // 0, если ответа не было
    };

    class LicenseClient
    {
    public:
        // Первая активация: код лицензии + отпечаток машины, токен сохраняется в tokenFile
        static ClientResult activate (const ClientConfig&, const juce::String& licenseCode, const Options&,
                                      const juce::File& tokenFile);

        // Продление: отправляет текущий токен (он может быть уже просроченным), сохраняет новый
        static ClientResult renew (const ClientConfig&, const juce::File& tokenFile, const Options&);

        // Разбор ответа сервера без сети: проверяет новый токен и атомарно заменяет tokenFile
        static ClientResult applyResponse (const juce::String& responseJson, int httpStatus, const Options&,
                                           const juce::File& tokenFile);
    };
}
