"""Разметка библиотеки Studio600 поверх выгрузки из .nki.

Берёт выгрузки tools/nki_extract.py (sample_library/mapping/*.json) и ручную разметку
tools/kit-studio600.json, собирает исходные манифесты пакетов по контракту docs/pack-format.md
и пишет их в sample_library/packs-src/<packId>.json. Только стандартная библиотека.

    python tools/mark_kit.py            собрать манифесты
    python tools/mark_kit.py --check    собрать в памяти и проверить, ничего не писать

Правила:
  * удар = клавиша + значение velocity; файлы удара берутся только из выгрузки .nki, не по именам;
  * группа -D из .nki это отдельные удары (другое окно velocity); если на той же клавише и velocity
    есть и обычный удар, и -D с непересекающимися микрофонами, они сливаются в один удар (peerGroup);
  * удар, окно которого руководство не описывает или с ним расходится, и любой удар из группы -D
    получают "unverified": true и уходят в пакет studio600-unverified под тем же id барабана;
  * вывод детерминирован: одинаковый вход даёт побайтно одинаковый файл.
"""
import argparse
import copy
import json
import struct
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_KIT = ROOT / 'tools' / 'kit-studio600.json'
DEFAULT_MAPPING = ROOT / 'sample_library' / 'mapping'
DEFAULT_LIBRARY = ROOT / 'sample_library' / '00-Studio600ru-Drum-LIB'
DEFAULT_OUT = ROOT / 'sample_library' / 'packs-src'

# Итоги из docs/library-structure.md для одного .nki
EXPECTED_HITS = 2361
EXPECTED_ZONES = 16226

NOTE_NAMES = 'C C# D D# E F F# G G# A A# B'.split()


def note_name(key):
    """Имя ноты в нумерации Kontakt и Cubase (C3 = 60)."""
    return f'{NOTE_NAMES[key % 12]}{key // 12 - 2}'


# ---------------------------------------------------------------- чтение входных данных

