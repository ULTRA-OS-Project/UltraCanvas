- **ChatCube Direct, Mattermost and XMPP over WebSocket connect on macOS 14
  and 15.** Their connections are WebSockets, and on those systems there
  was none to be had: the WebSocket client was libcurl's, and Apple's own
  libcurl lists no WebSockets through macOS 15. Mattermost signed in and
  then received nothing, XMPP's WebSocket binding failed its sign-in, and
  ChatCube Direct - whose relay connection is a WebSocket - could not reach
  its relay. UltraNet now brings a WebSocket client of its own for such a
  system, on the Mac's own TLS and certificates; nothing changes where
  libcurl has WebSockets.
