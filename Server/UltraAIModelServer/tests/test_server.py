"""The model server end to end, through FastAPI's test client."""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path

import pytest
from fastapi.testclient import TestClient

from ultraai_models.app import SEED_DIR, create_app
from ultraai_models.config import Settings

SUPER = "super-secret-token"
REPO = Path(__file__).resolve().parents[3]


def make(tmp_path: Path, seed: bool = True, token: str = SUPER) -> TestClient:
    s = Settings(database=str(tmp_path / "db.sqlite3"), data_dir=str(tmp_path / "data"),
                 public_base_url="https://models.example", super_token=token, seed=seed)
    return TestClient(create_app(s))


def auth(token: str = SUPER) -> dict:
    return {"Authorization": f"Bearer {token}"}


def draft(client: TestClient, model_id: str = "tiny-q4", capability: str = "textllm") -> None:
    r = client.put(f"/v1/admin/models/{model_id}", headers=auth(), json={
        "displayName": "Tiny", "capability": capability, "engine": "llama-server", "license": "MIT",
        "requires": {"memoryBytes": 1000}, "features": {"contextWindow": 4096, "tools": True}, "tags": ["chat"]})
    assert r.status_code == 200, r.text


def test_seed_publishes_the_builtin_gallery(tmp_path):
    c = make(tmp_path)
    index = c.get("/gallery/index.json").json()
    assert index["formatVersion"] == 1 and index["engines"][0]["id"] == "llama-server"
    cat = c.get("/gallery/catalogue.json").json()
    ids = {m["id"] for m in cat["models"]}
    assert "qwen3-4b-q4" in ids and "nomic-embed-text-v1.5-q8" in ids
    # The public catalogue carries no admin fields.
    assert "status" not in cat["models"][0]
    topics = c.get("/gallery/topics.json").json()["topics"]
    chat = next(t for t in topics if t["id"] == "chat")
    assert chat["models"][0] == {"id": "qwen3-4b-q4", "default": True}


def test_seed_matches_the_framework_builtins():
    # The server's first state is what a client without it already has.
    src = (REPO / "UltraAI/runtime/src/LocalRuntimeBuiltinData.cpp").read_text(encoding="utf-8")
    blocks = re.findall(r'R"json\((.*?)\)json"', src, re.S)
    assert len(blocks) == 3, "index, catalogue and topics"
    for name, block in zip(("index", "catalogue", "topics"), blocks):
        assert json.loads(block) == json.loads((SEED_DIR / f"{name}.json").read_text(encoding="utf-8")), name


def test_admin_needs_a_token(tmp_path):
    c = make(tmp_path)
    assert c.get("/v1/admin/models").status_code == 401
    assert c.get("/v1/admin/models", headers=auth("wrong")).status_code == 401
    assert c.get("/v1/admin/whoami", headers=auth()).json()["role"] == "super"


def test_without_a_super_token_the_admin_api_is_closed(tmp_path):
    c = make(tmp_path, token="")
    assert c.get("/v1/admin/models", headers=auth("anything")).status_code == 503
    assert c.get("/healthz").status_code == 503


