"""The account service's records, on the service's database (ultrastore_db:
MySQL or PostgreSQL in production, SQLite for tests and development only).

The users with what the store needs to know of them, the codes sent by
mail or text message, the phone number (verified by a texted code, for a
paid plan only), the subscription (the plan the account is on, which the
other ULTRA OS services - UltraAccount, cloud storage, the paid AI services -
read), the recovery keys an application escrows here (UltraAuthenticator's
half of the key that opens a vault whose master password was forgotten), the sessions - one per machine signed in, the device the design shows -
the sign-in history, the licences attached to an account and the machines
each is installed on, the notices sent to a user, the data archives, and the
audit log of every operator action - append-only, hash-chained, never
edited, the same chain the marketplace service keeps.

What is never here: the password (its scrypt hash only), a payment method
(the payment service's), the listening and watch history (the machine's,
unless the user turns it on later), a merchant's records.

Version: 0.2.0 - the phone, the subscription, the recovery-key escrow
Version: 0.1.0
"""

from __future__ import annotations

import hashlib
import json
import secrets
import time
from dataclasses import dataclass, field
from typing import Any

from ultrastore_db import Database

from .auth import hash_code, hash_recovery, hash_token

SCHEMA = 2

USER_STATES = ("active", "suspended", "deleting", "deleted")
CODE_PURPOSES = ("verify_email", "change_email", "reset_password", "verify_phone", "recover_escrow")
NOTICE_KINDS = ("notice", "security", "warning", "suspension", "information_request")

DEFAULT_SETTINGS: dict[str, Any] = {
    "notifications": {"security": "email", "orders": "email_and_devices", "updates": "devices", "follows": "devices",
                      "replies": "devices", "deals": "monthly_email"},
    "history_on_account": False,
    "public_profile": True,
    "marketing": False,
}


# The plans an account can be on, and what each allows. "free" is every
# account's; "plus" is paid - a payment-service licence for the
# subscription app (Settings.subscription_app) or the operator's grant. A text
# message costs money, so a phone number and every code sent to it need a
# paid plan.
PLANS: dict[str, dict[str, Any]] = {
    "free": {"paid": False, "features": {"sms": False}},
    "plus": {"paid": True, "features": {"sms": True}},
}
SUBSCRIPTION_SOURCES = ("licence", "operator")
MAX_ESCROWS = 20


def now_iso() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def iso_after(seconds: int) -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(time.time() + seconds))


def today() -> str:
    return now_iso()[:10]


def add_days(day: str, days: int) -> str:
    import datetime as dt
    return (dt.date.fromisoformat(day[:10]) + dt.timedelta(days=days)).isoformat()


def canonical(value: Any) -> str:
    return json.dumps(value, separators=(",", ":"), sort_keys=True, ensure_ascii=False)


def _json(text: str, default: Any) -> Any:
    try:
        return json.loads(text) if text else default
    except ValueError:
        return default


def mask_email(email: str) -> str:
    """an***@example.org - what the operator's table shows."""
    name, _, domain = email.partition("@")
    return (name[:2] + "***@" + domain) if domain else email


def mask_phone(phone: str) -> str:
    """+49 *** 67 - where a code is going, without the number."""
    return (phone[:3] + " *** " + phone[-2:]) if len(phone) > 6 else "***"


@dataclass
class User:
    id: str
    email: str
    email_verified: bool
    password_hash: str
    display_name: str
    handle: str
    language: str
    country: str
    year_of_birth: int
    about: str
    state: str
    created_at: str
    password_changed_at: str
    totp_secret: str
    totp_enabled_at: str
    recovery_codes: list[str]
    settings: dict
    deletion_requested_at: str
    delete_after: str
    suspended_reason: str
    suspended_until: str
    phone: str = ""
    phone_verified_at: str = ""

    @property
    def two_factor(self) -> bool:
        return bool(self.totp_enabled_at)

    def me(self) -> dict:
        """What the user sees of themselves."""
        return {"id": self.id, "email": self.email, "email_verified": self.email_verified, "display_name": self.display_name, "handle": self.handle,
                "language": self.language, "country": self.country, "year_of_birth": self.year_of_birth, "about": self.about, "state": self.state,
                "created_at": self.created_at, "password_changed_at": self.password_changed_at, "two_factor": self.two_factor,
                "recovery_codes_left": len(self.recovery_codes), "settings": self.settings,
                "deletion_requested_at": self.deletion_requested_at, "delete_after": self.delete_after,
                "phone": self.phone, "phone_verified": bool(self.phone and self.phone_verified_at)}

    def file(self) -> dict:
        """What the operator sees: no password, no year of birth, the email masked."""
        return {"id": self.id, "email": mask_email(self.email), "email_verified": self.email_verified, "display_name": self.display_name,
                "handle": self.handle, "language": self.language, "country": self.country, "state": self.state, "created_at": self.created_at,
                "two_factor": self.two_factor, "deletion_requested_at": self.deletion_requested_at, "delete_after": self.delete_after,
                "suspended_reason": self.suspended_reason, "suspended_until": self.suspended_until,
                "phone": mask_phone(self.phone) if self.phone else ""}


@dataclass
class Session:
    id: str
    user_id: str
    device_name: str
    os: str
    client: str
    created_at: str
    last_seen: str
    last_ip: str
    signed_out_at: str

    def public(self, current: str = "") -> dict:
        return {"id": self.id, "device_name": self.device_name, "os": self.os, "client": self.client, "created_at": self.created_at,
                "last_seen": self.last_seen, "last_ip": self.last_ip, "signed_out_at": self.signed_out_at, "current": self.id == current}


@dataclass
class Licence:
    id: str
    user_id: str
    app: str
    order_id: str
    token: str
    issued: str
    updates: str
    updates_until: str
    valid_until: str
    seats: int
    attached_at: str
    released_at: str
    installs: list[dict] = field(default_factory=list)

    def public(self, *, with_token: bool = True) -> dict:
        out = {"id": self.id, "app": self.app, "order": self.order_id, "issued": self.issued, "updates": self.updates, "updates_until": self.updates_until,
               "valid_until": self.valid_until, "seats": self.seats, "attached_at": self.attached_at, "released_at": self.released_at,
               "installs": self.installs, "seats_used": len([i for i in self.installs if not i["removed_at"]])}
        if with_token:
            out["licence"] = self.token
        return out


