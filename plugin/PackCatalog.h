#pragma once

// Каталог установленных паков (docs/setup-spec.md §6). Синхронный скан: фоновый запуск делает вызывающий

#include "KitLoader.h"
#include "Setup.h"

#include "samplit/core/Manifest.h"

#include <vector>

namespace samplit
{
    struct CatalogPiece
    {
        juce::String id, type, name;
        std::vector<pack::Group> groups;
        std::vector<int> keys;
    };

    struct CatalogPack
    {
        juce::String packId, title;
        std::vector<CatalogPiece> pieces;
    };

    // Источники те же, что у загрузчика: libraryRoot + manifestDir (локальная библиотека) или packDir + keyFor (.splpack).
    // Остальные поля options не используются. Без includeUnverified скрываются пак studio600-unverified
    // и группы, все слои которых unverified. Результат отсортирован по packId
    std::vector<CatalogPack> scanPackCatalog (const KitLoadOptions& options, bool includeUnverified);

    // Паки, у которых есть барабаны типа места; в pieces остаются только барабаны этого типа
    std::vector<CatalogPack> packsForPlace (const std::vector<CatalogPack>& catalog, const PlaceDef& place);

    // Состав кита из установки: выключенные места пропускаются, клавиши барабана переносятся на место (mapKeysToPlace).
    // Пак не найден в каталоге: место остаётся в составе с пустым keyMap, выбор не стирается (загрузчик сообщит missingPacks)
    KitSpec makeKitSpec (const Setup&, const std::vector<CatalogPack>& catalog, bool includeUnverified,
                         std::vector<juce::String>& warnings);
}
