- **IMAP: one sign-in serves many checks.** Every call into the IMAP plug-in
  signed in to the server anew: a mail check - the folder's status, the list
  of new messages, the read flags, the bodies - came to four sign-ins, twelve
  a minute per account at a check every twenty seconds, which some providers
  limit or block. The plug-in now keeps its signed-in sessions in a pool
  (`SessionPool`), keyed by server, user, credentials and TLS settings: up to
  two a key and sixteen in all, each kept for up to 330 s unused. A call takes
  one, runs on it and gives it back (`WithSession`); one that has been idle
  for over 15 s is first asked `NOOP`, and one the server closed (`* BYE`, an
  error, a time-out) is dropped and a new one opened. `ListFolders`,
  `GetMailboxStatus`, `FetchEnvelopes`, `FetchEnvelopesByUid`, `FetchMessage`,
  `FetchMessageBodies`, `FetchAllFlags`, `StoreFlags`, `ExpungeMessage` and
  `MoveMessage` run on a pooled session, each with the URL-based way as the
  fallback when libcurl will not open one. `STATUS` is not trusted on the
  folder a session has open (RFC 3501 6.3.10), so the session leaves it first
  - `UNSELECT`, or a failed `EXAMINE` where the server lacks it, neither of
  which expunges as `CLOSE` would. `Shutdown` closes the pool. Measured
  against Dovecot with a check every twenty seconds: no sign-in at all over
  65 s of checks after the first (four a check before), a newly delivered
  message picked up within one check, and a message marked read on the
  server, without signing in. `ImapResponse::AsLine` hands a response with
  its literals to the line parsers (a folder name the server sends as a
  literal), and a quoted folder name in a `LIST` response is now unescaped
  (`\"`, `\\`). Tests: `test_imap_mailbox.cpp`.
