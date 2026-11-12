#pragma once

#include "Kit.h"
#include "Setup.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace samplit::pack { struct Piece; }

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
}
