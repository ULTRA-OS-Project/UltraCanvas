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
import secrets
import time
from contextlib import asynccontextmanager
from typing import Any

from fastapi import Depends, FastAPI, Header, HTTPException, Request, WebSocket, WebSocketDisconnect
from fastapi.responses import JSONResponse
from pydantic import BaseModel, Field

from . import __version__
from .config import Settings
from .identity import decode_b64, login_message, register_message, user_id_for, verify
from .store import GROUP_PREFIX, Store

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
    display_name: str = Field(min_length=1, max_length=64)


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

def create_app(settings: Settings | None = None) -> FastAPI:
    settings = settings or Settings.from_environment()
    store = Store(settings.database)
    hub = Hub()
    challenges: dict[str, tuple[str, float]] = {}     # nonce -> (user_id, expiry)

    @asynccontextmanager
    async def lifespan(_app: FastAPI):
        async def housekeeping():
            while True:
                await asyncio.sleep(3600)
                store.expire(settings.queue_ttl_seconds)
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

    @app.put("/v1/profile")
    async def set_profile(body: ProfileBody, user_id: str = Depends(current_user)):
        store.set_display_name(user_id, body.display_name)
        return user_json(store.get_user(user_id))

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
