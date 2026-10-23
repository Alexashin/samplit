"""Тесты tools/mark_kit.py на маленькой синтетической выгрузке.

Запуск: python -I tests/test_mark_kit.py
"""
import json
import sys
import tempfile
import unittest
import wave
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'tools'))
import mark_kit  # noqa: E402


def rec(path, **extra):
    d = {'file': path}
    d.update(extra)
    return d


def hit(key, vel, mics, d=False):
    return {'key': key, 'note': mark_kit.note_name(key), 'vel': vel, 'dGroup': d, 'mics': mics}


def synthetic_mapping():
    """Кик (разметка полная), малый с окном без описания, тарелка с -D, райд со слитым ударом, клавиша без разметки."""
    kick = lambda v: {'KICK-IN': rec(f'kick/KICK-IN/{v}.wav'), 'OVER-1-Hat': rec(f'kick/OVER-1-Hat/{v}.wav')}
    hits = []
    for v in (80, 81, 82, 83):
        hits.append(hit(21, v, kick(v)))
    for v in (120, 121):
        hits.append(hit(26, v, {'SN-top': rec(f'sn/SN-top/{v}.wav'), 'OVER-1-Hat': rec(f'sn/OVER-1-Hat/{v}.wav')}))
    for v in (30, 31):   # окно, которого нет в разметке
        hits.append(hit(26, v, {'SN-top': rec(f'sn/SN-top/{v}.wav')}))
    for v in (120, 121):
        hits.append(hit(36, v, {'OVER-1-Hat': rec(f'cr/OVER-1-Hat/{v}.wav'), 'OVER-2-Ride': rec(f'cr/OVER-2-Ride/{v}.wav')}))
    for v in (112, 113):
        hits.append(hit(36, v, {'OVER-1-Hat': rec(f'cr/OVER-1-Hat/{v}.wav'), 'OVER-2-Ride': rec(f'cr/OVER-2-Ride/{v}.wav')}, d=True))
    # Слитый удар: обычная группа и -D покрывают разные микрофоны одного удара
    hits.append(hit(93, 108, {'RIDE': rec('ride/RIDE/108.wav')}))
    hits.append(hit(93, 108, {'OVER-1-Hat': rec('ride/OVER-1-Hat/108.wav')}, d=True))
    hits.append(hit(99, 100, {'OVER-1-Hat': rec('orphan/OVER-1-Hat/100.wav')}))   # клавиша без разметки
    return {'source': 'FAKE.nki', 'groups': [], 'zones': 0, 'hits': hits}


def synthetic_kit():
    return {
        'sources': {'base': 'FAKE'},
        'unverifiedPack': 'unv',
        'mics': [
            {'id': 'kick_in', 'source': 'KICK-IN', 'name': 'Kick In', 'role': 'close', 'out': 1},
            {'id': 'sn_top', 'source': 'SN-top', 'name': 'Snare Top', 'role': 'close', 'out': 2},
            {'id': 'oh_1', 'source': 'OVER-1-Hat', 'name': 'OH1', 'role': 'overhead', 'out': 3},
            {'id': 'oh_2', 'source': 'OVER-2-Ride', 'name': 'OH2', 'role': 'overhead', 'out': 4},
            {'id': 'ride', 'source': 'RIDE', 'name': 'Ride', 'role': 'close', 'out': 5},
        ],
        'packs': [
            {'id': 'core', 'title': 'Ядро', 'tier': 'base', 'requires': []},
            {'id': 'cym', 'title': 'Тарелки', 'tier': 'addon', 'requires': []},
            {'id': 'unv', 'title': 'Непроверенное', 'tier': 'unverified', 'requires': []},
        ],
        'pieces': [
            {'id': 'kick', 'name': 'Кик', 'pack': 'core', 'source': 'base', 'choke': None,
             'keys': [{'key': 21, 'artic': 'a', 'windows': [{'lo': 80, 'hi': 83, 'label': 'strong'}]}]},
            {'id': 'snare', 'name': 'Малый', 'pack': 'core', 'source': 'base', 'choke': None,
             'keys': [{'key': 26, 'artic': 'center',
                       'windows': [{'lo': 120, 'hi': 121, 'label': 'strong', 'pick': 'exact'}]}]},
            {'id': 'crash', 'name': 'Крэш', 'pack': 'cym', 'source': 'base', 'choke': 'cymbals',
             'keys': [{'key': 36, 'artic': 'calm',
                       'windows': [{'lo': 120, 'hi': 121, 'label': 'calm'}]}]},
            {'id': 'ride', 'name': 'Райд', 'pack': 'cym', 'source': 'base', 'choke': None,
             'keys': [{'key': 93, 'artic': 'dome', 'windows': [{'lo': 108, 'hi': 108, 'label': 'dome'}]}]},
        ],
    }


