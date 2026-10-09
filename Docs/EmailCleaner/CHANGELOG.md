#### 2026-10-09 *0.4.6*
- **New app icon.** The trash can keeps its red @ and gains ribs down its side
  (`media/appicon/EmailCleaner.svg`, the uploaded artwork).
  `media/appicon/EmailCleaner.png` is re-rendered from it at 256 px through
  librsvg, padded to a square on a transparent background as before. The
  window and taskbar icon, the Windows `.exe` icon and the `hicolor` theme
  icons are all made from this pair, so every one of them changes with it.

#### 2026-10-08 *0.4.5*
- **Rules match HTML mail through the framework's HTML reader.**
  EmailCleaner's own tag stripper knew eleven entities; the rest - `&eacute;`,
  `&#8364;`, `&rsquo;` - stayed as written, so a rule term spelled with the
  character never matched a message that wrote the entity. Message text now
  goes through `HTML::ExtractPlainText`: every entity is decoded, `<style>`
  and `<script>` contents are left out, and a word split by formatting
  (`<b>via</b>gra`) is still one word, as before. En and em dashes fold to
  `-` and the ellipsis to `...` on both sides of a match, so terms written
  either way still match. `Classifier::StripHtml` and `EmailCleaner::StripHtml`
  are gone.

#### 2026-10-04 *0.4.4*
- **When a credential vault will not open, the window says why.** For
  UltraMail's vault the reason was guessed from whether the vault file
  exists, so a build without an encryption library - which has no vault file
  either - was told "UltraMail has no credential vault yet - set the account
  up in UltraMail first". It now says what is wrong: "UltraMail's credential
  vault cannot be opened: this build has no encryption library (UltraCrypt
  was built without libsodium), so it cannot keep passwords", a folder that
  cannot be written, or (as before) a vault locked with a master password.
  EmailCleaner's own vault, which was skipped in silence - the window then
  said no account had a saved password - reports its reason the same way, and
  so does adding an account whose password could not be stored (framework
  changelog: `DeviceKeyVault::GetLastUnlockStatus` / `DescribeUnlockStatus`).

