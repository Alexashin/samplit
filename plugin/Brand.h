#pragma once

#include <juce_graphics/juce_graphics.h>

// Тёмная тема из brand/tokens.json. Плагин живёт в тёмных DAW, светлая тема позже.
namespace samplit::brand
{
    inline const juce::Colour bg          { 0xff0b0e15 };
    inline const juce::Colour surface     { 0xff131720 };
    inline const juce::Colour pad         { 0xff1e232d };
    inline const juce::Colour line        { 0xff2d333e };
    inline const juce::Colour text        { 0xffeef0f4 };
    inline const juce::Colour textMuted   { 0xffa6abb5 };
    inline const juce::Colour accent      { 0xff7ca1ff };
    inline const juce::Colour accentHover { 0xffaac5ff };
    inline const juce::Colour accentSoft  { 0xff1b295a };
    inline const juce::Colour onAccent    { 0xff0e141f };

    // Радиус пэда: 24% стороны, как у знака
    constexpr float padRadiusRatio = 0.24f;

    // Цвет группы: по индексу, одинаков на клавиатуре, в списке групп и в таблице зон
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
