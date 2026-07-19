# JMAP Support in UltraNet — Investigation & Design

**Status:** Phase 1 implemented (read path) — see `Plugins/UltraNet/jmap/`;
phases 2–3 remain as designed below
**Scope:** Add JMAP (JSON Meta Application Protocol, RFC 8620 core + RFC 8621 mail)
as an UltraNet protocol plug-in, consumable by UltraMail alongside IMAP/SMTP.

---

## 1. What JMAP is, in UltraNet terms

JMAP is the modern replacement for IMAP + SMTP submission, used by Fastmail,
Cyrus, Stalwart, Apache James and others. Unlike the line-based protocols the
existing mail plug-ins wrap, JMAP is:

* **JSON over HTTPS** — a single `POST` endpoint (`apiUrl`) carrying batched
  method calls (`Mailbox/get`, `Email/query`, `Email/set`, …) with
  back-references between calls in one round-trip.
* **Session-based** — the client fetches a *session resource*
  (`https://host/.well-known/jmap`) that advertises capabilities, account ids,
  and the `apiUrl` / `uploadUrl` / `downloadUrl` / `eventSourceUrl` endpoints.
* **Push-capable** — server state changes stream over **Server-Sent Events**
  (the `eventSourceUrl`), removing IMAP-IDLE-style polling.
* **Both directions in one protocol** — mail submission is
  `EmailSubmission/set`, so one plug-in replaces the IMAP+SMTP pair.

## 2. Why UltraNet is unusually well positioned

The investigation found that every transport-level building block JMAP needs
already exists in the core (Tier 1) surface — no new native dependencies:

| JMAP requirement | Existing UltraNet facility |
|---|---|
| HTTPS POST w/ JSON body, Basic/Bearer auth | `UltraNet_HttpRequest` / `UltraNetHttpOptions.credentials` (`UltraNetAuthType::Basic`, `Bearer`, `OAuth2`) — `UltraNet/UltraNetHttp.h` |
| Push via EventSource | `UltraNet_SseStream` / `UltraNet_SseStreamAsync` + reusable `UltraNetSseParser` — `UltraNet/UltraNetSse.h` |
| Autodiscovery via DNS SRV (`_jmap._tcp`) | `UltraNet_DnsResolve(..., UltraNetDnsType::SRV)` — `UltraNet/UltraNetDns.h` |
| Blob download/upload (raw RFC 5322, attachments) | `UltraNet_HttpDownloadFile`, request body upload, `onDataChunk` streaming |
| TLS defaults, proxies, timeouts | Core config; TLS verification ON by default |
| Plug-in packaging | `IUltraNetPlugin` DSO contract (v2 host-vtable) — `UltraNet/UltraNetPlugins.h` |

The consumer side also lines up: **UltraMail**'s `SyncEngine`
(`Apps/UltraMail/engine/UltraMailSyncEngine.h`) depends only on
`IMailboxProtocolPlugin`, so a JMAP plug-in implementing that interface slots
into the existing folder-sync / envelope-sync / body-cache pipeline with no
engine changes (subject to the id-mapping question in §4).

## 3. Gaps found

### 3.1 No JSON library in the build (blocking)

UltraNet has **no JSON parser/serializer**. Notably,
`Plugins/Documents/UltraCanvasDocument.cpp` already `#include`s
`<nlohmann/json.hpp>` ("assuming we have a JSON library"), but nlohmann/json is
neither vendored under `third_party/` nor referenced by any CMakeLists — that
include only compiles where the system happens to provide it.

Options:

* **(A) Vendor `nlohmann/json` single header** into
  `UltraCanvas/third_party/nlohmann/json.hpp` (MIT, header-only, no build
  impact). Also repairs the latent Documents-plug-in dependency. **Recommended.**
* **(B) Hand-rolled minimal JSON** in the plug-in (`JmapJson.h`), following the
  pure-parser pattern of `imap/ImapParse.h`. Zero new third-party code, but
  JSON string escaping/UTF-16 surrogate handling is easy to get subtly wrong,
  and JMAP payloads are deeply nested.

### 3.2 The mailbox interface is IMAP-shaped