#### 2026-10-04 *0.4.3*
- **A server name that cannot be one is caught before the sign-in is tried.**
  Adding an account with `mail@example.com` as the incoming server (the
  address's @ where the name has a dot) used to wait out the sign-in timeout
  and then report that the server did not answer. The account form now says
  what is wrong, with the likely fix: *Incoming (IMAP) server
  "mail@example.com": A server name has no @ - did you mean mail.example.com?*
  It also catches a URL scheme (`imaps://`), a port after a colon, a path,
  spaces inside the name and characters no host name holds. Spaces around a
  correct name are simply dropped. This is UltraMail's own check
  (`UltraMail::ServerNameProblem`, UltraMail 0.10.25), applied in
  `OwnAccounts::Validate`. Test: `test_accounts.cpp`
  (`Accounts_ValidateCatchesAServerNameThatCannotBeOne`).

#### 2026-10-02 *0.4.2*
- **Mail in folders and accounts with non-English names is read on
  Windows.** Scanning the mail cache and opening a message for its
  attachments joined the account and folder onto the path as plain strings,
  which Windows converts in its ANSI code page, so a folder such as
  "Entwürfe" was looked for under a mangled name and its mail was left out
  of the analysis. Both now pass every part through `PathFromUtf8`. Found
  by EmailCleaner's engine tests, which now run on Windows CI too.

#### 2026-09-29 *0.4.1*
- **UltraMail accounts that signed in through the browser can be acted on.**
  An account set up in UltraMail with its provider's browser login (OAuth2 -
  Gmail, Outlook, Yahoo) keeps a token set in UltraMail's vault, not a
  password, and EmailCleaner only ever looked for a password: such an account
  was analysed but never registered with the mail backend, so **Move to
  Trash** and the unsubscribe mail refused its messages. EmailCleaner now reads
  the token set too and, before every call to the server, signs in with a
  current access token (XOAUTH2), renewing it through the provider when it has
  expired - with UltraMail's own OAuth client, whose refresh token it is
  (environment, baked-in client, and now UltraMail's `oauth.ini` as well). The
  renewed token stays in memory: UltraMail's vault is only ever read. When the
  sign-in cannot be renewed the action fails before anything reaches the
  server, and says to sign in again in UltraMail.
- **Acting on mail no longer freezes the window.** **Apply** ran every step on
  the UI thread: the unsubscribe request, a sign-in (and now possibly a token
  refresh), and one IMAP round trip per message moved to Trash - so moving a
  few hundred messages left the window unresponsive until the last one was
  done. The block, which is local and instant, still happens at once; the
  steps that talk to a server run on a worker, the status line counts the
  messages as they move ("Moving to Trash… 40 of 212"), and the outcome and
  any warning appear when they are done. **Apply** stays disabled until then,
  so a second plan cannot start against messages that are still moving. The
  mail backend is now safe to use from that worker while an account is added
  or registered again on the UI thread (the account records were an unguarded
  map; ThreadSanitizer reported the race and is clean now).
- **Mail moved to Trash leaves the map.** A message **Move to Trash** moved
  stayed in the analysis - the map, the counts, the message list - until the
  account was scanned again, and even that did not help: the body is still in
  the mail cache it was read from (UltraMail kept every cached `.eml` for
  good until its 0.10.15, and removes one only at its next sync of the folder
  now), so the scan analysed it again. Now the
  moved messages are taken out of the analysis as soon as the moves come
  back, and the move is remembered (schema 5, `moved_messages`), so neither
  **Load mail** nor **Re-analyse** brings them back. The record keeps the
  Message-ID, so a UID the server hands to a different message after a
  UIDVALIDITY reset is analysed again. And Trash folders are no longer
  analysed at all - found by the server's folder role where UltraMail or
  EmailCleaner knows it, and by name (`Trash`, `[Gmail]/Bin`, `Deleted Items`,
  `Papierkorb`, ...) - so what was moved does not come back from there either;
  rows an earlier scan stored for a Trash folder are removed.
- **Opened attachments no longer pile up.** Opening an attachment writes a
  copy into `<data dir>/attachments` for the viewer, and none was ever
  deleted. The folder is now pruned at every start, before any viewer has a
  file open: what was not opened for a week goes, then the oldest until the
  rest fits in 256 MB (`PruneAttachmentCache`, on UltraMail's
  `AttachmentCache::Prune`, the rule UltraMail applies to its own copies).
  Opening an attachment again makes its copy new.
- **Two attachments with the same name no longer overwrite each other.**
  The viewer's copy was written under the attachment's name, truncating
  whatever was there: opening a second "invoice.pdf" from another sender
  replaced the first - even while it was still open in a viewer. The copies
  are now written through UltraMail's `AttachmentCache`: the same bytes reuse
  the copy already there, different bytes get "invoice (1).pdf".

#### 2026-09-29 *0.4.0*
- **EmailCleaner can have accounts of its own.** Until now every account came
  from UltraMail, so a mailbox could only be cleaned after it had been set up
  in a mail client. **Accounts…** in the toolbar lists every account with where
  its mail comes from, and adds one to EmailCleaner alone: the address (**Find
  servers** fills the IMAP server in from UltraMail's provider table, then
  autoconfig), a password — an app password at Gmail, Outlook and Yahoo — and
  **Sign in and add**, which checks the sign-in against the server before
  anything is saved. The account keeps its own list (`accounts.db`), its own
  password (`vault/emailcleaner.vault`) and its own downloaded copy of the
  inbox and junk folder (`mail/ec-<account>/`) under EmailCleaner's data
  directory, fetched by UltraMail's own `SyncEngine` — one IMAP implementation,
  two places its results are kept. **Load mail** downloads what is new for
  these accounts before analysing, and **Remove** deletes the password, the
  copy and the analysis without touching the server. UltraMail's accounts are
  shared exactly as before; an address UltraMail already shares cannot be
  added twice, and its id (`ec-…`) cannot collide with UltraMail's. The
  analysis database records each account's source (schema 4).
- **EmailCleaner loads UltraNet's plug-ins.** It asked the registry for the
  IMAP plug-in without ever initialising it, so the plug-in was never found and
  Block's companions — **Move to Trash** and the unsubscribe mail — always said
  "The IMAP plug-in is not loaded". It now brings the registry up at start-up
  from `Plugins/UltraNet` beside the executable (as UltraMail does), or from
  `EMAILCLEANER_PLUGIN_DIR`, and names the folder it looked in when the
  plug-in is missing.
- **UltraMail accounts use the servers UltraMail stored.** The mail backend
  looked each shared account's server up in the provider table only, so an
  account whose servers were found by autoconfig or typed in by hand in
  UltraMail could not be acted on. It now takes the account's stored servers,
  and the provider table only when there are none.

#### 2026-09-28 *0.3.3*
- **The version is in the window title** — `EmailCleaner 0.3.3` — so a screenshot or a
  bug report says which build it came from. The number is this changelog's
  first line, as everywhere else (`cmake/UltraCanvasVersion.cmake`).

#### 2026-09-22 *0.3.2*
- **EmailCleaner can read UltraMail's passwords again.** It opened UltraMail's
  credential vault and asked for each account's password without unlocking it
  first, so every lookup came back empty and the app always reported "No
  account has a server and a saved password". It now unlocks the vault with
  the device key UltraMail keeps beside it (framework 0.9.23,
  `UltraVault::DeviceKeyVault`), and when that is not possible it says which
  case it is: a vault still locked with a master password (open UltraMail once
  so it stores its device key), or no vault yet (set the account up in
  UltraMail first).

#### 2026-09-19 *0.3.1*
- **EmailCleaner has an icon.** The trash can with the red @
  (`media/appicon/EmailCleaner.svg`) is drawn everywhere the app is shown: the
  window and the taskbar entry that follows it (`SetDefaultWindowIcon` and the
  `UCAPP_ICON_PATH` fallback), the icon compiled into the Windows `.exe`, and
  the launcher in an application menu and in a filer. Until now the app set no
  icon at all, so every one of those surfaces wore the generic UltraCanvas or
  executable glyph. The PNG the fixed-size consumers read is rendered from the
  SVG (padded to a square on a transparent background; the drawing is
  243 x 257 units), so the scalable and the fixed-size icon agree at every
  size.
- **EmailCleaner has a desktop entry.** `Apps/EmailCleaner/EmailCleaner.desktop`
  is installed to `share/applications`, with the PNG and the SVG installed to
  `share/icons/hicolor/256x256/apps` and `share/icons/hicolor/scalable/apps`.
  `Icon=EmailCleaner` is an icon *name* resolved through the installed themes,
  and UltraFiler finds an application's icon by reading its desktop entry, so
  without one the new artwork would show in the window and nowhere else. The
  entry carries no MIME type and no field code: `main()` takes no arguments.

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
