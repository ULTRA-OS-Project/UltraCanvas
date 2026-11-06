"""The HTTP and WebSocket API of the ChatCube relay.

REST for everything that is a request (accounts, contacts, groups, the
offline queue, TURN credentials, the Telegram application's keys); one WebSocket per signed-in device for
what is pushed (presence, typing, P2P signalling, new queued messages).
The relay forwards signalling payloads and stores envelopes without reading
either: both are end-to-end protected by the clients."""

from __future__ import annotations

import asyncio
import base64
import hashlib
import hmac
import logging
import re
import secrets
import time
import urllib.parse
from contextlib import asynccontextmanager
from typing import Any

from fastapi import Depends, FastAPI, Header, HTTPException, Request, WebSocket, WebSocketDisconnect
from fastapi.responses import HTMLResponse, JSONResponse
from pydantic import BaseModel, Field, field_validator

from . import __version__
from .config import Settings
from cryptography.exceptions import InvalidTag
from cryptography.hazmat.primitives.asymmetric.ed25519 import Ed25519PrivateKey
from cryptography.hazmat.primitives.ciphers.aead import AESGCM
from cryptography.hazmat.primitives.serialization import Encoding, PublicFormat

from . import pages
from .identity import decode_b64, login_message, register_message, user_id_for, verify
from .notify import Notifier
from .store import GROUP_PREFIX, Store, hash_token, normalize_email, normalize_phone

log = logging.getLogger("chatcube_relay")

# Plausible, not proven: an address is proven by the mail that reaches it.
EMAIL_PATTERN = re.compile(r"[^@\s]+@[^@\s]+\.[^@\s]+")
# International form: "+" and 7 to 15 digits (E.164), spaces, dashes and
# brackets as people write them.
PHONE_PATTERN = re.compile(r"\+[0-9 ()\-]{7,24}")

SIGNAL_KINDS = {"offer", "answer", "ice", "hangup", "call-invite", "call-accept", "call-reject",
                "call-update"}


# ============================================================================
# CONNECTED DEVICES
# ============================================================================

class Hub:
    """The open WebSockets, by user. A user may be signed in on several
    devices; every one receives what is pushed to the user."""

    def __init__(self) -> None:
        self._sockets: dict[str, set[WebSocket]] = {}

    def add(self, user_id: str, ws: WebSocket) -> bool:
        first = user_id not in self._sockets
        self._sockets.setdefault(user_id, set()).add(ws)
        return first

    def remove(self, user_id: str, ws: WebSocket) -> bool:
        sockets = self._sockets.get(user_id)
        if not sockets:
            return False
        sockets.discard(ws)
        if not sockets:
            del self._sockets[user_id]
            return True
        return False

    def is_online(self, user_id: str) -> bool:
        return user_id in self._sockets

    async def send(self, user_id: str, frame: dict[str, Any]) -> bool:
        sent = False
        for ws in list(self._sockets.get(user_id, ())):
            try:
                await ws.send_json(frame)
                sent = True
            except Exception:  # a socket closing under us; its handler cleans up
                pass
        return sent


# ============================================================================
# REQUEST BODIES
# ============================================================================

class RegisterBody(BaseModel):
    public_key: str
    display_name: str = Field(min_length=1, max_length=64)
    signature: str


class ChallengeBody(BaseModel):
    user_id: str


class LoginBody(BaseModel):
    user_id: str
    nonce: str
    signature: str


class ProfileBody(BaseModel):
    # Each left out stays as it is; "" removes the e-mail or the phone. The
    # e-mail and the phone are kept to recover the account with, for its
    # owner alone (GET /v1/profile), never in what others see of the user.
    display_name: str | None = Field(default=None, min_length=1, max_length=64)
    email: str | None = Field(default=None, max_length=254)
    phone: str | None = Field(default=None, max_length=32)

    @field_validator("email")
    @classmethod
    def _email(cls, value: str | None) -> str | None:
        if value is None:
            return None
        value = value.strip()
        if value and not EMAIL_PATTERN.fullmatch(value):
            raise ValueError("not an e-mail address")
        return value

    @field_validator("phone")
    @classmethod
    def _phone(cls, value: str | None) -> str | None:
        if value is None:
            return None
        value = value.strip()
        digits = sum(c.isdigit() for c in value)
        if value and (not PHONE_PATTERN.fullmatch(value) or not 7 <= digits <= 15):
            raise ValueError("not a phone number in international form (+ and the digits)")
        return value


