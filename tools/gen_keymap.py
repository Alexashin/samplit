#!/usr/bin/env python3
"""Генерирует docs/keymap.md: какая MIDI-клавиша какой барабан и приём играет.

Источник: tools/kit-studio600.json (разметка из .nki). Нумерация нот как в плагине и Cubase по умолчанию: C3 = 60.

    python tools/gen_keymap.py          # пишет docs/keymap.md
    python tools/gen_keymap.py --check  # ничего не пишет, код 1, если файл устарел
"""
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "tools" / "kit-studio600.json"
OUT = ROOT / "docs" / "keymap.md"

NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def note_name(n: int) -> str:
    return f"{NAMES[n % 12]}{n // 12 - 2}"


def rows_for(piece, templates):
    template = templates.get(piece.get("template"))
    if template is not None:
        offset = piece.get("keyOffset", 0)
        for k in template["keys"]:
            key = k["key"] if "key" in k else k.get("offset", 0) + offset
            yield key, k
    else:
        yield from ((k["key"], k) for k in piece.get("keys", []))


def build() -> str:
    data = json.loads(SRC.read_text(encoding="utf-8"))
    templates = data.get("templates", {})
    lines = [
        "# Раскладка клавиш Studio600",
        "",
        "Файл создаётся скриптом `python tools/gen_keymap.py` из `tools/kit-studio600.json`, руками не править.",
        "Нумерация нот как в Samplit и Cubase по умолчанию: **C3 = 60**. Раскладка не GM; для Cubase 15 её можно перенести в карту ударных вручную по этой таблице.",
        "",
        "Строки со словом «проверить» в колонке «Заметка» взяты из `.nki`, но в руководстве не подтверждены (см. [open-questions.md](open-questions.md)).",
        "",
    ]
    for piece in data["pieces"]:
        rows = sorted(rows_for(piece, templates), key=lambda r: r[0])
        if not rows:
            continue
        lines += [f"## {piece['name']} (`{piece['id']}`)", "", "| Клавиша | MIDI | Приём | Заметка |", "|---|---|---|---|"]
        seen = set()
        for key, k in rows:
            if key in seen:
                continue
            seen.add(key)
            unverified = any(w.get("verified") is False for w in k.get("windows", []))
            note = "проверить" if unverified else ""
            lines.append(f"| {note_name(key)} | {key} | {k.get('name', k.get('artic', ''))} | {note} |")
        lines.append("")
    return "\n".join(lines).rstrip() + "\n"


def main() -> int:
    text = build()
    if "--check" in sys.argv:
        current = OUT.read_text(encoding="utf-8").replace("\r\n", "\n") if OUT.exists() else ""
        if current != text:
            print("docs/keymap.md устарел: python tools/gen_keymap.py")
            return 1
        return 0
    OUT.write_text(text, encoding="utf-8", newline="\n")
    print(f"записано {OUT}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
