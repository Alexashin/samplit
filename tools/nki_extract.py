# SPDX-License-Identifier: LGPL-3.0-or-later
# Адаптация (порт на Python) разбора формата .nki из проекта ConvertWithMoss,
# https://github.com/git-moss/ConvertWithMoss, (c) Jürgen Moßgraber, LGPL-3.0.
# Изменения: Алексашин А.А., 2026 — только нужный Samplit случай, вывод в JSON.
# Этот файл распространяется на условиях GNU LGPL v3 (third_party/LICENSES/LGPL-3.0.txt);
# на остальной код Samplit эта лицензия не распространяется.
"""Извлечение маппинга из инструментов Kontakt 5+ (.nki) без Kontakt.

Читает контейнер NI (hsin), распаковывает FastLZ, разбирает дерево пресета
и выдаёт группы, зоны и список файлов. Разбор формата сделан по описанию
в проекте ConvertWithMoss (J. Moßgraber, LGPL-3.0), поддержан только
нужный нам случай: WAV по внешним путям, одна программа в файле.

Использование:
    python tools/nki_extract.py <файл.nki> [--out mapping.json]

В JSON попадают:
    groups: имена групп (у библиотеки Studio600 группа = микрофон);
    hits:   удар = клавиша + velocity, для каждого микрофона свой файл,
            громкость зоны в дБ (если не 0) и обрезка (если задана).
"""
import argparse
import io
import json
import math
import struct
import sys
from collections import defaultdict
from pathlib import Path

# Типы чанков контейнера NI
CT_TERMINATOR = 1
CT_PRESET = 109
CT_SUBTREE = 115

# Идентификаторы чанков пресета Kontakt
K_GROUP = 0x04
K_PROGRAM = 0x28
K_GROUP_LIST = 0x33
K_ZONE_LIST = 0x34
K_PARAMS_16 = 0x3B
K_PARAMS_32 = 0x3C
K_FILENAME_LIST = 0x3D
K_FILENAME_LIST_EX = 0x4B
STRUCT_IDS = {0x03, 0x06, 0x17, K_PROGRAM, 0x29, 0x32, 0x3A, 0x45, 0x47, 0x4E}

NOTE_NAMES = 'C C# D D# E F F# G G# A A# B'.split()


def note_name(key: int) -> str:
    """Имя ноты в нумерации Kontakt/Cubase (C3 = 60)."""
    return f'{NOTE_NAMES[key % 12]}{key // 12 - 2}'


def _u32(b, o):
    return struct.unpack_from('<I', b, o)[0]


def fastlz_decompress(src: bytes, out_size: int) -> bytes:
    level = (src[0] >> 5) + 1
    out = bytearray()
    ip = 1
    ctrl = src[0] & 31
    n = len(src)
    while True:
        if ctrl >= 32:
            ln = (ctrl >> 5) - 1
            ofs = (ctrl & 31) << 8
            ref = len(out) - ofs - 1
            if ln == 6:
                if level == 1:
                    ln += src[ip]
                    ip += 1
                else:
                    while True:
                        c = src[ip]
                        ip += 1
                        ln += c
                        if c != 255:
                            break
            c = src[ip]
            ip += 1
            ref -= c
            if level == 2 and c == 255 and ofs == (31 << 8):
                ofs = (src[ip] << 8) + src[ip + 1]
                ip += 2
                ref = len(out) - ofs - 8191 - 1
            for _ in range(ln + 3):
                out.append(out[ref])
                ref += 1
        else:
            ctrl += 1
            out += src[ip:ip + ctrl]
            ip += ctrl
        if ip >= n:
            break
        ctrl = src[ip]
        ip += 1
    if len(out) != out_size:
        raise ValueError(f'FastLZ: получено {len(out)} байт, ожидалось {out_size}')
    return bytes(out)


# --- Контейнер NI ---

def _block64(b, o):
    size = struct.unpack_from('<Q', b, o)[0]
    return b[o + 8:o + size], o + size


