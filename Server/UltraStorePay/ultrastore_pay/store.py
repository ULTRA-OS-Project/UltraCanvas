"""The orders and the payments, on the service's database (db.py:
PostgreSQL in production, SQLite for tests and development).

An order is one purchase: made when the store asks for a checkout, paid
when Skrill's status report says so, and then the carrier of the licence.
A payment is one movement of money on an order: the sale, each renewal of
a subscription, a refund, a chargeback - each with its own receipt number,
and the sales and the renewals numbered again in the sequence the admin
backend fetches (`seq`), the refunds and the chargebacks in theirs
(`reversal_seq`).

Version: 0.2.0 - payments, subscriptions (cycle, paid_until, valid_until),
                 refunds and chargebacks (revoked_at), schema 2
Version: 0.1.0
"""

from __future__ import annotations

import secrets
import time
from dataclasses import dataclass, field

from .db import Database

SCHEMA = 2


def now_iso() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


@dataclass
class Payment:
    id: int
    order_id: str
    kind: str                # sale, renewal, refund, chargeback
    skrill_transaction: str
    amount: str              # decimal, always positive; the kind says the direction
    currency: str
    at: str
    receipt_no: int = 0
    seq: int = 0             # the sales feed (sale, renewal)
    reversal_seq: int = 0    # the reversals feed (refund, chargeback)
    note: str = ""
    reverses: str = ""       # a refund or a chargeback: the Skrill transaction of the sale or renewal it takes back
    revoking: bool = False   # the reversal that took the licence back

    @property
    def reference(self) -> str:
        """What the admin's ledger records the payment under: the order for
        the sale, the order and the payment for a renewal."""
        return self.order_id if self.kind == "sale" else f"{self.order_id}/{self.id}"

    def public(self) -> dict:
        return {"kind": self.kind, "at": self.at, "amount": self.amount, "currency": self.currency, "receipt_no": self.receipt_no,
                "skrill_transaction": self.skrill_transaction}


@dataclass
class Order:
    id: str
    secret: str
    created_at: str
    app: str
    app_name: str
    amount: str          # what the buyer pays (the first payment of a subscription), decimal
    currency: str
    list_price: str      # before the code; a subscription's renewal amount
    code: str            # normalized, or ""
    code_applied: bool
    status: str          # open, pending, paid, cancelled, failed, chargeback, refunded
    skrill_transaction: str = ""
    payer_email: str = ""
    paid_at: str = ""
    licence_id: str = ""
    licence: str = ""
    receipt_no: int = 0
    notes: str = ""
    cycle: str = ""                  # "" for one purchase; "month" or "year" for a subscription
    paid_until: str = ""             # a subscription: the day the paid period ends
    valid_until: str = ""            # a subscription: paid_until plus the grace days, as the licence carries it
    rec_payment_id: str = ""         # Skrill's id of the recurring payment
    subscription_status: str = ""    # active, failing, cancelled
    revoked_at: str = ""             # a refund or a chargeback took the licence back
    revoked_reason: str = ""         # refund, chargeback
    refunded: str = ""               # the amount refunded so far, decimal, "" for none
    payments: list[Payment] = field(default_factory=list)

    @property
    def revoked(self) -> bool:
        return bool(self.revoked_at)

    @property
    def subscription(self) -> bool:
        return bool(self.cycle)

    def public(self) -> dict:
        out = {"order_id": self.id, "app": self.app, "app_name": self.app_name, "amount": self.amount, "currency": self.currency,
               "list_price": self.list_price, "code": self.code, "code_applied": self.code_applied, "status": self.status,
               "created_at": self.created_at, "cycle": self.cycle}
        if self.paid_at:
            out.update({"paid_at": self.paid_at, "licence_id": self.licence_id, "receipt_no": self.receipt_no})
        if self.status == "paid":
            out["licence"] = self.licence
        if self.cycle:
            out.update({"paid_until": self.paid_until, "valid_until": self.valid_until, "subscription_status": self.subscription_status})
        if self.revoked_at:
            out.update({"revoked": True, "revoked_at": self.revoked_at, "revoked_reason": self.revoked_reason})
        if self.refunded:
            out["refunded"] = self.refunded
        if self.payments:
            out["payments"] = [p.public() for p in self.payments]
        return out


