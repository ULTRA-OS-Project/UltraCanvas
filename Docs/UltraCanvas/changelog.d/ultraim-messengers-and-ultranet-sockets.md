- **UltraIM: Mattermost and Zulip providers** (`Plugins/UltraIM/mattermost`,
  `Plugins/UltraIM/zulip`; modules `ultraim_mattermost`, `ultraim_zulip`).
  Mattermost over its REST API v4: password with an MFA code
  (`UltraIMAuthStep::Code`) or a personal access token, direct and group
  messages and team channels, polled, thread replies, files, edits,
  deletes, read marks. Zulip over its REST API: password or API key, direct
  and group messages and stream topics, news from the event queue, uploads,
  edits, deletes, read flags. Both keep their session in UltraVault, answer
  `FetchAccountProfile` with the account's name, and are tested against a
  stand-in server in standard-library python (`MattermostProviderTests`,
  `ZulipProviderTests`). No plug-in ABI change.
- **UltraIM: Signal, XMPP and IRC providers** (`Plugins/UltraIM/signal`,
  `Plugins/UltraIM/xmpp`, `Plugins/UltraIM/irc`; modules `ultraim_signal`,
  `ultraim_xmpp`, `ultraim_irc`). Signal through a signal-cli daemon's
  JSON-RPC (the user's, or one the provider starts), linked to the phone by
  a QR code (`UltraIMLoginType::QRCode`, step `OtherDevice`). XMPP over TCP
  (STARTTLS on 5222, direct TLS on 5223 - XEP-0368 - by `UltraNet_TlsWrap`;
  the server found by SRV, then host-meta, then `<domain>:5222`, the endpoint
  that connected kept) or the WebSocket binding (RFC 7395), with tinyxml2:
  SCRAM-SHA-256/1 keeping the derived keys rather than the password, roster, MUC rooms from bookmarks,
  MAM history, receipts, markers, chat states, corrections, retractions,
  replies, carbons, HTTP File Upload. IRC with IRCv3 over UltraNet TCP and
  `UltraNet_TlsWrap`: SASL PLAIN or NickServ, CHATHISTORY, server-time,
  msgid, echo-message, labeled-response, typing, replies, a `*server`
  conversation for notices and raw commands, reconnecting with backoff.
  Each tested against a stand-in in standard-library python
  (`SignalProviderTests`, `XmppProviderTests`, `IrcProviderTests`). No
  plug-in ABI change.
- **UltraNet sockets: no SIGPIPE, and a timeout told from a broken
  connection.** `UltraNet_TcpSend` no longer raises SIGPIPE on a connection
  the peer closed - which ended the program - but fails with
  `ConnectionReset`: sends pass `MSG_NOSIGNAL`, sockets carry `SO_NOSIGPIPE`
  where there is no such flag (macOS), and OpenSSL's own writes (handshake,
  write, close-notify) hold SIGPIPE back on their thread. `UltraNet_TcpReceive`
  now answers the same three ways on every backend, plain and through
  `UltraNet_TlsWrap`: bytes; `Ok` with no bytes when the peer closed;
  `Timeout` when nothing came in time (the connection is still there) - before,
  OpenSSL and plain sockets answered a timeout like a broken connection
  (`ReceiveFailed`) and Schannel and Secure Transport like a closed one (no
  bytes), so callers timed their reads to tell them apart. A broken one is
  `ConnectionReset` or `ReceiveFailed`; `UltraNet_UdpReceive` answers
  `Timeout` too. The IRC provider and ChatCube Direct's LAN link go by the
  code now. Tests: `test_socket_errors.cpp` in `UltraNetTests` (plain TCP,
  UDP, and TLS against a python3 ssl server, with SIGPIPE at its default
  action).
- **`UltraNetTls.h`: header comment corrected.** It still described TLS on
  a TCP handle as Stage-3 stubs deferred to UltraNet v1.1; `UltraNet_TlsWrap`
  and `UltraNet_TlsHandshake` have long been implemented (OpenSSL, Schannel,
  SecureTransport). The comment now says so and when to use it. No code
  change.