@dataclass
class Notice:
    id: str
    user_id: str
    kind: str
    subject: str
    text: str
    created_at: str
    created_by: str
    read_at: str

    def public(self) -> dict:
        return {"id": self.id, "kind": self.kind, "subject": self.subject, "text": self.text, "created_at": self.created_at,
                "created_by": self.created_by, "read_at": self.read_at}


@dataclass
class Archive:
    id: str
    user_id: str
    requested_at: str
    built_at: str
    released_at: str
    released_by: str
    downloaded_at: str

    def public(self) -> dict:
        state = "downloaded" if self.downloaded_at else "released" if self.released_at else "built" if self.built_at else "requested"
        return {"id": self.id, "requested_at": self.requested_at, "built_at": self.built_at, "released_at": self.released_at,
                "released_by": self.released_by, "downloaded_at": self.downloaded_at, "state": state, "due": add_days(self.requested_at, 1)}


@dataclass
class Subscription:
    id: str
    user_id: str
    plan: str
    source: str
    licence_id: str
    started_at: str
    valid_until: str
    cancelled_at: str
    created_by: str

    def active(self, day: str) -> bool:
        return not self.cancelled_at and (not self.valid_until or self.valid_until[:10] >= day)

    def public(self) -> dict:
        return {"id": self.id, "plan": self.plan, "source": self.source, "licence": self.licence_id, "started_at": self.started_at,
                "valid_until": self.valid_until, "cancelled_at": self.cancelled_at}


@dataclass
class Escrow:
    """A recovery key an application left with the account: the service's
    half of what opens, say, an UltraAuthenticator vault whose master
    password is forgotten. The other half never leaves the vault file, so
    this one alone opens nothing; it is released only to whoever proves the
    account's verified email or phone with a fresh code. Kept encrypted with
    the service's escrow key (`sealed`, auth.seal_escrow)."""
    id: str
    user_id: str
    app: str
    label: str
    sealed: str
    created_at: str
    released_at: str
    releases: int

    def public(self) -> dict:
        return {"id": self.id, "app": self.app, "label": self.label, "created_at": self.created_at, "released_at": self.released_at,
                "releases": self.releases}


