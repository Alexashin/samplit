#pragma once

#include <juce_core/juce_core.h>

#include <map>
#include <vector>

namespace samplit::pack
{
    // Русские строки в коде: JUCE читает обычный литерал как Latin-1, поэтому текст пишем как "..."_s (UTF-8)
    inline juce::String operator""_s (const char* text, size_t length)
    {
        return juce::String::fromUTF8 (text, (int) length);
    }

    // Манифест пакета (docs/pack-format.md). Один тип для исходного манифеста, который читает сборщик
    // (в HitFile задан path), и для хранимого в контейнере (задан chunk).

    struct Mic
    {
        juce::String id, name, role;
        int out = 0;  // выход по умолчанию: 0 = Main, 1..16 = Out N (моно)
        juce::NamedValueSet extra;  // поля, которых нет в схеме: читаются и пишутся как есть
    };

    struct Layer
    {
        juce::String id, artic;
        int velLo = 1, velHi = 127;
        juce::NamedValueSet extra;  // например key, label, pick, peerGroup, unverified от разметки
    };

    // Файл одного микрофона в ударе
    struct HitFile
    {
        juce::String path;     // исходный манифест: путь относительно корня библиотеки
        int chunk = -1;        // хранимый манифест: номер блока с FLAC (целиком файл, либо голова при tailChunk)
        int tailChunk = -1;    // блок с хвостом, если файл разрезан (см. docs/streaming-design.md), иначе -1
        float gainDb = 0.0f;   // громкость зоны
        int start = 0;         // обрезка в отсчётах: первый оставленный отсчёт
        int end = 0;           // обрезка: отсчёт, на котором файл заканчивается (0 = без обрезки)
        int frames = 0;        // хранимый: длина после обрезки
        double rate = 0.0;     // хранимый: частота дискретизации
        int peak = -1;         // хранимый: пик |отсчёта| в 24-битных единицах, −1 = неизвестен, 0 = файл из нулей
        juce::NamedValueSet extra;
    };

    // Один удар: клавиша + окно velocity + файлы по микрофонам. Играются вместе
    struct Hit
    {
        juce::String id, layer, peerGroup;  // peerGroup копирует extra["peerGroup"], если это строка
        int key = 60;
        int velLo = 1, velHi = 127;
        std::map<juce::String, HitFile> files;  // ключ: Mic::id
        juce::NamedValueSet extra;              // например unverified; peerGroup тоже сохраняется здесь
    };

    struct Piece
    {
        juce::String id, name;
        std::vector<int> keys;
        juce::String choke;  // пусто = нет choke-группы
        std::vector<Layer> layers;
        std::vector<Hit> hits;
        juce::NamedValueSet extra;
    };

    struct Manifest
    {
        int format = 1;
        juce::String id, tier, title;
        int version = 1;
        std::vector<juce::String> requiresPacks;
        std::vector<Mic> mics;
        std::vector<Piece> pieces;
        juce::NamedValueSet extra;

        // Разбор JSON. При ошибке возвращает false и текст в error
        static bool fromJson (const juce::String& json, Manifest& out, juce::String& error);

        // Запись JSON. Поля path и chunk пишутся, только если заданы (path непустой, chunk >= 0)
        juce::String toJson() const;

        const Mic* findMic (const juce::String& micId) const;
        int countHits() const;
    };

    // Результат проверки. errors: пакет нельзя собирать или играть. warnings: странности, возможные намеренно
    struct VerifyReport
    {
        std::vector<juce::String> errors, warnings;
        bool ok() const noexcept { return errors.empty(); }
    };

    // Проверка структуры манифеста без обращения к файлам: удары без файлов, неизвестные микрофоны,
    // повторы id, неверные окна velocity, дыры и пересечения velocity внутри клавиши
    VerifyReport verifyManifest (const Manifest&);
}
