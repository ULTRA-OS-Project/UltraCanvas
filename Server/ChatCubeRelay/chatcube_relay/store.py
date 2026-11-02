"""SQLite storage: users, sessions, contacts, groups and the offline queue.

One connection, one lock: the relay is a small service and SQLite answers
these queries in microseconds. Tokens are stored as their SHA-256 only, so
a copy of the database opens no session. Envelopes are stored exactly as
the client sent them; nothing here can read them."""

from __future__ import annotations

import hashlib
import hmac
import sqlite3
import threading
import time
import uuid
from dataclasses import dataclass

SCHEMA = """
CREATE TABLE IF NOT EXISTS users (
    user_id      TEXT PRIMARY KEY,
    public_key   BLOB NOT NULL,
    display_name TEXT NOT NULL,
    created_at   INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS sessions (
    token_hash TEXT PRIMARY KEY,
    user_id    TEXT NOT NULL REFERENCES users(user_id),
    expires_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS contacts (
    owner      TEXT NOT NULL,
    contact    TEXT NOT NULL,
    created_at INTEGER NOT NULL,
    PRIMARY KEY (owner, contact)
);
CREATE INDEX IF NOT EXISTS contacts_by_contact ON contacts(contact);
CREATE TABLE IF NOT EXISTS groups (
    group_id   TEXT PRIMARY KEY,
    title      TEXT NOT NULL,
    owner      TEXT NOT NULL,
    created_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS group_members (
    group_id TEXT NOT NULL REFERENCES groups(group_id),
    user_id  TEXT NOT NULL,
    PRIMARY KEY (group_id, user_id)
);
CREATE INDEX IF NOT EXISTS members_by_user ON group_members(user_id);
CREATE TABLE IF NOT EXISTS messages (
    id           INTEGER PRIMARY KEY AUTOINCREMENT,
    recipient    TEXT NOT NULL,
    sender       TEXT NOT NULL,
    message_id   TEXT NOT NULL,
    conversation TEXT NOT NULL,
    envelope     TEXT NOT NULL,
    created_at   INTEGER NOT NULL,
    UNIQUE (recipient, message_id)
);
CREATE INDEX IF NOT EXISTS messages_by_recipient ON messages(recipient, id);
-- What a user gave to recover the account with: theirs alone, never part of
-- what anyone else is told about them (user_json). Verified once a code sent
-- to it came back (the columns are added by _migrate on a 1.1.0 database).
CREATE TABLE IF NOT EXISTS recovery (
    user_id    TEXT PRIMARY KEY REFERENCES users(user_id),
    email      TEXT NOT NULL DEFAULT '',
    phone      TEXT NOT NULL DEFAULT '',
    updated_at INTEGER NOT NULL
);
-- A code waiting to come back: confirming an e-mail address or a phone.
CREATE TABLE IF NOT EXISTS codes (
    key        TEXT PRIMARY KEY,              -- "verify:<user>:<channel>"
    digest     TEXT NOT NULL,                 -- SHA-256 of key and code
    expires_at INTEGER NOT NULL,
    attempts   INTEGER NOT NULL DEFAULT 0
);
-- The recovery copy of a user's key, encrypted with the relay's recovery key
-- (AES-256-GCM, the user id as associated data). Present only while the
-- user has recovery turned on.
CREATE TABLE IF NOT EXISTS escrow (
    user_id    TEXT PRIMARY KEY REFERENCES users(user_id),
    nonce      BLOB NOT NULL,
    sealed     BLOB NOT NULL,
    updated_at INTEGER NOT NULL
);
-- A recovery under way: the codes sent to both, until they come back. One
-- for an address nobody has too (user_id NULL), so the answer never tells.
CREATE TABLE IF NOT EXISTS recoveries (
    recovery_id  TEXT PRIMARY KEY,
    user_id      TEXT,
    email_digest TEXT NOT NULL,
    phone_digest TEXT NOT NULL,
    expires_at   INTEGER NOT NULL,
    attempts     INTEGER NOT NULL DEFAULT 0
);
-- When codes went where, for the hourly limit.
CREATE TABLE IF NOT EXISTS sends (
    key TEXT NOT NULL,
    at  INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS sends_by_key ON sends(key, at);
"""


