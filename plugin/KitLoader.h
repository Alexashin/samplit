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
    // Пресет (docs/preset-format.md): какие пакеты и барабаны играют и как звучит. Ключей и аудио в нём нет
    struct Preset
    {
        struct MicSetting
        {
            float levelDb = 0.0f;
            float pan = 0.0f;
            int out = -1;           // -1: выход из манифеста пакета
            bool mute = false;
        };

        // Как выбирается удар на слоях барабанов (docs/preset-format.md). Каждое правило становится переключателем
        // на экране кита. Слои с pick "exact" из манифеста правило не трогает, если не задано includeExact
        struct PickRule
        {
            juce::String name;
            std::vector<juce::String> pieces;
            int keyLo = 0, keyHi = 127;
            Pick mode = Pick::Random;
            bool includeExact = false;
        };

        juce::String id, name, comment;
        juce::String mixId;                 // микс по умолчанию (файл в папке mixes), пусто: берётся блок mics
        std::vector<juce::String> packs;
        std::vector<juce::String> pieces;   // пусто: все барабаны пакетов
        bool includeUnverified = false;
        HitSelect mode = HitSelect::Live;
        int layerSpread = 0;
        float velocityToGain = 0.0f;
        std::map<juce::String, MicSetting> mics;
        std::vector<PickRule> pick;

        static bool fromJson (const juce::String& json, Preset& out, juce::String& error);
        static bool fromFile (const juce::File&, Preset& out, juce::String& error);

        // Пресет как JSON (формат docs/preset-format.md): так сохраняется «Сохранить как…»
        juce::String toJson() const;
    };

    // Микс микрофонов: уровень, панорама, выход и mute по id микрофона. Применяется к загруженному киту без перезагрузки.
    // Файл в папке mixes рядом с пресетами (docs/preset-format.md)
    struct Mix
    {
        juce::String id, name, comment;
        std::map<juce::String, Preset::MicSetting> mics;

        static bool fromJson (const juce::String& json, Mix& out, juce::String& error);
        static bool fromFile (const juce::File&, Mix& out, juce::String& error);
        juce::String toJson() const;
    };

    // Режим pick в пресете: "random", "cycle", "exact"
    bool pickFromString (const juce::String&, Pick& out);
    juce::String pickToString (Pick);

    // Расставляет слоям кита режимы и переключатели из правил пресета. Вызывается до Kit::buildIndex()
    void applyPickRules (const Preset&, Kit&);

    // Уровень, панорама и глушение микрофона кита; индекс совпадает с Kit::mics
    struct MicMix
    {
        float levelDb = 0.0f;
        float pan = 0.0f;
        bool mute = false;
    };

    struct KitLoadOptions
    {
        juce::File packDir;     // папка с *.splpack
        // Режим «локальная библиотека» (для проверки на своих сэмплах): если задана libraryRoot, звук берётся прямо из WAV
        // этой папки (та же структура, что у sample_library/00-Studio600ru-Drum-LIB), целиком и без обрезки, а раскладка
        // из исходных манифестов <manifestDir>/<id пакета>.json. Пакеты и лицензия не нужны
        juce::File libraryRoot;
        juce::File manifestDir;
        // Ключ пакета по его id (из токена лицензии). false: лицензии на пакет нет
        std::function<bool (const juce::String& packId, pack::PackKey& key)> keyFor;
        const std::atomic<bool>* cancel = nullptr;      // true: загрузка прерывается
        std::function<void (float)> progress;           // 0..1, зовётся из рабочих потоков
        int threads = 0;                                // 0: по числу ядер
    };

    struct KitLoadResult
    {
        std::shared_ptr<const Kit> kit;     // nullptr, если загрузка не удалась или отменена
        std::vector<MicMix> mix;            // по индексу микрофона кита
        std::vector<juce::String> missingPacks;     // пакеты пресета без файла или без лицензии: их барабаны молчат
        std::vector<juce::String> warnings;
        juce::String error;
        bool cancelled = false;
        int64_t memoryBytes = 0;

        bool ok() const noexcept { return kit != nullptr; }
    };

    // Собирает кит из пресета и пакетов: расшифровывает, декодирует FLAC в float32 (24 бит без потерь), склеивает манифесты по id барабана
    // и удара. Блокирует вызывающий поток и сам ничего не публикует: вызывать из фонового потока, готовый кит
    // отдавать в SamplitProcessor::setKit(). Audio thread не затрагивает
    KitLoadResult loadKit (const Preset&, const KitLoadOptions&);
}
