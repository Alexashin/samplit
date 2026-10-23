# Формат пакета `.splpack` и манифеста (v1)

Общий контракт для потоков: сборщик пакетов (`tools/packer`, `core/pack`), разметка (`tools/mark_kit.py`), движок (`plugin/Kit*`), лицензия (`core/license`). План и причины решений: `docs/packs-plan.md`.

## Исходный манифест (то, что подаётся сборщику)

JSON, UTF-8. Пути к файлам относительны корню библиотеки, который передаётся сборщику (`--root`).

```json
{
  "format": 1,
  "id": "studio600-base", "version": 1, "tier": "base", "requires": [],
  "title": "Studio600 Drum LIB: базовый",
  "mics": [
    { "id": "kick_in", "name": "Kick In", "role": "close", "out": 1 }
  ],
  "pieces": [
    { "id": "snare", "name": "Малый", "keys": [26], "choke": null,
      "layers": [ { "id": "center_120_127", "artic": "center", "velLo": 120, "velHi": 127 } ],
      "hits": [
        { "id": "snare_26_120", "layer": "center_120_127", "key": 26, "velLo": 120, "velHi": 120,
          "files": {
            "sn_top": { "path": "SNARE/Snare-Ahead/Ahead-LIVE/SN-top/0-01.wav", "gainDb": -3.0, "start": 0, "end": 0 }
          } }
      ] }
  ]
}
```

- `mics[].id` — стабильные идентификаторы микрофонов. `out` — выход по умолчанию 1…16 (моно, выход 0 = Main).
- `pieces[].keys` — клавиши (C3 = 60). Один барабан может занимать несколько клавиш (лево и право, слои на соседних нотах).
- `layers` — слои: окно velocity и артикуляция. Режим «Живой» выбирает слой по velocity, а удар внутри слоя случайно.
- `hits[]` — один удар: клавиша `key`, окно velocity `velLo..velHi` (в библиотеке Studio600 это одно значение), `layer`, файлы по микрофонам. Все файлы удара играются вместе, с одного сэмпла.
- `files[mic]`: `path`, `gainDb` (громкость зоны, 0 по умолчанию), `start` и `end` (обрезка в отсчётах, 0 = без обрезки).
- Поле `peerGroup` у удара (необязательное) — признак группы `-D` из `.nki`, пока не интерпретируется.

## Хранимый манифест (внутри контейнера)

Тот же JSON, но `files[mic]` заменено на `{ "chunk": N, "gainDb": .., "start": .., "end": .., "frames": .., "rate": .., "tailChunk": M }`. `chunk` — номер блока с FLAC всего файла (или только головы, если задан `tailChunk`; способ стриминга хвостов описывает `docs/streaming-design.md`).

## Контейнер

```
magic "SPLP" (4 байта) | formatVersion u32 | packIdLen u16 | packId | manifestLen u32 | nonce(24) | manifest (AEAD) | chunkCount u32
таблица блоков: chunkCount × { offset u64, length u32, nonce(24) }
блоки: FLAC, каждый зашифрован отдельно (XChaCha20-Poly1305, тег 16 байт в конце блока)
```

- Шифрование: Monocypher `crypto_aead_lock/unlock` (XChaCha20-Poly1305). Ключ пакета 32 байта, в контейнере не хранится (его выдаёт токен лицензии). Заголовок (magic, версия, packId) идёт как дополнительные данные AEAD.
- Nonce блока: случайный 24 байта на блок, записан в таблицу блоков.
- Порядок байт little-endian.
- Подпись пакета не нужна: подмена байта ловится тегом AEAD.

## Токен лицензии

JSON `{ "payload": {...}, "sig": "<base64 Ed25519>" }`. Подпись на SHA-512 (Monocypher `crypto_ed25519_*`, совместима с PyNaCl и libsodium). Payload: `licenseId`, `user`, `machine` (хеш отпечатка), `notBefore`, `expires` (Unix, секунды), `packs`: `{ packId: "<ключ пакета, зашифрованный ключом машины, base64>" }`. Подробности в `docs/packs-plan.md`, раздел 6.
