# ChatCube Direct, UltraIM and UltraRTC — Implementation Plan

**Date:** 2026-10-21
**Status:** Accepted plan; implementation in progress
**Supersedes the order of work in:** [`../ChatCube/HANDOFF_UltraIM_ChatCube.md`](../ChatCube/HANDOFF_UltraIM_ChatCube.md)
(its analysis of the v1 draft still holds; its phase order does not)
**Builds on:** [`UltraRTCDesignProposal.md`](UltraRTCDesignProposal.md)

---

## 1. Goal

The first deliverable is **an ULTRA OS native messenger**, ChatCube Direct:

- chat from one ChatCube to another **without a server** carrying the
  messages: peer to peer over the internet, and with no server at all on a
  local network;
- **group chats** through a server;
- **voice and video calls**.

Clients for Matrix, Telegram and the other messengers with an open API come
afterwards, as further provider plug-ins of the same framework.

## 2. Decisions

| Id | Question | Decision |
|---|---|---|
| — | What "without a server" means | 1:1 messages, files and calls go **directly between the two peers**. Over the internet a relay server is still the rendezvous: login, contacts, presence, connection set-up (signalling), offline queue. It never sees 1:1 content. **On a local network peers also find each other through mDNS and chat with no server at all.** |
| D1 | Relay server stack | **Python + SQLite**, `Server/ChatCubeRelay/`, next to `Server/OAuthCallback/` and deployed the same way. coturn alongside for TURN. |
| D2 | WebRTC engine | **libdatachannel** (C++, MPL-2.0) behind the `UltraRtc_*` API of the UltraRTC proposal. This promotes the proposal's tier 2 to the only tier for now; the Rust tier can still be added behind the same API. |
| D3 | Provider plug-ins | **Loadable modules** (`.so` / `.dll` / `.dylib`) with a host table and ABI version, found in a plug-in directory at run time. |
| D4 | UltraIM public surface | **`UltraIM_*` free functions**, as UltraNet, FileLoader and PixelFX. |
| D5 | End-to-end encryption | **From v1.** Key-pair identity; every message is encrypted to its recipient; the relay stores and forwards ciphertext only. |
| D6 | Telegram through TDLib (its own sockets) | Not needed yet — decided when the Telegram plug-in starts. |
| D7 | Where the modules live | In this repository, **next to UltraNet**: `UltraCanvas/{include,core}/UltraIM/`, `UltraCanvas/{include,core}/UltraRTC/`, provider plug-ins under `UltraCanvas/Plugins/UltraIM/<name>/`, the app in `Apps/ChatCube/`, the relay in `Server/ChatCubeRelay/`. |

## 3. Layers

```
ChatCube app (Apps/ChatCube)            UltraCanvas UI: contact list, bubbles,
  │  UltraIM_* calls ▼  ▲ UltraIM events  input, sign-in, call window
UltraIM module (UI-free)                 types, provider registry, event bus
  │                                      with an injected dispatcher, DSO host
  ├── Plugins/UltraIM/direct  ── ChatCube Direct provider ─────────────┐
  └── Plugins/UltraIM/matrix  (later), telegram, xmpp …               │
                                                                      │
UltraRTC module (UI-free)  ◄── peers, data channels, audio/video ─────┤
  └── libdatachannel + libjuice + usrsctp + libsrtp (vendored)         │
UltraNet  ◄── relay REST (HTTPS), relay WebSocket (wss), mDNS ────────┤
UltraCrypt ◄── identity keys, key agreement, AEAD ─────────────────────┘
UltraVault ◄── private key and session token at rest
```

Dependency rule: UltraNet, UltraRTC, UltraCrypt ← UltraIM ← ChatCube. UltraIM
and UltraRTC include no UltraCanvas UI header.

## 4. Identity and encryption (D5)

- **One identity key pair per account**, Ed25519, generated on the device
  with `UltraCrypt_GenerateSignatureKeyPair`. The secret key never leaves the
  device; it is stored in UltraVault under `com.cloverleaf.chatcube.identity`.
- **User id** = lowercase hex of the first 16 bytes of SHA-256 of the public
  key, shown grouped (`3f2a 9c01 …`) so two people can compare it.
- **Key agreement** uses the X25519 form of the same key (`UltraCrypt_SignaturePublicKeyToKeyExchange` /
  `UltraCrypt_SignatureSecretKeyToKeyExchange`), so one key
  pair both signs and encrypts.