class Store:
    def __init__(self, url: str) -> None:
        self._db = Database(url)
        self._db.script([
            "CREATE TABLE IF NOT EXISTS meta (key TEXT PRIMARY KEY, value TEXT NOT NULL)",
            "CREATE TABLE IF NOT EXISTS orders ("
            " id TEXT PRIMARY KEY, secret TEXT NOT NULL, created_at TEXT NOT NULL, app TEXT NOT NULL, app_name TEXT NOT NULL,"
            " amount TEXT NOT NULL, currency TEXT NOT NULL, list_price TEXT NOT NULL, code TEXT NOT NULL DEFAULT '',"
            " code_applied INTEGER NOT NULL DEFAULT 0, status TEXT NOT NULL, skrill_transaction TEXT NOT NULL DEFAULT '',"
            " payer_email TEXT NOT NULL DEFAULT '', paid_at TEXT NOT NULL DEFAULT '', licence_id TEXT NOT NULL DEFAULT '',"
            " licence TEXT NOT NULL DEFAULT '', receipt_no INTEGER NOT NULL DEFAULT 0, notes TEXT NOT NULL DEFAULT '',"
            " seq INTEGER NOT NULL DEFAULT 0)",
            "CREATE INDEX IF NOT EXISTS orders_seq ON orders (seq)",
            f"CREATE TABLE IF NOT EXISTS payments ("
            f" id {self._db.serial} PRIMARY KEY, order_id TEXT NOT NULL, kind TEXT NOT NULL, skrill_transaction TEXT NOT NULL DEFAULT '',"
            " amount TEXT NOT NULL, currency TEXT NOT NULL, at TEXT NOT NULL, receipt_no INTEGER NOT NULL DEFAULT 0,"
            " seq INTEGER NOT NULL DEFAULT 0, reversal_seq INTEGER NOT NULL DEFAULT 0, note TEXT NOT NULL DEFAULT '',"
            " reverses TEXT NOT NULL DEFAULT '', revoking INTEGER NOT NULL DEFAULT 0)",
            "CREATE INDEX IF NOT EXISTS payments_order ON payments (order_id)",
            "CREATE INDEX IF NOT EXISTS payments_seq ON payments (seq)",
            "CREATE INDEX IF NOT EXISTS payments_reversal_seq ON payments (reversal_seq)",
        ])
        self._upgrade()

    def _upgrade(self) -> None:
        """Schema 1 (orders with the sale on the row) to 2: the subscription
        and revocation columns, and one payment per paid order."""
        have = set(self._db.columns("orders"))
        for column, definition in (("cycle", "TEXT NOT NULL DEFAULT ''"), ("paid_until", "TEXT NOT NULL DEFAULT ''"),
                                   ("valid_until", "TEXT NOT NULL DEFAULT ''"), ("rec_payment_id", "TEXT NOT NULL DEFAULT ''"),
                                   ("subscription_status", "TEXT NOT NULL DEFAULT ''"), ("revoked_at", "TEXT NOT NULL DEFAULT ''"),
                                   ("revoked_reason", "TEXT NOT NULL DEFAULT ''"), ("refunded", "TEXT NOT NULL DEFAULT ''")):
            if column not in have:
                self._db.execute(f"ALTER TABLE orders ADD COLUMN {column} {definition}")
        if int(self.meta("schema") or "1") < 2:
            for row in self._db.fetchall("SELECT * FROM orders WHERE status = 'paid' AND seq > 0 ORDER BY seq"):
                self._db.execute("INSERT INTO payments (order_id, kind, skrill_transaction, amount, currency, at, receipt_no, seq)"
                                 " VALUES (?, 'sale', ?, ?, ?, ?, ?, ?)",
                                 (row["id"], row["skrill_transaction"], row["amount"], row["currency"], row["paid_at"], row["receipt_no"], row["seq"]))
            self.set_meta("schema", str(SCHEMA))

    @property
    def engine(self) -> str:
        return self._db.engine

    def close(self) -> None:
        self._db.close()

    # ----- meta -----
    def meta(self, key: str) -> str:
        row = self._db.fetchone("SELECT value FROM meta WHERE key = ?", (key,))
        return row["value"] if row else ""

    def set_meta(self, key: str, value: str) -> None:
        self._db.execute("INSERT INTO meta (key, value) VALUES (?, ?) ON CONFLICT (key) DO UPDATE SET value = excluded.value", (key, value))

    def _next(self, counter: str) -> int:
        """The next number of a counter kept in meta (receipt_no, sales_seq,
        reversal_seq), taken inside the caller's transaction."""
        value = int(self.meta(counter) or "0") + 1
        self.set_meta(counter, str(value))
        return value

    # ----- orders -----
    @staticmethod
    def _order(row: dict) -> Order:
        return Order(id=row["id"], secret=row["secret"], created_at=row["created_at"], app=row["app"], app_name=row["app_name"],
                     amount=row["amount"], currency=row["currency"], list_price=row["list_price"], code=row["code"],
                     code_applied=bool(row["code_applied"]), status=row["status"], skrill_transaction=row["skrill_transaction"],
                     payer_email=row["payer_email"], paid_at=row["paid_at"], licence_id=row["licence_id"], licence=row["licence"],
                     receipt_no=row["receipt_no"], notes=row["notes"], cycle=row["cycle"], paid_until=row["paid_until"],
                     valid_until=row["valid_until"], rec_payment_id=row["rec_payment_id"], subscription_status=row["subscription_status"],
                     revoked_at=row["revoked_at"], revoked_reason=row["revoked_reason"], refunded=row["refunded"])

    @staticmethod
    def _payment(row: dict) -> Payment:
        return Payment(id=row["id"], order_id=row["order_id"], kind=row["kind"], skrill_transaction=row["skrill_transaction"],
                       amount=row["amount"], currency=row["currency"], at=row["at"], receipt_no=row["receipt_no"], seq=row["seq"],
                       reversal_seq=row["reversal_seq"], note=row["note"], reverses=row["reverses"], revoking=bool(row["revoking"]))

    def create_order(self, *, app: str, app_name: str, amount: str, currency: str, list_price: str, code: str, code_applied: bool,
                     cycle: str = "") -> Order:
        order = Order(id=secrets.token_hex(8), secret=secrets.token_urlsafe(24), created_at=now_iso(), app=app, app_name=app_name,
                      amount=amount, currency=currency, list_price=list_price, code=code, code_applied=code_applied, status="open", cycle=cycle)
        self._db.execute(
            "INSERT INTO orders (id, secret, created_at, app, app_name, amount, currency, list_price, code, code_applied, status, cycle)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (order.id, order.secret, order.created_at, app, app_name, amount, currency, list_price, code, int(code_applied), "open", cycle))
        return order

    def get_order(self, order_id: str) -> Order | None:
        row = self._db.fetchone("SELECT * FROM orders WHERE id = ?", (order_id,))
        if not row:
            return None
        order = self._order(row)
        order.payments = self.payments_of(order_id)
        return order

    def order_of_licence(self, licence_id: str) -> Order | None:
        row = self._db.fetchone("SELECT * FROM orders WHERE licence_id = ?", (licence_id,))
        return self._order(row) if row else None

    def set_status(self, order_id: str, status: str, *, skrill_transaction: str = "", payer_email: str = "", note: str = "") -> None:
        self._db.execute("UPDATE orders SET status = ?, skrill_transaction = COALESCE(NULLIF(?, ''), skrill_transaction),"
                         " payer_email = COALESCE(NULLIF(?, ''), payer_email), notes = CASE WHEN ? = '' THEN notes ELSE notes || ? || ? END"
                         " WHERE id = ?", (status, skrill_transaction, payer_email, note, note, "\n", order_id))

    def note(self, order_id: str, note: str) -> None:
        self._db.execute("UPDATE orders SET notes = notes || ? || ? WHERE id = ?", (note, "\n", order_id))

    def mark_paid(self, order_id: str, *, skrill_transaction: str, payer_email: str, licence_id: str, licence: str,
                  paid_until: str = "", valid_until: str = "", rec_payment_id: str = "") -> Order:
        """Paid: the sale recorded with the next receipt number and the next
        sales sequence; a subscription active until its first period ends."""
        self._db.begin()
        try:
            receipt, seq, at = self._next("receipt_no"), self._next("sales_seq"), now_iso()
            order = self.get_order(order_id)
            assert order is not None
            self._db.execute("UPDATE orders SET status = 'paid', skrill_transaction = ?, payer_email = ?, paid_at = ?, licence_id = ?,"
                             " licence = ?, receipt_no = ?, seq = ?, paid_until = ?, valid_until = ?, rec_payment_id = ?,"
                             " subscription_status = ? WHERE id = ?",
                             (skrill_transaction, payer_email, at, licence_id, licence, receipt, seq, paid_until, valid_until,
                              rec_payment_id, "active" if order.cycle else "", order_id))
            self._db.execute("INSERT INTO payments (order_id, kind, skrill_transaction, amount, currency, at, receipt_no, seq)"
                             " VALUES (?, 'sale', ?, ?, ?, ?, ?, ?)", (order_id, skrill_transaction, order.amount, order.currency, at, receipt, seq))
            self._db.commit()
        except Exception:
            self._db.rollback()
            raise
        return self.get_order(order_id)   # type: ignore[return-value]

    def record_renewal(self, order_id: str, *, skrill_transaction: str, amount: str, currency: str, paid_until: str, valid_until: str,
                       licence: str) -> Order:
        """A renewal paid: the period extended, the licence re-issued, the
        payment numbered as a sale for the admin backend."""
        self._db.begin()
        try:
            receipt, seq, at = self._next("receipt_no"), self._next("sales_seq"), now_iso()
            self._db.execute("UPDATE orders SET paid_until = ?, valid_until = ?, licence = ?, subscription_status = 'active' WHERE id = ?",
                             (paid_until, valid_until, licence, order_id))
            self._db.execute("INSERT INTO payments (order_id, kind, skrill_transaction, amount, currency, at, receipt_no, seq)"
                             " VALUES (?, 'renewal', ?, ?, ?, ?, ?, ?)", (order_id, skrill_transaction, amount, currency, at, receipt, seq))
            self._db.commit()
        except Exception:
            self._db.rollback()
            raise
        return self.get_order(order_id)   # type: ignore[return-value]

    def set_subscription(self, order_id: str, status: str, *, rec_payment_id: str = "", note: str = "") -> None:
        self._db.execute("UPDATE orders SET subscription_status = ?, rec_payment_id = COALESCE(NULLIF(?, ''), rec_payment_id),"
                         " notes = CASE WHEN ? = '' THEN notes ELSE notes || ? || ? END WHERE id = ?",
                         (status, rec_payment_id, note, note, "\n", order_id))

    def record_reversal(self, order_id: str, kind: str, *, skrill_transaction: str, reverses: str, amount: str, currency: str, refunded: str,
                        revoke: bool, note: str = "") -> Order:
        """A refund (kind "refund", by us) or a chargeback (kind "chargeback",
        by the buyer's bank): the payment numbered in the reversals feed, the
        amount refunded so far on the order, and - for a full refund or a
        chargeback - the licence taken back and the order's status the kind."""
        self._db.begin()
        try:
            receipt, seq, at = self._next("receipt_no"), self._next("reversal_seq"), now_iso()
            self._db.execute("INSERT INTO payments (order_id, kind, skrill_transaction, reverses, amount, currency, at, receipt_no, reversal_seq, note, revoking)"
                             " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                             (order_id, kind, skrill_transaction, reverses, amount, currency, at, receipt, seq, note, int(revoke)))
            if revoke:
                self._db.execute("UPDATE orders SET status = ?, refunded = ?, revoked_at = ?, revoked_reason = ?,"
                                 " subscription_status = CASE WHEN cycle = '' THEN '' ELSE 'cancelled' END WHERE id = ?",
                                 ("refunded" if kind == "refund" else kind, refunded, at, kind, order_id))
            else:
                self._db.execute("UPDATE orders SET refunded = ? WHERE id = ?", (refunded, order_id))
            self._db.commit()
        except Exception:
            self._db.rollback()
            raise
        return self.get_order(order_id)   # type: ignore[return-value]

    # ----- payments -----
    def payments_of(self, order_id: str) -> list[Payment]:
        return [self._payment(r) for r in self._db.fetchall("SELECT * FROM payments WHERE order_id = ? ORDER BY id", (order_id,))]

    def payment_by_transaction(self, skrill_transaction: str) -> Payment | None:
        if not skrill_transaction:
            return None
        row = self._db.fetchone("SELECT * FROM payments WHERE skrill_transaction = ?", (skrill_transaction,))
        return self._payment(row) if row else None

    def sales_since(self, seq: int, limit: int = 500) -> list[tuple[Payment, Order]]:
        rows = self._db.fetchall("SELECT * FROM payments WHERE seq > ? ORDER BY seq LIMIT ?", (seq, limit))
        return [(self._payment(r), self.get_order(r["order_id"])) for r in rows]   # type: ignore[misc]

    def reversals_since(self, seq: int, limit: int = 500) -> list[tuple[Payment, Order]]:
        rows = self._db.fetchall("SELECT * FROM payments WHERE reversal_seq > ? ORDER BY reversal_seq LIMIT ?", (seq, limit))
        return [(self._payment(r), self.get_order(r["order_id"])) for r in rows]   # type: ignore[misc]
