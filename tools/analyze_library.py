"""Анализ объёма и хвостов библиотеки Studio600 для выбора схемы загрузки в память.

Считает по маппингу из .nki (tools/nki_extract.py -> sample_library/mapping/*.json):
  * сколько файлов реально используется, их длительности и объём;
  * какая доля объёма лежит в хвостах длиннее 0,5 / 1 / 2 с;
  * на какой отметке затухает сигнал (RMS по блокам 10 мс ниже порога) и сколько даёт обрезка тихого хвоста;
  * бюджет оперативной памяти для пресета «вся установка + один малый» в разных схемах.

Только чтение, без внешних библиотек (нужен Python 3.12: модуль audioop).
Размеры и длительности считаются по всем файлам, затухание по случайной выборке
(--per-category файлов на категорию, --seed фиксирует выборку, вывод воспроизводим).

    python tools/analyze_library.py [--root sample_library/Studio600-v2]
        [--mapping sample_library/mapping] [--per-category 60] [--seed 1] [--json out.json]
"""
import argparse
import audioop
import json
import math
import random
import struct
import sys
from collections import defaultdict
from pathlib import Path

RATE = 44100
BYTES24 = 3
BLOCK_MS = 10
BLOCK = RATE * BLOCK_MS // 1000          # 441 отсчёт
THRESHOLDS_DB = (-70, -60, -50)           # RMS блока в dBFS, ниже которого хвост считается тишиной
LONG_S = (0.5, 1.0, 2.0)
HEADS_MS = (300, 1000)
HOST_RATES = (44100, 48000)
SPARE_MS = 20                             # запас после последнего громкого блока

CATEGORIES = {
    'KICK-Pearl-Masters': 'kick',
    'SNARE': 'snare',
    'TOMS-Pearl-Masters': 'toms',
    'HAT': 'hat',
    'RIDE': 'ride',
    'Crash': 'crash',
    'CHINA': 'china',
    'SPLASH': 'splash',
}
CYMBALS = ('crash', 'china', 'splash', 'ride', 'hat')
ORDER = ('kick', 'snare', 'toms', 'hat', 'ride', 'crash', 'china', 'splash')


def category(rel: str) -> str:
    return CATEGORIES.get(rel.split('/')[0], 'other')


def wav_info(path: Path):
    """Возвращает (смещение данных, число отсчётов) для моно 24 бит PCM."""
    with open(path, 'rb') as f:
        head = f.read(4096)
    if head[:4] != b'RIFF' or head[8:12] != b'WAVE':
        raise ValueError(f'не WAV: {path}')
    i = 12
    fmt = None
    while i + 8 <= len(head):
        cid = head[i:i + 4]
        size = struct.unpack_from('<I', head, i + 4)[0]
        if cid == b'fmt ':
            fmt = struct.unpack_from('<HHIIHH', head, i + 8)
        elif cid == b'data':
            if fmt is None:
                raise ValueError(f'нет fmt перед data: {path}')
            tag, ch, rate, _, _, bits = fmt
            if ch != 1 or bits != 24 or rate != RATE:
                raise ValueError(f'ожидался моно 24 бит 44100: {path} {fmt}')
            return i + 8, size // BYTES24
        i += 8 + size + (size & 1)
    raise ValueError(f'нет data: {path}')


def load_int16(path: Path, offset: int, frames: int) -> bytes:
    """Старшие 16 бит каждого 24-битного отсчёта (достаточно для оценки уровня)."""
    with open(path, 'rb') as f:
        f.seek(offset)
        raw = f.read(frames * BYTES24)
    n = len(raw) // BYTES24
    out = bytearray(2 * n)
    out[0::2] = raw[1:3 * n:3]
    out[1::2] = raw[2:3 * n:3]
    return bytes(out)


def block_db(pcm: bytes):
    """RMS блоков по 10 мс в dBFS."""
    res = []
    for i in range(0, len(pcm), BLOCK * 2):
        frag = pcm[i:i + BLOCK * 2]
        r = audioop.rms(frag, 2)
        res.append(20 * math.log10(r / 32768) if r > 0 else -200.0)
    return res


