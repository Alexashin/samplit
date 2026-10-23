#include "samplit/core/License.h"

namespace samplit::license
{
    // ВНИМАНИЕ: это ТЕСТОВЫЙ публичный ключ (сид 000102...1f лежит в tests/ и tools/license_server/tests).
    // Токены, подписанные им, умеет выпускать любой читающий репозиторий. Перед выпуском сборки
    // ЗАМЕНИТЬ БОЕВЫМ: сгенерировать пару командой `python tools/license_server/admin.py init`,
    // приватный ключ хранить вне репозитория, публичный вставить сюда.
    const Key32& embeddedPublicKey()
    {
        static const Key32 key = {
            0x03, 0xa1, 0x07, 0xbf, 0xf3, 0xce, 0x10, 0xbe, 0x1d, 0x70, 0xdd, 0x18, 0xe7, 0x4b, 0xc0, 0x99,
            0x67, 0xe4, 0xd6, 0x30, 0x9b, 0xa5, 0x0d, 0x5f, 0x1d, 0xdc, 0x86, 0x64, 0x12, 0x55, 0x31, 0xb8
        };
        return key;
    }
}
