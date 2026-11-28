"""The HTTP API of the account service.

Anyone:
  POST /v1/signup                       {email, password, country, year_of_birth, language, marketing} -> {user, verify}; a code goes out by mail
  POST /v1/signup/verify                {email, code, device} -> {token, user}: the account verified and this machine signed in
  POST /v1/signup/resend                {email}
  POST /v1/signin                       {email, password, device, totp?, recovery_code?} -> {token, user}; 401 two_factor_required without the code
  POST /v1/password/forgot              {email}: a code by mail; POST /v1/password/reset {email, code, new_password, totp?}
  GET  /healthz
A signed-in store (Authorization: Bearer <session token>):
  GET  /v1/me                           the profile, the security summary, the devices, the licences, the settings, the unread notices
  PUT  /v1/me                           {display_name?, handle?, language?, country?, year_of_birth?, about?}
  PUT  /v1/me/settings                  {notifications?, history_on_account?, public_profile?, marketing?}
  POST /v1/me/email                     {email, password}: a code to the new address; POST /v1/me/email/verify {code}
  POST /v1/me/password                  {current, new}: the other devices signed out
  POST /v1/me/two-factor/setup          -> {secret, otpauth_url}; POST /v1/me/two-factor/enable {code} -> {recovery_codes}
  POST /v1/me/two-factor/disable        {code}; POST /v1/me/two-factor/recovery-codes {code} -> a new set
  GET  /v1/me/devices; PUT /v1/me/devices/{id} {name}; DELETE /v1/me/devices/{id} (signs it out, frees its seats)
  POST /v1/me/signout; POST /v1/me/signout-all
  GET  /v1/me/sign-ins                  the sign-in history
  GET  /v1/me/licences; POST /v1/me/licences {licence}: a payment-service licence attached; DELETE /v1/me/licences/{id}
  POST /v1/me/licences/{id}/install, /uninstall   a seat taken or freed for this device
  GET  /v1/me/notices; POST /v1/me/notices/{id}/read
  POST /v1/me/archive                   the data archive requested and built; GET /v1/me/archive; GET /v1/me/archive/{id}/download (once, when released)
  POST /v1/me/delete                    {password}: deletion requested, carried out after the grace; POST /v1/me/delete/cancel
  GET  /v1/me/subscription              the plan, paid or not, until when, what it allows; POST /v1/me/subscription {licence}: the paid plan
  POST /v1/me/phone                     {phone, password}: a texted code (a paid plan only); POST /v1/me/phone/verify {code}; POST /v1/me/phone/remove {password}
  GET  /v1/me/recovery-keys; POST /v1/me/recovery-keys {app, label, secret} -> {recovery_key}; DELETE /v1/me/recovery-keys/{id}
The recovery of an escrowed key (anyone holding the id - it is in the vault file):
  GET  /v1/recovery-keys/{id}           {app, label, channels: [{channel, to}]}: where a code can be sent, masked
  POST /v1/recovery-keys/{id}/code      {channel: email|sms}: a code sent there; POST /v1/recovery-keys/{id}/release {code} -> {secret}
  GET  /v1/plans                        the plans and what each allows
The operator (Authorization: Bearer <operator token or session>; the static token is the bootstrap admin, a session from the marketplace
service's sign-in names the person and the role - a support role reads and sends notices, an admin suspends and deletes):
  GET  /v1/operators/me                 who the bearer is
  GET  /v1/users?state=&q=              with the counts; GET /v1/users/{id} the file: devices, licences, notices, the history
  POST /v1/users/{id}/notice            {kind, subject, text}: written to the account and sent by mail
  POST /v1/users/{id}/signout-all; POST /v1/users/{id}/two-factor/reset {reason}
  POST /v1/users/{id}/suspend           {reason, days}; POST /v1/users/{id}/reinstate {reason}
  POST /v1/users/{id}/delete            {reason}: a requested deletion carried out now
  POST /v1/users/{id}/subscription      {plan, days, reason}: a plan granted; POST /v1/users/{id}/subscription/cancel {reason}
  POST /v1/archives/{id}/release
  POST /v1/maintenance                  deletions past their grace carried out, suspensions past their end lifted
  GET  /v1/stats; GET /v1/audit?since=N&limit=; GET /v1/audit/verify

Errors are {"error", "error_description"} with a 4xx status.

Version: 0.3.1 - a deletion's grace is 90 days by default (config.py; it was 30)
Version: 0.3.0 - UltraAccount: the phone verified by a texted code, the subscription, the recovery-key escrow
Version: 0.2.0 - the operator sessions with their roles, verified with the shared secret
Version: 0.1.0
"""

from __future__ import annotations

import hmac
from typing import Any

from fastapi import FastAPI, Header, HTTPException, Request
from fastapi.responses import JSONResponse, PlainTextResponse, Response
from pydantic import BaseModel, Field

from ultrastore_db.operator_session import allowed, verify as verify_session

from . import __version__

from .auth import (ESCROW_SECRET_BYTES, escrow_key_problem, hash_password, hash_recovery, new_code, new_recovery_codes, new_token, new_totp_secret,
                   normalise_phone, open_escrow, otpauth_url, password_problems, seal_escrow, verify_password, verify_totp)
from .config import Settings
from .licence import verify as verify_licence
from .mail import Mailer, mailer_from_settings
from .sms import Texter, texter_from_settings
from .store import NOTICE_KINDS, PLANS, AccountStore, Escrow, Licence, Session, User, add_days, mask_email, mask_phone, today


class Device(BaseModel):
    name: str = Field(default="", max_length=120)
    os: str = Field(default="", max_length=120)
    client: str = Field(default="", max_length=120)


class SignupBody(BaseModel):
    email: str = Field(min_length=3, max_length=254)
    password: str = Field(min_length=1, max_length=256)
    country: str = Field(default="", max_length=2)
    year_of_birth: int = 0
    language: str = Field(default="en", max_length=16)
    marketing: bool = False


class VerifyBody(BaseModel):
    email: str
    code: str = Field(min_length=1, max_length=16)
    device: Device = Field(default_factory=Device)


class EmailBody(BaseModel):
    email: str


class SigninBody(BaseModel):
    email: str
    password: str
    device: Device = Field(default_factory=Device)
    totp: str = ""
    recovery_code: str = ""


class ResetBody(BaseModel):
    email: str
    code: str
    new_password: str
    totp: str = ""
    recovery_code: str = ""


