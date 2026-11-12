"""The HTTP API of the UltraAI model server.

What every UltraAI client reads (no authentication; LocalRuntime's gallery
URL points at the first, the other two are read from beside it):

  GET  /gallery/index.json        the engines and their bundles (gallery index, formatVersion 1)
  GET  /gallery/catalogue.json    the published models (model catalogue, formatVersion 1)
  GET  /gallery/topics.json       the topics and the published models offered for each, the default first
  GET  /gallery/<document>.sig    the Ed25519 signature of one of the three (128 hex characters)
  GET  /gallery/keys.json         the public keys a client pins to check those signatures
  GET  /files/{sha256}/{name}     an uploaded model file (Range requests are answered)
  GET  /healthz

What UltraAI ServerAdmin uses (Authorization: Bearer <token>; "super" is
the token from the environment or an account with the super role,
"editor" an account that manages models and topics):

  GET    /v1/admin/whoami
  GET    /v1/admin/models                          every model, any status, with its files and topics
  GET    /v1/admin/models/{id}
  PUT    /v1/admin/models/{id}                     create (a draft) or change the description
  DELETE /v1/admin/models/{id}                     super
  GET    /v1/admin/models/{id}/check               why it cannot be published yet
  POST   /v1/admin/models/{id}/publish | withdraw | draft
  PUT    /v1/admin/models/{id}/files/{name}        {url, sha256, size}: a file hosted elsewhere
  PUT    /v1/admin/models/{id}/files/{name}/upload the file's bytes (X-Content-SHA256 checked when sent)
  DELETE /v1/admin/models/{id}/files/{name}
  GET    /v1/admin/topics
  PUT    /v1/admin/topics/{id}                     {name, description, capability, position?, models: [{id, default?}]}
  DELETE /v1/admin/topics/{id}                     super
  GET    /v1/admin/engines
  PUT    /v1/admin/engines/{id}                    super: the engine as the gallery index has it
  DELETE /v1/admin/engines/{id}                    super
  POST   /v1/admin/import                          super: {index?, catalogue?, topics?, publish?}
  GET    /v1/admin/blobs                           the uploads and how often each is used
  POST   /v1/admin/blobs/prune                     super: delete the uploads no model uses
  GET    /v1/admin/admins                          super
  PUT    /v1/admin/admins/{name}                   super: {role} -> {token} (shown once)
  DELETE /v1/admin/admins/{name}                   super
  GET    /v1/admin/audit?since=N

Errors are {"error", "error_description"} with a 4xx or 503 status."""

from __future__ import annotations

import hashlib
import hmac
import json
import os
import secrets
from pathlib import Path

from fastapi import Depends, FastAPI, Header, HTTPException, Request
from fastapi.responses import FileResponse, JSONResponse, PlainTextResponse, Response

from . import __version__
from .config import Settings
from .signing import SIGNATURE_HEADER, SigningKey, canonical_bytes
from .store import SHA_RE, Store, StoreError, check_file_name

SEED_DIR = Path(__file__).parent / "seed"


def _error(status: int, code: str, description: str) -> HTTPException:
    return HTTPException(status_code=status, detail={"error": code, "error_description": description})


def _seed(store: Store) -> None:
    docs = {name: json.loads((SEED_DIR / f"{name}.json").read_text(encoding="utf-8"))
            for name in ("index", "catalogue", "topics")}
    store.import_documents("seed", index=docs["index"], catalogue=docs["catalogue"], topics=docs["topics"], publish=True)


