"""Офлайн-выпуск файла лицензии на компьютер без сервера: на срок или бессрочно.

Берёт ключи пакетов и ключ подписи из каталога секретов сервера лицензий (SAMPLIT_LICENSE_DIR или
~/.samplit-license), запись в базе лицензий не создаёт.

  python tools/make_token.py --fingerprint "win:<guid>:<серийник>" --user "Защита ВКР" --days 120 --out license.token
  python tools/make_token.py --fingerprint "..." --user "Пачевский Д.Е." --perpetual --out license.token

Без --packs в лицензию идут все пакеты, ключи которых есть в каталоге секретов. Код компьютера (отпечаток)
плагин показывает в окне лицензии с кнопкой «Скопировать», на этом компьютере его печатает
`samplit-packer fingerprint`. Готовый файл пользователь открывает кнопкой «Загрузить файл лицензии».
"""
from __future__ import annotations

import argparse
import base64
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent / "license_server"))

import licensecore as lc  # noqa: E402
from store import Config, Store, default_dir  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", type=pathlib.Path, default=default_dir())
    who = ap.add_mutually_exclusive_group(required=True)
    who.add_argument("--fingerprint", help="отпечаток машины (строка целиком)")
    who.add_argument("--any-machine", action="store_true",
                     help="служебный токен для любой машины (временная мера; только с --days)")
    ap.add_argument("--packs", default="", help="packId через запятую; по умолчанию все пакеты из секретов")
    ap.add_argument("--user", default="offline")
    term = ap.add_mutually_exclusive_group(required=True)
    term.add_argument("--days", type=int, help="срок действия в днях")
    term.add_argument("--perpetual", action="store_true", help="бессрочная лицензия (expires = 0)")
    ap.add_argument("--id", default="L-OFFLINE", help="идентификатор лицензии в токене")
    ap.add_argument("--out", type=pathlib.Path, required=True)
    args = ap.parse_args()

    if args.days is not None and (args.days < 1 or args.days > 3650):
        print("--days: от 1 до 3650", file=sys.stderr)
        return 1

    if args.any_machine and args.perpetual:
        print("--any-machine: бессрочный токен не допускается, укажите --days", file=sys.stderr)
        return 1

    cfg = Config(args.dir)
    store = Store(cfg)
    try:
        ids = [p.strip() for p in args.packs.split(",") if p.strip()] or store.pack_ids()
        packs = {pid: store.get_pack_key(pid) for pid in ids}
    except KeyError as exc:
        print("ошибка:", exc, file=sys.stderr)
        return 1

    if not packs:
        print("ошибка: в каталоге секретов нет ключей пакетов", file=sys.stderr)
        return 1

    valid = None if args.perpetual else args.days * 86400
    payload = lc.build_payload(args.id, args.user, ("*" if args.any_machine else args.fingerprint.strip()), packs, valid_seconds=valid)
    token = cfg.signer.sign_payload(payload)
    args.out.write_text(json.dumps(token, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    key = "SPL1-" + base64.b64encode(json.dumps(token, ensure_ascii=False, separators=(",", ":")).encode("utf-8")).decode("ascii")
    args.out.with_suffix(".key.txt").write_text(key + "\n", encoding="ascii")
    term_text = "бессрочно" if args.perpetual else f"на {args.days} дн."
    print(f"лицензия записана: {args.out} ({term_text}, пакеты: {', '.join(sorted(packs))})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
