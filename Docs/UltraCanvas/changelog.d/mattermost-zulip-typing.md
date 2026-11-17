- **UltraIM: typing on Mattermost and Zulip.** Both providers now have the
  `TypingIndicator` capability, both ways. Zulip sends through
  `/api/v1/typing` (direct messages; stream topics on Zulip 8.0+) and hears
  typing on its event queue, repeating it every `typingRefreshMs` (4 s) until
  the stop, the typist's message or `typingExpireMs` (15 s), since Zulip's
  own clients say it only every ten seconds. Mattermost sends through
  `/api/v4/users/me/typing` and hears typing on a WebSocket opened for that
  alone (chained into UltraNet's process-wide WebSocket callbacks as the XMPP
  stream is), sending the stop itself after `typingStopMs` (5 s), the
  typist's post or a dropped socket. Also fixed in Zulip: a message deleted
  before the event queue echoed it came back and was deleted twice. Tested
  in `ZulipProviderTests` and `MattermostProviderTests` (the stand-in
  Mattermost server now has a WebSocket).
