<!-- Generated from UltraNet/README.md by scripts/generate_llms_txt.py; edit that file, then rerun the script. -->
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
**libcurl** for the core, with platform glue for Linux, macOS,
Windows, and ULTRA OS native.

To minimise impact on the host system, the TLS backend follows whatever
libcurl was built against on each platform:

| Platform | TLS backend     | Extra dependency? |
|---|---|---|
| Linux    | OpenSSL         | `libcurl4-openssl-dev` |
| Windows  | Schannel        | None — Win32 native crypto |
| macOS    | SecureTransport | None — Apple's system libcurl |
| ULTRA OS | tbd             | Selected by the ULTRA OS build |

UltraNet never calls TLS-library APIs directly, so swapping the backend
is purely a libcurl build option.

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
| Resolution | DNS (A, AAAA, MX, TXT, SRV, PTR, …); per-call name servers and deadline (`UltraNetDnsOptions`) | `UltraNet/UltraNetDns.h` |
| Sessions | Cookies, connection reuse | `UltraNet/UltraNetCookies.h` |
| Auth | OAuth 2.0 authorization-code + PKCE, loopback redirect, token refresh | `UltraNet/UltraNetOAuth2.h` |
| Auth | The process-wide OAuth2 *app registry*: the client id / secret / redirect URI per provider, from code, the environment, an INI file or a baked-in default, with aliases — shared by UltraMail and UltraCloud | `UltraNet/UltraNetOAuth2Apps.h` |
| Proxy | HTTP / HTTPS / SOCKS4 / SOCKS5 / system | `UltraNet/UltraNetProxy.h` |
| URL | Parse, build, encode, query strings | `UltraNet/UltraNetUrl.h` |
| MIME | base64 / quoted-printable, RFC 2047 headers, multipart parse + build; text in any charset iconv knows (ISO-2022-JP - also raw in headers -, Shift_JIS, GB18030, KOI8-R, windows-125x, ...) converted to UTF-8, UTF-8 and Latin-1 without iconv; unlabelled 8-bit header text read in the body's charset or the best-scoring guess | `UltraNet/UltraNetMime.h` |

### Plugin-supplied (Tier 2/3)

`SMTP`, `IMAP`, `POP3`, `MQTT`, `gRPC`, `WebDAV`, `mDNS`, `SSH`,
`Telnet`, `LDAP`, `RTSP`, `RTMP`, `RTP`, `SIP`, `CoAP`, `MQTT-SN`,
`SNMP`. Each is added through the
`I<Category>ProtocolPlugin` interface in
`UltraNet/UltraNetPlugins.h`.

A plug-in is a DSO (`Plugins/UltraNet/*.so|.dll`) loaded by
`UltraNet_RefreshPlugins()`, and it reaches the core **only through the host
table** (`UltraNetPluginHost`, ABI 2) handed to its `UltraNet_PluginInit`:
registering itself, and the few core functions it calls — `UltraNet_ParseUrl`,
`UltraNet_UrlEncode` / `UrlDecode`, `UltraNet_ResolveCaBundlePath`,
`UltraNet_MimeBuild`, `UltraNet_HttpGet` / `HttpRequest`, ... Plug-in sources
call those functions as usual;
`Plugins/UltraNet/common/UltraNetPluginHostShim.cpp`, compiled into every
plug-in, defines them inside the DSO and forwards each call to the table. So a
plug-in has no undefined core symbol: it loads into an app on a **static**
core (which carries only the objects the app itself uses), and a Windows DLL
links without the core's import library, static core or shared. A plug-in that
needs another core function appends it to `UltraNetPluginHost` (bumping the
ABI), to the host's table in `core/UltraNet/UltraNetPlugins.cpp` and to the
shim, and uses only header-only UltraCanvas helpers. The macOS and Windows
linkers reject a plug-in that calls the core directly; on Linux the
`UltraNetPluginHostImports` test (`scripts/check_ultranet_plugin_imports.py`)
does, reading every built plug-in's undefined symbols. `UltraNet_PluginInit(host)`
is the only entry point the loader accepts: the old POSIX-only v1 entry,
`UltraNet_PluginRegister()`, which reached into the host's symbol table, is
refused (`plugin_loader_refuses_a_v1_only_plugin`).

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
        // A transfer cut off mid-body (timeout, lost connection, over
        // maxReceiveSize, cancelled) keeps the server's status line:
        // check IsComplete() / transferError before trusting resp.body.
        if (!resp.IsComplete()) { /* resp.transferError says why */ }
    });

// A relative reference against a base URL (RFC 3986 section 5)
std::string url;
UltraNet_ResolveUrl("https://x.org/app/main.wasm", "../data/a.json", url);
// url == "https://x.org/data/a.json"; a URL with a control character in it
// (a CR or LF that would end the request line) is refused

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

