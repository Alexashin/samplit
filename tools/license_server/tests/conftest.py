import pathlib
import sys

import pytest
from fastapi.testclient import TestClient

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))

import licensecore as lc  # noqa: E402
from app import create_app  # noqa: E402
from store import Config, Store  # noqa: E402

FINGERPRINT = "win:pytest-guid:0badf00d"
PACK_KEYS = {"kick": bytes(range(1, 33)), "cymbals": bytes(range(101, 133))}


@pytest.fixture()
def config(tmp_path):
    return Config.init(tmp_path / "secrets")


@pytest.fixture()
def store(config):
    s = Store(config)
    for pid, key in PACK_KEYS.items():
        s.put_pack_key(pid, key)
    yield s
    s.close()


@pytest.fixture()
def client(config, store):
    app = create_app(config, activate_limit=5)
    return TestClient(app)


@pytest.fixture()
def license_code(store):
    return store.create_license("Тест", ["kick", "cymbals"], 7)[1]
