"""Общая часть токена лицензии: канонический JSON, подпись Ed25519, обёртка ключей пакетов.

Формат описан в docs/pack-format.md (раздел «Токен лицензии»). Всё, что подписывается и шифруется, должно
побайтно совпадать с C++ (core/src/License.cpp), поэтому правила здесь и там одни и те же.
"""
from __future__ import annotations

import base64
import hashlib
import json
import os
import time
from dataclasses import dataclass

from nacl import bindings
from nacl.signing import SigningKey, VerifyKey

# Метки разделения доменов для хешей и ассоциированных данных AEAD (совпадают с C++)
ID_DOMAIN = b"samplit-machine-id-v1"
KEY_DOMAIN = b"samplit-machine-key-v1"
PACKKEY_AD = b"samplit-packkey-v1:"


def canonical_json(value) -> bytes:
    """Канонический JSON: ключи по возрастанию, без пробелов, UTF-8 без экранирования не-ASCII.

    Числа в payload только целые. Это те же байты, что строит C++ canonicalJson()."""
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def machine_id(fingerprint: str) -> str:
    """Публичный идентификатор машины (попадает в токен): hex BLAKE2b-256 от отпечатка."""
    return hashlib.blake2b(ID_DOMAIN + fingerprint.encode("utf-8"), digest_size=32).hexdigest()


def machine_key(fingerprint: str) -> bytes:
    """Ключ машины для обёртки ключей пакетов. В токен не попадает, знает только сервер и сама машина."""
    return hashlib.blake2b(KEY_DOMAIN + fingerprint.encode("utf-8"), digest_size=32).digest()


def wrap_pack_key(pack_id: str, pack_key: bytes, fingerprint: str, nonce: bytes | None = None) -> str:
    """base64( nonce(24) | шифртекст(32) | тег(16) ), XChaCha20-Poly1305, AD = метка + packId."""
    if len(pack_key) != 32:
        raise ValueError("ключ пакета должен быть 32 байта")
    nonce = nonce or os.urandom(24)
    sealed = bindings.crypto_aead_xchacha20poly1305_ietf_encrypt(
        pack_key, PACKKEY_AD + pack_id.encode("utf-8"), nonce, machine_key(fingerprint))
    return base64.b64encode(nonce + sealed).decode("ascii")


def unwrap_pack_key(pack_id: str, wrapped_b64: str, fingerprint: str) -> bytes:
    raw = base64.b64decode(wrapped_b64)
    nonce, sealed = raw[:24], raw[24:]
    return bindings.crypto_aead_xchacha20poly1305_ietf_decrypt(
        sealed, PACKKEY_AD + pack_id.encode("utf-8"), nonce, machine_key(fingerprint))


@dataclass
class Signer:
    key: SigningKey

    @classmethod
    def from_seed_hex(cls, seed_hex: str) -> "Signer":
        return cls(SigningKey(bytes.fromhex(seed_hex.strip())))

    @property
    def public_key(self) -> bytes:
        return bytes(self.key.verify_key)

    def sign_payload(self, payload: dict) -> dict:
        sig = self.key.sign(canonical_json(payload)).signature
        return {"payload": payload, "sig": base64.b64encode(sig).decode("ascii")}


def verify_token(token: dict, public_key: bytes) -> dict:
    """Возвращает payload, если подпись верна. Иначе бросает ValueError."""
    try:
        payload = token["payload"]
        sig = base64.b64decode(token["sig"])
        VerifyKey(public_key).verify(canonical_json(payload), sig)
    except Exception as exc:  # BadSignatureError, KeyError, ошибки base64
        raise ValueError("подпись токена неверна") from exc
    return payload


def build_payload(license_id: str, user: str, fingerprint: str, packs: dict[str, bytes],
                  not_before: int | None = None, valid_seconds: int | None = 7 * 86400) -> dict:
    """valid_seconds=None: бессрочная лицензия, в токене expires = 0 (плагин не проверяет срок и не продлевает)."""
    now = int(time.time())
    nb = now if not_before is None else not_before
    return {
        "licenseId": license_id,
        "user": user,
        "machine": machine_id(fingerprint),
        "notBefore": nb,
        "expires": 0 if valid_seconds is None else nb + valid_seconds,
        "packs": {pid: wrap_pack_key(pid, key, fingerprint) for pid, key in sorted(packs.items())},
    }
