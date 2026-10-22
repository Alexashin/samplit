#pragma once

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <vector>

struct sqlite3;

namespace samplit
{
    struct SampleInfo
    {
        juce::int64 id = 0;
        juce::File file;
        juce::String name;
        double duration = 0.0;
        int sampleRate = 0;
        int channels = 0;
    };

    struct ScanResult
    {
        int added = 0, updated = 0, removed = 0, unchanged = 0, failed = 0;
    };

    // Индекс сэмплов в SQLite. Один объект на одно соединение: из нескольких потоков вызывать нельзя,
    // фоновое сканирование делается на отдельном экземпляре
    class Library
    {
    public:
        // Открывает базу, создаёт схему, если её нет. При ошибке возвращает nullptr и текст в error
        static std::unique_ptr<Library> open (const juce::File& dbFile, juce::String& error);

        ~Library();
        Library (const Library&) = delete;
        Library& operator= (const Library&) = delete;

        // Рекурсивно обходит папку и приводит индекс в соответствие с диском.
        // Файл с тем же размером и mtime не открывается. shouldCancel опрашивается между файлами
        ScanResult scanFolder (const juce::File& folder, std::function<bool()> shouldCancel = {});

        // Каждое слово запроса должно встретиться в имени или пути (без учёта регистра). Пустой запрос: всё по пути
        std::vector<SampleInfo> search (const juce::String& query, int limit = 500) const;

        int count() const;

    private:
        explicit Library (sqlite3* handle);

        sqlite3* db = nullptr;
    };
}