def trimmed_frames(db, frames: int, thr: float) -> int:
    last = -1
    for i, v in enumerate(db):
        if v > thr:
            last = i
    if last < 0:
        return min(frames, BLOCK)
    return min(frames, (last + 1) * BLOCK + RATE * SPARE_MS // 1000)


def pct(sorted_vals, p):
    if not sorted_vals:
        return 0.0
    k = (len(sorted_vals) - 1) * p / 100
    lo, hi = math.floor(k), math.ceil(k)
    return sorted_vals[lo] + (sorted_vals[hi] - sorted_vals[lo]) * (k - lo)


def used_files(mapping_path: Path):
    d = json.loads(mapping_path.read_text(encoding='utf-8'))
    files = set()
    per_mic = defaultdict(set)
    for h in d['hits']:
        for mic, m in h['mics'].items():
            files.add(m['file'])
            per_mic[mic].add(m['file'])
    mics_per_hit = defaultdict(list)
    for h in d['hits']:
        mics_per_hit[category(next(iter(h['mics'].values()))['file'])].append(len(h['mics']))
    return files, mics_per_hit, len(d['hits']), d['zones'], per_mic


def mb(x):
    return x / 1024 / 1024


def main():
    sys.stdout.reconfigure(encoding='utf-8')
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default='sample_library/Studio600-v2')
    ap.add_argument('--mapping', default='sample_library/mapping')
    ap.add_argument('--per-category', type=int, default=60)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--json', default='')
    a = ap.parse_args()
    root, mdir = Path(a.root), Path(a.mapping)

    maps = {p.stem: used_files(p) for p in sorted(mdir.glob('*.json'))}
    live = maps['ED-Ahead-LIVE']
    union = set().union(*[m[0] for m in maps.values()])

    print('=== Маппинг ===')
    for name, (fs, mph, hits, zones, _pm) in maps.items():
        print(f'{name}: ударов {hits}, зон {zones}, уникальных файлов {len(fs)}')
    print(f'Объединение трёх .nki: {len(union)} файлов из 30396 на диске')

    # размеры и длительности по всем используемым файлам
    info = {}
    bad = []
    for rel in sorted(union):
        try:
            info[rel] = wav_info(root / rel)
        except ValueError as e:
            bad.append((rel, str(e)))
    print(f'Нечитаемых файлов (не WAV или не моно 24 бит 44,1 кГц): {len(bad)}')
    for rel, _ in bad[:20]:
        print('  ', rel, f'({(root / rel).stat().st_size} байт)')
    if len(bad) > 20:
        print(f'   ... и ещё {len(bad) - 20}')
    union = {r for r in union if r in info}
    maps = {k: ({r for r in v[0] if r in info}, v[1], v[2], v[3], {m: {r for r in fs if r in info} for m, fs in v[4].items()}) for k, v in maps.items()}
    live = maps['ED-Ahead-LIVE']

    def stats(files, title):
        by = defaultdict(list)
        for rel in files:
            by[category(rel)].append(info[rel][1])
        print(f'\n--- {title} ---')
        print(f'{"категория":9} {"файлов":>7} {"сумма, с":>9} {"сред, с":>8} {"p50":>6} {"p90":>6} {"p99":>6} {"макс":>6}'
              f' {"24 бит МБ":>10} {"16 бит МБ":>10}')
        tot_f = tot_n = 0
        for c in ORDER:
            v = sorted(x / RATE for x in by.get(c, []))
            if not v:
                continue
            n = len(v)
            s = sum(v)
            frames = sum(by[c])
            print(f'{c:9} {n:7d} {s:9.0f} {s / n:8.2f} {pct(v, 50):6.2f} {pct(v, 90):6.2f} {pct(v, 99):6.2f} {v[-1]:6.1f}'
                  f' {mb(frames * 3):10.0f} {mb(frames * 2):10.0f}')
            tot_f += frames
            tot_n += n
        print(f'{"всего":9} {tot_n:7d} {"":9} {"":8} {"":6} {"":6} {"":6} {"":6}'
              f' {mb(tot_f * 3):10.0f} {mb(tot_f * 2):10.0f}')
        return by

    stats(union, 'Все используемые файлы (три .nki)')
    by_live = stats(live[0], 'Пресет «установка + малый Ahead-LIVE»')

    print('\n=== Микрофонов на удар (по маппингу LIVE) ===')
    for c in ORDER:
        v = live[1].get(c)
        if v:
            print(f'{c:9} ударов {len(v):5d}, микрофонов на удар: мин {min(v)}, сред {sum(v) / len(v):.1f}, макс {max(v)}')

    print('\n=== Доля объёма в хвостах (пресет LIVE, по всем файлам) ===')
    print(f'{"категория":9} ' + ' '.join(f'>{t:g} с'.rjust(8) for t in LONG_S))
    tail_share = {}
    for c in ORDER:
        fr = by_live.get(c)
        if not fr:
            continue
        tot = sum(fr)
        row = []
        for t in LONG_S:
            lim = int(t * RATE)
            row.append(sum(max(0, x - lim) for x in fr) / tot)
        tail_share[c] = row
        print(f'{c:9} ' + ' '.join(f'{r * 100:7.0f}%' for r in row))

    # затухание по выборке
    rnd = random.Random(a.seed)
    pools = defaultdict(list)
    for rel in sorted(live[0]):
        pools[category(rel)].append(rel)
    print(f'\n=== Затухание: выборка до {a.per_category} файлов на категорию, seed {a.seed}, блок {BLOCK_MS} мс ===')
    keep = {}  # категория -> {порог: доля}
    for c in ORDER:
        pool = pools.get(c, [])
        if not pool:
            continue
        pick = rnd.sample(pool, min(a.per_category, len(pool)))
        full = 0
        trim = {t: 0 for t in THRESHOLDS_DB}
        dur_trim = {t: [] for t in THRESHOLDS_DB}
        for rel in pick:
            off, fr = info[rel]
            db = block_db(load_int16(root / rel, off, fr))
            full += fr
            for t in THRESHOLDS_DB:
                tf = trimmed_frames(db, fr, t)
                trim[t] += tf
                dur_trim[t].append(tf / RATE)
        keep[c] = {t: trim[t] / full for t in THRESHOLDS_DB}
        parts = []
        for t in THRESHOLDS_DB:
            v = sorted(dur_trim[t])
            parts.append(f'{t} дБ: остаётся {keep[c][t] * 100:3.0f}% (длина p50 {pct(v, 50):.2f} с, p90 {pct(v, 90):.2f} с)')
        print(f'{c:9} n={len(pick):3d}  ' + ' | '.join(parts))

    # бюджет памяти
    print('\n=== Бюджет RAM: пресет «установка + малый Ahead-LIVE», моно int16, МБ ===')
    thr = -60
    rows = []
    cat_frames = {c: sum(v) for c, v in by_live.items()}
    total_frames = sum(cat_frames.values())

    def trimmed_frames_cat(c):
        return cat_frames[c] * keep.get(c, {thr: 1.0})[thr]

    def head_frames(files, ms, trim_keep=None):
        lim = int(ms / 1000 * RATE)
        s = 0
        for rel in files:
            fr = info[rel][1]
            s += min(fr, lim)
        return s

    full = total_frames
    trimmed = sum(trimmed_frames_cat(c) for c in cat_frames)
    scheme = [('всё целиком, без обрезки', full)]
    for t in THRESHOLDS_DB:
        fr_t = sum(cat_frames[c] * keep.get(c, {t: 1.0})[t] for c in cat_frames)
        scheme.append((f'всё целиком, обрезка хвоста {t} дБ', fr_t))
    for ms in HEADS_MS:
        hf = head_frames(live[0], ms)
        scheme.append((f'голова {ms} мс в RAM (остаток на диске)', hf))
    # гибрид: ударные целиком с обрезкой, тарелки (ride/hat/crash/china/splash) голова 300 мс
    for ms in HEADS_MS:
        s = 0
        for rel in live[0]:
            c = category(rel)
            fr = info[rel][1]
            if c in ('kick', 'snare', 'toms'):
                s += fr * keep.get(c, {thr: 1.0})[thr]
            else:
                s += min(fr, int(ms / 1000 * RATE))
        scheme.append((f'гибрид: ударные целиком с обрезкой, тарелки голова {ms} мс', s))
    print(f'{"схема":70} {"44,1 кГц":>9} {"48 кГц":>9}')
    for name, fr in scheme:
        m441 = mb(fr * 2)
        m48 = mb(fr * 2 * 48000 / 44100)
        rows.append({'scheme': name, 'mb_44100': round(m441), 'mb_48000': round(m48)})
        print(f'{name:70} {m441:9.0f} {m48:9.0f}')
    on_disk = mb(sum(info[r][1] for r in live[0]) * 3)
    print(f'\nЭталон: те же файлы на диске в WAV 24 бит: {on_disk:.0f} МБ; FLAC обычно даёт 50 - 60% (не измерено: нет кодека)')

    # по микрофонам: что можно не грузить, если микрофон выключен в пресете
    print('\n=== По микрофонам (пресет LIVE): файлов и RAM, МБ int16 (обрезка -60 дБ по доле категории) ===')
    print(f'{"микрофон":18} {"файлов":>7} {"без обрезки":>12} {"с обрезкой":>11}')
    per = live[4]
    sums = {}
    for mic, fs in per.items():
        fr_full = sum(info[r][1] for r in fs)
        fr_trim = sum(info[r][1] * keep.get(category(r), {thr: 1.0})[thr] for r in fs)
        sums[mic] = (len(fs), mb(fr_full * 2), mb(fr_trim * 2))
    for mic, (n, f, t) in sorted(sums.items(), key=lambda kv: -kv[1][2]):
        print(f'{mic:18} {n:7d} {f:12.0f} {t:11.0f}')

    # нагрузка стримов
    print('\n=== Нагрузка при стриминге хвостов (оценка) ===')
    rate_stream = RATE * 2  # байт/с int16 с диска, если хвост хранится несжатым
    for c in ('crash', 'china', 'ride', 'hat', 'splash'):
        v = live[1].get(c)
        if not v:
            continue
        mm = sum(v) / len(v)
        print(f'{c:7} микрофонов на удар в среднем {mm:.1f}: один звучащий удар = {mm:.1f} потоков по {rate_stream / 1024:.0f} КБ/с = {mm * rate_stream / 1024:.0f} КБ/с')

    if a.json:
        Path(a.json).write_text(json.dumps({'budget': rows, 'keep': {c: {str(t): v for t, v in d.items()} for c, d in keep.items()},
                                            'tail_share': tail_share}, ensure_ascii=False, indent=1), encoding='utf-8')


if __name__ == '__main__':
    sys.exit(main())
