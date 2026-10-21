- **The UltraNet JMAP plug-in pushes (phase 3), and the mail plug-ins share
  one message builder.** `IMailboxProtocolPlugin::WatchMailbox` opens a
  watch on an account and returns its handle; the plug-in then reports a
  change (`UltraNetMailChange`: which folders when known, and whether the
  folder list changed) on its own thread each time the server announces
  one, and `onEnd` once the watch is over - the stream closed, the
  connection dropped, or `StopWatching` ended it (Cancelled); the caller
  opens the next watch when it wants one. The JMAP plug-in holds the
  session's event source open (`UltraNet_SseStreamAsync`, `Email,Mailbox`,
  a ping every five minutes, a stall limit of two missed pings) and reports
  a `StateChange` whose Email or Mailbox state it has not seen - the first
  event only records where the account stands. The defaults report "not
  implemented", so the IMAP plug-in and test fakes are simply polled.
  `UltraNet_MimeBuildMailMessage` (UltraNetMime.h) builds the raw RFC 5322
  message of a `UltraNetMailMessage` - media type and charset from
  `contentType`, the text alternative and inline pictures of an HTML body,
  the attachments, the extra headers - and the SMTP and JMAP plug-ins both
  send through it instead of a copy each. The plug-in host table is ABI 3:
  `MimeBuildMailMessage`, `SseStreamAsync`, `CancelRequest` and
  `IsRequestActive` appended; the shim refuses an older host, since every
  plug-in in the tree calls them.
