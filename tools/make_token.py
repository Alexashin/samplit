"""Оффлайн-выпуск токена на длинный срок (например, для защиты диплома, когда интернета нет).

Берёт ключи пакетов и ключ подписи из каталога секретов сервера лицензий (SAMPLIT_LICENSE_DIR или
~/.samplit-license), запись в базе лицензий не создаёт.

  python tools/make_token.py --fingerprint "win:<guid>:<серийник>" --packs studio600-kick,studio600-cymbals \
         --user "Защита ВКР" --days 120 --out token.json

Отпечаток машины плагин показывает в окне лицензии; для проверки на этом компьютере его печатает
тест `SamplitTests "[license]"` или функция machineFingerprint().
"""
from __future__ import annotations

import argparse
import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent / "license_server"))

import licensecore as lc  # noqa: E402
from store import Config, Store, default_dir  # noqa: E402


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", type=pathlib.Path, default=default_dir())
    ap.add_argument("--fingerprint", required=True, help="отпечаток машины (строка целиком)")
    ap.add_argument("--packs", required=True, help="packId через запятую")
    ap.add_argument("--user", default="offline")
    ap.add_argument("--days", type=int, required=True, help="срок действия токена в днях")
    ap.add_argument("--id", default="L-OFFLINE", help="идентификатор лицензии в токене")
    ap.add_argument("--out", type=pathlib.Path, required=True)
    args = ap.parse_args()

    if args.days < 1 or args.days > 3650:
        print("--days: от 1 до 3650", file=sys.stderr)
        return 1

    cfg = Config(args.dir)
    store = Store(cfg)
    try:
        packs = {pid: store.get_pack_key(pid) for pid in (p.strip() for p in args.packs.split(",")) if pid}
    except KeyError as exc:
        print("ошибка:", exc, file=sys.stderr)
        return 1

    payload = lc.build_payload(args.id, args.user, args.fingerprint, packs, valid_seconds=args.days * 86400)
    token = cfg.signer.sign_payload(payload)
    args.out.write_text(json.dumps(token, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("токен записан:", args.out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
