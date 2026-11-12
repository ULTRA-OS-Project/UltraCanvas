- **Apps can open WebSockets (element ABI version 3, `uc_ws_*`).** A
  WebAssembly app opens `ws:` / `wss:` sockets through UltraNet, with TLS
  verified, and sends and receives text or bytes. The rules are a
  browser's: the request carries the app's origin in an `Origin` header
  and the server decides; no cookies are sent; an https app opens `wss:`
  only. `UC_EVENT_WS` says when a socket opens, closes or fails, or when a
  message arrives while none was waiting. The app then reads with
  `uc_ws_receive` until none is left. Limits: 8 sockets, 1 MB a message,
  4 MB unread (`host/UltraWebSocket.cpp`, `UltraWebNetworkSocket.cpp`).
- **Apps can connect peer to peer (`uc_rtc_*`).** WebRTC peer connections
  and data channels run through UltraRTC, DTLS-encrypted. The app does its
  own signalling: it sends the descriptions and candidates it polls to the
  other side, and hands back what arrives. Negotiation is automatic.
  An app from the network connects through TURN relays only, so the other
  side never learns this machine's local addresses. Limits: 4 peers, 16
  channels a peer, 256 KB a message (`host/UltraWebRtc.cpp`,
  `UltraWebNetworkRtc.cpp`).
- Closing a tab closes its app's sockets ("going away") and hangs up its
  peers. In the browser runtime both answer `UC_ERR_DENIED` for now.
  `Docs/UltraWeb/UltraWebRTCIntegration.md` marks steps W1 and W2 done
  (`Tests/UltraWebRealtimeTest.cpp`).