def test_model_lifecycle_with_an_upload(tmp_path):
    c = make(tmp_path)
    draft(c)
    # A draft is not offered.
    assert "tiny-q4" not in {m["id"] for m in c.get("/gallery/catalogue.json").json()["models"]}
    check = c.get("/v1/admin/models/tiny-q4/check", headers=auth()).json()
    assert not check["publishable"] and "no files" in check["problems"][0]
    assert c.post("/v1/admin/models/tiny-q4/publish", headers=auth()).status_code == 409

    payload = b"GGUF" + bytes(range(256)) * 64
    sha = hashlib.sha256(payload).hexdigest()
    r = c.put("/v1/admin/models/tiny-q4/files/tiny.gguf/upload", headers={**auth(), "X-Content-SHA256": sha}, content=payload)
    assert r.status_code == 200, r.text
    assert r.json() == {"sha256": sha, "size": len(payload), "stored": True}
    # Not served while the model is a draft.
    assert c.get(f"/files/{sha}/tiny.gguf").status_code == 404

    r = c.post("/v1/admin/models/tiny-q4/publish", headers=auth())
    assert r.status_code == 200 and r.json()["status"] == "published"
    model = next(m for m in c.get("/gallery/catalogue.json").json()["models"] if m["id"] == "tiny-q4")
    assert model["files"] == [{"name": "tiny.gguf", "url": f"https://models.example/files/{sha}/tiny.gguf",
                               "sha256": sha, "size": len(payload)}]
    assert model["features"] == {"contextWindow": 4096, "tools": True}

    got = c.get(f"/files/{sha}/tiny.gguf")
    assert got.status_code == 200 and got.content == payload
    # A resumed download.
    part = c.get(f"/files/{sha}/tiny.gguf", headers={"Range": "bytes=100-"})
    assert part.status_code == 206 and part.content == payload[100:]
    # Only under the name the model offers it.
    assert c.get(f"/files/{sha}/other.gguf").status_code == 404

    # The same bytes again are not stored twice.
    r = c.put("/v1/admin/models/tiny-q4/files/tiny.gguf/upload", headers={**auth(), "X-Content-SHA256": sha}, content=b"")
    assert r.json()["stored"] is False

    # Withdrawn: gone from the catalogue and the files, kept in the database.
    c.post("/v1/admin/models/tiny-q4/withdraw", headers=auth())
    assert "tiny-q4" not in {m["id"] for m in c.get("/gallery/catalogue.json").json()["models"]}
    assert c.get(f"/files/{sha}/tiny.gguf").status_code == 404
    assert c.get("/v1/admin/models/tiny-q4", headers=auth()).json()["status"] == "withdrawn"


def test_an_upload_with_the_wrong_hash_is_refused(tmp_path):
    c = make(tmp_path)
    draft(c)
    r = c.put("/v1/admin/models/tiny-q4/files/tiny.gguf/upload", headers={**auth(), "X-Content-SHA256": "0" * 64}, content=b"abc")
    assert r.status_code == 400 and r.json()["error"] == "hash_mismatch"
    assert c.get("/v1/admin/models/tiny-q4", headers=auth()).json()["files"] == []
    assert list((tmp_path / "data" / "tmp").iterdir()) == []


def test_external_files_and_the_publish_checks(tmp_path):
    c = make(tmp_path)
    draft(c)
    r = c.put("/v1/admin/models/tiny-q4/files/a.gguf", headers=auth(), json={"url": "http://insecure/a.gguf", "size": 5})
    assert r.status_code == 400
    c.put("/v1/admin/models/tiny-q4/files/a.gguf", headers=auth(), json={"url": "https://cdn.example/a.gguf", "size": 5})
    problems = c.get("/v1/admin/models/tiny-q4/check", headers=auth()).json()["problems"]
    assert any("SHA-256" in p for p in problems)
    c.put("/v1/admin/models/tiny-q4/files/a.gguf", headers=auth(), json={"url": "https://cdn.example/a.gguf", "sha256": "A" * 64, "size": 5})
    assert c.post("/v1/admin/models/tiny-q4/publish", headers=auth()).status_code == 200
    model = next(m for m in c.get("/gallery/catalogue.json").json()["models"] if m["id"] == "tiny-q4")
    assert model["files"][0]["sha256"] == "a" * 64
    # The last file of a published model stays.
    assert c.delete("/v1/admin/models/tiny-q4/files/a.gguf", headers=auth()).status_code == 409


def test_topics(tmp_path):
    c = make(tmp_path)
    r = c.put("/v1/admin/topics/legal", headers=auth(), json={
        "name": "Legal texts", "capability": "textllm", "models": [{"id": "qwen3-8b-q4"}, {"id": "qwen3-4b-q4", "default": True}]})
    assert r.status_code == 200, r.text
    legal = next(t for t in c.get("/gallery/topics.json").json()["topics"] if t["id"] == "legal")
    assert legal["models"] == [{"id": "qwen3-8b-q4"}, {"id": "qwen3-4b-q4", "default": True}]
    # A model of another capability does not fit the topic.
    r = c.put("/v1/admin/topics/legal", headers=auth(), json={"name": "Legal", "capability": "textllm", "models": ["nomic-embed-text-v1.5-q8"]})
    assert r.status_code == 400
    # A withdrawn default gives way to the first remaining model.
    c.post("/v1/admin/models/qwen3-4b-q4/withdraw", headers=auth())
    legal = next(t for t in c.get("/gallery/topics.json").json()["topics"] if t["id"] == "legal")
    assert legal["models"] == [{"id": "qwen3-8b-q4", "default": True}]


