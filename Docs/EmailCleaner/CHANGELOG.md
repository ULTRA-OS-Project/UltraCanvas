#### 2026-08-31 *0.3.0*
- **Your verdict beats the classifier's.** **This is fine** and **This is spam**
  on the actions strip record what you say about the selected sender or domain,
  and that decides their mail from then on. Two properties make it safe to use:
  the classifier's own verdict is kept alongside in the store, so *taking a
  correction back restores what it actually said*, message by message, rather
  than leaving a hand-set value with nothing to return to; and an address
  correction beats a domain one, so "all of this domain is spam except this one
  address" is sayable. **Corrected senders…** lists them and undoes them.
- Deliberately **not** the same control as Block. "I do not want to hear from
  them" and "your verdict about them is wrong" are different statements about a
  sender, and either can be true without the other — so they are separate
  tables, separate lists and separate buttons.
- **The rules are editable where they are used.** **Rules…** opens `rules.txt`
  in a dialog: your own rules listed, added and removed, seeded with the
  strongest term behind the current selection, then saved and re-analysed in one
  step. The 127 built-ins stay out of reach on purpose — they are the floor your
  rules are layered on, and a rule set you can break is one you can also
  silently disarm. A phrase typed here goes through the same parse and the same
  normalisation as a hand-edited line, so the two cannot mean different things.
- **Attachments can be looked at — except the ones that matter.** A message with
  attachments carries an **Attachments** button; the index holds metadata only,
  so the bytes are read back out of the .eml UltraMail cached and opened in
  `UltraCanvasMediaViewer`. Executable, script and macro-bearing attachments are
  **not** opened and **not** copied anywhere: nothing is written to disk and no
  button is offered, only a note saying why. An app whose subject is unwanted
  mail — one that classifies partly *on* those types — must not be the thing
  that opens them. The refusal is checked twice, against the index row and
  against the part the message really carries, so neither a stale index nor a
  misleading filename gets one through.
- Schema v3: the override table, plus `base_category` / `base_score` on messages
  so a correction is reversible. `AnalysisStore::kSchemaVersion` is now one
  constant the migration list is checked against, so a forgotten bump fails at
  `Open()` instead of silently.
- The unwanted-category SQL list is built from the taxonomy instead of written
  out in three queries, where adding a category would have silently missed one.
- Engine tests: **181** (was 155), covering the override table, what it does to
  the corpus, that removing one restores the classifier's verdict, and the
  attachment refusals — including a stale "harmless" index row and a name that
  does not match the part.

#### 2026-08-31 *0.2.0*
- **EmailCleaner can now act on the block you select, not just describe it.**
  An actions strip above the message list offers **Block sender**,
  **Unsubscribe** and **Move to Trash** for the selected sender or domain, in
  any combination. The split that makes destructive work reviewable is in the
  engine: `ActionPlanner` is pure and says exactly what *would* happen — which
  messages, from which folders, whether an unsubscribe offer exists and whether
  it is worth taking, and what to warn about — and the strip shows that plan
  continuously as the tick boxes change, so the consequence is on screen before
  the button is pressed. **Apply** repeats the plan and every warning in a
  confirmation; `ActionExecutor` then runs it, block first (local, cannot fail
  outward), then the unsubscribe, then the moves.
- **Unsubscribing from spam is refused, not offered** (`EmailCleanerUnsubscribe`).
  An unsubscribe link works for bulk mail you opted into; in a spam, phishing or
  dating-scam message it is a *liveness probe*, and taking it confirms a human
  reads the address. So for every unwanted family the advice is refusal —
  **whether or not the offer is well formed**: a valid RFC 8058 one-click link
  in a phishing message is more dangerous, not less. Where the offer is genuine,
  one-click (`POST List-Unsubscribe=One-Click`, https only, no redirects) is
  preferred, then a `mailto:` the app can send unattended; a bare link is
  reported for the user to open, never followed. RFC 2369 parsing tolerates
  missing brackets, odd spacing and commas inside a `?subject=`.
- **Deleting moves to Trash, and the Trash folder is resolved rather than
  assumed** (`EmailCleanerMailBackend`, over UltraNet's `IMailboxProtocolPlugin`
  and its UID MOVE): the IMAP SPECIAL-USE role first, then the names servers
  actually use (`Trash`, `[Gmail]/Bin`, `Deleted Items`, `Papierkorb`,
  `Corbeille`, `Papelera`, `Cestino`, ...), matched on the leaf under any
  delimiter. If none can be identified the move is **refused** — a wrong guess
  scatters mail into a folder nobody looks in. Resolution happens once per
  account, not once per message. `unwantedOnly` is on by default for a domain
  target, and any message the analysis calls wanted is counted in a warning
  before the confirmation.
