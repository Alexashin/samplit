#!/usr/bin/env python3
"""Генерирует карты ударных Cubase (.drm) для Studio600 в tools/dist/drum-maps/.

Источник: tools/kit-studio600.json (как gen_keymap.py). Формат взят из образцов Cubase 15 (docs/samples/*.drm).
Нумерация нот: C3 = 60.

    Studio600.drm         вход = выход = клавиша Studio600 (имена приёмов в редакторе ударных)
    GM-to-Studio600.drm   вход General MIDI (кик 36, малый 38...), выход клавиша Studio600

    python tools/gen_drum_map.py          # пишет файлы
    python tools/gen_drum_map.py --check  # код 1, если файлы устарели
"""
import json
import re
import sys
from pathlib import Path
from xml.sax.saxutils import quoteattr

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "tools" / "kit-studio600.json"
OUT = ROOT / "tools" / "dist" / "drum-maps"

# Подпись барабана в названии приёма
PREFIX = {"kick": "Бочка", "snare": "Малый", "hat": "Хэт", "ride": "Райд"}

# GM -> клавиша Studio600 (удар по умолчанию). Тома: правая рука, она в руководстве подтверждена
GM = {
    35: (24, "Acoustic Bass Drum"), 36: (24, "Bass Drum"),
    37: (25, "Side Stick"), 38: (26, "Acoustic Snare"), 40: (26, "Electric Snare"),
    41: (97, "Low Floor Tom"), 43: (97, "High Floor Tom"), 45: (99, "Low Tom"),
    47: (101, "Low-Mid Tom"), 48: (101, "Hi-Mid Tom"), 50: (103, "High Tom"),
    42: (48, "Closed Hi-Hat"), 44: (77, "Pedal Hi-Hat"), 46: (64, "Open Hi-Hat"),
    49: (36, "Crash Cymbal 1"), 57: (40, "Crash Cymbal 2"), 52: (44, "Chinese Cymbal"), 55: (47, "Splash Cymbal"),
    51: (84, "Ride Cymbal 1"), 59: (84, "Ride Cymbal 2"), 53: (91, "Ride Bell"),
}


def key_names() -> dict:
    data = json.loads(SRC.read_text(encoding="utf-8"))
    templates = data.get("templates", {})
    names = {}
    for piece in data["pieces"]:
        template = templates.get(piece.get("template"))
        if template is not None:
            offset = piece.get("keyOffset", 0)
            rows = [(k["key"] if "key" in k else k.get("offset", 0) + offset, k) for k in template["keys"]]
        else:
            rows = [(k.get("outKey", k["key"]), k) for k in piece.get("keys", [])]
        base = re.sub(r"\s*\(.*?\)", "", piece["name"])
        prefix = PREFIX.get(piece["type"])
        for key, k in rows:
            if key in names:
                continue
            what = k.get("name", k.get("artic", ""))
            if piece["type"] in ("kick",):
                label = prefix
            elif prefix:
                label = f"{prefix}: {what}"
            else:
                label = f"{base}: {what}"
            names[key] = label[:60]
    return names


def item(i_note, o_note, name, mute=False) -> str:
    wide = ' wide="true"' if name != "---" else ""
    return f"""      <item>
         <int name="INote" value="{i_note}"/>
         <int name="ONote" value="{o_note}"/>
         <int name="Channel" value="9"/>
         <float name="Length" value="200"/>
         <int name="Mute" value="{1 if mute else 0}"/>
         <int name="DisplayNote" value="{i_note}"/>
         <int name="HeadSymbol" value="0"/>
         <int name="Voice" value="0"/>
         <int name="PortIndex" value="0"/>
         <string name="Name" value={quoteattr(name)}{wide}/>
         <int name="QuantizeIndex" value="0"/>
         <int name="NoteheadSet" value="0"/>
         <string name="InstrumentEntityID" value=""/>
         <string name="TechniqueEntityID" value=""/>
      </item>"""


def drum_map(title: str, rows: dict) -> str:
    """rows: входная нота -> (выходная нота, имя). Остальные ноты 0-127 пустые, в списке идут после занятых"""
    items = [item (n, *rows[n]) if n in rows else item(n, n, "---", mute=True) for n in range(128)]
    order = sorted(rows) + [n for n in range(128) if n not in rows]
    order_xml = "\n".join(f'      <item value="{n}"/>' for n in order)
    text = f"""<?xml version="1.0" encoding="utf-8"?>
<DrumMap>
   <string name="Name" value={quoteattr(title)} wide="true"/>
   <list name="Quantize" type="list">
      <item>
         <int name="Grid" value="4"/>
         <int name="Type" value="0"/>
         <float name="Swing" value="0"/>
         <int name="Legato" value="50"/>
      </item>
   </list>
   <list name="Map" type="list">
{chr(10).join(items)}
   </list>
   <list name="Order" type="int">
{order_xml}
   </list>
   <list name="OutputDevices" type="list">
      <item>
         <string name="DeviceName" value="Default Device"/>
         <string name="PortName" value="Default Port"/>
      </item>
   </list>
   <int name="Flags" value="0"/>
</DrumMap>
"""
    return text.replace("\n", "\r\n")


def build() -> dict:
    names = key_names()
    studio = {k: (k, n) for k, n in names.items()}
    gm = {g: (k, f"{n} -> {names.get(k, k)}") for g, (k, n) in GM.items()}
    return {
        "Studio600.drm": drum_map("Studio600", studio),
        "GM-to-Studio600.drm": drum_map("GM to Studio600", gm),
    }


def main() -> int:
    files = build()
    if "--check" in sys.argv:
        for name, text in files.items():
            p = OUT / name
            if not p.exists() or p.read_bytes() != text.encode("utf-8"):
                print(f"{p} устарел: python tools/gen_drum_map.py")
                return 1
        return 0
    OUT.mkdir(parents=True, exist_ok=True)
    for name, text in files.items():
        (OUT / name).write_bytes(text.encode("utf-8"))
        print("записано", OUT / name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
