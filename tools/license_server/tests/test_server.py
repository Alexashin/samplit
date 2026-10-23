import json
import time
from unittest import mock

import pytest

import admin
import licensecore as lc
from conftest import FINGERPRINT, PACK_KEYS
from store import Config, parse_key_bytes


def activate(client, code, fp=FINGERPRINT):
    return client.post("/activate", json={"code": code, "fingerprint": fp})


def payload_of(response, config):
    token = response.json()["token"]
    return token, lc.verify_token(token, config.signer.public_key)


# ---------------------------------------------------------------- licensecore

def test_canonical_json_matches_cpp_contract():
    value = {"é": [True, None, 3_000_000_000], "b": {"z": 1, "a": False}, "a": "\"q\"\\\n\t\x01 я"}
    assert lc.canonical_json(value) == (
        b'{"a":"\\"q\\"\\\\\\n\\t\\u0001 \xd1\x8f","b":{"a":false,"z":1},"\xc3\xa9":[true,null,3000000000]}')


def test_wrap_unwrap_roundtrip_and_binding():
    key = bytes(range(32))
    wrapped = lc.wrap_pack_key("kick", key, FINGERPRINT)
    assert lc.unwrap_pack_key("kick", wrapped, FINGERPRINT) == key
    with pytest.raises(Exception):
        lc.unwrap_pack_key("kick", wrapped, "win:other:1")
    with pytest.raises(Exception):
        lc.unwrap_pack_key("cymbals", wrapped, FINGERPRINT)


def test_machine_id_is_not_the_wrapping_key():
    wrapped = lc.wrap_pack_key("kick", bytes(32), FINGERPRINT)
    with pytest.raises(Exception):
        lc.unwrap_pack_key("kick", wrapped, lc.machine_id(FINGERPRINT))


# ---------------------------------------------------------------- activate

def test_activate_issues_a_verifiable_token(client, config, license_code):
    now = int(time.time())
    r = activate(client, license_code)
    assert r.status_code == 200
    _, payload = payload_of(r, config)
    assert payload["machine"] == lc.machine_id(FINGERPRINT)
    assert payload["expires"] - payload["notBefore"] == 7 * 86400
    assert abs(payload["notBefore"] - now) < 5
    for pid, key in PACK_KEYS.items():
        assert lc.unwrap_pack_key(pid, payload["packs"][pid], FINGERPRINT) == key


def test_token_lifetime_follows_the_license(client, config, store):
    code = store.create_license("Месяц", ["kick"], 30)[1]
    _, payload = payload_of(activate(client, code), config)
    assert payload["expires"] - payload["notBefore"] == 30 * 86400
    assert list(payload["packs"]) == ["kick"]


def test_activate_same_machine_again_is_fine_other_machine_is_not(client, license_code):
    assert activate(client, license_code).status_code == 200
    assert activate(client, license_code).status_code == 200
    r = activate(client, license_code, "win:another:2")
    assert r.status_code == 409
    assert "error" in r.json()


def test_activate_errors(client, license_code):
    assert activate(client, "0000-0000-0000-0000").status_code == 404
    assert client.post("/activate", json={"code": license_code}).status_code == 400
    assert client.post("/activate", json={"code": license_code, "fingerprint": "  "}).status_code == 400
    assert client.post("/activate", content=b"not json").status_code == 400


def test_code_is_case_and_space_insensitive(client, license_code):
    assert activate(client, " " + license_code.lower() + " ").status_code == 200


def test_activation_attempts_are_rate_limited(client):
    codes = [activate(client, f"BAD{i}").status_code for i in range(7)]
    assert codes[:5] == [404] * 5
    assert 429 in codes[5:]


# ---------------------------------------------------------------- renew

def test_renew_returns_fresh_token(client, config, license_code):
    first, _ = payload_of(activate(client, license_code), config)
    later = int(time.time()) + 3 * 86400
    with mock.patch("time.time", return_value=later):
        r = client.post("/renew", json={"token": first})
    assert r.status_code == 200
    _, payload = payload_of(r, config)
    assert payload["notBefore"] == later
    assert payload["expires"] == later + 7 * 86400


def test_renew_works_for_an_expired_token(client, config, license_code):
    first, _ = payload_of(activate(client, license_code), config)
    much_later = int(time.time()) + 90 * 86400
    with mock.patch("time.time", return_value=much_later):
        assert client.post("/renew", json={"token": first}).status_code == 200


def test_renew_rejects_tampered_and_foreign_tokens(client, config, license_code):
    token, _ = payload_of(activate(client, license_code), config)

    tampered = json.loads(json.dumps(token))
    tampered["payload"]["expires"] += 10 * 86400
    assert client.post("/renew", json={"token": tampered}).status_code == 401

    foreign = lc.Signer.from_seed_hex("11" * 32).sign_payload(token["payload"])
    assert client.post("/renew", json={"token": foreign}).status_code == 401
    assert client.post("/renew", json={"token": {"payload": {}}}).status_code == 401
    assert client.post("/renew", json={}).status_code == 400


