#pragma once

#include <string_view>
#include <utility>

namespace samplit
{
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
            "Kick In", "Kick Out", "Snare Top", "Snare Bottom", "Over Hat", "Over Ride",
            "Room Hat", "Room Ride", "Room Mid", "Hi-Hat", "Ride", "Альт 10", "Альт 12",
            "Том 14", "Флор том 16", "Out 16"
        };
        return output >= 1 && output <= 16 ? names[output - 1] : nullptr;
    }
}