def code_digest(key: str, code: str) -> str:
    return hashlib.sha256((key + "\n" + code).encode()).hexdigest()


def normalize_email(address: str) -> str:
    return address.strip().lower()


def normalize_phone(number: str) -> str:
    """"+49 (151) 2345-6789" -> "+4915123456789": what is compared and dialled."""
    digits = "".join(c for c in number if c.isdigit())
    return "+" + digits if digits else ""


@dataclass
class Contacts:
    email: str = ""
    phone: str = ""
    email_verified: bool = False
    phone_verified: bool = False

GROUP_PREFIX = "group:"


def hash_token(token: str) -> str:
    return hashlib.sha256(token.encode()).hexdigest()


@dataclass
class User:
    user_id: str
    public_key: bytes
    display_name: str


@dataclass
class Group:
    group_id: str
    title: str
    owner: str
    members: list[str]


class Store:
    def __init__(self, path: str) -> None:
        self._db = sqlite3.connect(path, check_same_thread=False, isolation_level=None)
        self._db.row_factory = sqlite3.Row
        self._lock = threading.Lock()
        with self._lock:
            self._db.execute("PRAGMA journal_mode=WAL")
            self._db.execute("PRAGMA foreign_keys=ON")
            self._db.executescript(SCHEMA)
            self._migrate()

    def _migrate(self) -> None:
        columns = {row["name"] for row in self._db.execute("PRAGMA table_info(recovery)")}
        for column in ("email_verified", "phone_verified"):
            if column not in columns:
                self._db.execute(f"ALTER TABLE recovery ADD COLUMN {column} INTEGER NOT NULL DEFAULT 0")

    def close(self) -> None:
        with self._lock:
            self._db.close()

    # ---- users ---------------------------------------------------------------

    def add_user(self, user_id: str, public_key: bytes, display_name: str) -> bool:
        with self._lock:
            try:
                self._db.execute(
                    "INSERT INTO users(user_id, public_key, display_name, created_at) VALUES (?,?,?,?)",
                    (user_id, public_key, display_name, int(time.time())))
                return True
            except sqlite3.IntegrityError:
                return False

    def get_user(self, user_id: str) -> User | None:
        with self._lock:
            row = self._db.execute(
                "SELECT user_id, public_key, display_name FROM users WHERE user_id=?", (user_id,)).fetchone()
        return User(row["user_id"], bytes(row["public_key"]), row["display_name"]) if row else None

    def set_display_name(self, user_id: str, display_name: str) -> None:
        with self._lock:
            self._db.execute("UPDATE users SET display_name=? WHERE user_id=?", (display_name, user_id))

    # ---- the owner's e-mail and phone, and their verification -------------------

    def get_contacts(self, user_id: str) -> Contacts:
        with self._lock:
            row = self._db.execute(
                "SELECT email, phone, email_verified, phone_verified FROM recovery WHERE user_id=?",
                (user_id,)).fetchone()
        if not row:
            return Contacts()
        return Contacts(row["email"], row["phone"], bool(row["email_verified"]), bool(row["phone_verified"]))

    def set_contacts(self, user_id: str, email: str | None, phone: str | None) -> list[str]:
        """None leaves a field as it is; "" removes it. A field that changes
        is no longer verified. Answers the channels that changed to a value."""
        current = self.get_contacts(user_id)
        changed: list[str] = []
        new_email, email_ok = current.email, current.email_verified
        new_phone, phone_ok = current.phone, current.phone_verified
        if email is not None and email != current.email:
            new_email, email_ok = email, False
            if email:
                changed.append("email")
        if phone is not None and phone != current.phone:
            new_phone, phone_ok = phone, False
            if phone:
                changed.append("phone")
        with self._lock:
            self._db.execute(
                "INSERT INTO recovery(user_id, email, phone, email_verified, phone_verified, updated_at) "
                "VALUES (?,?,?,?,?,?) ON CONFLICT(user_id) DO UPDATE SET email=excluded.email, "
                "phone=excluded.phone, email_verified=excluded.email_verified, "
                "phone_verified=excluded.phone_verified, updated_at=excluded.updated_at",
                (user_id, new_email, new_phone, int(email_ok), int(phone_ok), int(time.time())))
            for channel in ("email", "phone"):
                if channel not in changed and (email if channel == "email" else phone) == "":
                    self._db.execute("DELETE FROM codes WHERE key=?", (f"verify:{user_id}:{channel}",))
        return changed

    def mark_verified(self, user_id: str, channel: str) -> None:
        column = "email_verified" if channel == "email" else "phone_verified"
        with self._lock:
            self._db.execute(f"UPDATE recovery SET {column}=1 WHERE user_id=?", (user_id,))

    def find_verified(self, address: str) -> str | None:
        """The user whose verified e-mail address or phone number this is."""
        with self._lock:
            if "@" in address:
                row = self._db.execute("SELECT user_id FROM recovery WHERE email_verified=1 AND lower(email)=?",
                                       (normalize_email(address),)).fetchone()
            else:
                wanted = normalize_phone(address)
                rows = self._db.execute("SELECT user_id, phone FROM recovery WHERE phone_verified=1").fetchall()
                row = next((r for r in rows if wanted and normalize_phone(r["phone"]) == wanted), None)
        return row["user_id"] if row else None

    # ---- codes --------------------------------------------------------------------

    def put_code(self, key: str, code: str, ttl: int) -> None:
        with self._lock:
            self._db.execute(
                "INSERT INTO codes(key, digest, expires_at, attempts) VALUES (?,?,?,0) "
                "ON CONFLICT(key) DO UPDATE SET digest=excluded.digest, expires_at=excluded.expires_at, attempts=0",
                (key, code_digest(key, code), int(time.time()) + ttl))

    def take_code(self, key: str, code: str, max_attempts: int) -> bool:
        """True (and the code gone) when `code` is the one waiting under
        `key`; a wrong one counts, and the last allowed attempt spends it."""
        with self._lock:
            row = self._db.execute("SELECT digest, expires_at, attempts FROM codes WHERE key=?", (key,)).fetchone()
            if not row or row["expires_at"] < time.time():
                return False
            if hmac.compare_digest(row["digest"], code_digest(key, code)):
                self._db.execute("DELETE FROM codes WHERE key=?", (key,))
                return True
            if row["attempts"] + 1 >= max_attempts:
                self._db.execute("DELETE FROM codes WHERE key=?", (key,))
            else:
                self._db.execute("UPDATE codes SET attempts=attempts+1 WHERE key=?", (key,))
            return False

    def count_sends(self, key: str, within: int) -> int:
        with self._lock:
            self._db.execute("DELETE FROM sends WHERE at<?", (int(time.time()) - 24 * 3600,))
            row = self._db.execute("SELECT COUNT(*) AS n FROM sends WHERE key=? AND at>=?",
                                   (key, int(time.time()) - within)).fetchone()
        return int(row["n"])

    def note_send(self, key: str) -> None:
        with self._lock:
            self._db.execute("INSERT INTO sends(key, at) VALUES (?,?)", (key, int(time.time())))

    # ---- the recovery copy of a key, and recoveries under way ------------------------

    def put_escrow(self, user_id: str, nonce: bytes, sealed: bytes) -> None:
        with self._lock:
            self._db.execute(
                "INSERT INTO escrow(user_id, nonce, sealed, updated_at) VALUES (?,?,?,?) "
                "ON CONFLICT(user_id) DO UPDATE SET nonce=excluded.nonce, sealed=excluded.sealed, "
                "updated_at=excluded.updated_at", (user_id, nonce, sealed, int(time.time())))

    def get_escrow(self, user_id: str) -> tuple[bytes, bytes] | None:
        with self._lock:
            row = self._db.execute("SELECT nonce, sealed FROM escrow WHERE user_id=?", (user_id,)).fetchone()
        return (bytes(row["nonce"]), bytes(row["sealed"])) if row else None

    def remove_escrow(self, user_id: str) -> None:
        with self._lock:
            self._db.execute("DELETE FROM escrow WHERE user_id=?", (user_id,))

    def start_recovery(self, recovery_id: str, user_id: str | None, email_code: str, phone_code: str,
                       ttl: int) -> None:
        with self._lock:
            self._db.execute("DELETE FROM recoveries WHERE expires_at<?", (int(time.time()),))
            self._db.execute(
                "INSERT INTO recoveries(recovery_id, user_id, email_digest, phone_digest, expires_at) "
                "VALUES (?,?,?,?,?)",
                (recovery_id, user_id, code_digest(recovery_id + ":email", email_code),
                 code_digest(recovery_id + ":phone", phone_code), int(time.time()) + ttl))

    def finish_recovery(self, recovery_id: str, email_code: str, phone_code: str,
                        max_attempts: int) -> str | None:
        """The user id when both codes are right (and the recovery gone); None
        otherwise - also for an address nobody has. A wrong pair counts."""
        with self._lock:
            row = self._db.execute(
                "SELECT user_id, email_digest, phone_digest, expires_at, attempts FROM recoveries "
                "WHERE recovery_id=?", (recovery_id,)).fetchone()
            if not row or row["expires_at"] < time.time():
                return None
            email_ok = hmac.compare_digest(row["email_digest"], code_digest(recovery_id + ":email", email_code))
            phone_ok = hmac.compare_digest(row["phone_digest"], code_digest(recovery_id + ":phone", phone_code))
            if email_ok and phone_ok and row["user_id"]:
                self._db.execute("DELETE FROM recoveries WHERE recovery_id=?", (recovery_id,))
                return row["user_id"]
            if row["attempts"] + 1 >= max_attempts:
                self._db.execute("DELETE FROM recoveries WHERE recovery_id=?", (recovery_id,))
            else:
                self._db.execute("UPDATE recoveries SET attempts=attempts+1 WHERE recovery_id=?", (recovery_id,))
            return None

    # ---- sessions ------------------------------------------------------------

    def add_session(self, token: str, user_id: str, ttl: int) -> int:
        expires = int(time.time()) + ttl
        with self._lock:
            self._db.execute("INSERT INTO sessions(token_hash, user_id, expires_at) VALUES (?,?,?)",
                             (hash_token(token), user_id, expires))
        return expires

    def session_user(self, token: str) -> str | None:
        with self._lock:
            row = self._db.execute("SELECT user_id, expires_at FROM sessions WHERE token_hash=?",
                                   (hash_token(token),)).fetchone()
        if not row or row["expires_at"] < time.time():
            return None
        return row["user_id"]

    def remove_session(self, token: str) -> None:
        with self._lock:
            self._db.execute("DELETE FROM sessions WHERE token_hash=?", (hash_token(token),))

    # ---- contacts ------------------------------------------------------------

    def add_contact(self, owner: str, contact: str) -> None:
        with self._lock:
            self._db.execute("INSERT OR IGNORE INTO contacts(owner, contact, created_at) VALUES (?,?,?)",
                             (owner, contact, int(time.time())))

    def remove_contact(self, owner: str, contact: str) -> None:
        with self._lock:
            self._db.execute("DELETE FROM contacts WHERE owner=? AND contact=?", (owner, contact))

    def contacts(self, owner: str) -> list[User]:
        with self._lock:
            rows = self._db.execute(
                "SELECT u.user_id, u.public_key, u.display_name FROM contacts c "
                "JOIN users u ON u.user_id = c.contact WHERE c.owner=? ORDER BY u.display_name",
                (owner,)).fetchall()
        return [User(r["user_id"], bytes(r["public_key"]), r["display_name"]) for r in rows]

    def watchers(self, user_id: str) -> list[str]:
        """Everyone who should hear this user's presence: whoever has them as a
        contact, and whoever shares a group with them."""
        with self._lock:
            rows = self._db.execute(
                "SELECT owner AS uid FROM contacts WHERE contact=? "
                "UNION SELECT m2.user_id FROM group_members m1 "
                "JOIN group_members m2 ON m1.group_id = m2.group_id "
                "WHERE m1.user_id=? AND m2.user_id<>?",
                (user_id, user_id, user_id)).fetchall()
        return [r["uid"] for r in rows]

    # ---- groups --------------------------------------------------------------

    def create_group(self, owner: str, title: str, members: list[str]) -> Group:
        group_id = uuid.uuid4().hex
        everyone = list(dict.fromkeys([owner] + members))
        with self._lock:
            self._db.execute("BEGIN")
            try:
                self._db.execute("INSERT INTO groups(group_id, title, owner, created_at) VALUES (?,?,?,?)",
                                 (group_id, title, owner, int(time.time())))
                self._db.executemany("INSERT INTO group_members(group_id, user_id) VALUES (?,?)",
                                     [(group_id, m) for m in everyone])
                self._db.execute("COMMIT")
            except Exception:
                self._db.execute("ROLLBACK")
                raise
        return Group(group_id, title, owner, everyone)

    def get_group(self, group_id: str) -> Group | None:
        with self._lock:
            row = self._db.execute("SELECT group_id, title, owner FROM groups WHERE group_id=?",
                                   (group_id,)).fetchone()
            if not row:
                return None
            members = [r["user_id"] for r in self._db.execute(
                "SELECT user_id FROM group_members WHERE group_id=? ORDER BY rowid", (group_id,))]
        return Group(row["group_id"], row["title"], row["owner"], members)

    def groups_of(self, user_id: str) -> list[Group]:
        with self._lock:
            ids = [r["group_id"] for r in self._db.execute(
                "SELECT group_id FROM group_members WHERE user_id=?", (user_id,))]
        return [g for g in (self.get_group(i) for i in ids) if g]

    def add_member(self, group_id: str, user_id: str) -> None:
        with self._lock:
            self._db.execute("INSERT OR IGNORE INTO group_members(group_id, user_id) VALUES (?,?)",
                             (group_id, user_id))

    def remove_member(self, group_id: str, user_id: str) -> None:
        with self._lock:
            self._db.execute("DELETE FROM group_members WHERE group_id=? AND user_id=?", (group_id, user_id))

    # ---- offline queue -------------------------------------------------------

    def enqueue(self, recipient: str, sender: str, message_id: str, conversation: str,
                envelope: str) -> int | None:
        """Queues one copy. Returns its id, or None when the recipient already
        holds this message id (a resend after a lost answer)."""
        with self._lock:
            try:
                cur = self._db.execute(
                    "INSERT INTO messages(recipient, sender, message_id, conversation, envelope, created_at) "
                    "VALUES (?,?,?,?,?,?)",
                    (recipient, sender, message_id, conversation, envelope, int(time.time())))
                return cur.lastrowid
            except sqlite3.IntegrityError:
                return None

    def pending(self, recipient: str, since: int, limit: int = 500) -> list[dict]:
        with self._lock:
            rows = self._db.execute(
                "SELECT id, sender, message_id, conversation, envelope, created_at FROM messages "
                "WHERE recipient=? AND id>? ORDER BY id LIMIT ?", (recipient, since, limit)).fetchall()
        return [{"id": r["id"], "from": r["sender"], "message_id": r["message_id"],
                 "conversation": r["conversation"], "envelope": r["envelope"],
                 "created_at": r["created_at"]} for r in rows]

    def acknowledge(self, recipient: str, ids: list[int]) -> int:
        if not ids:
            return 0
        with self._lock:
            cur = self._db.executemany("DELETE FROM messages WHERE recipient=? AND id=?",
                                       [(recipient, i) for i in ids])
            return cur.rowcount

    def expire(self, max_age_seconds: int) -> int:
        cutoff = int(time.time()) - max_age_seconds
        with self._lock:
            n = self._db.execute("DELETE FROM messages WHERE created_at<?", (cutoff,)).rowcount
            self._db.execute("DELETE FROM sessions WHERE expires_at<?", (int(time.time()),))
        return n
