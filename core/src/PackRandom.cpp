// Случайные байты от генератора ОС. Отдельный файл без JUCE: windows.h и макросы JUCE мешают друг другу

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #include <bcrypt.h>
#endif

namespace samplit::pack
{
    void fillRandom (void* dest, size_t size)
    {
        auto* p = static_cast<uint8_t*> (dest);

#if defined(_WIN32)
        while (size > 0)
        {
            const ULONG n = size > 0x10000000u ? 0x10000000u : (ULONG) size;
            if (BCryptGenRandom (nullptr, p, n, BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
                std::abort();  // без случайных чисел нельзя ни ключ, ни nonce, молча продолжать нельзя
            p += n;
            size -= n;
        }
#elif defined(__APPLE__)
        arc4random_buf (p, size);
#else
        FILE* f = std::fopen ("/dev/urandom", "rb");
        if (f == nullptr || std::fread (p, 1, size, f) != size)
            std::abort();
        std::fclose (f);
#endif
    }
}
