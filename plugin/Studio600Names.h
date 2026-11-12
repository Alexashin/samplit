#pragma once

#include <string_view>
#include <utility>

namespace samplit
{
    // Фиксированные слоты микрофонов Studio600: индекс микрофона в ките, микшере и параметрах хоста не зависит от состава
    // установки. Порядок менять нельзя (идентификаторы параметров и автоматизация в проектах привязаны к id, но индексы
    // микшера и движка к слотам). Незнакомые id (будущие паки) занимают слоты после этих
    struct Studio600MicSlot
    {
        std::string_view id;
        int defaultOut;     // выход по умолчанию в режиме «Мульти-аут» (1..10, как в манифестах)
    };

    inline constexpr int kStudio600MicCount = 15;
    inline constexpr Studio600MicSlot kStudio600Mics[kStudio600MicCount] = {
        { "kick_in", 1 }, { "kick_out", 2 }, { "sn_top", 3 }, { "sn_bot", 4 }, { "hat", 5 }, { "ride", 6 },
        { "tom_a1", 8 }, { "tom_a2", 8 }, { "tom_a3", 8 }, { "tom_a4", 8 },
        { "oh_1", 9 }, { "oh_2", 9 }, { "room_mid", 7 }, { "room_1", 10 }, { "room_2", 10 }
    };

    // Слот микрофона по id, -1: id не из Studio600
    inline int studio600MicSlot (std::string_view id) noexcept
    {
        for (int i = 0; i < kStudio600MicCount; ++i)
            if (kStudio600Mics[i].id == id)
                return i;
        return -1;
    }

    // Публичные подписи Studio600. Внутренние id и номера шин остаются прежними.
    inline const char* studio600MicName (std::string_view id) noexcept
    {
        using Pair = std::pair<std::string_view, const char*>;
        static constexpr Pair names[] = {
            { "kick_in", "Kick In" }, { "kick_out", "Kick Out" },
            { "sn_top", "Snare Top" }, { "sn_bot", "Snare Bottom" },
            { "oh_1", "Over Hat" }, { "oh_2", "Over Ride" },
            { "room_1", "Room Hat" }, { "room_2", "Room Ride" },
            { "room_mid", "Room Mid" }, { "hat", "Hi-Hat" }, { "ride", "Ride" },
            { "tom_a1", "Альт 10" }, { "tom_a2", "Альт 12" },
            { "tom_a3", "Том 14" }, { "tom_a4", "Флор том 16" }
        };
        for (const auto& [key, name] : names)
            if (key == id)
                return name;
        return nullptr;
    }

    inline const char* studio600PieceName (std::string_view id) noexcept
    {
        using Pair = std::pair<std::string_view, const char*>;
        static constexpr Pair names[] = {
            { "tom_10", "Альт 10" }, { "tom_14", "Альт 12" },
            { "tom_16m", "Том 14" }, { "tom_16l", "Флор том 16" }
        };
        for (const auto& [key, name] : names)
            if (key == id)
                return name;
        return nullptr;
    }

    inline const char* studio600OutputName (int output) noexcept
    {
        static constexpr const char* names[] = {
            "Kick In", "Kick Out", "Snare Top", "Snare Bottom", "Hi-Hat", "Ride", "Room Mid",
            "Toms", "Over", "Room"
        };
        return output >= 1 && output <= 10 ? names[output - 1] : nullptr;
    }

    // Стартовая панорама микрофона на стерео-шине (Over, Room, Toms): слева направо как на схеме. Остальные по центру
    inline float studio600MicPan (std::string_view id) noexcept
    {
        using Pair = std::pair<std::string_view, float>;
        static constexpr Pair pans[] = {
            { "oh_1", -1.0f }, { "oh_2", 1.0f }, { "room_1", -1.0f }, { "room_2", 1.0f },
            { "tom_a1", -0.7f }, { "tom_a2", -0.25f }, { "tom_a3", 0.25f }, { "tom_a4", 0.7f }
        };
        for (const auto& [key, pan] : pans)
            if (key == id)
                return pan;
        return 0.0f;
    }
}
