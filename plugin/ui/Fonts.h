#pragma once

#include <BinaryData.h>
#include <juce_graphics/juce_graphics.h>

namespace samplit::fonts
{
    // IBM Plex Sans (текст) и JetBrains Mono (числа, клавиши): вшиты в плагин, OFL; не зависят от шрифтов системы
    inline juce::Typeface::Ptr sansRegular()
    {
        static const auto t = juce::Typeface::createSystemTypefaceFor (BinaryData::PlexSansRegular_ttf, (size_t) BinaryData::PlexSansRegular_ttfSize);
        return t;
    }
    inline juce::Typeface::Ptr sansMedium()
    {
        static const auto t = juce::Typeface::createSystemTypefaceFor (BinaryData::PlexSansMedium_ttf, (size_t) BinaryData::PlexSansMedium_ttfSize);
        return t;
    }
    inline juce::Typeface::Ptr monoRegular()
    {
        static const auto t = juce::Typeface::createSystemTypefaceFor (BinaryData::JetBrainsMonoRegular_ttf, (size_t) BinaryData::JetBrainsMonoRegular_ttfSize);
        return t;
    }
    inline juce::Typeface::Ptr monoMedium()
    {
        static const auto t = juce::Typeface::createSystemTypefaceFor (BinaryData::JetBrainsMonoMedium_ttf, (size_t) BinaryData::JetBrainsMonoMedium_ttfSize);
        return t;
    }

    inline juce::Font text (float height)    { return juce::Font (juce::FontOptions (sansRegular()).withHeight (height)); }
    inline juce::Font strong (float height)  { return juce::Font (juce::FontOptions (sansMedium()).withHeight (height)); }
    inline juce::Font mono (float height)    { return juce::Font (juce::FontOptions (monoRegular()).withHeight (height)); }
    inline juce::Font monoStrong (float height) { return juce::Font (juce::FontOptions (monoMedium()).withHeight (height)); }

    // Заголовок раздела: мелкие прописные с разрядкой
    inline juce::Font caption() { return strong (11.5f).withExtraKerningFactor (0.08f); }
}
