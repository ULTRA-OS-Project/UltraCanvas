"""The layer on every engine the environment offers: the dialect helpers
and a round trip through a table with a serial, a text key, an upsert, a
concatenation, an index made twice and a transaction rolled back."""

from __future__ import annotations

import os

import pytest

from ultrastore_db import Database


def urls(tmp_path) -> list[str]:
    out = ["sqlite:///" + str(tmp_path / "t.sqlite3")]
    for name in ("ULTRASTORE_TEST_PG_URL", "ULTRASTORE_TEST_MYSQL_URL"):
        if os.environ.get(name):
            out.append(os.environ[name])
    return out


@pytest.fixture(params=["sqlite", "postgresql", "mysql"])
def db(request, tmp_path):
    env = {"postgresql": "ULTRASTORE_TEST_PG_URL", "mysql": "ULTRASTORE_TEST_MYSQL_URL"}.get(request.param)
    if env and not os.environ.get(env):
        pytest.skip(f"{env} not set")
    url = os.environ[env] if env else "sqlite:///" + str(tmp_path / "t.sqlite3")
    database = Database(url)
    database.execute("DROP TABLE IF EXISTS t_items")
    database.execute("DROP TABLE IF EXISTS t_meta")
    yield database
    database.execute("DROP TABLE IF EXISTS t_items")
    database.execute("DROP TABLE IF EXISTS t_meta")
    database.close()


def test_round_trip(db):
    assert db.engine in ("sqlite", "postgresql", "mysql")
    db.script([
        f"CREATE TABLE t_meta ({db.ident('key')} {db.text_key} PRIMARY KEY, v TEXT NOT NULL)",
        f"CREATE TABLE t_items (id {db.serial} PRIMARY KEY, name {db.text_key} NOT NULL, note TEXT NOT NULL DEFAULT '')",
    ])
    db.create_index("t_items_name", "t_items", "name")
    db.create_index("t_items_name", "t_items", "name")   # made once
    assert set(db.columns("t_items")) == {"id", "name", "note"} and db.columns("t_nothing") == []
    db.execute(db.upsert("t_meta", db.ident("key"), "v"), ("a", "1"))
    db.execute(db.upsert("t_meta", db.ident("key"), "v"), ("a", "2"))
    assert db.fetchone(f"SELECT v FROM t_meta WHERE {db.ident('key')} = ?", ("a",)) == {"v": "2"}
    db.execute("INSERT INTO t_items (name, note) VALUES (?, ?)", ("x", "first"))
    first = db.last_id()
    db.execute("INSERT INTO t_items (name) VALUES (?)", ("y",))
    assert db.last_id() == first + 1
    db.execute(f"UPDATE t_items SET note = {db.concat('note', '?', '?')} WHERE id = ?", (" and", " second", first))
    assert db.fetchone("SELECT note FROM t_items WHERE id = ?", (first,))["note"] == "first and second"
    assert [r["name"] for r in db.fetchall("SELECT name FROM t_items ORDER BY id")] == ["x", "y"]
    db.begin()
    db.execute("INSERT INTO t_items (name) VALUES (?)", ("z",))
    db.rollback()
    assert len(db.fetchall("SELECT id FROM t_items")) == 2
    db.begin()
    db.execute("INSERT INTO t_items (name) VALUES (?)", ("z",))
    db.commit()
    assert len(db.fetchall("SELECT id FROM t_items")) == 3


def test_bad_scheme():
    with pytest.raises(RuntimeError, match="unknown database scheme"):
        Database("redis://x")


def test_operator_session_round_trips_and_is_refused_when_forged_or_over():
    from ultrastore_db.operator_session import ROLES, allowed, issue, verify
    token = issue("s3cret", operator_id="op-1", name="Alex", role="reviewer", hours=1, now=1_700_000_000)
    assert token.startswith("op1.")
    data = verify("s3cret", token, now=1_700_000_100)
    assert data == {"id": "op-1", "name": "Alex", "role": "reviewer", "exp": 1_700_003_600}
    assert verify("other", token, now=1_700_000_100) is None
    assert verify("s3cret", token, now=1_700_004_000) is None
    assert verify("s3cret", token[:-2] + "00", now=1_700_000_100) is None
    assert verify("", token, now=1_700_000_100) is None
    assert verify("s3cret", "Bearer nope") is None
    assert allowed("admin", "operators.manage") and not allowed("reviewer", "operators.manage")
    assert allowed("support", "users.support") and not allowed("support", "users.manage")
    assert allowed("auditor", "audit.read") and not allowed("auditor", "notices.send")
    assert set(ROLES) == {"admin", "reviewer", "support", "auditor"}


def test_totp_and_the_codes():
    from ultrastore_db.totp import hash_code, hash_recovery, new_code, new_recovery_codes, new_totp_secret, otpauth_url, totp_code, verify_totp
    secret = new_totp_secret()
    assert len(secret) == 32 and "secret=" + secret in otpauth_url(secret, "alex@example.org")
    assert verify_totp(secret, totp_code(secret, 1_700_000_000), at=1_700_000_000)
    assert verify_totp(secret, totp_code(secret, 1_700_000_000), at=1_700_000_030)     # one step of drift
    assert not verify_totp(secret, totp_code(secret, 1_700_000_000), at=1_700_000_090)
    assert verify_totp("GEZDGNBVGY3TQOJQGEZDGNBVGY3TQOJQ", "287082", at=59)              # RFC 6238's SHA-1 vector, 20-byte key
    codes = new_recovery_codes()
    assert len(codes) == 8 and all(len(c) == 8 for c in codes) and hash_recovery(" " + codes[0].upper()) == hash_recovery(codes[0])
    assert len(new_code()) == 6 and hash_code("123456", "s") != hash_code("123456", "t")
