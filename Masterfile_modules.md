# Masterfile_modules

Authoritative module registry for the ULTRA OS / UltraCanvas framework.
This file enumerates the top-level modules and the rules each module must
follow. Each section pairs a one-paragraph purpose with the public function
surface that downstream applications and plugins can depend on.

---

### **1. UltraCanvas**

UI widget framework — windows, controls, layout, rendering. Sources under
`UltraCanvas/{include,core,libspecific,OS/<Platform>,Plugins}`.

**DataFormats section** — framework-wide structured-data facilities under
`UltraCanvas/{include,core}/DataFormats/`. These are core services (usable by
core, plugins and applications alike; never implemented inside a file-type
plugin). Public engines are always wrapped behind an UltraCanvas-owned API so
the backing implementation can be replaced without affecting callers.

- **UltraCanvasJSON** (`DataFormats/UltraCanvasJSON.h`) — general-purpose JSON
  parsing and serialization, backed by the vendored yyjson engine
  (`UltraCanvas/third_party/yyjson`, MIT). Public surface:
  - `JSONValue` — value-semantic DOM (Null / Boolean / Number / String /
    Array / Object; objects preserve insertion order); accessors
    `GetBoolean/GetInteger/GetNumber/GetString` (fallback-based, never throw),
    structure access `At/Find/Get/Contains/GetSize/operator[]`, mutation
    `Append/Set/Remove`, builders `MakeArray/MakeObject`.
  - `JSON::Parse`, `JSON::ParseFile` — strict RFC 8259 by default; opt-in
    comments / trailing commas / Inf-NaN via `JSONParseOptions`; errors are
    reported through `JSONParseResult` (message, byte position, line, column);
    nesting depth is limited to defend against hostile input.
  - `JSON::Serialize`, `JSON::SerializeToFile` — compact or pretty output via
    `JSONSerializeOptions`; output is always strictly valid JSON.
  - `JSON::EscapeString` and framework-type helpers
    `FromColor/ToColor`, `FromPoint/ToPoint`, `FromRect/ToRect`.

- **UltraCanvasFileAssociations** (`UltraCanvasFileAssociations.h`) — the
  cross-platform "Open with" service: which applications the OS registers
  for a file, and detached launching. Core worker/cache in
  `core/UltraCanvasFileAssociations.cpp`; per-platform backends behind the
  internal `UltraCanvasFileAssociationsBackend.h` under `OS/<Platform>/`
  (Linux/BSD: freedesktop, full; Windows/macOS: default-open placeholders).
  Public surface (`namespace FileAssociations` + `FileAssociationApp`):
  - `GetApplicationsForFiles` — candidates for a selection (intersection),
    default application first, cache-served once prewarmed.
  - `OpenWithDefaultApplication` / `OpenWithApplication` /
    `OpenWithApplicationPath` — detached launches (default handler /
    enumerated app / user-picked executable).
  - `GetApplicationFilter` / `GetApplicationsDirectory` — file-dialog setup
    for an "Other application…" picker (the picker UI lives with the caller).
  - `PrewarmAsync` / `PrewarmExtensionsAsync` — background-worker warm-up;
    the worker only exists once a caller asks for it.
  See `Docs/UltraCanvas/UltraCanvasFileAssociations.md`.

### **2. UltraAI**

Provider-agnostic AI capabilities (LLM, embeddings, STT, TTS, vision,
image / video / music generation, translation, code assist). See
`Docs/Modules/UltraAI/README.md`.

### **3. FileLoader**

Universal file loading / saving / converting facade. See
`Docs/Modules/FileLoader/README.md`. Generic byte-level entry point
`UltraCanvasFileLoader::LoadFile(pathOrUrl, autoDecompress = true)` accepts
both filesystem paths and `http(s)://` URLs (URLs require UltraNet to be
built in). Compressed content (gzip/zlib/Zstandard/LZ4, detected by magic
bytes) is transparently decompressed via the VirtualFS compression API, so
applications never deal with compression formats themselves;
`FileBytesResult::decompressedFrom` records the source format and
`autoDecompress = false` opts out.

### **4. Plug-ins for File Types**

