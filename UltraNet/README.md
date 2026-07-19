# UltraNet

**Unified network communication layer for ULTRA OS.**
Sibling of `UltraCanvas` (UI) and `UltraAI` (AI capabilities).

UltraNet gives every ULTRA OS app a single, secure, high-performance
surface for network protocols — HTTP/HTTPS, WebSocket, FTP/SFTP,
TCP/UDP sockets, TLS, DNS — with a plugin architecture for everything
else (SMTP, IMAP, MQTT, SSH, LDAP, RTSP, gRPC, …).

> Status: Public API specified in the master registry. Implementation
> tracked separately; this overview reflects what apps and other ULTRA
> OS modules can rely on.

---

## Why it exists

Networking is exactly the kind of concern an OS owns once: TLS
verification, proxy detection, retries, connection pooling, DNS
caching, certificate handling, HTTP/2, streaming, cancellation.
Without a shared module, every UltraCanvas app and every UltraAI
adapter would re-implement the same plumbing — and inevitably get
the security parts wrong.

UltraNet centralises that plumbing behind a stable C-style API
(`UltraNet_*` free functions + opaque `UltraNetHandle`s) backed by
**libcurl + OpenSSL** for the core, with platform glue for Linux,
macOS, Windows, and ULTRA OS native.

---

## Protocols at a glance

### Core (Tier 1, in module)

| Category | Protocols | Header |
|---|---|---|
| Web | HTTP, HTTPS | `UltraNet/UltraNetHttp.h` |
| Real-time | WebSocket (ws / wss) | `UltraNet/UltraNetWebSocket.h` |
| File transfer | FTP, FTPS, SFTP | `UltraNet/UltraNetFtp.h` |
| Transport | TCP, UDP | `UltraNet/UltraNetSocket.h` |
| Security | TLS 1.2 / 1.3, custom CA bundles | `UltraNet/UltraNetTls.h` |
| Resolution | DNS (A, AAAA, MX, TXT, SRV, PTR, …) | `UltraNet/UltraNetDns.h` |
| Sessions | Cookies, connection reuse | `UltraNet/UltraNetCookies.h` |
| Proxy | HTTP / HTTPS / SOCKS4 / SOCKS5 / system | `UltraNet/UltraNetProxy.h` |
| URL | Parse, build, encode, query strings | `UltraNet/UltraNetUrl.h` |

### Plugin-supplied (Tier 2/3)

`SMTP`, `IMAP`, `POP3`, `JMAP`, `MQTT`, `gRPC`, `WebDAV`, `mDNS`, `SSH`,
`Telnet`, `LDAP`, `RTSP`, `RTMP`, `RTP`, `SIP`, `CoAP`, `MQTT-SN`,
`SNMP`. Each is added through the
`I<Category>ProtocolPlugin` interface in
`UltraNet/UltraNetPlugins.h`.

---

## Architecture

```
App / Module (UltraCanvas, UltraAI, IODeviceManager, ...)
  │
  ├─► UltraNet_Http* / WebSocket* / Ftp* / Tcp* / Udp* / Tls* / Dns*
  │     │
  │     ├─► libcurl + OpenSSL                  (core protocols)
  │     ├─► Platform glue                      (Linux / Win / Mac / UltraOS)
  │     └─► Plugin manager
  │            └─► IUltraNetPlugin             (smtp, mqtt, ssh, ...)
  │
  └─► UltraVault                                (named credentials)  [opt-in]
```

* Single C-style entrypoint surface (`UltraNet_*`).
* Opaque `UltraNetHandle` (`uint64_t`) for sockets / sessions /
  WebSockets / async requests.
* `UltraNetResult` for every blocking operation — explicit error
  code, message, HTTP status, processing time.
* Sync **and** async variants for HTTP requests; cancellation via
  `UltraNet_CancelRequest(handle)`.
* Protocol additions are **plugin-only** — the core binary doesn't
  change to add SMTP / MQTT / SSH.

---

## Quick examples

```cpp
#include <UltraNet/UltraNetCore.h>
#include <UltraNet/UltraNetHttp.h>

// HTTPS GET
UltraNetResponse r;
if (UltraNet_HttpGet("https://api.example.com/v1/health", r)) {
    std::cout << r.GetBodyAsString() << '\n';
}

// Async POST with body and headers
UltraNetHttpRequest req;
req.url    = "https://api.example.com/v1/items";
req.method = UltraNetHttpMethod::Post;
req.headers.Set("authorization", "Bearer " + token);
req.headers.Set("content-type", "application/json");
req.body   = SerializeJson(payload);

UltraNetHandle h = UltraNet_HttpRequestAsync(req,
    [](const UltraNetResponse& resp) {
        // runs on libcurl multi worker thread — marshal to UI thread if needed
    });

// File download streamed straight to disk
UltraNet_HttpDownloadFile("https://cdn.example.com/big.zip",
                          "/tmp/big.zip");
```