class AccountStore:
    def __init__(self, url: str) -> None:
        db = self._db = Database(url)
        self._key = db.ident("key")
        tk = db.text_key
        db.script([
            f"CREATE TABLE IF NOT EXISTS meta ({self._key} {tk} PRIMARY KEY, value TEXT NOT NULL)",
            f"CREATE TABLE IF NOT EXISTS users (id {tk} PRIMARY KEY, email {tk} NOT NULL, email_verified INTEGER NOT NULL DEFAULT 0,"
            f" password_hash TEXT NOT NULL DEFAULT '', display_name TEXT NOT NULL DEFAULT '', handle {tk} NOT NULL DEFAULT '',"
            " language TEXT NOT NULL DEFAULT '', country TEXT NOT NULL DEFAULT '', year_of_birth INTEGER NOT NULL DEFAULT 0,"
            " about TEXT NOT NULL DEFAULT '', state TEXT NOT NULL, created_at TEXT NOT NULL, password_changed_at TEXT NOT NULL DEFAULT '',"
            " totp_secret TEXT NOT NULL DEFAULT '', totp_enabled_at TEXT NOT NULL DEFAULT '', recovery_codes TEXT NOT NULL DEFAULT '[]',"
            " settings TEXT NOT NULL DEFAULT '{}', deletion_requested_at TEXT NOT NULL DEFAULT '', delete_after TEXT NOT NULL DEFAULT '',"
            " suspended_reason TEXT NOT NULL DEFAULT '', suspended_until TEXT NOT NULL DEFAULT '')",
            f"CREATE TABLE IF NOT EXISTS codes (id {db.serial} PRIMARY KEY, user_id {tk} NOT NULL, purpose {tk} NOT NULL, code_hash TEXT NOT NULL,"
            " salt TEXT NOT NULL, payload TEXT NOT NULL DEFAULT '', expires_at TEXT NOT NULL, used_at TEXT NOT NULL DEFAULT '',"
            " created_at TEXT NOT NULL, attempts INTEGER NOT NULL DEFAULT 0)",
            f"CREATE TABLE IF NOT EXISTS sessions (seq {db.serial} PRIMARY KEY, id {tk} NOT NULL, user_id {tk} NOT NULL, token_hash {tk} NOT NULL,"
            " device_name TEXT NOT NULL DEFAULT '', os TEXT NOT NULL DEFAULT '', client TEXT NOT NULL DEFAULT '', created_at TEXT NOT NULL,"
            " last_seen TEXT NOT NULL, last_ip TEXT NOT NULL DEFAULT '', signed_out_at TEXT NOT NULL DEFAULT '')",
            f"CREATE TABLE IF NOT EXISTS signins (id {db.serial} PRIMARY KEY, user_id {tk} NOT NULL, at TEXT NOT NULL, ip TEXT NOT NULL DEFAULT '',"
            " device TEXT NOT NULL DEFAULT '', ok INTEGER NOT NULL, reason TEXT NOT NULL DEFAULT '')",
            f"CREATE TABLE IF NOT EXISTS licences (id {tk} PRIMARY KEY, user_id {tk} NOT NULL, app {tk} NOT NULL, order_id TEXT NOT NULL DEFAULT '',"
            " token TEXT NOT NULL, issued TEXT NOT NULL DEFAULT '', updates TEXT NOT NULL DEFAULT '', updates_until TEXT NOT NULL DEFAULT '',"
            " valid_until TEXT NOT NULL DEFAULT '', seats INTEGER NOT NULL DEFAULT 0, attached_at TEXT NOT NULL, released_at TEXT NOT NULL DEFAULT '')",
            f"CREATE TABLE IF NOT EXISTS installs (id {db.serial} PRIMARY KEY, licence_id {tk} NOT NULL, session_id {tk} NOT NULL,"
            " installed_at TEXT NOT NULL, removed_at TEXT NOT NULL DEFAULT '')",
            f"CREATE TABLE IF NOT EXISTS notices (id {tk} PRIMARY KEY, user_id {tk} NOT NULL, kind TEXT NOT NULL, subject TEXT NOT NULL,"
            " text TEXT NOT NULL, created_at TEXT NOT NULL, created_by TEXT NOT NULL DEFAULT '', read_at TEXT NOT NULL DEFAULT '')",
            f"CREATE TABLE IF NOT EXISTS archives (id {tk} PRIMARY KEY, user_id {tk} NOT NULL, requested_at TEXT NOT NULL,"
            " built_at TEXT NOT NULL DEFAULT '', released_at TEXT NOT NULL DEFAULT '', released_by TEXT NOT NULL DEFAULT '',"
            " downloaded_at TEXT NOT NULL DEFAULT '', data TEXT NOT NULL DEFAULT '')",
            f"CREATE TABLE IF NOT EXISTS subscriptions (id {tk} PRIMARY KEY, user_id {tk} NOT NULL, plan TEXT NOT NULL, source TEXT NOT NULL,"
            " licence_id TEXT NOT NULL DEFAULT '', started_at TEXT NOT NULL, valid_until TEXT NOT NULL DEFAULT '', cancelled_at TEXT NOT NULL DEFAULT '',"
            " created_by TEXT NOT NULL DEFAULT '')",
            f"CREATE TABLE IF NOT EXISTS escrows (id {tk} PRIMARY KEY, user_id {tk} NOT NULL, app TEXT NOT NULL, label TEXT NOT NULL DEFAULT '',"
            " sealed TEXT NOT NULL, created_at TEXT NOT NULL, released_at TEXT NOT NULL DEFAULT '', releases INTEGER NOT NULL DEFAULT 0,"
            " deleted_at TEXT NOT NULL DEFAULT '')",
            f"CREATE TABLE IF NOT EXISTS audit (id {db.serial} PRIMARY KEY, at TEXT NOT NULL, actor TEXT NOT NULL, action TEXT NOT NULL,"
            f" target_kind TEXT NOT NULL, target {tk} NOT NULL, clause TEXT NOT NULL DEFAULT '', state_before TEXT NOT NULL DEFAULT '',"
            " state_after TEXT NOT NULL DEFAULT '', acted_on TEXT NOT NULL DEFAULT '', prev_hash TEXT NOT NULL, hash TEXT NOT NULL)",
        ])
        for name, table, cols in (("users_email", "users", "email"), ("users_handle", "users", "handle"), ("codes_user", "codes", "user_id, purpose"),
                                  ("sessions_id", "sessions", "id"), ("sessions_user", "sessions", "user_id"), ("sessions_token", "sessions", "token_hash"), ("signins_user", "signins", "user_id"),
                                  ("licences_user", "licences", "user_id"), ("installs_licence", "installs", "licence_id, session_id"),
                                  ("notices_user", "notices", "user_id"), ("archives_user", "archives", "user_id"),
                                  ("subscriptions_user", "subscriptions", "user_id"), ("escrows_user", "escrows", "user_id")):
            db.create_index(name, table, cols)
        # Schema 2 (0.3.0): the phone, added to a users table schema 1 made.
        have = set(db.columns("users"))
        for column in ("phone", "phone_verified_at"):
            if column not in have:
                db.execute(f"ALTER TABLE users ADD COLUMN {column} TEXT NOT NULL DEFAULT ''")
        if self.meta("schema") != str(SCHEMA):
            self.set_meta("schema", str(SCHEMA))

    @property
    def engine(self) -> str:
        return self._db.engine

    def close(self) -> None:
        self._db.close()

    # ----- meta -----
    def meta(self, key: str) -> str:
        row = self._db.fetchone(f"SELECT value FROM meta WHERE {self._key} = ?", (key,))
        return row["value"] if row else ""

    def set_meta(self, key: str, value: str) -> None:
        self._db.execute(self._db.upsert("meta", self._key, "value"), (key, value))

    def next_number(self, counter: str) -> int:
        value = int(self.meta(counter) or "0") + 1
        self.set_meta(counter, str(value))
        return value

    # ----- the audit log (the marketplace service's chain) -----
    @staticmethod
    def _audit_hash(prev_hash: str, row: dict) -> str:
        fields = {k: row[k] for k in ("at", "actor", "action", "target_kind", "target", "clause", "acted_on")}
        fields["before"], fields["after"] = row["state_before"], row["state_after"]
        return hashlib.sha256((prev_hash + "\n" + canonical(fields)).encode()).hexdigest()

    def audit(self, actor: str, action: str, target_kind: str, target: str, *, clause: str = "", before: Any = None, after: Any = None,
              acted_on: str = "") -> int:
        last = self._db.fetchone("SELECT hash FROM audit ORDER BY id DESC LIMIT 1")
        prev = last["hash"] if last else ""
        row = {"at": now_iso(), "actor": actor, "action": action, "target_kind": target_kind, "target": target, "clause": clause,
               "state_before": canonical(before) if before is not None else "", "state_after": canonical(after) if after is not None else "",
               "acted_on": acted_on}
        row_hash = self._audit_hash(prev, row)
        self._db.execute("INSERT INTO audit (at, actor, action, target_kind, target, clause, state_before, state_after, acted_on, prev_hash, hash)"
                         " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                         (row["at"], actor, action, target_kind, target, clause, row["state_before"], row["state_after"], acted_on, prev, row_hash))
        return self._db.last_id()

    @staticmethod
    def _audit_row(r: dict) -> dict:
        return {"id": r["id"], "at": r["at"], "actor": r["actor"], "action": r["action"], "target_kind": r["target_kind"], "target": r["target"],
                "clause": r["clause"], "before": _json(r["state_before"], None), "after": _json(r["state_after"], None), "acted_on": r["acted_on"],
                "prev_hash": r["prev_hash"], "hash": r["hash"]}

    def audit_since(self, since: int, limit: int = 500) -> list[dict]:
        return [self._audit_row(r) for r in self._db.fetchall("SELECT * FROM audit WHERE id > ? ORDER BY id LIMIT ?", (since, limit))]

    def audit_of(self, target: str, limit: int = 500) -> list[dict]:
        return [self._audit_row(r) for r in self._db.fetchall("SELECT * FROM audit WHERE target = ? ORDER BY id DESC LIMIT ?", (target, limit))]

    def verify_audit(self) -> dict:
        prev = ""
        count = 0
        for r in self._db.fetchall("SELECT * FROM audit ORDER BY id"):
            if r["prev_hash"] != prev or self._audit_hash(prev, r) != r["hash"]:
                return {"ok": False, "rows": count, "broken_at": r["id"]}
            prev = r["hash"]
            count += 1
        return {"ok": True, "rows": count, "last_hash": prev}

    # ----- users -----
    @staticmethod
    def _user(r: dict) -> User:
        return User(id=r["id"], email=r["email"], email_verified=bool(r["email_verified"]), password_hash=r["password_hash"],
                    display_name=r["display_name"], handle=r["handle"], language=r["language"], country=r["country"],
                    year_of_birth=int(r["year_of_birth"] or 0), about=r["about"], state=r["state"], created_at=r["created_at"],
                    password_changed_at=r["password_changed_at"], totp_secret=r["totp_secret"], totp_enabled_at=r["totp_enabled_at"],
                    recovery_codes=_json(r["recovery_codes"], []), settings={**DEFAULT_SETTINGS, **_json(r["settings"], {})},
                    deletion_requested_at=r["deletion_requested_at"], delete_after=r["delete_after"], suspended_reason=r["suspended_reason"],
                    suspended_until=r["suspended_until"], phone=r.get("phone") or "", phone_verified_at=r.get("phone_verified_at") or "")

    def get_user(self, user_id: str) -> User | None:
        r = self._db.fetchone("SELECT * FROM users WHERE id = ?", (user_id,))
        return self._user(r) if r else None

    def user_by_email(self, email: str) -> User | None:
        r = self._db.fetchone("SELECT * FROM users WHERE email = ? AND state <> 'deleted'", (email.strip().lower(),))
        return self._user(r) if r else None

    def user_by_handle(self, handle: str) -> User | None:
        r = self._db.fetchone("SELECT * FROM users WHERE handle = ? AND state <> 'deleted'", (handle.strip().lower().lstrip("@"),))
        return self._user(r) if r else None

    def add_user(self, *, email: str, password_hash: str, country: str, year_of_birth: int, language: str, marketing: bool) -> User:
        user_id = "u-" + secrets.token_hex(4)
        while self.get_user(user_id):
            user_id = "u-" + secrets.token_hex(4)
        settings = {**DEFAULT_SETTINGS, "marketing": marketing}
        at = now_iso()
        self._db.execute("INSERT INTO users (id, email, email_verified, password_hash, language, country, year_of_birth, state, created_at,"
                         " password_changed_at, settings) VALUES (?, ?, 0, ?, ?, ?, ?, 'active', ?, ?, ?)",
                         (user_id, email.strip().lower(), password_hash, language, country, year_of_birth, at, at, canonical(settings)))
        self.audit(user_id, "signup", "user", user_id, after={"country": country, "language": language})
        return self.get_user(user_id)   # type: ignore[return-value]

    def update_user(self, user_id: str, **fields: Any) -> User:
        allowed = {"email", "email_verified", "password_hash", "display_name", "handle", "language", "country", "year_of_birth", "about", "state",
                   "password_changed_at", "totp_secret", "totp_enabled_at", "recovery_codes", "settings", "deletion_requested_at", "delete_after",
                   "suspended_reason", "suspended_until", "phone", "phone_verified_at"}
        sets, values = [], []
        for key, value in fields.items():
            if key not in allowed:
                raise ValueError(key)
            if key in ("recovery_codes", "settings"):
                value = canonical(value)
            if key == "email_verified":
                value = 1 if value else 0
            sets.append(f"{key} = ?")
            values.append(value)
        if sets:
            self._db.execute(f"UPDATE users SET {', '.join(sets)} WHERE id = ?", (*values, user_id))
        return self.get_user(user_id)   # type: ignore[return-value]

    def users(self, *, state: str = "", query: str = "") -> list[User]:
        rows = self._db.fetchall("SELECT * FROM users WHERE state <> 'deleted' ORDER BY created_at DESC")
        users = [self._user(r) for r in rows]
        if query:
            q = query.strip().lower().lstrip("@")
            users = [u for u in users if q in u.email.lower() or q in u.handle.lower() or q in u.display_name.lower() or q == u.id]
        if state == "requests":
            with_request = {r["user_id"] for r in self._db.fetchall("SELECT user_id FROM archives WHERE released_at = ''")}
            users = [u for u in users if u.id in with_request or u.state == "deleting"]
        elif state and state != "all":
            users = [u for u in users if u.state == state]
        return users

    def stats(self) -> dict:
        day30 = iso_after(-30 * 86400)
        total = self._db.fetchone("SELECT COUNT(*) AS n FROM users WHERE state <> 'deleted'")["n"]   # type: ignore[index]
        new = self._db.fetchone("SELECT COUNT(*) AS n FROM users WHERE state <> 'deleted' AND created_at >= ?", (day30,))["n"]   # type: ignore[index]
        tfa = self._db.fetchone("SELECT COUNT(*) AS n FROM users WHERE state <> 'deleted' AND totp_enabled_at <> ''")["n"]   # type: ignore[index]
        suspended = self._db.fetchone("SELECT COUNT(*) AS n FROM users WHERE state = 'suspended'")["n"]   # type: ignore[index]
        deleting = self._db.fetchone("SELECT COUNT(*) AS n FROM users WHERE state = 'deleting'")["n"]   # type: ignore[index]
        archives = [self._archive(r) for r in self._db.fetchall("SELECT * FROM archives WHERE released_at = ''")]
        due = today()
        return {"accounts": int(total), "new_in_30_days": int(new), "two_factor_percent": round(100 * int(tfa) / int(total)) if total else 0,
                "archive_requests": len(archives), "archive_requests_due": len([a for a in archives if a.public()["due"] <= add_days(due, 1)]),
                "deletion_requests": int(deleting), "suspended": int(suspended)}

    # ----- codes sent by mail -----
    def add_code(self, user_id: str, purpose: str, code: str, *, minutes: int, payload: str = "") -> None:
        self._db.execute("UPDATE codes SET used_at = ? WHERE user_id = ? AND purpose = ? AND used_at = ''", (now_iso() + " superseded", user_id, purpose))
        salt = secrets.token_hex(8)
        self._db.execute("INSERT INTO codes (user_id, purpose, code_hash, salt, payload, expires_at, created_at) VALUES (?, ?, ?, ?, ?, ?, ?)",
                         (user_id, purpose, hash_code(code, salt), salt, payload, iso_after(minutes * 60), now_iso()))

    def take_code(self, user_id: str, purpose: str, code: str) -> dict | None:
        """The code's row when it is the live one for this purpose, marked used;
        None when wrong, expired or tried too often."""
        r = self._db.fetchone("SELECT * FROM codes WHERE user_id = ? AND purpose = ? AND used_at = '' ORDER BY id DESC LIMIT 1", (user_id, purpose))
        if not r or r["expires_at"] < now_iso() or int(r["attempts"]) >= 5:
            return None
        if hash_code(code.strip(), r["salt"]) != r["code_hash"]:
            self._db.execute("UPDATE codes SET attempts = attempts + 1 WHERE id = ?", (r["id"],))
            return None
        self._db.execute("UPDATE codes SET used_at = ? WHERE id = ?", (now_iso(), r["id"]))
        return dict(r)

    def code_sent_recently(self, user_id: str, purpose: str, seconds: int = 60) -> bool:
        r = self._db.fetchone("SELECT created_at FROM codes WHERE user_id = ? AND purpose = ? ORDER BY id DESC LIMIT 1", (user_id, purpose))
        return bool(r) and r["created_at"] >= iso_after(-seconds)

    # ----- sessions (the devices) -----
    @staticmethod
    def _session(r: dict) -> Session:
        return Session(id=r["id"], user_id=r["user_id"], device_name=r["device_name"], os=r["os"], client=r["client"], created_at=r["created_at"],
                       last_seen=r["last_seen"], last_ip=r["last_ip"], signed_out_at=r["signed_out_at"])

    def add_session(self, user_id: str, token: str, *, device_name: str, os: str, client: str, ip: str) -> Session:
        sid = "d-" + secrets.token_hex(6)
        at = now_iso()
        self._db.execute("INSERT INTO sessions (id, user_id, token_hash, device_name, os, client, created_at, last_seen, last_ip)"
                         " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)", (sid, user_id, hash_token(token), device_name, os, client, at, at, ip))
        return self.get_session(sid)   # type: ignore[return-value]

    def get_session(self, sid: str) -> Session | None:
        r = self._db.fetchone("SELECT * FROM sessions WHERE id = ?", (sid,))
        return self._session(r) if r else None

    def session_by_token(self, token: str, *, idle_days: int, ip: str = "") -> Session | None:
        r = self._db.fetchone("SELECT * FROM sessions WHERE token_hash = ?", (hash_token(token),))
        if not r or r["signed_out_at"]:
            return None
        if r["last_seen"] < iso_after(-idle_days * 86400):
            self._db.execute("UPDATE sessions SET signed_out_at = ? WHERE id = ?", (now_iso() + " idle", r["id"]))
            return None
        self._db.execute("UPDATE sessions SET last_seen = ?, last_ip = ? WHERE id = ?", (now_iso(), ip or r["last_ip"], r["id"]))
        return self._session(r)

    def sessions_of(self, user_id: str, *, active_only: bool = True) -> list[Session]:
        rows = self._db.fetchall("SELECT * FROM sessions WHERE user_id = ? ORDER BY last_seen DESC, seq DESC", (user_id,))
        return [self._session(r) for r in rows if not active_only or not r["signed_out_at"]]

    def rename_session(self, sid: str, name: str) -> None:
        self._db.execute("UPDATE sessions SET device_name = ? WHERE id = ?", (name, sid))

    def sign_out(self, sid: str, *, reason: str = "") -> None:
        """The session ended and every licence install on it freed."""
        self._db.execute("UPDATE sessions SET signed_out_at = ? WHERE id = ? AND signed_out_at = ''", ((now_iso() + (" " + reason if reason else "")), sid))
        self._db.execute("UPDATE installs SET removed_at = ? WHERE session_id = ? AND removed_at = ''", (now_iso(), sid))

    def sign_out_all(self, user_id: str, *, except_session: str = "", reason: str = "") -> int:
        count = 0
        for s in self.sessions_of(user_id):
            if s.id != except_session:
                self.sign_out(s.id, reason=reason)
                count += 1
        return count

    # ----- the sign-in history -----
    def record_signin(self, user_id: str, *, ip: str, device: str, ok: bool, reason: str = "") -> None:
        self._db.execute("INSERT INTO signins (user_id, at, ip, device, ok, reason) VALUES (?, ?, ?, ?, ?, ?)", (user_id, now_iso(), ip, device, 1 if ok else 0, reason))

    def signins_of(self, user_id: str, limit: int = 50) -> list[dict]:
        rows = self._db.fetchall("SELECT * FROM signins WHERE user_id = ? ORDER BY id DESC LIMIT ?", (user_id, limit))
        return [{"at": r["at"], "ip": r["ip"], "device": r["device"], "ok": bool(r["ok"]), "reason": r["reason"]} for r in rows]

    def failed_signins_recently(self, user_id: str, minutes: int = 15) -> int:
        r = self._db.fetchone("SELECT COUNT(*) AS n FROM signins WHERE user_id = ? AND ok = 0 AND at >= ?", (user_id, iso_after(-minutes * 60)))
        return int(r["n"]) if r else 0

    # ----- licences -----
    def _licence(self, r: dict) -> Licence:
        installs = [{"id": i["id"], "session": i["session_id"], "installed_at": i["installed_at"], "removed_at": i["removed_at"]}
                    for i in self._db.fetchall("SELECT * FROM installs WHERE licence_id = ? ORDER BY id", (r["id"],))]
        return Licence(id=r["id"], user_id=r["user_id"], app=r["app"], order_id=r["order_id"], token=r["token"], issued=r["issued"], updates=r["updates"],
                       updates_until=r["updates_until"], valid_until=r["valid_until"], seats=int(r["seats"] or 0), attached_at=r["attached_at"],
                       released_at=r["released_at"], installs=installs)

    def get_licence(self, licence_id: str) -> Licence | None:
        r = self._db.fetchone("SELECT * FROM licences WHERE id = ?", (licence_id,))
        return self._licence(r) if r else None

    def licences_of(self, user_id: str) -> list[Licence]:
        return [self._licence(r) for r in self._db.fetchall("SELECT * FROM licences WHERE user_id = ? AND released_at = '' ORDER BY attached_at", (user_id,))]

    def attach_licence(self, user_id: str, *, licence_id: str, app: str, order_id: str, token: str, issued: str, updates: str, updates_until: str,
                       valid_until: str, seats: int) -> Licence:
        """A licence from the payment service bound to this account; a
        renewal of one already here replaces its token."""
        existing = self.get_licence(licence_id)
        if existing and existing.user_id == user_id and not existing.released_at:
            self._db.execute("UPDATE licences SET token = ?, valid_until = ?, updates = ?, updates_until = ? WHERE id = ?",
                             (token, valid_until, updates, updates_until, licence_id))
            return self.get_licence(licence_id)   # type: ignore[return-value]
        if existing:
            raise ValueError("licence_taken")
        self._db.execute("INSERT INTO licences (id, user_id, app, order_id, token, issued, updates, updates_until, valid_until, seats, attached_at)"
                         " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                         (licence_id, user_id, app, order_id, token, issued, updates, updates_until, valid_until, seats, now_iso()))
        self.audit(user_id, "attach_licence", "user", user_id, after={"licence": licence_id, "app": app})
        return self.get_licence(licence_id)   # type: ignore[return-value]

    def release_licence(self, licence_id: str, actor: str) -> None:
        self._db.execute("UPDATE licences SET released_at = ? WHERE id = ? AND released_at = ''", (now_iso(), licence_id))
        self._db.execute("UPDATE installs SET removed_at = ? WHERE licence_id = ? AND removed_at = ''", (now_iso(), licence_id))
        self.audit(actor, "release_licence", "licence", licence_id)

    def install(self, licence: Licence, session_id: str) -> Licence:
        """The licence installed on this machine; a seat taken, or 'no_seat'."""
        active = [i for i in licence.installs if not i["removed_at"]]
        if any(i["session"] == session_id for i in active):
            return licence
        if licence.seats and len(active) >= licence.seats:
            raise ValueError("no_seat")
        self._db.execute("INSERT INTO installs (licence_id, session_id, installed_at) VALUES (?, ?, ?)", (licence.id, session_id, now_iso()))
        return self.get_licence(licence.id)   # type: ignore[return-value]

    def uninstall(self, licence: Licence, session_id: str) -> Licence:
        self._db.execute("UPDATE installs SET removed_at = ? WHERE licence_id = ? AND session_id = ? AND removed_at = ''", (now_iso(), licence.id, session_id))
        return self.get_licence(licence.id)   # type: ignore[return-value]

    # ----- notices -----
    @staticmethod
    def _notice(r: dict) -> Notice:
        return Notice(id=r["id"], user_id=r["user_id"], kind=r["kind"], subject=r["subject"], text=r["text"], created_at=r["created_at"],
                      created_by=r["created_by"], read_at=r["read_at"])

    def add_notice(self, user_id: str, *, kind: str, subject: str, text: str, actor: str) -> Notice:
        nid = f"n-{self.next_number('notice_no'):08d}"
        self._db.execute("INSERT INTO notices (id, user_id, kind, subject, text, created_at, created_by) VALUES (?, ?, ?, ?, ?, ?, ?)",
                         (nid, user_id, kind, subject, text, now_iso(), actor))
        if actor != user_id and actor != "service":
            self.audit(actor, "notify", "user", user_id, after={"notice": nid, "kind": kind, "subject": subject})
        return self._notice(self._db.fetchone("SELECT * FROM notices WHERE id = ?", (nid,)))   # type: ignore[arg-type]

    def notices_of(self, user_id: str) -> list[Notice]:
        return [self._notice(r) for r in self._db.fetchall("SELECT * FROM notices WHERE user_id = ? ORDER BY read_at <> '', id DESC", (user_id,))]

    def mark_read(self, user_id: str, nid: str) -> bool:
        r = self._db.fetchone("SELECT id FROM notices WHERE id = ? AND user_id = ?", (nid, user_id))
        if not r:
            return False
        self._db.execute("UPDATE notices SET read_at = ? WHERE id = ? AND read_at = ''", (now_iso(), nid))
        return True

    # ----- the data archive -----
    @staticmethod
    def _archive(r: dict) -> Archive:
        return Archive(id=r["id"], user_id=r["user_id"], requested_at=r["requested_at"], built_at=r["built_at"], released_at=r["released_at"],
                       released_by=r["released_by"], downloaded_at=r["downloaded_at"])

    def archives_of(self, user_id: str) -> list[Archive]:
        return [self._archive(r) for r in self._db.fetchall("SELECT * FROM archives WHERE user_id = ? ORDER BY id DESC", (user_id,))]

    def open_archive(self, user_id: str) -> Archive | None:
        r = self._db.fetchone("SELECT * FROM archives WHERE user_id = ? AND downloaded_at = '' ORDER BY id DESC LIMIT 1", (user_id,))
        return self._archive(r) if r else None

    def build_archive(self, user: User) -> Archive:
        """Everything the account holds of the person, as one JSON document;
        built at once, released by the operator, downloaded once."""
        data = {"profile": user.me(), "subscriptions": [s.public() for s in self.subscriptions_of(user.id)],
                "recovery_keys": [e.public() for e in self.escrows_of(user.id)],
                "devices": [s.public() for s in self.sessions_of(user.id, active_only=False)],
                "licences": [l.public() for l in self.licences_of(user.id)], "notices": [n.public() for n in self.notices_of(user.id)],
                "sign_ins": self.signins_of(user.id, limit=1000), "history": self.audit_of(user.id)}
        aid = "a-" + secrets.token_hex(6)
        at = now_iso()
        self._db.execute("INSERT INTO archives (id, user_id, requested_at, built_at, data) VALUES (?, ?, ?, ?, ?)", (aid, user.id, at, at, canonical(data)))
        self.audit(user.id, "request_archive", "user", user.id, after={"archive": aid})
        return self._archive(self._db.fetchone("SELECT * FROM archives WHERE id = ?", (aid,)))   # type: ignore[arg-type]

    def release_archive(self, aid: str, actor: str) -> Archive | None:
        r = self._db.fetchone("SELECT * FROM archives WHERE id = ?", (aid,))
        if not r:
            return None
        self._db.execute("UPDATE archives SET released_at = ?, released_by = ? WHERE id = ? AND released_at = ''", (now_iso(), actor, aid))
        self.audit(actor, "release_archive", "user", r["user_id"], acted_on=f"archive:{aid}")
        return self._archive(self._db.fetchone("SELECT * FROM archives WHERE id = ?", (aid,)))   # type: ignore[arg-type]

    def take_archive(self, user_id: str, aid: str) -> str | None:
        """The archive's JSON, once, when released."""
        r = self._db.fetchone("SELECT * FROM archives WHERE id = ? AND user_id = ?", (aid, user_id))
        if not r or not r["released_at"] or r["downloaded_at"]:
            return None
        self._db.execute("UPDATE archives SET downloaded_at = ?, data = '' WHERE id = ?", (now_iso(), aid))
        return r["data"]

    # ----- the subscription -----
    @staticmethod
    def _subscription(r: dict) -> Subscription:
        return Subscription(id=r["id"], user_id=r["user_id"], plan=r["plan"], source=r["source"], licence_id=r["licence_id"], started_at=r["started_at"],
                            valid_until=r["valid_until"], cancelled_at=r["cancelled_at"], created_by=r["created_by"])

    def subscriptions_of(self, user_id: str) -> list[Subscription]:
        return [self._subscription(r) for r in self._db.fetchall("SELECT * FROM subscriptions WHERE user_id = ? ORDER BY started_at DESC, id DESC", (user_id,))]

    def active_subscription(self, user_id: str) -> Subscription | None:
        """The paid plan in force today, the one running longest when there
        are several; None on the free plan."""
        day = today()
        live = [s for s in self.subscriptions_of(user_id) if s.active(day) and PLANS.get(s.plan, {}).get("paid")]
        live.sort(key=lambda s: s.valid_until or "9999", reverse=True)
        return live[0] if live else None

    def plan_of(self, user_id: str) -> dict:
        """What the account is on: the plan, whether it is paid, until when,
        and what it allows - the one answer every service asks."""
        sub = self.active_subscription(user_id)
        name = sub.plan if sub else "free"
        plan = PLANS.get(name, PLANS["free"])
        return {"plan": name, "paid": bool(plan["paid"]), "valid_until": sub.valid_until if sub else "", "source": sub.source if sub else "",
                "features": dict(plan["features"]), "subscription": sub.public() if sub else None}

    def add_subscription(self, user_id: str, *, plan: str, source: str, valid_until: str, actor: str, licence_id: str = "") -> Subscription:
        """A plan started; one from a licence already here is renewed in place."""
        if plan not in PLANS or source not in SUBSCRIPTION_SOURCES:
            raise ValueError("bad_plan")
        if licence_id:
            r = self._db.fetchone("SELECT * FROM subscriptions WHERE licence_id = ? AND user_id = ?", (licence_id, user_id))
            if r:
                self._db.execute("UPDATE subscriptions SET valid_until = ?, cancelled_at = '' WHERE id = ?", (valid_until, r["id"]))
                self.audit(actor, "renew_subscription", "user", user_id, after={"plan": plan, "valid_until": valid_until, "licence": licence_id})
                return self._subscription(self._db.fetchone("SELECT * FROM subscriptions WHERE id = ?", (r["id"],)))   # type: ignore[arg-type]
        sid = "s-" + secrets.token_hex(6)
        self._db.execute("INSERT INTO subscriptions (id, user_id, plan, source, licence_id, started_at, valid_until, created_by) VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                         (sid, user_id, plan, source, licence_id, now_iso(), valid_until, actor))
        self.audit(actor, "start_subscription", "user", user_id, after={"plan": plan, "source": source, "valid_until": valid_until, "licence": licence_id})
        return self._subscription(self._db.fetchone("SELECT * FROM subscriptions WHERE id = ?", (sid,)))   # type: ignore[arg-type]

    def cancel_subscriptions(self, user_id: str, *, actor: str, reason: str = "") -> int:
        live = [s for s in self.subscriptions_of(user_id) if not s.cancelled_at]
        for s in live:
            self._db.execute("UPDATE subscriptions SET cancelled_at = ? WHERE id = ?", (now_iso(), s.id))
        if live:
            self.audit(actor, "cancel_subscription", "user", user_id, after={"count": len(live), "reason": reason})
        return len(live)

    # ----- the recovery-key escrow -----
    @staticmethod
    def _escrow(r: dict) -> Escrow:
        return Escrow(id=r["id"], user_id=r["user_id"], app=r["app"], label=r["label"], sealed=r["sealed"], created_at=r["created_at"],
                      released_at=r["released_at"], releases=int(r["releases"] or 0))

    def escrows_of(self, user_id: str) -> list[Escrow]:
        return [self._escrow(r) for r in self._db.fetchall("SELECT * FROM escrows WHERE user_id = ? AND deleted_at = '' ORDER BY created_at, id", (user_id,))]

    def get_escrow(self, eid: str) -> Escrow | None:
        r = self._db.fetchone("SELECT * FROM escrows WHERE id = ? AND deleted_at = ''", (eid,))
        return self._escrow(r) if r else None

    def new_escrow_id(self) -> str:
        """Unguessable: the id is what a vault file names its escrow by, and
        anyone holding it may ask for a code to be sent to the owner."""
        return "e-" + secrets.token_hex(16)

    def add_escrow(self, user_id: str, *, eid: str, app: str, label: str, sealed: str) -> Escrow:
        if len(self.escrows_of(user_id)) >= MAX_ESCROWS:
            raise ValueError("too_many")
        self._db.execute("INSERT INTO escrows (id, user_id, app, label, sealed, created_at) VALUES (?, ?, ?, ?, ?, ?)",
                         (eid, user_id, app, label, sealed, now_iso()))
        self.audit(user_id, "add_escrow", "user", user_id, after={"escrow": eid, "app": app})
        return self.get_escrow(eid)   # type: ignore[return-value]

    def delete_escrow(self, eid: str, actor: str) -> None:
        r = self._db.fetchone("SELECT user_id FROM escrows WHERE id = ?", (eid,))
        self._db.execute("UPDATE escrows SET deleted_at = ?, sealed = '' WHERE id = ? AND deleted_at = ''", (now_iso(), eid))
        if r:
            self.audit(actor, "delete_escrow", "user", r["user_id"], acted_on=f"escrow:{eid}")

    def mark_escrow_released(self, eid: str, *, channel: str) -> None:
        r = self._db.fetchone("SELECT user_id FROM escrows WHERE id = ?", (eid,))
        self._db.execute("UPDATE escrows SET released_at = ?, releases = releases + 1 WHERE id = ?", (now_iso(), eid))
        if r:
            self.audit(r["user_id"], "release_escrow", "user", r["user_id"], acted_on=f"escrow:{eid}", after={"channel": channel})

    def code_sent_recently_for(self, user_id: str, purpose: str, payload: str, seconds: int = 60) -> bool:
        r = self._db.fetchone("SELECT created_at FROM codes WHERE user_id = ? AND purpose = ? AND payload = ? ORDER BY id DESC LIMIT 1",
                              (user_id, purpose, payload))
        return bool(r) and r["created_at"] >= iso_after(-seconds)

    def codes_sent_since(self, user_id: str, purpose: str, seconds: int) -> int:
        r = self._db.fetchone("SELECT COUNT(*) AS n FROM codes WHERE user_id = ? AND purpose = ? AND created_at >= ?", (user_id, purpose, iso_after(-seconds)))
        return int(r["n"]) if r else 0

    # ----- suspension and deletion -----
    def suspend_user(self, user_id: str, *, reason: str, days: int, actor: str) -> User:
        before = self.get_user(user_id)
        until = add_days(today(), days) if days > 0 else ""
        self.update_user(user_id, state="suspended", suspended_reason=reason, suspended_until=until)
        self.sign_out_all(user_id, reason="suspended")
        self.audit(actor, "suspend", "user", user_id, before={"state": before.state if before else ""}, after={"reason": reason, "until": until})
        return self.get_user(user_id)   # type: ignore[return-value]

    def reinstate_user(self, user_id: str, *, reason: str, actor: str) -> User:
        before = self.get_user(user_id)
        self.update_user(user_id, state="active", suspended_reason="", suspended_until="")
        self.audit(actor, "reinstate", "user", user_id, before={"state": before.state if before else ""}, after={"reason": reason})
        return self.get_user(user_id)   # type: ignore[return-value]

    def request_deletion(self, user_id: str, *, grace_days: int) -> User:
        after = add_days(today(), grace_days)
        self.update_user(user_id, state="deleting", deletion_requested_at=now_iso(), delete_after=after)
        self.audit(user_id, "request_deletion", "user", user_id, after={"delete_after": after})
        return self.get_user(user_id)   # type: ignore[return-value]

    def cancel_deletion(self, user_id: str) -> User:
        self.update_user(user_id, state="active", deletion_requested_at="", delete_after="")
        self.audit(user_id, "cancel_deletion", "user", user_id)
        return self.get_user(user_id)   # type: ignore[return-value]

    def delete_user(self, user_id: str, *, actor: str, reason: str = "") -> None:
        """Carried out: the person removed, the audit rows kept under the id."""
        user = self.get_user(user_id)
        if not user or user.state == "deleted":
            return
        for s in self.sessions_of(user_id):
            self.sign_out(s.id, reason="deleted")
        for lic in self.licences_of(user_id):
            self.release_licence(lic.id, actor)
        self._db.execute("DELETE FROM codes WHERE user_id = ?", (user_id,))
        self._db.execute("DELETE FROM notices WHERE user_id = ?", (user_id,))
        self._db.execute("DELETE FROM archives WHERE user_id = ?", (user_id,))
        self._db.execute("DELETE FROM signins WHERE user_id = ?", (user_id,))
        self._db.execute("DELETE FROM sessions WHERE user_id = ?", (user_id,))
        self._db.execute("DELETE FROM escrows WHERE user_id = ?", (user_id,))
        self.cancel_subscriptions(user_id, actor=actor, reason="account deleted")
        self.update_user(user_id, email=f"deleted-{user_id}", email_verified=False, password_hash="", display_name="", handle="", about="",
                         year_of_birth=0, totp_secret="", totp_enabled_at="", recovery_codes=[], settings={}, state="deleted",
                         phone="", phone_verified_at="")
        self.audit(actor, "delete", "user", user_id, before={"state": user.state}, after={"reason": reason})

    def due_deletions(self) -> list[User]:
        day = today()
        return [u for u in self.users(state="deleting") if u.delete_after and u.delete_after <= day]

    def expired_suspensions(self) -> list[User]:
        day = today()
        return [u for u in self.users(state="suspended") if u.suspended_until and u.suspended_until < day]
