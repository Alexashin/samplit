"""Хранилище сервера лицензий: SQLite, ключи пакетов зашифрованы мастер-ключом.

Секреты (сид подписи, мастер-ключ, токен админа, база) лежат в каталоге ВНЕ репозитория:
переменная SAMPLIT_LICENSE_DIR или ~/.samplit-license.
"""
from __future__ import annotations

import hashlib
import json
import os
import pathlib
import secrets
import sqlite3
import time
from dataclasses import dataclass

from nacl import secret, utils

import licensecore as lc

SCHEMA = """
CREATE TABLE IF NOT EXISTS licenses (
    id          TEXT PRIMARY KEY,
    code_hash   TEXT NOT NULL UNIQUE,
    user        TEXT NOT NULL,
    packs       TEXT NOT NULL,          -- JSON-список packId
    renew_days  INTEGER NOT NULL,       -- срок одного токена: 7 или 30
    fingerprint TEXT,                   -- NULL, пока лицензия не активирована
    revoked     INTEGER NOT NULL DEFAULT 0,
    created     INTEGER NOT NULL,
    last_issue  INTEGER
);
CREATE TABLE IF NOT EXISTS pack_keys (
    pack_id TEXT PRIMARY KEY,
    sealed  BLOB NOT NULL               -- SecretBox(мастер-ключ): nonce | шифртекст | тег
);
"""


def default_dir() -> pathlib.Path:
    return pathlib.Path(os.environ.get("SAMPLIT_LICENSE_DIR", pathlib.Path.home() / ".samplit-license"))


def code_hash(code: str) -> str:
    normalized = code.strip().upper().replace(" ", "")
    return hashlib.sha256(normalized.encode("utf-8")).hexdigest()


def parse_key_bytes(raw: bytes) -> bytes:
    """Ключ пакета: файл .key из samplit-packer (64 hex-символа) или 32 сырых байта."""
    text = raw.strip()
    if len(text) == 64:
        try:
            return bytes.fromhex(text.decode("ascii"))
        except (ValueError, UnicodeDecodeError):
            pass
    if len(raw) == 32:
        return raw
    raise ValueError("ключ пакета: ожидалось 64 hex-символа или 32 байта")


@dataclass
class Config:
    directory: pathlib.Path

    @property
    def db_path(self) -> pathlib.Path:
        return self.directory / "licenses.db"

    def _read_hex(self, name: str) -> bytes:
        return bytes.fromhex((self.directory / name).read_text(encoding="ascii").strip())

    @property
    def signer(self) -> lc.Signer:
        return lc.Signer.from_seed_hex((self.directory / "signing.seed").read_text(encoding="ascii"))

    @property
    def master_key(self) -> bytes:
        return self._read_hex("master.key")

    @property
    def admin_token(self) -> str:
        return (self.directory / "admin.token").read_text(encoding="ascii").strip()

    @classmethod
    def init(cls, directory: pathlib.Path) -> "Config":
        """Создаёт каталог с секретами. Существующие файлы не перезаписывает."""
        directory.mkdir(parents=True, exist_ok=True)
        files = {
            "signing.seed": os.urandom(32).hex(),
            "master.key": os.urandom(32).hex(),
            "admin.token": secrets.token_urlsafe(32),
        }
        for name, value in files.items():
            path = directory / name
            if path.exists():
                raise FileExistsError(f"{path} уже существует, init ничего не перезаписывает")
        for name, value in files.items():
            path = directory / name
            path.write_text(value + "\n", encoding="ascii")
            try:
                path.chmod(0o600)
            except OSError:
                pass
        return cls(directory)


class Store:
    def __init__(self, config: Config):
        self.config = config
        self.db = sqlite3.connect(config.db_path, check_same_thread=False)
        self.db.row_factory = sqlite3.Row
        self.db.executescript(SCHEMA)
        self.box = secret.SecretBox(config.master_key)

    def close(self) -> None:
        self.db.close()

    # ---- ключи пакетов
    def put_pack_key(self, pack_id: str, key: bytes) -> None:
        if len(key) != 32:
            raise ValueError("ключ пакета должен быть 32 байта")
        sealed = bytes(self.box.encrypt(key, utils.random(secret.SecretBox.NONCE_SIZE)))
        with self.db:
            self.db.execute("INSERT OR REPLACE INTO pack_keys(pack_id, sealed) VALUES (?, ?)", (pack_id, sealed))

    def get_pack_key(self, pack_id: str) -> bytes:
        row = self.db.execute("SELECT sealed FROM pack_keys WHERE pack_id = ?", (pack_id,)).fetchone()
        if row is None:
            raise KeyError(f"нет ключа пакета {pack_id}")
        return self.box.decrypt(bytes(row["sealed"]))

    def pack_ids(self) -> list[str]:
        return [r["pack_id"] for r in self.db.execute("SELECT pack_id FROM pack_keys ORDER BY pack_id")]

    # ---- лицензии
    def create_license(self, user: str, packs: list[str], renew_days: int) -> tuple[str, str]:
        """Возвращает (id лицензии, код активации). Код хранится только хешем, показывается один раз."""
        if renew_days not in (7, 30):
            raise ValueError("срок токена: 7 или 30 дней")
        missing = [p for p in packs if p not in self.pack_ids()]
        if missing:
            raise KeyError("нет ключей пакетов: " + ", ".join(missing))
        lic_id = "L-" + secrets.token_hex(4).upper()
        raw = secrets.token_hex(8).upper()
        code = "-".join(raw[i:i + 4] for i in range(0, 16, 4))
        with self.db:
            self.db.execute(
                "INSERT INTO licenses(id, code_hash, user, packs, renew_days, created) VALUES (?,?,?,?,?,?)",
                (lic_id, code_hash(code), user, json.dumps(sorted(packs)), renew_days, int(time.time())))
        return lic_id, code

    def by_id(self, lic_id: str):
        return self.db.execute("SELECT * FROM licenses WHERE id = ?", (lic_id,)).fetchone()

    def by_code(self, code: str):
        return self.db.execute("SELECT * FROM licenses WHERE code_hash = ?", (code_hash(code),)).fetchone()

    def list_licenses(self):
        return self.db.execute("SELECT * FROM licenses ORDER BY created").fetchall()

    def bind(self, lic_id: str, fingerprint: str) -> None:
        with self.db:
            self.db.execute("UPDATE licenses SET fingerprint = ? WHERE id = ?", (fingerprint, lic_id))

    def set_revoked(self, lic_id: str, revoked: bool) -> bool:
        with self.db:
            cur = self.db.execute("UPDATE licenses SET revoked = ? WHERE id = ?", (1 if revoked else 0, lic_id))
        return cur.rowcount > 0

    def reset_machine(self, lic_id: str) -> bool:
        with self.db:
            cur = self.db.execute("UPDATE licenses SET fingerprint = NULL WHERE id = ?", (lic_id,))
        return cur.rowcount > 0

    # ---- выпуск токена
    def issue_token(self, row, valid_seconds: int | None = None, now: int | None = None) -> dict:
        now = int(time.time()) if now is None else now
        packs = {pid: self.get_pack_key(pid) for pid in json.loads(row["packs"])}
        payload = lc.build_payload(
            row["id"], row["user"], row["fingerprint"], packs, not_before=now,
            valid_seconds=valid_seconds if valid_seconds is not None else row["renew_days"] * 86400)
        with self.db:
            self.db.execute("UPDATE licenses SET last_issue = ? WHERE id = ?", (now, row["id"]))
        return self.config.signer.sign_payload(payload)