Per-format plug-ins under `UltraCanvas/Plugins/` (Charts, Diagrams, Text,
Vector, SVG, Documents, Videos, Graphs).

### **5. UltraNet**

The UltraNet module provides comprehensive functionality to equip programmers
with all kinds of network communication services to cover all client and
server connectivity tasks.

UltraNet elements must comply with the following rules:
- Clear structure
- Function and call names must be easily understandable by their names
- Comprehensive options to cover all use cases
- New protocols must use the same function and extension naming patterns as
  previously created functions and callbacks
- TLS verification is ON by default; insecure modes require explicit opt-in
- All blocking operations return `UltraNetResult`; all connection-oriented
  operations return `UltraNetHandle`

UltraNet uses open-source libraries (libcurl, OpenSSL, libssh2, c-ares) and
encapsulates their functionality in an easy-to-understand and usable way to
remain flexible for extending or replacing supporting libraries in the
future.

**Available Functions (Core, Tier 1):**
- `UltraNet_HttpGet`, `UltraNet_HttpPost`, `UltraNet_HttpPut`,
  `UltraNet_HttpDelete`, `UltraNet_HttpHead`, `UltraNet_HttpPatch`
- `UltraNet_HttpRequest`, `UltraNet_HttpRequestAsync`
- `UltraNet_HttpDownloadFile`, `UltraNet_HttpUploadFile`
- `UltraNet_WebSocketConnect`, `UltraNet_WebSocketSendText`,
  `UltraNet_WebSocketSendBinary`, `UltraNet_WebSocketClose`
- `UltraNet_FtpDownload`, `UltraNet_FtpUpload`, `UltraNet_FtpListDirectory`,
  `UltraNet_FtpDelete`, `UltraNet_FtpRename`
- `UltraNet_TcpConnect`, `UltraNet_TcpListen`, `UltraNet_TcpAccept`,
  `UltraNet_TcpSend`, `UltraNet_TcpReceive`, `UltraNet_SocketLocalEndpoint`
- `UltraNet_OAuth2GeneratePkce`, `UltraNet_OAuth2ChallengeFromVerifier`,
  `UltraNet_OAuth2GenerateState`, `UltraNet_OAuth2BuildAuthUrl`,
  `UltraNet_OAuth2WaitForCallback`, `UltraNet_OAuth2ExchangeCode`,
  `UltraNet_OAuth2Refresh`, `UltraNet_OAuth2ParseTokenResponse`,
  `UltraNet_OAuth2AuthorizeInteractive`
- `UltraNet_UdpOpen`, `UltraNet_UdpSend`, `UltraNet_UdpReceive`
- `UltraNet_TlsWrap`, `UltraNet_TlsHandshake`, `UltraNet_TlsGetInfo`
- `UltraNet_DnsResolve`, `UltraNet_DnsResolveAsync`, `UltraNet_DnsReverseLookup`
- `UltraNet_CreateSession`, `UltraNet_SessionHttpGet`, `UltraNet_SessionHttpPost`
- `UltraNet_ParseUrl`, `UltraNet_BuildUrl`, `UltraNet_UrlEncode`,
  `UltraNet_UrlDecode`
- `UltraNet_CancelRequest`, `UltraNet_GetTransferStats`
- `UltraNet_RegisterPlugin`, `UltraNet_GetSupportedSchemes`

**Plugin Categories (Tier 2 / Tier 3, located under `Plugins/UltraNet/`):**
- Mail: SMTP, IMAP, POP3
- Messaging: MQTT, AMQP
- Remote Access: SSH, Telnet
- Directory: LDAP, LDAPS
- Streaming: RTSP, RTMP, RTP, SIP
- IoT: CoAP, MQTT-SN, SNMP
- Discovery: mDNS / Bonjour / Zeroconf
- Web Modern: gRPC, HTTP/3 (QUIC), WebDAV

UltraNet is the recommended communication module for ULTRA Store,
UltraTexter, UltraFiler, and any other UltraCanvas-based application that
requires network connectivity. FileLoader uses UltraNet internally when
loading files from `http://`, `https://`, `ftp://`, etc. URLs.

