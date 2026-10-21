- **The UltraNet JMAP plug-in writes as well as reads (phase 2).**
  `StoreFlags` patches the message's keywords (`$seen`, `$answered`,
  `$flagged`, `$draft`); `\Deleted`, which JMAP has not, moves the message
  to the Trash mailbox, or destroys it when it is there already or the
  account has none, and clearing it is a no-op. `MoveMessage` replaces the
  message's `mailboxIds`, `ExpungeMessage` destroys it, `AppendMessage`
  uploads the raw message to the session's `uploadUrl` and imports it into
  the folder with its keywords (`Email/import`), and `FetchAllFlags` walks
  the folder and reads every message's keywords, so a client's reconcile
  step sees reads and deletions made elsewhere. `SendMail` sends through
  JMAP: the raw message (built with `UltraNet_MimeBuild`, as the SMTP
  plug-in does) is uploaded, imported into Drafts and handed to
  `EmailSubmission/set` with the `Identity` whose address is the From (a
  domain wildcard identity, else the account's first); on success the
  server files it in Sent with `$draft` cleared, or destroys it when there
  is no Sent mailbox. The `jmap://` server URL comes in
  `UltraNetMailOptions::serverUrl`, as for SMTP. The wire shapes live in
  `JmapCore.h` and are tested without a server
  (`Tests/UltraNet/test_jmap_plugin.cpp`); push over the session's event
  source (phase 3) remains.
