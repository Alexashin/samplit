#include "samplit/core/License.h"

namespace samplit::license
{
    // Боевой публичный ключ сервера лицензий. Пара создана командой `python tools/license_server/admin.py init`,
    // приватный ключ (signing.seed) лежит в каталоге секретов сервера вне репозитория. Токены выпускают
    // tools/make_token.py (офлайн) и сервер. Тестовые токены в tests/ подписаны своим тестовым ключом и сюда не подходят
    const Key32& embeddedPublicKey()
    {
        static const Key32 key = {
            0x32, 0xe1, 0xe2, 0x42, 0xa1, 0x84, 0xe2, 0x9f, 0x69, 0xa0, 0xac, 0x81, 0x31, 0xc9, 0x08, 0xd8,
            0xa8, 0xdd, 0x1d, 0xcc, 0xad, 0x6b, 0x08, 0xa3, 0x85, 0x6e, 0x6c, 0xd6, 0x47, 0x5a, 0x43, 0x50
        };
        return key;
    }
}