**Implementation status (this branch):** Stage 2/3 — the synchronous HTTP
verbs, `UltraNet_HttpRequest`, download/upload, async via a curl_multi
worker (`UltraNet_HttpRequestAsync` with chunked `onDataChunk` streaming,
`UltraNet_CancelRequest` / `UltraNet_IsRequestActive` /
`UltraNet_GetTransferStats`), SSE (`UltraNet_SseStream[Async]` +
`UltraNetSseParser`), WebSocket (on libcurl ≥ 7.86 with ws support),
sessions/cookies, TLS layering, DNS, and the URL utilities are implemented
on libcurl. Remaining gaps are per-function and environment-dependent —
build and run the `UltraNetApiStatus` probe tool for ground truth on a
given machine. See `Docs/Modules/UltraNet/README.md`.

### **6. UltraDatabase**

Unified database access layer. Central registry of **named connections**
that apps define once and reference everywhere; the backing engine is a
configuration detail. Bundles **SQLite** for zero-config embedded storage
and adds other engines (PostgreSQL, MySQL/MariaDB, MSSQL, Redis, MongoDB,
DuckDB) as driver plug-ins under `Plugins/UltraDatabase/` via the
`IDatabaseDriverPlugin` interface — the core binary does not change to add
an engine.

UltraDatabase elements must comply with the same rules as UltraNet:
- Clear structure; call names understandable by their names
- New drivers use the same function and callback naming patterns
- **Parameter binding always** — SQL text and values travel separately, so
  injection is not possible through the public API (no string-built SQL)
- TLS verification ON by default for networked engines; credentials come
  from UltraVault, never from app code or config files
- All blocking operations return `UltraDbResult`; all statement /
  transaction / cursor / async operations return `UltraDbHandle`

Like UltraNet, it encapsulates open-source libraries (bundled SQLite,
`libpq`, `libmariadb`, ODBC, `hiredis`) so a backing library can be
extended or replaced without changing callers.

**Available Functions (Core, Tier 1):**
- `UltraDb_RegisterConnection`, `UltraDb_CloseConnection`,
  `UltraDb_GetConnectionInfo`
- `UltraDb_Query`, `UltraDb_Exec`, `UltraDb_QueryAsync`
- `UltraDb_Prepare`, `UltraDb_ExecPrepared`, `UltraDb_Finalize`
- `UltraDb_Begin`, `UltraDb_ExecInTx`, `UltraDb_Commit`, `UltraDb_Rollback`
- `UltraDb_OpenCursor`, `UltraDb_FetchRow`, `UltraDb_CloseCursor`
- `UltraDb_Migrate`, `UltraDb_CancelQuery`
- `UltraDb_RegisterDriver`, `UltraDb_GetSupportedDrivers`

**Driver Categories (Tier 2 / Tier 3, located under `Plugins/UltraDatabase/`):**
- Relational (SQL): PostgreSQL, MySQL / MariaDB, Microsoft SQL Server
- Embedded: SQLite (core, bundled), DuckDB (analytics)
- Key-value: Redis / Valkey
- Document: MongoDB

UltraDatabase is the recommended persistence module for UltraMail, ULTRA
Store, and any other UltraCanvas-based application that needs local or
networked storage. It depends on UltraVault for credentials and,
optionally, on UltraNet for TLS transport to networked engines.

**Implementation status (this branch):** Concept / design only. Public
surface specified in `Docs/Modules/UltraDatabase/README.md`; suggested
rollout is SQLite core + registry + query/transaction/migration API
(Stage 1), async + pooling + PostgreSQL/MySQL drivers (Stage 2),
remaining drivers and at-rest encryption (Stage 3).

### **7. UltraWin**

The UltraWin module runs Windows applications on Linux / ULTRA OS as single
native windows — never a Windows desktop — with the user's own folders
visible to the applications under a unified drive letter.

UltraWin elements must comply with the following rules:
- Clear structure; function and call names must be easily understandable
- Blocking operations return `UltraWinResult`; application instances are
  opaque `UltraWinHandle`s
- No Windows desktop is ever displayed; no full-desktop viewer paths
- Engines are never linked: Wine (and later QEMU) run as spawned child
  processes, keeping LGPL/GPL licensing outside the framework binaries