class MarkKitTest(unittest.TestCase):
    def build(self, library=None):
        return mark_kit.build_packs(synthetic_kit(), {'FAKE': synthetic_mapping()}, library)


    def test_excluded_file_drops_the_whole_hit(self):
        kit = synthetic_kit()
        kit['excludeFiles'] = ['kick/KICK-IN/81.wav']
        manifests, report = mark_kit.build_packs(kit, {'FAKE': synthetic_mapping()})
        ids = [h['id'] for p in manifests['core']['pieces'] if p['id'] == 'kick' for h in p['hits']]
        self.assertNotIn('kick_21_81', ids)
        self.assertEqual(len(ids), 3)
        self.assertEqual(report['excluded'], ['kick_21_81'])

    def hits_of(self, manifest):
        return {h['id']: h for p in manifest['pieces'] for h in p['hits']}

    def test_verified_and_unverified_go_to_their_packs(self):
        packs, report = self.build()
        self.assertEqual(report['problems'], [])
        self.assertEqual(sorted(packs), ['core', 'cym', 'unv'])
        core = self.hits_of(packs['core'])
        # кик: 4 удара; малый: 2 удара из описанного окна 120-121
        self.assertEqual(sorted(core), ['kick_21_80', 'kick_21_81', 'kick_21_82', 'kick_21_83',
                                        'snare_26_120', 'snare_26_121'])
        self.assertTrue(all('unverified' not in h for h in core.values()))
        unv = self.hits_of(packs['unv'])
        # окно 30-31 малого, удары -D крэша, слитый удар райда и клавиша без разметки
        self.assertIn('snare_26_30', unv)
        self.assertIn('crash_36_112_d', unv)
        self.assertIn('ride_93_108', unv)
        self.assertIn('orphan_99_100', unv)
        self.assertTrue(all(h.get('unverified') for h in unv.values()))

    def test_merged_hit_has_all_mics_of_both_groups(self):
        packs, _ = self.build()
        merged = self.hits_of(packs['unv'])['ride_93_108']
        self.assertEqual(sorted(merged['files']), ['oh_1', 'ride'])
        self.assertTrue(merged['peerGroup'])

    def test_same_piece_in_two_packs_keeps_its_id(self):
        packs, _ = self.build()
        ids = {pid for m in packs.values() for pid in (p['id'] for p in m['pieces'])}
        self.assertIn('snare', {p['id'] for p in packs['core']['pieces']})
        self.assertIn('snare', {p['id'] for p in packs['unv']['pieces']})
        self.assertTrue({'kick', 'snare', 'crash', 'ride', 'orphan'} <= ids)

    def test_orphan_keys_are_reported(self):
        _, report = self.build()
        self.assertEqual(report['orphans'], [99])

    def test_layers_follow_data_and_pick_is_kept(self):
        packs, _ = self.build()
        snare = next(p for p in packs['core']['pieces'] if p['id'] == 'snare')
        layer = snare['layers'][0]
        self.assertEqual((layer['key'], layer['velLo'], layer['velHi'], layer['label']), (26, 120, 121, 'strong'))
        self.assertEqual(layer['pick'], 'exact')
        for h in snare['hits']:
            self.assertEqual(h['layer'], layer['id'])
            self.assertEqual(h['velLo'], h['velHi'])

    def test_mics_of_pack_are_only_the_used_ones_and_sorted_by_output(self):
        packs, _ = self.build()
        self.assertEqual([m['id'] for m in packs['core']['mics']], ['kick_in', 'sn_top', 'oh_1'])
        self.assertEqual([m['id'] for m in packs['cym']['mics']], ['oh_1', 'oh_2'])

    def test_output_is_deterministic(self):
        a = mark_kit.dump(self.build()[0]['core'])
        b = mark_kit.dump(self.build()[0]['core'])
        self.assertEqual(a, b)
        self.assertTrue(a.endswith('}\n'))

    def test_end_of_trim_is_converted_using_file_length(self):
        mapping = synthetic_mapping()
        mapping['hits'][0]['mics']['KICK-IN'].update(start=0, end=(1 << 32) - 1152, gainDb=-3.0)
        with tempfile.TemporaryDirectory() as tmp:
            wav = Path(tmp) / 'kick/KICK-IN/80.wav'
            wav.parent.mkdir(parents=True)
            with wave.open(str(wav), 'wb') as w:
                w.setnchannels(1)
                w.setsampwidth(3)
                w.setframerate(44100)
                w.writeframes(b'\x00\x00\x00' * 2000)
            packs, report = mark_kit.build_packs(synthetic_kit(), {'FAKE': mapping}, tmp)
        entry = self.hits_of(packs['core'])['kick_21_80']['files']['kick_in']
        self.assertEqual(entry['end'], 2000 - 1152)
        self.assertEqual(entry['gainDb'], -3.0)
        self.assertEqual(report['problems'], [])

    def test_trim_without_file_is_reported_and_kept_relative(self):
        mapping = synthetic_mapping()
        mapping['hits'][0]['mics']['KICK-IN'].update(start=0, end=(1 << 32) - 100)
        packs, report = mark_kit.build_packs(synthetic_kit(), {'FAKE': mapping}, None)
        self.assertEqual(self.hits_of(packs['core'])['kick_21_80']['files']['kick_in']['end'], -100)
        self.assertEqual(len(report['problems']), 1)

    def test_duplicate_key_in_markup_is_an_error(self):
        kit = synthetic_kit()
        kit['pieces'][1]['keys'][0]['key'] = 21
        with self.assertRaises(ValueError):
            mark_kit.build_packs(kit, {'FAKE': synthetic_mapping()}, None)

    def test_check_reports_missing_files(self):
        with tempfile.TemporaryDirectory() as tmp:
            lines = []
            errors = mark_kit.check(synthetic_kit(), {'FAKE': synthetic_mapping()}, tmp,
                                    out=lines.append, expected=None)
        self.assertGreater(errors, 0)
        self.assertTrue(any('нет на диске' in l for l in lines))

    def test_check_is_clean_when_files_exist(self):
        mapping = synthetic_mapping()
        with tempfile.TemporaryDirectory() as tmp:
            for h in mapping['hits']:
                for e in h['mics'].values():
                    f = Path(tmp) / e['file']
                    f.parent.mkdir(parents=True, exist_ok=True)
                    f.write_bytes(b'RIFF')
            lines = []
            errors = mark_kit.check(synthetic_kit(), {'FAKE': mapping}, tmp, out=lines.append, expected=None)
        self.assertEqual(errors, 0, '\n'.join(lines))

    def test_missing_mapping_is_reported(self):
        kit = synthetic_kit()
        kit['sources']['base'] = 'ABSENT'
        _, report = mark_kit.build_packs(kit, {'FAKE': synthetic_mapping()}, None)
        self.assertTrue(report['problems'])

    def test_real_markup_file_is_valid_json_and_consistent(self):
        kit = json.loads((Path(__file__).resolve().parent.parent / 'tools' / 'kit-studio600.json').read_text(encoding='utf-8'))
        expanded = mark_kit.expand_templates(kit)
        mark_kit.key_index(expanded)   # клавиши не пересекаются внутри источника
        outputs = sorted(m['out'] for m in kit['mics'])
        self.assertEqual(outputs, list(range(1, 16)))
        self.assertEqual(len({m['id'] for m in kit['mics']}), 15)
        packs = {p['id'] for p in kit['packs']}
        self.assertTrue({p['pack'] for p in kit['pieces']} <= packs)


if __name__ == '__main__':
    unittest.main()