def test_published_date_and_revision_move(tmp_path):
    c = make(tmp_path)
    before = c.get("/v1/admin/whoami", headers=auth()).json()["revision"]
    c.put("/v1/admin/topics/chat", headers=auth(), json={"name": "Chat", "capability": "textllm", "models": ["qwen3-8b-q4"]})
    assert c.get("/v1/admin/whoami", headers=auth()).json()["revision"] > before


def test_roles(tmp_path):
    c = make(tmp_path)
    r = c.put("/v1/admin/admins/alice", headers=auth(), json={"role": "editor"})
    token = r.json()["token"]
    assert c.get("/v1/admin/whoami", headers=auth(token)).json() == {**c.get("/v1/admin/whoami", headers=auth(token)).json(),
                                                                    "name": "alice", "role": "editor"}
    # An editor manages models and topics ...
    r = c.put("/v1/admin/models/e-model", headers=auth(token), json={"capability": "embeddings", "engine": "llama-server"})
    assert r.status_code == 200
    # ... but not engines, accounts or deletions.
    assert c.put("/v1/admin/engines/x", headers=auth(token), json={"version": "1"}).status_code == 403
    assert c.get("/v1/admin/admins", headers=auth(token)).status_code == 403
    assert c.delete("/v1/admin/models/e-model", headers=auth(token)).status_code == 403
    # The account list holds no token, and the audit log names who did what.
    admins = c.get("/v1/admin/admins", headers=auth()).json()["admins"]
    assert admins[0]["name"] == "alice" and "token" not in admins[0] and "token_hash" not in admins[0]
    log = c.get("/v1/admin/audit", headers=auth()).json()["entries"]
    assert any(e["admin"] == "alice" and e["action"] == "model.put" for e in log)
    # A new token replaces the old one.
    c.put("/v1/admin/admins/alice", headers=auth(), json={"role": "super"})
    assert c.get("/v1/admin/whoami", headers=auth(token)).status_code == 401
    c.delete("/v1/admin/admins/alice", headers=auth())
    assert c.get("/v1/admin/admins", headers=auth()).json()["admins"] == []


def test_engines_and_import(tmp_path):
    c = make(tmp_path, seed=False)
    assert c.get("/gallery/index.json").json()["engines"] == []
    docs = {n: json.loads((SEED_DIR / f"{n}.json").read_text()) for n in ("index", "catalogue", "topics")}
    r = c.post("/v1/admin/import", headers=auth(), json={**docs, "publish": False})
    assert r.json() == {"engines": 2, "models": 9, "topics": 7}   # llama-server and whisper-server
    # Imported as drafts: nothing offered yet, and the topics list none.
    assert c.get("/gallery/catalogue.json").json()["models"] == []
    assert all(t["models"] == [] for t in c.get("/gallery/topics.json").json()["topics"])
    # An engine a published model runs on stays.
    c.post("/v1/admin/models/qwen3-4b-q4/publish", headers=auth())
    assert c.delete("/v1/admin/engines/llama-server", headers=auth()).status_code == 409
    bad = c.put("/v1/admin/engines/llama-server", headers=auth(), json={"version": "b1", "serves": ["textllm"],
                                                                       "bundles": [{"platform": "amiga", "url": "https://x/y"}]})
    assert bad.status_code == 400


def test_prune_deletes_unused_uploads(tmp_path):
    c = make(tmp_path)
    draft(c)
    c.put("/v1/admin/models/tiny-q4/files/t.gguf/upload", headers=auth(), content=b"one")
    c.put("/v1/admin/models/tiny-q4/files/t.gguf/upload", headers=auth(), content=b"two")   # replaces the first
    blobs = c.get("/v1/admin/blobs", headers=auth()).json()["blobs"]
    assert sorted(b["used"] for b in blobs) == [0, 1]
    r = c.post("/v1/admin/blobs/prune", headers=auth()).json()
    assert r == {"deleted": 1, "bytes": 3}
    assert len(c.get("/v1/admin/blobs", headers=auth()).json()["blobs"]) == 1
