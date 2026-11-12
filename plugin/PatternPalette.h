#pragma once

#include "Kit.h"
#include "PackCatalog.h"
#include "Pattern.h"
#include "Setup.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace samplit
{
    // Один вариант удара для партии: клавиша + точное число velocity, плюс понятное имя
    struct StrengthOption
    {
        int key = 0;
        juce::String groupId;       // id группы из манифеста (пусто, если меты нет)
        juce::String name;          // русское имя группы («Основной», «Дроби»…), для exact с несколькими ударами с номером
        juce::String role;          // роль группы из манифеста ("main", "ghost"…), "other" если меты нет
        juce::String label;         // «тихо» / «средне» / «сильно» по velocity
        int velocity = 100;
        bool exact = false;         // удар выбирается самим числом (Pick::Exact)
        juce::String pairType, pairPlace, pairGroup;    // парный удар из манифеста, пусто если нет
    };

    // Палитра места установки. Режим выбора (Exact / остальные) берётся из кита, то есть учитывает настройку пользователя.
    // meta: описание барабана из каталога (роли, пары); можно nullptr. Сортировка: клавиша, затем velocity
    std::vector<StrengthOption> buildPalette (const Kit& kit, int slot, const pack::Piece* meta = nullptr);

    // Описание барабана места из каталога (роли и пары групп): пак места, барабан по pieceId или единственный барабан типа места.
    // false: пака или барабана нет в каталоге
    bool findPieceMeta (const std::vector<CatalogPack>&, const PlaceDef&, const PlaceSetup&, pack::Piece& out);

    // Вариант палитры по клавише и числу: точное совпадение, иначе ближайшее число на той же клавише; -1 если клавиши нет
    int findOption (const std::vector<StrengthOption>&, int key, int velocity);

    // ---- Парные удары (docs/phrase-editor-spec.md): малый «Крещендо» и томы «Крещендо» с одинаковыми числами ----

    // Палитра одного места установки
    struct PlacePalette
    {
        int slot = -1;
        juce::String placeId, type;
        std::vector<StrengthOption> options;
    };

    // Вариант в палитре партнёра для удара o: группа o.pairGroup, то же число, иначе ближайшее в группе; -1 если группы нет
    int pairOption (const std::vector<StrengthOption>& partner, const StrengthOption& o);

    // Может ли место играть пару к o: у o есть пара, место подходит (pairPlace, иначе pairType) и в его палитре есть группа
    bool canPair (const PlacePalette& partner, const StrengthOption& o);

    // Поставить удар o на шаг места own и, если partner не nullptr и может играть пару, парный удар на тот же шаг.
    // false: шаг вне партии
    bool setPairedStep (Pattern&, int step, const PlacePalette& own, const StrengthOption& o, const PlacePalette* partner);

    // Снять удар места own; у партнёра парный удар на том же шаге снимается, только если там стоит именно пара к снятому
    void removePairedStep (Pattern&, int step, const PlacePalette& own, const PlacePalette* partner);
}
