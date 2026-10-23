#pragma once

namespace samplit
{
    // Кубическая интерполяция Эрмита по четырём соседним отсчётам (xm1, x0, x1, x2), t от 0 до 1 между x0 и x1.
    // Нужна при несовпадении частот дискретизации и при сдвиге высоты: линейная заметно съедает верхи
    inline float hermite (float xm1, float x0, float x1, float x2, float t) noexcept
    {
        const float c = (x1 - xm1) * 0.5f;
        const float v = x0 - x1;
        const float w = c + v;
        const float a = w + v + (x2 - x0) * 0.5f;
        const float bNeg = w + a;
        return (((a * t) - bNeg) * t + c) * t + x0;
    }
}
