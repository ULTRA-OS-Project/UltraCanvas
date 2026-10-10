- **Mailbox plug-ins can make and delete folders.**
  `IMailboxProtocolPlugin::CreateFolder` and `DeleteFolder` take a
  folder's full name in its wire form, with the server's separator between
  its levels. The IMAP plug-in sends `CREATE` and `DELETE`, on a kept
  session when one is open and on a connection of its own otherwise. Before
  `DELETE`, a kept session that has that mailbox open opens INBOX
  (read-only) instead, so the server is not asked to delete the mailbox in
  use. `DeleteFolder` refuses INBOX itself (`AccessDenied`). Both methods
  are added at the end of the interface, and their defaults report "not
  implemented", so existing plug-ins and test fakes still build unchanged.
  `CreateMailboxCommand` / `DeleteMailboxCommand` in `ImapParse.h` quote the
  name and leave out a line break, which would end the command early.
  Tests: `test_imap_mailbox.cpp`.
- **`UltraNet_ImapUtf7Encode`: a typed mailbox name for the wire.** This is
  the counterpart of `UltraNet_ImapUtf7Decode`: UTF-8 in, IMAP's modified
  UTF-7 out (RFC 3501 5.1.3). Printable ASCII passes through, `&` becomes
  `&-`, and each run of other characters, surrogate pairs included, becomes
  one `&…-` shift ("Bücher" -> `B&APw-cher`). A byte that is not UTF-8 is
  taken as U+FFFD, so the result is always a valid name, and
  `UltraNet_ImapUtf7Decode` reads back every name it writes.