def _parse_item(b, o, found):
    blk, end = _block64(b, o)
    if _u32(blk, 0) != 1 or blk[4:8] != b'hsin':
        raise ValueError('не контейнер NI (нет hsin)')
    p = _parse_data_chunk(blk, 32, found)  # версия, hsin, 2×u32, UUID
    p += 4  # версия элемента
    children = _u32(blk, p)
    p += 4
    for _ in range(children):
        p = _parse_item(blk, p + 12, found)  # индекс, домен, тип
    return end


def _parse_data_chunk(b, o, found):
    blk, end = _block64(b, o)
    ctype = _u32(blk, 4)
    p = 12
    if ctype != CT_TERMINATOR:
        p = _parse_data_chunk(blk, p, found)
    data = blk[p:]
    found.append((ctype, data))
    if ctype == CT_SUBTREE:
        if data[4]:
            usize, csize = _u32(data, 5), _u32(data, 9)
            _parse_item(fastlz_decompress(data[13:13 + csize], usize), 0, found)
        else:
            _parse_item(data, 5, found)
    return end


# --- Дерево пресета Kontakt ---

class Chunk:
    def __init__(self):
        self.id = -1
        self.ver = -1
        self.pub = b''
        self.children = []


def _r(d, fmt):
    return struct.unpack(fmt, d.read(struct.calcsize(fmt)))[0]


def _read_chunk(f):
    c = Chunk()
    c.id = _r(f, '<H')
    size = _r(f, '<I')
    d = io.BytesIO(f.read(size))
    if c.id in (K_GROUP_LIST, K_ZONE_LIST):
        for _ in range(_r(d, '<I')):
            ch = Chunk()
            # у зоны вместо id хранится индекс группы
            ch.id = _r(d, '<I') if c.id == K_ZONE_LIST else K_GROUP
            _read_struct(ch, d, size)
            c.children.append(ch)
    elif c.id in STRUCT_IDS:
        _read_struct(c, d, size)
    elif c.id in (K_PARAMS_16, K_PARAMS_32):
        if d.read(1) != b'\0':
            raise ValueError('битый массив параметров')
        c.ver = _r(d, '<H')
        for _ in range(16 if c.id == K_PARAMS_16 else 32):
            if d.read(1)[0]:
                ch = Chunk()
                ch.id = _r(d, '<H')
                ch.pub = d.read(_r(d, '<I'))
                c.children.append(ch)
    else:
        c.pub = d.read(size)
    return c


def _read_struct(c, d, size):
    if d.read(1)[0] == 0:
        if size > 0:
            c.pub = d.read(size - 1)
        return
    c.ver = _r(d, '<H')
    d.read(_r(d, '<I'))  # приватные данные
    c.pub = d.read(_r(d, '<I'))
    n = _r(d, '<I')
    cd = io.BytesIO(d.read(n))
    while cd.tell() < n:
        c.children.append(_read_chunk(cd))


def _utf16(d):
    return d.read(_r(d, '<I') * 2).decode('utf-16-le')


def _read_path(d):
    out = ''
    for _ in range(_r(d, '<I')):
        t = d.read(1)[0]
        if t == 0:
            drive = d.read(2).decode('ascii').strip()
            out += f'{drive}:/' if drive else '/'
        elif t == 1:
            drive = _utf16(d)
            out += f'{drive}:/' if drive else '/'
        elif t == 2:
            out += _utf16(d) + '/'
        elif t == 3:
            out += '../'
        elif t in (4, 8, 9):
            out += _utf16(d)
        elif t != 6:
            raise ValueError(f'неизвестный сегмент пути {t}')
    return out


def _file_list(c):
    d = io.BytesIO(c.pub)
    total = len(c.pub)
    if c.id == K_FILENAME_LIST_EX and _r(d, '<H') == 3:
        files = []
        n = _r(d, '<i')
        d.read(8)
        for _ in range(n - 1):
            files.append(_read_path(d))
            if d.tell() < total:
                d.read(28)
        return files
    lists = []
    for _ in range(2):  # служебные файлы, затем сэмплы
        lists.append([_read_path(d) for _ in range(_r(d, '<i'))] if d.tell() < total else [])
    return lists[1]


