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
        int out = 0;  // выход по умолчанию: 0 = Main, 1..10 = выход плагина (1..7 моно, 8..10 стерео)
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
        int chunk = -1;        // хранимый манифест: номер блока с FLAC (целиком файл, либо голова при tailFirstChunk >= 0)
        int tailFirstChunk = -1;  // файл с хвостом: номер первого блока хвоста (блоки идут подряд), иначе -1
        int headFrames = 0;    // файл с хвостом: длина головы (блок chunk) в кадрах
        int chunkFrames = 0;   // файл с хвостом: длина одного куска хвоста; последний кусок может быть короче
        float gainDb = 0.0f;   // громкость зоны
        int start = 0;         // обрезка в отсчётах: первый оставленный отсчёт
        int end = 0;           // обрезка: отсчёт, на котором файл заканчивается (0 = без обрезки)
        int frames = 0;        // хранимый: длина после обрезки (у файла с хвостом: полная длина голова + хвост)
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

    // Парная группа другого инструмента (данные для помощника, движок не использует): тип или место + id группы
    struct GroupPair
    {
        juce::String type, place, group;
        bool empty() const noexcept { return type.isEmpty() && place.isEmpty() && group.isEmpty(); }
    };

    // Смысловая группа слоёв (docs/setup-spec.md §3.2): то, что видит пользователь вместо сырых слоёв
    struct Group
    {
        juce::String id, name, hint;
        juce::String role = "other";        // из списка ролей; незнакомое значение читается как "other"
        std::vector<juce::String> layers;   // id слоёв барабана
        juce::String pick;                  // random / cycle / exact; пусто = по умолчанию (random)
        std::vector<juce::String> picks;    // какие режимы доступны пользователю; пусто = все три
        GroupPair pair;
    };

    // Известные роли групп
    bool isKnownGroupRole (const juce::String& role);
    // Тип барабана по префиксу id (для старых паков без поля type): kick, snare, tom, hat, ride, crash, china, splash, иначе other
    juce::String deriveDrumType (const juce::String& pieceId);

    struct Piece
    {
        juce::String id, name;
        juce::String type;  // kick | snare | tom | hat | crash | china | splash | ride | other; при разборе выводится по id
        std::vector<Group> groups;
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

    // Число кусков хвоста файла (0, если хвоста нет): ceil ((frames - headFrames) / chunkFrames)
    int tailChunkCount (const HitFile&);

    // Согласованность полей хвоста одного файла хранимого манифеста; true, если всё верно или хвоста нет.
    // chunkCount >= 0: ещё и проверка, что блоки хвоста лежат в таблице блоков пакета
    bool checkTailFields (const HitFile&, int chunkCount, juce::String& error);

    // Результат проверки. errors: пакет нельзя собирать или играть. warnings: странности, возможные намеренно
    struct VerifyReport
    {
        std::vector<juce::String> errors, warnings;
        bool ok() const noexcept { return errors.empty(); }
    };

    // Проверка структуры манифеста без обращения к файлам: удары без файлов, неизвестные микрофоны,
    // повторы id, неверные окна velocity, дыры и пересечения velocity внутри клавиши,
    // группы (слой в двух группах, неизвестный слой, повтор id, пустые поля, pick вне picks)
    VerifyReport verifyManifest (const Manifest&);
}