def test_renew_after_machine_reset_requires_the_same_machine(client, config, store, license_code):
    token, payload = payload_of(activate(client, license_code), config)
    store.reset_machine(payload["licenseId"])
    assert client.post("/renew", json={"token": token}).status_code == 409  # лицензия снова ни к кому не привязана
    assert activate(client, license_code, "win:new-pc:3").status_code == 200
    assert client.post("/renew", json={"token": token}).status_code == 409  # старый токен от прежней машины


# ---------------------------------------------------------------- revoke

def test_revoke_requires_admin_token_and_blocks_renew(client, config, license_code):
    token, payload = payload_of(activate(client, license_code), config)
    body = {"licenseId": payload["licenseId"]}
    admin_header = {"X-Admin-Token": config.admin_token}

    assert client.post("/revoke", json=body).status_code == 401
    assert client.post("/revoke", json=body, headers={"X-Admin-Token": "wrong"}).status_code == 401
    assert client.post("/revoke", json={"licenseId": "L-NONE"}, headers=admin_header).status_code == 404

    assert client.post("/revoke", json=body, headers=admin_header).status_code == 200
    assert client.post("/renew", json={"token": token}).status_code == 403
    assert activate(client, license_code).status_code == 403

    client.post("/revoke", json={**body, "revoked": False}, headers=admin_header)
    assert client.post("/renew", json={"token": token}).status_code == 200


# ---------------------------------------------------------------- хранилище и админ-утилита

def test_pack_keys_are_encrypted_at_rest(store, config):
    store.db.commit()
    raw = config.db_path.read_bytes()
    for key in PACK_KEYS.values():
        assert key not in raw
        assert key.hex().encode() not in raw
    assert store.get_pack_key("kick") == PACK_KEYS["kick"]


def test_create_license_validates_input(store):
    with pytest.raises(KeyError):
        store.create_license("x", ["no-such-pack"], 7)
    with pytest.raises(ValueError):
        store.create_license("x", ["kick"], 14)


def test_parse_key_bytes_accepts_hex_and_raw():
    key = bytes(range(32))
    assert parse_key_bytes(key.hex().encode() + b"\n") == key
    assert parse_key_bytes(key) == key
    with pytest.raises(ValueError):
        parse_key_bytes(b"short")


def test_init_does_not_overwrite_secrets(tmp_path):
    Config.init(tmp_path / "s")
    with pytest.raises(FileExistsError):
        Config.init(tmp_path / "s")


def test_admin_cli_flow(tmp_path, capsys):
    d = str(tmp_path / "cli")
    keyfile = tmp_path / "kick.key"
    keyfile.write_text(bytes(range(7, 39)).hex(), encoding="ascii")

    assert admin.main(["--dir", d, "init"]) == 0
    assert "Публичный ключ" in capsys.readouterr().out
    assert admin.main(["--dir", d, "add-pack-key", "--pack-id", "kick", "--key-file", str(keyfile)]) == 0
    assert admin.main(["--dir", d, "create", "--user", "Иван", "--packs", "kick", "--renew-days", "30"]) == 0
    out = capsys.readouterr().out
    lic_id = [line for line in out.splitlines() if line.startswith("лицензия:")][0].split()[-1]
    assert admin.main(["--dir", d, "create", "--user", "Иван", "--packs", "unknown"]) == 1
    assert admin.main(["--dir", d, "list"]) == 0
    assert "не активирована" in capsys.readouterr().out
    assert admin.main(["--dir", d, "revoke", "--id", lic_id]) == 0
    assert admin.main(["--dir", d, "list"]) == 0
    assert "ОТОЗВАНА" in capsys.readouterr().out
    assert admin.main(["--dir", d, "show", "--id", "L-NONE"]) == 1


def test_make_token_offline(tmp_path, store, config, monkeypatch):
    import importlib.util
    import pathlib
    import sys

    spec = importlib.util.spec_from_file_location(
        "make_token", pathlib.Path(__file__).resolve().parents[2] / "make_token.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)

    out = tmp_path / "token.json"
    monkeypatch.setattr(sys, "argv", ["make_token.py", "--dir", str(config.directory), "--fingerprint", FINGERPRINT,
                                      "--packs", "kick,cymbals", "--days", "120", "--out", str(out)])
    assert mod.main() == 0
    payload = lc.verify_token(json.loads(out.read_text(encoding="utf-8")), config.signer.public_key)
    assert payload["expires"] - payload["notBefore"] == 120 * 86400
    assert payload["machine"] == lc.machine_id(FINGERPRINT)
    assert lc.unwrap_pack_key("kick", payload["packs"]["kick"], FINGERPRINT) == PACK_KEYS["kick"]