- Wine's default `Z:` → `/` host-root exposure is off by default; the
  user's home is mapped as `U:` in every environment

UltraWin uses open-source engines (Wine for the API-translation tier;
QEMU/KVM + FreeRDP RemoteApp planned for the full-virtualisation tier) and
encapsulates them so backings can be swapped — see
`Docs/Research/UltraWinDesignProposal.md`.

**Available Functions (Stage 1, Wine tier):**
- `UltraWin_Initialize`, `UltraWin_Shutdown`, `UltraWin_IsInitialized`,
  `UltraWin_GetConfig`, `UltraWin_SetConfig`, `UltraWin_GetCapabilities`,
  `UltraWin_GetVersion`
- `UltraWin_CreateEnvironment`, `UltraWin_DeleteEnvironment`,
  `UltraWin_ListEnvironments`, `UltraWin_EnvironmentExists`
- `UltraWin_MapFolder`, `UltraWin_UnmapFolder`, `UltraWin_ListMappings`
- `UltraWin_InstallComponent`, `UltraWin_ListComponents` (winetricks-verb
  components: VC++ runtimes, fonts, .NET, DXVK, … — spawned winetricks)
- `UltraWin_RunApp`, `UltraWin_CloseApp`, `UltraWin_KillApp`,
  `UltraWin_GetAppInfo`, `UltraWin_GetAppState`, `UltraWin_ListApps`,
  `UltraWin_WaitApp`, `UltraWin_ReleaseApp`

**Planned (Stage 2/3):** `UltraWin_VmProvision`, `UltraWin_VmStart`,
`UltraWin_VmSuspend`, `UltraWin_VmStop`,
`UltraWin_QueryCompatibility`, and the `UltraCanvasRemoteAppView` element
for FreeRDP RemoteApp windows.

UltraWin is the recommended way for UltraFiler and any UltraCanvas-based
application to launch Windows executables. Linux / ULTRA OS only.

**Implementation status (this branch):** Stage 1 of the rollout — module
lifecycle, capability probing, environments (isolated Wine prefixes with
persisted drive mappings), application launch/supervision, and the
component installer (winetricks wrapper) are implemented; the VM tier and
compatibility routing are planned for Stages 2-3. See
`Docs/Modules/UltraWin/README.md`.

### **8. UltraCrypt**

Cryptographic services — the single place where hashing, message
authentication, authenticated encryption, key derivation and secure random
generation are implemented, so no application, plugin or sibling module has
to write its own. Sources under `UltraCanvas/{include,core}/UltraCrypt/`;
see `Docs/Modules/UltraCrypt/README.md`.

Scope is **data at rest and data integrity**. Transport security stays in
UltraNet (per-platform and OS-native); credential *storage and policy* stay
in UltraVault, which consumes UltraCrypt for its file-backed fallback
backend.

UltraCrypt elements must comply with the same rules as UltraNet and
UltraDatabase:
- Clear structure; call names understandable by their names
- New algorithms use the same function and type naming patterns
- **AEAD only** — no raw block-cipher or unauthenticated-mode surface is
  exposed, so a caller cannot accidentally ship unauthenticated ciphertext
- Nonces are generated by the module by default; reuse must be opted into
- The CSPRNG never falls back to a PRNG — entropy failure is an error
- Secrets travel in `UltraCryptSecureBuffer` (move-only, zeroized on
  destruction, page-locked where the OS permits), never in `std::string`
- All blocking operations return `UltraCryptResult`

Like UltraNet and UltraDatabase, it encapsulates an open-source library so
the backing implementation can be replaced without affecting callers; the
backing library is never visible in a public header.

**Available Functions (Core, Tier 1):**
- `UltraCrypt_Initialize`, `UltraCrypt_Shutdown`, `UltraCrypt_IsAvailable`,
  `UltraCrypt_GetBackendName`
- `UltraCrypt_Hash`, `UltraCrypt_HashFile`, `UltraCrypt_GetDigestSize`,
  `UltraCrypt_IsHashAvailable`
