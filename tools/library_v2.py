# SPDX-License-Identifier: LicenseRef-Samplit-Proprietary
"""Подготовка данных обновления библиотеки Studio600 (L1, 12.11.2026).

Из выгрузок .nki (nki_extract.py) делает:
  * каталог маппинга v2: микрофон SUB KICK убран (его файла нет в присланном), удары томов
    дополнены проникновением в микрофоны KICK-IN, KICK-OUT, SN-top, SN-bot, RIDE (файлы
    TOMS-Pearl-Masters/<микрофон>/<то же имя>.wav; совпадение номеров и длин проверено на всех 544 ударах);
  * tools/kit-studio600-v2.json: источники новых малых барабанов, пять новых пакетов малых,
    без excludeFiles (файлы A2 0-397, 0-398 исправлены).

    python tools/library_v2.py --new-maps <папка с ED-*.json новых .nki> [--library <Studio600-v2>]
Затем: python tools/mark_kit.py --kit tools/kit-studio600-v2.json --mapping sample_library/mapping-v2 \
           --library sample_library/Studio600-v2 --out sample_library/packs-src-v2
"""
import argparse
import copy
import json
import os
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
BLEED = ['KICK-IN', 'KICK-OUT', 'SN-top', 'SN-bot', 'RIDE']
NEW_SNARES = [  # id, nki, название
    ('snare_amb_mid',  'ED-Ahead-AMB-MID', 'Ahead AMB Mid'),
    ('snare_tone',     'ED-Ahead-TONE',    'Ahead Tone'),
    ('snare_fat_mid',  'ED-FAT-MID',       'FAT Mid'),
    ('snare_ludwig_hi',  'ED-LUDWIG-HI',   'Ludwig Hi'),
    ('snare_ludwig_mid', 'ED-LUDWIG-MID',  'Ludwig Mid'),
]


def fix_mapping(d, library):
    stats = {'sub_kick': 0, 'bleed_added': 0, 'bleed_missing': 0}
    for h in d['hits']:
        if 'SUB KICK' in h['mics']:
            del h['mics']['SUB KICK']; stats['sub_kick'] += 1
        toms = [v['file'] for v in h['mics'].values() if v['file'].startswith('TOMS-Pearl-Masters/')]
        if not toms:
            continue
        name = os.path.basename(toms[0])
        for m in BLEED:
            if m in h['mics']:
                continue
            rel = f'TOMS-Pearl-Masters/{m}/{name}'
            if library and not (Path(library) / rel).is_file():
                stats['bleed_missing'] += 1; continue
            h['mics'][m] = {'file': rel}; stats['bleed_added'] += 1
    d['hits'] = [h for h in d['hits'] if h['mics']]
    d['groups'] = [g for g in d['groups'] if g != 'SUB KICK']
    return stats


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--new-maps', required=True)
    ap.add_argument('--old-maps', default=str(ROOT / 'sample_library' / 'mapping'))
    ap.add_argument('--out', default=str(ROOT / 'sample_library' / 'mapping-v2'))
    ap.add_argument('--library', default=str(ROOT / 'sample_library' / 'Studio600-v2'))
    ap.add_argument('--kit', default=str(ROOT / 'tools' / 'kit-studio600.json'))
    ap.add_argument('--kit-out', default=str(ROOT / 'tools' / 'kit-studio600-v2.json'))
    a = ap.parse_args()
    out = Path(a.out); out.mkdir(parents=True, exist_ok=True)
    srcs = {}
    for f in sorted(Path(a.new_maps).glob('ED-*.json')):
        srcs[f.stem] = f
    for n in ('ED-Ahead-LOW', 'ED-Ambasador-NEW'):  # старые .nki не обновлялись
        srcs.setdefault(n, Path(a.old_maps) / f'{n}.json')
    for name, f in sorted(srcs.items()):
        d = json.load(open(f, encoding='utf-8'))
        st = fix_mapping(d, a.library)
        json.dump(d, open(out / f'{name}.json', 'w', encoding='utf-8'), ensure_ascii=False)
        print(name, st)

    kit = json.load(open(a.kit, encoding='utf-8'))
    kit['comment'] += ' v2 (12.11.2026): новые малые, проникание томов, без excludeFiles; создано tools/library_v2.py.'
    kit['excludeFiles'] = []
    kit['excludeFilesNote'] = 'Файлы A2 0-397, 0-398 исправлены в обновлении 12.11.2026, исключений нет.'
    kit['sources']['base'] = 'ED-Ahead-LIVE'
    tmpl = next(p for p in kit['pieces'] if p['id'] == 'snare_live')
    pos = [p['id'] for p in kit['pieces']].index('snare_hi') + 1
    for i, (pid, nki, title) in enumerate(NEW_SNARES):
        kit['sources'][pid] = nki
        pack = 'studio600-' + pid.replace('snare_', 'snare-').replace('_', '-')
        kit['packs'].insert([p['id'] for p in kit['packs']].index('studio600-snare-hi') + 1 + i,
                            {'id': pack, 'title': f'Studio600 Drum LIB: малый {title}', 'tier': 'addon', 'requires': []})
        piece = copy.deepcopy(tmpl)
        piece.update({'id': pid, 'name': f'Малый барабан, {title}', 'pack': pack, 'source': pid})
        kit['pieces'].insert(pos + i, piece)
    json.dump(kit, open(a.kit_out, 'w', encoding='utf-8'), ensure_ascii=False, indent=2)
    print('kit ->', a.kit_out)


if __name__ == '__main__':
    main()