class VerifyBody(BaseModel):
    channel: str = Field(pattern="^(email|phone)$")
    code: str = Field(default="", max_length=16)


class EscrowBody(BaseModel):
    # The 64-byte Ed25519 secret key (seed and public key), base64.
    secret_key: str = Field(min_length=1, max_length=200)


class RecoveryStartBody(BaseModel):
    address: str = Field(min_length=3, max_length=254)     # the ID's e-mail address


class RecoveryPollBody(BaseModel):
    recovery_id: str = Field(min_length=8, max_length=128)
    poll_token: str = Field(min_length=16, max_length=128)


def new_code() -> str:
    return f"{secrets.randbelow(10 ** 6):06d}"


def digits_of(code: str) -> str:
    return "".join(c for c in code if c.isdigit())


class OutgoingMessage(BaseModel):
    to: str
    message_id: str = Field(min_length=1, max_length=64)
    conversation: str = Field(min_length=1, max_length=128)
    envelope: str


class MessagesBody(BaseModel):
    messages: list[OutgoingMessage]


class AckBody(BaseModel):
    ids: list[int]


class GroupBody(BaseModel):
    title: str = Field(min_length=1, max_length=64)
    members: list[str] = []


class MemberBody(BaseModel):
    user_id: str


def user_json(user, online: bool | None = None) -> dict[str, Any]:
    out = {"user_id": user.user_id,
           "public_key": base64.b64encode(user.public_key).decode(),
           "display_name": user.display_name}
    if online is not None:
        out["presence"] = "online" if online else "offline"
    return out


def group_json(group) -> dict[str, Any]:
    return {"group_id": group.group_id, "conversation": GROUP_PREFIX + group.group_id,
            "title": group.title, "owner": group.owner, "members": group.members}


# ============================================================================
# APPLICATION
# ============================================================================

