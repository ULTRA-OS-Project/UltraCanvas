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
