#pragma once

#include <juce_graphics/juce_graphics.h>

// Визуальная система Samplit (docs/ui-guidelines.md): «студийный прибор», тёмная графитовая основа,
// один акцент ультрамарин. Значения из brand/tokens.json (акцент 500 для заливок, 300/400 для текста и тонких линий).
namespace samplit::brand
{
    // Поверхности: от дальней к ближней
    inline const juce::Colour bg          { 0xff0b0e15 };
    inline const juce::Colour surface     { 0xff121620 };
    inline const juce::Colour pad         { 0xff1a1f2a };   // поднятый элемент: кнопка, поле
    inline const juce::Colour raised2     { 0xff232937 };   // нажатый, выбранный в сегменте
    inline const juce::Colour line        { 0xff272d39 };
    inline const juce::Colour lineStrong  { 0xff384050 };

    inline const juce::Colour text        { 0xffeef0f4 };
    inline const juce::Colour textMuted   { 0xffa6abb5 };   // контраст на surface ≥ 4.5:1
    inline const juce::Colour textFaint   { 0xff757b8a };   // только неважные подписи и шкалы

    // Акцент. Заливка (главная кнопка, включено) = accent; текст и тонкие линии = accentText
    inline const juce::Colour accent      { 0xff4d75ff };
    inline const juce::Colour accentHover { 0xff7ca1ff };
    inline const juce::Colour accentText  { 0xffaac5ff };
    inline const juce::Colour accentSoft  { 0xff16224f };
    inline const juce::Colour onAccent    { 0xff0b0e15 };

    // Состояния
    inline const juce::Colour ok          { 0xff62d394 };
    inline const juce::Colour warn        { 0xfff2b45c };
    inline const juce::Colour err         { 0xffff8080 };

    // Радиусы и сетка (шаг 4 px). Никаких больших скруглений: прибор, не игрушка
    constexpr float radiusS = 3.0f;
    constexpr float radiusM = 4.0f;
    constexpr int   unit    = 4;
    constexpr int   controlHeight = 28;   // кликабельное не ниже 28 px

    // Радиус пэда: 24% стороны, как у знака (логотип, значок)
    constexpr float padRadiusRatio = 0.24f;

    // Цвет группы по индексу (старые панели; в новом интерфейсе барабаны различаются подписью, цвет только у состояния)
    inline juce::Colour groupColour (int index)
    {
        static const juce::Colour base[] = { juce::Colour (0xff7ca1ff), juce::Colour (0xffffa66b), juce::Colour (0xff6fd6a1),
                                             juce::Colour (0xffe27bd0), juce::Colour (0xffffd166), juce::Colour (0xff5fd0e8),
                                             juce::Colour (0xffb79cff), juce::Colour (0xffff7b7b) };
        const auto c = base[(size_t) (index % 8)];
        return (index / 8) % 2 == 0 ? c : c.darker (0.35f);
    }
}

namespace samplit
{
    // Строковые литералы в исходниках в UTF-8 (/utf-8 для MSVC). juce::String из char* ждёт ASCII
    inline juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }
}
