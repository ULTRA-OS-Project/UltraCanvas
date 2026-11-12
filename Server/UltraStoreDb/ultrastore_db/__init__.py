"""The database behind the UltraStore services (the payment service, the
marketplace service): MySQL or PostgreSQL in production, SQLite for tests
and development only.

The URL names the engine: mysql://user:password@host:3306/db (PyMySQL, the
`mysql` extra), postgresql://user:password@host:5432/db (psycopg 3, the
`postgres` extra) or sqlite:///path/to/file.sqlite3 (a bare path means the
same). A service writes one SQL dialect with `?` placeholders, and the few
differences that cannot be hidden in the statement - the placeholder style,
the serial and the key column types, an upsert, string concatenation, an
index made only once, how a row is read - are methods here. Nothing else in
a service knows which engine runs.

Version: 0.2.0 - MySQL (PyMySQL); text_key, upsert, concat, create_index, columns
Version: 0.1.0 - PostgreSQL and SQLite (the payment service's db.py)
"""
from __future__ import annotations

import sqlite3
from typing import Any, Iterable
from urllib.parse import unquote, urlparse

__all__ = ["Database", "__version__"]
__version__ = "0.2.0"


class Database:
    """One connection, used from one request at a time (a service is a
    single process; FastAPI runs the handlers one after the other for the
    synchronous calls made there)."""

    def __init__(self, url: str) -> None:
        self.url = url
        scheme = urlparse(url).scheme if "://" in url else "sqlite"
        if scheme in ("postgresql", "postgres"):
            try:
                import psycopg
            except ImportError as e:   # pragma: no cover - the extra is named in the error
                raise RuntimeError("PostgreSQL needs the `postgres` extra: pip install 'ultrastore-db[postgres]'") from e
            self.engine = "postgresql"
            self._db = psycopg.connect(url, autocommit=True, row_factory=psycopg.rows.dict_row)
        elif scheme in ("mysql", "mariadb"):
            try:
                import pymysql
                import pymysql.cursors
            except ImportError as e:   # pragma: no cover
                raise RuntimeError("MySQL needs the `mysql` extra: pip install 'ultrastore-db[mysql]'") from e
            self.engine = "mysql"
            parts = urlparse(url)
            kwargs: dict[str, Any] = {"host": parts.hostname or "127.0.0.1", "port": parts.port or 3306, "user": unquote(parts.username or ""),
                                      "password": unquote(parts.password or ""), "database": parts.path.lstrip("/"), "autocommit": True,
                                      "charset": "utf8mb4", "cursorclass": pymysql.cursors.DictCursor}
            for key, value in (q.split("=", 1) for q in parts.query.split("&") if "=" in q):
                if key == "unix_socket":
                    kwargs["unix_socket"] = unquote(value)
            self._db = pymysql.connect(**kwargs)
        elif scheme == "sqlite":
            self.engine = "sqlite"
            path = url[len("sqlite:///"):] if url.startswith("sqlite:///") else url
            self._db = sqlite3.connect(path, check_same_thread=False, isolation_level=None)
            self._db.row_factory = sqlite3.Row
            self._db.execute("PRAGMA journal_mode=WAL")
        else:
            raise RuntimeError(f"unknown database scheme {scheme!r} (mysql://, postgresql:// or sqlite:///)")

    # ----- the dialect -----
    @property
    def serial(self) -> str:
        """An integer that counts up on its own, as a column type (with
        PRIMARY KEY after it)."""
        return {"postgresql": "BIGSERIAL", "mysql": "BIGINT AUTO_INCREMENT", "sqlite": "INTEGER"}[self.engine]

    @property
    def text_key(self) -> str:
        """A text column that is a primary key or indexed: MySQL wants a
        bounded one."""
        return "VARCHAR(191)" if self.engine == "mysql" else "TEXT"

    def upsert(self, table: str, key: str, value: str) -> str:
        """INSERT ... that replaces `value` when `key` is already there;
        two parameters, the key then the value."""
        if self.engine == "mysql":
            return f"INSERT INTO {table} ({key}, {value}) VALUES (?, ?) ON DUPLICATE KEY UPDATE {value} = VALUES({value})"
        return f"INSERT INTO {table} ({key}, {value}) VALUES (?, ?) ON CONFLICT ({key}) DO UPDATE SET {value} = excluded.{value}"

    def ident(self, name: str) -> str:
        """A column or table name quoted, for one that is a reserved word
        somewhere (`key` on MySQL)."""
        return f"`{name}`" if self.engine == "mysql" else f'"{name}"'

    def concat(self, *parts: str) -> str:
        """The SQL that joins these expressions as one string."""
        return "CONCAT(" + ", ".join(parts) + ")" if self.engine == "mysql" else " || ".join(parts)

    def create_index(self, name: str, table: str, columns: str) -> None:
        """An index, made once."""
        if self.engine == "mysql":
            row = self.fetchone("SELECT COUNT(*) AS n FROM information_schema.statistics WHERE table_schema = DATABASE() AND table_name = ? AND index_name = ?",
                                (table, name))
            if row and int(row["n"]) > 0:
                return
            self.execute(f"CREATE INDEX {name} ON {table} ({columns})")
        else:
            self.execute(f"CREATE INDEX IF NOT EXISTS {name} ON {table} ({columns})")

    def columns(self, table: str) -> list[str]:
        """The table's column names, empty when there is no such table."""
        if self.engine == "postgresql":
            rows = self.fetchall("SELECT column_name FROM information_schema.columns WHERE table_name = ?", (table,))
            return [r["column_name"] for r in rows]
        if self.engine == "mysql":
            rows = self.fetchall("SELECT column_name AS column_name FROM information_schema.columns WHERE table_schema = DATABASE() AND table_name = ?", (table,))
            return [r["column_name"] for r in rows]
        return [r["name"] for r in self.fetchall(f"PRAGMA table_info({table})")]

    def _sql(self, sql: str) -> str:
        return sql if self.engine == "sqlite" else sql.replace("?", "%s")

    # ----- statements -----
    def execute(self, sql: str, params: Iterable[Any] = ()) -> None:
        if self.engine == "sqlite":
            self._db.execute(sql, tuple(params))
        else:
            with self._db.cursor() as cur:
                cur.execute(self._sql(sql), tuple(params))

    def script(self, statements: Iterable[str]) -> None:
        for statement in statements:
            self.execute(statement)

    def fetchone(self, sql: str, params: Iterable[Any] = ()) -> dict | None:
        if self.engine == "sqlite":
            row = self._db.execute(sql, tuple(params)).fetchone()
            return dict(row) if row is not None else None
        with self._db.cursor() as cur:
            cur.execute(self._sql(sql), tuple(params))
            row = cur.fetchone()
            return dict(row) if row is not None else None

    def fetchall(self, sql: str, params: Iterable[Any] = ()) -> list[dict]:
        if self.engine == "sqlite":
            return [dict(row) for row in self._db.execute(sql, tuple(params)).fetchall()]
        with self._db.cursor() as cur:
            cur.execute(self._sql(sql), tuple(params))
            return [dict(row) for row in cur.fetchall()]

    def last_id(self) -> int:
        """The serial the last INSERT got."""
        if self.engine == "sqlite":
            return int(self._db.execute("SELECT last_insert_rowid() AS id").fetchone()[0])
        if self.engine == "mysql":
            return int(self._db.insert_id())
        return int(self.fetchone("SELECT lastval() AS id")["id"])   # type: ignore[index]

    # ----- transactions -----
    def begin(self) -> None:
        if self.engine == "mysql":
            self._db.begin()
        else:
            self.execute("BEGIN")

    def commit(self) -> None:
        self._db.commit() if self.engine == "mysql" else self.execute("COMMIT")

    def rollback(self) -> None:
        self._db.rollback() if self.engine == "mysql" else self.execute("ROLLBACK")

    def close(self) -> None:
        self._db.close()
