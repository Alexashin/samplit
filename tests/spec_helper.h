#pragma once

// Заготовка тестов: состав кита (KitSpec) из пары «пак, барабан» с клавишами без сдвига

#include "KitLoader.h"

#include <vector>

namespace samplit::testspec
{
    inline KitSpec::Place placeFor (const char* placeId, int slot, const juce::String& packId, const juce::String& pieceId,
                                    const std::vector<int>& keys)
    {
        KitSpec::Place p;
        p.slot = slot;
        p.placeId = placeId;
        p.packId = packId;
        p.pieceId = pieceId;
        for (int k : keys)
            p.keyMap[k] = k;
        return p;
    }

    // Один барабан на месте slot 0
    inline KitSpec oneSpec (const juce::String& packId, const juce::String& pieceId, int key)
    {
        KitSpec s;
        s.places.push_back (placeFor ("kick", 0, packId, pieceId, { key }));
        return s;
    }
}
