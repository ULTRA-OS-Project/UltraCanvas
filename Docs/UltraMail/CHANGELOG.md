#### 2026-10-04 *0.10.30*
- **Switching accounts is immediate.** A click on an account's tile shows its
  mail as stored at once - the list first, the message beside it a moment
  later, once the list is on screen - and then fetches its inbox from the
  server in the background; new mail joins the list in place when it comes.
  On a mailbox of 5000 messages the window used to stand still for 0.4 to 1
  second per switch (measured: 420-960 ms); it now takes 10-55 ms. What made
  it slow:
  - **The waiting-for-reply count.** Its "only people you have written to"
    rule compared every waiting message with every message in the Sent
    folder, in the database, on every count - half a second each time the
    account bar or the list was redrawn. The addresses written to are now
    read once and only again when the Sent mail changes: 470 ms became 15 ms.
    The rule also missed most of them: it matched only recipients written as
    a bare address, never "Maya Bennett <maya@example.com>", the way the
    composer and most mail programs address mail. Those count now.
  - A switch no longer re-counts every account and re-reads the address
    book: nothing either depends on changed by looking at another account.
  - The list fills in one step instead of row by row, and the message in the
    reading pane is laid out once instead of twice (also on every click on a
    message, and on every sync that brought mail above it - which also sent
    the reading pane back to the top of the message).
  - After a sync only the senders of the new mail are added to the address
    book, not every sender of the inbox again.