def create_app(settings: Settings | None = None, notifier: Notifier | None = None) -> FastAPI:
    settings = settings or Settings.from_environment()
    notifier = notifier or Notifier(settings)
    store = Store(settings.database)
    recovery_key = decode_b64(settings.recovery_key) if settings.recovery_key else None
    if recovery_key is not None and len(recovery_key) != 32:
        raise ValueError("CHATCUBE_RELAY_RECOVERY_KEY must be 32 bytes, base64 (openssl rand -base64 32)")
    hub = Hub()
    challenges: dict[str, tuple[str, float]] = {}     # nonce -> (user_id, expiry)

    @asynccontextmanager
    async def lifespan(_app: FastAPI):
        def tidy():
            store.expire(settings.queue_ttl_seconds)
            pruned = {table: n for table, n in store.prune().items() if n}
            if pruned:
                log.info("housekeeping: deleted %s", ", ".join(f"{n} from {t}" for t, n in pruned.items()))

        async def housekeeping():
            while True:
                await asyncio.sleep(3600)
                tidy()
        tidy()                                  # what expired while the relay was down
        task = asyncio.create_task(housekeeping())
        try:
            yield
        finally:
            task.cancel()
            store.close()

    app = FastAPI(title="ChatCube relay", version=__version__, lifespan=lifespan)
    app.state.store = store
    app.state.hub = hub

    @app.exception_handler(HTTPException)
    async def http_error(_request: Request, exc: HTTPException):
        # Errors are {"error", "error_description"} at the top level.
        content = exc.detail if isinstance(exc.detail, dict) else {"error": "error", "error_description": str(exc.detail)}
        return JSONResponse(status_code=exc.status_code, content=content)

    def error(status: int, code: str, description: str) -> HTTPException:
        return HTTPException(status_code=status, detail={"error": code, "error_description": description})

    def current_user(authorization: str = Header(default="")) -> str:
        if not authorization.startswith("Bearer "):
            raise error(401, "unauthorized", "Bearer token required")
        user_id = store.session_user(authorization[7:])
        if not user_id:
            raise error(401, "unauthorized", "Session expired or unknown")
        return user_id

    async def push_presence(user_id: str, online: bool) -> None:
        frame = {"type": "presence", "from": user_id, "status": "online" if online else "offline"}
        for watcher in store.watchers(user_id):
            await hub.send(watcher, frame)

    async def push_group(group) -> None:
        frame = {"type": "group.update", "group": group_json(group)}
        for member in group.members:
            await hub.send(member, frame)

    # ---- service -------------------------------------------------------------

    @app.get("/healthz")
    async def healthz():
        return {"status": "ok", "version": __version__}

    # ---- accounts ------------------------------------------------------------

    @app.post("/v1/register")
    async def register(body: RegisterBody):
        public_key = decode_b64(body.public_key)
        signature = decode_b64(body.signature)
        if not public_key or len(public_key) != 32 or not signature:
            raise error(400, "invalid_request", "public_key and signature must be base64")
        if not verify(public_key, register_message(body.public_key, body.display_name), signature):
            raise error(400, "invalid_signature", "The signature does not prove the key")
        user_id = user_id_for(public_key)
        if not store.add_user(user_id, public_key, body.display_name):
            existing = store.get_user(user_id)
            if not existing or existing.public_key != public_key:
                raise error(409, "conflict", "User id already taken")
        return {"user_id": user_id}

    @app.post("/v1/login/challenge")
    async def login_challenge(body: ChallengeBody):
        if not store.get_user(body.user_id):
            raise error(404, "unknown_user", "No such user")
        now = time.time()
        for nonce, (_, expiry) in list(challenges.items()):
            if expiry < now:
                del challenges[nonce]
        nonce = base64.b64encode(secrets.token_bytes(32)).decode()
        challenges[nonce] = (body.user_id, now + settings.challenge_ttl_seconds)
        return {"nonce": nonce, "expires_in": settings.challenge_ttl_seconds}

    @app.post("/v1/login")
    async def login(body: LoginBody):
        entry = challenges.pop(body.nonce, None)       # a nonce is good for one attempt
        if not entry or entry[0] != body.user_id or entry[1] < time.time():
            raise error(401, "invalid_challenge", "Unknown or expired challenge")
        user = store.get_user(body.user_id)
        signature = decode_b64(body.signature)
        if not user or not signature or not verify(user.public_key, login_message(body.nonce), signature):
            raise error(401, "invalid_signature", "The signature does not match the key")
        token = secrets.token_urlsafe(32)
        expires = store.add_session(token, user.user_id, settings.token_ttl_seconds)
        return {"token": token, "expires_at": expires, "user_id": user.user_id,
                "display_name": user.display_name}

    @app.post("/v1/logout")
    async def logout(authorization: str = Header(default=""), user_id: str = Depends(current_user)):
        store.remove_session(authorization[7:])
        return {"status": "ok", "user_id": user_id}

    # ---- the owner's e-mail and phone; account recovery ---------------------
    #
    # The e-mail address is confirmed by a link the relay e-mails to it - at
    # registration (ChatCube sends the address right after its first sign-in)
    # and whenever it changes; the phone number by a code sent by SMS. A user
    # with a confirmed address can turn recovery on: the relay then keeps a
    # copy of their key, encrypted with its recovery key (README: "Account
    # recovery"). On another computer, ChatCube asks for a recovery by that
    # address; the relay e-mails a link to it - only when an ID really has
    # it, answering the same either way - and once the link's page is
    # approved, the asking computer, which polls with a token only it holds,
    # gets the key.

    def recovery_offered() -> bool:
        return recovery_key is not None and notifier.can_email

    def send_later(job) -> None:
        # After the answer, on a worker thread: an SMTP server or an SMS
        # gateway takes its time, and how long must not tell anything.
        asyncio.get_running_loop().run_in_executor(None, job)

    def public_base(request: Request) -> str:
        return settings.public_url or str(request.base_url).rstrip("/")

    def rate_limited(*limits: tuple[str, int]) -> bool:
        """True when any (key, per hour) is used up; otherwise each is counted."""
        if any(store.count_sends(key, 3600) >= per_hour for key, per_hour in limits):
            return True
        for key, _ in limits:
            store.note_send(key)
        return False

    def send_confirmation(request: Request, user_id: str, email: str) -> bool:
        """A link that confirms `email` for `user_id`, e-mailed to it."""
        if not notifier.can_email:
            return False
        if rate_limited((f"confirm:{user_id}", settings.sends_per_hour)):
            raise error(429, "too_many_links", "Too many links were sent - try again in an hour")
        token = secrets.token_urlsafe(32)
        store.put_link(token, "confirm", user_id, email, settings.confirm_link_ttl_seconds)
        link = f"{public_base(request)}/confirm-email?token={token}"
        hours = settings.confirm_link_ttl_seconds // 3600
        send_later(lambda: notifier.send_email(
            email, "ChatCube: confirm your e-mail address",
            "Please confirm that this e-mail address belongs to your ChatCube ID - open this link:\n\n"
            f"{link}\n\nIt works for {hours} hours. With the address confirmed, you can turn on recovery in "
            "ChatCube's Edit profile window and get your ID back by e-mail if you lose your computer.\n\n"
            "If you did not add this address to a ChatCube ID, ignore this message - nothing happens."))
        return True

    def send_phone_code(user_id: str, phone: str) -> bool:
        if not notifier.can_sms:
            return False
        limit_key = f"verify:{user_id}:phone"
        if rate_limited((limit_key, settings.sends_per_hour)):
            raise error(429, "too_many_codes", "Too many codes were sent - try again in an hour")
        code = new_code()
        store.put_code(limit_key, code, settings.code_ttl_seconds)
        minutes = settings.code_ttl_seconds // 60
        send_later(lambda: notifier.send_sms(
            normalize_phone(phone),
            f"ChatCube code {code} - confirms this phone number for your ChatCube ID ({minutes} min)."))
        return True

    def own_profile(user_id: str, sent: list[str] | None = None) -> dict[str, Any]:
        c = store.get_contacts(user_id)
        return {**user_json(store.get_user(user_id)), "email": c.email, "phone": c.phone,
                "email_verified": c.email_verified, "phone_verified": c.phone_verified,
                "recovery_available": recovery_offered(), "phone_codes_available": notifier.can_sms,
                "recovery_enabled": store.get_escrow(user_id) is not None,
                "sent": sent or []}

    @app.get("/v1/profile")
    async def get_profile(user_id: str = Depends(current_user)):
        return own_profile(user_id)

    @app.put("/v1/profile")
    async def set_profile(body: ProfileBody, request: Request, user_id: str = Depends(current_user)):
        if body.display_name is not None:
            store.set_display_name(user_id, body.display_name)
        sent: list[str] = []
        if body.email is not None or body.phone is not None:
            # A changed address gets a confirmation link, a changed number a code.
            for channel in store.set_contacts(user_id, body.email, body.phone):
                if channel == "email" and send_confirmation(request, user_id, body.email or ""):
                    sent.append("email")
                elif channel == "phone" and send_phone_code(user_id, body.phone or ""):
                    sent.append("phone")
        return own_profile(user_id, sent)

    @app.post("/v1/profile/verify")
    async def verify_phone(body: VerifyBody, user_id: str = Depends(current_user)):
        if body.channel == "email":
            raise error(400, "use_the_link", "The e-mail address is confirmed by the link e-mailed to it")
        if not store.get_contacts(user_id).phone:
            raise error(404, "not_set", "There is no phone number to confirm")
        if not store.take_code(f"verify:{user_id}:phone", digits_of(body.code), settings.code_attempts):
            raise error(400, "invalid_code", "That is not the code, or it has expired - ask for a new one")
        store.mark_verified(user_id, "phone")
        return own_profile(user_id)

    @app.post("/v1/profile/verify/resend")
    async def resend(body: VerifyBody, request: Request, user_id: str = Depends(current_user)):
        contacts = store.get_contacts(user_id)
        email = body.channel == "email"
        address = contacts.email if email else contacts.phone
        if not address:
            raise error(404, "not_set", "There is no address to confirm")
        if contacts.email_verified if email else contacts.phone_verified:
            return own_profile(user_id)
        sent = send_confirmation(request, user_id, address) if email else send_phone_code(user_id, address)
        if not sent:
            raise error(503, "not_configured", "This relay cannot send to that channel")
        return own_profile(user_id, [body.channel])

    @app.get("/confirm-email", response_class=HTMLResponse)
    async def confirm_email(token: str = ""):
        link = store.find_link(token, "confirm") if token else None
        if link and normalize_email(store.get_contacts(link["user_id"]).email) != normalize_email(link["email"]):
            link = None             # the address changed since: this link is for the old one
        if not link or not store.use_link(token):
            return HTMLResponse(pages.page(
                "This link does not work any more",
                ["It has expired, it was used already, or the address was changed since it was sent.",
                 "In ChatCube, open Edit profile and choose Send again under your e-mail address for a new one."],
                bad=True), status_code=410)
        store.mark_verified(link["user_id"], "email")
        user = store.get_user(link["user_id"])
        return HTMLResponse(pages.page(
            "Your e-mail address is confirmed",
            [f"{link['email']} now belongs to the ChatCube ID of {user.display_name if user else 'yours'}.",
             "You can close this page. In ChatCube's Edit profile window you can now turn on recovery, "
             "to get your ID back by e-mail if you lose your computer."]))

    @app.put("/v1/recovery/escrow")
    async def put_escrow(body: EscrowBody, user_id: str = Depends(current_user)):
        if not recovery_offered():
            raise error(503, "not_configured", "This relay offers no account recovery")
        if not store.get_contacts(user_id).email_verified:
            raise error(409, "not_verified", "Confirm the e-mail address first")
        secret = decode_b64(body.secret_key)
        user = store.get_user(user_id)
        if not secret or len(secret) != 64 or not user:
            raise error(400, "invalid_request", "secret_key must be the 64-byte key, base64")
        # Only this user's own key: the public half must be theirs.
        public = Ed25519PrivateKey.from_private_bytes(secret[:32]).public_key().public_bytes(
            Encoding.Raw, PublicFormat.Raw)
        if public != user.public_key or secret[32:] != user.public_key:
            raise error(400, "wrong_key", "That is not this account's key")
        nonce = secrets.token_bytes(12)
        store.put_escrow(user_id, nonce, AESGCM(recovery_key).encrypt(nonce, secret, user_id.encode()))
        return own_profile(user_id)

    @app.delete("/v1/recovery/escrow")
    async def remove_escrow(user_id: str = Depends(current_user)):
        store.remove_escrow(user_id)
        return own_profile(user_id)

    @app.post("/v1/recovery/start")
    async def recovery_start(body: RecoveryStartBody, request: Request):
        if not recovery_offered():
            raise error(503, "not_configured", "This relay offers no account recovery")
        address = normalize_email(body.address)
        client = request.client.host if request.client else "?"
        if rate_limited(("recover:" + address, settings.sends_per_hour),
                        ("recover-from:" + client, settings.recovery_starts_per_client)):
            raise error(429, "too_many_links", "Too many recoveries were started - try again in an hour")
        # The same answer whether the address belongs to anyone: a recovery
        # is recorded either way; only one for an ID with that confirmed
        # address and recovery on gets a link.
        user_id = store.find_verified(address) if "@" in address else None
        if user_id and not store.get_escrow(user_id):
            user_id = None
        recovery_id = secrets.token_urlsafe(24)
        poll_token = secrets.token_urlsafe(32)
        store.start_recovery(recovery_id, user_id, poll_token, settings.recovery_ttl_seconds, client)
        if user_id:
            token = secrets.token_urlsafe(32)
            email = store.get_contacts(user_id).email
            store.put_link(token, "recover", user_id, email, settings.recovery_ttl_seconds, recovery_id)
            link = f"{public_base(request)}/recover?token={token}"
            minutes = settings.recovery_ttl_seconds // 60
            when = time.strftime("%Y-%m-%d %H:%M UTC", time.gmtime())
            send_later(lambda: notifier.send_email(
                email, "ChatCube: recover your ChatCube ID",
                f"Someone - you, we hope - asked at {when} to recover your ChatCube ID on another computer.\n\n"
                f"To let that computer sign in as you, open this link and confirm:\n\n{link}\n\n"
                f"It works for {minutes} minutes, once. If this was not you, ignore this message: without "
                "the link nobody gets your ID - and you can turn recovery off in ChatCube's Edit profile."))
        return {"recovery_id": recovery_id, "poll_token": poll_token,
                "expires_in": settings.recovery_ttl_seconds}

    def recover_link_page(token: str):
        link = store.find_link(token, "recover") if token else None
        recovery = store.get_recovery(link["recovery_id"]) if link else None
        if not link or not recovery or recovery["expires_at"] < time.time():
            return None, HTMLResponse(pages.page(
                "This link does not work any more",
                ["It has expired or was used already. Start the recovery again in ChatCube "
                 "(Recover my ChatCube ID, under the sign-in card) for a new one."], bad=True), status_code=410)
        return (link, recovery), None

    @app.get("/recover", response_class=HTMLResponse)
    async def recover_page(token: str = ""):
        # Opening the link only asks: a mail scanner opening it approves nothing.
        found, refused = recover_link_page(token)
        if refused:
            return refused
        link, recovery = found
        user = store.get_user(link["user_id"])
        asked = time.strftime("%Y-%m-%d %H:%M UTC", time.gmtime(recovery["expires_at"] - settings.recovery_ttl_seconds))
        return HTMLResponse(pages.page(
            "Recover your ChatCube ID?",
            [f"A computer asked at {asked} to sign in as the ChatCube ID of "
             f"{user.display_name if user else 'yours'}.",
             "If that was you, confirm below - ChatCube on that computer continues by itself. "
             "If it was not you, close this page: nothing happens without this confirmation."],
            form=pages.approve_form(token)))

    def origin_of(url: str) -> str:
        parts = urllib.parse.urlsplit(url)
        return f"{parts.scheme}://{parts.netloc}".lower() if parts.scheme and parts.netloc else ""

    def from_another_site(request: Request) -> bool:
        """A form sent by a page that is not the relay's own. Browsers say
        where a POST comes from (Origin; Sec-Fetch-Site in newer ones), and
        no page can make them say it is ours; a request without them is not
        from a browser, so it is not a forged one either."""
        if request.headers.get("sec-fetch-site", "same-origin") not in ("same-origin", "none"):
            return True
        origin = request.headers.get("origin")
        if origin is None:
            return False
        ours = {origin_of(str(request.base_url))}
        if settings.public_url:
            ours.add(origin_of(settings.public_url))
        return origin.strip().lower() not in ours           # "null" included

    @app.post("/recover", response_class=HTMLResponse)
    async def recover_approve(request: Request):
        if from_another_site(request):
            return HTMLResponse(pages.page(
                "Not approved",
                ["The confirmation did not come from the page the e-mailed link opens. Open the link "
                 "again and press the button there."], bad=True), status_code=403)
        form = urllib.parse.parse_qs((await request.body()).decode("utf-8", "replace"))
        token = (form.get("token") or [""])[0]
        found, refused = recover_link_page(token)
        if refused:
            return refused
        link, recovery = found
        if not store.use_link(token) or not store.approve_recovery(recovery["recovery_id"]):
            return HTMLResponse(pages.page("This link does not work any more",
                                           ["It was used already."], bad=True), status_code=410)
        return HTMLResponse(pages.page(
            "Your ChatCube ID is on its way",
            ["Go back to the computer that asked: ChatCube signs in there by itself in a few seconds.",
             "You will get an e-mail when it has. You can close this page."]))

    @app.post("/v1/recovery/poll")
    async def recovery_poll(body: RecoveryPollBody):
        """Pending (202) until the link's page is approved; then the key, once."""
        if not recovery_offered():
            raise error(503, "not_configured", "This relay offers no account recovery")
        recovery = store.get_recovery(body.recovery_id)
        if (not recovery or recovery["expires_at"] < time.time() or
                not hmac.compare_digest(recovery["poll_digest"], hash_token(body.poll_token))):
            raise error(410, "expired", "This recovery has expired - start again")
        if recovery["approved_at"] is None or not recovery["user_id"]:
            return JSONResponse(status_code=202, content={"status": "pending",
                                                          "expires_in": int(recovery["expires_at"] - time.time())})
        user_id = recovery["user_id"]
        store.end_recovery(body.recovery_id)
        sealed = store.get_escrow(user_id)
        user = store.get_user(user_id)
        if not sealed or not user:
            raise error(410, "expired", "Recovery was turned off for this ID")
        try:
            secret = AESGCM(recovery_key).decrypt(sealed[0], sealed[1], user_id.encode())
        except InvalidTag:
            raise error(500, "escrow_unreadable", "The recovery copy cannot be read with this relay's key")
        contacts = store.get_contacts(user_id)
        when = time.strftime("%Y-%m-%d %H:%M UTC", time.gmtime())
        email = contacts.email
        send_later(lambda: notifier.send_email(
            email, "ChatCube: your ID was recovered on another computer",
            f"Your ChatCube ID was recovered on another computer at {when}, through the link e-mailed to "
            "you.\n\nIf this was you, there is nothing to do. If it was not, someone has your e-mail: "
            "secure it, and make a new ChatCube ID - a recovered key cannot be taken back."))
        return {"user_id": user_id, "secret_key": base64.b64encode(secret).decode(),
                "display_name": user.display_name, "email": contacts.email, "phone": contacts.phone}

    @app.get("/v1/users/{other}")
    async def get_user(other: str, user_id: str = Depends(current_user)):
        user = store.get_user(other)
        if not user:
            raise error(404, "unknown_user", "No such user")
        return user_json(user)

    # ---- contacts ------------------------------------------------------------

    @app.get("/v1/contacts")
    async def list_contacts(user_id: str = Depends(current_user)):
        return {"contacts": [user_json(u, hub.is_online(u.user_id)) for u in store.contacts(user_id)]}

    @app.put("/v1/contacts/{other}")
    async def add_contact(other: str, user_id: str = Depends(current_user)):
        user = store.get_user(other)
        if not user:
            raise error(404, "unknown_user", "No such user")
        if other == user_id:
            raise error(400, "invalid_request", "You cannot add yourself")
        store.add_contact(user_id, other)
        return user_json(user, hub.is_online(other))

    @app.delete("/v1/contacts/{other}")
    async def remove_contact(other: str, user_id: str = Depends(current_user)):
        store.remove_contact(user_id, other)
        return {"status": "ok"}

    # ---- groups --------------------------------------------------------------

    @app.post("/v1/groups")
    async def create_group(body: GroupBody, user_id: str = Depends(current_user)):
        members = [m for m in dict.fromkeys(body.members) if m != user_id]
        if len(members) + 1 > settings.max_group_members:
            raise error(400, "too_many_members", "A group holds at most %d members" % settings.max_group_members)
        for m in members:
            if not store.get_user(m):
                raise error(404, "unknown_user", "No such user: " + m)
        group = store.create_group(user_id, body.title, members)
        await push_group(group)
        return group_json(group)

    @app.get("/v1/groups")
    async def list_groups(user_id: str = Depends(current_user)):
        return {"groups": [group_json(g) for g in store.groups_of(user_id)]}

    def member_group(group_id: str, user_id: str):
        group = store.get_group(group_id)
        if not group or user_id not in group.members:
            raise error(404, "unknown_group", "No such group")
        return group

    @app.get("/v1/groups/{group_id}")
    async def get_group(group_id: str, user_id: str = Depends(current_user)):
        return group_json(member_group(group_id, user_id))

    @app.post("/v1/groups/{group_id}/members")
    async def add_member(group_id: str, body: MemberBody, user_id: str = Depends(current_user)):
        group = member_group(group_id, user_id)
        if not store.get_user(body.user_id):
            raise error(404, "unknown_user", "No such user")
        if len(group.members) >= settings.max_group_members:
            raise error(400, "too_many_members", "The group is full")
        store.add_member(group_id, body.user_id)
        group = store.get_group(group_id)
        await push_group(group)
        return group_json(group)

    @app.delete("/v1/groups/{group_id}/members/{member}")
    async def remove_member(group_id: str, member: str, user_id: str = Depends(current_user)):
        group = member_group(group_id, user_id)
        if member != user_id and group.owner != user_id:
            raise error(403, "forbidden", "Only the owner removes other members")
        store.remove_member(group_id, member)
        await hub.send(member, {"type": "group.update",
                                "group": {**group_json(group), "removed": True}})
        updated = store.get_group(group_id)
        if updated:
            await push_group(updated)
        return {"status": "ok"}

    # ---- offline queue -------------------------------------------------------

    @app.post("/v1/messages")
    async def post_messages(body: MessagesBody, user_id: str = Depends(current_user)):
        if len(body.messages) > settings.max_batch:
            raise error(400, "too_many_messages", "At most %d copies per request" % settings.max_batch)
        groups: dict[str, Any] = {}
        accepted: list[dict[str, Any]] = []
        for m in body.messages:
            if len(m.envelope.encode()) > settings.max_envelope_bytes:
                raise error(413, "too_large", "An envelope holds at most %d bytes" % settings.max_envelope_bytes)
            if m.conversation.startswith(GROUP_PREFIX):
                gid = m.conversation[len(GROUP_PREFIX):]
                if gid not in groups:
                    groups[gid] = store.get_group(gid)
                group = groups[gid]
                if not group or user_id not in group.members or m.to not in group.members:
                    raise error(403, "forbidden", "Sender and recipient must both be in the group")
            elif not store.get_user(m.to):
                raise error(404, "unknown_user", "No such user: " + m.to)
        for m in body.messages:
            queued_id = store.enqueue(m.to, user_id, m.message_id, m.conversation, m.envelope)
            accepted.append({"to": m.to, "message_id": m.message_id, "id": queued_id})
            if queued_id is not None:
                await hub.send(m.to, {"type": "message.new", "message": {
                    "id": queued_id, "from": user_id, "message_id": m.message_id,
                    "conversation": m.conversation, "envelope": m.envelope,
                    "created_at": int(time.time())}})
        return {"accepted": accepted}

    @app.get("/v1/messages")
    async def get_messages(since: int = 0, user_id: str = Depends(current_user)):
        return {"messages": store.pending(user_id, since)}

    @app.post("/v1/messages/ack")
    async def ack_messages(body: AckBody, user_id: str = Depends(current_user)):
        return {"deleted": store.acknowledge(user_id, body.ids)}

    # ---- TURN ----------------------------------------------------------------

    @app.get("/v1/turn-credentials")
    async def turn_credentials(user_id: str = Depends(current_user)):
        servers: list[dict[str, str]] = [{"url": u} for u in settings.stun_urls]
        ttl = 0
        if settings.turn_secret and settings.turn_urls:
            # coturn "use-auth-secret": username = expiry:user, password =
            # base64(HMAC-SHA1(secret, username)).
            ttl = settings.turn_ttl_seconds
            username = "%d:%s" % (int(time.time()) + ttl, user_id)
            credential = base64.b64encode(
                hmac.new(settings.turn_secret.encode(), username.encode(), hashlib.sha1).digest()).decode()
            servers += [{"url": u, "username": username, "credential": credential} for u in settings.turn_urls]
        return {"ice_servers": servers, "ttl": ttl}

    # ---- Telegram ------------------------------------------------------------

    @app.get("/v1/telegram-credentials")
    async def telegram_credentials(user_id: str = Depends(current_user)):
        # The API ID and hash of this ChatCube's Telegram application
        # (my.telegram.org), kept in the relay's settings so they are changed
        # here, not built into the application or typed by the user. They are
        # not a secret from the signed-in user - every Telegram client carries
        # its application's keys - but only a signed-in one gets them.
        if not settings.telegram_api_id or not settings.telegram_api_hash:
            raise error(404, "not_configured", "This relay has no Telegram application keys")
        return {"api_id": settings.telegram_api_id, "api_hash": settings.telegram_api_hash}

    # ---- WebSocket -----------------------------------------------------------

    @app.websocket("/v1/ws")
    async def websocket(ws: WebSocket):
        auth = ws.headers.get("authorization", "")
        user_id = store.session_user(auth[7:]) if auth.startswith("Bearer ") else None
        if not user_id:
            await ws.close(code=4401)
            return
        await ws.accept()
        if hub.add(user_id, ws):
            await push_presence(user_id, True)
        await ws.send_json({"type": "hello", "user_id": user_id})
        try:
            while True:
                frame = await ws.receive_json()
                if not isinstance(frame, dict):
                    continue
                kind = frame.get("type")
                to = frame.get("to")
                if kind == "ping":
                    await ws.send_json({"type": "pong"})
                elif kind == "typing" and isinstance(to, str):
                    await hub.send(to, {"type": "typing", "from": user_id,
                                        "conversation": str(frame.get("conversation", "")),
                                        "typing": bool(frame.get("typing"))})
                elif kind == "signal" and isinstance(to, str) and frame.get("kind") in SIGNAL_KINDS:
                    delivered = await hub.send(to, {"type": "signal", "from": user_id,
                                                    "kind": frame["kind"],
                                                    "session": str(frame.get("session", "")),
                                                    "payload": frame.get("payload", "")})
                    if not delivered:
                        await ws.send_json({"type": "signal.undeliverable", "to": to,
                                            "session": str(frame.get("session", ""))})
                else:
                    await ws.send_json({"type": "error", "error": "unknown_frame"})
        except WebSocketDisconnect:
            pass
        except Exception:
            pass
        finally:
            if hub.remove(user_id, ws):
                await push_presence(user_id, False)

    return app