class ProfileBody(BaseModel):
    display_name: str | None = Field(default=None, max_length=80)
    handle: str | None = Field(default=None, max_length=32)
    language: str | None = Field(default=None, max_length=16)
    country: str | None = Field(default=None, max_length=2)
    year_of_birth: int | None = None
    about: str | None = Field(default=None, max_length=1000)


class SettingsBody(BaseModel):
    notifications: dict[str, str] | None = None
    history_on_account: bool | None = None
    public_profile: bool | None = None
    marketing: bool | None = None


class ChangeEmailBody(BaseModel):
    email: str
    password: str


class CodeBody(BaseModel):
    code: str = Field(min_length=1, max_length=16)


class PasswordBody(BaseModel):
    current: str
    new: str


class NameBody(BaseModel):
    name: str = Field(min_length=1, max_length=120)


class LicenceBody(BaseModel):
    licence: str = Field(min_length=10, max_length=4000)
    seats: int = 0


class DeleteBody(BaseModel):
    password: str


class UserNoticeBody(BaseModel):
    kind: str = "notice"
    subject: str = Field(min_length=1, max_length=300)
    text: str = Field(default="", max_length=20000)


class ReasonBody(BaseModel):
    reason: str = ""


class SuspendBody(BaseModel):
    reason: str = ""
    days: int = 0


class SubscriptionBody(BaseModel):
    licence: str = Field(min_length=10, max_length=4000)


class GrantBody(BaseModel):
    plan: str = "plus"
    days: int = 0          # 0: until cancelled
    reason: str = ""


class PhoneBody(BaseModel):
    phone: str = Field(min_length=4, max_length=32)
    password: str


class EscrowBody(BaseModel):
    app: str = Field(min_length=1, max_length=64)
    label: str = Field(default="", max_length=120)
    secret: str = Field(min_length=2 * ESCROW_SECRET_BYTES, max_length=2 * ESCROW_SECRET_BYTES)   # hex


class ChannelBody(BaseModel):
    channel: str = "email"


