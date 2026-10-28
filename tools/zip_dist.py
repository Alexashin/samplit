#!/usr/bin/env python3
"""Упаковывает папку дистрибутива в zip без сжатия, с именами файлов в UTF-8.

bsdtar из Windows пишет русские имена без флага UTF-8, и 7-Zip / WinRAR показывают их кракозябрами.
Python ставит флаг сам, когда в имени есть не-ASCII. Пакеты уже сжаты (FLAC), поэтому ZIP_STORED.

    python tools/zip_dist.py dist/Samplit-0.2.0 dist/Samplit-0.2.0.zip
"""
import sys
import zipfile
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    src = Path(sys.argv[1]).resolve()
    dst = Path(sys.argv[2]).resolve()
    if not src.is_dir():
        print(f"нет папки: {src}")
        return 1
    if dst.exists():
        dst.unlink()

    with zipfile.ZipFile(dst, "w", compression=zipfile.ZIP_STORED, allowZip64=True) as z:
        for path in sorted(src.rglob("*")):
            arc = f"{src.name}/{path.relative_to(src).as_posix()}"
            if path.is_dir():
                z.writestr(arc + "/", b"")
            else:
                z.write(path, arc)
    print(f"Архив: {dst} ({dst.stat().st_size / 1048576:,.0f} МБ)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
