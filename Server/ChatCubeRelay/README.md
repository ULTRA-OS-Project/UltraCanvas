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
- short-lived **TURN credentials** for coturn;
- the **Telegram application's keys** (API ID and hash from my.telegram.org)
  for ChatCube's Telegram account, so they are set here once and never built
  into the application or typed by the user;
- the owner's **e-mail address and phone number** - the address confirmed
  by a link it e-mails there, the number by a code it texts - and, for a
  user who turns it on, **account recovery**: a copy of their key, given
  back to a new computer once the owner approves it through a link e-mailed
  to the confirmed address (see *Account recovery*; the one thing here that
  it *can* read).

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
| `PUT /v1/profile` | `{display_name?, email?, phone?}` → the caller's profile. Each left out stays; `""` removes the e-mail or the phone. The e-mail address and the phone number (`+` and the digits) are kept to recover the account with, for the owner alone: no other answer carries them. A changed one is no longer confirmed: the address gets a confirmation link, the number a code (`sent` says which). ChatCube sends them right after an ID is made, so a new user gets the link at registration |
| `GET /v1/profile` | `{user_id, public_key, display_name, email, phone, email_verified, phone_verified, recovery_available, phone_codes_available, recovery_enabled, sent}` — the caller's own |
| `GET /confirm-email?token=` | the page the confirmation link opens: the address confirmed (`email_verified`), or 410 when the link expired (a day), was used, was replaced by a newer one, or the address changed since |
| `POST /v1/profile/verify` | `{channel: "phone", code}` → the profile, the number confirmed. 400 `invalid_code`; 400 `use_the_link` for `"email"` |
| `POST /v1/profile/verify/resend` | `{channel: "email" \| "phone"}` → a new link (the one before stops working) or code. 429 past five an hour |
| `PUT /v1/recovery/escrow` | `{secret_key}` — the caller's own 64-byte Ed25519 key, base64: recovery on (the address confirmed: else 409 `not_verified`; another key: 400 `wrong_key`) |
| `DELETE /v1/recovery/escrow` | recovery off: the copy is deleted |
| `POST /v1/recovery/start` | `{address}` (the ID's e-mail address, any capitals), no session → `{recovery_id, poll_token, expires_in}`, the same for an address nobody has; a recovery link to the address only when an ID with recovery on has it confirmed. 429 past five an hour per address and per client |
| `GET /recover?token=` | the page the recovery link opens: who asked and when, and a button (a form `POST`) to approve - opening the link alone approves nothing. 410 when it expired (30 minutes) or was used |
| `POST /recover` | form `token=` → the recovery approved, the link spent; the page says to go back to the computer that asked. 403 when the browser says the form came from another site (`Origin` not the relay's own, `null`, or `Sec-Fetch-Site` not `same-origin`) |
| `POST /v1/recovery/poll` | `{recovery_id, poll_token}`, no session → 202 `{status: "pending", expires_in}` until approved; then once `{user_id, secret_key, display_name, email, phone}` and the owner is e-mailed a notice; 410 `expired` (or a wrong poll token) |
| `GET /v1/users/{id}` | `{user_id, public_key, display_name}` — contact discovery by id |
| `GET /v1/contacts` | `{contacts: [{user_id, public_key, display_name, presence}]}` |
| `PUT` / `DELETE /v1/contacts/{id}` | add / remove |
| `POST /v1/groups` | `{title, members}` → `{group_id, conversation: "group:<id>", title, owner, members}` |
| `GET /v1/groups`, `GET /v1/groups/{id}` | the caller's groups |
| `PATCH /v1/groups/{id}` | `{title}` — any member renames (1.3.0) |
| `POST /v1/groups/{id}/members` | `{user_id}` — any member adds |
| `DELETE /v1/groups/{id}/members/{user}` | the owner removes anyone; anyone leaves |
| `POST /v1/messages` | `{messages: [{to, message_id, conversation, envelope}]}` — offline 1:1 copies, or one copy per group member (sender and recipient must both be members of a `group:` conversation). A `message_id` already queued for that recipient is not queued twice. Envelopes up to 64 KiB |
| `GET /v1/messages?since=<id>` | `{messages: [{id, from, message_id, conversation, envelope, created_at}]}` |
| `POST /v1/messages/ack` | `{ids}` — deletes them |
| `GET /v1/turn-credentials` | `{ice_servers: [{url, username?, credential?}], ttl}` — coturn `use-auth-secret` |
| `GET /v1/telegram-credentials` | `{api_id, api_hash}` — this ChatCube's Telegram application, from `CHATCUBE_RELAY_TELEGRAM_API_ID` / `_HASH`; 404 `not_configured` when the relay has none |
| `GET /healthz` | `{status, version}` |

WebSocket `/v1/ws`, JSON frames:

| Direction | Frame |
|---|---|
| server → | `hello {user_id}` on connect |
| server → | `presence {from, status: online\|offline}` — for your contacts and fellow group members |
| server → | `message.new {message}` — a queued copy for you, as `GET /v1/messages` returns it |
| server → | `group.update {group}` (`removed: true` when you left or were removed) |
| ↔ | `typing {to, conversation, typing}` (arrives with `from`) |
| ↔ | `signal {to, kind, session, payload}` — `kind` is `offer`, `answer`, `ice`, `hangup`, `call-invite`, `call-accept`, `call-reject` or `call-update` (a call's camera on or off); `payload` is forwarded untouched (arrives with `from`). Not online: `signal.undeliverable {to, session}` comes back |
| ↔ | `ping` → `pong` |

## Account recovery

A ChatCube ID is its key, and the key lives on the computer that made it.
Without recovery, an ID lost with its computer is gone - which is the point
of an identity nobody else holds. Recovery trades a part of that for being
able to get the ID back, **for the users who turn it on** (ChatCube: *Edit
profile*, *Getting this ID back*), and the trade is said there in as many
words:

- The relay keeps a **copy of the user's key**, encrypted with the relay's
  recovery key (`CHATCUBE_RELAY_RECOVERY_KEY`, AES-256-GCM, the user id as
  associated data). A copy of the database alone does not give the keys;
  **whoever runs the relay, with its recovery key, can.** That is the cost:
  for these users the relay is trusted with the key itself, not only with
  envelopes it cannot open.
- **The e-mail address is confirmed by a link.** Setting or changing it
  e-mails a link to it (`/confirm-email`, good for a day, once); opening it
  shows a page that says the address is confirmed and records it
  (`email_verified`). Recovery can only be turned on with the address
  confirmed, and only with the account's own key. The phone number, if the
  relay can send SMS, is confirmed by a code; recovery does not need it.
- **Recovery is by a link too.** A new computer asks with the ID's address
  (`POST /v1/recovery/start`). If an ID with recovery on has that address
  confirmed, the relay e-mails it a link to `/recover`, good for 30 minutes
  and once. The page asks before it approves: a button that sends a form
  `POST`, so a mail scanner that opens links by itself approves nothing,
  and the relay takes that `POST` only from its own page: a form on another
  site, which a browser marks with its `Origin`, is refused.
  Meanwhile the new computer asks every few seconds (`POST
  /v1/recovery/poll`) with a poll token it alone was given; once the owner
  has approved, it - and only it - gets the key back, once.
- Starting a recovery answers the same whether the address belongs to anyone
  (a recovery is recorded either way; one nobody has is never approved), and
  the e-mail is sent after the answer, so neither the answer nor its timing
  tells. Starts are limited to five an hour per address and thirty per
  client (network address - many people can share one), the one refused not
  counted against the other.
- When an ID is recovered, its owner is e-mailed a notice. A recovered key
  cannot be taken back: an owner who did not do it secures the mailbox and
  makes a new ID. Whoever controls the mailbox controls a recovery-enabled
  ID, and ChatCube says so where recovery is turned on.

Links and poll tokens are stored as SHA-256 digests, never as themselves.
At every start and every hour the relay deletes what can no longer be used:
expired or used links, expired recoveries and codes, send counts older than
a day, expired sessions.
Turning recovery off deletes the copy. Changing the address takes the
confirmation away until the link sent to the new one is opened, and
recovery waits for it.

**Configuring it.** The relay offers recovery when it has a recovery key and
can send e-mail; SMS is only for confirming the phone number:

| Setting | |
|---|---|
| `CHATCUBE_RELAY_RECOVERY_KEY` | 32 random bytes, base64: `openssl rand -base64 32`. Keep it as you keep the database's backups - apart from them; lose it and no copy can be read |
| `CHATCUBE_RELAY_PUBLIC_URL` | the relay's address as browsers reach it (`https://relay.example.org`): the links in the e-mails start with it. Unset, the links use the address the request came to - right only when nothing (nginx) sits in front |
| `CHATCUBE_RELAY_SMTP_HOST`, `_PORT` (587), `_SECURITY` (`starttls`, `ssl` or `none`), `_USER`, `_PASSWORD`, `_FROM` | the SMTP server the e-mail goes through, and its sender (`ChatCube <no-reply@example.org>`) |
| `CHATCUBE_RELAY_SMS` | `twilio` or `webhook` (optional: the phone's confirmation code) |
| `CHATCUBE_RELAY_TWILIO_ACCOUNT_SID`, `_AUTH_TOKEN`, `_FROM` | Twilio's Messages API and the sending number |
| `CHATCUBE_RELAY_SMS_WEBHOOK_URL`, `_TOKEN` | any other gateway: one `POST` of `{"to", "text"}` per SMS, with `Authorization: Bearer <token>` when one is set; a 2xx answer counts as sent |
| `CHATCUBE_RELAY_RECOVERY_STARTS_PER_CLIENT` | recoveries one network address may start in an hour (30); each e-mail address has its own limit of five |
| `CHATCUBE_RELAY_OUTBOX` | development and tests only: every e-mail and SMS is appended to this file as a JSON line instead of being sent |

A relay without a recovery key or an e-mail sender works as before and says
`recovery_available: false`; ChatCube then tells the user that their ID
cannot come back without the computer it is on. A failed send is logged with
the address masked (`e***@example.org`, `+49 ••• 89`), never with a link or
a code.

## The database and its migrations

One SQLite file (`CHATCUBE_RELAY_DB`). Its schema has a version - SQLite's
`PRAGMA user_version` - and every start brings it to the newest one, a step
at a time (`chatcube_relay/migrations.py`). Each step runs in a transaction
of its own that also records its number, so a step that fails leaves the
database as it was, at the version before, and the relay does not start. A
database written by a newer relay is refused rather than used: run that
relay, or a newer one.

| Version | Relay | What it adds |
|---|---|---|
| 1 | 1.0 | accounts, sessions, contacts, groups, the offline queue |
| 2 | 1.1 | the owner's e-mail address and phone number |
| 3 | 1.2 | whether each is confirmed (`email_verified`, `phone_verified`, both starting unconfirmed - also what 1.1 kept), the confirmation and recovery links, the phone's codes, the recovery copies, recoveries under way, and the send counts behind the rate limits |

Relays before 1.2 did not record a version: their databases are recognised
by their tables (no `recovery` table: version 1) and brought forward with
their data. Take a copy of the file before upgrading all the same - after
the upgrade, the older relay refuses it. To change the schema, append a
migration; never edit one that has shipped.

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

Give ChatCube the relay's `https://` address. A bare host is taken as
`https://`, and an `http://` address that the server redirects to `https://`
(the usual port-80 block in nginx) is followed and the new address kept -
but going straight there saves the detour on every start.

A relay with a self-signed certificate is trusted by key in ChatCube
(UltraNet's `pinnedPublicKey`), never by switching verification off.
