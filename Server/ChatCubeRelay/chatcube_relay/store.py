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

from .migrations import migrate

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
            # The schema, brought to this relay's version (migrations.py).
            self.migrations_applied = migrate(self._db)

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

    # ---- links e-mailed: confirming the address, approving a recovery ---------------

    def put_link(self, token: str, purpose: str, user_id: str, email: str, ttl: int,
                 recovery_id: str | None = None) -> None:
        """A link's token, kept as its SHA-256 only. A new confirmation link
        replaces the ones before it for that user."""
        with self._lock:
            now = int(time.time())
            if purpose == "confirm":
                self._db.execute("DELETE FROM links WHERE user_id=? AND purpose='confirm'", (user_id,))
            self._db.execute(
                "INSERT INTO links(token_digest, purpose, user_id, email, recovery_id, expires_at) "
                "VALUES (?,?,?,?,?,?)", (hash_token(token), purpose, user_id, email, recovery_id, now + ttl))

    def find_link(self, token: str, purpose: str) -> dict | None:
        """The link behind `token` while it can still be used: not expired,
        not used. None otherwise."""
        with self._lock:
            row = self._db.execute(
                "SELECT user_id, email, recovery_id, expires_at, used_at FROM links "
                "WHERE token_digest=? AND purpose=?", (hash_token(token), purpose)).fetchone()
        if not row or row["used_at"] is not None or row["expires_at"] < time.time():
            return None
        return dict(row)

    def use_link(self, token: str) -> bool:
        """Marks the link used; False when it already was (two clicks at once)."""
        with self._lock:
            cur = self._db.execute("UPDATE links SET used_at=? WHERE token_digest=? AND used_at IS NULL",
                                   (int(time.time()), hash_token(token)))
            return cur.rowcount == 1

    def start_recovery(self, recovery_id: str, user_id: str | None, poll_token: str, ttl: int,
                       requested_from: str) -> None:
        """A recovery for `user_id` (None: an address nobody has - recorded
        anyway, so the answer is the same), which the asking computer polls
        with `poll_token`."""
        with self._lock:
            now = int(time.time())
            self._db.execute(
                "INSERT INTO recoveries(recovery_id, user_id, poll_digest, expires_at, requested_from) "
                "VALUES (?,?,?,?,?)", (recovery_id, user_id, hash_token(poll_token), now + ttl, requested_from))

    def get_recovery(self, recovery_id: str) -> dict | None:
        with self._lock:
            row = self._db.execute(
                "SELECT recovery_id, user_id, poll_digest, approved_at, expires_at, requested_from "
                "FROM recoveries WHERE recovery_id=?", (recovery_id,)).fetchone()
        return dict(row) if row else None

    def approve_recovery(self, recovery_id: str) -> bool:
        with self._lock:
            cur = self._db.execute(
                "UPDATE recoveries SET approved_at=? WHERE recovery_id=? AND approved_at IS NULL AND expires_at>=?",
                (int(time.time()), recovery_id, int(time.time())))
            return cur.rowcount == 1

    def end_recovery(self, recovery_id: str) -> None:
        with self._lock:
            self._db.execute("DELETE FROM recoveries WHERE recovery_id=?", (recovery_id,))
            self._db.execute("UPDATE links SET used_at=COALESCE(used_at, ?) WHERE recovery_id=?",
                             (int(time.time()), recovery_id))

    # ---- housekeeping ------------------------------------------------------------

    # Send counts are kept this long - longer than any rate limit looks back.
    SENDS_KEPT_SECONDS = 24 * 3600

    def prune(self) -> dict[str, int]:
        """Deletes what can no longer be used: expired or used links, expired
        recoveries and codes, send counts older than any limit looks at, and
        expired sessions. Run at start and every hour. Answers how many rows
        went from each table."""
        now = int(time.time())
        steps = {
            "links": ("DELETE FROM links WHERE expires_at<? OR used_at IS NOT NULL", (now,)),
            "recoveries": ("DELETE FROM recoveries WHERE expires_at<?", (now,)),
            "codes": ("DELETE FROM codes WHERE expires_at<?", (now,)),
            "sends": ("DELETE FROM sends WHERE at<?", (now - self.SENDS_KEPT_SECONDS,)),
            "sessions": ("DELETE FROM sessions WHERE expires_at<?", (now,)),
        }
        with self._lock:
            return {table: self._db.execute(sql, args).rowcount for table, (sql, args) in steps.items()}

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

    def rename_group(self, group_id: str, title: str) -> None:
        with self._lock:
            self._db.execute("UPDATE groups SET title=? WHERE group_id=?", (title, group_id))

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
