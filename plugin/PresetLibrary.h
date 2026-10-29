#pragma once

#include "KitLoader.h"

#include <utility>

namespace samplit
{
    // Что с пакетом пресета на этой машине
    enum class PackState
    {
        Ready,          // файл есть, лицензия на него есть
        NotInstalled,   // файла нет в папке пакетов
        NoLicense       // файл есть, ключа в токене нет
    };

    struct PresetEntry
    {
        Preset preset;
        juce::File file;
        std::vector<std::pair<juce::String, PackState>> packs;  // в порядке пресета

        bool isPlayable() const noexcept;   // хотя бы один пакет готов: что-то зазвучит
        bool isComplete() const noexcept;   // готовы все пакеты
    };

    // Миксы из папки (*.json, по имени). Битые файлы пропускаются
    std::vector<Mix> scanMixes (const juce::File& mixDir);

    // Пресеты из папки (*.json, по имени) и состояние их пакетов. Битые пресеты пропускаются.
    // Читает заголовки пакетов с диска, поэтому не для audio thread; на десятке пакетов быстро
    // libraryRoot не пуст: режим локальной библиотеки, пакет «установлен», если есть <manifestDir>/<id>.json, лицензия не нужна
    std::vector<PresetEntry> scanPresets (const juce::File& presetDir, const juce::File& packDir,
                                          const std::function<bool (const juce::String&, pack::PackKey&)>& keyFor,
                                          const juce::File& libraryRoot = {}, const juce::File& manifestDir = {});
}
