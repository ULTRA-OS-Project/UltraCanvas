"""The HTTP API of the marketplace service.

The operator (Authorization: Bearer <operator token or session>; the static token is the bootstrap admin, a session comes from the sign-in and names the person and the role):
  POST /v1/operators/signin             {email, password, totp?, recovery_code?} -> {token, operator, hours}; 401 two_factor_required without the code
  GET  /v1/operators/me; POST /v1/operators/me/two-factor/setup -> {secret, otpauth_url}; .../enable {code} -> {recovery_codes};
                                        .../disable {code}; .../recovery-codes {code} -> a new set
  POST /v1/operators/password/forgot    {email}: a code by mail; POST /v1/operators/password/reset {email, code, new_password, totp?, recovery_code?}
  GET  /v1/submissions/{id}/packages/{sha256}   the uploaded package (the review's Download build)
  GET  /v1/operators; POST /v1/operators {email, name, role, password}; PUT /v1/operators/{id} {name?, role?, password?, disabled?}
  POST /v1/merchants                    register a merchant with its key; GET /v1/merchants, GET /v1/merchants/{id}, PUT /v1/merchants/{id}
  POST /v1/merchants/{id}/suspend       {clause, reason, days}; POST /v1/merchants/{id}/reinstate {reason}
  POST /v1/apps/{app}/suspend, /reinstate   the same for one app
  GET  /v1/submissions[?state=&merchant=&app=]; GET /v1/submissions/{id}
  POST /v1/submissions/{id}/decide      {action: start_review|release|request_changes|reject, message, sequence?}
  GET  /v1/released                     what the catalog is generated from
  POST /v1/publish                      the catalog generated from it and signed into the publish folder (publish.py)
  GET  /v1/reports[?state=]; GET /v1/reports/{id}; POST /v1/reports/{id}/assign {assignee}; /resolve, /dismiss {findings, outcome}
  GET  /v1/applications[?state=]; GET /v1/applications/{id}; POST /v1/applications/{id}/approve {id?, message?}; POST .../reject {message}
  POST /v1/notices                      {kind, subject, text, clause, recipients}; GET /v1/notices
  POST /v1/terms                        {text, changes, in_force_from, version?}
  GET  /v1/policy, PUT /v1/policy       the enforcement policy
  GET  /v1/audit?since=N, GET /v1/audit/verify
A merchant (Authorization: Merchant <id>:<timestamp>:<signature>, auth.py):
  GET  /v1/merchants/me                 the standing, the strikes, the terms
  PUT  /v1/merchants/me                 {name?, contact?, website?, description?, key?}: the merchant's own profile; a key given is the rotation, signed with the old one
  POST /v1/submissions                  {component, signature, packages, answers?}; GET /v1/merchants/me/submissions
  POST /v1/submissions/{id}/packages/{sha256}   the package's bytes (the body, signed like every request): into the package store;
                                        checks.packages goes from waiting to ok once every package named is there
  POST /v1/submissions/{id}/withdraw
  GET  /v1/merchants/me/notices; POST /v1/notices/{id}/ack
  GET  /v1/merchants/me/reports         the reports about the merchant and its apps, the reporter removed
  GET  /v1/merchants/me/history         the audit rows about them
  POST /v1/terms/accept                 {version, accepted_by}
  POST /v1/reports                      a merchant reporting (the store's door is the same URL, unsigned)
Public:
  POST /v1/merchants/apply              {name, contact, email?, website?, description?, key, signature}: a shop asked for, the key proven -> {id, secret, state}
  GET  /v1/applications/{id}?secret=    what became of it: applied, approved (with the merchant id), rejected (with the message)
  GET  /v1/publish/key                  the operator's signing key (public half), for a store's trusted_keys
  POST /v1/reports                      {target_kind, target, kind, text, attachments?, reporter?} -> {id, secret}
  GET  /v1/reports/{id}/outcome?secret= the one line the reporter learns
  GET  /v1/terms, GET /v1/terms/current
  GET  /healthz

Errors are {"error", "error_description"} with a 4xx status.

Version: 0.5.0 - the package store: a package uploaded with a submission, the operator's download, the release gate, the publish laying them out;
               the operators' second factor and the password reset by mail
Version: 0.4.0 - the operators with their roles: sign-in, the signed session, a permission on every operator call
Version: 0.3.0 - the applications; the merchant's own profile
Version: 0.2.0 - the publish, the signing key
Version: 0.1.0
"""

from __future__ import annotations

import hashlib
import hmac
import secrets
from pathlib import Path
from typing import Any

from fastapi import FastAPI, Header, HTTPException, Request
from fastapi.responses import FileResponse, JSONResponse, PlainTextResponse
from pydantic import BaseModel, Field

from ultrastore_db.operator_session import ROLES, allowed, issue, verify as verify_session
from ultrastore_db.totp import hash_recovery, new_code, new_recovery_codes, new_totp_secret, otpauth_url, verify_totp

from . import __version__
from .auth import hash_password, parse_merchant_header, verify_bytes, verify_password, verify_request
from .config import Settings
from .mail import Mailer, mailer_from_settings
from .publish import SigningKey, publish
from .store import NOTICE_KINDS, MarketStore, Merchant, canonical, package_file_name, today


class MerchantBody(BaseModel):
    id: str = Field(default="", max_length=64)
    name: str = Field(min_length=1, max_length=200)
    contact: str = ""
    key: str = Field(min_length=64, max_length=64)