- **The message list sorts by its column headers.** A click on *From*, on the
  sender-badge column, on *Subject* or on *Date* orders the list by it; a
  second click turns the order round, and a small triangle in the header
  shows which column and which way. Dates start newest first, the rest from A
  (contacts first for the badge column). Subjects sort without their "Re:",
  "Fwd:", "AW:" or "WG:", so a reply stays with what it answers, and accented
  letters sort with their base letter ("Ärztekammer" among the A's). The
  message being read stays selected, new mail arriving during a sync goes in
  at its place in the order, and the choice is remembered (`list_sort` in
  `preferences.ini`).
- **New mail that never showed up.** Several ways a message could be left out
  of the list for good, all repaired by the next sync:
  - Every sync now compares the inbox with the server's own list of messages,
    not only "anything above the highest message number held". A message an
    interrupted sync skipped, or one stored blank because its header could
    not be read (earlier versions did that), is fetched; mail deleted, moved
    or read on another computer is followed in the background sync too (it
    used to be only when a folder was opened by hand).
  - A cache whose message numbers the server has not handed out yet is
    dropped and fetched again - the server renumbered the mailbox, or the
    account's server changed. Before, every new message numbered below the
    old highest number was skipped, without an error.
  - **Windows only:** a server's mailbox number (UIDVALIDITY) above 2147483647
    was read as 2147483647 on Windows, so a renumbered mailbox looked
    unchanged there - and only there (framework changelog, "IMAP: numbers above
    2147483647 are read right on Windows").
  - Changing an account's incoming server or user name in *Account Settings*
    drops the mail downloaded from the old one and fetches the new mailbox.
  - **"(message body not downloaded yet)" no longer stays.** A message whose
    body download failed was never downloaded again. Opening it now downloads
    it at once (the reading pane says so meanwhile), and each sync fetches the
    bodies still missing among the newest 100 messages.
  - The newest message was downloaded again on every sync (the server always
    answers "messages from number N on" with its newest one).
- **The connection pill says how many messages the server's inbox holds**
  ("Inbox on the server: 59 messages" in its tooltip). When another computer
  shows mail this one does not, this tells whether the mail is on the server
  this account reads at all: if the numbers differ, compare the *Incoming
  server* in *Account Settings* on both computers.
- The list opens with its newest message in view; it used to open scrolled two
  rows down, the selected message hidden above the top (framework changelog,
  "ListView: EnsureRowVisible before the first layout").
- **Folders as the server names them.** On servers that put every folder
  under the inbox with a dot - Courier-style, "INBOX.Drafts" - the folder tree
  showed "INBOX.Drafts", "INBOX.Trash" and "INBOX.INBOX^Sent" as names. The
  separator the server lists is now kept with each folder (schema 9) and the
  names are read by it: Inbox › Sent, Drafts, Trash, Investor, Invoice, as on
  any other server. The same names appear in the list's title, the status line
  and *Move to folder* ("Projects / 2026" for a folder two levels down).
  - **The Sent folder is found** on such servers: "INBOX^Sent" - how a folder
    came across from a server with another separator - is the Sent folder,
    and so are the German names servers use ("Gesendete Objekte",
    "Papierkorb", "Entwürfe" …). That also makes "Waiting for reply" work
    there: its "people you have written to" rule reads the Sent folder
    (framework changelog, "IMAP: folder roles by the server's own separator").
- **Folders deleted or renamed on the server leave the tree.** The folder list
  only ever added folders; one deleted on the server, or renamed there, stayed
  in the tree with its old mail for good. Every sync now drops a folder the
  server no longer lists, with its messages and downloaded bodies (never on an
  empty list, never the inbox). A folder that is open when it goes takes the
  view back to the inbox, and pressing *Update* on it says "The folder … is no
  longer on the server" instead of a "Select failed" alert.
- **A renumbered mailbox was never noticed in the regular sync.** Reading the
  folder list wrote 0 over each folder's stored UIDVALIDITY, so the inbox sync
  that followed found nothing to compare and kept a stale cache. The folder
  list now leaves the numbering alone.
- **The highlight stays on the message being read while new mail streams
  in.** Rows inserted above it moved the message down but not the highlight,
  which sat on whatever message took its place until the sync finished
  (framework changelog, "ListView: the selection follows the rows").

#### 2026-10-04 *0.10.29*
- **The message text fits its pane.** An HTML message tall enough to scroll
  was laid out for the pane's full width, and the vertical scrollbar then took
  its strip on top: the end of every line ran under the bar and the few
  hidden pixels raised a horizontal scrollbar across the bottom as well. The
  body is now laid out at the width beside the bar, so lines wrap before it and
  a horizontal bar appears only for content that really cannot wrap (a
  fixed-width table, a large picture).
- **No dotted line or slivers after scrolling.** Scrolling a message left a
  faint dotted yellow line just left of the text and slivers of glyphs below
  it: the edges of the letters, which scrolling never painted over. Fixed in
  the framework (framework changelog, "A scrolled view no longer leaves glyph
  fringes beside and below it").
- **Resizing the window keeps your place in the message.** It sent the
  message back to its top (framework changelog, "Resizing a window keeps the
  scroll position of everything in a split pane").
- **Thin, round scrollbars in the reading pane.** The message text (HTML and
  plain text) scrolls with the same thin, rounded scrollbar as the message
  list instead of the wide square one. Plain-text mail needs the framework's
  new text-area scrollbar style (framework changelog, "TextArea: the
  scrollbar's thickness and rounding are styleable").
#### 2026-10-04 *0.10.28*
- **Mail addresses in a message open a new message.** A `mailto:` link in
  formatted mail, and a `mailto:` or plain address ("support@shop.example")
  written in plain-text mail, opens a new message in UltraMail, from the
  account the message was read in, with the subject and text the link
  carries. Before, a `mailto:` link went to the system's mail program, and
  addresses in plain text did nothing.
  - **Copies too:** the link's `cc` and `bcc` fill the message's Cc and Bcc,
    and a `to` field adds recipients. A message with blind copies opens with
    its **Bcc** row shown, so no recipient is added unseen.
- **Bcc in the compose window.** A **Bcc** toggle at the end of the Cc row
  shows a Bcc row for blind copies the other recipients don't see. Hiding the
  row again empties it, so nothing goes to an address that is out of sight.
- **To, Cc and Bcc complete from the address book.** Typing in a recipient
  field pops up the contacts whose name, organization or address matches what
  is typed after the last comma - matches at the start of a word first. Down
  and Enter, or a click, puts in "Name <address>, " and keeps the recipients
  before it; addresses already in the field are not offered again. The
  people you write to most - counted from the mail in your Sent folders -
  come first, and recent mail counts more than old: a message's weight halves
  every 90 days, so someone you wrote to often years ago does not stay on top.
- **Plain-text mail: addresses are links.** They show the pointing hand, show
  their address in the status line or as a tooltip, and are counted with the
  message's links. They are never judged as web links by the threat scan.

#### 2026-10-04 *0.10.27*
- **The phishing scan knows the brands phishing pretends to be.** The
  known-sender registry grows from about 50 services to about 400, with 600 of
  their own domains: banks and brokers (Chase, Bank of America, Barclays, HSBC,
  Revolut, Deutsche Bank, Commerzbank, ING, N26, UBS, BNP Paribas, Nordea, RBC,
  Commonwealth Bank …), payment services (Venmo, Zelle, Cash App, Wise, Western
  Union, Klarna, Visa, Mastercard …), crypto exchanges and wallets (Coinbase,
  Binance, Kraken, Crypto.com, MetaMask, Ledger, Trezor …), online shops and
  marketplaces (Walmart, AliExpress, Temu, Zalando, Vinted, Kleinanzeigen,
  Lidl, Aldi …), parcel carriers and postal services (USPS, Royal Mail, Evri,
  DPD, GLS, InPost, PostNL …), cloud, hosting and file-sharing services
  (WeTransfer, DocuSign, Cloudflare, Hetzner, IONOS, OVHcloud …), domain
  registrars (GoDaddy, Namecheap, INWX, DENIC …), tax offices and agencies (IRS,
  HMRC, ELSTER, impots.gouv.fr, CRA, ATO …), telecoms, game stores and
  security software (Norton, McAfee, LastPass …). A display name or subject
  claiming one of them from another domain is flagged as impersonation; their
  genuine mail gets their name, icon and the business-contact badge. Eight new
  categories describe them, and the **Payments** filter now also shows mail
  from banks and crypto exchanges.
- **Fewer false impersonation warnings.** Brands whose name is an ordinary
  word ("Chase", "Target", "Visa", "Steam", "Booking", "UPS") are now claimed
  only by specific phrases ("chase bank", "booking.com"), so a hotel's "your
  booking is confirmed" or a subject with "follow-ups" no longer reads as
  impersonation; Amazon is no longer claimed by "prime" alone, nor Microsoft
  by "office" or "outlook". A display name that is just the sender's own
  mailbox address (`jane@outlook.com`) claims no brand.
- **A squatted country domain no longer passes for a big brand.** Amazon,
  eBay, Google, DHL, Etsy and Pinterest were trusted under any domain ending
  - `amazon.xyz` got Amazon's name, icon and badge. They now list their real
  country sites (`amazon.de`, `ebay.co.uk`, `dhl.de`, `google.co.jp`,
  `pinterest.de`, …) one by one, and no entry is trusted that way any more.
- Brand lookups use an index instead of walking the table for every message
  in a folder. See [SenderBadges.md](SenderBadges.md#2-the-known-sender-registry)
  for the rules an entry must follow.

#### 2026-10-04 *0.10.26*
- **Unread mail stays unread.** Syncing marked every new message read on the
  server, so the list had no unread mail to show in bold, the account's unread
  counts stayed at 0, and other mail programs saw the mail as read too. The
  IMAP plug-in now puts a message's unread state back after reading it (see
  the framework changelog, "IMAP plug-in: reading a message no longer marks it
  read on the server"). Mail marked read before this fix stays read on the
  server; *Mark as unread* brings a message back.
- **The account bar's counts follow what you read.** Opening an unread message
  lowered the list's unread count, but the account tile kept the old numbers
  until the next sync. It is now re-counted at once.
- **Settings > Reading > Waiting for reply: which unanswered mail counts.**
  Every personal message in the inbox that had never been answered counted as
  waiting for a reply, however old it was: 1083 on one account. The count, the
  list's reply mark and the *Needs an answer* filter now take only mail from
  the last 14 days (or 7, 30, or any age) and, by default, only from people you
  have written to, that is the recipients of your Sent mail. That rule is left
  out while the Sent folder has not been fetched. A message you mark *Needs an
  answer* yourself always counts. The rules are applied when the mail is
  counted (`LocalStore::SetNeedsAnswerRules`), so changing them needs no
  re-sync. Saved as `needs_answer_max_age_days` / `needs_answer_only_written_to`
  in `preferences.ini`. Tests: `test_localstore.cpp`, `test_preferences.cpp`.

#### 2026-10-04 *0.10.25*
- **A server name that cannot be one is caught before the sign-in is tried.**
  Typing `mail@interkontakt.net` for the outgoing server - the address's @
  where the name has a dot - waited out a ten-second timeout and then blamed
  the server for not answering. Save on the server settings page now checks
  both names first and says what is wrong, with the likely fix: *Outgoing
  (SMTP) server "mail@interkontakt.net": A server name has no @ - did you mean
  mail.interkontakt.net?* It also catches a URL scheme (`imaps://`), a port
  after a colon, a path, spaces, characters no host name holds and empty or
  over-long parts; one-word LAN names, IP addresses and international names
  still pass. The check is `ServerNameProblem` (`UltraMailDiscovery.h`); tests
  in `test_discovery.cpp`.
- The preferences reader includes `UltraCanvasPathUtf8.h` once instead of
  twice.

#### 2026-10-04 *0.10.24*
- **Settings > Display > Links: where a link's address is shown.** Two
  choices:
  - **Show in status bar** (the default): the status line counts the open
    message's links and names the sites they go to, its tooltip lists every
    link, and pointing at a link shows its address there.
  - **Show as tooltip**: the address of the link under the pointer - a text
    link or a linked picture - appears in a tooltip beside it that follows the
    pointer along the link, and the status line's links segment is hidden.
  The choice applies at once to the message on screen and is remembered
  (`link_display` in `preferences.ini`).
- **Web addresses in plain-text mail work like links.** An address written in
  a plain-text message (or an HTML message shown as plain text) shows the
  pointing hand, reports itself as the pointer rests on it - in the status
  line or as a tooltip, as Settings > Display > Links says - and opens in the
  browser when clicked ("www." addresses as https). Dragging across one still
  selects the text.

#### 2026-10-04 *0.10.23*
- **A fake "It's a Match!" is flagged as a scam.** A phishing mail that dressed
  itself as Tinder - Tinder's name and Tinder's own pictures, sent from an
  unrelated address, every link to a third site - passed the threat scan as
  clean: Tinder was not in the brand table, and its links' texts ("FIND OUT
  WHO", "Privacy Policy") name no site. Two new signs catch it (score 70, scam):
  - **Borrowed pictures:** the mail's pictures come from a site its display
    name or subject names (`gotinder.com` for "Tinder"), but it was sent from
    elsewhere and none of its links go to that site. This needs no brand
    table, so it also catches services the table does not know.
  - **Dating services in the brand table:** Tinder, Bumble, Hinge, OkCupid and
    Parship, so a display name claiming one from a foreign domain is
    impersonation.
  - **Older verdicts are judged again.** A verdict is stored the first time a
    message is read; one stored by older rules is now re-scanned when the
    message is opened, so mail an earlier version let through is caught.
- **The status line shows a message's links.** While a message is open, the
  status line says how many links it has and which sites they go to
  ("6 links → vakantiehuiseichenbach.nl"); its tooltip lists every link with
  the text it shows and the address it really opens. Pointing at a link or a
  linked picture in the message shows that link's address there, before you
  click.
- **HTML mail renders closer to Thunderbird:** text keeps the mail's
  `line-height` and `letter-spacing`; mail without a standards doctype lays out
  its tables as browsers do in quirks mode (a centring cell centres the
  tables, not every line of text); content wider than its box is drawn instead
  of cut off; borders are drawn per side with mitred corners; images honour
  `object-fit`, borders, rounded corners and size limits, and sit side by side;
  a shrink-to-fit button keeps its caption on one line. See the framework
  changelog for the HTML reader entries.

#### 2026-10-03 *0.10.22*
- **Mailchimp mail fits a narrow reading pane, and its footer icons are
  their real size.** Newsletters and invoices built with Mailchimp (Lexware's
  among them) stayed 600px wide in a pane narrower than 480px, because their
  narrow-screen rules use CSS attribute selectors the HTML reader skipped; and
  their footer icons were drawn 5px wide instead of 25px. Fixed in the
  framework's HTML reader (see the framework changelog, "attribute selectors"
  and "a px width or height is the content box").
- **Two-column newsletters keep their columns.** Mailchimp and similar
  templates place two columns side by side as floats (`<table align="left">`),
  which the HTML reader ignored, so the columns came one under the other in
  any pane width. And rules addressing the first or last item of a list
  (`:last-child` and similar) now apply. Fixed in the framework's HTML reader
  (see the framework changelog, "floats" and "structural pseudo-classes").
- **Text runs beside a floated picture, and newsletter headers, buttons and
  lists look as in a browser.** Text after a picture or column floated to one
  side now runs beside it instead of starting below it. Patreon's and other
  MJML-built newsletters showed a 30px logo 138px wide, their button centred
  instead of on the left, and every bullet on a line of its own above its
  text. Fixed in the framework's HTML reader (see the framework changelog,
  "CSSLayout: floats in block layout" and the HTML reader entries beside it).

#### 2026-10-02 *0.10.21*
- **Switching accounts is immediate, even while mail is being fetched.**
  Clicking another account's tile sometimes took 10 to 20 seconds. The
  background sync and the window read the mail database through one shared
  connection, which runs one statement at a time, and the sync writes a row
  per message - each its own commit forced to disk. A click that came
  during a sync waited behind all of them. The workers now have a
  connection of their own, and the database runs in write-ahead-log mode,
  where reading never waits for writing and a commit no longer forces the
  disk.
- **Names and subjects written as HTML read properly.** Some senders'
  systems put HTML character references into the header - Lexware's
  messages arrived "to Stefan Fr&ouml;hling". The list, the reading pane,
  the sender badge and collected contacts now show "Fröhling", and so do
  replies and forwards - the "... wrote:" line, the forwarded From/To
  lines and the Re:/Fwd: subject, in plain and formatted mail; every named
  and numeric reference (`&auml;`, `&amp;`, `&#8211;`, `&#x20AC;`) is
  decoded, and a plain "&" (AT&T) stays as it is.

#### 2026-10-02 *0.10.20*
- **Settings > Reading > Layout sets the folder list's width.** *Auto* (the
  default) makes the folder tree on the left 10 px wider than its longest
  account address or folder name, and fits it again as folders arrive or a
  branch is opened or closed; *Fixed width* keeps it at the number of pixels
  set beside it (100 to 600, 200 to start with - typing a width chooses it).
  Dragging the divider still resizes the list for the moment. Saved as
  `folder_tree_width_mode` / `folder_tree_width` in `preferences.ini`; the
  page's *Restore default layout* puts it back to *Auto*. Measuring the rows
  is the framework's new `UltraCanvasTreeView::GetRequiredWidth` (see the
  framework changelog, "TreeView: GetRequiredWidth").

#### 2026-10-02 *0.10.19*
- **The Outbox window: what waits to be sent, and what to do about it.** While
  messages wait, the toolbar shows *Outbox (N)*; it opens a window listing
  each one - To, Subject, the account it goes from, how often it was tried
  and why it has not gone out (the full reason in the tooltip), and whether
  its copy is in Drafts. *Send now* tries them all at once. *Edit…* (or a
  double-click) opens the message in a compose window, formatting and
  pictures included, to correct it - a wrong address the server keeps
  refusing, say; the old version is held meanwhile (no automatic attempt
  sends it) and is replaced, Drafts copy and all, once the corrected one is
  sent. Closing the window without sending lets the old version go out as
  it was. *Delete* asks first, then takes the message out of the outbox for
  good and deletes its Drafts copy - so a message that can never be sent is
  no longer tried every 30 minutes for ever.
- **Edit and Delete work while a message is being sent.** They used to be
  greyed out until the attempt was over. Now *Delete* is carried out right
  after it (and says so if the message went out meanwhile), and *Edit*
  opens the message as soon as the attempt has finished - the window says
  it will. A failed attempt does not warn about a message you are deleting.
- **Deleted Drafts copies are gone for good.** A copy taken out of Drafts -
  the message was sent, deleted or replaced by a corrected version - is now
  expunged on the server (`UID EXPUNGE`, that message only) instead of being
  left flagged as deleted, which some mail apps keep showing. A server
  without UIDPLUS leaves it flagged, as before.
- **A Drafts copy that cannot be deleted now is deleted later.** When a
  message is deleted or replaced while the server cannot be reached
  (offline, or the credential vault locked), it leaves the outbox at once
  and is never sent; its Drafts copy is deleted by the next pass that
  reaches the server, after a restart too. Delete says when this happens.
- **A sent message is filed in the Sent folder.** Once a message has gone
  out, a copy is saved to the account's Sent folder (the folder the server
  marks as Sent, else "Sent"), marked read, with the same Message-ID the
  message was sent with. Not on Gmail and Outlook.com / Microsoft 365, which
  file what is sent through them by themselves - a second copy would be a
  duplicate. A copy that cannot be saved does not affect the send.
- **The Drafts copy arrives as a draft, and read.** The IMAP plug-in now sets
  the flags of an uploaded message (it ignored them), so the copy in Drafts
  carries `\Draft` and `\Seen` instead of showing up as a new, unread
  message.
- Sending, deleting and the server copies all run through one outbox queue on
  a worker, one job at a time, so a delete never races a send.

#### 2026-10-02 *0.10.18*
- **Mail in folders with non-English names is found on Windows.** Message
  bodies are cached as `mail/<account>/<folder>/<uid>.eml`, and the account
  and folder were joined onto that path as plain strings - which Windows
  converts in its ANSI code page. A folder such as "Entwürfe" or "Корзина"
  was cached under a mangled name. UltraMail read it back the same way, so
  it went unnoticed, but EmailCleaner, which reads the cache as UTF-8,
  never found that mail. The cache path, the preview's read and the
  removal of an account's mail now pass every part through `PathFromUtf8`.
  Bodies already cached under a mangled name are fetched again.
  - The sender-icon cache built its file names the same way
    (`dir / (brandId + ".png")`); they go through `PathFromUtf8` too, found
    by the extended path check (framework changelog).
  - New test `cached_body_path_keeps_a_non_ascii_folder_name`. UltraMail's
    engine tests now run on Windows CI, where two tests failed because they
    still had a file open while it was replaced or deleted; they close it
    first.

#### 2026-10-02 *0.10.17*
- **Pictures in newsletters built from mail templates are shown.** Mail whose
  images carry `height="auto"` (Kickstarter's, and most Beefree / Braze
  newsletters) showed none of them - not even after *Show images* - because
  the HTML reader drew each one zero pixels tall. The fix is in the framework's
  HTML reader (see the framework changelog, "pictures with `height="auto"` are
  shown").
- **Newsletters fit the reading pane, menus included.** A picture as wide as
  its column made the whole 600px newsletter as wide as the picture's file -
  often 2000px - so the text ran off the right of the pane and a centred menu
  (Kickstarter's ART / COMICS / DESIGN …) was off screen entirely. Fixed in the
  framework's HTML reader (see the framework changelog, "HTML mail no longer
  runs off the right of the pane").
- **In a narrow reading pane, newsletter columns stack, as on a phone.**
  Articles side by side in the newsletter come one under another when the
  pane is narrower than the newsletter's own breakpoint (620px for most
  templates), each at the full width, with its picture centred at its own
  size instead of drawn over the text below it. Fixed in the framework's HTML
  reader (see the framework changelog, "HTML mail columns stack in a narrow
  pane").

#### 2026-10-02 *0.10.16*
- **Quote + and Quote − in the compose window's formatting toolbar.** They
  are the two quote-mark buttons at the end of the first row. They move the
  paragraph at the cursor, or every selected paragraph, one quote level in
  or out.
  - Use them to place an answer between quoted lines of a reply, or to take
    a quoted line out of the quote.
  - Each click can be undone.
  - The signature editor's toolbar does not show them.

#### 2026-10-01 *0.10.15*
- **The message cache no longer only grows.** Every message body UltraMail
  downloads is kept as `mail/<account>/<folder>/<uid>.eml`, and none was ever
  deleted: a message expunged on the server, moved to Trash or Junk, deleted,
  or renumbered by a UIDVALIDITY reset lost its row in the index but kept its
  file, so the mail folder grew by every message ever received - and
  EmailCleaner, which reads the same cache, kept finding mail that was gone.
  The body now goes with the row: `SyncEngine::MoveMessage`, the expunge in
  `ReconcileFlags`, Delete without a Trash folder (`SyncEngine::ForgetMessage`)
  and a UIDVALIDITY reset (the whole folder's files) remove it. And the first
  reconcile of each folder prunes what earlier versions left behind - only
  once the server has actually listed the folder (the same guard the expunge
  has), and only up to the highest UID the index held when it started, so a
  body a sync is writing at that moment is never touched.
- **Opened attachments no longer pile up.** Opening an attachment writes a
  copy for the viewer, and those copies were never deleted - straight into the
  `cache` folder, for good. They now go to `cache/attachments`, which is pruned
  at every start (before any viewer has a file open): what was not opened for
  a week goes, then the oldest until the rest fits in 256 MB. Opening an
  attachment again marks its copy as new. The loose copies earlier versions
  left in `cache` are cleared once; the sender icons, in their own folder
  there, are untouched. `AttachmentCache` also builds its paths through
  `PathFromUtf8` now, so an attachment named in Thai or with an emoji is
  written where it should be on Windows too.

#### 2026-10-01 *0.10.14*
- **Send works in the background, and nothing is lost on the way.** *Send*
  puts the message in UltraMail's outbox - the local store, which survives a
  crash or a restart - and closes the compose window at once; the message
  goes out on a worker while you carry on. The window stays open only when
  the message could not be queued (no recipient, no outbox), so nothing typed
  is lost.
- **A copy waits in the Drafts folder until the message is sent.** Before the
  first attempt, the message is saved to the account's Drafts folder on the
  server (the folder the server marks as Drafts, else "Drafts"), so it is
  there on every device; once it has gone out, the copy is deleted from
  Drafts. A copy that cannot be saved does not hold the message back - it is
  still in the outbox, and the warning says so.
- **A message that is not sent says so, with Retry.** The warning names the
  reason (the server refused, no connection, no outgoing server known, no
  SMTP plug-in), where the message is kept (Drafts and the outbox, or the
  outbox only) and offers *Retry*. Without an SMTP plug-in or a known
  outgoing server, the Drafts copy is still saved.
- **And it is tried again by itself.** A message left unsent goes out without
  anyone pressing *Retry*: a minute later, then after 2, 5 and 10 minutes,
  then every 30 - and at once when the connection is back (a mail check
  reached the server, the computer woke from sleep, UltraMail started with
  messages waiting). These attempts are silent; the warning is shown once,
  and a message that then goes out says so on the status line. An attempt
  never asks for the master password: with the vault locked it waits.
- *Retry* sends to the account's outgoing server as it is now: a message
  queued before the server was known, or before it was corrected in Account
  Settings, goes out once it is.
- Replies keep their thread: the In-Reply-To and References headers are sent
  (and kept in the outbox and the Drafts copy); they were dropped before.
- The outbox files a message under the account that owns its From address.

#### 2026-09-30 *0.10.13*
- **Several compose windows at once work.** UltraMail had one compose view for
  every compose window, so opening a second message rebound the first window's
  Send, Cancel, attachment buttons and formatting toolbar to the second: Send
  in the first window sent the second message, Cancel closed the other window.
  Each compose window now has its own view, and its entry is dropped once the
  window has closed (the windows used to be kept until UltraMail quit).
- What answers after a compose window closed - the file or cloud picker, the
  "send as plain text?" question, a Link… or Picture… dialog - finds the
  window gone and changes nothing.

#### 2026-09-30 *0.10.12*
- **The compose window has the full formatting toolbar.** The signature
  editor's tools now sit above every message body: bold, italic, underline,
  strikethrough, font, size and text colour in one row; left / centre /
  right, bulleted and numbered lists, a horizontal line, *Link…* and
  *Picture…* in the next. The small B / I / U / list row that only formatted
  replies had is gone. Both windows share one toolbar (`UltraMailFormatBar`),
  so they cannot drift apart.
- **Plain text | Formatted.** The switch at the right end of the toolbar
  decides how the message is written and sent. A new message starts as plain
  text, with only the switch showing. *Formatted* turns what is written into
  the rich editor, `> ` quotes becoming quote bars, and shows the tools; the
  message is then sent as HTML with a plain-text version. Replies and forwards
  of HTML mail, and mail signed with an HTML signature, open formatted.
  Switching back to plain text asks first when there is something to lose,
  since formatting, links and pictures are dropped.
- The compose window is 40 px taller, for the second toolbar row.
- Fixed while building it: a toolbar row of buttons sized to their labels
  widened the whole compose window past its right edge, hiding the switch and
  *Cancel*. The rows are now capped at the window's width.

#### 2026-09-30 *0.10.11*
- **A signature per account.** *Account Settings* has a new *Signature* row,
  which shows the account's signature in a few words, and an *Edit signature…*
  button that opens the signature editor. The account signs with nothing, with
  plain text, or with HTML.
  - *Plain text* is typed into a text box. It goes into the message below a
    `-- ` line, the separator mail programs recognise as the start of a
    signature (one the user wrote themselves is not doubled).
  - *HTML* is designed in a WYSIWYG editor (`UltraCanvasRichTextEdit`) with
    two rows of tools: bold, italic, underline, strikethrough, font, size and
    text colour; left / centre / right, bulleted and numbered lists, a
    horizontal line, *Link…* (a web page or an e-mail address) and
    *Picture…* (a logo or photo, stored with the signature and sent as an
    inline part of the message). *HTML source* switches to the markup, coloured
    as HTML, for a signature made elsewhere; *Design* reads it back.
  - Both versions are kept whichever is chosen, so switching loses neither. A
    first switch to HTML starts from the plain-text signature.
  - *Add the signature to replies and forwards too* is on by default.
- **Where it goes.** New mail, replies and forwards get the signature of the
  account they are sent from, below the line the message is written on and
  above the quoted or forwarded text. An HTML signature makes the message a
  formatted one: a plain-text reply is turned into the rich editor first, its
  `> ` quotes becoming quote bars, and is sent as HTML with a plain-text
  version beside it.
- The editor saves on its own *Save*, not with the account page's, which
  checks the sign-in first: a signature can be changed while the server is
  unreachable. The signature is kept in the local store (schema 8) and is left
  alone when the account's servers are saved or the address is added again.
- Demo: `ULTRAMAIL_DEMO_SIGNATURE=1` opens the editor on a sample HTML
  signature; `=2` (with `ULTRAMAIL_DEMO_MAIL=1`) opens a new message signed
  with it.

#### 2026-09-30 *0.10.10*
- **New mail is fetched as soon as UltraMail starts.** The first check used to
  wait for the five-minute timer (it ran at start only when the vault needed a
  password), so the inbox showed what was cached until *Update* was pressed, and
  nothing on screen said a check was due. Now every account is checked right after
  the window appears: the *Update* button reads "Updating…", the status line says
  "Checking …" with its spinner and the connection pill turns "Checking…", then
  "Connected". A network that is not up yet right after boot gets the usual grace
  period - status line and a retry, no alert.
- **And right after the computer wakes from sleep.** The five-minute timer cannot
  tell that the machine slept, so after a wake the inbox could stay as it was
  before the sleep for minutes. A light 15-second check (`WakeDetector`) notices
  that far more time passed between two of its ticks than it should have, and
  every account is checked 5 seconds later, once Wi-Fi has had a moment to
  reconnect. What was offline before the sleep starts a fresh grace period, so a
  network that is still coming back shows on the status line, not in an alert.
- **A Settings window, like UltraFiler's.** The gear at the right end of the
  toolbar (UltraFiler's gear button) opens it: a page tree on the left, and
  pages with their notes and a *Restore default* button.
  - *Privacy > Images*: remote pictures load **always**, **only from trusted
    websites, trusted senders and contacts** (the default), or **never by
    themselves**. Trusted websites are new: mail from such a domain (or a
    subdomain) shows its pictures, and a picture hosted there loads in any
    message. The "Always from <sender>" list can be edited here. Junk and
    suspicious mail still never load pictures by themselves.
  - *Reading > Messages*: HTML mail formatted or as plain text, and the message
    text size (11-16 px). *Reading > Layout*: the reading pane.
    *Privacy > Sender icons*: downloading the known senders' icons.
  - The reading-pane and sender-icon switches moved here from each account's
    *Account Settings*, since they were never per account.
  - The start page (no account yet) carries the same gear in its top-right
    corner, so privacy can be set before the first account is added.
- **HTML mail is laid out for the width of the preview pane.** A newsletter's
  `@media (min-width: …)` rules (side-by-side columns from 480px up) are answered for
  the pane's width when the message is opened; resizing the pane does not re-lay the
  message out yet. The HTML rendering improvements behind it (tables, buttons,
  background pictures, rounded borderless buttons) are framework changes - see the
  pending `html-mail-table-layout` and `html-media-backgrounds` entries in
  `Docs/UltraCanvas/CHANGELOG.md`.

#### 2026-09-30 *0.10.9*
- **No "New mail could not be fetched" alert while the network is still
  coming up.** Right after the computer starts, the first background sync
  often runs before the connection is there, and the alert it raised
  ("Could not resolve host") was a false alarm. A background sync that cannot
  reach the server at all — no name resolution, no route, nobody listening,
  a timed-out connection — now shows the reason on the status line only, and
  UltraMail tries the account again every minute. The alert appears only
  when the account has stayed unreachable for ten minutes, and then once.
  - Mail arrives within a minute of the network coming up, instead of at
    the next five-minute sync.
  - Opening a folder while the server cannot be reached takes the same
    grace period; it used to raise "That folder could not be fetched" once.
  - A failure the server itself produced — a rejected password, an
    untrusted certificate — is reported at once, as before, and so is any
    failure of a sync you asked for with Update.
  - The engine's sync outcome now carries UltraNet's result code, and a
    failed inbox fetch keeps its connection details for the alert.
- **A connection pill at the right end of the status line** shows how the
  selected account's last contact with its mail server went: *Not checked*,
  *Checking…*, *Connected* (green), *Offline* (amber — the server could not
  be reached) or *Failed* (red — the server answered but refused the
  request). Hovering it shows the account, the server, the state, the time
  of the last contact and the last attempt, the reason for a failure, how
  many attempts in a row have failed, and what happens next.
- The `[UMSTREAM]` debug lines the sync engine and the progress callback
  printed to stderr on every fetch since 0.9.51 are gone.
- `ULTRAMAIL_DEMO_COLLECT=1` now leaves the main window on top of the seeded
  mail; `ULTRAMAIL_DEMO_COLLECT=contacts` opens the contact manager as it
  always did.

#### 2026-09-29 *0.10.8*
- **Replies and forwards keep an HTML message's formatting.** Answering or
  forwarding an HTML mail used to turn it into plain text with "> " in front
  of every line. Headings, bold and italic, colours, links, lists, tables and
  pictures were lost.
  - The composer now opens the message as formatted text in the rich text
    editor, with an empty line at the top to write in.
  - A reply shows the original below "On <date>, <sender> wrote:", marked
    as a quote by a bar at its left.
  - A forward shows the forwarded-message header, then the original as it
    was.
  - Pictures embedded in the message come along. Remote pictures come along
    only if the reading pane has already loaded them, so composing never
    contacts the sender's server.
  - A row above the text has Bold, Italic, Underline and the two list
    buttons.
  - To answer between two quoted lines, press Enter on an empty quoted line
    to step out of the quote, or press Backspace at the start of a quoted
    paragraph.
  - A plain-text message is still answered as before.
- **Formatted replies are sent as HTML with a plain-text version.** The
  pictures travel as parts of the message (`cid:`), so the recipient's mail
  program shows them without downloading anything.
  - The outbox keeps the plain-text version and the pictures (outbox
    migration 2), so a message queued while offline goes out complete.

#### 2026-09-28 *0.10.7*
- **A turning ring on the status line** while UltraMail works in the
  background: a sync, a send, or a move, delete or flag change on the server.
  It stops, and leaves the space blank, when the app is idle. It is the new
  framework element `UltraCanvasBusyIndicator` (see
  `Docs/UltraCanvas/changelog.d/`).
- **Japanese (and other non-Latin) mail is readable.** Sender names,
  subjects and message text in ISO-2022-JP showed as `$B3t<02q…(B`, and
  Shift_JIS, GB2312, EUC-KR or KOI8-R text was garbled the same way. They are
  now converted to UTF-8 (framework change, see `Docs/UltraCanvas/changelog.d/`).
  Subjects and names sent as raw 8-bit bytes with no charset label are now
  decoded too. They are read in the message's own charset when there is
  one; otherwise UltraMail guesses the most likely one. Contacts already
  saved with such a name are repaired the next time UltraMail starts.
- **The Contacts window is fast.** Choosing a section with a thousand
  contacts took most of a second, because each contact was built as its own
  set of widgets after two database queries per contact. The list is now a
  scrolling list view that draws only the visible rows, loaded in three
  queries per section. The sender badges' address-book read after every sync
  uses the same batched read.
- **The Contacts window no longer closes unexpectedly.** Choosing a section
  destroyed the sidebar entry that was still handling the click, and opening
  Contacts a second time built a second panel on the same view as the first.
  Choosing now only restyles the sidebar, and there is one Contacts window:
  asking for it again brings it forward, and closing it releases it.
- **Contacts window:**
  - a scrollbar, and the window is 100 px wider (720 px);
  - a filter field above the list (name, address, phone or organization);
  - a bin icon at the end of each row to delete the contact, after a
    confirmation;
  - **Add group** under the sidebar, and a right-click menu on the sidebar
    with *Add group* and *Delete group*. Deleting a group keeps its contacts:
    they go back to their sections. The built-in sections cannot be deleted.
  - a right-click menu on a contact with *Edit*, *Move to group ▸* (every
    section and group) and *Delete*. A contact filed in a group keeps its
    section, which still decides its sender-badge colour.
- **Images in HTML mail.**
  - Images carried inside the message (`cid:` parts of multipart/related
    mail, `data:` URIs) are shown. Until now every image rendered empty.
  - Images on the web are not loaded until you ask, because loading one
    tells the sender that, when and where you opened the message (a
    tracking pixel is exactly that). A bar above the body counts them and
    offers *Show images* for this message, or *Always from <sender>*, which
    is remembered in `preferences.ini` (`remote_images_from`).
  - A message in the Junk folder or flagged as spam or scam never loads
    them by itself and is never offered *Always*.
  - Images download in the background (at most 60 per message, 5 MB each),
    and the body is redrawn when they arrive.
  - Links in HTML mail open in the browser (web and `mailto:` links only).
- **Attachments open in UltraCanvas's media viewer.** Images, PDF,
  spreadsheets, text and source files, e-books, fonts, 3D models, audio and
  video open in a viewer window of UltraMail's own, the same on every
  platform. Only a kind the viewer does not know goes to the system's default
  application, or is offered for saving as before.
- **View source is colour-coded as HTML.** The message source window
  highlights tags, attributes and values, so an HTML mail's structure is
  readable. The headers stay plain text.
- **A paperclip marks mail with attachments.** It sits at the right end of
  the subject cell, and the row's tooltip says how many attachments there
  are.
  - The count is taken when the message body is downloaded, from the same
    parts the reading pane shows as attachment chips. Inline images of the
    body do not count.
  - It is stored beside the message's security verdict, so the list needs no
    extra work to show it.
  - Mail downloaded by an earlier version is counted during the next syncs
    (300 per folder per sync, newest first), or as soon as it is opened.
- **Unread mail is bold.** An unread message's subject and date are drawn
  bold in the list, besides the ● and the darker colour it already had.
- **Update button with a download icon.** *Reload* is now *Update*, with a
  download icon; it reads *Updating…* while it runs and fetches the account on
  screen now instead of waiting for the five-minute background check.
- **Right-click menu on a message:**
  - *Mark as read* or *Mark as unread*.
  - *Needs an answer* or *Doesn't need an answer*: puts the message on the ↩
    needs-an-answer list, or takes it off, whatever the automatic rule says.
    The choice is kept across syncs and ends when you answer the message (here
    or on another client). It is stored locally, because IMAP has no standard
    flag for it.
  - *Mark as spam* (moves it to the Junk folder) or, in the Junk folder,
    *Not spam* (moves it back to the inbox).
  - *Unsubscribe…*: leaves the mailing list the way its `List-Unsubscribe`
    header asks, after a confirmation. It uses a one-click request with no
    browser where the list offers one (RFC 8058), otherwise it opens the
    list's unsubscribe page, or prepares the unsubscribe email for you to send.
    A message in the spam folder gets a warning first, because unsubscribing
    from real spam tells the sender that the address is read. The message is
    downloaded first if its body is not cached yet.
  - *Move to folder ▸*: every folder of the account that holds mail.
  - *Show emails ▸* narrows the list to one kind of mail, with a check mark
    on the active choice:
    - *All messages*.
    - *Same sender*.
    - *Unread*.
    - *Needs an answer*.
    - *Spam*: the sender badge says spam or scam.
    - *Social media*: social networks and messaging services. That means
      the sender registry, about fifty more social-network domains
      (subdomains included), self-hosted networks (Mastodon, Pleroma,
      Friendica, Lemmy, `social.` hosts), and notification subjects such
      as "commented on your", "new follower" or "hat deinen Beitrag
      kommentiert", in English, German, French, Spanish, Italian, Dutch
      and Portuguese.
    - *Payments & invoices*: payment services such as PayPal and Stripe, or
      a subject about an invoice, a receipt, a bill or a payment, in
      English, German, French, Spanish, Italian, Dutch or Portuguese.

    A search field above the list narrows it further. Every word typed must
    appear in the sender's name, the address or the subject. Matching
    ignores case, including accented letters, so "ü" finds "Ü". The ×
    clears it. The search works together with the *Show emails* choice.

    The list title names the filter and the search ("Inbox · Unread ·
    “invoice” — 3 messages"). The
    filter stays through syncs and is cleared by switching folder or
    account. Right-clicking the empty area of the list offers *Show emails*
    too, so a filter that leaves nothing to click can still be cleared.
  - The menu is titled with the sender's address.
  - *Add to contact group ▸*: every section and group of the address book.
    It files the sender there, adding them to the address book first when
    they are not in it yet.
  - *Add to contacts* when the sender's address is not in the address book,
    else *Edit contact*. Both open the address book's contact editor, with the
    name and address filled in for a new contact. That editor also no longer
    drops a contact's other addresses when you change the first one: it now
    edits only the primary address.
- **A long subject no longer takes two rows in the message list.** Some
  senders (LinkedIn, for one) encode line breaks into the subject. The list
  now shows the sender and the subject on one line, with runs of spaces, tabs
  and line breaks folded into a single space.
- **Service icons without a frame.** A sender with a known service icon
  (Gmail, Outlook, …) now shows the icon alone and at the full badge size,
  in the list and in the reading pane. Senders without an icon keep the
  framed initial.
- **Buttons fit their labels.** Every button with text now sizes itself to
  its label, with its old width as the minimum, so "Reloading…" and longer
  translated labels are no longer cut off.

#### 2026-09-28 *0.10.6*
- **Sending no longer holds the window.** Send and the outbox's Retry ran
  SMTP on the UI thread, flushing every queued message - of every account -
  while the window waited: with a slow or failing outgoing server that was the
  connect and operation timeout per queued message, plus any OAuth2 token
  refresh, with no response to clicks. The flush runs on a worker now ("Sending…"
  on the status line), one at a time - two would send a queued message twice -
  and a send made meanwhile is flushed right after. Send failures show the
  connection details too.
- **A failing account is no longer silent after the first alert.** One flag
  for all accounts muted every later sync failure - this account's and any
  other's - until some sync succeeded, and the status line only said "Could
  not reach the server" (then "Up to date" once another account synced). Each
  account now has its own alert-once state, and its last failure ("Could not
  fetch mail for …: reason") shows on the status line whenever it is the
  selected account, until it syncs again.
- **Connection details for every failed sign-in or sync.** The alert for a
  failed sync, move or flag change now shows, under the reason, a
  *Connection details* block: UltraMail's and the framework's versions, the
  UltraNet plug-in and its version, server, address, TLS mode, sign-in method,
  libcurl and TLS library versions, trusted roots and the system - what a bug
  report needs, and copyable. The server settings page gets a **Details**
  button beside *Save anyway* after a failed check, showing the same.
  Needs the framework's `UltraNetResult::diagnostics` (see
  `Docs/UltraCanvas/changelog.d/`).
- **Windows: mail servers are checked against the Windows certificate
  store** (framework change, see `Docs/UltraCanvas/changelog.d/`), so a Let's
  Encrypt server no longer fails with "the certificate or certificate chain is
  based on an untrusted root".

#### 2026-09-28 *0.10.5*
- **The version is in the window title** — `UltraMail 0.10.5` — so a screenshot or a
  bug report says which build it came from. The number is this changelog's
  first line, as everywhere else (`cmake/UltraCanvasVersion.cmake`).
- **The setup guide matches the Gmail sign-in.** Gmail has signed in through
  the browser only since 2026-09-23 (no password field in the wizard);
  `Docs/UltraMail/AccountSetup.md` still described an app password as the
  alternative and as the fallback for a build without a Google client. The
  engine test that expected Gmail to take a typed password is corrected too.

#### 2026-09-27 *0.10.4*
- **An authentication method per server, as in Thunderbird.** The server
  settings page - during setup for an unknown domain and on every account's
  Settings - has *Sign-in (IMAP)* and *Sign-in (SMTP)*: Automatic, Normal
  password, Encrypted password, OAuth2, Kerberos / GSSAPI, NTLM, and (for the
  outgoing server) No authentication. The provider table sets OAuth2 for
  Gmail, Outlook/Hotmail/Live and Yahoo; an autoconfig document sets the
  method it lists; everything else, and every existing account, starts on
  Automatic, which is what UltraMail did before. Stored per account
  (`imap_auth` / `smtp_auth`, schema step 5) and applied to every IMAP
  session, send and login check. Needs the framework's
  `UltraNetMailOptions::auth` (see `Docs/UltraCanvas/changelog.d/`).
  `Docs/UltraMail/AccountSetup.md` §5 lists what each choice sends.
- **Sign-in errors say why.** A failed login check or sync now shows the
  TLS library's own reason in brackets - "unable to get local issuer
  certificate", "certificate has expired", ... - where it used to say only
  "SSL peer certificate or SSH remote key was not OK". §6 of the setup guide
  says what each one means.

#### 2026-09-23 *0.10.3*
- **`OAuthApps` is a profile of UltraNet's shared OAuth2 app registry**
  (framework 0.9.34). Same API, same order - `Set()`, `ULTRAMAIL_*` in the
  environment, `oauth.ini`, the baked-in client - and the same behaviour for
  the wizard; what changes is where the registration lives: the Google and
  Microsoft clients UltraMail ships or reads from `oauth.ini` now also serve
  UltraCloud's Google Drive and OneDrive sign-ins in the composer's "Attach
  cloud link…" picker, and the shared `ULTRANET_OAUTH_GOOGLE_CLIENT_ID` /
  `ULTRANET_OAUTH_MICROSOFT_CLIENT_ID` names work beside the `ULTRAMAIL_`
  ones. `Docs/UltraMail/AccountSetup.md` §3 says what to add to the consent
  screen for that.

#### 2026-09-20 *0.10.2*
- **The credential vault is the framework's now.** `UltraMailCredentialVault.cpp`
  was the only implementation of the device-key vault, and UltraSocial had
  copied it; the implementation moved to `UltraVault::DeviceKeyVault`
  (framework 0.9.23) and `UltraMailCredentialVault.h` only names UltraMail's
  profile — `ultramail.vault` and `device.key` in the account folder, keys
  `mail.ultramail.<account>`. Nothing on disk changes and no existing vault
  needs migrating; `CredentialVault`, `OAuthTokens`, `SignInMethod` and
  `VaultStatus` keep their names in `namespace UltraMail` (the last three are
  aliases of the framework's types), so the app, EmailCleaner and the tests
  compile unchanged. `KeyFor` is a member now rather than static.
- **Cloud account credentials live in the mail vault too.** "Attach cloud
  link" kept its Nextcloud / Dropbox / Drive tokens in obfuscated files under
  `cloud-vault/` (UltraCloud's file store, which framework 0.9.23 removes).
  They are in `ultramail.vault` now, under `cloud.<accountId>.*`, and the old
  files are carried across and removed the first time the vault is open — at
  start-up with the device key, or after the one master-password prompt an
  older vault still needs.

#### 2026-09-19 *0.10.1*
- **Crowdfunding and creator-support platforms are known senders.** Kickstarter,
  Indiegogo, GoFundMe, Startnext, Crowd Supply, Patreon, Buy Me a Coffee, Ko-fi,
  Liberapay, Open Collective, Gumroad and Substack join the registry, along with
  Pinterest, Tumblr, Mastodon, Twitch, Vimeo and Etsy. Each carries its own
  site's favicon URL, which is exactly what the sender-icon cache fetches — one
  request per service, once, on the sync worker — so their mail arrives wearing
  the platform's own mark.
- **Registry entries now carry a category** (`BrandCategory`: social, messaging,
  crowdfunding, creator support, shopping, payment, technology, media, travel,
  delivery). The badge tooltip names it — "Kickstarter (Crowdfunding platform)" —
  and a collected contact's note records it.
- **The registry is a source of new business contacts, not only of icons.**
  A sender that belongs to a known service now reads as a **business contact**
  (blue) even before the address book has caught up, and the auto-collector
  (`ContactCollector::CollectSender`) files it under **Services** with the
  service's name as the contact's organization and a note saying what kind of
  service it is. Ordinary addresses still land in Other, a personal mailbox is
  never a service however large its provider, and **an existing contact is never
  modified or reclassified** — an address the user filed under Friends stays
  there. Bulk mail keeps its own badge: a campaign newsletter from a known
  platform is still an advertisement (dark blue), because that is what the
  dark-blue badge is for; a pledge receipt or order confirmation from the same
  service carries no bulk markers and reads blue.

#### 2026-09-19 *0.10.0*
- **A sender badge left of every subject line.** The message list gained a
  narrow column between From and Subject, and the reading pane's avatar became
  the same square: the service's own icon where the address belongs to one, the
  sender's initial where it does not, inside a frame that says who the sender
  is — filled green for an address book contact under Family/Friends/Leisure,
  filled blue for one under Work/Services, a black outline for a sender never
  seen before, dark blue for bulk mail, orange for likely spam and red for a
  likely scam. Two rules decide: known beats guessed (a contact stays a contact
  even when the mail is bulk), and danger beats known (a message whose links
  lie is a scam even from a known address — an address book entry says who an
  address belongs to, not that *this* message came from them). The colour is
  never the only place a verdict is said: the badge's tooltip spells out the
  class, the service and every reason, and the reading pane adds a warning
  strip above the body of anything suspicious. Nothing is hidden, moved or
  deleted. New: `UltraMailSenderTrust`, `UltraMailSenderBadge`, the `kTrust*`
  colours in `UltraMailTheme.h`.
- **A cache folder of known senders' icons.** `<dataDir>/cache/sender-icons`
  now holds one icon per entry of a curated registry of ~30 services —
  Facebook, Instagram, WhatsApp, LinkedIn, X, Claude, OpenAI, the Google and
  Apple services, Microsoft, GitHub, Amazon, PayPal, Stripe, eBay, Netflix,
  Spotify, Dropbox, Slack, Discord, Telegram, Reddit, TikTok, Zoom, Booking,
  Airbnb, DHL, UPS, FedEx — fetched once each, on the sync worker, over a
  TLS-verified HTTPS GET, with the file extension sniffed from the bytes and a
  failure remembered for a week rather than retried every sync. Two boundaries
  are deliberate: **only the registry is ever fetched** (UltraMail never asks
  the internet about a stranger's domain, which would tell a third party who
  writes to the user), and **the fetch is injected**, so the engine keeps no
  network dependency and the suite drives the cache with a fake. A new
  Settings checkbox, *Download icons of known senders*
  (`fetch_sender_icons` in `preferences.ini`), turns downloading off; icons
  already cached keep being shown, and a missing icon is a normal state — the
  badge falls back to the initial in the brand's own colour.
- **A brand is matched on the registrable domain, never on a name.**
  `amazon.secure-login.ru` is not Amazon and is not handed Amazon's icon, and —
  as asked — a Google *service* domain is Google while an ordinary `gmail.com`
  address is just a person (the same holds for `icloud.com`, `gmx.net` and the
  other mailbox providers). `UltraMailSenderBrands` carries the table, the
  registrable-domain reduction and the "does this domain really belong to the
  brand it claims?" test the phishing scan is built on.
- **UltraMail now says when a message's links do not go where they say.**
  `UltraMailThreatScan` reads every link and button out of an HTML or plain
  text body and weighs seventeen signals: anchor text naming one site while the
  href goes to another, `http://paypal.com@203.0.113.9/` userinfo hiding the
  real host, numeric-address and punycode targets, a brand's name worn in front
  of a foreign domain, a sender claiming a brand its address does not own,
  credential language plus a link off the sender's domain, `spf`/`dkim`/`dmarc`
  failures, a Reply-To pointing elsewhere, sign-in links over plain HTTP, URL
  shorteners, and an attachment that is a program — `Invoice_2026.pdf.exe`
  weighs more than a plainly named `setup.exe`, because the disguise *is* the
  attack. 45 points is a scam, 22 is suspicious, bulk markers alone are an
  advertisement. It is just as deliberately quiet about ordinary mail: a
  personal message, a tracking link on the sender's own domain and a genuine
  brand newsletter all come out clean.
- **Each body is scanned once, where it is downloaded.** The verdict (level,
  score, reasons) is stored by `LocalStore` in a new `message_security` table —
  schema 4 — keyed by account/folder/UID. It is a separate table rather than
  columns on `messages` because an envelope upsert runs on every header sync,
  long before a body exists to scan, and would reset the verdict each time. So
  the list colours a whole folder's badges from one query, a phishing mail is
  marked before it is ever opened, and a message cached by an older build is
  scanned the first time it is read.
- Docs: [`Docs/UltraMail/SenderBadges.md`](SenderBadges.md) — the colours, the
  registry, the cache and the full rule table. Tests:
  `Tests/UltraMail/test_senderidentity.cpp` and `test_threatscan.cpp`, plus the
  stored-verdict and scan-on-download cases in `test_localstore.cpp` and
  `test_syncengine.cpp`.

#### 2026-09-15 *0.9.5*
- **"Add email account" accepts a name like `Fröhling`.** Typing one into the
  wizard's Your name field (or a passphrase with an umlaut into Password) used
  to leave invalid UTF-8 in the field the moment the caret moved over the
  character, backspaced across it or a click landed on it — the text stopped
  drawing and the debug log filled with Pango's `Invalid UTF-8 string passed to
  pango_layout_set_text()`. The fault was in the framework's text field, not in
  UltraMail: see framework 0.8.61 for what changed. That entry also covers the
  other half of the same name's journey — the display name now goes out as an
  RFC 2047 encoded-word in `From:` instead of as a raw 8-bit header byte.

#### 2026-09-14 *0.9.4*
- **One icon for UltraMail, everywhere it is shown.** `media/appicon/UltraMail.png`
  — the coloured ring around an `@` — replaces the envelope placeholder
  (`UltraMail.svg`, now removed) as the app's single mark, and every place that
  shows one now reads that one file:
  - the **window icon**, published as `_NET_WM_ICON` from `main.cpp`, which is
    also where a Linux / ULTRA OS **taskbar** takes the button's image from;
  - the **taskbar and Explorer icon on Windows**, which read the binary rather
    than the window: `ultracanvas_embed_app_icon(UltraMail …)` converts the PNG
    to a multi-resolution `.ico` and links it into `UltraMail.exe`;
  - the **filer's app icon**, via a new `Apps/UltraMail/UltraMail.desktop`
    (`Icon=UltraMail`) installed to `share/applications` with the PNG installed
    to `share/icons/hicolor/256x256/apps` — UltraFiler resolves an application's
    icon by reading its desktop entry and looking the name up in the installed
    themes (`UltraCanvasNativeFileIcons.cpp`), so the entry is what it needs to
    find anything but a generic executable glyph;
  - the **start-page logo** (`UltraMailStartPage`);
  - and `UCAPP_ICON_PATH`, the core's build-time fallback, so a window cannot
    come up unbranded even if an icon is never set explicitly.
  The desktop entry deliberately declares no `MimeType=` and no `%U`:
  UltraMail's `main()` takes no arguments yet, so claiming the `mailto:`
  association would route mail links to a program that drops them.

#### 2026-09-14 *0.9.3*
- Implemented OAuth for Google. Fixes in layout and HTML renderer.

#### 2026-09-11 *0.9.2*
- **Type scale matched to UltraFiler.** UltraMail's text was far larger than
  the rest of the desktop (13pt body, 15pt headings, 18pt titles next to
  UltraFiler's 9pt UI font). `UltraMailTheme.h` now uses 9pt body text,
  8.5pt secondary, 8pt small, 11pt headings and 13pt titles; controls are
  24px high in a 28px toolbar, the avatar square 28px, and the paddings,
  gaps and radii shrank with them. Every view followed: the inbox list
  (22px rows and header, narrower From / Date columns), the message header
  (13pt subject, 26px avatar, HTML bodies at 12px), the account cards and
  their counters, the contact rows, the composer, the attachment chips, the
  start page, and every dialog (wizard, master password, wait, server
  settings, contact) with its fixed heights. Dropdowns get the body size
  through the new `Theme::StyleDropdown`.

#### 2026-09-10 *0.9.1*
- **The settings page checks the sign-in before it saves.** Save opens one
  IMAP session to the incoming server with the entered host, port and
  security and the account's credentials — the typed password in the
  wizard, or the stored password / OAuth2 token of an existing account
  (resolved on the worker like a sync) — and only closes on success. A
  failed check shows the reason in place and offers **Save anyway**, for a
  server that is down right now or when the IMAP plug-in is not loaded. The
  engine side is `LoginCheck::Imap` / `OptionsFor` in
  `UltraMailLoginCheck.{h,cpp}` (tested with a recording mailbox); the page
  takes a `Verifier` and runs it off the UI thread via
  `UltraMailApp::LoginVerifier`. SMTP has no sign-in-only operation in the
  plug-in interface, so the outgoing server is not checked.

#### 2026-09-10 *0.9.0*
- **Any provider: autoconfig lookup and a manual settings page.** An address
  outside the provider table no longer ends as an account that cannot fetch.
  The wizard now asks the domain's own autoconfig document, its
  `.well-known` copy and the Thunderbird ISPDB (`AutoDiscovery::Discover`,
  on a worker thread behind a cancellable "Looking up server settings"
  dialog) and, when nothing is published, opens the **server settings page**
  (`UltraMailServerSettingsDialog`: incoming / outgoing host · port ·
  security dropdown, username; prefilled with `imap.<domain>` 993 SSL/TLS
  and `smtp.<domain>` 587 STARTTLS via `AutoDiscovery::GuessForDomain`;
  validates in place). The account-ready dialog says where the settings
  came from.
- **Server settings are stored on the account.** `Account` carries
  `imap` / `smtp` (`MailServerSettings`: host, port, security, username,
  oauth flag) and `providerName`; `LocalStore` schema 2 adds the columns.
  Every sync and send resolves the servers through
  `AutoDiscovery::ForAccount` — the stored ones, else the provider table for
  accounts created before — and applies the stored security (SSL/TLS,
  STARTTLS, plain) and username instead of assuming implicit TLS and the
  address. `Outbox::Flush` takes an options resolver that sets the SMTP
  session's username and TLS mode along with the credentials.
- **Reload opens the settings page** for an account whose servers are not
  known, instead of only saying so; saving stores them and syncs at once.
  Adding an address again keeps the servers it already has.
- `MailSecurity` and `MailServerSettings` moved from `UltraMailDiscovery.h`
  to `UltraMailTypes.h`; the plaintext value is `MailSecurity::Plain` (the
  old `None` collides with the X11 macro once a UI translation unit includes
  the type). `OAuthWaitDialog` became the generic `WaitDialog`.

#### 2026-09-10 *0.8.3*
- **Account setup guide.** `Docs/UltraMail/AccountSetup.md`: how to sign in
  to each provider in the table (Gmail, Outlook / Microsoft 365, Yahoo,
  iCloud, GMX, WEB.DE, mailbox.org, Posteo) — servers, the sign-in each one
  expects, where its app password is generated, the OAuth client registration
  for the browser sign-in, what lives on the machine, and what the error
  messages mean.
- **Outlook is browser sign-in only.** Microsoft retired password
  ("basic") authentication for IMAP/SMTP on Outlook.com and in Microsoft 365,
  app passwords included. The wizard hint no longer offers an app password
  for Outlook addresses, and the account-ready dialog says a typed password
  will be refused. `ProviderAcceptsPassword` in `UltraMailOAuth` carries the
  rule.

#### 2026-09-10 *0.8.2*
- **App-password hint for Yahoo and iCloud.** The wizard's live hint under
  the password field now also covers providers that offer no OAuth2 to mail
  apps but reject the normal account password: Yahoo and iCloud get "enter an
  app password generated in your account's security settings" as the address
  is typed (placeholder "App password"), the same advice the account-ready
  dialog gives. The rule is `ProviderNeedsAppPassword` in `UltraMailOAuth`,
  shared by the wizard and the dialog.

#### 2026-09-10 *0.8.1*
- **Outlook / Microsoft 365 sign in with Microsoft.** The second entry in the
  OAuth2 provider table: `microsoft` — the Microsoft identity platform's
  `common` tenant endpoints, the `IMAP.AccessAsUser.All` + `SMTP.Send` +
  `offline_access` scopes, `prompt=select_account`, a public client (no
  secret). Outlook, Hotmail, Live and Microsoft 365 addresses get the same
  browser sign-in, wait dialog, vault token set and XOAUTH2 sessions as Gmail.
  Registration: `[microsoft]` in `oauth.ini` or `ULTRAMAIL_MICROSOFT_CLIENT_ID`
  (README, "OAuth2 sign-in").
- **Per-provider redirect default.** Microsoft matches loopback redirects on
  host and path with the port ignored, so its default is
  `http://127.0.0.1:0/` (register `http://127.0.0.1`); Google keeps
  `/callback`. `OAuthApps::Get` fills an empty `redirectUri` with
  `DefaultRedirectUri(provider)`.
- **Login hint.** The typed address goes to the consent page as `login_hint`
  for both providers, so the user is not asked to pick the account again.

#### 2026-09-10 *0.8.0*
- **Gmail signs in with Google.** Leave the password empty in the account
  wizard for a Gmail / Googlemail address and UltraMail opens Google's consent
  page in the browser (OAuth2 authorization code + PKCE over UltraNet's OAuth2
  client, redirect caught on an ephemeral loopback port). A "Sign in with
  Google" dialog waits — with Cancel — until the redirect arrives, the tokens
  go into the credential vault, and the inbox is fetched right away. Every
  IMAP and SMTP session of such an account then authenticates with XOAUTH2
  and a fresh bearer token: an expired one is refreshed through Google on the
  worker thread before the fetch, and stored again. A typed password still
  works the classic way (an app password).
- **Engine: `UltraMailOAuth.{h,cpp}`** — the provider table (`google`:
  endpoints, the `https://mail.google.com/` scope, `access_type=offline`,
  `prompt=consent`), the app registration (`OAuthApps`: `Set()`, the
  environment `ULTRAMAIL_GOOGLE_CLIENT_ID` / `_CLIENT_SECRET` /
  `_REDIRECT_URI`, or `oauth.ini` in the data folder), and `MailOAuth`
  (`SignIn`, `EnsureFresh`, `CredentialsFor`) with test seams for the
  interactive authorization and the refresh. `CredentialVault` stores an
  OAuth2 token set (access + refresh + expiry) beside the password slot — an
  account has exactly one sign-in method (`MethodFor`). `Outbox::Flush` takes
  a credentials resolver; `SyncService::SyncInBackground` takes a prepare step
  that runs on the worker. The SMTP plug-in now honours XOAUTH2 bearer
  credentials like the IMAP plug-in already did.
- **Wizard hint.** As the address is typed, the wizard says whether to leave
  the password empty for the browser sign-in, or — when no Google OAuth client
  is configured — to use an app password.

#### 2026-09-10 *0.7.1*
- **A new account fetches its inbox right away.** Adding an account only
  wrote it to the store; the first sync waited for the five-minute timer —
  which had been started before the account existed, so it never covered it —
  or for a manual Reload. Once the password is in the vault the account is
  put on the schedule and synced at once, and the timer starts if it was not
  running yet (the plug-in used to be checked only at start-up).
- **The IMAP plug-in is found wherever the app is started from.** The UltraNet
  registry looks for plug-ins in `Plugins/UltraNet` relative to the *working
  directory*, which matches the build tree only when UltraMail is run from
  there. The app now resolves the directory against the executable (up to two
  levels above it, then the working directory), `ULTRAMAIL_PLUGIN_DIR` still
  overriding.
- **Nothing fails silently any more when mail cannot be fetched.** Reload and
  the first sync of a new account used to return without a word when the IMAP
  plug-in was not loaded, when no server was known for the address, or when no
  password was stored. Each case now says what is missing and where (the
  plug-in message names the directory that was searched). Errors from a sync
  the user asked for are always shown; timer syncs still report once.
- **Passwords of the second and later accounts are saved on Windows.**
  UltraVault replaced the vault file with C's `rename()`, which on Windows
  refuses to overwrite an existing file — so the first account's password was
  stored and every later one failed with "could not be saved to the credential
  vault". It uses `std::filesystem::rename` now.
- **The data folder is `%APPDATA%\UltraMail` on Windows.** `HOME` is normally
  unset there, so the mailbox database and the vault were created in whatever
  folder the app was started from.
- **App-password hint.** The account-ready dialog tells Gmail, Outlook and
  Yahoo users that the normal sign-in password is rejected over IMAP and an app
  password from the provider's security settings is needed; the earlier
  "Sign-in: OAuth2 (browser)" line described a flow the app does not have.

#### 2026-09-09 *0.7.0*
- **Every window restyled on one theme.** `Apps/UltraMail/ui/UltraMailTheme.h`
  now holds the app's colours (near-white page, white cards with hairline
  borders, one blue accent, a primary / secondary / muted text scale), type
  sizes, metrics, and the styling helpers (`StylePrimary`, `StyleSecondary`,
  `StyleInput`, `CardGroupBox`, `MakeAvatar`, `ClickSurface`). The windows use
  it instead of styling in place, so they cannot drift apart.
- **Main window.** The 150px button column is gone; a single toolbar row
  carries **New email** (the one filled button), **Reload**, **Contacts** and,
  on the right, **Add account**. The account summary is a compact card:
  provider initial in a tinted square, the local part with the domain under it
  (full address on hover), and the three counters as tinted *count · caption*
  pills (blue = new today, green = unread before, orange = waiting for reply).
  With several accounts the tiles are the same cards, the selected one with an
  accent frame; their height follows their content instead of a fixed square.
- **Inbox and message panes** are white cards separated by an invisible
  splitter gap. The list has 30px rows, a quiet header, soft hover / selection
  tints and no expander column; the date column shortens the way mail clients
  do (time today, "Sep 09" this year, "Jan 14, 2025" older; the full date is in
  the row tooltip). The message header is one row — sender avatar, name over
  address · recipients, date, **Reply** — above a rule and the body; with no
  selection the pane shows a muted hint and no header chrome.
- **Composer** is a flex layout that follows the window (760×620 by default):
  To / Cc / Subject rows with captions, a rule, a borderless body that takes
  the remaining height, an attachment row shown only while something is
  attached, and a bottom toolbar with **Send** (primary), **Attach file…**,
  **Attach cloud link…** and, apart on the right, **Cancel** — which now closes
  the window.
- **Contacts** is a sidebar + list layout that follows the window: sections
  are selectable entries with counts in a tinted sidebar, the list is a titled
  column of contact cards (initial, name, email · phone, organisation). The
  contact dialog, the account wizard and the master-password dialog share the
  same rows, inputs and button order (Cancel, then the primary action).
- **UltraCloud dialogs** (`UltraCloud/ui/UltraCloudUiStyle.h`): the account
  dialog and the link picker use the same palette — captions, styled inputs,
  a quiet list header, secondary buttons and one primary action.

#### 2026-09-04 *0.6.0*
- **Account tiles grow with their counters.** The tile was a fixed 176×176 box,
  so a four- or five-digit unread count pushed the counter row past the rounded
  frame and the container clipped it. The tile's width is now **auto** with 176
  as a *minimum* (the height stays fixed), so the frame widens with the numbers
  while the letter, the address and the counter row stay centred on the tile's
  centre line. The counter row also centres explicitly
  (`JustifyContent::Center`) instead of relying on shrink-wrap.
- **Counters are rounded boxes, not pills.** `BadgeStyle::cornerRadius` (new,
  `-1` keeps the pill default) lets `UltraCanvasBadge` draw a rounded box; the
  account counters use an 8px radius so they echo the tile's rounded frame, as
  in the design.

#### 2026-09-03 *0.3.0*
- **Attachments in the composer.** "Attach file…" opens the file dialog and
  adds the file to the draft (media type guessed from the extension); the
  attachment strip under the body lists what is attached, and forwards carry
  the original's attachments there too. Attached files go out through the
  existing MIME builder.
- **"Attach cloud link…"** through the new **UltraCloud** module
  (`Docs/Modules/UltraCloud/README.md`): the picker lists the cloud accounts
  (default preselected), browses the chosen account, uploads a local file into
  the current folder, and puts a share link for the selected file into the
  body as "<name>: <url>". With no account yet it offers the add-account
  dialog (Nextcloud / ownCloud with password- and expiry-capable links, generic
  WebDAV, and an in-memory demo). Accounts live in `cloud.db` next to the mail
  store, secrets in `cloud-vault/`. `ULTRAMAIL_DEMO_CLOUD=1` seeds a demo
  account and opens the composer.
#### 2026-09-03 *0.5.0*
- **Mail account passwords now live in UltraVault.** The 0.1 credential vault
  XOR-ed each secret against a 32-byte key it wrote to `vault.key` **in the same
  directory as the ciphertext** — anyone who could read the vault folder could
  recover every mail password, and the file permissions were the only real
  control. Secrets now go through `UltraVault`
  (`UltraCanvas/include/UltraVault`), the framework's credential module, whose
  file backend derives its key from a passphrase with Argon2id and seals the
  store with XChaCha20-Poly1305 via UltraCrypt, authenticating the header so
  tampering with the stored cost parameters is detected rather than obeyed.
  UltraMail no longer implements a secret format of its own — the same module
  UltraNet, UltraDatabase and UltraAI resolve credentials through.
- **A master password guards the vault.** It is the passphrase the key is
  derived from and is never written to disk, so the stored secrets genuinely
  cannot be read without the user. UltraMail asks for it once per session —
  with confirmation the first time, when there is no vault yet — through the
  new `ui/UltraMailPassphraseDialog`. Cancelling leaves the vault locked rather
  than falling back to something weaker.
- Secrets under the 0.1 format are migrated on the first successful unlock and
  the old `creds.dat` / `vault.key` are deleted — but only once every secret is
  safely in the new vault, so a partial migration loses nothing.
- `CredentialVault` reports *why* an unlock failed (`VaultStatus`): a wrong
  master password re-prompts with the reason shown in the dialog, while a build
  without libsodium says so and stops instead of appearing to work. A wrong
  passphrase and a tampered vault are deliberately indistinguishable — that is
  UltraVault's no-oracle rule, and the message covers both.
- The vault is now a session-lifetime member of `UltraMailApp` (it was
  constructed per call site), and its derived key is wiped on shutdown. Sending
  and adding an account unlock in the foreground; the background sync timer
  never raises a password prompt over what the user is doing — it skips the
  round and says once that mail is not being fetched while the vault is locked.
- Alert helpers take an optional completion callback, so the add-account flow
  shows the discovery result and *then* asks for the master password instead of
  stacking one dialog under the other.
- Linking UltraVault pulls in UltraCrypt, which exposed a latent link-time
  collision in the framework's text utilities that broke the Windows build of
  both UltraMail and EmailCleaner. The fix is a framework change and is recorded
  in [`Docs/UltraCanvas/CHANGELOG.md`](../UltraCanvas/CHANGELOG.md) *0.3.95*;
  this release depends on it.

#### 2026-09-03 *0.4.0*
- **"Save As…" on an attachment now works.** The reading view's attachment strip
  raised its `onSaveAs` callback into nothing — the menu entry was inert — and
  `SaveAttachment()` ignored where the user wanted the file, writing blindly
  into the attachment cache instead. The strip's callback is now wired, and
  saving goes through `UltraCanvasFileLoader::SaveFileDialog`: the user picks
  the destination, the file is written there via the `AttachmentCache::SaveAs()`
  the engine already provided, the path is registered with the platform's
  recent-documents list, and the result is reported. The dialog opens on
  Downloads (falling back to home), pre-fills the sanitised attachment name, and
  offers the attachment's own media type as a filter.
- UltraMail now uses the framework's file loader at all: it previously called
  `UltraCanvasFileLoader` nowhere, while every other application in the tree
  uses it. `ULTRAMAIL_DEMO_SAVE=1` exercises the save dialog, matching the
  existing `ULTRAMAIL_DEMO_OPEN` demo path.
- Cached message bodies are read through `UltraCanvasFileLoader::LoadFile()`
  rather than a bare `ifstream`. A body that exists but cannot be read now says
  so and shows the reason, instead of being indistinguishable from one that was
  never downloaded — the "not downloaded" text now reads "not downloaded yet".
- Note on modules: plain local file and directory work continues to use
  `std::filesystem`, matching every other application in the tree. VirtualFS is
  the transparent-archive module (ZIP/7z/TAR as folders), not a filesystem
  wrapper, so it is not the right tool for writing the mail store; the file
  loader already routes through it for transparent decompression.

#### 2026-09-03 *0.3.0*
- **Failures are now reported instead of swallowed.** UltraMail used to fail
  silently almost everywhere: a rejected password, an untrusted certificate, an
  unreachable server, a database that would not open, an attachment that could
  not be written — each ended in a bare `return`, so the app simply appeared to
  do nothing. The diagnosis already existed in `UltraNetResult::message`,
  `UltraDbResult::message` and `SyncOutcome::message`; it was being dropped at
  the UI boundary. Every one of those paths now raises an alert that names the
  cause. This delivers the error-handling contract in `Concept.md`.
- Alerts are `UltraCanvasAlert` (`UltraCanvas/include/UltraCanvasAlert.h`), so a
  failure carries an Error severity and icon rather than the Information dialog
  a failed send used to show. The one-line summary goes in the alert's message
  and the underlying diagnostic in its `details` line.
- New `ui/UltraMailAlerts.{h,cpp}`: `FriendlyMessage()` maps the UltraNet result
  codes a mail client actually hits — `AuthenticationFailed`,
  `TlsCertificateInvalid`, `TlsCertificateExpired`, `HostNotFound`,
  `ConnectionRefused`, `PluginNotFound` and the rest — to text a user can act
  on, and `IsRetryable()` decides when a Retry button is offered.
- A failed send now offers **Retry**, which re-flushes the outbox, rather than
  only stating that the message was queued.
- Input is validated where it is entered: sending with no recipient, adding an
  account with an empty or malformed address, and saving a nameless contact each
  explain what is wrong instead of discarding what was typed. New pure helper
  `LooksLikeEmailAddress()` beside `EmailDomain()` / `EmailLocalPart()` in the
  discovery engine.
- Background sync failures surface. `RunDueSyncs()` discarded its `SyncOutcome`
  entirely, so a wrong password or an expired certificate meant mail silently
  never arrived. The outcome is now reported once per run of failures — not on
  every timer tick — and re-arms when a sync succeeds again.
- A startup failure shows an alert naming the data folder and the database error
  before exiting, instead of terminating with no window and no message.
- `Outbox::FlushStats` carries `lastFailure` (the `UltraNetResult` of the most
  recent failed send) so the UI can say *why* a message stayed in the queue; the
  reason was already persisted as the outbox row's `last_error` but was never
  read back. `OutboxStore::IsOpen()` added to mirror `LocalStore` /
  `ContactStore`, so "the queue is unavailable" is distinguishable from "the
  queue is empty".
- `UltraMailApp::Initialize()` takes an optional `outError` and records why the
  contacts / outbox stores failed to open, so the Contacts button reports the
  problem rather than doing nothing.

#### 2026-09-03 *0.2.0*
- **First run shows a start page, nothing else.** Until the first email account
  exists the main window holds only the UltraMail logo, the app title and an
  "Add email account" button (`UltraMailStartPage`). The old "Welcome to
  UltraMail. Add an account to begin." hint is gone. The button is a
  primary-style `UltraCanvasButton` with the envelope icon; the page is a
  centred flex column that follows the window size.
- **The main window is one screen: actions · account bar · inbox | message.**
  The Toolbox grid, the info-tile bar and the separate three-pane reading
  window are replaced by a single account view (`UltraMailApp::BuildAccountView`,
  a flex column sized to the window):
  - an **actions column** — New email, Reload email, Contacts, Add account;
  - the **account bar** (`UltraMailAccountBar`): with one account a summary
    strip showing the provider's initial (first letter of the address's domain,
    upper-case, bold), the account name (local part; full address in the
    tooltip) and three `UltraCanvasBadge` counters — New today (blue), Unread
    before today (lime), Waiting for reply (orange); with several accounts a
    row of square tiles carrying the same information, the clicked tile
    (selection-blue frame) driving the mail view;
  - the **mail view** (`UltraMailMailView`): an `UltraCanvasSplitPane` with an
    "Inbox" group box holding the selected account's inbox as an
    `UltraCanvasColumnsTreeView` (From · Subject · Date, `●` unread, `↩`
    waiting for reply, counts in the caption) and a "Message" group box holding
    the `UltraMailMessagePreview` (subject, from, to, date, Reply, the body —
    HTML through HTMLReader / CSSLayout, plain text in a read-only text area —
    and the attachment strip). The preview is the reading view's pane, moved
    into its own class; `UltraMailReadingView`, `UltraMailToolbox` and
    `UltraMailInfoTileBar` are removed.
- **Reload email** syncs every account immediately when the IMAP plug-in is
  loaded (the button reads "Reloading…" until the last sync returns) and
  re-reads the store either way.
- **Engine: `GetAccountStatus` splits unread into today / before today** and
  carries the account's email (`AccountStatus::unreadToday`, `unreadOlder`,
  `email`); an optional `todayStart` argument pins local midnight for tests.
- **UltraMail has an app icon** (`media/appicon/UltraMail.svg`): the envelope on
  the selection blue. It is the start-page logo and the window icon.
- Not used: `UltraCanvasTableView` does not compile in this tree (it is unused
  by every other target); the inbox list is a `UltraCanvasColumnsTreeView`.

#### 2026-08-31 *0.1.0*
- **UltraMail keeps its own changelog from here.** Everything up to and
  including this version shipped as part of a framework release and is recorded
  in [`Docs/UltraCanvas/CHANGELOG.md`](../UltraCanvas/CHANGELOG.md) — nothing
  was rewritten or moved, so that history stays where it was published. From
  now on a change to the mail client (`Apps/UltraMail`) is described here and
  carries this file's version, and UltraMail no longer moves when the framework
  releases.
- A framework change UltraMail needs still belongs in the framework changelog.
  Cross-reference it from here when a release depends on it; never describe one
  change in two files under two version numbers.

<!--
Version source of truth: the first line of this file, format
`#### YYYY-MM-DD *x.y.z*`, read by cmake/UltraCanvasVersion.cmake.
-->
