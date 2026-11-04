"""The schema migrations: a new database, and the databases relays 1.0 and 1.1
wrote (no version recorded), brought to the newest version with their data."""

from __future__ import annotations

import sqlite3

import pytest

from chatcube_relay import migrations
from chatcube_relay.migrations import LATEST, Migration, SchemaTooNew, migrate
from chatcube_relay.store import Store

# What relay 1.0 created (its SCHEMA, as it shipped).
RELAY_1_0 = """
CREATE TABLE users (user_id TEXT PRIMARY KEY, public_key BLOB NOT NULL, display_name TEXT NOT NULL,
                    created_at INTEGER NOT NULL);
CREATE TABLE sessions (token_hash TEXT PRIMARY KEY, user_id TEXT NOT NULL REFERENCES users(user_id),
                       expires_at INTEGER NOT NULL);
CREATE TABLE contacts (owner TEXT NOT NULL, contact TEXT NOT NULL, created_at INTEGER NOT NULL,
                       PRIMARY KEY (owner, contact));
CREATE INDEX contacts_by_contact ON contacts(contact);
CREATE TABLE groups (group_id TEXT PRIMARY KEY, title TEXT NOT NULL, owner TEXT NOT NULL,
                     created_at INTEGER NOT NULL);
CREATE TABLE group_members (group_id TEXT NOT NULL REFERENCES groups(group_id), user_id TEXT NOT NULL,
                            PRIMARY KEY (group_id, user_id));
CREATE INDEX members_by_user ON group_members(user_id);
CREATE TABLE messages (id INTEGER PRIMARY KEY AUTOINCREMENT, recipient TEXT NOT NULL, sender TEXT NOT NULL,
                       message_id TEXT NOT NULL, conversation TEXT NOT NULL, envelope TEXT NOT NULL,
                       created_at INTEGER NOT NULL, UNIQUE (recipient, message_id));
CREATE INDEX messages_by_recipient ON messages(recipient, id);
"""
# And what 1.1 added.
RELAY_1_1 = RELAY_1_0 + """
CREATE TABLE recovery (user_id TEXT PRIMARY KEY REFERENCES users(user_id), email TEXT NOT NULL DEFAULT '',
                       phone TEXT NOT NULL DEFAULT '', updated_at INTEGER NOT NULL);
"""


def made_by(path, schema: str) -> None:
    db = sqlite3.connect(path)
    db.executescript(schema)
    db.execute("INSERT INTO users VALUES ('u1', x'00', 'Erin', 1)")
    db.execute("INSERT INTO messages(recipient, sender, message_id, conversation, envelope, created_at) "
               "VALUES ('u1', 'u2', 'm1', 'dm', 'sealed', 1)")
    if "recovery" in schema:
        db.execute("INSERT INTO recovery(user_id, email, phone, updated_at) "
                   "VALUES ('u1', 'erin@example.org', '+4915123456789', 1)")
    db.commit()
    db.close()


def version_of(path) -> int:
    db = sqlite3.connect(path)
    try:
        return db.execute("PRAGMA user_version").fetchone()[0]
    finally:
        db.close()


def columns(store: Store, table: str) -> set[str]:
    return {row["name"] for row in store._db.execute(f"PRAGMA table_info({table})")}


def test_a_new_database_gets_every_migration(tmp_path):
    store = Store(str(tmp_path / "new.sqlite3"))
    assert store.migrations_applied == [m.version for m in migrations.MIGRATIONS]
    assert {"email_verified", "phone_verified"} <= columns(store, "recovery")
    assert {"links", "escrow", "recoveries", "codes", "sends"} <= {
        r[0] for r in store._db.execute("SELECT name FROM sqlite_master WHERE type='table'")}
    store.close()
    assert version_of(tmp_path / "new.sqlite3") == LATEST


def test_a_relay_1_0_database_keeps_its_data(tmp_path):
    path = tmp_path / "r10.sqlite3"
    made_by(path, RELAY_1_0)
    store = Store(str(path))
    assert store.migrations_applied == [2, 3]
    assert store.get_user("u1").display_name == "Erin"
    assert store.pending("u1", 0)[0]["envelope"] == "sealed"
    contacts = store.get_contacts("u1")
    assert contacts.email == "" and not contacts.email_verified
    store.close()
    assert version_of(path) == LATEST


def test_a_relay_1_1_database_starts_unconfirmed(tmp_path):
    path = tmp_path / "r11.sqlite3"
    made_by(path, RELAY_1_1)
    store = Store(str(path))
    assert store.migrations_applied == [3]
    contacts = store.get_contacts("u1")
    # What 1.1 kept is kept - and not confirmed: nobody had confirmed it.
    assert contacts.email == "erin@example.org" and contacts.phone == "+4915123456789"
    assert not contacts.email_verified and not contacts.phone_verified
    store.close()


def test_the_branch_before_this_release_is_brought_along(tmp_path):
    # Verified columns already there, and recoveries in their earlier shape.
    path = tmp_path / "branch.sqlite3"
    made_by(path, RELAY_1_1 + """
ALTER TABLE recovery ADD COLUMN email_verified INTEGER NOT NULL DEFAULT 0;
ALTER TABLE recovery ADD COLUMN phone_verified INTEGER NOT NULL DEFAULT 0;
CREATE TABLE recoveries (recovery_id TEXT PRIMARY KEY, user_id TEXT, email_digest TEXT NOT NULL,
                         phone_digest TEXT NOT NULL, expires_at INTEGER NOT NULL,
                         attempts INTEGER NOT NULL DEFAULT 0);
""")
    store = Store(str(path))
    assert store.migrations_applied == [3]
    assert "poll_digest" in columns(store, "recoveries")
    store.close()


def test_reopening_applies_nothing(tmp_path):
    path = str(tmp_path / "again.sqlite3")
    Store(path).close()
    store = Store(path)
    assert store.migrations_applied == []
    store.close()


def test_a_database_from_a_newer_relay_is_refused(tmp_path):
    path = tmp_path / "newer.sqlite3"
    db = sqlite3.connect(path)
    db.execute(f"PRAGMA user_version = {LATEST + 1}")
    db.close()
    with pytest.raises(SchemaTooNew):
        Store(str(path))


def test_a_failing_step_leaves_the_database_as_it_was(tmp_path, monkeypatch):
    path = tmp_path / "half.sqlite3"
    made_by(path, RELAY_1_1)

    def breaks(db):
        db.execute("CREATE TABLE half_made (x INTEGER)")
        raise RuntimeError("disk full")

    monkeypatch.setattr(migrations, "MIGRATIONS",
                        migrations.MIGRATIONS[:2] + [Migration(3, "breaks", breaks)])
    db = sqlite3.connect(path, isolation_level=None)
    with pytest.raises(RuntimeError):
        migrate(db)
    # Recognised as 1.1 (version 2, recorded), and nothing of the step stayed.
    assert db.execute("PRAGMA user_version").fetchone()[0] == 2
    assert not db.execute("SELECT name FROM sqlite_master WHERE name='half_made'").fetchone()
    db.close()
