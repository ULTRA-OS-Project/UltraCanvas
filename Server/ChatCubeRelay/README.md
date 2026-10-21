# The ChatCube relay

The server side of **ChatCube Direct**, the ULTRA OS native messenger. A
small [FastAPI](https://fastapi.tiangolo.com/) service on SQLite that does
what two peers cannot do for themselves over the internet:

- **accounts** without passwords: an Ed25519 key pair made on the device is
  the identity; login is challenge–response;
- **contacts** and **presence**;
- **signalling** for peer-to-peer links and calls (SDP offers and answers,
  ICE candidates) over a WebSocket;
- the **offline queue**: a 1:1 message for someone who is offline waits here
  until their device fetches and acknowledges it;
- **groups**: membership, and fan-out of group messages;
- short-lived **TURN credentials** for coturn.

It **cannot read a message.** Every message is end-to-end encrypted by the
clients (X25519 + XChaCha20-Poly1305, `Docs/Research/ChatCubeDirectPlan.md`
§4); the relay stores and forwards the envelope as an opaque string, and
signalling payloads are signed by the sender's identity key so the relay
cannot insert itself into a call. When both people are online, 1:1 messages
do not pass through it at all — they travel on a WebRTC data channel between
the two devices. On a local network ChatCube works without it.

TURN is a different thing (coturn, `deploy/turnserver.conf`): it only
forwards encrypted packets when no direct path between two devices exists,
stores nothing and knows no users.

## Identity

- `user_id` = lowercase hex of the first 16 bytes of SHA-256 of the 32-byte
  Ed25519 public key. The relay derives it; nobody chooses it.
- Register: sign `"chatcube-register-v1\n" + <public_key base64> + "\n" + <display name>`.
- Login: `POST /v1/login/challenge` → `nonce`; sign
  `"chatcube-login-v1\n" + <nonce>`; `POST /v1/login` → `token`. A nonce is
  good for one attempt and two minutes. Tokens live 30 days and are stored as
  their SHA-256 only.

## API

Requests and answers are JSON. Errors are
`{"error": "...", "error_description": "..."}` with a 4xx status.
Authenticated calls carry `Authorization: Bearer <token>` (the WebSocket
too, in its upgrade request).

| | |
|---|---|
| `POST /v1/register` | `{public_key, display_name, signature}` → `{user_id}` (idempotent for the same key) |
| `POST /v1/login/challenge` | `{user_id}` → `{nonce, expires_in}` |
| `POST /v1/login` | `{user_id, nonce, signature}` → `{token, expires_at, user_id, display_name}` |
| `POST /v1/logout` | ends this session |
| `PUT /v1/profile` | `{display_name}` |
| `GET /v1/users/{id}` | `{user_id, public_key, display_name}` — contact discovery by id |
| `GET /v1/contacts` | `{contacts: [{user_id, public_key, display_name, presence}]}` |
| `PUT` / `DELETE /v1/contacts/{id}` | add / remove |
| `POST /v1/groups` | `{title, members}` → `{group_id, conversation: "group:<id>", title, owner, members}` |
| `GET /v1/groups`, `GET /v1/groups/{id}` | the caller's groups |
| `POST /v1/groups/{id}/members` | `{user_id}` — any member adds |
| `DELETE /v1/groups/{id}/members/{user}` | the owner removes anyone; anyone leaves |
| `POST /v1/messages` | `{messages: [{to, message_id, conversation, envelope}]}` — offline 1:1 copies, or one copy per group member (sender and recipient must both be members of a `group:` conversation). A `message_id` already queued for that recipient is not queued twice. Envelopes up to 64 KiB |
| `GET /v1/messages?since=<id>` | `{messages: [{id, from, message_id, conversation, envelope, created_at}]}` |
| `POST /v1/messages/ack` | `{ids}` — deletes them |
| `GET /v1/turn-credentials` | `{ice_servers: [{url, username?, credential?}], ttl}` — coturn `use-auth-secret` |
| `GET /healthz` | `{status, version}` |

WebSocket `/v1/ws`, JSON frames:

| Direction | Frame |
|---|---|
| server → | `hello {user_id}` on connect |
| server → | `presence {from, status: online\|offline}` — for your contacts and fellow group members |
| server → | `message.new {message}` — a queued copy for you, as `GET /v1/messages` returns it |
| server → | `group.update {group}` (`removed: true` when you left or were removed) |
| ↔ | `typing {to, conversation, typing}` (arrives with `from`) |
| ↔ | `signal {to, kind, session, payload}` — `kind` is `offer`, `answer`, `ice`, `hangup`, `call-invite`, `call-accept` or `call-reject`; `payload` is forwarded untouched (arrives with `from`). Not online: `signal.undeliverable {to, session}` comes back |
| ↔ | `ping` → `pong` |

## Running it

```bash
cd Server/ChatCubeRelay
python3 -m venv .venv && .venv/bin/pip install -e '.[test]'
.venv/bin/python -m pytest -q          # the test suite
CHATCUBE_RELAY_DB=/tmp/relay.sqlite3 .venv/bin/chatcube-relay   # http://127.0.0.1:8790
```

Settings come from the environment (`.env.example` lists them). Deployment:
`deploy/chatcube-relay.service` (systemd), `deploy/nginx.conf` (TLS and the
WebSocket upgrade in front of it), `deploy/turnserver.conf` (coturn, sharing
`CHATCUBE_RELAY_TURN_SECRET`).

A relay with a self-signed certificate is trusted by key in ChatCube
(UltraNet's `pinnedPublicKey`), never by switching verification off.