class MerchantUpdate(BaseModel):
    name: str | None = None
    contact: str | None = None
    key: str | None = Field(default=None, min_length=64, max_length=64)
    website: str | None = Field(default=None, max_length=300)
    description: str | None = Field(default=None, max_length=2000)


class ApplyBody(BaseModel):
    name: str = Field(min_length=1, max_length=200)
    contact: str = Field(min_length=1, max_length=300)
    email: str = Field(default="", max_length=254)
    website: str = Field(default="", max_length=300)
    description: str = Field(default="", max_length=2000)
    key: str = Field(min_length=64, max_length=64)
    signature: str = Field(min_length=128, max_length=128)

    def signed(self) -> dict:
        return {"name": self.name, "contact": self.contact, "email": self.email, "website": self.website, "description": self.description, "key": self.key}


class OperatorSigninBody(BaseModel):
    email: str = Field(min_length=3, max_length=254)
    password: str = Field(min_length=1, max_length=256)
    totp: str = ""
    recovery_code: str = ""


class CodeBody(BaseModel):
    code: str = Field(min_length=1, max_length=64)


class ForgotBody(BaseModel):
    email: str = Field(min_length=3, max_length=254)


class ResetBody(BaseModel):
    email: str = Field(min_length=3, max_length=254)
    code: str = Field(min_length=1, max_length=16)
    new_password: str = Field(min_length=12, max_length=256)
    totp: str = ""
    recovery_code: str = ""


class OperatorBody(BaseModel):
    email: str = Field(min_length=3, max_length=254)
    name: str = Field(default="", max_length=120)
    role: str = Field(default="support", max_length=20)
    password: str = Field(min_length=12, max_length=256)


class OperatorUpdate(BaseModel):
    name: str | None = Field(default=None, max_length=120)
    role: str | None = Field(default=None, max_length=20)
    password: str | None = Field(default=None, min_length=12, max_length=256)
    disabled: bool | None = None
    two_factor: bool | None = None   # False only: an admin resets another's second factor after the identity was checked


class ApproveBody(BaseModel):
    id: str = Field(default="", max_length=64)
    message: str = ""


class RejectBody(BaseModel):
    message: str = Field(min_length=1)


class SuspendBody(BaseModel):
    clause: str = ""
    reason: str = ""
    days: int = 0        # 0: until reinstated


class ReinstateBody(BaseModel):
    reason: str = ""


class SubmissionBody(BaseModel):
    component: dict
    signature: str = Field(min_length=128, max_length=128)
    packages: list[dict] = Field(default_factory=list)
    answers: str = ""


class DecideBody(BaseModel):
    action: str
    message: str = ""
    sequence: int = 0


class ReportBody(BaseModel):
    target_kind: str = Field(pattern="^(app|release|merchant)$")
    target: str = Field(min_length=1, max_length=200)
    kind: str = Field(min_length=1, max_length=64)
    text: str = Field(min_length=1, max_length=20000)
    attachments: list[str] = Field(default_factory=list)
    reporter: str = ""


class AssignBody(BaseModel):
    assignee: str


class CloseBody(BaseModel):
    findings: str = ""
    outcome: str = ""


class NoticeBody(BaseModel):
    kind: str
    subject: str = Field(min_length=1, max_length=300)
    text: str = ""
    clause: str = ""
    recipients: list[str] = Field(min_length=1)


class TermsBody(BaseModel):
    text: str = Field(min_length=1)
    changes: str = ""
    in_force_from: str = Field(pattern=r"^\d{4}-\d{2}-\d{2}$")
    version: int = 0


class AcceptBody(BaseModel):
    version: int
    accepted_by: str = ""