def create_app(settings: Settings | None = None) -> FastAPI:
    settings = settings or Settings.from_environment()
    store = Store(settings.database)
    data_dir = Path(settings.data_dir)
    (data_dir / "blobs").mkdir(parents=True, exist_ok=True)
    (data_dir / "tmp").mkdir(parents=True, exist_ok=True)
    if settings.seed and store.is_empty():
        _seed(store)

    # The signing key: from the environment, else made once and kept.
    if settings.signing_seed:
        signing_key = SigningKey(bytes.fromhex(settings.signing_seed))
    elif store.meta("signing_seed"):
        signing_key = SigningKey(bytes.fromhex(store.meta("signing_seed")))
    else:
        signing_key = SigningKey.generate()
        store.set_meta("signing_seed", signing_key.seed_hex)

    app = FastAPI(title="UltraAI model server", version=__version__)
    app.state.store = store
    app.state.settings = settings
    app.state.signing_key = signing_key

    def blob_path(sha: str) -> Path:
        return data_dir / "blobs" / sha[:2] / sha

    @app.exception_handler(HTTPException)
    async def http_error(_request: Request, exc: HTTPException):
        content = exc.detail if isinstance(exc.detail, dict) else {"error": "error", "error_description": str(exc.detail)}
        return JSONResponse(status_code=exc.status_code, content=content)

    @app.exception_handler(StoreError)
    async def store_error(_request: Request, exc: StoreError):
        return JSONResponse(status_code=exc.status, content={"error": exc.code, "error_description": exc.description})

    # ----- authentication -----
    def admin(authorization: str = Header(default="")) -> dict:
        token = authorization[7:].strip() if authorization.lower().startswith("bearer ") else ""
        if not token:
            raise _error(401, "unauthorized", "send Authorization: Bearer <token>")
        if settings.super_token and hmac.compare_digest(token, settings.super_token):
            return {"name": "super", "role": "super"}
        account = store.admin_by_token(token)
        if account:
            return account
        if not settings.super_token and not store.list_admins():
            raise _error(503, "not_configured", "the server has no super administrator token (ULTRAAI_MODELS_SUPER_TOKEN)")
        raise _error(401, "unauthorized", "the token is not one of this server's")

    def super_admin(who: dict = Depends(admin)) -> dict:
        if who["role"] != "super":
            raise _error(403, "forbidden", "only a super administrator may do this")
        return who

    async def body_json(request: Request) -> dict:
        try:
            body = await request.json()
        except ValueError:
            raise _error(400, "invalid_request", "the body is not JSON")
        if not isinstance(body, dict):
            raise _error(400, "invalid_request", "the body is a JSON object")
        return body

    base = settings.public_base_url

    # ----- what a client reads -----
    def documents() -> dict:
        return {"index.json": store.gallery_index, "catalogue.json": lambda: store.catalogue(base),
                "topics.json": store.topics_document}

    def keys_document() -> dict:
        keys = [signing_key.public_hex] + [k for k in settings.next_public_keys if k != signing_key.public_hex]
        return {"formatVersion": 1, "algorithm": "ed25519", "keys": keys}

    @app.get("/gallery/{document}")
    def gallery(document: str):
        if document == "keys.json":
            return Response(canonical_bytes(keys_document()), media_type="application/json",
                            headers={"Cache-Control": "max-age=300"})
        signature = document.endswith(".sig")
        name = document[:-4] if signature else document
        make = documents().get(name)
        if make is None:
            raise _error(404, "not_found", "no such document")
        data = canonical_bytes(make())
        sig = signing_key.sign_hex(data)
        if signature:
            return PlainTextResponse(sig + "\n", headers={"Cache-Control": "max-age=60"})
        return Response(data, media_type="application/json",
                        headers={"Cache-Control": "max-age=300", SIGNATURE_HEADER: sig})

    @app.get("/files/{sha}/{name}")
    def file(sha: str, name: str):
        # Only what a published model offers, under the name it offers it.
        if not SHA_RE.match(sha) or not store.blob_served(sha, name):
            raise _error(404, "not_found", "no such file")
        path = blob_path(sha)
        if not path.is_file():
            raise _error(404, "not_found", "the file is missing on the server")
        return FileResponse(path, filename=name, media_type="application/octet-stream",
                            headers={"Cache-Control": "public, max-age=31536000, immutable"})

    @app.get("/healthz", response_class=PlainTextResponse)
    def healthz():
        if not settings.super_token and not store.list_admins():
            return PlainTextResponse("ULTRAAI_MODELS_SUPER_TOKEN is not set", status_code=503)
        return "ok"

    # ----- admin: who -----
    @app.get("/v1/admin/whoami")
    def whoami(who: dict = Depends(admin)):
        return {"name": who["name"], "role": who["role"], "server": __version__, "published": store.published(),
                "revision": int(store.meta("revision") or "0"), "baseUrl": base, "signingKey": signing_key.public_hex}

    # ----- admin: models -----
    @app.get("/v1/admin/models")
    def models(status: str | None = None, who: dict = Depends(admin)):
        return {"models": store.list_models(base, status)}

    @app.get("/v1/admin/models/{model_id}")
    def model(model_id: str, who: dict = Depends(admin)):
        return store.get_model(model_id, base)

    @app.put("/v1/admin/models/{model_id}")
    async def put_model(model_id: str, request: Request, who: dict = Depends(admin)):
        body = await body_json(request)
        body.pop("files", None)      # files go through the file endpoints
        store.put_model(model_id, body, who["name"])
        return store.get_model(model_id, base)

    @app.delete("/v1/admin/models/{model_id}")
    def delete_model(model_id: str, who: dict = Depends(super_admin)):
        store.delete_model(model_id, who["name"])
        return {"deleted": model_id}

    @app.get("/v1/admin/models/{model_id}/check")
    def check_model(model_id: str, who: dict = Depends(admin)):
        problems = store.publish_problems(model_id)
        return {"publishable": not problems, "problems": problems}

    for action, status in (("publish", "published"), ("withdraw", "withdrawn"), ("draft", "draft")):
        def make(status: str):
            def change(model_id: str, who: dict = Depends(admin)):
                store.set_status(model_id, status, who["name"])
                return store.get_model(model_id, base)
            return change
        app.post(f"/v1/admin/models/{{model_id}}/{action}")(make(status))

    @app.put("/v1/admin/models/{model_id}/files/{name}")
    async def put_file(model_id: str, name: str, request: Request, who: dict = Depends(admin)):
        body = await body_json(request)
        size = body.get("size") or 0
        if isinstance(size, bool) or not isinstance(size, int) or size < 0:
            raise _error(400, "invalid_request", "size is a number of bytes")
        store.put_file(model_id, name, who["name"], url=body.get("url") or "", sha256=body.get("sha256") or "", size=size)
        return store.get_model(model_id, base)

    @app.put("/v1/admin/models/{model_id}/files/{name}/upload")
    async def upload_file(model_id: str, name: str, request: Request, who: dict = Depends(admin),
                          x_content_sha256: str = Header(default="")):
        check_file_name(name)
        store.get_model(model_id, base)                      # 404 before the bytes are read
        expected = x_content_sha256.strip().lower()
        if expected and not SHA_RE.match(expected):
            raise _error(400, "invalid_request", "X-Content-SHA256 is 64 hex digits")
        if expected and store.blob_size(expected) is not None and blob_path(expected).is_file():
            # Already here: nothing to store again.
            size = store.blob_size(expected) or 0
            store.put_file(model_id, name, who["name"], blob=expected, size=size)
            return {"sha256": expected, "size": size, "stored": False}
        tmp = data_dir / "tmp" / (secrets.token_hex(16) + ".part")
        digest = hashlib.sha256()
        size = 0
        try:
            with open(tmp, "wb") as out:
                async for chunk in request.stream():
                    size += len(chunk)
                    if size > settings.max_upload_bytes:
                        raise _error(413, "too_large", f"uploads are limited to {settings.max_upload_bytes} bytes")
                    digest.update(chunk)
                    out.write(chunk)
            sha = digest.hexdigest()
            if size == 0:
                raise _error(400, "invalid_request", "the upload is empty")
            if expected and sha != expected:
                raise _error(400, "hash_mismatch", f"the upload's SHA-256 is {sha}, not {expected}")
            target = blob_path(sha)
            target.parent.mkdir(parents=True, exist_ok=True)
            os.replace(tmp, target)
        finally:
            if tmp.exists():
                tmp.unlink()
        store.add_blob(sha, size, who["name"], name)
        store.put_file(model_id, name, who["name"], blob=sha, size=size)
        return {"sha256": sha, "size": size, "stored": True}

    @app.delete("/v1/admin/models/{model_id}/files/{name}")
    def delete_file(model_id: str, name: str, who: dict = Depends(admin)):
        store.delete_file(model_id, name, who["name"])
        return store.get_model(model_id, base)

    # ----- admin: topics -----
    @app.get("/v1/admin/topics")
    def topics(who: dict = Depends(admin)):
        return {"topics": store.list_topics()}

    @app.put("/v1/admin/topics/{topic_id}")
    async def put_topic(topic_id: str, request: Request, who: dict = Depends(admin)):
        return store.put_topic(topic_id, await body_json(request), who["name"])

    @app.delete("/v1/admin/topics/{topic_id}")
    def delete_topic(topic_id: str, who: dict = Depends(super_admin)):
        store.delete_topic(topic_id, who["name"])
        return {"deleted": topic_id}

    # ----- admin: engines -----
    @app.get("/v1/admin/engines")
    def engines(who: dict = Depends(admin)):
        return {"engines": store.list_engines()}

    @app.put("/v1/admin/engines/{engine_id}")
    async def put_engine(engine_id: str, request: Request, who: dict = Depends(super_admin)):
        return store.put_engine(engine_id, await body_json(request), who["name"])

    @app.delete("/v1/admin/engines/{engine_id}")
    def delete_engine(engine_id: str, who: dict = Depends(super_admin)):
        store.delete_engine(engine_id, who["name"])
        return {"deleted": engine_id}

    @app.post("/v1/admin/import")
    async def import_documents(request: Request, who: dict = Depends(super_admin)):
        body = await body_json(request)
        return store.import_documents(who["name"], index=body.get("index"), catalogue=body.get("catalogue"),
                                      topics=body.get("topics"), publish=bool(body.get("publish")))

    # ----- admin: uploads -----
    @app.get("/v1/admin/blobs")
    def blobs(who: dict = Depends(admin)):
        return {"blobs": store.list_blobs()}

    @app.post("/v1/admin/blobs/prune")
    def prune(who: dict = Depends(super_admin)):
        unused = store.unused_blobs()
        freed = 0
        for sha in unused:
            path = blob_path(sha)
            if path.is_file():
                freed += path.stat().st_size
                path.unlink()
        store.forget_blobs(unused, who["name"])
        return {"deleted": len(unused), "bytes": freed}

    # ----- admin: accounts -----
    @app.get("/v1/admin/admins")
    def admins(who: dict = Depends(super_admin)):
        return {"admins": store.list_admins()}

    @app.put("/v1/admin/admins/{name}")
    async def put_admin(name: str, request: Request, who: dict = Depends(super_admin)):
        body = await body_json(request)
        token = store.put_admin(name, body.get("role") or "editor", who["name"])
        return {"name": name, "role": body.get("role") or "editor", "token": token}

    @app.delete("/v1/admin/admins/{name}")
    def delete_admin(name: str, who: dict = Depends(super_admin)):
        if name == who["name"]:
            raise _error(409, "in_use", "an account cannot delete itself")
        store.delete_admin(name, who["name"])
        return {"deleted": name}

    @app.get("/v1/admin/audit")
    def audit(since: int = 0, who: dict = Depends(admin)):
        entries = store.audit_since(since)
        return {"entries": entries, "next": entries[-1]["seq"] if entries else since}

    return app
