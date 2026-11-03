"""The orders, on SQLite.

An order is one purchase attempt: made when the store asks for a checkout,
paid when Skrill's status report says so, and then the carrier of the
licence and the receipt number. The sales the admin backend fetches are
the paid orders."""

from __future__ import annotations

import secrets
import sqlite3
import time
from dataclasses import dataclass


def now_iso() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


@dataclass
class Order:
    id: str
    secret: str
    created_at: str
    app: str
    app_name: str
    amount: str          # what the buyer pays, decimal
    currency: str
    list_price: str      # before the code
    code: str            # normalized, or ""
    code_applied: bool
    status: str          # open, pending, paid, cancelled, failed, chargeback
    skrill_transaction: str = ""
    payer_email: str = ""
    paid_at: str = ""
    licence_id: str = ""
    licence: str = ""
    receipt_no: int = 0
    notes: str = ""

    def public(self) -> dict:
        out = {"order_id": self.id, "app": self.app, "app_name": self.app_name, "amount": self.amount, "currency": self.currency,
               "list_price": self.list_price, "code": self.code, "code_applied": self.code_applied, "status": self.status,
               "created_at": self.created_at}
        if self.status == "paid":
            out.update({"paid_at": self.paid_at, "licence_id": self.licence_id, "licence": self.licence, "receipt_no": self.receipt_no})
        return out


class Store:
    def __init__(self, path: str) -> None:
        self._db = sqlite3.connect(path, check_same_thread=False, isolation_level=None)
        self._db.row_factory = sqlite3.Row
        self._db.execute("PRAGMA journal_mode=WAL")
        self._db.executescript("""
            CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
            CREATE TABLE IF NOT EXISTS orders (
                id TEXT PRIMARY KEY, secret TEXT NOT NULL, created_at TEXT NOT NULL, app TEXT NOT NULL, app_name TEXT NOT NULL,
                amount TEXT NOT NULL, currency TEXT NOT NULL, list_price TEXT NOT NULL, code TEXT NOT NULL DEFAULT '',
                code_applied INTEGER NOT NULL DEFAULT 0, status TEXT NOT NULL, skrill_transaction TEXT NOT NULL DEFAULT '',
                payer_email TEXT NOT NULL DEFAULT '', paid_at TEXT NOT NULL DEFAULT '', licence_id TEXT NOT NULL DEFAULT '',
                licence TEXT NOT NULL DEFAULT '', receipt_no INTEGER NOT NULL DEFAULT 0, notes TEXT NOT NULL DEFAULT '',
                seq INTEGER NOT NULL DEFAULT 0);
            CREATE INDEX IF NOT EXISTS orders_seq ON orders (seq);
        """)

    def close(self) -> None:
        self._db.close()

    # ----- meta -----
    def meta(self, key: str) -> str:
        row = self._db.execute("SELECT value FROM meta WHERE key = ?", (key,)).fetchone()
        return row["value"] if row else ""

    def set_meta(self, key: str, value: str) -> None:
        self._db.execute("INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)", (key, value))

    # ----- orders -----
    @staticmethod
    def _order(row: sqlite3.Row) -> Order:
        return Order(id=row["id"], secret=row["secret"], created_at=row["created_at"], app=row["app"], app_name=row["app_name"],
                     amount=row["amount"], currency=row["currency"], list_price=row["list_price"], code=row["code"],
                     code_applied=bool(row["code_applied"]), status=row["status"], skrill_transaction=row["skrill_transaction"],
                     payer_email=row["payer_email"], paid_at=row["paid_at"], licence_id=row["licence_id"], licence=row["licence"],
                     receipt_no=row["receipt_no"], notes=row["notes"])

    def create_order(self, *, app: str, app_name: str, amount: str, currency: str, list_price: str, code: str, code_applied: bool) -> Order:
        order = Order(id=secrets.token_hex(8), secret=secrets.token_urlsafe(24), created_at=now_iso(), app=app, app_name=app_name,
                      amount=amount, currency=currency, list_price=list_price, code=code, code_applied=code_applied, status="open")
        self._db.execute(
            "INSERT INTO orders (id, secret, created_at, app, app_name, amount, currency, list_price, code, code_applied, status)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (order.id, order.secret, order.created_at, app, app_name, amount, currency, list_price, code, int(code_applied), "open"))
        return order

    def get_order(self, order_id: str) -> Order | None:
        row = self._db.execute("SELECT * FROM orders WHERE id = ?", (order_id,)).fetchone()
        return self._order(row) if row else None

    def set_status(self, order_id: str, status: str, *, skrill_transaction: str = "", payer_email: str = "", note: str = "") -> None:
        self._db.execute("UPDATE orders SET status = ?, skrill_transaction = COALESCE(NULLIF(?, ''), skrill_transaction),"
                         " payer_email = COALESCE(NULLIF(?, ''), payer_email), notes = CASE WHEN ? = '' THEN notes ELSE notes || ? || char(10) END"
                         " WHERE id = ?", (status, skrill_transaction, payer_email, note, note, order_id))

    def mark_paid(self, order_id: str, *, skrill_transaction: str, payer_email: str, licence_id: str, licence: str) -> Order:
        """Paid, with the next receipt number and the next sales sequence."""
        receipt = int(self.meta("receipt_no") or "0") + 1
        seq = int(self.meta("sales_seq") or "0") + 1
        self._db.execute("BEGIN")
        try:
            self._db.execute("UPDATE orders SET status = 'paid', skrill_transaction = ?, payer_email = ?, paid_at = ?, licence_id = ?,"
                             " licence = ?, receipt_no = ?, seq = ? WHERE id = ?",
                             (skrill_transaction, payer_email, now_iso(), licence_id, licence, receipt, seq, order_id))
            self.set_meta("receipt_no", str(receipt))
            self.set_meta("sales_seq", str(seq))
            self._db.execute("COMMIT")
        except Exception:
            self._db.execute("ROLLBACK")
            raise
        order = self.get_order(order_id)
        assert order is not None
        return order

    def sales_since(self, seq: int, limit: int = 500) -> list[tuple[int, Order]]:
        rows = self._db.execute("SELECT * FROM orders WHERE status = 'paid' AND seq > ? ORDER BY seq LIMIT ?", (seq, limit)).fetchall()
        return [(row["seq"], self._order(row)) for row in rows]