def create_app(settings: Settings | None = None, mailer: Mailer | None = None) -> FastAPI:
    settings = settings or Settings.from_environment()
    store = MarketStore(settings.database)
    mail = mailer or mailer_from_settings(settings.smtp_url, settings.mail_from)
    # The operator's signing key: from the environment, else made once and kept.
    if settings.signing_seed:
        signing = SigningKey(bytes.fromhex(settings.signing_seed))
    elif store.meta("signing_seed"):
        signing = SigningKey(bytes.fromhex(store.meta("signing_seed")))
    else:
        signing = SigningKey.generate()
        store.set_meta("signing_seed", signing.seed_hex)

    app = FastAPI(title="UltraStore marketplace service", version=__version__)
    app.state.store = store
    app.state.signing_key = signing

    @app.exception_handler(HTTPException)
    async def http_error(_request: Request, exc: HTTPException):
        content = exc.detail if isinstance(exc.detail, dict) else {"error": "error", "error_description": str(exc.detail)}
        return JSONResponse(status_code=exc.status_code, content=content)

    def error(status: int, code: str, description: str) -> HTTPException:
        return HTTPException(status_code=status, detail={"error": code, "error_description": description})

    def whoami(authorization: str | None) -> dict:
        """The person behind the bearer: the static token is the bootstrap
        admin, a session names the operator and the role."""
        token = (authorization or "").removeprefix("Bearer ").strip()
        if settings.operator_token and hmac.compare_digest(token, settings.operator_token):
            return {"id": "", "name": "operator", "role": "admin", "bootstrap": True}
        session = verify_session(settings.operator_session_secret, token)
        if not session:
            raise error(401, "unauthorized", "The operator token is missing or wrong, or the session is over: sign in again")
        op = store.get_operator(str(session.get("id", "")))
        if not op or not op.active:
            raise error(401, "unauthorized", "This operator account is disabled or gone")
        return {"id": op.id, "name": op.name or op.email, "role": op.role, "bootstrap": False}

    def operator(authorization: str | None, permission: str = "audit.read") -> str:
        """The actor's name for the audit log, once the role allows the action."""
        who = whoami(authorization)
        if not allowed(who["role"], permission):
            raise error(403, "forbidden", f"The role {who['role']} may not {permission.replace('.', ' ')}")
        return who["name"]

    # ----- the operators: sign in, who am I, the accounts (admin) -----
    @app.post("/v1/operators/signin")
    async def operator_signin(body: OperatorSigninBody) -> dict[str, Any]:
        if not settings.operator_session_secret:
            raise error(503, "not_configured", "The service has no operator session secret (ULTRASTORE_MARKET_OPERATOR_SESSION_SECRET)")
        op = store.operator_by_email(body.email)
        if not op or not op.active or not verify_password(body.password, op.password_hash):
            raise error(401, "bad_credentials", "The email or the password is wrong")
        second_factor(op, body.totp, body.recovery_code)
        store.record_operator_signin(op.id)
        token = issue(settings.operator_session_secret, operator_id=op.id, name=op.name or op.email, role=op.role, hours=settings.operator_session_hours)
        return {"token": token, "operator": op.public(), "hours": settings.operator_session_hours}

    def second_factor(op, totp: str, recovery_code: str) -> None:
        """The second factor checked when the account has one; a recovery
        code is used up."""
        if not op.two_factor:
            return
        if totp and verify_totp(op.totp_secret, totp):
            return
        if recovery_code and store.use_operator_recovery_code(op.id, hash_recovery(recovery_code)):
            return
        raise error(401, "two_factor_required", "The six-digit code from your authenticator app, or a recovery code")

    def own_account(authorization: str | None):
        who = whoami(authorization)
        if who["bootstrap"]:
            raise error(409, "bootstrap", "The static token has no account: sign in with one")
        return store.get_operator(who["id"])

    @app.post("/v1/operators/me/two-factor/setup")
    async def two_factor_setup(authorization: str | None = Header(default=None)) -> dict[str, Any]:
        op = own_account(authorization)
        if op.two_factor:
            raise error(409, "two_factor_on", "Two-factor sign-in is on: turn it off first to change the app")
        secret = new_totp_secret()
        store.set_operator_two_factor(op.id, actor=op.name or op.email, secret=secret)
        return {"secret": secret, "otpauth_url": otpauth_url(secret, op.email, issuer="UltraStore operations")}

    @app.post("/v1/operators/me/two-factor/enable")
    async def two_factor_enable(body: CodeBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        op = own_account(authorization)
        if not op.totp_secret or op.two_factor:
            raise error(409, "no_setup", "Set the authenticator up first" if not op.totp_secret else "Two-factor sign-in is on already")
        if not verify_totp(op.totp_secret, body.code):
            raise error(400, "bad_code", "The code is not the authenticator's")
        codes = new_recovery_codes()
        store.set_operator_two_factor(op.id, actor=op.name or op.email, enabled=True, recovery_hashes=[hash_recovery(c) for c in codes], audit_action="enable_two_factor")
        return {"two_factor": True, "recovery_codes": codes}

    @app.post("/v1/operators/me/two-factor/disable")
    async def two_factor_disable(body: CodeBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        op = own_account(authorization)
        if not op.two_factor:
            raise error(409, "two_factor_off", "Two-factor sign-in is off")
        if not verify_totp(op.totp_secret, body.code):
            raise error(400, "bad_code", "The code is not the authenticator's")
        store.set_operator_two_factor(op.id, actor=op.name or op.email, enabled=False, audit_action="disable_two_factor")
        return {"two_factor": False}

    @app.post("/v1/operators/me/two-factor/recovery-codes")
    async def two_factor_recovery_codes(body: CodeBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        op = own_account(authorization)
        if not op.two_factor or not verify_totp(op.totp_secret, body.code):
            raise error(400, "bad_code", "Two-factor sign-in is on and the code is the authenticator's, or no new set")
        codes = new_recovery_codes()
        store.set_operator_two_factor(op.id, actor=op.name or op.email, recovery_hashes=[hash_recovery(c) for c in codes], audit_action="new_recovery_codes")
        return {"recovery_codes": codes}

    @app.post("/v1/operators/password/forgot")
    async def operator_forgot(body: ForgotBody) -> dict[str, Any]:
        """A code to the account's mailbox; the answer says nothing about
        whether the address is an operator's."""
        op = store.operator_by_email(body.email)
        if op and op.active:
            code = new_code()
            store.add_operator_code(op.id, "reset", code, minutes=settings.code_minutes)
            mail(op.email, "UltraStore operations: your password reset code",
                 f"Your code is {code}. It is good for {settings.code_minutes} minutes. If you did not ask for it, tell another admin: "
                 "someone typed your address at the operator's sign-in.")
        return {"sent": True, "minutes": settings.code_minutes}

    @app.post("/v1/operators/password/reset")
    async def operator_reset(body: ResetBody) -> dict[str, Any]:
        op = store.operator_by_email(body.email)
        if not op or not op.active or not store.take_operator_code(op.id, "reset", body.code):
            raise error(400, "bad_code", "The code is wrong, over, or was tried too often: ask for a new one")
        second_factor(op, body.totp, body.recovery_code)
        store.set_operator_password(op.id, hash_password(body.new_password), actor=op.name or op.email)
        mail(op.email, "UltraStore operations: your password was changed", "The password of your operator account was just reset with a mailed code. "
             "If that was not you, tell another admin at once.")
        return {"reset": True}

    @app.get("/v1/operators/me")
    async def operator_me(authorization: str | None = Header(default=None)) -> dict[str, Any]:
        who = whoami(authorization)
        return {**who, "permissions": sorted(p for p in ("merchants.manage", "review.decide", "publish", "reports.handle", "notices.send", "terms.manage",
                                                            "policy.manage", "users.support", "users.manage", "operators.manage") if allowed(who["role"], p))}

    @app.get("/v1/operators")
    async def list_operators(authorization: str | None = Header(default=None)) -> dict[str, Any]:
        operator(authorization, "operators.read")
        return {"operators": [o.public() for o in store.operators()], "roles": list(ROLES)}

    @app.post("/v1/operators")
    async def add_operator(body: OperatorBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "operators.manage")
        if body.role not in ROLES:
            raise error(400, "bad_role", "The role is one of " + ", ".join(ROLES))
        if "@" not in body.email:
            raise error(400, "bad_email", "That is not an email address")
        if store.operator_by_email(body.email):
            raise error(409, "operator_exists", "An operator with that email exists already")
        return store.add_operator(email=body.email, name=body.name, role=body.role, password_hash=hash_password(body.password), actor=actor).public()

    @app.put("/v1/operators/{op_id}")
    async def update_operator(op_id: str, body: OperatorUpdate, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        who = whoami(authorization)
        own = who["id"] == op_id
        # Anyone may change their own name and password; the role and the switch are an admin's.
        if (body.role is not None or body.disabled is not None or body.two_factor is not None or not own) and not allowed(who["role"], "operators.manage"):
            raise error(403, "forbidden", "Only an admin changes another operator, a role, the switch or another's second factor")
        if body.two_factor is not None:
            if body.two_factor or own:
                raise error(400, "bad_request", "An admin resets another operator's second factor (two_factor: false), after the identity was checked; one's own is set up by oneself")
            if not store.set_operator_two_factor(op_id, actor=who["name"], enabled=False, audit_action="reset_two_factor"):
                raise error(404, "no_such_operator", "No such operator")
        if body.role is not None and body.role not in ROLES:
            raise error(400, "bad_role", "The role is one of " + ", ".join(ROLES))
        if own and body.disabled:
            raise error(409, "own_account", "An operator does not disable their own account")
        if body.role is not None and own and body.role != "admin":
            raise error(409, "own_account", "An admin does not take their own admin role away")
        updated = store.update_operator(op_id, actor=who["name"], name=body.name, role=body.role,
                                        password_hash=hash_password(body.password) if body.password else None, disabled=body.disabled)
        if not updated:
            raise error(404, "no_such_operator", "No such operator")
        return updated.public()

    async def merchant(request: Request) -> Merchant:
        parsed = parse_merchant_header(request.headers.get("authorization"))
        if not parsed:
            raise error(401, "unauthorized", "A merchant signs its request: Authorization: Merchant <id>:<timestamp>:<signature>")
        merchant_id, stamp, signature = parsed
        m = store.get_merchant(merchant_id)
        if not m:
            raise error(401, "unauthorized", "No such merchant")
        body = await request.body()
        path = request.url.path + (f"?{request.url.query}" if request.url.query else "")
        if not verify_request(m.key_hex, request.method, path, stamp, signature, body, window_seconds=settings.request_window_seconds):
            raise error(401, "unauthorized", "The request's signature is not the merchant's, or its timestamp is out of the window")
        return m

    def merchant_or_404(merchant_id: str) -> Merchant:
        m = store.get_merchant(merchant_id)
        if not m:
            raise error(404, "no_such_merchant", "No such merchant")
        return m

    # ----- merchants -----
    @app.post("/v1/merchants")
    async def register_merchant(body: MerchantBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "merchants.manage")
        merchant_id = body.id or "m-" + secrets.token_hex(4)
        if store.get_merchant(merchant_id):
            raise error(409, "merchant_exists", f"The merchant {merchant_id} is registered already")
        try:
            bytes.fromhex(body.key)
        except ValueError as e:
            raise error(400, "bad_key", "The key is 32 bytes, hex") from e
        return store.add_merchant(id=merchant_id, name=body.name, contact=body.contact, key_hex=body.key, actor=actor).public()

    @app.get("/v1/merchants")
    async def list_merchants(authorization: str | None = Header(default=None)) -> dict[str, Any]:
        operator(authorization, "merchants.read")
        return {"merchants": [m.public() for m in store.merchants()]}

    @app.get("/v1/merchants/me")
    async def my_standing(request: Request) -> dict[str, Any]:
        m = await merchant(request)
        day = today()
        return {**m.public(), "suspensions": [s.public() for s in store.suspensions_of("merchant", m.id) if s.active_on(day)],
                "acceptances": store.acceptances_of(m.id)}

    @app.put("/v1/merchants/me")
    async def update_me(request: Request) -> dict[str, Any]:
        m = await merchant(request)
        try:
            body = MerchantUpdate.model_validate_json(await request.body())
        except ValueError as e:
            raise error(400, "bad_body", str(e)) from e
        if body.key is not None:
            try:
                bytes.fromhex(body.key)
            except ValueError as e:
                raise error(400, "bad_key", "The key is 32 bytes, hex") from e
            other = store.merchant_by_key(body.key)
            if other and other.id != m.id:
                raise error(409, "key_taken", "That key signs for another merchant")
        if body.website is not None and body.website and not body.website.startswith(("http://", "https://")):
            raise error(400, "bad_website", "The website is an http(s) URL")
        updated = store.update_merchant(m.id, name=body.name, contact=body.contact, key_hex=body.key, actor=m.id, website=body.website, description=body.description)
        return updated.public()   # type: ignore[union-attr]

    # ----- applications: a shop asked for -----
    @app.post("/v1/merchants/apply")
    async def apply(body: ApplyBody) -> dict[str, Any]:
        try:
            bytes.fromhex(body.key)
        except ValueError as e:
            raise error(400, "bad_key", "The key is 32 bytes, hex") from e
        if not verify_bytes(body.key, body.signature, canonical(body.signed()).encode()):
            raise error(400, "bad_signature", "The application is signed with the key it names, over its canonical JSON")
        if body.website and not body.website.startswith(("http://", "https://")):
            raise error(400, "bad_website", "The website is an http(s) URL")
        if store.merchant_by_key(body.key):
            raise error(409, "key_registered", "That key signs for a merchant already")
        if store.open_application_for_key(body.key):
            raise error(409, "applied_already", "An application with that key is waiting for the operator")
        a = store.add_application(name=body.name, contact=body.contact, email=body.email, website=body.website, description=body.description, key_hex=body.key)
        return {"id": a.id, "secret": a.secret, "state": a.state}

    @app.get("/v1/applications/{aid}")
    async def application_state(aid: str, secret: str = "", authorization: str | None = Header(default=None)) -> dict[str, Any]:
        a = store.get_application(aid)
        if not a:
            raise error(404, "no_such_application", "No such application")
        if authorization:
            operator(authorization, "merchants.read")
            return a.public(for_operator=True)
        if not secret or not hmac.compare_digest(secret, a.secret):
            raise error(404, "no_such_application", "No such application")
        return a.public()

    @app.get("/v1/applications")
    async def list_applications(state: str = "", authorization: str | None = Header(default=None)) -> dict[str, Any]:
        operator(authorization, "merchants.read")
        return {"applications": [a.public(for_operator=True) for a in store.applications(state)]}

    @app.post("/v1/applications/{aid}/approve")
    async def approve_application(aid: str, body: ApproveBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "merchants.manage")
        try:
            a = store.decide_application(aid, approve=True, message=body.message, actor=actor, merchant_id=body.id)
        except ValueError as e:
            code = str(e)
            if code == "no_such_application":
                raise error(404, code, "No such application") from None
            if code == "merchant_exists":
                raise error(409, code, f"The merchant {body.id} is registered already") from None
            raise error(409, "decided", "The application was decided already") from None
        return {"application": a.public(for_operator=True), "merchant": store.get_merchant(a.merchant_id).public()}   # type: ignore[union-attr]

    @app.post("/v1/applications/{aid}/reject")
    async def reject_application(aid: str, body: RejectBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "merchants.manage")
        try:
            a = store.decide_application(aid, approve=False, message=body.message, actor=actor)
        except ValueError as e:
            if str(e) == "no_such_application":
                raise error(404, "no_such_application", "No such application") from None
            raise error(409, "decided", "The application was decided already") from None
        return {"application": a.public(for_operator=True)}

    @app.get("/v1/merchants/me/submissions")
    async def my_submissions(request: Request) -> dict[str, Any]:
        m = await merchant(request)
        return {"submissions": [store.get_submission(s.id).public(for_merchant=True) for s in store.submissions(merchant_id=m.id)]}   # type: ignore[union-attr]

    @app.get("/v1/merchants/me/notices")
    async def my_notices(request: Request) -> dict[str, Any]:
        m = await merchant(request)
        return {"notices": [n.public() for n in store.notices_for(m.id)]}

    @app.get("/v1/merchants/me/reports")
    async def my_reports(request: Request) -> dict[str, Any]:
        m = await merchant(request)
        return {"reports": [r.public(for_merchant=True) for r in store.reports(targets=store.merchant_targets(m.id))]}

    @app.get("/v1/merchants/me/history")
    async def my_history(request: Request) -> dict[str, Any]:
        m = await merchant(request)
        return {"history": store.audit_of(store.merchant_targets(m.id))}

    @app.get("/v1/merchants/{merchant_id}")
    async def get_merchant(merchant_id: str, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        operator(authorization, "merchants.read")
        m = merchant_or_404(merchant_id)
        return {**m.public(), "notes": m.notes, "suspensions": [s.public() for s in store.suspensions_of("merchant", m.id)],
                "acceptances": store.acceptances_of(m.id), "submissions": [s.public() for s in store.submissions(merchant_id=m.id)]}

    @app.put("/v1/merchants/{merchant_id}")
    async def update_merchant(merchant_id: str, body: MerchantUpdate, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "merchants.manage")
        merchant_or_404(merchant_id)
        updated = store.update_merchant(merchant_id, name=body.name, contact=body.contact, key_hex=body.key, actor=actor)
        return updated.public()   # type: ignore[union-attr]

    @app.post("/v1/merchants/{merchant_id}/suspend")
    async def suspend_merchant(merchant_id: str, body: SuspendBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "merchants.manage")
        merchant_or_404(merchant_id)
        s = store.suspend("merchant", merchant_id, clause=body.clause, reason=body.reason, days=body.days, actor=actor)
        return {"suspension": s.public(), "merchant": store.get_merchant(merchant_id).public()}   # type: ignore[union-attr]

    @app.post("/v1/merchants/{merchant_id}/reinstate")
    async def reinstate_merchant(merchant_id: str, body: ReinstateBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "merchants.manage")
        merchant_or_404(merchant_id)
        lifted = store.reinstate("merchant", merchant_id, actor=actor, reason=body.reason)
        return {"lifted": lifted, "merchant": store.get_merchant(merchant_id).public()}   # type: ignore[union-attr]

    @app.post("/v1/apps/{app_id}/suspend")
    async def suspend_app(app_id: str, body: SuspendBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "merchants.manage")
        return {"suspension": store.suspend("app", app_id, clause=body.clause, reason=body.reason, days=body.days, actor=actor).public()}

    @app.post("/v1/apps/{app_id}/reinstate")
    async def reinstate_app(app_id: str, body: ReinstateBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "merchants.manage")
        return {"lifted": store.reinstate("app", app_id, actor=actor, reason=body.reason)}

    # ----- submissions -----
    @app.post("/v1/submissions")
    async def submit(request: Request, body: SubmissionBody) -> dict[str, Any]:
        m = await merchant(request)
        app_id = str(body.component.get("id", ""))
        if not app_id:
            raise error(400, "bad_component", "The component names no id")
        if body.answers:
            previous = store.get_submission(body.answers)
            if not previous or previous.merchant_id != m.id or previous.app_id != app_id:
                raise error(400, "bad_answers", "A resubmission answers one of the merchant's own submissions of the same app")
        # The automatic checks: what the service can tell at once; the scans
        # and the smoke run are the review pipeline's, not run here yet.
        checks = {
            "signature": "ok" if verify_bytes(m.key_hex, body.signature, canonical(body.component).encode()) else "failed",
            "merchant_standing": "ok" if m.standing == "active" else m.standing,
            "packages": ("waiting" if settings.package_dir else "ok") if body.packages and all(_package_ok(p) for p in body.packages) else "failed",
            "scans": "not run", "entry": "not run", "smoke": "not run", "listing": "not run",
        }
        packages = [{k: v for k, v in p.items() if k != "uploaded"} for p in body.packages]
        if settings.package_dir:
            # A package already in the store (the same bytes in an earlier submission) needs no second upload.
            for p in packages:
                if _package_ok(p) and (Path(settings.package_dir) / package_file_name(p)).is_file():
                    p["uploaded"] = True
            if checks["packages"] == "waiting" and all(p.get("uploaded") for p in packages):
                checks["packages"] = "ok"
        return store.add_submission(merchant_id=m.id, component=body.component, signature=body.signature, packages=packages, checks=checks,
                                    answers=body.answers).public(for_merchant=True)

    @app.post("/v1/submissions/{sid}/packages/{sha256}")
    async def upload_package(sid: str, sha256: str, request: Request) -> dict[str, Any]:
        """The package's bytes, into the package store under their hash; the
        request is signed over them like every merchant request."""
        if not settings.package_dir:
            raise error(503, "not_configured", "The service keeps no packages (ULTRASTORE_MARKET_PACKAGE_DIR): the catalog names the merchant's URLs")
        m = await merchant(request)
        s = store.get_submission(sid)
        if not s or s.merchant_id != m.id:
            raise error(404, "no_such_submission", "No such submission of this merchant")
        if s.state not in ("checked", "failed_checks", "in_review"):
            raise error(409, "wrong_state", f"A submission {s.state} takes no package")
        sha256 = sha256.lower()
        named = next((p for p in s.packages if str(p.get("sha256", "")).lower() == sha256), None)
        if not named:
            raise error(404, "no_such_package", "The submission names no package with that hash")
        body = await request.body()
        if len(body) > settings.max_package_mb << 20:
            raise error(413, "too_large", f"A package is at most {settings.max_package_mb} MB")
        if hashlib.sha256(body).hexdigest() != sha256:
            raise error(400, "hash_mismatch", "The bytes do not hash to the package's SHA-256")
        if int(named.get("size", 0)) != len(body):
            raise error(400, "size_mismatch", f"The submission says {named.get('size')} bytes, the upload is {len(body)}")
        folder = Path(settings.package_dir)
        folder.mkdir(parents=True, exist_ok=True)
        target = folder / package_file_name(named)
        if not target.is_file():
            tmp = target.with_name(target.name + ".part")
            tmp.write_bytes(body)
            tmp.replace(target)
        packages = [{**p, "uploaded": True} if p is named else p for p in s.packages]
        checks = dict(s.checks)
        if checks.get("packages") in ("waiting", "ok") and all(p.get("uploaded") for p in packages):
            checks["packages"] = "ok"
        return store.update_submission_packages(sid, packages, checks, actor=m.id, sha256=sha256).public(for_merchant=True)   # type: ignore[union-attr]

    @app.get("/v1/submissions/{sid}/packages/{sha256}")
    async def download_package(sid: str, sha256: str, authorization: str | None = Header(default=None)) -> FileResponse:
        operator(authorization, "review.read")
        s = store.get_submission(sid)
        if not s:
            raise error(404, "no_such_submission", "No such submission")
        named = next((p for p in s.packages if str(p.get("sha256", "")).lower() == sha256.lower()), None)
        path = Path(settings.package_dir) / package_file_name(named) if named and settings.package_dir else None
        if not named or not named.get("uploaded") or not path or not path.is_file():
            raise error(404, "not_uploaded", "The merchant has not uploaded that package")
        return FileResponse(str(path), media_type="application/octet-stream", filename=path.name)

    def _package_ok(p: dict) -> bool:
        sha = str(p.get("sha256", ""))
        try:
            return len(sha) == 64 and bytes.fromhex(sha) is not None and int(p.get("size", 0)) > 0 and bool(p.get("file") or p.get("url"))
        except ValueError:
            return False

    @app.get("/v1/submissions")
    async def list_submissions(state: str = "", merchant: str = "", app: str = "", authorization: str | None = Header(default=None)) -> dict[str, Any]:
        operator(authorization, "review.read")
        return {"submissions": [s.public() for s in store.submissions(state=state, merchant_id=merchant, app_id=app)]}

    @app.get("/v1/submissions/{sid}")
    async def get_submission(sid: str, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        operator(authorization, "review.read")
        s = store.get_submission(sid)
        if not s:
            raise error(404, "no_such_submission", "No such submission")
        return s.public()

    @app.post("/v1/submissions/{sid}/decide")
    async def decide(sid: str, body: DecideBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "review.decide")
        s = store.get_submission(sid)
        if not s:
            raise error(404, "no_such_submission", "No such submission")
        allowed = {"start_review": ("checked", "failed_checks"), "release": ("checked", "in_review"), "request_changes": ("checked", "in_review", "failed_checks"),
                   "reject": ("checked", "in_review", "failed_checks")}
        if body.action not in allowed:
            raise error(400, "bad_action", "The action is start_review, release, request_changes or reject")
        if s.state not in allowed[body.action]:
            raise error(409, "wrong_state", f"A submission {s.state} cannot be {body.action.replace('_', ' ')}")
        if body.action == "release":
            m = store.get_merchant(s.merchant_id)
            if not m or m.standing != "active":
                raise error(409, "merchant_not_active", f"The merchant is {m.standing if m else 'gone'}")
            if s.checks.get("packages") != "ok":
                raise error(409, "packages_missing", "Not every package the submission names is in the package store yet")
        return store.decide(sid, body.action, message=body.message, actor=actor, sequence=body.sequence).public()   # type: ignore[union-attr]

    @app.post("/v1/submissions/{sid}/withdraw")
    async def withdraw(sid: str, request: Request) -> dict[str, Any]:
        m = await merchant(request)
        s = store.get_submission(sid)
        if not s or s.merchant_id != m.id:
            raise error(404, "no_such_submission", "No such submission of this merchant")
        if s.state in ("rejected", "withdrawn"):
            raise error(409, "wrong_state", f"The submission is {s.state} already")
        return store.decide(sid, "withdraw", message="", actor=m.id).public(for_merchant=True)   # type: ignore[union-attr]

    @app.get("/v1/released")
    async def released(authorization: str | None = Header(default=None)) -> dict[str, Any]:
        operator(authorization, "review.read")
        # `sequence`: the last release's; `published`: the last publish's (a release past it waits for the next publish).
        return {"released": store.released(today()), "sequence": int(store.meta("catalog_sequence") or "0"),
                "published": int(store.meta("publish_sequence") or "0")}

    @app.post("/v1/publish")
    async def publish_catalog(authorization: str | None = Header(default=None)) -> dict[str, Any]:
        """The catalog from the released submissions, signed into the publish folder."""
        actor = operator(authorization, "publish")
        if not settings.publish_dir:
            raise error(503, "not_configured", "ULTRASTORE_MARKET_PUBLISH_DIR names where the catalog goes")
        report = publish(store, signing, settings.publish_dir, day=today(), extra_trusted_keys=settings.trusted_keys, payment_url=settings.payment_url,
                         licence_key=settings.licence_key, market_url=settings.public_base_url, actor=actor, package_dir=settings.package_dir,
                         catalog_url=settings.catalog_url)
        return {"sequence": report.sequence, "apps": report.apps, "components": report.components, "publishers": report.publishers,
                "left_out": report.left_out, "files": report.files, "public_key": report.public_key, "packages": report.packages_laid_out}

    @app.get("/v1/publish/key")
    async def publish_key() -> dict[str, Any]:
        return {"public_key": signing.public_hex, "trusted_keys": [signing.public_hex] + [k for k in settings.trusted_keys if k != signing.public_hex]}

    # ----- reports: three doors -----
    @app.post("/v1/reports")
    async def report(request: Request, body: ReportBody) -> dict[str, Any]:
        auth = request.headers.get("authorization", "")
        if auth.startswith("Bearer "):
            operator(auth)
            source, reporter, reporter_merchant = "operator", body.reporter, ""
        elif auth.startswith("Merchant "):
            m = await merchant(request)
            source, reporter, reporter_merchant = "merchant", m.name, m.id
        else:
            source, reporter, reporter_merchant = "store", body.reporter, ""   # the licence or the install id, or anonymous
        r = store.add_report(source=source, reporter=reporter, reporter_merchant=reporter_merchant, target_kind=body.target_kind, target=body.target,
                             kind=body.kind, text=body.text, attachments=body.attachments)
        return {"id": r.id, "secret": r.secret, "state": r.state, "created_at": r.created_at}

    @app.get("/v1/reports/{rid}/outcome")
    async def report_outcome(rid: str, secret: str) -> dict[str, Any]:
        r = store.get_report(rid)
        if not r or not hmac.compare_digest(r.secret, secret):
            raise error(404, "no_such_report", "No such report")
        return {"id": r.id, "state": r.state, "outcome": r.outcome, "resolved_at": r.resolved_at}

    @app.get("/v1/reports")
    async def list_reports(state: str = "", authorization: str | None = Header(default=None)) -> dict[str, Any]:
        operator(authorization, "reports.read")
        return {"reports": [r.public() for r in store.reports(state=state)]}

    @app.get("/v1/reports/{rid}")
    async def get_report(rid: str, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        operator(authorization, "reports.read")
        r = store.get_report(rid)
        if not r:
            raise error(404, "no_such_report", "No such report")
        return r.public()

    @app.post("/v1/reports/{rid}/assign")
    async def assign(rid: str, body: AssignBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "reports.handle")
        r = store.assign_report(rid, body.assignee, actor)
        if not r:
            raise error(404, "no_such_report", "No such report")
        return r.public()

    @app.post("/v1/reports/{rid}/resolve")
    async def resolve(rid: str, body: CloseBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "reports.handle")
        r = store.close_report(rid, "resolve", findings=body.findings, outcome=body.outcome, actor=actor)
        if not r:
            raise error(404, "no_such_report", "No such report")
        return r.public()

    @app.post("/v1/reports/{rid}/dismiss")
    async def dismiss(rid: str, body: CloseBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "reports.handle")
        r = store.close_report(rid, "dismiss", findings=body.findings, outcome=body.outcome, actor=actor)
        if not r:
            raise error(404, "no_such_report", "No such report")
        return r.public()

    # ----- notices -----
    @app.post("/v1/notices")
    async def notify(body: NoticeBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "notices.send")
        if body.kind not in NOTICE_KINDS:
            raise error(400, "bad_kind", "The kind is " + ", ".join(NOTICE_KINDS))
        if body.kind == "warning" and not body.clause:
            raise error(400, "no_clause", "A warning names the clause it is about")
        for recipient in body.recipients:
            if recipient != "*" and not store.get_merchant(recipient):
                raise error(404, "no_such_merchant", f"No such merchant {recipient}")
        notice, suspended = store.add_notice(kind=body.kind, subject=body.subject, text=body.text, clause=body.clause, recipients=body.recipients, actor=actor)
        return {"notice": notice.public(), "suspended": [s.public() for s in suspended]}

    @app.get("/v1/notices")
    async def list_notices(authorization: str | None = Header(default=None)) -> dict[str, Any]:
        operator(authorization, "notices.read")
        return {"notices": [n.public() for n in store.notices()]}

    @app.post("/v1/notices/{nid}/ack")
    async def acknowledge(nid: str, request: Request) -> dict[str, Any]:
        m = await merchant(request)
        n = store.get_notice(nid)
        if not n or not n.addressed_to(m.id):
            raise error(404, "no_such_notice", "No such notice for this merchant")
        return {"id": nid, "acknowledged_at": store.acknowledge(nid, m.id)}

    # ----- terms, policy, audit -----
    @app.get("/v1/terms")
    async def terms() -> dict[str, Any]:
        return {"versions": store.terms_versions(), "in_force": store.terms_in_force(today())}

    @app.get("/v1/terms/current")
    async def terms_current() -> dict[str, Any]:
        version = store.terms_in_force(today())
        current = next((t for t in store.terms_versions() if t["version"] == version), None)
        if not current:
            raise error(404, "no_terms", "No terms are in force")
        return current

    @app.post("/v1/terms")
    async def add_terms(body: TermsBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "terms.manage")
        if body.version and any(t["version"] == body.version for t in store.terms_versions()):
            raise error(409, "version_exists", f"Terms version {body.version} exists")
        return store.add_terms(text=body.text, changes=body.changes, in_force_from=body.in_force_from, actor=actor, version=body.version)

    @app.post("/v1/terms/accept")
    async def accept_terms(request: Request, body: AcceptBody) -> dict[str, Any]:
        m = await merchant(request)
        if not any(t["version"] == body.version for t in store.terms_versions()):
            raise error(404, "no_such_version", "No such terms version")
        accepted = store.accept_terms(m.id, body.version, body.accepted_by)
        return {**accepted, "standing": store.get_merchant(m.id).standing}   # type: ignore[union-attr]

    @app.get("/v1/policy")
    async def policy(authorization: str | None = Header(default=None)) -> dict[str, Any]:
        operator(authorization, "policy.read")
        return store.policy()

    @app.put("/v1/policy")
    async def set_policy(body: dict, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "policy.manage")
        return store.set_policy(body, actor)

    @app.get("/v1/audit")
    async def audit(since: int = 0, limit: int = 500, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        operator(authorization, "audit.read")
        rows = store.audit_since(since, min(limit, 1000))
        return {"audit": rows, "next": rows[-1]["id"] if rows else since}

    @app.get("/v1/audit/verify")
    async def audit_verify(authorization: str | None = Header(default=None)) -> dict[str, Any]:
        operator(authorization, "audit.read")
        return store.verify_audit()

    @app.get("/healthz", response_class=PlainTextResponse)
    async def healthz() -> str:
        missing = settings.configured()
        return "ok" if not missing else "not configured: " + ", ".join(missing)

    return app