- `UltraCrypt_Hmac`
- `UltraCrypt_AeadSeal`, `UltraCrypt_AeadOpen`, `UltraCrypt_GetKeySize`,
  `UltraCrypt_GetNonceSize`, `UltraCrypt_GetTagSize`,
  `UltraCrypt_IsAeadAvailable`
- `UltraCrypt_DeriveKeyFromPassword`, `UltraCrypt_DeriveKeyHkdf`,
  `UltraCrypt_RecommendedKdfParams`
- `UltraCrypt_RandomBytes`, `UltraCrypt_RandomSecureBuffer`,
  `UltraCrypt_RandomUInt32`, `UltraCrypt_GenerateUuidV4`
- `UltraCrypt_SecureZero`, `UltraCrypt_ConstantTimeEquals`
- `UltraCrypt_ToHex`, `UltraCrypt_FromHex`, `UltraCrypt_Base64Encode`,
  `UltraCrypt_Base64Decode`, `UltraCrypt_Base32Encode`,
  `UltraCrypt_Base32Decode`

Streaming classes: `UltraCryptHasher`, `UltraCryptHmacHasher`.

UltraCrypt is the required cryptographic module for the UCD v2 file format,
UltraVault, UltraDatabase at-rest encryption, UltraAuthenticator and any
other UltraCanvas-based code that hashes, signs or encrypts stored data.
Hand-rolled crypto and direct calls into a vendored crypto library are
defects, not shortcuts.

**One cipher, one KDF.** Everything UltraCanvas writes is encrypted with
**XChaCha20-Poly1305** and keyed with **Argon2id**; no algorithm menu is
offered, because the formats are ours and a second choice would only be a
second code path in every reader. AES-256-GCM is present solely for reading
foreign data and is hardware-gated. Algorithm agility lives in format
version numbers, not in per-file algorithm identifiers.

**Implementation status (this branch):** Implemented on libsodium
(`UltraCanvas/{include,core}/UltraCrypt/`, target `UltraCrypt`): secure
buffers, secure zero, constant-time compare, CSPRNG, SHA-1/SHA-2 (one-shot,
streaming, file), HMAC, XChaCha20-Poly1305 AEAD, Argon2id, HKDF, and the
RFC 4648 Base16/32/64 companions — with published-vector unit tests
(`Tests/UltraCryptTests.cpp`). Built without libsodium every operation
fails closed with `BackendUnavailable`. First consumers: the UCD document
envelope and UltraVault's encrypted-file backend.

### **9. UltraVault**

Credential and secret storage — the single system-level home for API keys,
tokens and passphrases, so no application or module rolls its own
(`UltraAI/Docs/UltraVault.md` is the design document). Sources under
`UltraCanvas/{include,core}/UltraVault/`, target `UltraVault`, header
`<UltraVault/UltraVault.h>`, `namespace UltraVault`.

Public surface: `Result`/`ResultCode`, `SecretValue` (bytes + MIME type),
`SecretAcl`, `Initialize`/`Shutdown`/`IsAvailable`/`GetBackendName`,
`Put`/`Get`/`Delete`/`List(prefix)`, and namespaced keys
(`<vendor>.<app>.<purpose>`, e.g. `ai.anthropic.api_key`). UltraAI resolves
`ProviderConfig::apiKeyVaultRef` through `UltraVault::Get` when built with
`ULTRAAI_USE_ULTRAVAULT` (on by default in-tree).

**Implementation status (this branch):** v0.1 — memory backend (CI /
ephemeral) and encrypted-file backend (Argon2id-derived key, stored cost
parameters, XChaCha20-Poly1305 with the header as associated data; wrong
passphrase and file tampering are deliberately indistinguishable). Unit
tests in `Tests/UltraVaultTests.cpp`; the UltraAI resolution path is
covered by `Tests/UltraAIVaultIntegrationTests.cpp`. Platform-native
backends (libsecret / Keychain / Credential Manager) and
`Import`/`PromptUserForSecret` are planned.
persisted drive mappings), application launch/supervision, and the
component installer (winetricks wrapper) are implemented; the VM tier and
compatibility routing are planned for Stages 2-3. See
`Docs/Modules/UltraWin/README.md`.