- **Message envelope** (the same on every path — data channel, LAN, relay):
  ```json
  { "v": 1, "id": "<uuid, the clientMessageId>", "from": "<userId>",
    "to": "<userId>", "conv": "<conversationId>", "ts": 1760000000000,
    "nonce": "<base64>", "ct": "<base64 ciphertext>" }
  ```
  `ct` is XChaCha20-Poly1305 over the JSON body (`type`, `text`,
  attachment metadata, reply id), keyed by
  `HKDF-SHA256(X25519(mySecret, theirPublic), "chatcube-msg-v1")`, with the
  envelope header (`v id from to conv ts`) as associated data, so nothing
  in the header can be altered unnoticed.
- **Groups**: the sender encrypts the body once per member (fine for the
  group sizes v1 targets) and posts all copies in one request; the relay
  fans them out without being able to read any.
- **Login to the relay** is challenge–response: the relay sends a random
  nonce, the client signs it with its identity key, the relay verifies
  against the registered public key and issues a session token. No password
  exists.
- **P2P links are bound to the identity.** The DTLS fingerprint in every SDP
  offer/answer is signed with the identity key inside the signalling message,
  so the relay cannot sit in the middle of a call or a data channel.

### Framework additions this needs

UltraCrypt had Ed25519 signatures and AEAD but no key agreement. It was
added to UltraCrypt (not to ChatCube, so the next caller finds it):

- `UltraCrypt_SignaturePublicKeyToKeyExchange` /
  `UltraCrypt_SignatureSecretKeyToKeyExchange` — Ed25519 → X25519, over
  libsodium's conversion functions;
- `UltraCrypt_KeyExchange` — the X25519 shared secret, as an
  `UltraCryptSecureBuffer` to feed `UltraCrypt_DeriveKeyHkdf`;
- `UltraCrypt_GenerateKeyExchangeKeyPair` — an ephemeral pair.

## 5. Transport and delivery rules

| Situation | Path |
|---|---|
| Both online, data channel open | send on the data channel; nothing reaches the server |
| Both online, no channel yet | open one (signalling through the relay), queue the message locally until it opens; after 10 s fall back to the relay queue |
| Recipient offline | `POST /v1/messages` — the relay queues the ciphertext until the recipient acknowledges it |
| Same local network | found by mDNS (`_chatcube._tcp`, TXT `id=<userId>`); signalling over a direct TCP link to the advertised port; then the same data channel. Works with no relay configured |
| Group text | always through the relay (store + fan-out) |
| 1:1 call | WebRTC audio/video between the peers (STUN, TURN only as last resort) |
| Group call | not in v1 (needs an SFU); `CanCall()` answers no for groups |

Every message carries its client-generated id, so a message that arrived by
two paths during a reconnect is shown once.

TURN (coturn) only forwards packets when a direct path cannot be built; it
never stores anything and knows no users. The relay issues short-lived TURN
credentials (coturn `use-auth-secret`), so no TURN password ships in the app.

### Framework addition this needs

The UltraNet mDNS plug-in browses and resolves but cannot advertise. Service
registration (Avahi entry group / `DNSServiceRegister` /
`DnsServiceRegister`) is added to that plug-in for LAN chat.

## 6. Relay server (D1)

`Server/ChatCubeRelay/` — Python 3.11+, FastAPI + uvicorn, SQLite, the same
deployment shape as `Server/OAuthCallback/`.

```
POST   /v1/register            {publicKey, displayName}            → {userId}
POST   /v1/login/challenge     {userId}                            → {nonce}
POST   /v1/login               {userId, nonce, signature}          → {token, expires}
POST   /v1/logout
GET    /v1/users/{userId}      public key + display name (contact discovery)
GET|PUT|DELETE /v1/contacts[/{userId}]
POST   /v1/messages            [{to, envelope}]   offline 1:1 or group copies
GET    /v1/messages?since=     the caller's queue
POST   /v1/messages/ack        {ids}              deletes acknowledged copies
POST   /v1/groups              {title, members}   → {groupId}
GET    /v1/groups              the caller's groups
POST|DELETE /v1/groups/{id}/members[/{userId}]
GET    /v1/turn-credentials    short-lived coturn credentials
WS     /v1/ws?token=           frames: presence, typing, signal.offer,
                               signal.answer, signal.ice, signal.hangup,
                               message.new, message.ack
```

