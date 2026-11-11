#pragma once

#include "Kit.h"

#include "samplit/core/Pack.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace samplit
{
    // Режим выбора удара в JSON (манифест, установка): "random", "cycle", "exact"
    bool pickFromString (const juce::String&, Pick& out);
    juce::String pickToString (Pick);

    struct KitLoadOptions
    {
        juce::File packDir;     // папка с *.splpack
        // Режим «локальная библиотека» (для проверки на своих сэмплах): если задана libraryRoot, звук берётся прямо из WAV
        // этой папки (та же структура, что у sample_library/00-Studio600ru-Drum-LIB), целиком и без обрезки, а раскладка
        // из исходных манифестов <manifestDir>/<id пакета>.json. Пакеты и лицензия не нужны
        juce::File libraryRoot;
        juce::File manifestDir;
        // Лицензия отложена: пакеты .splpack не ищутся, без libraryRoot загрузка сразу отказывает с просьбой выбрать папку
        bool requireLibrary = false;
        // Локальная библиотека: в памяти остаётся голова каждого длинного 24-битного моно WAV (столько секунд), а хвост
        // читается с диска во время игры (DiskStreamer). 0: файлы целиком в память
        double headSeconds = 0.35;
        // Ключ пакета по его id (из токена лицензии). false: лицензии на пакет нет
        std::function<bool (const juce::String& packId, pack::PackKey& key)> keyFor;
        const std::atomic<bool>* cancel = nullptr;      // true: загрузка прерывается
        std::function<void (float)> progress;           // 0..1, зовётся из рабочих потоков
        int threads = 0;                                // 0: по числу ядер
        // Текущий кит: его клипы (по DecodedClip::sourceKey) берутся без повторного декодирования
        std::shared_ptr<const Kit> reuse;
    };

    // Состав кита по установке (docs/setup-spec.md §3.3): что играет на каких местах. Выключенных мест здесь нет
    struct KitSpec
    {
        struct Place
        {
            int slot = 0;
            std::string placeId;
            juce::String packId, pieceId;           // pieceId пуст: единственный барабан типа места
            std::map<int, int> keyMap;              // клавиша барабана -> клавиша места
            std::map<std::string, Pick> picks;      // id группы -> выбор пользователя
        };

        std::vector<Place> places;
        bool includeUnverified = false;
        HitSelect mode = HitSelect::Live;
    };

    struct KitLoadResult
    {
        std::shared_ptr<const Kit> kit;     // nullptr, если загрузка не удалась или отменена
        std::vector<juce::String> missingPacks;     // пакеты установки без файла или без лицензии: их барабаны молчат
        std::vector<juce::String> warnings;
        juce::String error;
        bool cancelled = false;
        int64_t memoryBytes = 0;
        int decodedClips = 0;       // клипов декодировано в этой загрузке
        int reusedClips = 0;        // клипов взято из KitLoadOptions::reuse

        bool ok() const noexcept { return kit != nullptr; }
    };

    // Собирает кит из установки и пакетов: места, клавиши, группы слоёв -> переключатели Kit::choices. Расшифровывает,
    // декодирует FLAC в float32 (24 бит без потерь), склеивает манифесты по id барабана и удара. Один пак читается один раз,
    // даже если обслуживает несколько мест. Пак без файла/лицензии: место молчит, id в missingPacks.
    // Блокирует вызывающий поток и сам ничего не публикует: вызывать из фонового потока, готовый кит отдавать
    // в SamplitProcessor::setKit(). Audio thread не затрагивает
    KitLoadResult loadKit (const KitSpec&, const KitLoadOptions&);
}
