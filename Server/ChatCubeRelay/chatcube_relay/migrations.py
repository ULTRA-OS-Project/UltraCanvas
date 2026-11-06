"""The relay database's schema, as numbered migrations.

The version a database is at is SQLite's `PRAGMA user_version`. Opening one
brings it to the newest version, a step at a time: each migration runs in a
transaction of its own that also records its number, so a step that fails
leaves the database as it was, at the version before. A database at a version
newer than this relay knows (written by a later relay) is refused rather than
used.

Relays before 1.2.0 did not record a version; such a database is recognised
by its tables (`baseline_version`): `users` alone is version 1 (relay 1.0),
`recovery` without the verified columns version 2 (relay 1.1).

To change the schema, append a migration - never edit one that has shipped:
a relay that ran it will not run it again.
"""

from __future__ import annotations

import sqlite3
from dataclasses import dataclass
from typing import Callable


@dataclass(frozen=True)
class Migration:
    version: int
    title: str
    apply: Callable[[sqlite3.Connection], None]


def _run(db: sqlite3.Connection, statements: str) -> None:
    # One statement at a time: executescript() would commit the transaction
    # the migration runs in.
    for statement in statements.split(";"):
        if statement.strip():
            db.execute(statement)


def _columns(db: sqlite3.Connection, table: str) -> set[str]:
    return {row[1] for row in db.execute(f"PRAGMA table_info({table})")}


def _tables(db: sqlite3.Connection) -> set[str]:
    return {row[0] for row in db.execute("SELECT name FROM sqlite_master WHERE type='table'")}


def _v1_accounts_and_messages(db: sqlite3.Connection) -> None:
    _run(db, """
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
CREATE INDEX IF NOT EXISTS messages_by_recipient ON messages(recipient, id)
""")


def _v2_recovery_contacts(db: sqlite3.Connection) -> None:
    # What a user gave to recover the account with: theirs alone, never part
    # of what anyone else is told about them.
    _run(db, """
CREATE TABLE IF NOT EXISTS recovery (
    user_id    TEXT PRIMARY KEY REFERENCES users(user_id),
    email      TEXT NOT NULL DEFAULT '',
    phone      TEXT NOT NULL DEFAULT '',
    updated_at INTEGER NOT NULL
)
""")


def _v3_confirmation_and_recovery(db: sqlite3.Connection) -> None:
    # Whether the e-mail address and the phone number are confirmed: the
    # address by the link e-mailed to it, the phone by the code sent to it.
    # Everyone's starts unconfirmed - including what relay 1.1 kept.
    present = _columns(db, "recovery")
    for column in ("email_verified", "phone_verified"):
        if column not in present:
            db.execute(f"ALTER TABLE recovery ADD COLUMN {column} INTEGER NOT NULL DEFAULT 0")
    # A recovery under way had another shape on the branch that preceded this
    # release; it is short-lived, so it is simply made afresh.
    if "recoveries" in _tables(db) and "poll_digest" not in _columns(db, "recoveries"):
        db.execute("DROP TABLE recoveries")
    _run(db, """
CREATE TABLE IF NOT EXISTS codes (
    key        TEXT PRIMARY KEY,
    digest     TEXT NOT NULL,
    expires_at INTEGER NOT NULL,
    attempts   INTEGER NOT NULL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS links (
    token_digest TEXT PRIMARY KEY,
    purpose      TEXT NOT NULL,
    user_id      TEXT NOT NULL,
    email        TEXT NOT NULL,
    recovery_id  TEXT,
    expires_at   INTEGER NOT NULL,
    used_at      INTEGER
);
CREATE INDEX IF NOT EXISTS links_by_user ON links(user_id, purpose);
CREATE TABLE IF NOT EXISTS escrow (
    user_id    TEXT PRIMARY KEY REFERENCES users(user_id),
    nonce      BLOB NOT NULL,
    sealed     BLOB NOT NULL,
    updated_at INTEGER NOT NULL
);
CREATE TABLE IF NOT EXISTS recoveries (
    recovery_id    TEXT PRIMARY KEY,
    user_id        TEXT,
    poll_digest    TEXT NOT NULL,
    approved_at    INTEGER,
    expires_at     INTEGER NOT NULL,
    requested_from TEXT NOT NULL DEFAULT ''
);
CREATE TABLE IF NOT EXISTS sends (
    key TEXT NOT NULL,
    at  INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS sends_by_key ON sends(key, at)
""")


MIGRATIONS: list[Migration] = [
    Migration(1, "accounts, contacts, groups and the offline queue (relay 1.0)", _v1_accounts_and_messages),
    Migration(2, "the owner's e-mail address and phone number (relay 1.1)", _v2_recovery_contacts),
    Migration(3, "confirming them, account recovery by e-mail link (relay 1.2)", _v3_confirmation_and_recovery),
]
LATEST = MIGRATIONS[-1].version


class SchemaTooNew(RuntimeError):
    """The database was written by a relay newer than this one."""


def current_version(db: sqlite3.Connection) -> int:
    return int(db.execute("PRAGMA user_version").fetchone()[0])


def baseline_version(db: sqlite3.Connection) -> int:
    """The version of a database from before versions were recorded."""
    tables = _tables(db)
    if "users" not in tables:
        return 0                            # new
    if "recovery" not in tables:
        return 1                            # relay 1.0
    return 2                                # relay 1.1 (verified columns, if any, are 3's to add)


def migrate(db: sqlite3.Connection) -> list[int]:
    """Brings `db` (opened with isolation_level=None) to LATEST. Answers the
    versions applied, oldest first."""
    version = current_version(db)
    if version == 0:
        version = baseline_version(db)
        if version:
            db.execute(f"PRAGMA user_version = {version}")
    if version > LATEST:
        raise SchemaTooNew(f"the database is at schema version {version}; this relay knows up to {LATEST} - "
                           "run the relay that wrote it, or a newer one")
    applied: list[int] = []
    for migration in MIGRATIONS:
        if migration.version <= version:
            continue
        db.execute("BEGIN IMMEDIATE")
        try:
            migration.apply(db)
            db.execute(f"PRAGMA user_version = {migration.version}")
            db.execute("COMMIT")
        except BaseException:
            db.execute("ROLLBACK")
            raise
        applied.append(migration.version)
    return applied