// Ask a particular server, for this call only - a resolver you are testing,
// a split-horizon server, or an unroutable one to prove a deadline. Every
// backend honours it: c-ares on a channel of its own per list (any address,
// any port), libresolv and dnsapi with the resolver pointed at IPv4 servers
// on port 53. Such a lookup never touches the UltraNet cache.
UltraNetDnsOptions options;
options.servers   = {"9.9.9.9", "[2620:fe::fe]:53"};
options.timeoutMs = 2000;
std::vector<std::string> a;
UltraNet_DnsResolve("example.com", a, UltraNetDnsType::A, options);

// The reverse lookup takes the same options. Without servers it is the
// system resolver (hosts file included) under the deadline; with servers it
// is a PTR query for the address's reverse name at those servers.
std::string host;
UltraNet_DnsReverseLookup("9.9.9.9", host, options);
```

```cpp
#include <UltraNet/UltraNetOAuth2.h>

// OAuth2 sign-in for a desktop app (public client + PKCE). Blocking —
// run on a worker thread; the browser opens via the callback.
UltraNetOAuth2Config cfg;
cfg.authorizationEndpoint = "https://mastodon.social/oauth/authorize";
cfg.tokenEndpoint         = "https://mastodon.social/oauth/token";
cfg.clientId              = clientId;
cfg.scopes                = {"read", "write"};
cfg.redirectUri           = "http://127.0.0.1:0/callback";  // 0 = ephemeral port

UltraNetOAuth2Token token;
auto r = UltraNet_OAuth2AuthorizeInteractive(
    cfg, [](const std::string& url) { UltraCanvas::OpenURL(url); }, token);
if (r) {
    // token.accessToken -> "authorization: Bearer ..." on API calls
    // token.refreshToken -> store in the credential vault;
    //   renew later with UltraNet_OAuth2Refresh(cfg, refreshToken, token)
}
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
│   ├── UltraNetOAuth2.h
│   ├── UltraNetOAuth2Apps.h
│   ├── UltraNetProxy.h
│   ├── UltraNetUrl.h
│   ├── UltraNetMailAddr.h        (header-only, shared by the mail plug-ins)
│   ├── UltraNetCurlDebug.h       (header-only, opt-in protocol trace)
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

## Debugging a mail connection

An account that will not send or fetch is rarely diagnosable from the
`UltraNetResultCode` alone — the server says why in its reply text, and
libcurl discards that once it has mapped the exchange to a `CURLcode`. Set
`ULTRANET_CURL_VERBOSE` to anything but empty or `0` to put the conversation
on stderr:

```
ULTRANET_CURL_VERBOSE=1 ./UltraMail
```

| Line | Meaning |
|---|---|
| `[ultranet] * …` | libcurl's own notes (connection, TLS, auth mechanism chosen) |
| `[ultranet] > …` | what we sent |
| `[ultranet] < …` | what the server answered — `535 5.7.8 Username and Password not accepted`, `555 5.5.2 Syntax error`, `-ERR invalid password` |

It is off unless asked for, and message bodies are never printed. Every line
we send that can carry a secret is printed as `<redacted auth line>`: the
SASL exchange — including the bare base64 continuation lines of `AUTH LOGIN`,
which carry the password with no keyword on them — POP3's `PASS`, and IMAP's
`LOGIN`, which libcurl sends when a server offers no SASL. So a trace can be
pasted into a bug report as it stands. The server's side is kept whole, its
list of mechanisms and its answer to the sign-in included, because which step
failed is the useful part.

Applies to every mail plug-in that goes through libcurl (SMTP, IMAP, POP3);
`ultranet_curldebug::EnableIfRequested()` is one call in the handle setup.
On Windows a GUI build has no stderr to print to: run it from a console, or
read `UltraNetResult::diagnostics`, which carries the connection chain of a
failure either way. FTP calls have their own log, below.

---

## The FTP session log

Every FTP / FTPS / SFTP call (`UltraNetFtp.h`) can report its session as it
runs — the lines an FTP client shows in its message log:

```
Step:     Resolving address of ftp.example.com
Step:     Connecting to 203.0.113.7:21...
Step:     Connection established, waiting for welcome message...
Response: 220 Welcome                          (replyCode 220)
Command:  USER erika
Command:  PASS ********                        (the password is never logged)
Response: 230 Logged in
Step:     Logged in
Command:  PASV
Response: 227 Entering Passive Mode (203,0,113,7,246,253)
Step:     Retrieving directory listing...
Command:  MLSD
Error:    Connection timed out after 30 seconds of inactivity   (transportCode 28)
```

