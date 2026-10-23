"""Администрирование лицензий. Секреты хранятся вне репозитория (SAMPLIT_LICENSE_DIR или ~/.samplit-license).

  python admin.py init                              создать ключ подписи, мастер-ключ, токен админа
  python admin.py add-pack-key --pack-id P --key-file P.key
  python admin.py create --user "Имя" --packs a,b --renew-days 7|30
  python admin.py list | show --id L-XXXX
  python admin.py revoke --id L-XXXX [--undo]
  python admin.py reset-machine --id L-XXXX         отвязать лицензию от компьютера
"""
from __future__ import annotations

import argparse
import json
import pathlib
import sys
import time

from store import Config, Store, default_dir, parse_key_bytes


def fmt_time(ts) -> str:
    return time.strftime("%Y-%m-%d %H:%M", time.localtime(ts)) if ts else "-"


def cpp_array(data: bytes) -> str:
    rows = []
    for i in range(0, len(data), 16):
        rows.append("            " + ", ".join(f"0x{b:02x}" for b in data[i:i + 16]))
    return "static const Key32 key = {\n" + ",\n".join(rows) + "\n        };"


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", type=pathlib.Path, default=default_dir(), help="каталог с секретами")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("init")
    p = sub.add_parser("add-pack-key")
    p.add_argument("--pack-id", required=True)
    p.add_argument("--key-file", required=True, type=pathlib.Path)
    p = sub.add_parser("create")
    p.add_argument("--user", required=True)
    p.add_argument("--packs", required=True, help="packId через запятую")
    p.add_argument("--renew-days", type=int, choices=(7, 30), default=7)
    sub.add_parser("list")
    p = sub.add_parser("show")
    p.add_argument("--id", required=True)
    p = sub.add_parser("revoke")
    p.add_argument("--id", required=True)
    p.add_argument("--undo", action="store_true")
    p = sub.add_parser("reset-machine")
    p.add_argument("--id", required=True)
    args = ap.parse_args(argv)

    if args.cmd == "init":
        cfg = Config.init(args.dir)
        pub = cfg.signer.public_key
        print(f"Каталог секретов: {cfg.directory} (хранить вне репозитория, делать резервную копию)")
        print("Публичный ключ (hex):", pub.hex())
        print("Вставить в core/src/LicenseKey.cpp:\n        " + cpp_array(pub))
        print("Токен админа для /revoke лежит в admin.token")
        return 0

    store = Store(Config(args.dir))
    try:
        if args.cmd == "add-pack-key":
            store.put_pack_key(args.pack_id, parse_key_bytes(args.key_file.read_bytes()))
            print("ключ пакета сохранён:", args.pack_id)
        elif args.cmd == "create":
            packs = [x.strip() for x in args.packs.split(",") if x.strip()]
            lic_id, code = store.create_license(args.user, packs, args.renew_days)
            print("лицензия:", lic_id)
            print("код активации (показывается один раз):", code)
        elif args.cmd == "list":
            for r in store.list_licenses():
                state = "ОТОЗВАНА" if r["revoked"] else ("активна" if r["fingerprint"] else "не активирована")
                print(f"{r['id']}  {r['user']}  {state}  пакеты={','.join(json.loads(r['packs']))}  токен={r['renew_days']} дн.")
        elif args.cmd == "show":
            r = store.by_id(args.id)
            if r is None:
                print("не найдена", file=sys.stderr)
                return 1
            print(json.dumps({
                "id": r["id"], "user": r["user"], "packs": json.loads(r["packs"]), "renewDays": r["renew_days"],
                "bound": r["fingerprint"] is not None, "revoked": bool(r["revoked"]),
                "created": fmt_time(r["created"]), "lastIssue": fmt_time(r["last_issue"])}, ensure_ascii=False, indent=2))
        elif args.cmd == "revoke":
            if not store.set_revoked(args.id, not args.undo):
                print("не найдена", file=sys.stderr)
                return 1
            print("снята отметка отзыва" if args.undo else "лицензия отозвана", args.id)
        elif args.cmd == "reset-machine":
            if not store.reset_machine(args.id):
                print("не найдена", file=sys.stderr)
                return 1
            print("привязка к компьютеру сброшена", args.id)
    except (KeyError, ValueError, FileNotFoundError) as exc:
        print("ошибка:", exc, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
