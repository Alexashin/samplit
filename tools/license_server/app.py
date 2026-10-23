"""Сервер лицензий Samplit: выдача и продление токена. Только JSON.

Запуск:  SAMPLIT_LICENSE_DIR=<каталог с секретами> uvicorn app:app --host 127.0.0.1 --port 8443
Наружу выставлять только за обратным прокси с HTTPS: плагин принимает лишь https://.
"""
from __future__ import annotations

import hmac
import time
from collections import defaultdict, deque

from fastapi import FastAPI, Request
from fastapi.exceptions import RequestValidationError
from fastapi.responses import JSONResponse
from pydantic import BaseModel

import licensecore as lc
from store import Config, Store, default_dir


class ActivateBody(BaseModel):
    code: str
    fingerprint: str


class RenewBody(BaseModel):
    token: dict


class RevokeBody(BaseModel):
    licenseId: str
    revoked: bool = True


def error(status: int, message: str) -> JSONResponse:
    return JSONResponse(status_code=status, content={"error": message})


class RateLimiter:
    """Простое ограничение числа запросов с одного адреса за окно (защита от перебора кодов)."""

    def __init__(self, limit: int, window: float):
        self.limit, self.window = limit, window
        self.hits: dict[str, deque] = defaultdict(deque)

    def allow(self, key: str, now: float | None = None) -> bool:
        now = time.monotonic() if now is None else now
        q = self.hits[key]
        while q and now - q[0] > self.window:
            q.popleft()
        if len(q) >= self.limit:
            return False
        q.append(now)
        return True


def create_app(config: Config | None = None, activate_limit: int = 10) -> FastAPI:
    config = config or Config(default_dir())
    store = Store(config)
    signer = config.signer
    admin_token = config.admin_token
    limiter = RateLimiter(activate_limit, 60.0)

    app = FastAPI(title="Samplit license server", docs_url=None, redoc_url=None, openapi_url=None)
    app.state.store = store

    @app.exception_handler(RequestValidationError)
    async def _bad_request(_: Request, __: RequestValidationError):
        return error(400, "Некорректный запрос")

    @app.get("/health")
    def health():
        return {"ok": True}

    @app.post("/activate")
    def activate(body: ActivateBody, request: Request):
        client = request.client.host if request.client else "?"
        if not limiter.allow(client):
            return error(429, "Слишком много попыток, подождите минуту")
        if not body.fingerprint.strip():
            return error(400, "Не передан отпечаток машины")

        row = store.by_code(body.code)
        if row is None:
            return error(404, "Неверный код лицензии")
        if row["revoked"]:
            return error(403, "Лицензия отозвана")
        if row["fingerprint"] is not None and not hmac.compare_digest(row["fingerprint"], body.fingerprint):
            return error(409, "Лицензия уже привязана к другому компьютеру")

        if row["fingerprint"] is None:
            store.bind(row["id"], body.fingerprint)
            row = store.by_id(row["id"])
        return {"token": store.issue_token(row)}

    @app.post("/renew")
    def renew(body: RenewBody):
        try:
            payload = lc.verify_token(body.token, signer.public_key)
        except ValueError:
            return error(401, "Токен не распознан")

        row = store.by_id(str(payload.get("licenseId", "")))
        if row is None:
            return error(404, "Лицензия не найдена")
        if row["revoked"]:
            return error(403, "Лицензия отозвана")
        if row["fingerprint"] is None or not hmac.compare_digest(lc.machine_id(row["fingerprint"]), str(payload.get("machine", ""))):
            return error(409, "Токен выпущен для другого компьютера")
        # Просроченный токен продлеваем: пользователь мог не запускать плагин дольше срока
        return {"token": store.issue_token(row)}

    @app.post("/revoke")
    def revoke(body: RevokeBody, request: Request):
        given = request.headers.get("x-admin-token", "")
        if not hmac.compare_digest(given.encode("utf-8"), admin_token.encode("utf-8")):
            return error(401, "Нет доступа")
        if not store.set_revoked(body.licenseId, body.revoked):
            return error(404, "Лицензия не найдена")
        return {"ok": True}

    return app


# uvicorn app:app (каталог секретов должен быть создан командой admin.py init)
app = None
try:
    if (default_dir() / "signing.seed").exists():
        app = create_app()
except Exception:  # при импорте из тестов секретов может не быть
    app = None