The relay never parses `envelope.ct`. Self-hosted relays with their own
certificate are trusted by key (`UltraNetHttpOptions::pinnedPublicKey`),
never by turning verification off.

## 7. UltraIM module (D3, D4)

Public headers in `UltraCanvas/include/UltraIM/`:

- `UltraIMTypes.h` — `UltraIMMessage`, `UltraIMAttachment`,
  `UltraIMReplyContext`, `UltraIMContact`, `UltraIMConversation`,
  `UltraIMMessageType`, `UltraIMDeliveryStatus`, `UltraIMPresence`,
  `UltraIMCapabilityFlags`, `UltraIMLoginType`, `UltraIMCredentials`,
  `UltraIMConnectionState`, `UltraIMEvent`, `UltraIMEventType`,
  `UltraIMResult`, `UltraIMResultCode`, `UltraIMSendOptions` (with
  `clientMessageId`), `UltraIMConfig`, `UltraIMProviderInfo`
- `UltraIMPlugins.h` — `IUltraIMProviderPlugin` (handoff 3.3: message
  actions take the conversation id, `CanCall(conversationId, video)`),
  `UltraIMPluginHost` + `ULTRAIM_PLUGIN_HOST_ABI_VERSION`, the exported
  `UltraIM_PluginInit` entry point
- `UltraIMCore.h` — the `UltraIM_*` functions of handoff 3.4

Plug-in modules link the shared core (the framework is one shared library on
every platform), export `UltraIM_PluginInit(const UltraIMPluginHost*)`, and
register their provider through the host table. `UltraIM_RefreshProviders()`
loads every module in the plug-in directory once. Threading: plug-ins raise
events from any thread; the manager hands each to the dispatcher the app
installs (ChatCube installs `PostToUIThread`), so every app callback runs on
the UI thread. The manager never calls into a plug-in while holding its lock.

## 8. UltraRTC module (D2)

`UltraCanvas/{include,core}/UltraRTC/`, the API of the UltraRTC proposal §2,
implemented in phases:

1. peers, ICE servers, SDP and candidates passed through unchanged, data
   channels, events marshalled through an injected dispatcher;
2. audio: Opus (libopus, already a dependency) over libdatachannel's Opus
   RTP packetiser; microphone through a frame-callback mode of
   `UltraCanvasAudioRecorder`, playback through a push mode of the audio
   player, a first jitter buffer;
3. video: camera through IODeviceManager, VP8 through FFmpeg (VideoFX),
   `UltraCanvasVideoTile` and `UltraCanvasCallView` elements in the
   catalogue.

libdatachannel v0.24.6 and its dependencies (libjuice, usrsctp, libsrtp,
plog) are vendored under `UltraCanvas/third_party/libdatachannel/` with the
examples and tests removed. The module is built when OpenSSL is found (it is
libdatachannel's DTLS backend); without it, every call answers
`Unsupported`.

## 9. Phases

| # | Deliverable | Done when |
|---|---|---|
| 1 | This plan; registry entries | committed |
| 2 | UltraIM module: types, plug-in interface, manager, dispatcher, DSO host | builds with no UI header; unit tests pass |
| 3 | UltraRTC phase 1: vendored engine, peers, data channels | a test connects two peers in one process and exchanges messages |
| 4 | UltraCrypt key agreement | unit tests; doc updated |
| 5 | Relay server | pytest suite: register, login, contacts, queue, groups, WS signalling |
| 6 | Direct plug-in: relay client, E2E envelope, data channels, offline queue, groups | integration test: two clients through a local relay exchange 1:1 messages P2P and a group message through the relay |
| 7 | ChatCube app on the real UltraCanvas API | starts, signs in to a relay, shows contacts, sends and receives, ticks update, a message shows once |
| 8 | LAN without server: mDNS advertising, TCP signalling | two instances on one network chat with no relay configured |
| 9 | Voice calls (UltraRTC phase 2) | a 1:1 call between two ChatCubes |
| 10 | Video calls (UltraRTC phase 3) | a 1:1 video call between two ChatCubes |
| 11 | Matrix, then Telegram and other providers | per provider |

Every phase ends with a clean build, its tests, and a push.
