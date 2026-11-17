- **UltraIM XMPP: a silent server is found over TCP.** After
  `XmppTimings::pingIntervalMs` with nothing from the server, the TCP stream
  sends an XMPP ping (XEP-0199); no answer - nor anything else - within the
  new `XmppTimings::pingTimeoutMs` (30 s) ends the stream, and the provider
  reconnects as after any drop. The whitespace keepalive alone found a
  connection that was gone only when a write failed, which a server that
  hangs, or a path that drops packets silently, never makes happen. The
  pings' answers stay inside the stream. Tested in `XmppProviderTests`: a
  quiet server answering keeps the connection; one made silent with its
  socket open (`/_test/silence` in `xmpp_fake_server.py`) is found and
  reconnected to.
