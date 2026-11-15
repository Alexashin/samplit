"""Отчёт покрытия: каждый WAV из выгрузок .nki должен быть в каком-то ударе манифестов пака.

    python tools/coverage_report.py [--mapping DIR] [--packs DIR]

Код выхода 1, если есть WAV из .nki, не попавшие ни в один удар.
"""
import argparse
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent


def norm(p):
    return p.replace('\\', '/').lower()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mapping', default=str(ROOT / 'sample_library' / 'mapping-v2'))
    ap.add_argument('--packs', default=str(ROOT / 'sample_library' / 'packs-src-v2'))
    a = ap.parse_args()

    nki = {}
    for f in sorted(Path(a.mapping).glob('*.json')):
        for h in json.loads(f.read_text(encoding='utf-8'))['hits']:
            for m in h['mics'].values():
                nki.setdefault(norm(m['file']), f.stem)
    used = set()
    for f in Path(a.packs).glob('*.json'):
        for p in json.loads(f.read_text(encoding='utf-8')).get('pieces', []):
            for h in p['hits']:
                for e in h['files'].values():
                    used.add(norm(e['path']))
    missing = {k: v for k, v in nki.items() if k not in used}
    print(f'WAV в .nki: {len(nki)}, в ударах паков: {len(used)}, не покрыто: {len(missing)}')
    by = {}
    for k, v in missing.items():
        by.setdefault((v, k.split('/')[0]), []).append(k)
    for (src, top), l in sorted(by.items()):
        print(f'  {src} / {top}: {len(l)}, например {l[0]}')
    return 1 if missing else 0


sys.exit(main())