- **The blocklist is local, visible and reversible.** Schema v2 adds the
  unsubscribe offer columns and a `blocklist` table; blocking changes what the
  map shows and what the counts say and never touches the server, and every
  entry can be seen and taken back from **Blocked senders…**.
- **EmailCleaner engine tests: 155** (was 104), adding the unsubscribe parsing
  and judgement, the planner and executor against a recording backend, and the
  mail backend against a fake `IMailboxProtocolPlugin` — still no display and
  no network.
- Depends on **UltraCanvasTreeMapElement 1.1.0** (UltraCanvas 0.3.88), which
  made the treemap respond to clicks at all — until then the sender map drew
  correctly but could not be selected, so nothing could be acted on.

#### 2026-08-30 *0.1.0*
- **New application: EmailCleaner** (`Apps/EmailCleaner`, target `EmailCleaner`,
  `BUILD_EMAILCLEANER`). It loads several mail accounts into an **analysis
  database** and shows the *shape* of a mailbox rather than a list of messages:
  a **map view** where every sender is a block sized by how much of the mailbox
  it accounts for and coloured by what it mostly sends, a **timetable** of when
  each sender writes (weekday x hour, plus traffic over calendar time), and a
  **detail view** with the keyword evidence behind every verdict. Concept:
  [`Docs/EmailCleaner/Concept.md`](Concept.md).
- **It does not fetch mail — UltraMail does.** UltraMail already owns accounts,
  auto-discovery, the credential vault and the IMAP sync engine, and caches
  every body at `<data>/mail/<account>/<folder>/<uid>.eml`. EmailCleaner reads
  that cache and mirrors the account list into its own database; it never
  writes to UltraMail's tables. `EMAILCLEANER_MAIL_DIR` points it at another
  mailbox.
- **Content detection** for the families that fill a mailbox: product
  advertising, adult content, dating/romance scams, phishing, financial fraud,
  and messages carrying executable, script or macro-bearing attachments (double
  extensions like `invoice.pdf.exe` included), plus newsletters and
  transactional notifications. Two kinds of evidence are combined and both
  recorded, so the detail view can *explain* a verdict: a weighted keyword rule
  set, and structural signals a keyword list cannot see — a display name hiding
  a different address, a `Reply-To` on another domain, a subject in capitals, a
  machine-generated sender address, the bulk headers.
- **Both a rule term and the message text run through one normalisation
  pipeline** (`EmailCleanerText`), which is what makes a short term list hold up
  against real spam: `V1AGRA`, `v.i.a.g.r.a` and `<b>vi</b>agra` all normalise
  to `viagra`, and a rule written `no-reply@` still matches after `@` folds to
  `a`. Inline HTML elements are removed *without* a word boundary (that is the
  camouflage); block-level ones become one.
- **Rules are data, not code.** The built-in table is layered under an editable
  `rules.txt` in the app's data directory (`category | weight | field | phrase`,
  `*` for no word boundary); a bad line is reported and skipped rather than
  costing the file. **Re-analyse** re-reads it and re-classifies the stored
  corpus.
- **The analysis database** (`EmailCleanerStore`, on UltraDatabase) keeps
  messages, attachment metadata, keyword hits and per-folder ingest state, and
  answers the aggregate shapes the views need — sender and domain rollups with
  their dominant category, the weekday x hour grid, the timeline with empty
  buckets filled in, category and attachment-type totals, top keywords — so the
  UI runs no SQL of its own. Attachments and hits are derived data, replaced
  wholesale on re-analysis; a batch load is one transaction. Time bucketing is
  UTC, so a timetable does not shift with the reader's timezone.
- **Headless engine test suite** (`ULTRACANVAS_BUILD_EMAILCLEANER_TESTS=ON`,
  target `EmailCleanerEngineTests`, 104 tests) covering the text pipeline, the
  rule format, the classifier, the store, the ingest over real RFC 5322
  messages and the analytics shaping — no display and no network.

<!--
EmailCleaner keeps its own version from 0.1.0 onward. Both entries above were
first published in Docs/UltraCanvas/CHANGELOG.md as parts of framework releases
0.3.87 and 0.3.88 and were moved here verbatim when the app changelogs were
split out; those framework entries now cross-reference this file rather than
describing the same work a second time.
-->
