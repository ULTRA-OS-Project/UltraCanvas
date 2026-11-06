"""The model server's database, on SQLite.

What the super administrator manages lives here: the engines and their
bundles (the gallery index), the models with their files (the catalogue),
the topics a client picks a model for (chat, code, translation, ...), the
administrator accounts and the audit log. Uploaded files are not in the
database - only their hashes; the bytes are under the data folder.

A model is a draft until it is published; only published models reach
catalogue.json and topics.json. Withdrawing one takes it out of both
without deleting it, so a client that has it installed keeps it and the
administrator can publish it again."""

from __future__ import annotations

import hashlib
import json
import re
import secrets
import sqlite3
import threading
import time
from typing import Any, Iterable

CAPABILITIES = ("textllm", "embeddings", "speechtotext", "texttospeech", "imagegen")
ROLES = ("super", "editor")
STATUSES = ("draft", "published", "withdrawn")
PLATFORMS = ("linux-x64", "linux-arm64", "macos-x64", "macos-arm64", "win-x64", "win-arm64")

ID_RE = re.compile(r"^[a-z0-9][a-z0-9._-]{0,63}$")
FILE_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._+-]{0,200}$")
SHA_RE = re.compile(r"^[0-9a-f]{64}$")
NAME_RE = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._@-]{0,63}$")


class StoreError(Exception):
    """A request the database refuses: `code` is the API's error name."""

    def __init__(self, code: str, description: str, status: int = 400) -> None:
        super().__init__(description)
        self.code = code
        self.description = description
        self.status = status


def now_iso() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def today() -> str:
    return time.strftime("%Y-%m-%d", time.gmtime())


def hash_token(token: str) -> str:
    return hashlib.sha256(token.encode("utf-8")).hexdigest()


def _strings(value: Any, what: str) -> list[str]:
    if value is None:
        return []
    if not isinstance(value, list) or not all(isinstance(v, str) for v in value):
        raise StoreError("invalid_request", f"{what} must be a list of strings")
    return list(value)


def _uint(value: Any, what: str) -> int:
    if value is None:
        return 0
    if isinstance(value, bool) or not isinstance(value, (int, float)) or value < 0:
        raise StoreError("invalid_request", f"{what} must be a number of 0 or more")
    return int(value)


def check_id(value: str, what: str = "id") -> str:
    if not isinstance(value, str) or not ID_RE.match(value):
        raise StoreError("invalid_request", f"{what} must be 1-64 of a-z, 0-9, '.', '_' and '-', starting with a letter or digit")
    return value


def check_file_name(value: str) -> str:
    if not isinstance(value, str) or not FILE_RE.match(value) or ".." in value:
        raise StoreError("invalid_request", "a file name is 1-200 of letters, digits, '.', '_', '+' and '-', with no folder")
    return value


def check_url(value: str, what: str = "url") -> str:
    if not isinstance(value, str) or not value.startswith("https://") or len(value) > 2048 or any(c.isspace() for c in value):
        raise StoreError("invalid_request", f"{what} must be an https:// URL")
    return value


def check_sha(value: str) -> str:
    value = (value or "").lower()
    if value and not SHA_RE.match(value):
        raise StoreError("invalid_request", "sha256 must be 64 hex digits")
    return value


