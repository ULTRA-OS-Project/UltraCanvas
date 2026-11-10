"""The database behind the service: PostgreSQL in production, SQLite for
tests and development only.

ULTRASTORE_PAY_DB is a URL: postgresql://user:password@host:5432/ultrastore_pay
(psycopg 3, the `postgres` extra) or sqlite:///path/to/file.sqlite3 (a bare
path means the same). The store writes one SQL dialect with `?` placeholders
and a few named differences (the placeholder style, the serial type, how a
row is read), and this module hides them. Nothing else in the service knows
which engine runs.

Version: 0.1.0
"""
from __future__ import annotations

import sqlite3
from typing import Any, Iterable
from urllib.parse import urlparse


class Database:
    """One connection, used from one request at a time (the service is a
    single process; FastAPI runs the handlers one after the other for the
    synchronous calls made here)."""

    def __init__(self, url: str) -> None:
        self.url = url
        scheme = urlparse(url).scheme if "://" in url else "sqlite"
        if scheme in ("postgresql", "postgres"):
            try:
                import psycopg
            except ImportError as e:   # pragma: no cover - the extra is named in the error
                raise RuntimeError("PostgreSQL needs the `postgres` extra: pip install 'ultrastore-pay[postgres]'") from e
            self.engine = "postgresql"
            self._db = psycopg.connect(url, autocommit=True, row_factory=psycopg.rows.dict_row)
        elif scheme == "sqlite":
            self.engine = "sqlite"
            path = url[len("sqlite:///"):] if url.startswith("sqlite:///") else url
            self._db = sqlite3.connect(path, check_same_thread=False, isolation_level=None)
            self._db.row_factory = sqlite3.Row
            self._db.execute("PRAGMA journal_mode=WAL")
        else:
            raise RuntimeError(f"ULTRASTORE_PAY_DB: unknown database scheme {scheme!r} (postgresql:// or sqlite:///)")

    # ----- the dialect -----
    @property
    def serial(self) -> str:
        """An integer that counts up on its own, as a column type."""
        return "BIGSERIAL" if self.engine == "postgresql" else "INTEGER"

    def _sql(self, sql: str) -> str:
        return sql.replace("?", "%s") if self.engine == "postgresql" else sql

    # ----- statements -----
    def execute(self, sql: str, params: Iterable[Any] = ()) -> None:
        if self.engine == "postgresql":
            with self._db.cursor() as cur:
                cur.execute(self._sql(sql), tuple(params))
        else:
            self._db.execute(sql, tuple(params))

    def script(self, statements: Iterable[str]) -> None:
        for statement in statements:
            self.execute(statement)

    def fetchone(self, sql: str, params: Iterable[Any] = ()) -> dict | None:
        if self.engine == "postgresql":
            with self._db.cursor() as cur:
                cur.execute(self._sql(sql), tuple(params))
                return cur.fetchone()
        row = self._db.execute(sql, tuple(params)).fetchone()
        return dict(row) if row is not None else None

    def fetchall(self, sql: str, params: Iterable[Any] = ()) -> list[dict]:
        if self.engine == "postgresql":
            with self._db.cursor() as cur:
                cur.execute(self._sql(sql), tuple(params))
                return list(cur.fetchall())
        return [dict(row) for row in self._db.execute(sql, tuple(params)).fetchall()]

    # ----- transactions -----
    def begin(self) -> None:
        self.execute("BEGIN")

    def commit(self) -> None:
        self.execute("COMMIT")

    def rollback(self) -> None:
        self.execute("ROLLBACK")

    def close(self) -> None:
        self._db.close()
