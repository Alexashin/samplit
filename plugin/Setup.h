#pragma once

// Установка Studio600 (docs/setup-spec.md §2, §4, §5): фиксированные места на клавишах, выбранный на месте пак,
// громкость и режимы выбора ударов. Только данные, без GUI

#include "Kit.h"

#include <juce_core/juce_core.h>

#include <array>
#include <map>
#include <string>
#include <vector>

namespace samplit
{
    struct PlaceDef
    {
        int slot;
        const char* id;
        const char* type;           // kick | snare | crash | china | splash | hat | ride | tom
        int keyLo, keyHi;           // MIDI, C3 = 60
        const char* label;          // UTF-8
        const char* defaultPack;
        const char* defaultPiece;
    };

    constexpr int kMaxPlaces = 16;

    inline constexpr std::array<PlaceDef, 12> kStudio600Places { {
        {  0, "kick",    "kick",   24,  24, "Бочка",       "studio600-kick-rode",   "kick" },
        {  1, "snare",   "snare",  25,  35, "Малый",       "studio600-snare-live",  "snare_live" },
        {  2, "crash_a", "crash",  36,  39, "Crash 18\"",  "studio600-crash18",     "crash_a" },
        {  3, "crash_b", "crash",  40,  43, "Crash 16\"",  "studio600-crash16",     "crash_b" },
        {  4, "china",   "china",  44,  46, "China",       "studio600-china",       "china" },
        {  5, "splash",  "splash", 47,  47, "Splash",      "studio600-splash",      "splash" },
        {  6, "hat",     "hat",    48,  83, "Hi-hat",      "studio600-hat",         "hat" },
        {  7, "ride",    "ride",   84,  95, "Ride",        "studio600-ride",        "ride" },
        {  8, "tom_16l", "tom",    96,  97, "Флор том 16", "studio600-toms",        "tom_16l" },
        {  9, "tom_16m", "tom",    98,  99, "Том 14",      "studio600-toms",        "tom_16m" },
        { 10, "tom_14",  "tom",   100, 101, "Альт 12",     "studio600-toms",        "tom_14" },
        { 11, "tom_10",  "tom",   102, 103, "Альт 10",     "studio600-toms",        "tom_10" }
    } };
    static_assert (kStudio600Places.size() <= (size_t) kMaxPlaces);

    // nullptr: такого места нет
    const PlaceDef* findPlace (const std::string& id) noexcept;

    constexpr float kPlaceLevelMinDb = -60.0f;
    constexpr float kPlaceLevelMaxDb = 12.0f;

    struct PlaceSetup
    {
        std::string placeId;
        bool enabled = true;
        std::string packId, pieceId;        // pieceId пуст: единственный барабан нужного типа в паке
        float levelDb = 0.0f;               // -60...+12
        std::map<std::string, Pick> picks;  // id группы -> выбор пользователя (только отличия от умолчания)
    };

    struct Setup
    {
        std::vector<PlaceSetup> places;     // по всем kStudio600Places, в порядке slot

        static Setup factory();
        juce::String toJson() const;
        // Незнакомые места пропускаются, недостающие берутся из factory(). Битый JSON: false, out не меняется
        static bool fromJson (const juce::String&, Setup& out, juce::String& error);

        const PlaceSetup* find (const std::string& placeId) const noexcept;
    };

    // Перенос клавиш барабана на место (§4)
    struct KeyMapping
    {
        std::map<int, int> keys;                    // клавиша барабана -> клавиша места
        std::vector<juce::String> warnings;         // отброшенные клавиши
    };

    KeyMapping mapKeysToPlace (const std::vector<int>& pieceKeys, const std::string& pieceType, const PlaceDef& target);
}