class Store:
    def __init__(self, path: str) -> None:
        self._db = sqlite3.connect(path, check_same_thread=False, isolation_level=None)
        self._db.row_factory = sqlite3.Row
        self._lock = threading.RLock()
        self._db.execute("PRAGMA journal_mode=WAL")
        self._db.execute("PRAGMA foreign_keys=ON")
        self._db.executescript("""
            CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS admins (
                name TEXT PRIMARY KEY, role TEXT NOT NULL, token_hash TEXT NOT NULL UNIQUE,
                created_at TEXT NOT NULL, created_by TEXT NOT NULL, last_used_at TEXT NOT NULL DEFAULT '');
            CREATE TABLE IF NOT EXISTS engines (
                id TEXT PRIMARY KEY, version TEXT NOT NULL, license TEXT NOT NULL DEFAULT '',
                executable TEXT NOT NULL, serves TEXT NOT NULL DEFAULT '[]', default_args TEXT NOT NULL DEFAULT '[]',
                updated_at TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS bundles (
                engine_id TEXT NOT NULL REFERENCES engines(id) ON DELETE CASCADE,
                platform TEXT NOT NULL, accel TEXT NOT NULL, version TEXT NOT NULL DEFAULT '',
                url TEXT NOT NULL, sha256 TEXT NOT NULL DEFAULT '', size INTEGER NOT NULL DEFAULT 0,
                optional INTEGER NOT NULL DEFAULT 0, min_os TEXT NOT NULL DEFAULT '', requires TEXT NOT NULL DEFAULT '{}',
                PRIMARY KEY (engine_id, platform, accel));
            CREATE TABLE IF NOT EXISTS models (
                id TEXT PRIMARY KEY, display_name TEXT NOT NULL, description TEXT NOT NULL DEFAULT '',
                capability TEXT NOT NULL, engine TEXT NOT NULL, license TEXT NOT NULL DEFAULT '',
                requires TEXT NOT NULL DEFAULT '{}', features TEXT NOT NULL DEFAULT '{}',
                engine_args TEXT NOT NULL DEFAULT '[]', tags TEXT NOT NULL DEFAULT '[]',
                status TEXT NOT NULL DEFAULT 'draft', published_at TEXT NOT NULL DEFAULT '',
                created_at TEXT NOT NULL, updated_at TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS model_files (
                model_id TEXT NOT NULL REFERENCES models(id) ON DELETE CASCADE,
                name TEXT NOT NULL, position INTEGER NOT NULL DEFAULT 0,
                url TEXT NOT NULL DEFAULT '', blob TEXT NOT NULL DEFAULT '',
                sha256 TEXT NOT NULL DEFAULT '', size INTEGER NOT NULL DEFAULT 0,
                PRIMARY KEY (model_id, name));
            CREATE TABLE IF NOT EXISTS blobs (
                sha256 TEXT PRIMARY KEY, size INTEGER NOT NULL, uploaded_at TEXT NOT NULL,
                uploaded_by TEXT NOT NULL, original_name TEXT NOT NULL DEFAULT '');
            CREATE TABLE IF NOT EXISTS topics (
                id TEXT PRIMARY KEY, name TEXT NOT NULL, description TEXT NOT NULL DEFAULT '',
                capability TEXT NOT NULL, position INTEGER NOT NULL DEFAULT 0, updated_at TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS topic_models (
                topic_id TEXT NOT NULL REFERENCES topics(id) ON DELETE CASCADE,
                model_id TEXT NOT NULL REFERENCES models(id) ON DELETE CASCADE,
                rank INTEGER NOT NULL DEFAULT 0, is_default INTEGER NOT NULL DEFAULT 0,
                PRIMARY KEY (topic_id, model_id));
            CREATE TABLE IF NOT EXISTS audit (
                seq INTEGER PRIMARY KEY AUTOINCREMENT, at TEXT NOT NULL, admin TEXT NOT NULL,
                action TEXT NOT NULL, target TEXT NOT NULL DEFAULT '', detail TEXT NOT NULL DEFAULT '');
        """)

    def close(self) -> None:
        self._db.close()

    # ----- helpers -----
    def _tx(self):
        """BEGIN IMMEDIATE ... COMMIT, under the store's lock."""
        store = self

        class _Tx:
            def __enter__(self_inner):
                store._lock.acquire()
                store._db.execute("BEGIN IMMEDIATE")
                return store._db

            def __exit__(self_inner, exc_type, exc, tb):
                try:
                    store._db.execute("ROLLBACK" if exc_type else "COMMIT")
                finally:
                    store._lock.release()
                return False

        return _Tx()

    def meta(self, key: str) -> str:
        row = self._db.execute("SELECT value FROM meta WHERE key = ?", (key,)).fetchone()
        return row["value"] if row else ""

    def set_meta(self, key: str, value: str) -> None:
        self._db.execute("INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)", (key, value))

    def is_empty(self) -> bool:
        for table in ("engines", "models", "topics"):
            if self._db.execute(f"SELECT 1 FROM {table} LIMIT 1").fetchone():
                return False
        return True

    def _touch_published(self, db: sqlite3.Connection) -> None:
        """What a client reads changed: the published date moves on."""
        db.execute("INSERT OR REPLACE INTO meta (key, value) VALUES ('published', ?)", (today(),))
        db.execute("INSERT OR REPLACE INTO meta (key, value) VALUES ('revision', ?)",
                   (str(int(self.meta("revision") or "0") + 1),))

    def audit(self, admin: str, action: str, target: str = "", detail: str = "") -> None:
        self._db.execute("INSERT INTO audit (at, admin, action, target, detail) VALUES (?, ?, ?, ?, ?)",
                         (now_iso(), admin, action, target, detail[:2000]))

    def audit_since(self, since: int, limit: int = 500) -> list[dict]:
        rows = self._db.execute("SELECT * FROM audit WHERE seq > ? ORDER BY seq LIMIT ?", (since, limit)).fetchall()
        return [dict(r) for r in rows]

    # ----- administrators -----
    def admin_by_token(self, token: str) -> dict | None:
        row = self._db.execute("SELECT name, role FROM admins WHERE token_hash = ?", (hash_token(token),)).fetchone()
        if not row:
            return None
        self._db.execute("UPDATE admins SET last_used_at = ? WHERE name = ?", (now_iso(), row["name"]))
        return {"name": row["name"], "role": row["role"]}

    def list_admins(self) -> list[dict]:
        rows = self._db.execute("SELECT name, role, created_at, created_by, last_used_at FROM admins ORDER BY name").fetchall()
        return [dict(r) for r in rows]

    def put_admin(self, name: str, role: str, by: str) -> str:
        """Make an account, or give an existing one a new role and a new
        token. Returns the token - the only time it is ever seen."""
        if not isinstance(name, str) or not NAME_RE.match(name) or name == "super":
            raise StoreError("invalid_request", "an account name is 1-64 of letters, digits, '.', '_', '@' and '-' (and not \"super\")")
        if role not in ROLES:
            raise StoreError("invalid_request", f"role must be one of {', '.join(ROLES)}")
        token = "uaims_" + secrets.token_urlsafe(32)
        with self._tx() as db:
            db.execute("""INSERT INTO admins (name, role, token_hash, created_at, created_by) VALUES (?, ?, ?, ?, ?)
                          ON CONFLICT(name) DO UPDATE SET role = excluded.role, token_hash = excluded.token_hash""",
                       (name, role, hash_token(token), now_iso(), by))
        self.audit(by, "admin.put", name, role)
        return token

    def delete_admin(self, name: str, by: str) -> None:
        with self._tx() as db:
            if db.execute("DELETE FROM admins WHERE name = ?", (name,)).rowcount == 0:
                raise StoreError("not_found", f"no account \"{name}\"", 404)
        self.audit(by, "admin.delete", name)

    # ----- engines -----
    def _engine(self, row: sqlite3.Row) -> dict:
        bundles = self._db.execute("SELECT * FROM bundles WHERE engine_id = ? ORDER BY platform, accel", (row["id"],)).fetchall()
        out = {"id": row["id"], "version": row["version"], "license": row["license"], "executable": row["executable"],
               "serves": json.loads(row["serves"]), "defaultArgs": json.loads(row["default_args"]), "bundles": []}
        for b in bundles:
            bj: dict[str, Any] = {"platform": b["platform"], "accel": b["accel"], "url": b["url"]}
            if b["version"]:
                bj["version"] = b["version"]
            if b["sha256"]:
                bj["sha256"] = b["sha256"]
            if b["size"]:
                bj["size"] = b["size"]
            if b["optional"]:
                bj["optional"] = True
            if b["min_os"]:
                bj["minOs"] = b["min_os"]
            req = json.loads(b["requires"])
            if req:
                bj["requires"] = req
            out["bundles"].append(bj)
        return out

    def list_engines(self) -> list[dict]:
        return [self._engine(r) for r in self._db.execute("SELECT * FROM engines ORDER BY id").fetchall()]

    def put_engine(self, engine_id: str, body: dict, by: str, *, audit: bool = True) -> dict:
        check_id(engine_id, "engine id")
        version = body.get("version")
        if not isinstance(version, str) or not version:
            raise StoreError("invalid_request", "an engine needs a \"version\"")
        executable = body.get("executable") or engine_id
        if not isinstance(executable, str) or not FILE_RE.match(executable):
            raise StoreError("invalid_request", "\"executable\" is a file name")
        serves = _strings(body.get("serves"), "serves")
        for cap in serves:
            if cap not in CAPABILITIES:
                raise StoreError("invalid_request", f"\"{cap}\" is not a capability ({', '.join(CAPABILITIES)})")
        default_args = _strings(body.get("defaultArgs"), "defaultArgs")
        bundles = body.get("bundles") or []
        if not isinstance(bundles, list):
            raise StoreError("invalid_request", "\"bundles\" is a list")
        rows = []
        seen = set()
        for b in bundles:
            if not isinstance(b, dict):
                raise StoreError("invalid_request", "a bundle is an object")
            platform, accel = b.get("platform"), b.get("accel") or "cpu"
            if platform not in PLATFORMS:
                raise StoreError("invalid_request", f"a bundle's platform is one of {', '.join(PLATFORMS)}")
            if not isinstance(accel, str) or not ID_RE.match(accel):
                raise StoreError("invalid_request", "a bundle's accel is an id (cpu, vulkan, metal, cuda12, rocm)")
            if (platform, accel) in seen:
                raise StoreError("invalid_request", f"two bundles for {platform} / {accel}")
            seen.add((platform, accel))
            req = b.get("requires") or {}
            if not isinstance(req, dict) or not all(isinstance(v, str) for v in req.values()):
                raise StoreError("invalid_request", "a bundle's \"requires\" maps names to strings")
            rows.append((engine_id, platform, accel, str(b.get("version") or ""), check_url(b.get("url"), "a bundle's url"),
                         check_sha(b.get("sha256") or ""), _uint(b.get("size"), "size"), 1 if b.get("optional") else 0,
                         str(b.get("minOs") or ""), json.dumps(req)))
        with self._tx() as db:
            db.execute("""INSERT INTO engines (id, version, license, executable, serves, default_args, updated_at)
                          VALUES (?, ?, ?, ?, ?, ?, ?)
                          ON CONFLICT(id) DO UPDATE SET version = excluded.version, license = excluded.license,
                              executable = excluded.executable, serves = excluded.serves,
                              default_args = excluded.default_args, updated_at = excluded.updated_at""",
                       (engine_id, version, str(body.get("license") or ""), executable, json.dumps(serves),
                        json.dumps(default_args), now_iso()))
            db.execute("DELETE FROM bundles WHERE engine_id = ?", (engine_id,))
            db.executemany("""INSERT INTO bundles (engine_id, platform, accel, version, url, sha256, size, optional, min_os, requires)
                              VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)""", rows)
            self._touch_published(db)
        if audit:
            self.audit(by, "engine.put", engine_id, f"{version}, {len(rows)} bundles")
        return self._engine(self._db.execute("SELECT * FROM engines WHERE id = ?", (engine_id,)).fetchone())

    def delete_engine(self, engine_id: str, by: str) -> None:
        used = self._db.execute("SELECT id FROM models WHERE engine = ? AND status = 'published'", (engine_id,)).fetchall()
        if used:
            raise StoreError("in_use", "published models run on this engine: " + ", ".join(r["id"] for r in used), 409)
        with self._tx() as db:
            if db.execute("DELETE FROM engines WHERE id = ?", (engine_id,)).rowcount == 0:
                raise StoreError("not_found", f"no engine \"{engine_id}\"", 404)
            self._touch_published(db)
        self.audit(by, "engine.delete", engine_id)

    # ----- models -----
    def _model(self, row: sqlite3.Row, base_url: str = "", public: bool = False) -> dict:
        out: dict[str, Any] = {"id": row["id"], "displayName": row["display_name"]}
        if row["description"]:
            out["description"] = row["description"]
        out["capability"] = row["capability"]
        out["engine"] = row["engine"]
        if row["license"]:
            out["license"] = row["license"]
        files = []
        for f in self._db.execute("SELECT * FROM model_files WHERE model_id = ? ORDER BY position, name", (row["id"],)).fetchall():
            fj: dict[str, Any] = {"name": f["name"]}
            if f["blob"]:
                fj["url"] = f"{base_url}/files/{f['blob']}/{f['name']}"
                fj["sha256"] = f["blob"]
                if not public:
                    fj["uploaded"] = True
            else:
                fj["url"] = f["url"]
                if f["sha256"]:
                    fj["sha256"] = f["sha256"]
            if f["size"]:
                fj["size"] = f["size"]
            files.append(fj)
        out["files"] = files
        for key, col in (("requires", "requires"), ("features", "features")):
            value = json.loads(row[col])
            if value:
                out[key] = value
        for key, col in (("engineArgs", "engine_args"), ("tags", "tags")):
            value = json.loads(row[col])
            if value:
                out[key] = value
        if not public:
            out["status"] = row["status"]
            out["publishedAt"] = row["published_at"]
            out["createdAt"] = row["created_at"]
            out["updatedAt"] = row["updated_at"]
            out["topics"] = [r["topic_id"] for r in self._db.execute(
                "SELECT topic_id FROM topic_models WHERE model_id = ? ORDER BY topic_id", (row["id"],)).fetchall()]
        return out

    def list_models(self, base_url: str, status: str | None = None) -> list[dict]:
        if status:
            rows = self._db.execute("SELECT * FROM models WHERE status = ? ORDER BY capability, id", (status,)).fetchall()
        else:
            rows = self._db.execute("SELECT * FROM models ORDER BY capability, id").fetchall()
        return [self._model(r, base_url) for r in rows]

    def get_model(self, model_id: str, base_url: str) -> dict:
        row = self._db.execute("SELECT * FROM models WHERE id = ?", (model_id,)).fetchone()
        if not row:
            raise StoreError("not_found", f"no model \"{model_id}\"", 404)
        return self._model(row, base_url)

    def put_model(self, model_id: str, body: dict, by: str, *, status: str | None = None, audit: bool = True) -> None:
        """Create a model (a draft) or change its description. The files are
        changed through put_file / delete_file; a body's "files" list is
        taken only when it holds external URLs (an import)."""
        check_id(model_id, "model id")
        display = body.get("displayName") or model_id
        if not isinstance(display, str) or len(display) > 200:
            raise StoreError("invalid_request", "\"displayName\" is text of up to 200 characters")
        capability = body.get("capability")
        if capability not in CAPABILITIES:
            raise StoreError("invalid_request", f"\"capability\" is one of {', '.join(CAPABILITIES)}")
        engine = body.get("engine")
        if not isinstance(engine, str) or not ID_RE.match(engine):
            raise StoreError("invalid_request", "\"engine\" names an engine of the gallery")
        requires = body.get("requires") or {}
        features = body.get("features") or {}
        if not isinstance(requires, dict) or not isinstance(features, dict):
            raise StoreError("invalid_request", "\"requires\" and \"features\" are objects")
        req = {k: _uint(requires.get(k), k) for k in ("memoryBytes", "gpuMemoryBytes") if requires.get(k)}
        feat: dict[str, Any] = {}
        for k in ("contextWindow", "embeddingDimensions"):
            if features.get(k):
                feat[k] = _uint(features.get(k), k)
        for k in ("tools", "vision"):
            if features.get(k):
                feat[k] = bool(features.get(k))
        engine_args = _strings(body.get("engineArgs"), "engineArgs")
        tags = _strings(body.get("tags"), "tags")
        description = body.get("description") or ""
        if not isinstance(description, str) or len(description) > 4000:
            raise StoreError("invalid_request", "\"description\" is text of up to 4000 characters")
        files = body.get("files")
        file_rows = None
        if files is not None:
            if not isinstance(files, list):
                raise StoreError("invalid_request", "\"files\" is a list")
            file_rows = []
            for pos, f in enumerate(files):
                if not isinstance(f, dict):
                    raise StoreError("invalid_request", "a file is an object")
                file_rows.append((model_id, check_file_name(f.get("name")), pos, check_url(f.get("url"), "a file's url"),
                                  check_sha(f.get("sha256") or ""), _uint(f.get("size"), "size")))
        stamp = now_iso()
        with self._tx() as db:
            existing = db.execute("SELECT status FROM models WHERE id = ?", (model_id,)).fetchone()
            new_status = status or (existing["status"] if existing else "draft")
            db.execute("""INSERT INTO models (id, display_name, description, capability, engine, license, requires, features,
                                              engine_args, tags, status, published_at, created_at, updated_at)
                          VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
                          ON CONFLICT(id) DO UPDATE SET display_name = excluded.display_name, description = excluded.description,
                              capability = excluded.capability, engine = excluded.engine, license = excluded.license,
                              requires = excluded.requires, features = excluded.features, engine_args = excluded.engine_args,
                              tags = excluded.tags, status = excluded.status, updated_at = excluded.updated_at""",
                       (model_id, display, description, capability, engine, str(body.get("license") or ""), json.dumps(req),
                        json.dumps(feat), json.dumps(engine_args), json.dumps(tags), new_status,
                        stamp if new_status == "published" else "", stamp, stamp))
            if existing and existing["status"] == "published" and new_status == "published":
                pass
            elif new_status == "published":
                db.execute("UPDATE models SET published_at = ? WHERE id = ?", (stamp, model_id))
            if file_rows is not None:
                db.execute("DELETE FROM model_files WHERE model_id = ?", (model_id,))
                db.executemany("INSERT INTO model_files (model_id, name, position, url, sha256, size) VALUES (?, ?, ?, ?, ?, ?)", file_rows)
            if new_status == "published" or (existing and existing["status"] == "published"):
                self._touch_published(db)
        if audit:
            self.audit(by, "model.put", model_id, "created" if not existing else "changed")

    def delete_model(self, model_id: str, by: str) -> None:
        with self._tx() as db:
            row = db.execute("SELECT status FROM models WHERE id = ?", (model_id,)).fetchone()
            if not row:
                raise StoreError("not_found", f"no model \"{model_id}\"", 404)
            db.execute("DELETE FROM models WHERE id = ?", (model_id,))
            if row["status"] == "published":
                self._touch_published(db)
        self.audit(by, "model.delete", model_id)

    def publish_problems(self, model_id: str) -> list[str]:
        """Why a model cannot be published yet; empty when it can."""
        row = self._db.execute("SELECT * FROM models WHERE id = ?", (model_id,)).fetchone()
        if not row:
            raise StoreError("not_found", f"no model \"{model_id}\"", 404)
        problems = []
        files = self._db.execute("SELECT * FROM model_files WHERE model_id = ?", (model_id,)).fetchall()
        if not files:
            problems.append("it has no files")
        for f in files:
            if not f["blob"] and not f["sha256"] and not f["url"].startswith("https://huggingface.co/"):
                problems.append(f"{f['name']}: no SHA-256 (only a Hugging Face file may leave it to the host)")
            if not f["size"]:
                problems.append(f"{f['name']}: no size")
        engine = self._db.execute("SELECT serves FROM engines WHERE id = ?", (row["engine"],)).fetchone()
        if not engine:
            problems.append(f"the engine \"{row['engine']}\" is not in the gallery")
        elif row["capability"] not in json.loads(engine["serves"]):
            problems.append(f"the engine \"{row['engine']}\" does not serve {row['capability']}")
        return problems

    def set_status(self, model_id: str, status: str, by: str) -> None:
        if status not in STATUSES:
            raise StoreError("invalid_request", f"status is one of {', '.join(STATUSES)}")
        if status == "published":
            problems = self.publish_problems(model_id)
            if problems:
                raise StoreError("not_publishable", "; ".join(problems), 409)
        with self._tx() as db:
            row = db.execute("SELECT status FROM models WHERE id = ?", (model_id,)).fetchone()
            if not row:
                raise StoreError("not_found", f"no model \"{model_id}\"", 404)
            if row["status"] == status:
                return
            db.execute("UPDATE models SET status = ?, updated_at = ?, published_at = CASE WHEN ? = 'published' THEN ? ELSE published_at END WHERE id = ?",
                       (status, now_iso(), status, now_iso(), model_id))
            self._touch_published(db)
        self.audit(by, "model." + {"published": "publish", "withdrawn": "withdraw", "draft": "draft"}[status], model_id)

    # ----- model files -----
    def _model_status(self, db: sqlite3.Connection, model_id: str) -> str:
        row = db.execute("SELECT status FROM models WHERE id = ?", (model_id,)).fetchone()
        if not row:
            raise StoreError("not_found", f"no model \"{model_id}\"", 404)
        return row["status"]

    def put_file(self, model_id: str, name: str, by: str, *, url: str = "", sha256: str = "", size: int = 0, blob: str = "") -> None:
        check_file_name(name)
        if blob:
            url, sha256 = "", blob
        else:
            check_url(url, "url")
            sha256 = check_sha(sha256)
        with self._tx() as db:
            status = self._model_status(db, model_id)
            if blob and not db.execute("SELECT 1 FROM blobs WHERE sha256 = ?", (blob,)).fetchone():
                raise StoreError("not_found", "no such upload", 404)
            pos = db.execute("SELECT COALESCE(MAX(position), -1) + 1 AS p FROM model_files WHERE model_id = ?", (model_id,)).fetchone()["p"]
            db.execute("""INSERT INTO model_files (model_id, name, position, url, blob, sha256, size) VALUES (?, ?, ?, ?, ?, ?, ?)
                          ON CONFLICT(model_id, name) DO UPDATE SET url = excluded.url, blob = excluded.blob,
                              sha256 = excluded.sha256, size = excluded.size""",
                       (model_id, name, pos, url, blob, sha256, size))
            db.execute("UPDATE models SET updated_at = ? WHERE id = ?", (now_iso(), model_id))
            if status == "published":
                self._touch_published(db)
        self.audit(by, "file.put", f"{model_id}/{name}", ("upload " + blob) if blob else url)

    def delete_file(self, model_id: str, name: str, by: str) -> None:
        with self._tx() as db:
            status = self._model_status(db, model_id)
            if db.execute("DELETE FROM model_files WHERE model_id = ? AND name = ?", (model_id, name)).rowcount == 0:
                raise StoreError("not_found", f"model \"{model_id}\" has no file \"{name}\"", 404)
            if status == "published" and not db.execute("SELECT 1 FROM model_files WHERE model_id = ?", (model_id,)).fetchone():
                raise StoreError("in_use", "a published model keeps at least one file; withdraw it first", 409)
            if status == "published":
                self._touch_published(db)
        self.audit(by, "file.delete", f"{model_id}/{name}")

    # ----- blobs -----
    def add_blob(self, sha256: str, size: int, by: str, original_name: str) -> None:
        self._db.execute("INSERT OR IGNORE INTO blobs (sha256, size, uploaded_at, uploaded_by, original_name) VALUES (?, ?, ?, ?, ?)",
                         (sha256, size, now_iso(), by, original_name[:200]))

    def blob_size(self, sha256: str) -> int | None:
        row = self._db.execute("SELECT size FROM blobs WHERE sha256 = ?", (sha256,)).fetchone()
        return row["size"] if row else None

    def blob_served(self, sha256: str, name: str) -> bool:
        """Whether a published model offers this upload under this name."""
        row = self._db.execute("""SELECT 1 FROM model_files f JOIN models m ON m.id = f.model_id
                                  WHERE f.blob = ? AND f.name = ? AND m.status = 'published'""", (sha256, name)).fetchone()
        return row is not None

    def list_blobs(self) -> list[dict]:
        rows = self._db.execute("""SELECT b.*, (SELECT COUNT(*) FROM model_files f WHERE f.blob = b.sha256) AS used
                                   FROM blobs b ORDER BY uploaded_at""").fetchall()
        return [dict(r) for r in rows]

    def unused_blobs(self) -> list[str]:
        rows = self._db.execute("SELECT sha256 FROM blobs WHERE sha256 NOT IN (SELECT blob FROM model_files WHERE blob != '')").fetchall()
        return [r["sha256"] for r in rows]

    def forget_blobs(self, shas: Iterable[str], by: str) -> None:
        shas = list(shas)
        with self._tx() as db:
            db.executemany("DELETE FROM blobs WHERE sha256 = ?", [(s,) for s in shas])
        if shas:
            self.audit(by, "blob.prune", "", f"{len(shas)} files")

    # ----- topics -----
    def _topic(self, row: sqlite3.Row, published_only: bool) -> dict:
        query = """SELECT t.model_id, t.is_default FROM topic_models t JOIN models m ON m.id = t.model_id
                   WHERE t.topic_id = ?""" + (" AND m.status = 'published'" if published_only else "") + " ORDER BY t.rank, t.model_id"
        models = []
        for r in self._db.execute(query, (row["id"],)).fetchall():
            entry: dict[str, Any] = {"id": r["model_id"]}
            if r["is_default"]:
                entry["default"] = True
            models.append(entry)
        # A topic whose default was withdrawn names its first remaining model.
        if published_only and models and not any(m.get("default") for m in models):
            models[0]["default"] = True
        out = {"id": row["id"], "name": row["name"], "capability": row["capability"], "models": models}
        if row["description"]:
            out["description"] = row["description"]
        if not published_only:
            out["position"] = row["position"]
        return out

    def list_topics(self, published_only: bool = False) -> list[dict]:
        rows = self._db.execute("SELECT * FROM topics ORDER BY position, id").fetchall()
        return [self._topic(r, published_only) for r in rows]

    def put_topic(self, topic_id: str, body: dict, by: str, *, audit: bool = True) -> dict:
        check_id(topic_id, "topic id")
        name = body.get("name") or topic_id
        if not isinstance(name, str) or len(name) > 100:
            raise StoreError("invalid_request", "\"name\" is text of up to 100 characters")
        capability = body.get("capability")
        if capability not in CAPABILITIES:
            raise StoreError("invalid_request", f"\"capability\" is one of {', '.join(CAPABILITIES)}")
        description = body.get("description") or ""
        if not isinstance(description, str) or len(description) > 1000:
            raise StoreError("invalid_request", "\"description\" is text of up to 1000 characters")
        position = body.get("position")
        models = body.get("models") or []
        if not isinstance(models, list):
            raise StoreError("invalid_request", "\"models\" is a list")
        rows = []
        defaults = 0
        for rank, m in enumerate(models):
            if isinstance(m, str):
                m = {"id": m}
            if not isinstance(m, dict) or not isinstance(m.get("id"), str):
                raise StoreError("invalid_request", "a topic's model is {\"id\", \"default\"?}")
            defaults += 1 if m.get("default") else 0
            rows.append((topic_id, m["id"], rank, 1 if m.get("default") else 0))
        if defaults > 1:
            raise StoreError("invalid_request", "a topic has one default model")
        if rows and defaults == 0:
            rows[0] = rows[0][:3] + (1,)
        with self._tx() as db:
            for _, model_id, _, _ in rows:
                m = db.execute("SELECT capability FROM models WHERE id = ?", (model_id,)).fetchone()
                if not m:
                    raise StoreError("not_found", f"no model \"{model_id}\"", 404)
                if m["capability"] != capability:
                    raise StoreError("invalid_request", f"model \"{model_id}\" is {m['capability']}, the topic {capability}")
            if position is None:
                existing = db.execute("SELECT position FROM topics WHERE id = ?", (topic_id,)).fetchone()
                position = existing["position"] if existing else db.execute(
                    "SELECT COALESCE(MAX(position), -1) + 1 AS p FROM topics").fetchone()["p"]
            db.execute("""INSERT INTO topics (id, name, description, capability, position, updated_at) VALUES (?, ?, ?, ?, ?, ?)
                          ON CONFLICT(id) DO UPDATE SET name = excluded.name, description = excluded.description,
                              capability = excluded.capability, position = excluded.position, updated_at = excluded.updated_at""",
                       (topic_id, name, description, capability, _uint(position, "position"), now_iso()))
            db.execute("DELETE FROM topic_models WHERE topic_id = ?", (topic_id,))
            db.executemany("INSERT INTO topic_models (topic_id, model_id, rank, is_default) VALUES (?, ?, ?, ?)", rows)
            self._touch_published(db)
        if audit:
            self.audit(by, "topic.put", topic_id, ", ".join(r[1] + ("*" if r[3] else "") for r in rows))
        return self._topic(self._db.execute("SELECT * FROM topics WHERE id = ?", (topic_id,)).fetchone(), False)

    def delete_topic(self, topic_id: str, by: str) -> None:
        with self._tx() as db:
            if db.execute("DELETE FROM topics WHERE id = ?", (topic_id,)).rowcount == 0:
                raise StoreError("not_found", f"no topic \"{topic_id}\"", 404)
            self._touch_published(db)
        self.audit(by, "topic.delete", topic_id)

    # ----- what a client reads -----
    def published(self) -> str:
        return self.meta("published") or today()

    def gallery_index(self) -> dict:
        return {"formatVersion": 1, "published": self.published(), "engines": self.list_engines()}

    def catalogue(self, base_url: str) -> dict:
        rows = self._db.execute("SELECT * FROM models WHERE status = 'published' ORDER BY capability, id").fetchall()
        return {"formatVersion": 1, "published": self.published(),
                "models": [self._model(r, base_url, public=True) for r in rows]}

    def topics_document(self) -> dict:
        return {"formatVersion": 1, "published": self.published(), "topics": self.list_topics(published_only=True)}

    # ----- import -----
    def import_documents(self, by: str, index: dict | None = None, catalogue: dict | None = None,
                         topics: dict | None = None, publish: bool = False) -> dict:
        """Load a gallery index, a catalogue and a topics document (the
        formats a client reads). Engines and topics are replaced by id;
        models are created or changed, published when `publish`."""
        counts = {"engines": 0, "models": 0, "topics": 0}
        for doc, key in ((index, "engines"), (catalogue, "models"), (topics, "topics")):
            if doc is None:
                continue
            if not isinstance(doc, dict) or doc.get("formatVersion") != 1 or not isinstance(doc.get(key), list):
                raise StoreError("invalid_request", f"the {key} document is not formatVersion 1 with a \"{key}\" list")
        for e in (index or {}).get("engines", []):
            self.put_engine(e.get("id"), e, by, audit=False)
            counts["engines"] += 1
        for m in (catalogue or {}).get("models", []):
            self.put_model(m.get("id"), m, by, status="published" if publish else None, audit=False)
            counts["models"] += 1
        for t in (topics or {}).get("topics", []):
            self.put_topic(t.get("id"), t, by, audit=False)
            counts["topics"] += 1
        self.audit(by, "import", "", json.dumps(counts))
        return counts
