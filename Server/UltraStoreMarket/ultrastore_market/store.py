"""The operator's records, on the service's database (ultrastore_db: MySQL
or PostgreSQL in production, SQLite for tests and development only).

The merchants and where they stand with the terms, the submissions with
every decision appended, the reports from the three doors, the notices and
the strikes they count, the suspensions, the terms versions and the
acceptances, the enforcement policy, and the audit log - append-only,
hash-chained, never edited (the store's README, *The operator tier*).

Version: 0.5.0 - the review pipeline's findings on a submission (schema 4)
Version: 0.4.0 - the packages uploaded into the package store; the operators' second factor and reset codes
Version: 0.3.0 - the operators with their roles
Version: 0.2.0 - the applications, the merchant's website and description (schema 2, the columns added to a 0.1 database)
Version: 0.1.0
"""

from __future__ import annotations

import hashlib
import json
import secrets
import time
from dataclasses import dataclass, field
from typing import Any

from ultrastore_db import Database
from ultrastore_db.totp import hash_code

SCHEMA = 4

DEFAULT_POLICY: dict[str, Any] = {
    "strike_expiry_days": 365,          # a warning counts as a strike this long
    "strikes_before_suspension": 3,     # unexpired strikes that suspend the merchant
    "suspension_days": 30,              # how long a suspension by strikes lasts
    "answer_deadline_days": 14,         # an information request is answered within this
    "clauses_that_suspend": [],         # a warning naming one of these suspends at once
}

SUBMISSION_STATES = ("received", "checked", "failed_checks", "in_review", "changes_requested", "rejected", "released", "withdrawn")
REPORT_STATES = ("open", "assigned", "resolved", "dismissed")
NOTICE_KINDS = ("announcement", "reminder", "information_request", "warning", "suspension")


def now_iso() -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime())


def today() -> str:
    return now_iso()[:10]


def iso_after(seconds: int) -> str:
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(time.time() + seconds))


def package_file_name(p: dict) -> str:
    """`<sha256>.<format>` for a package a submission names: the format from
    the entry, else from the file or URL it names."""
    sha = str(p.get("sha256", "")).lower()
    fmt = str(p.get("format", "")).lower()
    if not fmt:
        name = str(p.get("file") or p.get("url") or "").rsplit("/", 1)[-1].lower()
        for known in ("tar.zst", "tar.gz", "tar.xz", "tgz", "zip", "7z", "appimage"):
            if name.endswith("." + known):
                fmt = known
                break
    return sha + "." + (fmt or "bin")


def package_restricted(p: dict) -> bool:
    """A paid app's package without a trial: served to nobody directly."""
    return str(p.get("access", "")) == "restricted" or "restricted/" in str(p.get("file") or p.get("url") or "")


def add_days(day: str, days: int) -> str:
    import datetime as dt
    return (dt.date.fromisoformat(day) + dt.timedelta(days=days)).isoformat()


def canonical(value: Any) -> str:
    """One text for a JSON value, the way a signature covers it."""
    return json.dumps(value, separators=(",", ":"), sort_keys=True, ensure_ascii=False)


def _json(text: str, default: Any) -> Any:
    try:
        return json.loads(text) if text else default
    except ValueError:
        return default


@dataclass
class Merchant:
    id: str
    name: str
    contact: str
    key_hex: str
    created_at: str
    notes: str = ""
    website: str = ""               # the publisher's page the catalog names
    description: str = ""           # a line about the publisher, the merchant's own
    standing: str = "active"        # computed: active, paused (terms), suspended
    strikes: int = 0                # computed
    terms_in_force: int = 0         # computed
    terms_accepted: int = 0         # computed: the newest version accepted

    def public(self) -> dict:
        return {"id": self.id, "name": self.name, "contact": self.contact, "website": self.website, "description": self.description, "key": self.key_hex,
                "created_at": self.created_at, "standing": self.standing, "strikes": self.strikes,
                "terms": {"in_force": self.terms_in_force, "accepted": self.terms_accepted}}


APPLICATION_STATES = ("applied", "approved", "rejected")


@dataclass
class Operator:
    """A person at the operator's window, with a role (ultrastore_db.operator_session)."""
    id: str
    email: str
    name: str
    role: str
    password_hash: str
    created_at: str
    created_by: str
    disabled_at: str
    last_signin: str
    totp_secret: str = ""        # set at setup, in force once totp_enabled_at is set
    totp_enabled_at: str = ""
    recovery_codes: list = field(default_factory=list)   # hashed, each good once

    @property
    def active(self) -> bool:
        return not self.disabled_at

    @property
    def two_factor(self) -> bool:
        return bool(self.totp_enabled_at)

    def public(self) -> dict:
        return {"id": self.id, "email": self.email, "name": self.name, "role": self.role, "created_at": self.created_at, "created_by": self.created_by,
                "disabled_at": self.disabled_at, "last_signin": self.last_signin, "active": self.active, "two_factor": self.two_factor,
                "recovery_codes_left": len(self.recovery_codes)}


@dataclass
class Application:
    """A merchant's request to open a shop: what they said of themselves, the
    key that will sign their submissions (proven by the signature over the
    application), and the operator's decision."""
    id: str
    name: str
    contact: str
    email: str
    website: str
    description: str
    key_hex: str
    state: str
    message: str
    secret: str
    created_at: str
    decided_at: str
    decided_by: str
    merchant_id: str

    def public(self, *, for_operator: bool = False) -> dict:
        out = {"id": self.id, "name": self.name, "contact": self.contact, "website": self.website, "description": self.description, "key": self.key_hex,
               "state": self.state, "message": self.message, "created_at": self.created_at, "decided_at": self.decided_at, "merchant": self.merchant_id}
        if for_operator:
            out.update({"email": self.email, "decided_by": self.decided_by})
        return out


@dataclass
class Submission:
    id: str
    merchant_id: str
    app_id: str
    version: str
    component: dict
    signature: str
    packages: list
    checks: dict
    state: str
    answers: str
    created_at: str
    decided_at: str = ""
    released_sequence: int = 0
    decisions: list[dict] = field(default_factory=list)
    findings: dict = field(default_factory=dict)   # check name -> the pipeline's lines (checks.py)
    checked_at: str = ""                           # when the pipeline last ran

    def public(self, *, for_merchant: bool = False) -> dict:
        out = {"id": self.id, "merchant": self.merchant_id, "app": self.app_id, "version": self.version, "component": self.component,
               "signature": self.signature, "packages": self.packages, "checks": self.checks, "state": self.state, "answers": self.answers,
               "created_at": self.created_at, "decided_at": self.decided_at, "released_sequence": self.released_sequence,
               "decisions": self.decisions, "findings": self.findings, "checked_at": self.checked_at}
        return out


