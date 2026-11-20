- **UltraNet: WebSockets on every system - a client of its own where libcurl
  has none (macOS through 15).** `UltraNet_WebSocket*` was libcurl's
  `curl_ws_*` and nothing else, and `UltraNet_WebSocketConnect` refused on a
  libcurl that lists neither `ws` nor `wss`. Apple's own libcurl, which the
  macOS build links, lists none through macOS 15 - so on such a Mac
  everything on a WebSocket had no connection: ChatCube Direct's relay,
  Mattermost's events, XMPP over WebSocket, UltraWeb's sockets, the UltraAI
  transport, the UltraDatabase relay. Nothing said so: UltraNet's WebSocket
  tests skipped there ("libcurl built without --enable-websockets"), and the
  build printed "WebSocket: OK" for macOS without asking the library. The
  chat-network tests' first run on a macOS runner (#1337) found it.
  - **The own client** (`core/UltraNet/UltraNetWebSocketOwn.{h,cpp}`, internal):
    RFC 6455 on UltraNet's own sockets and TLS - `UltraNet_TcpOpen` through
    the proxy setting (SOCKS, or an HTTP proxy as a `CONNECT` tunnel) and
    `UltraNet_TlsWrap` for `wss://` (OpenSSL, Schannel, SecureTransport), so
    macOS keeps the system's TLS and certificates and the HTTP side is
    untouched. `UltraNet_WebSocketConnect` uses it where libcurl has no
    WebSockets, and with `ULTRANET_WEBSOCKET_CLIENT=own` in the environment
    anywhere. The public API is unchanged and behaves the same on both:
    handles, callbacks, PINGs answered, `pingIntervalMs`, `maxMessageBytes`
    (a frame over it is refused by its header, before any of it is kept).
  - **What differs**, for `wss://` only: the own client verifies the
    certificate as `UltraNet_TlsWrap` does (the system's roots,
    `UltraNet_TlsSetCABundle`, `UltraNet_TlsAddTrustedCert`), libcurl's
    against libcurl's roots; and a proxy reached over TLS (`Https`) is one
    only libcurl's client goes through. The own client offers no extension
    (no `permessage-deflate`) and follows no redirect in the upgrade's
    answer.
  - **Tests.** `test_websocket_own.cpp`, 13 tests of the client by its
    internal class, on every system: the address, the accept key (the RFC's
    example), SHA-1, the upgrade as asked and every answer that is no
    upgrade, fragments with a ping between them, lengths of every encoding
    both ways, frames that are not the protocol, a frame over the allowance,
    idle / partial / closed, `wss://` - a certificate nobody vouches for
    refused, one added as trusted taken - and the proxy setting (an HTTP
    proxy gone through as a tunnel, this machine reached without it),
    against a scripted server. Three more through `UltraNet_WebSocket*` hold
    for either client (a message from fragments; `wss://`; the proxy
    setting). CTest's new `UltraNetOwnWebSocket` runs all 16 `ws_` tests on
    the own client on every platform. Seven deliberate breaks of the client
    each fail them. Also run on it here: `XmppProviderTests` (342 checks),
    `MattermostProviderTests` (294), `MastodonProviderTests` (154) and
    `DirectProviderTests` end to end against the real relay (1158).
  - **No more silent skips.** A WebSocket test with no connection to its own
    server fails instead of skipping; CI's "Run UltraNet tests" sets
    `ULTRANET_TEST_NO_SKIP` so the WebSocket tests run or the step fails
    (`test_framework.h`: that switch, and `ULTRANET_TEST_FILTER`). The
    chat-network step asks the Apple silicon row for `XmppProviderTests` and
    `MattermostProviderTests` again, without the check for a libcurl with
    no `ws` that #1337 put there; the build's status line says which client
    is used instead of "OK".
