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

        juce::String id, name;
        std::vector<juce::String> packs;
        std::vector<juce::String> pieces;   // пусто: все барабаны пакетов
        bool includeUnverified = false;
        HitSelect mode = HitSelect::Live;
        int layerSpread = 0;
        float velocityToGain = 0.0f;
        std::map<juce::String, MicSetting> mics;

        static bool fromJson (const juce::String& json, Preset& out, juce::String& error);
        static bool fromFile (const juce::File&, Preset& out, juce::String& error);
    };

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

    // Собирает кит из пресета и пакетов: расшифровывает, декодирует FLAC в int16, склеивает манифесты по id барабана
    // и удара. Блокирует вызывающий поток и сам ничего не публикует: вызывать из фонового потока, готовый кит
    // отдавать в SamplitProcessor::setKit(). Audio thread не затрагивает
    KitLoadResult loadKit (const Preset&, const KitLoadOptions&);
}