@dataclass
class Report:
    id: str
    source: str
    reporter: str
    reporter_merchant: str
    target_kind: str
    target: str
    kind: str
    text: str
    attachments: list
    state: str
    assignee: str
    findings: str
    outcome: str
    created_at: str
    resolved_at: str
    secret: str

    def public(self, *, for_merchant: bool = False) -> dict:
        out = {"id": self.id, "source": self.source, "target_kind": self.target_kind, "target": self.target, "kind": self.kind, "text": self.text,
               "attachments": self.attachments, "state": self.state, "created_at": self.created_at, "resolved_at": self.resolved_at,
               "outcome": self.outcome}
        if not for_merchant:
            # The reporter is the operator's to see, never the merchant's.
            out.update({"reporter": self.reporter, "reporter_merchant": self.reporter_merchant, "assignee": self.assignee, "findings": self.findings})
        return out


@dataclass
class Notice:
    id: str
    kind: str
    subject: str
    text: str
    clause: str
    recipients: list
    created_at: str
    created_by: str
    expires_at: str
    deadline: str
    acknowledged_at: str = ""   # for one merchant's view

    def addressed_to(self, merchant_id: str) -> bool:
        return "*" in self.recipients or merchant_id in self.recipients

    def public(self) -> dict:
        return {"id": self.id, "kind": self.kind, "subject": self.subject, "text": self.text, "clause": self.clause, "recipients": self.recipients,
                "created_at": self.created_at, "created_by": self.created_by, "expires_at": self.expires_at, "deadline": self.deadline,
                "acknowledged_at": self.acknowledged_at}


@dataclass
class Suspension:
    id: int
    scope: str       # merchant, app
    target: str
    clause: str
    reason: str
    start: str
    end_at: str      # "" until reinstated
    lifted_at: str
    created_by: str

    def active_on(self, day: str) -> bool:
        return not self.lifted_at and (not self.end_at or self.end_at >= day)

    def public(self) -> dict:
        return {"id": self.id, "scope": self.scope, "target": self.target, "clause": self.clause, "reason": self.reason, "start": self.start,
                "end": self.end_at, "lifted_at": self.lifted_at, "created_by": self.created_by}