def load_json(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def load_mappings(mapping_dir):
    """Все выгрузки каталога: имя .nki без расширения -> содержимое."""
    result = {}
    for p in sorted(Path(mapping_dir).glob('*.json')):
        data = load_json(p)
        result[p.stem] = data
    return result


def wav_frames(path):
    """Число отсчётов WAV по заголовку или None, если файла нет или он не читается."""
    try:
        with open(path, 'rb') as f:
            head = f.read(4096)
        if head[:4] != b'RIFF':
            return None
        pos, fmt, data = 12, None, None
        while pos + 8 <= len(head):
            cid = head[pos:pos + 4]
            size = struct.unpack_from('<I', head, pos + 4)[0]
            if cid == b'fmt ':
                fmt = struct.unpack_from('<HHIIHH', head, pos + 8)
            elif cid == b'data':
                data = size
                break
            pos += 8 + size + (size & 1)
        if fmt is None or data is None:
            return None
        block = fmt[4]
        return data // block if block else None
    except OSError:
        return None


# ---------------------------------------------------------------- ручная разметка

def expand_templates(kit):
    """Раскрывает template (snare, tom) в списки клавиш барабана. Возвращает копию разметки."""
    kit = copy.deepcopy(kit)
    templates = kit.get('templates', {})
    for piece in kit['pieces']:
        name = piece.get('template')
        if not name:
            continue
        tpl = templates[name]
        for gid, gdef in tpl.get('groups', {}).items():
            piece.setdefault('groups', {}).setdefault(gid, copy.deepcopy(gdef))
        keys = []
        for spec in tpl['keys']:
            spec = copy.deepcopy(spec)
            if 'offset' in spec:
                spec['key'] = piece['keyOffset'] + spec.pop('offset')
            keys.append(spec)
        piece['keys'] = keys
    return kit


def key_index(kit):
    """(source, key) -> (piece, keySpec)."""
    index = {}
    for piece in kit['pieces']:
        for spec in piece['keys']:
            k = (piece['source'], spec['key'])
            if k in index:
                raise ValueError(f"клавиша {spec['key']} источника {piece['source']} занята дважды: "
                                 f"{index[k][0]['id']} и {piece['id']}")
            index[k] = (piece, spec)
    return index


# ---------------------------------------------------------------- удары

def runs(values):
    """Непрерывные отрезки в отсортированном списке целых: [(lo, hi), ...]."""
    out, start, prev = [], None, None
    for v in sorted(values):
        if start is None:
            start = prev = v
        elif v == prev + 1:
            prev = v
        else:
            out.append((start, prev))
            start = prev = v
    if start is not None:
        out.append((start, prev))
    return out


def merge_hits(mapping_hits):
    """Группирует удары .nki в удары для манифеста.

    Возвращает список словарей: key, vel, mics{источник микрофона: запись}, d (из группы -D),
    merged (слит из обычного и -D). Порядок: (key, vel, d)."""
    by_pos = defaultdict(dict)
    for h in mapping_hits:
        by_pos[(h['key'], h['vel'])][bool(h['dGroup'])] = h
    result = []
    for (key, vel) in sorted(by_pos):
        pair = by_pos[(key, vel)]
        plain, dgrp = pair.get(False), pair.get(True)
        if plain and dgrp and not (set(plain['mics']) & set(dgrp['mics'])):
            mics = dict(plain['mics'])
            mics.update(dgrp['mics'])
            result.append({'key': key, 'vel': vel, 'mics': mics, 'd': True, 'merged': True})
            continue
        if plain:
            result.append({'key': key, 'vel': vel, 'mics': plain['mics'], 'd': False, 'merged': False})
        if dgrp:
            result.append({'key': key, 'vel': vel, 'mics': dgrp['mics'], 'd': True, 'merged': False})
    return result


def window_for(spec, lo, hi):
    """Окно ручной разметки для отрезка (lo, hi) данных или None, если окон нет или они расходятся."""
    for w in spec.get('windows', []):
        if (w['lo'], w['hi']) == (lo, hi):  # окно описывает именно этот отрезок данных
            return w
    return None


def file_entry(rec, library_root, frames_cache):
    """Запись файла удара по контракту: path, gainDb, start, end. Конец обрезки приводится к отсчёту."""
    path = rec['file']
    gain = float(rec.get('gainDb', 0.0))
    start = int(rec.get('start', 0))
    end = int(rec.get('end', 0))
    problems = []
    if end >= 1 << 31:  # беззнаковая запись отрицательного числа: отсчётов от конца файла
        rel = end - (1 << 32)
        frames = None
        if library_root is not None:
            if path not in frames_cache:
                frames_cache[path] = wav_frames(Path(library_root) / path)
            frames = frames_cache[path]
        if frames is None:
            end = rel
            problems.append('конец обрезки задан от конца файла, длина файла неизвестна')
        else:
            end = frames + rel
    return {'path': path, 'gainDb': gain, 'start': start, 'end': end}, problems


def piece_type(piece, report):
    t = piece.get('type')
    if t is None:
        if piece['id'] != 'orphan':
            report['problems'].append(f"барабан {piece['id']}: нет type")
        return 'other'
    return t


def build_groups(piece, layers, layer_group):
    """Группы барабана для манифеста (порядок как в разметке); пустые группы отбрасываются."""
    out = []
    for gid, gdef in piece.get('groups', {}).items():
        members = [l['id'] for l in layers if layer_group.get(l['id']) == gid]
        if not members:
            continue
        g = {'id': gid, 'name': gdef['name']}
        if gdef.get('hint'):
            g['hint'] = gdef['hint']
        g['role'] = gdef['role']
        g['layers'] = members
        g['pick'] = gdef.get('pick', 'random')
        if gdef.get('picks'):
            g['picks'] = list(gdef['picks'])
        if gdef.get('pair'):
            g['pair'] = copy.deepcopy(gdef['pair'])
        out.append(g)
    return out


def build_packs(kit, mappings, library_root=None):
    """Возвращает (пакеты, отчёт). Пакеты: id -> манифест. Отчёт: словарь для --check."""
    kit = expand_templates(kit)
    index = key_index(kit)
    mic_by_source = {m['source']: m for m in kit['mics']}
    unverified_pack = kit['unverifiedPack']
    pack_meta = {p['id']: p for p in kit['packs']}
    piece_order = {p['id']: i for i, p in enumerate(kit['pieces'])}
    frames_cache = {}

    report = {'problems': [], 'orphans': [], 'excluded': [], 'sources': {}}
    exclude = set(kit.get('excludeFiles', []))   # повреждённые файлы библиотеки: удар с таким файлом не берём

    # pack id -> piece id -> {'piece':..., 'hits': [...], 'layers': {...}}
    store = defaultdict(dict)

    def put_hit(pack_id, opid, piece, hit, layer, gid, files):
        entry = store[pack_id].setdefault(opid, {'piece': piece, 'hits': [], 'layers': {}, 'layer_group': {}})
        entry['layers'][layer['id']] = layer
        if gid:
            entry['layer_group'][layer['id']] = gid
        unverified = bool(layer.get('unverified'))
        h = {
            'id': hit['id'], 'layer': layer['id'], 'key': hit['key'],
            'velLo': hit['vel'], 'velHi': hit['vel'], 'files': files,
        }
        if hit['d']:
            h['peerGroup'] = True
        if unverified:
            h['unverified'] = True
        entry['hits'].append(h)

    # Клавиши базового источника, которых нет ни в одном барабане, не теряем: кладём в барабан orphan
    owned_keys = {k for (_, k) in index}
    base_name = kit['sources'].get('base')
    if base_name in mappings:
        loose = sorted({h['key'] for h in mappings[base_name]['hits'] if h['key'] not in owned_keys})
        if loose:
            orphan = {'id': 'orphan', 'name': 'Клавиши без разметки', 'pack': kit['unverifiedPack'],
                      'source': 'base', 'choke': None,
                      'keys': [{'key': k, 'artic': 'unknown', 'name': 'без разметки', 'windows': []} for k in loose]}
            kit['pieces'].append(orphan)
            piece_order['orphan'] = len(piece_order)
            for spec in orphan['keys']:
                index[('base', spec['key'])] = (orphan, spec)
            report['orphans'] = loose

    used_sources = sorted({p['source'] for p in kit['pieces']})
    for source in used_sources:
        nki = kit['sources'][source]
        if nki not in mappings:
            report['problems'].append(f'нет выгрузки {nki}.json для источника {source}')
            continue
        hits = merge_hits(mappings[nki]['hits'])
        stat = {'nki': nki, 'hits_in': 0, 'hits_out': 0, 'zones_out': 0, 'zones_excluded': 0, 'merges': 0}
        report['sources'][source] = stat

        # Группируем по (барабан, клавиша, -D) для разбиения на отрезки velocity
        groups = defaultdict(list)
        for h in hits:
            owner = index.get((source, h['key']))
            if owner is None:
                continue  # клавишу ведёт другой источник (малый барабан)
            groups[(owner[0]['id'], h['key'], h['d'])].append(h)

        piece_by_id = {p['id']: p for p in kit['pieces']}
        for (pid, key, d), key_hits in sorted(groups.items(), key=lambda kv: (piece_order[kv[0][0]], kv[0][1], kv[0][2])):
            piece = piece_by_id[pid]
            spec = index[(source, key)][1]
            for lo, hi in runs([h['vel'] for h in key_hits]):
                win = window_for(spec, lo, hi)
                run_hits = [h for h in key_hits if lo <= h['vel'] <= hi]
                label = win.get('label', 'unknown') if win else 'unlisted'
                verified = bool(win) and win.get('verified', True) and spec.get('verified', True)
                merged_run = any(h['merged'] for h in run_hits)
                peer = d or merged_run
                # peerOk: удар из группы -D проверен по руководству/заметке и идёт в основной пакет
                unverified = (not verified) or (peer and not (win or {}).get('peerOk'))
                # outId/outKey: имена в основном пакете (бочки: четыре папки .nki на клавише 24);
                # в непроверенном пакете остаются исходные, чтобы барабаны не склеились
                okey = key if unverified else spec.get('outKey', key)
                opid = pid if unverified else piece.get('outId', pid)
                gid = None if unverified else (win or {}).get('group')
                if gid and gid not in piece.get('groups', {}):
                    report['problems'].append(f'{pid}: группа {gid} не описана')
                    gid = None
                pick = (win or {}).get('pick') or piece.get('groups', {}).get(gid, {}).get('pick', 'random')
                lid = f'k{okey}_{lo}_{hi}' + ('_d' if peer else '')
                layer = {'id': lid, 'key': okey, 'artic': spec['artic'], 'label': label,
                         'velLo': lo, 'velHi': hi}
                if pick != 'random':
                    layer['pick'] = pick
                if peer:
                    layer['peerGroup'] = True
                if unverified:
                    layer['unverified'] = True
                for h in run_hits:
                    stat['hits_in'] += 2 if h['merged'] else 1
                    if h['merged']:
                        stat['merges'] += 1
                    files = {}
                    for src_mic, rec in sorted(h['mics'].items(), key=lambda kv: mic_by_source[kv[0]]['out']):
                        mic = mic_by_source.get(src_mic)
                        if mic is None:
                            report['problems'].append(f'микрофон {src_mic} не описан в разметке')
                            continue
                        files[mic['id']], probs = file_entry(rec, library_root, frames_cache)
                        for pr in probs:
                            report['problems'].append(f"{pid} клавиша {key} velocity {h['vel']} {mic['id']}: {pr}")
                    hid = f"{opid}_{okey}_{h['vel']}" + ('_d' if (h['d'] and not h['merged']) else '')
                    if any(f['path'] in exclude for f in files.values()):
                        report['excluded'].append(hid)
                        stat['zones_excluded'] += len(files)
                        continue
                    hit = {'id': hid, 'key': okey, 'vel': h['vel'], 'd': peer}
                    pack_id = unverified_pack if unverified else piece['pack']
                    put_hit(pack_id, opid, piece, hit, layer, gid, files)
                    stat['hits_out'] += 1
                    stat['zones_out'] += len(files)

    # Собираем манифесты
    manifests = {}
    for pack_id in sorted(store):
        meta = pack_meta.get(pack_id)
        if meta is None:
            report['problems'].append(f'пакет {pack_id} не описан в разметке')
            continue
        used_mics = set()
        pieces = []
        # Микрофоны-слои (SUB KICK): в .nki один и тот же файл звучит с каждым ударом барабана, своим каналом
        layer_mics = [lm for lm in kit.get('layerMics', []) if pack_id in lm['packs']]
        for pid in sorted(store[pack_id], key=lambda p: piece_order[store[pack_id][p]['piece']['id']]):
            entry = store[pack_id][pid]
            piece = entry['piece']
            for lm in layer_mics:
                rec, probs = file_entry({'file': lm['file']}, library_root, frames_cache)
                report['problems'].extend(f"{pack_id} {lm['mic']}: {pr}" for pr in probs)
                if lm['mic'] not in {m['id'] for m in kit['mics']}:
                    report['problems'].append(f"layerMics: микрофон {lm['mic']} не описан в разметке")
                    continue
                for h in entry['hits']:
                    h['files'].setdefault(lm['mic'], dict(rec))
            hits = sorted(entry['hits'], key=lambda h: (h['key'], h['velLo'], h['id']))
            for h in hits:
                used_mics.update(h['files'])
            layers = sorted(entry['layers'].values(), key=lambda l: (l['key'], l['velLo'], l['id']))
            keys = sorted({h['key'] for h in hits})
            out_piece = {'id': pid, 'name': piece['name'], 'type': piece_type(piece, report),
                         'keys': keys, 'choke': piece.get('choke'), 'layers': layers}
            groups = build_groups(piece, layers, entry['layer_group'])
            if groups:
                out_piece['groups'] = groups
            out_piece['hits'] = hits
            pieces.append(out_piece)
        mics = [{'id': m['id'], 'name': m['name'], 'role': m['role'], 'out': m['out']}
                for m in sorted(kit['mics'], key=lambda m: m['out']) if m['id'] in used_mics]
        manifests[pack_id] = {
            'format': 1, 'id': pack_id, 'version': 1, 'tier': meta['tier'],
            'requires': list(meta.get('requires', [])), 'title': meta['title'],
            'mics': mics, 'pieces': pieces,
        }

    # Пакеты не из .nki (SUB KICK): один удар на весь диапазон velocity
    mic_by_id = {m['id']: m for m in kit['mics']}
    for ex in kit.get('extraPacks', []):
        meta = pack_meta.get(ex['pack'])
        mic = mic_by_id.get(ex['mic'])
        if meta is None or mic is None:
            report['problems'].append(f"extraPacks {ex['pack']}: нет пакета или микрофона в разметке")
            continue
        pc, key = ex['piece'], ex['piece']['key']
        lid = f"k{key}_{ex['velLo']}_{ex['velHi']}"
        entry, probs = file_entry({'file': ex['file']}, library_root, frames_cache)
        report['problems'].extend(f"{ex['pack']}: {pr}" for pr in probs)
        manifests[ex['pack']] = {
            'format': 1, 'id': ex['pack'], 'version': 1, 'tier': meta['tier'],
            'requires': list(meta.get('requires', [])), 'title': meta['title'],
            'mics': [{'id': mic['id'], 'name': mic['name'], 'role': mic['role'], 'out': mic['out']}],
            'pieces': [{
                'id': pc['id'], 'name': pc['name'], 'type': pc['type'], 'keys': [key], 'choke': None,
                'layers': [{'id': lid, 'key': key, 'artic': pc['type'], 'label': 'sub',
                            'velLo': ex['velLo'], 'velHi': ex['velHi']}],
                'groups': [{'id': 'main', 'name': 'SUB KICK', 'hint': 'Один низкий удар на любой силе', 'role': 'main',
                            'layers': [lid], 'pick': 'random'}],
                'hits': [{'id': f"{pc['id']}_{key}_sub", 'layer': lid, 'key': key,
                          'velLo': ex['velLo'], 'velHi': ex['velHi'], 'files': {mic['id']: entry}}],
            }],
        }
    return manifests, report


def dump(manifest):
    """Детерминированная запись: отступ 1, юникод как есть, LF в конце."""
    return json.dumps(manifest, ensure_ascii=False, indent=1) + '\n'


def write_packs(manifests, out_dir):
    out = Path(out_dir)
    out.mkdir(parents=True, exist_ok=True)
    for pack_id, m in manifests.items():
        (out / f'{pack_id}.json').write_text(dump(m), encoding='utf-8', newline='')


# ---------------------------------------------------------------- проверка

def pack_files(manifest):
    return {f['path'] for p in manifest['pieces'] for h in p['hits'] for f in h['files'].values()}


ROLES = {'main', 'ghost', 'rimshot', 'rimclick', 'fill', 'roll', 'crescendo', 'flam', 'choke', 'open',
         'closed', 'half', 'pedal', 'closing', 'bell', 'edge', 'bow', 'other'}
PICKS = {'random', 'cycle', 'exact'}


def check_groups(manifest, manifests, out, require=False):
    """Проверка разметки групп (setup-spec §3.2). Возвращает число ошибок.
    require: каждый слой основного пакета обязан входить в группу."""
    errors = 0
    verified_pack = require and manifest['tier'] != 'unverified'
    for p in manifest['pieces']:
        layer_ids = {l['id'] for l in p['layers']}
        seen = {}
        gids = set()
        for g in p.get('groups', []):
            if g['id'] in gids:
                out(f"!  {p['id']}: группа {g['id']} повторяется")
                errors += 1
            gids.add(g['id'])
            if g['role'] not in ROLES:
                out(f"!  {p['id']}/{g['id']}: неизвестная роль {g['role']}")
                errors += 1
            if g['pick'] not in PICKS or any(x not in PICKS for x in g.get('picks', [])):
                out(f"!  {p['id']}/{g['id']}: неизвестный режим")
                errors += 1
            if g.get('picks') and g['pick'] not in g['picks']:
                out(f"!  {p['id']}/{g['id']}: pick не входит в picks")
                errors += 1
            for lid in g['layers']:
                if lid not in layer_ids:
                    out(f"!  {p['id']}/{g['id']}: нет слоя {lid}")
                    errors += 1
                if lid in seen:
                    out(f"!  {p['id']}: слой {lid} в группах {seen[lid]} и {g['id']}")
                    errors += 1
                seen[lid] = g['id']
            pair = g.get('pair')
            if pair:
                ok = any(q['type'] == pair['type'] and any(x['id'] == pair['group'] for x in q.get('groups', []))
                         for mm in manifests.values() for q in mm['pieces'])
                if not ok:
                    out(f"!  {p['id']}/{g['id']}: парная группа {pair} не найдена")
                    errors += 1
        if verified_pack:
            loose = sorted(layer_ids - set(seen))
            if loose:
                out(f"!  {p['id']}: слои без группы: {loose}")
                errors += 1
    return errors


def check(kit, mappings, library_root, out=print, expected=(EXPECTED_HITS, EXPECTED_ZONES)):
    """Печатает отчёт и возвращает число ошибок. expected=None отключает сверку с docs/library-structure.md."""
    manifests, report = build_packs(kit, mappings, library_root)
    errors = 0
    root = Path(library_root) if library_root and Path(library_root).is_dir() else None
    if root is None:
        out(f'! каталог библиотеки не найден ({library_root}), проверка файлов на диске и размеров пропущена')

    for line in report['problems']:
        out(f'! {line}')
        errors += 1

    grouped = any(p.get('groups') for m in manifests.values() for p in m['pieces'])
    sizes = {}
    for pack_id, m in manifests.items():
        hits = [h for p in m['pieces'] for h in p['hits']]
        out(f"\n== {pack_id} [{m['tier']}] {m['title']}")
        out(f"   барабанов {len(m['pieces'])}, ударов {len(hits)}, микрофонов {len(m['mics'])}, "
            f"помечено unverified: {sum(1 for h in hits if h.get('unverified'))}")

        no_files = [h['id'] for h in hits if not h['files']]
        out(f'   ударов без файла: {len(no_files)}')
        errors += len(no_files)

        missing = []
        total = 0
        if root is not None:
            for path in sorted(pack_files(m)):
                f = root / path
                if f.is_file():
                    total += f.stat().st_size
                else:
                    missing.append(path)
            sizes[pack_id] = (total, len(pack_files(m)))
        out(f'   файлов нет на диске: {len(missing)}' if root is not None else '   файлы на диске не проверялись')
        for path in missing[:5]:
            out(f'     нет: {path}')
        errors += len(missing)

        seen = defaultdict(list)
        for p in m['pieces']:
            for h in p['hits']:
                seen[(p['id'], h['key'], h['velLo'])].append(h['id'])
        clash = {k: v for k, v in seen.items() if len(v) > 1}
        out(f'   пересечений velocity на одной клавише: {len(clash)}')
        errors += check_groups(m, manifests, out, require=grouped)
        for (pid, key, vel), ids in sorted(clash.items())[:5]:
            out(f'     {pid} клавиша {key} velocity {vel}: {ids}')
        errors += len(clash)

        for p in m['pieces']:
            for key in p['keys']:
                wins = [l for l in p['layers'] if l['key'] == key]
                text = ', '.join(
                    f"{l['velLo']}-{l['velHi']}" + ('*' if l.get('unverified') else '') +
                    (f" {l['pick']}" if l.get('pick') else '')
                    for l in wins)
                count = sum(1 for h in p['hits'] if h['key'] == key)
                out(f"   {p['id']:<11} {key:>3} {note_name(key):<4} {count:>3} уд.  {text}")
    out('\n(* помечено unverified; "exact" значит, что velocity выбирает конкретный удар)')

    # Сверка с документом
    out('\n== Сверка с docs/library-structure.md')
    snare_sources = [s for s in report['sources'] if s != 'base']
    base = report['sources'].get('base')
    if base is None:
        out('! нет базового источника')
        return errors + 1
    base_nki = mappings[kit['sources']['base']]
    if expected is not None:
        for snare in snare_sources:
            nki_name = kit['sources'][snare]
            s = report['sources'][snare]
            nki = mappings[nki_name]
            hits_total = base['hits_in'] + s['hits_in']
            zones_total = base['zones_out'] + s['zones_out'] + base['zones_excluded'] + s['zones_excluded']
            out(f"   {nki_name}: ударов {hits_total} (ожидание {expected[0]}), "
                f"зон {zones_total} (ожидание {expected[1]}); зон в выгрузке {nki['zones']}")
            if hits_total != expected[0]:
                out(f'!  расхождение по ударам: {hits_total} вместо {expected[0]}')
                errors += 1
            if zones_total != expected[1]:
                out(f'!  расхождение по зонам: {zones_total} вместо {expected[1]}')
                errors += 1
    out(f"   слито обычный + -D в один удар (A5): {base['merges']}")
    if report['excluded']:
        out(f"   вырезано ударов с повреждёнными файлами (excludeFiles): {len(report['excluded'])}: {sorted(set(report['excluded']))}")
    if report['orphans']:
        out(f"!  клавиши без разметки (сохранены в барабан orphan пакета непроверенного): {report['orphans']}")
    # Различия между .nki вне малого барабана
    base_hits = {(h['key'], h['vel'], h['dGroup']): h for h in base_nki['hits']}
    snare_keys = {s['key'] for p in expand_templates(kit)['pieces'] if p['source'] != 'base' for s in p['keys']}
    for snare in snare_sources:
        other = {(h['key'], h['vel'], h['dGroup']): h for h in mappings[kit['sources'][snare]]['hits']}
        diff_keys = sorted({k[0] for k in set(base_hits) | set(other) if base_hits.get(k) != other.get(k)} - snare_keys)
        gain_only = all(
            {m: {k: v for k, v in e.items() if k != 'gainDb'} for m, e in base_hits[k]['mics'].items()} ==
            {m: {k: v for k, v in e.items() if k != 'gainDb'} for m, e in other[k]['mics'].items()}
            for k in base_hits if k[0] in diff_keys and k in other)
        if diff_keys:
            out(f"   {kit['sources'][snare]} отличается от базы вне малого на клавишах {diff_keys}"
                + (' (только громкость зон gainDb, берётся из базы)' if gain_only else ' (не только громкость!)'))
            if not gain_only:
                errors += 1

    if sizes:
        out('\n== Размеры пакетов (исходные WAV)')
        out(f"   {'пакет':<24}{'файлов':>8}{'ГБ':>8}")
        for pack_id, (total, n) in sizes.items():
            out(f'   {pack_id:<24}{n:>8}{total / 1e9:>8.2f}')
        all_files = set()
        for m in manifests.values():
            all_files |= pack_files(m)
        out(f"   {'уникальных файлов всего':<24}{len(all_files):>8}")
    out(f'\nОшибок: {errors}')
    return errors


def main(argv=None):
    ap = argparse.ArgumentParser(description='Манифесты пакетов Studio600 из выгрузки .nki и ручной разметки')
    ap.add_argument('--kit', default=str(DEFAULT_KIT), help='ручная разметка')
    ap.add_argument('--mapping', default=str(DEFAULT_MAPPING), help='каталог выгрузок из nki_extract.py')
    ap.add_argument('--library', default=str(DEFAULT_LIBRARY), help='корень библиотеки (пути файлов относительны ему)')
    ap.add_argument('--out', default=str(DEFAULT_OUT), help='куда писать манифесты')
    ap.add_argument('--check', action='store_true', help='проверить и не писать файлы')
    ap.add_argument('--no-expect', action='store_true', help='не сверять число ударов и зон с docs/library-structure.md (для v2: проникание томов добавляет зоны)')
    args = ap.parse_args(argv)

    kit = load_json(args.kit)
    mappings = load_mappings(args.mapping)
    if not mappings:
        print(f'в {args.mapping} нет выгрузок; сначала tools/nki_extract.py', file=sys.stderr)
        return 2

    if args.check:
        return 1 if check(kit, mappings, args.library, expected=None if args.no_expect else (EXPECTED_HITS, EXPECTED_ZONES)) else 0

    library = args.library if Path(args.library).is_dir() else None
    manifests, report = build_packs(kit, mappings, library)
    for line in report['problems']:
        print(f'! {line}', file=sys.stderr)
    write_packs(manifests, args.out)
    for pack_id, m in manifests.items():
        hits = sum(len(p['hits']) for p in m['pieces'])
        print(f"{pack_id}: ударов {hits} -> {Path(args.out) / (pack_id + '.json')}")
    return 1 if report['problems'] else 0


if __name__ == '__main__':
    sys.exit(main())