```cpp
UltraNetFtpOptions opt;
opt.credentials.username = "erika";
opt.credentials.password = secret;
opt.onLog = [](const UltraNetFtpLogLine& line) {   // on the calling thread, while it runs
    Show(line.kind, line.text, line.replyCode, line.transportCode);
};
std::vector<UltraNetFtpEntry> entries;
UltraNetResult r = UltraNet_FtpListDirectory("ftp://ftp.example.com/pub/", entries, opt);
// r.message:     "RETR response: 550 - the server said \"550 Permission denied\""
// r.diagnostics: "Error: ... (libcurl error 19: ...)\nConnected to: 203.0.113.7:21\n..."
```

| Kind | Holds |
|---|---|
| `Step` | what the client is doing: libcurl's own notes, plus the steps an FTP client names — *Logged in*, *Retrieving directory listing...*, *Directory listing successful*, *Insecure server, it does not support FTP over TLS* (an `ftpes://` server that refused AUTH TLS) |
| `Command` | a command sent, `PASS` / `ACCT` masked as `********` |
| `Response` | a reply line, with its three-digit `replyCode` (0 inside a multi-line reply) |
| `Error` | the call's last line on a failure: the message, `resultCode`, and libcurl's error number in `transportCode` |

(The kind is `Step`, not `Status`: Xlib `#define`s `Status`.)

A caller that reaches these functions through a layer that builds the options
itself — UltraFiler's drive worker calls UltraCloud's FTP provider — sets a
sink for its thread instead: `UltraNet_SetThreadFtpLog(fn)` returns the
previous sink to put back afterwards, and only calls without an `onLog` of
their own go to it.

What a failure says, whether or not anyone reads the log:

- `message` is libcurl's specific reason (its error buffer, not just the error
  class), with the server's refusal added when the last reply was a 4xx / 5xx.
- `diagnostics` is the connection chain (`UltraNetCurlError.h`) plus the last
  server reply.
- `UltraNetFtpOptions::inactivityTimeoutMs` (default 30 s) ends a call whose
  server has gone quiet — no reply to a command, no bytes of a listing or file
  — as *Connection timed out after N seconds of inactivity*. Before it, a data
  connection that opened and sent nothing held the call indefinitely.
- A listing tries MLSD, then LIST, then NLST only when the server refused the
  command; a failure to connect, sign in, set up TLS or the data connection, or
  a timeout, is reported once rather than three times, and an empty folder is
  listed with one request.

`Tests/UltraNet/test_ftp_log.cpp` drives all of it against a scripted FTP
server on loopback.

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
* **Public-key pinning:** `UltraNetHttpOptions::pinnedPublicKey`
  (`"sha256//<base64>"`, from `UltraNet_PublicKeyPinOf`) is checked on every
  handshake, with `acceptInvalidCert` too - so a self-signed server can be
  trusted by its key and nothing else. A mismatch is `TlsPublicKeyMismatch`;
  a TLS backend that cannot pin gets verification switched back on rather
  than the pin dropped. `capturePeerCertificate` fills
  `UltraNetResponse::tlsInfo`, the pin included. IODeviceManager's trust on
  first use for scanners and printers is built on these.
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
| Core implementation | Stage 2/3 on libcurl: sync + async HTTP, chunked streaming, SSE, WebSocket, sessions, TLS, DNS |
| Linux / macOS / Windows backends | Working (system or vendored libcurl; WebSocket needs libcurl >= 7.86 with ws) |
| ULTRA OS native backend | Planned |
| Plugins (SMTP, MQTT, SSH, ...) | Tracked separately |

Per-function status is not a table anyone has to maintain by hand: build and
run **`UltraNetApiStatus`**, which probes every public entry point on the
machine in front of you and reports it as WORKING, IMPLEMENTED (present but
unverifiable here), NOT IMPLEMENTED, or BROKEN.

```bash
cmake -S . -B build -DULTRACANVAS_BUILD_NET_TESTS=ON
cmake --build build --target UltraNetApiStatus
./build/bin/UltraNetApiStatus                     # or --format=markdown / --format=json
```

It brings its own HTTP, WebSocket, TCP/UDP and TLS peers, so it needs no
internet access. See [`ApiStatus.md`](ApiStatus.md).

---

## Reference

* **API status report** — [`ApiStatus.md`](ApiStatus.md) (the
  `UltraNetApiStatus` tool: statuses, options, how each area is verified,
  how to add a probe).
* **Master registry** — full function list, types, callbacks, plugin
  interfaces, reserved patterns, and security/performance rules.
* **UltraAI integration** — `UltraAI/Docs/UltraNetIntegration.md`
  (capability ↔ primitive map, SSE handling, threading model, adapter
  checklist, end-to-end examples).
* **Credential storage** — `UltraAI/Docs/UltraVault.md`.

---

*Part of ULTRA OS · MIT license · ULTRA OS Development GmbH*