class MarketStore:
    def __init__(self, url: str) -> None:
        db = self._db = Database(url)
        self._key = db.ident("key")
        tk = db.text_key
        db.script([
            f"CREATE TABLE IF NOT EXISTS meta ({self._key} {tk} PRIMARY KEY, value TEXT NOT NULL)",
            f"CREATE TABLE IF NOT EXISTS merchants (id {tk} PRIMARY KEY, name TEXT NOT NULL, contact TEXT NOT NULL DEFAULT '',"
            f" key_hex {tk} NOT NULL, created_at TEXT NOT NULL, notes TEXT NOT NULL DEFAULT '', website TEXT NOT NULL DEFAULT '',"
            " description TEXT NOT NULL DEFAULT '')",
            f"CREATE TABLE IF NOT EXISTS operators (id {tk} PRIMARY KEY, email {tk} NOT NULL, name TEXT NOT NULL DEFAULT '', role TEXT NOT NULL,"
            " password_hash TEXT NOT NULL DEFAULT '', created_at TEXT NOT NULL, created_by TEXT NOT NULL DEFAULT '', disabled_at TEXT NOT NULL DEFAULT '',"
            " last_signin TEXT NOT NULL DEFAULT '', totp_secret TEXT NOT NULL DEFAULT '', totp_enabled_at TEXT NOT NULL DEFAULT '',"
            " recovery_codes TEXT NOT NULL DEFAULT '[]')",
            f"CREATE TABLE IF NOT EXISTS operator_codes (id {db.serial} PRIMARY KEY, operator_id {tk} NOT NULL, purpose {tk} NOT NULL,"
            " code_hash TEXT NOT NULL, salt TEXT NOT NULL, expires_at TEXT NOT NULL, attempts INTEGER NOT NULL DEFAULT 0, used_at TEXT NOT NULL DEFAULT '',"
            " created_at TEXT NOT NULL)",
            f"CREATE TABLE IF NOT EXISTS applications (id {tk} PRIMARY KEY, name TEXT NOT NULL, contact TEXT NOT NULL DEFAULT '',"
            " email TEXT NOT NULL DEFAULT '', website TEXT NOT NULL DEFAULT '', description TEXT NOT NULL DEFAULT '',"
            f" key_hex {tk} NOT NULL, state TEXT NOT NULL, message TEXT NOT NULL DEFAULT '', secret TEXT NOT NULL DEFAULT '',"
            " created_at TEXT NOT NULL, decided_at TEXT NOT NULL DEFAULT '', decided_by TEXT NOT NULL DEFAULT '',"
            f" merchant_id {tk} NOT NULL DEFAULT '')",
            "CREATE TABLE IF NOT EXISTS terms_versions (version INTEGER PRIMARY KEY, text TEXT NOT NULL, changes TEXT NOT NULL DEFAULT '',"
            " in_force_from TEXT NOT NULL, created_at TEXT NOT NULL, created_by TEXT NOT NULL DEFAULT '')",
            f"CREATE TABLE IF NOT EXISTS terms_acceptances (id {db.serial} PRIMARY KEY, merchant_id {tk} NOT NULL, version INTEGER NOT NULL,"
            " accepted_by TEXT NOT NULL DEFAULT '', accepted_at TEXT NOT NULL)",
            f"CREATE TABLE IF NOT EXISTS submissions (id {tk} PRIMARY KEY, merchant_id {tk} NOT NULL, app_id {tk} NOT NULL,"
            " version TEXT NOT NULL DEFAULT '', component TEXT NOT NULL, signature TEXT NOT NULL DEFAULT '', packages TEXT NOT NULL DEFAULT '[]',"
            f" checks TEXT NOT NULL DEFAULT '{{}}', state TEXT NOT NULL, answers {tk} NOT NULL DEFAULT '', created_at TEXT NOT NULL,"
            " decided_at TEXT NOT NULL DEFAULT '', released_sequence INTEGER NOT NULL DEFAULT 0)",
            f"CREATE TABLE IF NOT EXISTS decisions (id {db.serial} PRIMARY KEY, submission_id {tk} NOT NULL, action TEXT NOT NULL,"
            " message TEXT NOT NULL DEFAULT '', actor TEXT NOT NULL DEFAULT '', at TEXT NOT NULL, sequence INTEGER NOT NULL DEFAULT 0)",
            f"CREATE TABLE IF NOT EXISTS reports (id {tk} PRIMARY KEY, source TEXT NOT NULL, reporter TEXT NOT NULL DEFAULT '',"
            f" reporter_merchant {tk} NOT NULL DEFAULT '', target_kind TEXT NOT NULL, target {tk} NOT NULL, kind TEXT NOT NULL,"
            " text TEXT NOT NULL, attachments TEXT NOT NULL DEFAULT '[]', state TEXT NOT NULL, assignee TEXT NOT NULL DEFAULT '',"
            " findings TEXT NOT NULL DEFAULT '', outcome TEXT NOT NULL DEFAULT '', created_at TEXT NOT NULL, resolved_at TEXT NOT NULL DEFAULT '',"
            " secret TEXT NOT NULL DEFAULT '')",
            f"CREATE TABLE IF NOT EXISTS notices (id {tk} PRIMARY KEY, kind TEXT NOT NULL, subject TEXT NOT NULL, text TEXT NOT NULL,"
            " clause TEXT NOT NULL DEFAULT '', recipients TEXT NOT NULL DEFAULT '[]', created_at TEXT NOT NULL, created_by TEXT NOT NULL DEFAULT '',"
            " expires_at TEXT NOT NULL DEFAULT '', deadline TEXT NOT NULL DEFAULT '')",
            f"CREATE TABLE IF NOT EXISTS notice_acks (id {db.serial} PRIMARY KEY, notice_id {tk} NOT NULL, merchant_id {tk} NOT NULL,"
            " acknowledged_at TEXT NOT NULL)",
            f"CREATE TABLE IF NOT EXISTS suspensions (id {db.serial} PRIMARY KEY, scope TEXT NOT NULL, target {tk} NOT NULL,"
            " clause TEXT NOT NULL DEFAULT '', reason TEXT NOT NULL DEFAULT '', start TEXT NOT NULL, end_at TEXT NOT NULL DEFAULT '',"
            " lifted_at TEXT NOT NULL DEFAULT '', created_by TEXT NOT NULL DEFAULT '')",
            f"CREATE TABLE IF NOT EXISTS audit (id {db.serial} PRIMARY KEY, at TEXT NOT NULL, actor TEXT NOT NULL, action TEXT NOT NULL,"
            f" target_kind TEXT NOT NULL, target {tk} NOT NULL, clause TEXT NOT NULL DEFAULT '', state_before TEXT NOT NULL DEFAULT '',"
            " state_after TEXT NOT NULL DEFAULT '', acted_on TEXT NOT NULL DEFAULT '', prev_hash TEXT NOT NULL, hash TEXT NOT NULL)",
        ])
        for name, table, cols in (("submissions_merchant", "submissions", "merchant_id"), ("submissions_app", "submissions", "app_id"),
                                  ("decisions_submission", "decisions", "submission_id"), ("reports_target", "reports", "target"),
                                  ("notice_acks_notice", "notice_acks", "notice_id, merchant_id"), ("suspensions_target", "suspensions", "target"),
                                  ("acceptances_merchant", "terms_acceptances", "merchant_id")):
            db.create_index(name, table, cols)
        # A database from 0.2: the profile columns the merchant edits itself.
        have = db.columns("merchants")
        for column in ("website", "description"):
            if column not in have:
                db.execute(f"ALTER TABLE merchants ADD COLUMN {column} TEXT NOT NULL DEFAULT ''")
        db.create_index("applications_key", "applications", "key_hex")
        db.create_index("operators_email", "operators", "email")
        db.create_index("operator_codes_operator", "operator_codes", "operator_id, purpose")
        # A database from 0.4: the pipeline's findings on a submission.
        have = db.columns("submissions")
        for column, default in (("findings", "'{}'"), ("checked_at", "''")):
            if column not in have:
                db.execute(f"ALTER TABLE submissions ADD COLUMN {column} TEXT NOT NULL DEFAULT {default}")
        # A database from 0.4: the operators' second factor.
        have = db.columns("operators")
        for column, default in (("totp_secret", "''"), ("totp_enabled_at", "''"), ("recovery_codes", "'[]'")):
            if column not in have:
                db.execute(f"ALTER TABLE operators ADD COLUMN {column} TEXT NOT NULL DEFAULT {default}")
        if not self.meta("schema"):
            self.set_meta("schema", str(SCHEMA))

    @property
    def engine(self) -> str:
        return self._db.engine

    def close(self) -> None:
        self._db.close()

    # ----- meta, the policy -----
    def meta(self, key: str) -> str:
        row = self._db.fetchone(f"SELECT value FROM meta WHERE {self._key} = ?", (key,))
        return row["value"] if row else ""

    def set_meta(self, key: str, value: str) -> None:
        self._db.execute(self._db.upsert("meta", self._key, "value"), (key, value))

    def next_number(self, counter: str) -> int:
        value = int(self.meta(counter) or "0") + 1
        self.set_meta(counter, str(value))
        return value

    def policy(self) -> dict[str, Any]:
        return {**DEFAULT_POLICY, **_json(self.meta("policy"), {})}

    def set_policy(self, policy: dict[str, Any], actor: str) -> dict[str, Any]:
        before = self.policy()
        merged = {**DEFAULT_POLICY, **{k: v for k, v in policy.items() if k in DEFAULT_POLICY}}
        self.set_meta("policy", canonical(merged))
        self.audit(actor, "policy", "policy", "enforcement", before=before, after=merged)
        return merged

    # ----- the audit log -----
    @staticmethod
    def _audit_hash(prev_hash: str, row: dict) -> str:
        """The row's hash: over its fields (the state before and after as
        their JSON texts) and the hash of the row before it."""
        fields = {k: row[k] for k in ("at", "actor", "action", "target_kind", "target", "clause", "acted_on")}
        fields["before"], fields["after"] = row["state_before"], row["state_after"]
        return hashlib.sha256((prev_hash + "\n" + canonical(fields)).encode()).hexdigest()

    def audit(self, actor: str, action: str, target_kind: str, target: str, *, clause: str = "", before: Any = None, after: Any = None,
              acted_on: str = "") -> int:
        """One row appended, chained to the one before it."""
        last = self._db.fetchone("SELECT hash FROM audit ORDER BY id DESC LIMIT 1")
        prev = last["hash"] if last else ""
        row = {"at": now_iso(), "actor": actor, "action": action, "target_kind": target_kind, "target": target, "clause": clause,
               "state_before": canonical(before) if before is not None else "", "state_after": canonical(after) if after is not None else "",
               "acted_on": acted_on}
        row_hash = self._audit_hash(prev, row)
        self._db.execute("INSERT INTO audit (at, actor, action, target_kind, target, clause, state_before, state_after, acted_on, prev_hash, hash)"
                         " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                         (row["at"], actor, action, target_kind, target, clause, row["state_before"], row["state_after"], acted_on, prev, row_hash))
        return self._db.last_id()

    @staticmethod
    def _audit_row(r: dict) -> dict:
        return {"id": r["id"], "at": r["at"], "actor": r["actor"], "action": r["action"], "target_kind": r["target_kind"], "target": r["target"],
                "clause": r["clause"], "before": _json(r["state_before"], None), "after": _json(r["state_after"], None), "acted_on": r["acted_on"],
                "prev_hash": r["prev_hash"], "hash": r["hash"]}

    def audit_since(self, since: int, limit: int = 500) -> list[dict]:
        return [self._audit_row(r) for r in self._db.fetchall("SELECT * FROM audit WHERE id > ? ORDER BY id LIMIT ?", (since, limit))]

    def audit_of(self, targets: list[str], limit: int = 500) -> list[dict]:
        """The rows about these targets (a merchant, its apps, its submissions)."""
        if not targets:
            return []
        marks = ", ".join("?" for _ in targets)
        return [self._audit_row(r) for r in self._db.fetchall(f"SELECT * FROM audit WHERE target IN ({marks}) ORDER BY id LIMIT ?", (*targets, limit))]

    def verify_audit(self) -> dict:
        """Walks the chain: the first row whose hash or link is wrong, or
        all of them good."""
        prev = ""
        count = 0
        for r in self._db.fetchall("SELECT * FROM audit ORDER BY id"):
            if r["prev_hash"] != prev or self._audit_hash(prev, r) != r["hash"]:
                return {"ok": False, "rows": count, "broken_at": r["id"]}
            prev = r["hash"]
            count += 1
        return {"ok": True, "rows": count, "last_hash": prev}

    # ----- merchants, terms, standing -----
    def _merchant(self, r: dict) -> Merchant:
        m = Merchant(id=r["id"], name=r["name"], contact=r["contact"], key_hex=r["key_hex"], created_at=r["created_at"], notes=r["notes"],
                     website=r.get("website", ""), description=r.get("description", ""))
        day = today()
        m.strikes = self.strikes(m.id, day)
        m.terms_in_force = self.terms_in_force(day)
        m.terms_accepted = self.terms_accepted(m.id)
        if any(s.active_on(day) for s in self.suspensions_of("merchant", m.id)):
            m.standing = "suspended"
        elif m.terms_in_force and m.terms_accepted < m.terms_in_force:
            m.standing = "paused"
        else:
            m.standing = "active"
        return m

    def add_merchant(self, *, id: str, name: str, contact: str, key_hex: str, actor: str, website: str = "", description: str = "") -> Merchant:
        self._db.execute("INSERT INTO merchants (id, name, contact, key_hex, created_at, website, description) VALUES (?, ?, ?, ?, ?, ?, ?)",
                         (id, name, contact, key_hex, now_iso(), website, description))
        self.audit(actor, "register", "merchant", id, after={"name": name, "contact": contact, "key": key_hex, "website": website})
        return self.get_merchant(id)   # type: ignore[return-value]

    def update_merchant(self, id: str, *, name: str | None, contact: str | None, key_hex: str | None, actor: str, website: str | None = None,
                        description: str | None = None) -> Merchant | None:
        before = self.get_merchant(id)
        if not before:
            return None
        new = {"name": name if name is not None else before.name, "contact": contact if contact is not None else before.contact,
               "key": key_hex if key_hex is not None else before.key_hex, "website": website if website is not None else before.website,
               "description": description if description is not None else before.description}
        self._db.execute("UPDATE merchants SET name = ?, contact = ?, key_hex = ?, website = ?, description = ? WHERE id = ?",
                         (new["name"], new["contact"], new["key"], new["website"], new["description"], id))
        self.audit(actor, "update", "merchant", id, before={"name": before.name, "contact": before.contact, "key": before.key_hex, "website": before.website,
                                                            "description": before.description}, after=new)
        return self.get_merchant(id)

    def merchant_by_key(self, key_hex: str) -> Merchant | None:
        r = self._db.fetchone("SELECT * FROM merchants WHERE key_hex = ?", (key_hex,))
        return self._merchant(r) if r else None

    # ----- the operators -----
    @staticmethod
    def _operator(r: dict) -> Operator:
        return Operator(id=r["id"], email=r["email"], name=r["name"], role=r["role"], password_hash=r["password_hash"], created_at=r["created_at"],
                        created_by=r["created_by"], disabled_at=r["disabled_at"], last_signin=r["last_signin"], totp_secret=r.get("totp_secret", ""),
                        totp_enabled_at=r.get("totp_enabled_at", ""), recovery_codes=_json(r.get("recovery_codes", "[]"), []))

    def operators(self) -> list[Operator]:
        return [self._operator(r) for r in self._db.fetchall("SELECT * FROM operators ORDER BY created_at, id")]

    def get_operator(self, op_id: str) -> Operator | None:
        r = self._db.fetchone("SELECT * FROM operators WHERE id = ?", (op_id,))
        return self._operator(r) if r else None

    def operator_by_email(self, email: str) -> Operator | None:
        r = self._db.fetchone("SELECT * FROM operators WHERE email = ?", (email.strip().lower(),))
        return self._operator(r) if r else None

    def add_operator(self, *, email: str, name: str, role: str, password_hash: str, actor: str) -> Operator:
        op_id = f"op-{self.next_number('operator_no'):04d}"
        self._db.execute("INSERT INTO operators (id, email, name, role, password_hash, created_at, created_by) VALUES (?, ?, ?, ?, ?, ?, ?)",
                         (op_id, email.strip().lower(), name, role, password_hash, now_iso(), actor))
        self.audit(actor, "add_operator", "operator", op_id, after={"email": email.strip().lower(), "name": name, "role": role})
        return self.get_operator(op_id)   # type: ignore[return-value]

    def update_operator(self, op_id: str, *, actor: str, name: str | None = None, role: str | None = None, password_hash: str | None = None,
                        disabled: bool | None = None) -> Operator | None:
        before = self.get_operator(op_id)
        if not before:
            return None
        new = {"name": name if name is not None else before.name, "role": role if role is not None else before.role,
               "password_hash": password_hash if password_hash is not None else before.password_hash,
               "disabled_at": (now_iso() if disabled else "") if disabled is not None else before.disabled_at}
        self._db.execute("UPDATE operators SET name = ?, role = ?, password_hash = ?, disabled_at = ? WHERE id = ?",
                         (new["name"], new["role"], new["password_hash"], new["disabled_at"], op_id))
        self.audit(actor, "update_operator", "operator", op_id, before={"name": before.name, "role": before.role, "disabled": not before.active},
                   after={"name": new["name"], "role": new["role"], "disabled": bool(new["disabled_at"]), "password_changed": password_hash is not None})
        return self.get_operator(op_id)

    def record_operator_signin(self, op_id: str) -> None:
        self._db.execute("UPDATE operators SET last_signin = ? WHERE id = ?", (now_iso(), op_id))

    def set_operator_two_factor(self, op_id: str, *, actor: str, secret: str | None = None, enabled: bool | None = None,
                                recovery_hashes: list[str] | None = None, audit_action: str = "") -> Operator | None:
        """The second factor's pieces: the secret at setup, enabled with the
        recovery codes, a new set of codes, or cleared (secret None and
        enabled False)."""
        op = self.get_operator(op_id)
        if not op:
            return None
        new_secret = op.totp_secret if secret is None else secret
        new_enabled = op.totp_enabled_at if enabled is None else (now_iso() if enabled else "")
        new_codes = op.recovery_codes if recovery_hashes is None else recovery_hashes
        if enabled is False and secret is None:
            new_secret, new_codes = "", []
        self._db.execute("UPDATE operators SET totp_secret = ?, totp_enabled_at = ?, recovery_codes = ? WHERE id = ?",
                         (new_secret, new_enabled, json.dumps(new_codes), op_id))
        if audit_action:
            self.audit(actor, audit_action, "operator", op_id, before={"two_factor": op.two_factor}, after={"two_factor": bool(new_enabled)})
        return self.get_operator(op_id)

    def use_operator_recovery_code(self, op_id: str, digest: str) -> bool:
        op = self.get_operator(op_id)
        if not op or digest not in op.recovery_codes:
            return False
        self._db.execute("UPDATE operators SET recovery_codes = ? WHERE id = ?", (json.dumps([c for c in op.recovery_codes if c != digest]), op_id))
        return True

    def set_operator_password(self, op_id: str, password_hash: str, *, actor: str, action: str = "reset_password") -> None:
        self._db.execute("UPDATE operators SET password_hash = ? WHERE id = ?", (password_hash, op_id))
        self.audit(actor, action, "operator", op_id)

    # ----- the operators' codes sent by mail -----
    def add_operator_code(self, op_id: str, purpose: str, code: str, *, minutes: int) -> None:
        self._db.execute("UPDATE operator_codes SET used_at = ? WHERE operator_id = ? AND purpose = ? AND used_at = ''", (now_iso() + " superseded", op_id, purpose))
        salt = secrets.token_hex(8)
        self._db.execute("INSERT INTO operator_codes (operator_id, purpose, code_hash, salt, expires_at, created_at) VALUES (?, ?, ?, ?, ?, ?)",
                         (op_id, purpose, hash_code(code, salt), salt, iso_after(minutes * 60), now_iso()))

    def take_operator_code(self, op_id: str, purpose: str, code: str) -> bool:
        """True when the code is the live one for this purpose, which is then
        used up; False when wrong, expired or tried too often."""
        r = self._db.fetchone("SELECT * FROM operator_codes WHERE operator_id = ? AND purpose = ? AND used_at = '' ORDER BY id DESC LIMIT 1", (op_id, purpose))
        if not r or r["expires_at"] < now_iso() or int(r["attempts"]) >= 5:
            return False
        if hash_code(code.strip(), r["salt"]) != r["code_hash"]:
            self._db.execute("UPDATE operator_codes SET attempts = attempts + 1 WHERE id = ?", (r["id"],))
            return False
        self._db.execute("UPDATE operator_codes SET used_at = ? WHERE id = ?", (now_iso(), r["id"]))
        return True

    # ----- applications -----
    @staticmethod
    def _application(r: dict) -> Application:
        return Application(id=r["id"], name=r["name"], contact=r["contact"], email=r["email"], website=r["website"], description=r["description"],
                           key_hex=r["key_hex"], state=r["state"], message=r["message"], secret=r["secret"], created_at=r["created_at"],
                           decided_at=r["decided_at"], decided_by=r["decided_by"], merchant_id=r["merchant_id"])

    def add_application(self, *, name: str, contact: str, email: str, website: str, description: str, key_hex: str) -> Application:
        aid = f"ap-{self.next_number('application_no'):06d}"
        secret = secrets.token_hex(16)
        self._db.execute("INSERT INTO applications (id, name, contact, email, website, description, key_hex, state, secret, created_at)"
                         " VALUES (?, ?, ?, ?, ?, ?, ?, 'applied', ?, ?)", (aid, name, contact, email, website, description, key_hex, secret, now_iso()))
        self.audit(aid, "apply", "application", aid, after={"name": name, "contact": contact, "website": website, "key": key_hex})
        return self.get_application(aid)   # type: ignore[return-value]

    def get_application(self, aid: str) -> Application | None:
        r = self._db.fetchone("SELECT * FROM applications WHERE id = ?", (aid,))
        return self._application(r) if r else None

    def applications(self, state: str = "") -> list[Application]:
        rows = self._db.fetchall("SELECT * FROM applications ORDER BY id")
        return [self._application(r) for r in rows if not state or r["state"] == state]

    def open_application_for_key(self, key_hex: str) -> Application | None:
        r = self._db.fetchone("SELECT * FROM applications WHERE key_hex = ? AND state = 'applied' ORDER BY id DESC LIMIT 1", (key_hex,))
        return self._application(r) if r else None

    def decide_application(self, aid: str, *, approve: bool, message: str, actor: str, merchant_id: str = "") -> Application:
        """Approved: the merchant registered with the application's key and
        profile. Rejected: the message is what the applicant learns."""
        a = self.get_application(aid)
        if not a:
            raise ValueError("no_such_application")
        if a.state != "applied":
            raise ValueError("decided")
        new_merchant = ""
        if approve:
            new_merchant = merchant_id or ("m-" + secrets.token_hex(4))
            if self.get_merchant(new_merchant):
                raise ValueError("merchant_exists")
            self.add_merchant(id=new_merchant, name=a.name, contact=a.contact, key_hex=a.key_hex, actor=actor, website=a.website, description=a.description)
        self._db.execute("UPDATE applications SET state = ?, message = ?, decided_at = ?, decided_by = ?, merchant_id = ? WHERE id = ?",
                         ("approved" if approve else "rejected", message, now_iso(), actor, new_merchant, aid))
        self.audit(actor, "approve" if approve else "reject", "application", aid, after={"message": message, "merchant": new_merchant})
        return self.get_application(aid)   # type: ignore[return-value]

    def get_merchant(self, id: str) -> Merchant | None:
        r = self._db.fetchone("SELECT * FROM merchants WHERE id = ?", (id,))
        return self._merchant(r) if r else None

    def merchants(self) -> list[Merchant]:
        return [self._merchant(r) for r in self._db.fetchall("SELECT * FROM merchants ORDER BY id")]

    def terms_versions(self) -> list[dict]:
        return [dict(r) for r in self._db.fetchall("SELECT * FROM terms_versions ORDER BY version")]

    def add_terms(self, *, text: str, changes: str, in_force_from: str, actor: str, version: int = 0) -> dict:
        if not version:
            last = self._db.fetchone("SELECT MAX(version) AS v FROM terms_versions")
            version = int(last["v"] or 0) + 1 if last else 1
        self._db.execute("INSERT INTO terms_versions (version, text, changes, in_force_from, created_at, created_by) VALUES (?, ?, ?, ?, ?, ?)",
                         (version, text, changes, in_force_from, now_iso(), actor))
        self.audit(actor, "terms", "terms", str(version), after={"in_force_from": in_force_from, "changes": changes})
        return dict(self._db.fetchone("SELECT * FROM terms_versions WHERE version = ?", (version,)) or {})

    def terms_in_force(self, day: str) -> int:
        r = self._db.fetchone("SELECT MAX(version) AS v FROM terms_versions WHERE in_force_from <= ?", (day,))
        return int(r["v"] or 0) if r else 0

    def terms_accepted(self, merchant_id: str) -> int:
        r = self._db.fetchone("SELECT MAX(version) AS v FROM terms_acceptances WHERE merchant_id = ?", (merchant_id,))
        return int(r["v"] or 0) if r else 0

    def accept_terms(self, merchant_id: str, version: int, accepted_by: str) -> dict:
        at = now_iso()
        self._db.execute("INSERT INTO terms_acceptances (merchant_id, version, accepted_by, accepted_at) VALUES (?, ?, ?, ?)",
                         (merchant_id, version, accepted_by, at))
        self.audit(merchant_id, "accept_terms", "merchant", merchant_id, after={"version": version, "accepted_by": accepted_by})
        return {"merchant": merchant_id, "version": version, "accepted_by": accepted_by, "accepted_at": at}

    def acceptances_of(self, merchant_id: str) -> list[dict]:
        return [dict(r) for r in self._db.fetchall("SELECT * FROM terms_acceptances WHERE merchant_id = ? ORDER BY id", (merchant_id,))]

    # ----- suspensions -----
    @staticmethod
    def _suspension(r: dict) -> Suspension:
        return Suspension(id=r["id"], scope=r["scope"], target=r["target"], clause=r["clause"], reason=r["reason"], start=r["start"], end_at=r["end_at"],
                          lifted_at=r["lifted_at"], created_by=r["created_by"])

    def suspensions_of(self, scope: str, target: str) -> list[Suspension]:
        return [self._suspension(r) for r in self._db.fetchall("SELECT * FROM suspensions WHERE scope = ? AND target = ? ORDER BY id", (scope, target))]

    def suspend(self, scope: str, target: str, *, clause: str, reason: str, days: int, actor: str, acted_on: str = "") -> Suspension:
        day = today()
        end = add_days(day, days) if days > 0 else ""
        self._db.execute("INSERT INTO suspensions (scope, target, clause, reason, start, end_at, created_by) VALUES (?, ?, ?, ?, ?, ?, ?)",
                         (scope, target, clause, reason, day, end, actor))
        sid = self._db.last_id()
        self.audit(actor, "suspend", scope, target, clause=clause, after={"reason": reason, "start": day, "end": end}, acted_on=acted_on or f"suspension:{sid}")
        return self._suspension(self._db.fetchone("SELECT * FROM suspensions WHERE id = ?", (sid,)))   # type: ignore[arg-type]

    def reinstate(self, scope: str, target: str, *, actor: str, reason: str = "") -> int:
        """Lifts every active suspension of the target; how many."""
        day = today()
        lifted = 0
        for s in self.suspensions_of(scope, target):
            if not s.active_on(day):
                continue
            row_id = self.audit(actor, "reinstate", scope, target, clause=s.clause, before=s.public(), after={"reason": reason}, acted_on=f"suspension:{s.id}")
            self._db.execute("UPDATE suspensions SET lifted_at = ? WHERE id = ?", (f"{now_iso()} audit:{row_id}", s.id))
            lifted += 1
        return lifted

    def app_suspended(self, app_id: str, day: str) -> bool:
        return any(s.active_on(day) for s in self.suspensions_of("app", app_id))

    # ----- notices and strikes -----
    @staticmethod
    def _notice(r: dict) -> Notice:
        return Notice(id=r["id"], kind=r["kind"], subject=r["subject"], text=r["text"], clause=r["clause"], recipients=_json(r["recipients"], []),
                      created_at=r["created_at"], created_by=r["created_by"], expires_at=r["expires_at"], deadline=r["deadline"])

    def add_notice(self, *, kind: str, subject: str, text: str, clause: str, recipients: list[str], actor: str) -> tuple[Notice, list[Suspension]]:
        """The notice written once; a warning counts as a strike for each
        recipient, and the policy may suspend them on it."""
        policy = self.policy()
        day = today()
        nid = f"n-{self.next_number('notice_no'):08d}"
        expires = add_days(day, int(policy["strike_expiry_days"])) if kind == "warning" else ""
        deadline = add_days(day, int(policy["answer_deadline_days"])) if kind == "information_request" else ""
        self._db.execute("INSERT INTO notices (id, kind, subject, text, clause, recipients, created_at, created_by, expires_at, deadline)"
                         " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)", (nid, kind, subject, text, clause, canonical(recipients), now_iso(), actor, expires, deadline))
        self.audit(actor, "warn" if kind == "warning" else "notify", "notice", nid, clause=clause,
                   after={"kind": kind, "subject": subject, "recipients": recipients})
        suspended: list[Suspension] = []
        if kind == "warning":
            for merchant_id in recipients:
                if merchant_id == "*" or not self.get_merchant(merchant_id):
                    continue
                at_once = clause and clause in policy["clauses_that_suspend"]
                if at_once or self.strikes(merchant_id, day) >= int(policy["strikes_before_suspension"]):
                    if any(s.active_on(day) for s in self.suspensions_of("merchant", merchant_id)):
                        continue
                    reason = f"clause {clause} suspends at once" if at_once else f"{self.strikes(merchant_id, day)} strikes within {policy['strike_expiry_days']} days"
                    suspended.append(self.suspend("merchant", merchant_id, clause=clause, reason=reason, days=int(policy["suspension_days"]),
                                                  actor="policy", acted_on=f"notice:{nid}"))
        return self.get_notice(nid), suspended   # type: ignore[return-value]

    def get_notice(self, nid: str) -> Notice | None:
        r = self._db.fetchone("SELECT * FROM notices WHERE id = ?", (nid,))
        return self._notice(r) if r else None

    def notices(self) -> list[Notice]:
        return [self._notice(r) for r in self._db.fetchall("SELECT * FROM notices ORDER BY id DESC")]

    def notices_for(self, merchant_id: str) -> list[Notice]:
        """The merchant's notices, the unacknowledged first."""
        acks = {r["notice_id"]: r["acknowledged_at"] for r in self._db.fetchall("SELECT notice_id, acknowledged_at FROM notice_acks WHERE merchant_id = ?", (merchant_id,))}
        out = []
        for n in self.notices():
            if n.addressed_to(merchant_id):
                n.acknowledged_at = acks.get(n.id, "")
                out.append(n)
        out.sort(key=lambda n: (bool(n.acknowledged_at), n.id))
        return out

    def acknowledge(self, nid: str, merchant_id: str) -> str:
        r = self._db.fetchone("SELECT acknowledged_at FROM notice_acks WHERE notice_id = ? AND merchant_id = ?", (nid, merchant_id))
        if r:
            return r["acknowledged_at"]
        at = now_iso()
        self._db.execute("INSERT INTO notice_acks (notice_id, merchant_id, acknowledged_at) VALUES (?, ?, ?)", (nid, merchant_id, at))
        self.audit(merchant_id, "acknowledge", "notice", nid)
        return at

    def strikes(self, merchant_id: str, day: str) -> int:
        count = 0
        for r in self._db.fetchall("SELECT recipients, expires_at FROM notices WHERE kind = 'warning' AND expires_at >= ?", (day,)):
            if merchant_id in _json(r["recipients"], []):
                count += 1
        return count

    # ----- submissions -----
    @staticmethod
    def _submission(r: dict) -> Submission:
        return Submission(id=r["id"], merchant_id=r["merchant_id"], app_id=r["app_id"], version=r["version"], component=_json(r["component"], {}),
                          signature=r["signature"], packages=_json(r["packages"], []), checks=_json(r["checks"], {}), state=r["state"], answers=r["answers"],
                          created_at=r["created_at"], decided_at=r["decided_at"], released_sequence=r["released_sequence"],
                          findings=_json(r.get("findings", "{}"), {}), checked_at=r.get("checked_at", ""))

    def add_submission(self, *, merchant_id: str, component: dict, signature: str, packages: list, checks: dict, answers: str) -> Submission:
        sid = f"s-{self.next_number('submission_no'):08d}"
        passed = all(v == "ok" or (k == "packages" and v == "waiting") for k, v in checks.items() if k in ("signature", "merchant_standing", "packages"))
        state = "checked" if passed else "failed_checks"
        version = str(component.get("releases", [{}])[-1].get("version", "")) if component.get("releases") else str(component.get("version", ""))
        self._db.execute("INSERT INTO submissions (id, merchant_id, app_id, version, component, signature, packages, checks, state, answers, created_at)"
                         " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                         (sid, merchant_id, str(component.get("id", "")), version, canonical(component), signature, canonical(packages), canonical(checks),
                          state, answers, now_iso()))
        self.audit(merchant_id, "submit", "submission", sid, after={"app": component.get("id", ""), "version": version, "state": state, "answers": answers})
        return self.get_submission(sid)   # type: ignore[return-value]

    def update_submission_packages(self, sid: str, packages: list, checks: dict, *, actor: str, sha256: str) -> Submission | None:
        """After an upload: the packages with their uploaded marks, the checks
        recomputed; a submission that failed the checks on the packages alone
        becomes checked once they are all there."""
        s = self.get_submission(sid)
        if not s:
            return None
        state = s.state
        if state == "failed_checks" and all(checks.get(k) == "ok" for k in ("signature", "merchant_standing", "packages")):
            state = "checked"
        self._db.execute("UPDATE submissions SET packages = ?, checks = ?, state = ? WHERE id = ?", (canonical(packages), canonical(checks), state, sid))
        self.audit(actor, "upload_package", "submission", sid, before={"state": s.state}, after={"state": state, "sha256": sha256, "packages": checks.get("packages")})
        return self.get_submission(sid)

    @staticmethod
    def blocked(checks: dict) -> bool:
        """What keeps a submission from the review's release: the signature
        or the standing anything but ok, the packages anything but ok or
        waiting, the scans or the entry failed."""
        if checks.get("signature") != "ok" or checks.get("merchant_standing") != "ok":
            return True
        if checks.get("packages") not in ("ok", "waiting"):
            return True
        return any(checks.get(k) == "failed" for k in ("scans", "entry"))

    def set_checks(self, sid: str, checks: dict, findings: dict, *, actor: str) -> Submission | None:
        """The pipeline's results kept: a failed blocking check makes a
        waiting submission failed_checks, all of them passing makes it checked
        again; one in review or decided keeps its state."""
        s = self.get_submission(sid)
        if not s:
            return None
        state = s.state
        if state in ("checked", "failed_checks"):
            state = "failed_checks" if self.blocked(checks) else "checked"
        self._db.execute("UPDATE submissions SET checks = ?, findings = ?, checked_at = ?, state = ? WHERE id = ?",
                         (canonical(checks), canonical(findings), now_iso(), state, sid))
        self.audit(actor, "run_checks", "submission", sid, before={"state": s.state}, after={"state": state, "checks": {k: checks.get(k) for k in ("scans", "entry", "smoke", "listing")}})
        return self.get_submission(sid)

    def get_submission(self, sid: str) -> Submission | None:
        r = self._db.fetchone("SELECT * FROM submissions WHERE id = ?", (sid,))
        if not r:
            return None
        s = self._submission(r)
        s.decisions = [{"id": d["id"], "action": d["action"], "message": d["message"], "actor": d["actor"], "at": d["at"], "sequence": d["sequence"]}
                       for d in self._db.fetchall("SELECT * FROM decisions WHERE submission_id = ? ORDER BY id", (sid,))]
        return s

    def submissions(self, *, state: str = "", merchant_id: str = "", app_id: str = "") -> list[Submission]:
        where, params = [], []
        if state:
            where.append("state = ?"); params.append(state)
        if merchant_id:
            where.append("merchant_id = ?"); params.append(merchant_id)
        if app_id:
            where.append("app_id = ?"); params.append(app_id)
        sql = "SELECT * FROM submissions" + (" WHERE " + " AND ".join(where) if where else "") + " ORDER BY id"
        return [self._submission(r) for r in self._db.fetchall(sql, params)]

    def decide(self, sid: str, action: str, *, message: str, actor: str, sequence: int = 0) -> Submission | None:
        """A decision appended: start_review, release, request_changes,
        reject (the operator), withdraw (the merchant)."""
        s = self.get_submission(sid)
        if not s:
            return None
        new_state = {"start_review": "in_review", "release": "released", "request_changes": "changes_requested", "reject": "rejected",
                     "withdraw": "withdrawn"}[action]
        if action == "release" and not sequence:
            sequence = self.next_number("catalog_sequence")
        at = now_iso()
        self._db.execute("INSERT INTO decisions (submission_id, action, message, actor, at, sequence) VALUES (?, ?, ?, ?, ?, ?)",
                         (sid, action, message, actor, at, sequence))
        self._db.execute("UPDATE submissions SET state = ?, decided_at = ?, released_sequence = CASE WHEN ? > 0 THEN ? ELSE released_sequence END WHERE id = ?",
                         (new_state, at if action != "start_review" else "", sequence, sequence, sid))
        self.audit(actor, action, "submission", sid, before={"state": s.state}, after={"state": new_state, "message": message, "sequence": sequence},
                   acted_on=f"decision:{self._db.last_id()}")
        return self.get_submission(sid)

    def released(self, day: str) -> list[dict]:
        """What the catalog is generated from: each app's latest released
        component, from merchants in good standing and apps not suspended."""
        out: dict[str, Submission] = {}
        for s in self.submissions(state="released"):
            if s.app_id not in out or s.released_sequence >= out[s.app_id].released_sequence:
                out[s.app_id] = s
        standing = {m.id: m.standing for m in self.merchants()}
        return [{"app": s.app_id, "merchant": s.merchant_id, "submission": s.id, "sequence": s.released_sequence, "component": s.component,
                 "packages": s.packages}
                for s in sorted(out.values(), key=lambda s: s.app_id)
                if standing.get(s.merchant_id) == "active" and not self.app_suspended(s.app_id, day)]

    # ----- reports -----
    @staticmethod
    def _report(r: dict) -> Report:
        return Report(id=r["id"], source=r["source"], reporter=r["reporter"], reporter_merchant=r["reporter_merchant"], target_kind=r["target_kind"],
                      target=r["target"], kind=r["kind"], text=r["text"], attachments=_json(r["attachments"], []), state=r["state"], assignee=r["assignee"],
                      findings=r["findings"], outcome=r["outcome"], created_at=r["created_at"], resolved_at=r["resolved_at"], secret=r["secret"])

    def add_report(self, *, source: str, reporter: str, reporter_merchant: str, target_kind: str, target: str, kind: str, text: str,
                   attachments: list) -> Report:
        rid = f"r-{self.next_number('report_no'):08d}"
        secret = secrets.token_urlsafe(18)
        self._db.execute("INSERT INTO reports (id, source, reporter, reporter_merchant, target_kind, target, kind, text, attachments, state, created_at, secret)"
                         " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 'open', ?, ?)",
                         (rid, source, reporter, reporter_merchant, target_kind, target, kind, text, canonical(attachments), now_iso(), secret))
        return self.get_report(rid)   # type: ignore[return-value]

    def get_report(self, rid: str) -> Report | None:
        r = self._db.fetchone("SELECT * FROM reports WHERE id = ?", (rid,))
        return self._report(r) if r else None

    def reports(self, *, state: str = "", targets: list[str] | None = None) -> list[Report]:
        if targets is not None:
            if not targets:
                return []
            marks = ", ".join("?" for _ in targets)
            rows = self._db.fetchall(f"SELECT * FROM reports WHERE target IN ({marks}) ORDER BY id", targets)
        elif state:
            rows = self._db.fetchall("SELECT * FROM reports WHERE state = ? ORDER BY id", (state,))
        else:
            rows = self._db.fetchall("SELECT * FROM reports ORDER BY id")
        return [self._report(r) for r in rows]

    def assign_report(self, rid: str, assignee: str, actor: str) -> Report | None:
        r = self.get_report(rid)
        if not r:
            return None
        self._db.execute("UPDATE reports SET assignee = ?, state = CASE WHEN state = 'open' THEN 'assigned' ELSE state END WHERE id = ?", (assignee, rid))
        self.audit(actor, "assign", "report", rid, before={"assignee": r.assignee, "state": r.state}, after={"assignee": assignee})
        return self.get_report(rid)

    def close_report(self, rid: str, action: str, *, findings: str, outcome: str, actor: str) -> Report | None:
        """resolve or dismiss: the findings for the operator, the outcome
        line for the reporter and the merchant."""
        r = self.get_report(rid)
        if not r:
            return None
        state = "resolved" if action == "resolve" else "dismissed"
        self._db.execute("UPDATE reports SET state = ?, findings = ?, outcome = ?, resolved_at = ? WHERE id = ?", (state, findings, outcome, now_iso(), rid))
        self.audit(actor, action, "report", rid, before={"state": r.state}, after={"state": state, "outcome": outcome})
        return self.get_report(rid)

    def merchant_targets(self, merchant_id: str) -> list[str]:
        """The merchant and everything the operator's records tie to it: its
        apps and its submissions - what its reports, its history are about."""
        apps = sorted({s.app_id for s in self.submissions(merchant_id=merchant_id)})
        subs = [s.id for s in self.submissions(merchant_id=merchant_id)]
        return [merchant_id, *apps, *subs]
