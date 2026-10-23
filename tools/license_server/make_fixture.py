"""Фикстура для C++ тестов: токен, подписанный Python, и всё, что нужно для его проверки.

Использует ТЕСТОВЫЙ сид (000102...1f): боевых секретов здесь нет. Результат tests/fixtures/license_fixture.json
лежит в git, скрипт запускают заново при смене формата.
"""
from __future__ import annotations

import json
import pathlib

import licensecore as lc

TEST_SEED_HEX = "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f"  # ТОЛЬКО ДЛЯ ТЕСТОВ
FINGERPRINT = "win:test-machine-guid:1a2b3c4d"
NOT_BEFORE = 1_700_000_000
EXPIRES = 4_100_000_000  # 2099 год: тест не протухнет
PACKS = {
    "studio600-kick": bytes(range(100, 132)),
    "studio600-тарелки": bytes(range(1, 33)),  # не-ASCII в идентификаторе проверяет канонический JSON
}


def main() -> None:
    signer = lc.Signer.from_seed_hex(TEST_SEED_HEX)
    payload = {
        "licenseId": "L-FIXTURE-1",
        "user": "Тестовый пользователь \"Q\"",
        "machine": lc.machine_id(FINGERPRINT),
        "notBefore": NOT_BEFORE,
        "expires": EXPIRES,
        "packs": {pid: lc.wrap_pack_key(pid, key, FINGERPRINT, nonce=bytes([i]) * 24)
                  for i, (pid, key) in enumerate(sorted(PACKS.items()), start=1)},
    }
    token = signer.sign_payload(payload)
    out = {
        "publicKeyHex": signer.public_key.hex(),
        "fingerprint": FINGERPRINT,
        "machineId": payload["machine"],
        "now": 1_800_000_000,
        "canonicalPayload": lc.canonical_json(payload).decode("utf-8"),
        "packKeysHex": {pid: key.hex() for pid, key in PACKS.items()},
        "token": token,
    }
    fixtures = pathlib.Path(__file__).resolve().parents[2] / "tests" / "fixtures"
    fixtures.mkdir(parents=True, exist_ok=True)
    target = fixtures / "license_fixture.json"
    target.write_text(json.dumps(out, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("записано", target)

    server_target = fixtures / "license_server_fixture.json"
    server_target.write_text(json.dumps(issue_with_server(), ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print("записано", server_target)


def issue_with_server() -> dict:
    """Токен выпускается настоящим кодом сервера (/activate), а не вручную: C++ обязан его принять."""
    import tempfile
    from unittest import mock

    from fastapi.testclient import TestClient

    from app import create_app
    from store import Config, Store

    issued_at = 1_800_000_000
    with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as tmp:
        directory = pathlib.Path(tmp)
        (directory / "signing.seed").write_text(TEST_SEED_HEX + "\n", encoding="ascii")
        (directory / "master.key").write_text("ab" * 32 + "\n", encoding="ascii")  # ТОЛЬКО ДЛЯ ТЕСТОВ
        (directory / "admin.token").write_text("test-admin-token\n", encoding="ascii")
        config = Config(directory)
        store = Store(config)
        for pid, key in PACKS.items():
            store.put_pack_key(pid, key)
        _, code = store.create_license("Серверный тест", sorted(PACKS), 7)
        app = create_app(config)
        with mock.patch("time.time", return_value=issued_at):
            response = TestClient(app).post("/activate", json={"code": code, "fingerprint": FINGERPRINT})
        app.state.store.close()
        store.close()
    response.raise_for_status()
    return {
        "publicKeyHex": lc.Signer.from_seed_hex(TEST_SEED_HEX).public_key.hex(),
        "fingerprint": FINGERPRINT,
        "issuedAt": issued_at,
        "validSeconds": 7 * 86400,
        "packKeysHex": {pid: key.hex() for pid, key in PACKS.items()},
        "token": response.json()["token"],
    }


if __name__ == "__main__":
    main()
