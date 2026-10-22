"""SQLite storage: users, sessions, contacts, groups and the offline queue.

One connection, one lock: the relay is a small service and SQLite answers
these queries in microseconds. Tokens are stored as their SHA-256 only, so
a copy of the database opens no session. Envelopes are stored exactly as
the client sent them; nothing here can read them."""

from __future__ import annotations

import hashlib
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
"""

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