def _parse_group(c):
    d = io.BytesIO(c.pub)
    name = _utf16(d)
    volume, pan, tune = struct.unpack('<fff', d.read(12))
    return {'name': name, 'volume': volume, 'pan': pan, 'tune': tune}


def _parse_zone(c):
    d = io.BytesIO(c.pub)
    z = {'group': c.id}
    z['sampleStart'], z['sampleEnd'], _ = struct.unpack('<III', d.read(12))
    (z['velLo'], z['velHi'], z['keyLo'], z['keyHi'],
     _, _, _, _, z['root']) = struct.unpack('<9H', d.read(18))
    z['volume'], z['pan'], z['tune'] = struct.unpack('<fff', d.read(12))
    if c.ver >= 0x9A:
        d.read(6)
    z['file'] = _r(d, '<I')
    return z


def load(path):
    """Группы, зоны и список файлов первой программы в .nki."""
    raw = Path(path).read_bytes()
    found = []
    _parse_item(raw, 0, found)
    preset = next((d for t, d in found if t == CT_PRESET), None)
    if preset is None:
        raise ValueError('в файле нет пресета Kontakt (возможно, зашифрован)')
    size = _u32(preset, 12)
    data = preset[20:20 + size]
    f = io.BytesIO(data)
    top = []
    while f.tell() < len(data):
        top.append(_read_chunk(f))

    files = next((_file_list(c) for c in top if c.id in (K_FILENAME_LIST, K_FILENAME_LIST_EX)), [])
    program = next(c for c in top if c.id == K_PROGRAM)
    groups, zones = [], []
    for ch in program.children:
        if ch.id == K_GROUP_LIST:
            groups = [_parse_group(g) for g in ch.children]
        elif ch.id == K_ZONE_LIST:
            zones = [_parse_zone(z) for z in ch.children]
    return groups, zones, files


def build_mapping(path, strip_prefix=None):
    groups, zones, files = load(path)

    def rel(p):
        return p[len(strip_prefix):] if strip_prefix and p.startswith(strip_prefix) else p

    hits = defaultdict(lambda: {'mics': {}})
    for z in zones:
        name = groups[z['group']]['name']
        d_group = name.endswith('-D')
        mic = name[:-2] if d_group else name
        entry = {'file': rel(files[z['file']])}
        if abs(z['volume'] - 1.0) > 1e-4:
            entry['gainDb'] = round(20 * math.log10(z['volume']), 2)
        if z['sampleStart'] or z['sampleEnd']:
            entry['start'], entry['end'] = z['sampleStart'], z['sampleEnd']
        for key in range(z['keyLo'], z['keyHi'] + 1):
            for vel in range(z['velLo'], z['velHi'] + 1):
                h = hits[(key, vel, d_group)]
                h.update(key=key, note=note_name(key), vel=vel, dGroup=d_group)
                h['mics'][mic] = entry

    return {
        'source': Path(path).name,
        'groups': [g['name'] for g in groups],
        'zones': len(zones),
        'hits': [hits[k] for k in sorted(hits)],
    }


def main():
    ap = argparse.ArgumentParser(description='Маппинг из .nki (Kontakt 5+) в JSON')
    ap.add_argument('nki')
    ap.add_argument('--out', help='куда записать JSON (по умолчанию stdout)')
    ap.add_argument('--strip', default='D:/00-Studio600ru-Drum-LIB/',
                    help='префикс пути, который убрать из имён файлов')
    args = ap.parse_args()
    mapping = build_mapping(args.nki, args.strip)
    text = json.dumps(mapping, ensure_ascii=False, indent=1)
    if args.out:
        Path(args.out).write_text(text, encoding='utf-8')
        print(f"{mapping['source']}: групп {len(mapping['groups'])}, зон {mapping['zones']}, "
              f"ударов {len(mapping['hits'])} -> {args.out}", file=sys.stderr)
    else:
        print(text)


if __name__ == '__main__':
    main()