`IMailboxProtocolPlugin` uses `uint32_t uid`, folder *path strings*, and the
`UltraNetMailFlags` bitfield. JMAP uses opaque **string ids** for Mailbox and
Email objects and free-form **keywords** (`$seen`, `$answered`, `$flagged`,
`$draft`) instead of flags; there is no `\Recent`, and deletion is
move-to-Trash / `Email/set destroy` rather than a `\Deleted` flag.

See §4 for the mapping strategy.

### 3.3 Discovery

`UltraMail::AutoDiscovery` knows presets + Mozilla autoconfig only. JMAP
defines its own autodiscovery: DNS SRV `_jmap._tcp.<domain>` plus the
`/.well-known/jmap` session probe (RFC 8620 §2.2). Both primitives exist
(`UltraNetDns` SRV, `UltraNetHttp`); the discovery pipeline needs a JMAP step.

## 4. Design proposal

### 4.1 New plug-in: `Plugins/UltraNet/jmap/`

```
UltraCanvas/Plugins/UltraNet/jmap/
├── CMakeLists.txt        # target UltraNetJmapPlugin → ultranet_jmap.{so,dylib}
├── JmapCore.h            # pure: session parse, request builder, response
│                         #   parsers, keyword↔flag mapping  (tested, no I/O)
└── JmapPlugin.cpp        # IMailboxProtocolPlugin impl over UltraNetHttp/Sse
```

* Registers scheme **`jmap`** (server URL convention:
  `jmap://user@mail.example.com/` → session at
  `https://mail.example.com/.well-known/jmap`; an explicit `https://…` session
  URL in `UltraNetMailOptions.serverUrl` is also accepted).
* Follows the mail-trio CMake shape: `ULTRACANVAS_PLUGIN_JMAP` option guard in
  `UltraCanvas/CMakeLists.txt`, output to `${CMAKE_BINARY_DIR}/Plugins/UltraNet`,
  v2 `UltraNet_PluginInit` entry point (+ v1 for POSIX parity).
* **No libcurl code of its own** — unlike the IMAP plug-in it goes through the
  `UltraNet_Http*` public surface, which brings pooling, proxy, TLS policy and
  cancellation for free. Only new dependency: the JSON header (§3.1).
* Caches the session resource + `state` strings per (server, account) with a
  mutex-guarded map inside the plug-in instance (same lifetime model as other
  plug-ins; `Shutdown()` clears it).

### 4.2 Mapping `IMailboxProtocolPlugin` → JMAP methods

| Interface call | JMAP realisation (single batched POST each) |
|---|---|
| `ListFolders` | `Mailbox/get` — JMAP's native `role` property maps 1:1 onto `UltraNetMailFolder.role` (cleaner than IMAP SPECIAL-USE sniffing); `parentId` chains are flattened into `name` paths with `/` delimiter |
| `GetMailboxStatus` | `Mailbox/get` (`totalEmails`, `unreadEmails`) — `uidValidity`/`uidNext` are synthesised from the mailbox `state` string (see id note below) |
| `FetchEnvelopes` | `Email/query` (filter `inMailbox`, sort `receivedAt desc`, `limit = maxMessages`) **+** `Email/get` via result back-reference — envelope properties only, one round-trip |
| `FetchMessage` | `Email/get` for `blobId` → GET on session `downloadUrl` → raw RFC 5322 |
| `StoreFlags` | `Email/set` keyword patch (`keywords/$seen` etc.); `Deleted` flag maps to move-to-Trash (role lookup) since JMAP has no `\Deleted` |
| `MoveMessage` | `Email/set` patch of `mailboxIds` |
| `AppendMessage` | POST to `uploadUrl` → `Email/import` with target mailbox + keywords |
| `SendMail` | `Email/set` create (or import) + `EmailSubmission/set` — **native**, no SMTP companion needed |

**Keyword mapping** lives in `JmapCore.h` as pure functions
(`FlagsToKeywords` / `KeywordsToFlags`), mirroring `FlagsToImapString` /
`ImapStringToFlags` in the IMAP plug-in, with the same unit-test treatment.