```cpp
#include <UltraNet/UltraNetWebSocket.h>

UltraNetHandle ws = UltraNet_WebSocketConnect("wss://stream.example.com");
UltraNet_WebSocketSendText(ws, R"({"subscribe":"BTCUSD"})");
// inbound frames arrive via onWebSocketText / onWebSocketBinary callbacks
UltraNet_WebSocketClose(ws);
```

```cpp
#include <UltraNet/UltraNetDns.h>

std::vector<std::string> mx;
UltraNet_DnsResolve("example.com", mx, UltraNetDnsType::MX);
```

---

## Module layout

```
UltraCanvas/                      (or wherever the build places it)
├── include/UltraNet/
│   ├── UltraNetCore.h
│   ├── UltraNetHttp.h
│   ├── UltraNetWebSocket.h
│   ├── UltraNetFtp.h
│   ├── UltraNetSocket.h
│   ├── UltraNetTls.h
│   ├── UltraNetDns.h
│   ├── UltraNetCookies.h
│   ├── UltraNetProxy.h
│   ├── UltraNetUrl.h
│   └── UltraNetPlugins.h
├── core/UltraNet/                (.cpp implementations of the headers above)
├── OS/<Platform>/UltraNetSupport.cpp
└── Plugins/UltraNet/
    ├── smtp/
    ├── mqtt/
    ├── ssh/
    └── ...
```

CMake target: `UltraNet`. Header include style: `<UltraNet/UltraNet*.h>`.

---

## Integration with sibling modules

| Caller | Uses UltraNet for |
|---|---|
| **UltraAI** adapters (Anthropic, OpenAI, ElevenLabs, …) | HTTPS REST + SSE-streamed responses + WebSocket live STT. See `UltraAI/Docs/UltraNetIntegration.md`. |
| **UltraCanvas apps** | Loading remote images / data, REST APIs, media downloads. |
| **IODeviceManager** | HTTP control of network cameras (ONVIF), printers, etc. |
| **Package / update tooling** | TLS-pinned downloads from ULTRA Store CDN. |
| **Future plugins** | Anything needing a transport — mail clients, MQTT brokers, SSH terminals. |

UltraNet itself depends on **UltraVault** (when present) for credential
lookup — keys, certs, tokens — instead of carrying secrets in caller
code. See `UltraAI/Docs/UltraVault.md` for the credential-storage
architecture.

---

## Conventions

* **Naming:** `UltraNet_<Action><Target>()` (e.g. `UltraNet_HttpGet`,
  `UltraNet_WebSocketSendText`). Types use `UltraNet<Type>`. Plugin
  interfaces use `I<Category>ProtocolPlugin`. Callbacks use
  `on<Event>` (base verb form).
* **Errors:** every blocking call returns `UltraNetResult`. Operator
  `bool` for quick success checks; structured fields for diagnostics.
* **Handles:** zero (`0`) is invalid. Always check before use.
* **Security defaults:** TLS verification ON, minimum TLS 1.2,
  hostname check ON, `acceptInvalidCert` requires explicit opt-in.
* **Threading:** async callbacks run on the libcurl multi worker
  thread — callers must marshal to their own loop and not block.
* **Reserved:** never write `HttpClient`, `Connect()`, `Download()`,
  `ResolveHost()` etc. at module level — always go through the
  `UltraNet_*` API. (Full reserved-pattern list lives in the master
  registry.)

---

## Status

| Component | State |
|---|---|
| Public API (master registry) | Locked at v1.0.0 |
| Core implementation | In progress (libcurl + OpenSSL) |
| Linux / macOS / Windows backends | Planned |
| ULTRA OS native backend | Planned |
| Plugins (SMTP, MQTT, SSH, …) | Tracked separately |

---

## Reference

* **Master registry** — full function list, types, callbacks, plugin
  interfaces, reserved patterns, and security/performance rules.
* **UltraAI integration** — `UltraAI/Docs/UltraNetIntegration.md`
  (capability ↔ primitive map, SSE handling, threading model, adapter
  checklist, end-to-end examples).
* **Credential storage** — `UltraAI/Docs/UltraVault.md`.

---

*Part of ULTRA OS · MIT license · Cloverleaf UG*