def create_app(settings: Settings | None = None, mailer: Mailer | None = None, texter: Texter | None = None) -> FastAPI:
    settings = settings or Settings.from_environment()
    store = AccountStore(settings.database)
    send_mail = mailer or mailer_from_settings(settings.smtp_url, settings.mail_from)
    send_text = texter or texter_from_settings(settings.sms_url, settings.sms_token, settings.sms_from)

    app = FastAPI(title="UltraStore account service", version=__version__)
    app.state.store = store

    @app.exception_handler(HTTPException)
    async def http_error(_request: Request, exc: HTTPException):
        content = exc.detail if isinstance(exc.detail, dict) else {"error": "error", "error_description": str(exc.detail)}
        return JSONResponse(status_code=exc.status_code, content=content)

    def error(status: int, code: str, description: str) -> HTTPException:
        return HTTPException(status_code=status, detail={"error": code, "error_description": description})

    def client_ip(request: Request) -> str:
        forwarded = request.headers.get("x-forwarded-for", "")
        return forwarded.split(",")[0].strip() if forwarded else (request.client.host if request.client else "")

    # ----- who is asking -----
    def whoami(authorization: str | None) -> dict:
        """The person behind the bearer: the static token is the bootstrap
        admin, a session (issued by the marketplace service's sign-in) names
        the operator and the role."""
        token = (authorization or "").removeprefix("Bearer ").strip()
        if settings.operator_token and hmac.compare_digest(token, settings.operator_token):
            return {"id": "", "name": "operator", "role": "admin", "bootstrap": True}
        session = verify_session(settings.operator_session_secret, token)
        if not session:
            raise error(401, "unauthorized", "The operator token is missing or wrong, or the session is over: sign in again")
        return {"id": str(session.get("id", "")), "name": str(session.get("name", "")) or "operator", "role": session["role"], "bootstrap": False}

    def operator(authorization: str | None, permission: str = "users.read") -> str:
        """The actor's name for the audit log, once the role allows the action."""
        who = whoami(authorization)
        if not allowed(who["role"], permission):
            raise error(403, "forbidden", f"The role {who['role']} may not {permission.replace('.', ' ')}")
        return who["name"]

    @app.get("/v1/operators/me")
    async def operator_me(authorization: str | None = Header(default=None)) -> dict[str, Any]:
        return whoami(authorization)

    def signed_in(request: Request) -> tuple[User, Session]:
        token = (request.headers.get("authorization") or "").removeprefix("Bearer ").strip()
        if not token.startswith("us_"):
            raise error(401, "unauthorized", "A signed-in store sends Authorization: Bearer <session token>")
        session = store.session_by_token(token, idle_days=settings.session_idle_days, ip=client_ip(request))
        if not session:
            raise error(401, "signed_out", "This machine is signed out: sign in again")
        user = store.get_user(session.user_id)
        if not user or user.state == "deleted":
            raise error(401, "signed_out", "This account no longer exists")
        if user.state == "suspended":
            raise error(403, "suspended", "This account is suspended" + (f" until {user.suspended_until}" if user.suspended_until else ""))
        return user, session

    def user_or_404(user_id: str) -> User:
        u = store.get_user(user_id)
        if not u or u.state == "deleted":
            raise error(404, "no_such_user", "No such user")
        return u

    def second_factor(user: User, totp: str, recovery_code: str) -> None:
        """The second factor checked when the account has one; a recovery
        code used is spent."""
        if not user.two_factor:
            return
        if totp and verify_totp(user.totp_secret, totp):
            return
        if recovery_code:
            digest = hash_recovery(recovery_code)
            if digest in user.recovery_codes:
                store.update_user(user.id, recovery_codes=[c for c in user.recovery_codes if c != digest])
                store.add_notice(user.id, kind="security", subject="A recovery code was used", text="One of your recovery codes signed you in. "
                                 f"{len(user.recovery_codes) - 1} are left; make a new set from Security if you did not do this.", actor="service")
                return
        raise error(401, "two_factor_required", "The six-digit code from your authenticator app, or a recovery code")

    def send_code(user: User, purpose: str, to: str, *, subject: str, lead: str, payload: str = "") -> None:
        if store.code_sent_recently(user.id, purpose):
            raise error(429, "code_sent", "A code went out a moment ago: wait a minute before asking for another")
        code = new_code()
        store.add_code(user.id, purpose, code, minutes=settings.code_minutes, payload=payload)
        send_mail(to, subject, f"{lead}\n\nYour code: {code}\n\nIt is good for {settings.code_minutes} minutes. If this was not you, ignore this mail.")

    def new_session(user: User, device: Device, request: Request) -> dict[str, Any]:
        token = new_token()
        session = store.add_session(user.id, token, device_name=device.name or "A machine", os=device.os, client=device.client, ip=client_ip(request))
        store.record_signin(user.id, ip=client_ip(request), device=session.device_name, ok=True)
        if len(store.sessions_of(user.id)) > 1:
            store.add_notice(user.id, kind="security", subject=f"New sign-in on {session.device_name}",
                             text=f"Your account was signed in on {session.device_name} ({device.os or 'unknown system'}). "
                                  "If this was not you, sign that device out from Devices and change your password.", actor="service")
            if user.settings.get("notifications", {}).get("security", "email") == "email":
                send_mail(user.email, "New sign-in to your UltraStore account",
                          f"Your UltraStore account was signed in on {session.device_name}. If this was not you, sign that device out and change your password.")
        return {"token": token, "device": session.public(session.id), "user": user.me()}

    def me(user: User, session: Session) -> dict[str, Any]:
        archive = store.open_archive(user.id)
        return {**user.me(), "subscription": store.plan_of(user.id), "recovery_keys": len(store.escrows_of(user.id)),
                "device": session.public(session.id), "devices": [s.public(session.id) for s in store.sessions_of(user.id)],
                "licences": [l.public() for l in store.licences_of(user.id)],
                "unread_notices": len([n for n in store.notices_of(user.id) if not n.read_at]),
                "archive": archive.public() if archive else None}

    def licence_or_404(user: User, licence_id: str) -> Licence:
        lic = store.get_licence(licence_id)
        if not lic or lic.user_id != user.id or lic.released_at:
            raise error(404, "no_such_licence", "No such licence on this account")
        return lic

    # ----- sign up -----
    @app.post("/v1/signup")
    async def signup(body: SignupBody) -> dict[str, Any]:
        email = body.email.strip().lower()
        if "@" not in email or "." not in email.rsplit("@", 1)[-1]:
            raise error(400, "bad_email", "That is not an email address")
        problem = password_problems(body.password)
        if problem:
            raise error(400, "weak_password", problem)
        if body.year_of_birth and not (1900 <= body.year_of_birth <= int(today()[:4])):
            raise error(400, "bad_year", "The year of birth is a four-digit year")
        if store.user_by_email(email):
            # Whether an address has an account is not told to a stranger: the
            # same answer as a new one, and a mail to the address's owner.
            send_mail(email, "Someone tried to create an UltraStore account with your address",
                      "An account with this address exists already. Sign in, or reset the password if you forgot it.")
            return {"user": {"id": "", "email": email}, "verify": "email_code"}
        user = store.add_user(email=email, password_hash=hash_password(body.password), country=body.country.upper(), year_of_birth=body.year_of_birth,
                              language=body.language, marketing=body.marketing)
        send_code(user, "verify_email", email, subject="Your UltraStore code", lead="Welcome to UltraStore. Enter this code in the store to verify your address.")
        return {"user": {"id": user.id, "email": email}, "verify": "email_code"}

    @app.post("/v1/signup/resend")
    async def resend(body: EmailBody) -> dict[str, Any]:
        user = store.user_by_email(body.email)
        if user and not user.email_verified:
            send_code(user, "verify_email", user.email, subject="Your UltraStore code", lead="Enter this code in the store to verify your address.")
        return {"sent": True}

    @app.post("/v1/signup/verify")
    async def verify(body: VerifyBody, request: Request) -> dict[str, Any]:
        user = store.user_by_email(body.email)
        if not user:
            raise error(404, "no_such_user", "No account with that address")
        if not user.email_verified:
            if not store.take_code(user.id, "verify_email", body.code):
                raise error(400, "bad_code", "The code is wrong or has run out: ask for a new one")
            user = store.update_user(user.id, email_verified=True)
            store.audit(user.id, "verify_email", "user", user.id)
        return new_session(user, body.device, request)

    # ----- sign in -----
    @app.post("/v1/signin")
    async def signin(body: SigninBody, request: Request) -> dict[str, Any]:
        user = store.user_by_email(body.email)
        ip = client_ip(request)
        if not user or not verify_password(body.password, user.password_hash):
            if user:
                store.record_signin(user.id, ip=ip, device=body.device.name, ok=False, reason="password")
                if store.failed_signins_recently(user.id) > 10:
                    raise error(429, "too_many_attempts", "Too many attempts: wait a quarter of an hour")
            raise error(401, "bad_credentials", "The email or the password is wrong")
        if store.failed_signins_recently(user.id) > 10:
            raise error(429, "too_many_attempts", "Too many attempts: wait a quarter of an hour")
        if user.state == "suspended":
            raise error(403, "suspended", "This account is suspended" + (f" until {user.suspended_until}" if user.suspended_until else ""))
        if not user.email_verified:
            raise error(403, "email_unverified", "Verify the address first: the code was mailed to it")
        try:
            second_factor(user, body.totp, body.recovery_code)
        except HTTPException:
            store.record_signin(user.id, ip=ip, device=body.device.name, ok=False, reason="two_factor")
            raise
        return new_session(user, body.device, request)

    @app.post("/v1/password/forgot")
    async def forgot(body: EmailBody) -> dict[str, Any]:
        user = store.user_by_email(body.email)
        if user and user.state != "suspended":
            send_code(user, "reset_password", user.email, subject="Reset your UltraStore password", lead="Enter this code in the store to set a new password.")
        return {"sent": True}

    @app.post("/v1/password/reset")
    async def reset(body: ResetBody) -> dict[str, Any]:
        user = store.user_by_email(body.email)
        if not user:
            raise error(404, "no_such_user", "No account with that address")
        problem = password_problems(body.new_password)
        if problem:
            raise error(400, "weak_password", problem)
        second_factor(user, body.totp, body.recovery_code)
        if not store.take_code(user.id, "reset_password", body.code):
            raise error(400, "bad_code", "The code is wrong or has run out: ask for a new one")
        store.update_user(user.id, password_hash=hash_password(body.new_password), password_changed_at=today())
        store.sign_out_all(user.id, reason="password reset")
        store.audit(user.id, "reset_password", "user", user.id)
        send_mail(user.email, "Your UltraStore password was changed", "Your password was reset and every device signed out. If this was not you, write to us at once.")
        return {"reset": True}

    # ----- me -----
    @app.get("/v1/me")
    async def get_me(request: Request) -> dict[str, Any]:
        user, session = signed_in(request)
        return me(user, session)

    @app.put("/v1/me")
    async def put_me(body: ProfileBody, request: Request) -> dict[str, Any]:
        user, session = signed_in(request)
        fields: dict[str, Any] = {}
        if body.handle is not None:
            handle = body.handle.strip().lower().lstrip("@")
            if handle and (not handle.replace("_", "").replace(".", "").isalnum() or len(handle) < 3):
                raise error(400, "bad_handle", "A handle is 3 to 32 letters, digits, dots or underscores")
            other = store.user_by_handle(handle) if handle else None
            if other and other.id != user.id:
                raise error(409, "handle_taken", "That handle is taken")
            fields["handle"] = handle
        if body.year_of_birth is not None and body.year_of_birth and not (1900 <= body.year_of_birth <= int(today()[:4])):
            raise error(400, "bad_year", "The year of birth is a four-digit year")
        for key in ("display_name", "language", "about", "year_of_birth"):
            value = getattr(body, key)
            if value is not None:
                fields[key] = value.strip() if isinstance(value, str) else value
        if body.country is not None:
            fields["country"] = body.country.strip().upper()
        user = store.update_user(user.id, **fields)
        return me(user, session)

    @app.put("/v1/me/settings")
    async def put_settings(body: SettingsBody, request: Request) -> dict[str, Any]:
        user, session = signed_in(request)
        new = dict(user.settings)
        if body.notifications is not None:
            new["notifications"] = {**new.get("notifications", {}), **body.notifications, "security": "email"}   # security mails cannot be turned off
        for key in ("history_on_account", "public_profile", "marketing"):
            value = getattr(body, key)
            if value is not None:
                new[key] = value
        user = store.update_user(user.id, settings=new)
        return me(user, session)

    @app.post("/v1/me/email")
    async def change_email(body: ChangeEmailBody, request: Request) -> dict[str, Any]:
        user, _ = signed_in(request)
        if not verify_password(body.password, user.password_hash):
            raise error(401, "bad_credentials", "The password is wrong")
        email = body.email.strip().lower()
        if "@" not in email:
            raise error(400, "bad_email", "That is not an email address")
        if store.user_by_email(email):
            raise error(409, "email_taken", "An account with that address exists already")
        send_code(user, "change_email", email, subject="Confirm your new UltraStore address", lead="Enter this code in the store to move your account to this address.",
                  payload=email)
        return {"verify": "email_code", "email": email}

    @app.post("/v1/me/email/verify")
    async def change_email_verify(body: CodeBody, request: Request) -> dict[str, Any]:
        user, session = signed_in(request)
        row = store.take_code(user.id, "change_email", body.code)
        if not row:
            raise error(400, "bad_code", "The code is wrong or has run out")
        old = user.email
        user = store.update_user(user.id, email=row["payload"], email_verified=True)
        store.audit(user.id, "change_email", "user", user.id)
        send_mail(old, "Your UltraStore address was changed", f"Your account now uses {user.email}. If this was not you, write to us at once.")
        return me(user, session)

    @app.post("/v1/me/password")
    async def change_password(body: PasswordBody, request: Request) -> dict[str, Any]:
        user, session = signed_in(request)
        if not verify_password(body.current, user.password_hash):
            raise error(401, "bad_credentials", "The current password is wrong")
        problem = password_problems(body.new)
        if problem:
            raise error(400, "weak_password", problem)
        store.update_user(user.id, password_hash=hash_password(body.new), password_changed_at=today())
        signed_out = store.sign_out_all(user.id, except_session=session.id, reason="password changed")
        store.audit(user.id, "change_password", "user", user.id)
        store.add_notice(user.id, kind="security", subject="Your password was changed", text=f"{signed_out} other device(s) were signed out.", actor="service")
        return {"changed": True, "signed_out": signed_out}

    # ----- two-factor -----
    @app.post("/v1/me/two-factor/setup")
    async def tfa_setup(request: Request) -> dict[str, Any]:
        user, _ = signed_in(request)
        if user.two_factor:
            raise error(409, "two_factor_on", "Two-factor sign-in is on: turn it off first to change the app")
        secret = new_totp_secret()
        store.update_user(user.id, totp_secret=secret)
        return {"secret": secret, "otpauth_url": otpauth_url(secret, user.email)}

    @app.post("/v1/me/two-factor/enable")
    async def tfa_enable(body: CodeBody, request: Request) -> dict[str, Any]:
        user, _ = signed_in(request)
        if not user.totp_secret or user.two_factor:
            raise error(409, "no_setup", "Call setup first")
        if not verify_totp(user.totp_secret, body.code):
            raise error(400, "bad_code", "The code does not match the app: check the phone's clock")
        codes = new_recovery_codes()
        store.update_user(user.id, totp_enabled_at=today(), recovery_codes=[hash_recovery(c) for c in codes])
        store.audit(user.id, "two_factor_on", "user", user.id)
        return {"enabled": True, "recovery_codes": codes}

    @app.post("/v1/me/two-factor/disable")
    async def tfa_disable(body: CodeBody, request: Request) -> dict[str, Any]:
        user, _ = signed_in(request)
        if not user.two_factor:
            return {"enabled": False}
        second_factor(user, body.code, body.code)
        store.update_user(user.id, totp_secret="", totp_enabled_at="", recovery_codes=[])
        store.audit(user.id, "two_factor_off", "user", user.id)
        store.add_notice(user.id, kind="security", subject="Two-factor sign-in was turned off", text="Turn it on again from Security if this was not you.", actor="service")
        return {"enabled": False}

    @app.post("/v1/me/two-factor/recovery-codes")
    async def tfa_recovery(body: CodeBody, request: Request) -> dict[str, Any]:
        user, _ = signed_in(request)
        if not user.two_factor:
            raise error(409, "two_factor_off", "Two-factor sign-in is off")
        second_factor(user, body.code, "")
        codes = new_recovery_codes()
        store.update_user(user.id, recovery_codes=[hash_recovery(c) for c in codes])
        return {"recovery_codes": codes}

    # ----- devices -----
    @app.get("/v1/me/devices")
    async def devices(request: Request) -> dict[str, Any]:
        user, session = signed_in(request)
        return {"devices": [s.public(session.id) for s in store.sessions_of(user.id)]}

    @app.put("/v1/me/devices/{device_id}")
    async def rename_device(device_id: str, body: NameBody, request: Request) -> dict[str, Any]:
        user, session = signed_in(request)
        target = store.get_session(device_id)
        if not target or target.user_id != user.id or target.signed_out_at:
            raise error(404, "no_such_device", "No such device")
        store.rename_session(device_id, body.name.strip())
        return {"device": store.get_session(device_id).public(session.id)}   # type: ignore[union-attr]

    @app.delete("/v1/me/devices/{device_id}")
    async def signout_device(device_id: str, request: Request) -> dict[str, Any]:
        user, _ = signed_in(request)
        target = store.get_session(device_id)
        if not target or target.user_id != user.id:
            raise error(404, "no_such_device", "No such device")
        store.sign_out(device_id, reason="by the user")
        return {"signed_out": True}

    @app.post("/v1/me/signout")
    async def signout(request: Request) -> dict[str, Any]:
        _, session = signed_in(request)
        store.sign_out(session.id)
        return {"signed_out": True}

    @app.post("/v1/me/signout-all")
    async def signout_all(request: Request) -> dict[str, Any]:
        user, session = signed_in(request)
        return {"signed_out": store.sign_out_all(user.id, except_session=session.id, reason="by the user")}

    @app.get("/v1/me/sign-ins")
    async def sign_ins(request: Request) -> dict[str, Any]:
        user, _ = signed_in(request)
        return {"sign_ins": store.signins_of(user.id)}

    # ----- licences -----
    @app.get("/v1/me/licences")
    async def licences(request: Request) -> dict[str, Any]:
        user, _ = signed_in(request)
        return {"licences": [l.public() for l in store.licences_of(user.id)]}

    @app.post("/v1/me/licences")
    async def attach_licence(body: LicenceBody, request: Request) -> dict[str, Any]:
        user, _ = signed_in(request)
        if not settings.licence_key:
            raise error(503, "not_configured", "The service has no licence key to verify a licence with")
        payload = verify_licence(body.licence, settings.licence_key)
        if not payload:
            raise error(400, "bad_licence", "The licence is not one the payment service signed")
        try:
            lic = store.attach_licence(user.id, licence_id=str(payload.get("licence", "")), app=str(payload.get("app", "")), order_id=str(payload.get("order", "")),
                                       token=body.licence, issued=str(payload.get("issued", "")), updates=str(payload.get("updates", "")),
                                       updates_until=str(payload.get("updates_until", "")), valid_until=str(payload.get("valid_until", "")),
                                       seats=body.seats or settings.default_seats)
        except ValueError:
            raise error(409, "licence_taken", "That licence is attached to another account") from None
        return {"licence": lic.public()}

    @app.delete("/v1/me/licences/{licence_id}")
    async def release_licence(licence_id: str, request: Request) -> dict[str, Any]:
        user, _ = signed_in(request)
        licence_or_404(user, licence_id)
        store.release_licence(licence_id, user.id)
        return {"released": True}

    @app.post("/v1/me/licences/{licence_id}/install")
    async def install_licence(licence_id: str, request: Request) -> dict[str, Any]:
        user, session = signed_in(request)
        lic = licence_or_404(user, licence_id)
        try:
            return {"licence": store.install(lic, session.id).public()}
        except ValueError:
            raise error(409, "no_seat", f"Every seat of this licence is taken ({lic.seats}): sign a machine out to free one") from None

    @app.post("/v1/me/licences/{licence_id}/uninstall")
    async def uninstall_licence(licence_id: str, request: Request) -> dict[str, Any]:
        user, session = signed_in(request)
        return {"licence": store.uninstall(licence_or_404(user, licence_id), session.id).public()}

    # ----- notices -----
    @app.get("/v1/me/notices")
    async def my_notices(request: Request) -> dict[str, Any]:
        user, _ = signed_in(request)
        return {"notices": [n.public() for n in store.notices_of(user.id)]}

    @app.post("/v1/me/notices/{notice_id}/read")
    async def read_notice(notice_id: str, request: Request) -> dict[str, Any]:
        user, _ = signed_in(request)
        if not store.mark_read(user.id, notice_id):
            raise error(404, "no_such_notice", "No such notice")
        return {"read": True}

    # ----- the data archive, the deletion -----
    @app.post("/v1/me/archive")
    async def request_archive(request: Request) -> dict[str, Any]:
        user, _ = signed_in(request)
        open_one = store.open_archive(user.id)
        if open_one:
            return {"archive": open_one.public()}
        return {"archive": store.build_archive(user).public()}

    @app.get("/v1/me/archive")
    async def archive_state(request: Request) -> dict[str, Any]:
        user, _ = signed_in(request)
        return {"archives": [a.public() for a in store.archives_of(user.id)]}

    @app.get("/v1/me/archive/{archive_id}/download")
    async def download_archive(archive_id: str, request: Request) -> Response:
        user, _ = signed_in(request)
        data = store.take_archive(user.id, archive_id)
        if data is None:
            raise error(404, "not_released", "No released archive with that id, or it was downloaded already")
        return Response(content=data, media_type="application/json",
                        headers={"Content-Disposition": f'attachment; filename="ultrastore-account-{user.id}.json"'})

    @app.post("/v1/me/delete")
    async def request_delete(body: DeleteBody, request: Request) -> dict[str, Any]:
        user, session = signed_in(request)
        if not verify_password(body.password, user.password_hash):
            raise error(401, "bad_credentials", "The password is wrong")
        user = store.request_deletion(user.id, grace_days=settings.deletion_grace_days)
        send_mail(user.email, "Your UltraStore account will be deleted",
                  f"Your account is deleted on {user.delete_after}. Sign in before then and cancel from Privacy and data to keep it. "
                  "Bought apps keep working on the machines they are on; the invoices are kept as the law asks.")
        return me(user, session)

    @app.post("/v1/me/delete/cancel")
    async def cancel_delete(request: Request) -> dict[str, Any]:
        user, session = signed_in(request)
        if user.state != "deleting":
            return me(user, session)
        return me(store.cancel_deletion(user.id), session)

    # ----- the subscription -----
    def may_text(user: User) -> bool:
        """A text message costs money: only an account on a paid plan is sent one."""
        return bool(store.plan_of(user.id)["features"].get("sms"))

    @app.get("/v1/plans")
    async def plans() -> dict[str, Any]:
        return {"plans": [{"plan": name, "paid": bool(p["paid"]), "features": p["features"]} for name, p in PLANS.items()],
                "subscription_app": settings.subscription_app}

    @app.get("/v1/me/subscription")
    async def my_subscription(request: Request) -> dict[str, Any]:
        user, _ = signed_in(request)
        return {**store.plan_of(user.id), "history": [s.public() for s in store.subscriptions_of(user.id)]}

    @app.post("/v1/me/subscription")
    async def subscribe(body: SubscriptionBody, request: Request) -> dict[str, Any]:
        """The paid plan, from a payment-service licence for the subscription
        app; a renewal of the same licence moves the end date."""
        user, _ = signed_in(request)
        if not settings.licence_key:
            raise error(503, "not_configured", "The service has no licence key to verify a licence with")
        payload = verify_licence(body.licence, settings.licence_key)
        if not payload:
            raise error(400, "bad_licence", "The licence is not one the payment service signed")
        if str(payload.get("app", "")) != settings.subscription_app:
            raise error(400, "not_a_subscription", f"That licence is for {payload.get('app', '')}, not for the {settings.subscription_app} subscription")
        valid_until = str(payload.get("valid_until", ""))
        if valid_until and valid_until[:10] < today():
            raise error(400, "expired", f"That subscription ran out on {valid_until[:10]}")
        try:
            lic = store.attach_licence(user.id, licence_id=str(payload.get("licence", "")), app=settings.subscription_app, order_id=str(payload.get("order", "")),
                                       token=body.licence, issued=str(payload.get("issued", "")), updates=str(payload.get("updates", "")),
                                       updates_until=str(payload.get("updates_until", "")), valid_until=valid_until, seats=0)
        except ValueError:
            raise error(409, "licence_taken", "That licence is attached to another account") from None
        store.add_subscription(user.id, plan="plus", source="licence", valid_until=valid_until, actor=user.id, licence_id=lic.id)
        store.add_notice(user.id, kind="notice", subject="Your UltraAccount Plus subscription is active",
                         text=("It runs until " + valid_until[:10] + "." if valid_until else "It runs until cancelled."), actor="service")
        return {**store.plan_of(user.id), "history": [s.public() for s in store.subscriptions_of(user.id)]}

    # ----- the phone -----
    @app.post("/v1/me/phone")
    async def add_phone(body: PhoneBody, request: Request) -> dict[str, Any]:
        user, _ = signed_in(request)
        if not verify_password(body.password, user.password_hash):
            raise error(401, "bad_credentials", "The password is wrong")
        if not may_text(user):
            raise error(402, "subscription_required", "A phone number needs a paid plan: a text message costs money to send")
        phone = normalise_phone(body.phone)
        if not phone:
            raise error(400, "bad_phone", "An international number with the country code, such as +49 151 23456789")
        if store.code_sent_recently(user.id, "verify_phone"):
            raise error(429, "code_sent", "A code went out a moment ago: wait a minute before asking for another")
        code = new_code()
        store.add_code(user.id, "verify_phone", code, minutes=settings.code_minutes, payload=phone)
        send_text(phone, f"Your UltraAccount code: {code}. It is good for {settings.code_minutes} minutes.")
        return {"verify": "sms_code", "phone": mask_phone(phone)}

    @app.post("/v1/me/phone/verify")
    async def verify_phone(body: CodeBody, request: Request) -> dict[str, Any]:
        user, session = signed_in(request)
        row = store.take_code(user.id, "verify_phone", body.code)
        if not row:
            raise error(400, "bad_code", "The code is wrong or has run out: ask for a new one")
        old = user.phone
        user = store.update_user(user.id, phone=row["payload"], phone_verified_at=today())
        store.audit(user.id, "verify_phone", "user", user.id, after={"phone": mask_phone(user.phone)})
        store.add_notice(user.id, kind="security", subject="A phone number was added to your account",
                         text=f"Codes can now be sent to {mask_phone(user.phone)}" + (f" instead of {mask_phone(old)}." if old and old != user.phone else "."),
                         actor="service")
        send_mail(user.email, "A phone number was added to your UltraAccount",
                  f"Your account now has the phone number {mask_phone(user.phone)}. If this was not you, change your password and remove it at once.")
        return me(user, session)

    @app.post("/v1/me/phone/remove")
    async def remove_phone(body: DeleteBody, request: Request) -> dict[str, Any]:
        user, session = signed_in(request)
        if not verify_password(body.password, user.password_hash):
            raise error(401, "bad_credentials", "The password is wrong")
        if user.phone:
            store.audit(user.id, "remove_phone", "user", user.id)
            user = store.update_user(user.id, phone="", phone_verified_at="")
            store.add_notice(user.id, kind="security", subject="The phone number was removed from your account", text="", actor="service")
        return me(user, session)

    # ----- the recovery-key escrow -----
    def escrow_key() -> str:
        if not settings.escrow_key or escrow_key_problem(settings.escrow_key):
            raise error(503, "not_configured", "The service has no escrow key: recovery keys cannot be kept")
        return settings.escrow_key

    def escrow_owner(eid: str) -> tuple[Escrow, User]:
        escrow = store.get_escrow(eid)
        owner = store.get_user(escrow.user_id) if escrow else None
        if not escrow or not owner or owner.state == "deleted":
            raise error(404, "no_such_key", "No such recovery key: it was removed, or the account is gone")
        if owner.state == "suspended":
            raise error(403, "suspended", "The account is suspended")
        return escrow, owner

    def channels(owner: User) -> list[dict[str, str]]:
        out = []
        if owner.email_verified:
            out.append({"channel": "email", "to": mask_email(owner.email)})
        if owner.phone and owner.phone_verified_at and may_text(owner):
            out.append({"channel": "sms", "to": mask_phone(owner.phone)})
        return out

    @app.get("/v1/me/recovery-keys")
    async def my_escrows(request: Request) -> dict[str, Any]:
        user, _ = signed_in(request)
        return {"recovery_keys": [e.public() for e in store.escrows_of(user.id)], "channels": channels(user)}

    @app.post("/v1/me/recovery-keys")
    async def add_escrow(body: EscrowBody, request: Request) -> dict[str, Any]:
        """An application's recovery key left with the account. The key is the
        service's half only; the application keeps the other half itself."""
        user, _ = signed_in(request)
        key = escrow_key()
        try:
            secret = bytes.fromhex(body.secret)
        except ValueError:
            raise error(400, "bad_secret", "The recovery key is 32 bytes, hex") from None
        eid = store.new_escrow_id()
        try:
            escrow = store.add_escrow(user.id, eid=eid, app=body.app.strip(), label=body.label.strip(), sealed=seal_escrow(key, eid, user.id, secret))
        except ValueError:
            raise error(409, "too_many", "This account holds as many recovery keys as it may: remove one first") from None
        store.add_notice(user.id, kind="security", subject=f"A recovery key was stored for {escrow.app}",
                         text=(escrow.label + ": " if escrow.label else "") + "a code to your email or phone will release it. Remove it from "
                              "Recovery keys if this was not you.", actor="service")
        return {"recovery_key": escrow.public(), "channels": channels(user)}

    @app.delete("/v1/me/recovery-keys/{escrow_id}")
    async def delete_escrow(escrow_id: str, request: Request) -> dict[str, Any]:
        user, _ = signed_in(request)
        escrow = store.get_escrow(escrow_id)
        if not escrow or escrow.user_id != user.id:
            raise error(404, "no_such_key", "No such recovery key on this account")
        store.delete_escrow(escrow_id, user.id)
        return {"deleted": True}

    @app.get("/v1/recovery-keys/{escrow_id}")
    async def escrow_info(escrow_id: str) -> dict[str, Any]:
        escrow, owner = escrow_owner(escrow_id)
        return {"id": escrow.id, "app": escrow.app, "label": escrow.label, "channels": channels(owner)}

    @app.post("/v1/recovery-keys/{escrow_id}/code")
    async def escrow_code(escrow_id: str, body: ChannelBody) -> dict[str, Any]:
        escrow, owner = escrow_owner(escrow_id)
        offered = {c["channel"]: c["to"] for c in channels(owner)}
        if body.channel not in offered:
            if body.channel == "sms":
                raise error(402, "subscription_required", "Codes by text message need a verified phone number and a paid plan")
            raise error(400, "bad_channel", "A code goes to the account's email or, on a paid plan, its phone")
        if store.code_sent_recently(owner.id, "recover_escrow"):
            raise error(429, "code_sent", "A code went out a moment ago: wait a minute before asking for another")
        if store.codes_sent_since(owner.id, "recover_escrow", 86400) >= settings.recovery_codes_per_day:
            raise error(429, "too_many_codes", "Too many recovery codes today: try again tomorrow")
        code = new_code()
        store.add_code(owner.id, "recover_escrow", code, minutes=settings.code_minutes, payload=escrow.id + "|" + body.channel)
        what = f"the recovery key of {escrow.app}" + (f" ({escrow.label})" if escrow.label else "")
        if body.channel == "sms":
            send_text(owner.phone, f"UltraAccount: {code} releases {what}. Good for {settings.code_minutes} minutes. Not you? Ignore it.")
        else:
            send_mail(owner.email, f"Your code to recover {escrow.app}",
                      f"Someone - hopefully you - asked to recover {what}, for a forgotten password.\n\nYour code: {code}\n\n"
                      f"It is good for {settings.code_minutes} minutes. If this was not you, ignore this mail: without the code nothing is released, "
                      "and without the vault file the key opens nothing.")
        return {"sent": True, "channel": body.channel, "to": offered[body.channel]}

    @app.post("/v1/recovery-keys/{escrow_id}/release")
    async def escrow_release(escrow_id: str, body: CodeBody) -> dict[str, Any]:
        escrow, owner = escrow_owner(escrow_id)
        key = escrow_key()
        row = store.take_code(owner.id, "recover_escrow", body.code)
        if not row or not row["payload"].startswith(escrow.id + "|"):
            raise error(400, "bad_code", "The code is wrong or has run out: ask for a new one")
        channel = row["payload"].split("|", 1)[1]
        secret = open_escrow(key, escrow.id, owner.id, escrow.sealed)
        if secret is None:
            raise error(500, "unreadable", "The recovery key could not be opened with the service's escrow key")
        store.mark_escrow_released(escrow.id, channel=channel)
        store.add_notice(owner.id, kind="security", subject=f"The recovery key of {escrow.app} was released",
                         text=f"A code sent by {'text message' if channel == 'sms' else 'mail'} released it. If this was not you, "
                              "change your password and the master password of that app at once.", actor="service")
        send_mail(owner.email, f"The recovery key of {escrow.app} was released",
                  f"The recovery key of {escrow.app}" + (f" ({escrow.label})" if escrow.label else "") +
                  " was released with a code just now. If this was not you, change your passwords at once.")
        return {"secret": secret.hex(), "app": escrow.app}

    # ----- the operator -----
    def user_file(u: User) -> dict[str, Any]:
        return {**u.file(), "subscription": store.plan_of(u.id), "subscriptions": [s.public() for s in store.subscriptions_of(u.id)],
                "recovery_keys": [e.public() for e in store.escrows_of(u.id)],
                "devices": [s.public() for s in store.sessions_of(u.id)],
                "licences": [l.public(with_token=False) for l in store.licences_of(u.id)], "notices": [n.public() for n in store.notices_of(u.id)],
                "archives": [a.public() for a in store.archives_of(u.id)], "history": store.audit_of(u.id),
                "sign_ins": store.signins_of(u.id, limit=20)}

    def maintenance(actor: str) -> dict[str, int]:
        deleted = 0
        for u in store.due_deletions():
            store.delete_user(u.id, actor=actor, reason="requested, grace over")
            deleted += 1
        lifted = 0
        for u in store.expired_suspensions():
            store.reinstate_user(u.id, reason="suspension over", actor=actor)
            lifted += 1
        return {"deleted": deleted, "reinstated": lifted}

    @app.get("/v1/users")
    async def list_users(state: str = "", q: str = "", authorization: str | None = Header(default=None)) -> dict[str, Any]:
        operator(authorization, "users.read")
        maintenance("service")
        requests_open = {r.user_id for u in store.users() for r in store.archives_of(u.id) if not r.released_at}
        out = []
        for u in store.users(state=state, query=q):
            row = u.file()
            row["devices"] = len(store.sessions_of(u.id))
            row["licences"] = len(store.licences_of(u.id))
            row["request"] = ("deletion" if u.state == "deleting" else "archive" if u.id in requests_open else "")
            out.append(row)
        return {"users": out, "stats": store.stats()}

    @app.get("/v1/users/{user_id}")
    async def get_user(user_id: str, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        operator(authorization, "users.read")
        return user_file(user_or_404(user_id))

    @app.post("/v1/users/{user_id}/notice")
    async def notify_user(user_id: str, body: UserNoticeBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "users.support")
        u = user_or_404(user_id)
        if body.kind not in NOTICE_KINDS:
            raise error(400, "bad_kind", "A notice is one of " + ", ".join(NOTICE_KINDS))
        n = store.add_notice(u.id, kind=body.kind, subject=body.subject, text=body.text, actor=actor)
        send_mail(u.email, f"UltraStore: {body.subject}", body.text + "\n\nThis notice is also in your account under Notifications.")
        return {"notice": n.public()}

    @app.post("/v1/users/{user_id}/signout-all")
    async def op_signout_all(user_id: str, body: ReasonBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "users.support")
        u = user_or_404(user_id)
        count = store.sign_out_all(u.id, reason="by the operator")
        store.audit(actor, "signout_all", "user", u.id, after={"reason": body.reason, "devices": count})
        return {"signed_out": count}

    @app.post("/v1/users/{user_id}/two-factor/reset")
    async def op_tfa_reset(user_id: str, body: ReasonBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "users.manage")
        u = user_or_404(user_id)
        store.update_user(u.id, totp_secret="", totp_enabled_at="", recovery_codes=[])
        store.sign_out_all(u.id, reason="two-factor reset")
        store.audit(actor, "two_factor_reset", "user", u.id, after={"reason": body.reason})
        store.add_notice(u.id, kind="security", subject="Two-factor sign-in was reset", text="The operator reset your second factor after checking your identity. "
                         "Sign in with your password and set it up again from Security.", actor=actor)
        send_mail(u.email, "Your UltraStore two-factor sign-in was reset", "Sign in with your password and set two-factor up again from Security.")
        return {"reset": True}

    @app.post("/v1/users/{user_id}/suspend")
    async def op_suspend(user_id: str, body: SuspendBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "users.manage")
        u = user_or_404(user_id)
        u = store.suspend_user(u.id, reason=body.reason, days=body.days, actor=actor)
        store.add_notice(u.id, kind="suspension", subject="Your account is suspended", text=body.reason + (f"\nUntil {u.suspended_until}." if u.suspended_until else ""),
                         actor=actor)
        send_mail(u.email, "Your UltraStore account is suspended", body.reason or "Write to us if you think this is a mistake.")
        return {"user": u.file()}

    @app.post("/v1/users/{user_id}/reinstate")
    async def op_reinstate(user_id: str, body: ReasonBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "users.manage")
        u = user_or_404(user_id)
        if u.state != "suspended":
            raise error(409, "not_suspended", "The account is not suspended")
        u = store.reinstate_user(u.id, reason=body.reason, actor=actor)
        store.add_notice(u.id, kind="notice", subject="Your account is active again", text=body.reason, actor=actor)
        return {"user": u.file()}

    @app.post("/v1/users/{user_id}/delete")
    async def op_delete(user_id: str, body: ReasonBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "users.manage")
        u = user_or_404(user_id)
        if u.state != "deleting":
            raise error(409, "not_requested", "The person has not asked for the deletion: only a requested deletion is carried out")
        store.delete_user(u.id, actor=actor, reason=body.reason or "requested")
        return {"deleted": True}

    @app.post("/v1/users/{user_id}/subscription")
    async def op_grant(user_id: str, body: GrantBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "users.manage")
        u = user_or_404(user_id)
        if body.plan not in PLANS or not PLANS[body.plan]["paid"]:
            raise error(400, "bad_plan", "A plan granted is one of " + ", ".join(p for p, v in PLANS.items() if v["paid"]))
        until = add_days(today(), body.days) if body.days > 0 else ""
        store.add_subscription(u.id, plan=body.plan, source="operator", valid_until=until, actor=actor)
        store.add_notice(u.id, kind="notice", subject=f"Your account is on {body.plan}", text=body.reason + (f"\nUntil {until}." if until else ""), actor=actor)
        return {"subscription": store.plan_of(u.id)}

    @app.post("/v1/users/{user_id}/subscription/cancel")
    async def op_cancel(user_id: str, body: ReasonBody, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "users.manage")
        u = user_or_404(user_id)
        count = store.cancel_subscriptions(u.id, actor=actor, reason=body.reason)
        if count:
            store.add_notice(u.id, kind="notice", subject="Your subscription was cancelled", text=body.reason, actor=actor)
        return {"cancelled": count, "subscription": store.plan_of(u.id)}

    @app.post("/v1/archives/{archive_id}/release")
    async def op_release_archive(archive_id: str, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        actor = operator(authorization, "users.support")
        a = store.release_archive(archive_id, actor)
        if not a:
            raise error(404, "no_such_archive", "No such archive")
        u = store.get_user(a.user_id)
        if u:
            store.add_notice(u.id, kind="notice", subject="Your data archive is ready", text="Download it from Privacy and data; it can be downloaded once.", actor=actor)
            send_mail(u.email, "Your UltraStore data archive is ready", "Download it from Privacy and data in the store. It can be downloaded once.")
        return {"archive": a.public()}

    @app.post("/v1/maintenance")
    async def op_maintenance(authorization: str | None = Header(default=None)) -> dict[str, Any]:
        return maintenance(operator(authorization, "users.manage"))

    @app.get("/v1/stats")
    async def stats(authorization: str | None = Header(default=None)) -> dict[str, Any]:
        operator(authorization, "users.read")
        return store.stats()

    @app.get("/v1/audit")
    async def audit(since: int = 0, limit: int = 500, authorization: str | None = Header(default=None)) -> dict[str, Any]:
        operator(authorization, "audit.read")
        return {"audit": store.audit_since(since, min(max(limit, 1), 2000))}

    @app.get("/v1/audit/verify")
    async def audit_verify(authorization: str | None = Header(default=None)) -> dict[str, Any]:
        operator(authorization, "audit.read")
        return store.verify_audit()

    @app.get("/healthz", response_class=PlainTextResponse)
    async def healthz() -> str:
        missing = settings.configured()
        return "ok" if not missing else "not configured: " + ", ".join(missing)

    return app