**Id mapping (the one real impedance mismatch).** JMAP Email ids are opaque
strings; the interface (and UltraMail's `LocalStore`) key messages by integer
uid. Proposal, phased:

1. *Short term (no interface change):* the plug-in keeps a per-folder
   bidirectional `uid ↔ emailId` table, assigning monotonically increasing
   uids in `receivedAt` order and persisting nothing — the table is rebuilt
   from `Email/query` on each session, and `uidValidity` is derived from a
   hash of the query `state` so consumers correctly resync when it changes.
   This keeps `SyncEngine` working unmodified.
2. *Longer term (interface v2):* add `std::string id` to
   `UltraNetMailEnvelope` and string-id overloads (or an
   `IMailbox2ProtocolPlugin`) so JMAP-native ids and `state`-string delta sync
   (`Email/changes`) can be used first-class — that unlocks JMAP's cheap
   incremental sync instead of uid-window polling.

### 4.3 Push (phase 3)

Subscribe `UltraNet_SseStreamAsync` to the session's `eventSourceUrl`;
`StateChange` events carry per-account `Email`/`Mailbox` state strings. Surface
as an optional callback on the plug-in (or a future
`IMailboxPushProtocolPlugin` mix-in) that UltraMail's `SyncScheduler` uses to
trigger immediate `SyncMessages` instead of its poll interval.

### 4.4 Discovery (phase 3)

Extend `UltraMail::AutoDiscovery::Discover` with a JMAP step *ahead of* the
IMAP presets: SRV `_jmap._tcp.<domain>` → fallback `/.well-known/jmap` probe on
the mail domain. A successful probe yields a `jmap://` account, marking the
`DiscoveryResult` source as `"jmap"`.

### 4.5 Tests

`Tests/UltraNet/test_jmap_plugin.cpp`, same structure as
`test_imap_mailbox.cpp`:

* Pure `JmapCore.h` tests, no network: session-resource parsing (capabilities,
  account selection, URL templates `{blobId}` etc.), request-builder JSON
  shape, `Email/get` → `UltraNetMailEnvelope`, keyword↔flag round-trip,
  error mapping (`methodNotAllowed`, `overQuota`, HTTP 401 →
  `AuthenticationFailed`).
* DSO test: plug-in loads, reports scheme `jmap`, exposes
  `IMailboxProtocolPlugin` via `dynamic_cast`, rejects malformed server URLs.
* UltraMail `SyncEngine` already runs against a fake `IMailboxProtocolPlugin`
  (`Tests/UltraMail/test_syncengine.cpp`), which covers the consumer side.

## 5. Phasing & effort

| Phase | Contents | Size |
|---|---|---|
| 1 | Vendor JSON header; `JmapCore.h` + session fetch + read path (`ListFolders`, `GetMailboxStatus`, `FetchEnvelopes`, `FetchMessage`, `FetchMessages`); CMake + tests | ~1.5–2 kLoC, the bulk of the work |
| 2 | Mutations + submission (`StoreFlags`, `MoveMessage`, `AppendMessage`, `SendMail` via `EmailSubmission`) | small — same request machinery |
| 3 | SSE push, JMAP autodiscovery step in UltraMail, envelope string-id interface v2 + `Email/changes` delta sync | independent follow-ups |

Verification targets: any Stalwart or Cyrus test instance, or a Fastmail
account (Basic app-password auth) — phase 1 is exercisable read-only.

## 6. Decisions

1. **JSON dependency** — *decided & done:* nlohmann/json v3.12.0
   ([json.nlohmann.me](https://json.nlohmann.me/),
   [github.com/nlohmann/json](https://github.com/nlohmann/json), MIT) is
   vendored at `UltraCanvas/third_party/nlohmann/json.hpp` and listed in
   `Docs/Dependencies.md`, the demo app's dependencies table and
   `THIRD_PARTY_LICENSES.md`.
2. **Interface evolution** — phase 1 ships on the uid-mapping shim
   (`JmapCore.h` `UidMap`); the `UltraNetMailEnvelope.id` extension remains a
   phase 3 follow-up.
3. Whether `SendMail` through JMAP should be preferred by UltraMail when an
   account is JMAP-discovered (replacing the SMTP path for those accounts) —
   open until phase 2.
