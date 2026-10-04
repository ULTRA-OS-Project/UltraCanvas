- **IMAP: numbers above 2147483647 are read right on Windows.** The IMAP
  parsers read UIDs, UIDVALIDITY and UIDNEXT with `strtol`, whose `long` is 32
  bits on Windows: every value above 2147483647 read as 2147483647 there. A
  mailbox's UIDVALIDITY is how a client notices that the server renumbered it,
  so a renumbering went unnoticed on Windows only, and new mail numbered below
  the old highest UID was never fetched. They are read as unsigned 32-bit
  values on every platform now (`ParseImapNumber`, `ImapParse.h`), a STATUS
  reply is read after its item list's `(` so a mailbox named "Recent messages"
  cannot hide the counts, and a SEARCH value too large for a UID is no longer
  taken for another one. Tests: `test_imap_mailbox.cpp`.
- **IMAP: `IMailboxProtocolPlugin::FetchEnvelopesByUid`** - the envelopes of
  the messages named, for mail an incremental fetch ("UID > the highest held")
  can no longer reach: one an interrupted sync skipped, one whose header could
  not be read the first time. Added last with a default (FetchEnvelopes from
  the lowest UID asked for, keeping the ones asked for), so the existing vtable
  is undisturbed and the JMAP plug-in and test fakes serve it unchanged; the
  IMAP plug-in fetches exactly those UIDs over one connection.
- **IMAP plug-in: no empty envelopes.** A message whose header fetch failed
  (or came back empty) was still handed to the caller - with no sender,
  subject, date or Message-ID - and UltraMail stored it as a blank row it never
  asked for again. Such a message is now left out; the caller asks again.
  `FetchEnvelopes` also drops the server's echo of the highest UID: "UID n:*"
  always matches it, even below n (RFC 3501 6.4.8), so the newest message was
  fetched again - body and all - on every sync.
- **`UltraCanvasMultiColumnListModel::SetItems`**: every row at once, with one
  change notification. `AddItem` notifies the view per row (row geometry,
  scrollbar, redraw), which for a list of thousands - a mailbox - was most of
  the time it took to fill; and a re-sort is a new order of the same rows.
  Documented in `UltraCanvasListViewExamples.md`.
- **ListView: EnsureRowVisible before the first layout.** Called while the
  view had no height yet, it measured against a zero - less the header,
  negative - viewport and scrolled the row below the top: a list filled and
  selected while its window was being built opened two rows down, the selected
  row hidden above it (UltraMail's message list did, at every start). The row
  is now remembered and revealed once the view has its size. Tests (with
  `SetItems`): `ListViewScrollTest.cpp`.
