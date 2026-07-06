# Masterfile_modules

Authoritative module registry for the ULTRA OS / UltraCanvas framework.
This file enumerates the top-level modules and the rules each module must
follow. Each section pairs a one-paragraph purpose with the public function
surface that downstream applications and plugins can depend on.

---

### **1. UltraCanvas**

UI widget framework — windows, controls, layout, rendering. Sources under
`UltraCanvas/{include,core,libspecific,OS/<Platform>,Plugins}`.

### **2. UltraAI**

Provider-agnostic AI capabilities (LLM, embeddings, STT, TTS, vision,
image / video / music generation, translation, code assist). See
`Docs/Modules/UltraAI/README.md`.

### **3. FileLoader**

Universal file loading / saving / converting facade. See
`Docs/Modules/FileLoader/README.md`. Generic byte-level entry point
`UltraCanvasFileLoader::LoadFile(pathOrUrl)` accepts both filesystem paths
and `http(s)://` URLs (URLs require UltraNet to be built in).

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
  `UltraNet_TcpSend`, `UltraNet_TcpReceive`
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

**Implementation status (this branch):** Stage 1 of the rollout —
`UltraNet_HttpGet/Post/Put/Delete/Head/Patch`, `UltraNet_HttpRequest`,
`UltraNet_HttpDownloadFile/UploadFile`, and the URL utilities are
implemented synchronously on libcurl. Async, WebSocket, FTP, raw sockets,
TLS layering, DNS, sessions, and the plugin manager are planned for
Stages 2-3. See `Docs/Modules/UltraNet/README.md`.

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
