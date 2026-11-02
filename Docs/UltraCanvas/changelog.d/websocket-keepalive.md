- **UltraNet WebSocket: an idle connection stays up, and a large message
  arrives whole.** Three faults in `UltraNetWebSocket.cpp`, found through
  ChatCube's relay connection dropping every half minute or so:
  - the server's PINGs went unanswered - libcurl 8.5 does not answer them in
    connect-only mode, whatever the comment said - so a server that pings and
    closes an unanswered connection (uvicorn: 20 s and 20 s) dropped every
    idle one; they are answered now, once each (`CURLWS_NOAUTOPONG` where
    libcurl has it);
  - `UltraNetWebSocketOptions::pingIntervalMs` was read by nothing: the client
    pings at it now, and a peer that sends nothing at all for two intervals is
    taken as gone (`onError` once, so the owner reconnects);
  - a frame larger than a read (8 KB) went to `onText` / `onBinary` one read
    at a time - libcurl's `bytesleft` was taken for "whole": a relay's 40 KB
    envelope arrived as five broken messages. Until now the drops hid it,
    since each reconnect fetched the queue over HTTP.
  Reads and writes of the one libcurl handle also share one mutex now (the
  receiver read while other threads sent).
- **`UltraNetWebSocketOptions`: every member does what it says.** Four did
  nothing. `maxFrameSize` ("0 = libcurl default (~64KB)" - there is none) is
  now `maxMessageBytes`: the largest incoming message, 64 MB unless set, 0
  for no limit; a larger one ends the connection (close 1009) before it is
  assembled, where a server could otherwise grow one without end.
  `autoReconnect`, `maxReconnectAttempts` and `reconnectDelayMs` are gone:
  reconnecting is the owner's, on `onClose` / `onError` (a handle is one
  connection), as ChatCube's relay client does. Nothing in the repository
  set any of the four. `test_websocket.cpp` covers each:
  a server pinging every half second, the client's pings, a silent peer,
  four threads sending while pinged, a 50 KB message, one past its limit.
