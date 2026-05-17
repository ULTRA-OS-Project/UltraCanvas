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
