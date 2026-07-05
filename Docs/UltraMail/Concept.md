# UltraMail — Application Concept and Design

UltraMail is the mail application of the ULTRA OS application family
(UltraTexter, UltraFiler, ULTRA Store, …). It is a full desktop e-mail
client built entirely on UltraCanvas for the UI and on the **UltraNet**
module (SMTP/IMAP/POP3 protocol plugins, DNS, HTTP) for all networking.

Two goals drive every design decision in this document:

1. **Easy to set up.** A new user should get from "empty program" to
   "reading their inbox" by entering nothing more than their e-mail
   address and password. Everything else — server names, ports, TLS
   modes — is discovered automatically.
2. **Easy to use.** The everyday feature set of a comfortable mail
   client (read, write, reply, forward, attachments, folders, search,
   multiple accounts, signatures, address suggestions, notifications)
   presented in a classic, instantly familiar three-pane layout with no
   configuration required to be productive.

> **Status (2026-07):** concept/design document. Nothing of UltraMail is
> implemented yet. UltraNet already ships working `smtp`, `imap` and
> `pop3` plugins (`UltraCanvas/Plugins/UltraNet/{smtp,imap,pop3}/`)
> implementing `IMailProtocolPlugin` (`SendMail`, `FetchMessages`);
> section 6 lists the extensions UltraMail needs from them.

---

## 1. Design principles

- **Zero-knowledge setup.** The user knows their address and password;
  UltraMail figures out the rest (section 2.2). Manual server settings
  exist but are an *expert fallback*, never the first screen.
- **Safe defaults.** TLS verification on, minimum TLS 1.2 (UltraNet's
  defaults), remote images in HTML mail blocked until the user allows
  them, credentials in the OS keychain.
- **Local-first.** Mail is synchronized into a local store; reading,
  searching and composing work offline. The network is a background
  activity, never something the UI waits for.
- **UltraCanvas-native.** Only framework widgets and patterns: factory
  functions, `Config` structs, `onXxx` callbacks, the Texter
  manager-class app composition style. One codebase, all platforms.
- **UltraNet-only networking.** UltraMail never opens a socket itself.
  Everything goes through UltraNet: mail plugins for SMTP/IMAP/POP3,
  `UltraNet_DnsResolveAsync` for autodiscovery, `UltraNet_HttpRequestAsync`
  for autoconfig lookup, LDAP/WebDAV plugins later for address books.

## 2. First-run experience

### 2.1 The Toolbox start screen

UltraMail opens onto its **Toolbox** — a start screen showing a grid of
large square tiles, one per configured e-mail account, in the visual
language planned for the ULTRA OS launcher.

**On first use the Toolbox contains exactly one square icon:**

```
┌──────────────────────────────────────────────────────────┐
│  UltraMail                                          _ □ ✕ │
├──────────────────────────────────────────────────────────┤
│                                                          │
│                    ┌─────────────┐                       │
│                    │             │                       │
│                    │      ✉ +    │                       │
│                    │             │                       │
│                    │  Add email  │                       │
│                    │   account   │                       │
│                    └─────────────┘                       │
│                                                          │
│        Welcome to UltraMail. Add an account to begin.    │
└──────────────────────────────────────────────────────────┘
```

Clicking the tile starts the account setup wizard (2.2). Once at least
one account exists, each account gets its own square tile (provider
glyph, address, unread-count badge) and the "Add email account" tile
moves to the end of the grid — it is always present, so adding a second
account is the same gesture as adding the first. Opening an account
tile enters the main mail window (section 3); the Toolbox stays
reachable via a home button in the main toolbar.

With a single configured account UltraMail skips the Toolbox on
subsequent launches and opens the mail window directly (the behaviour
is a settings option: *Start on: Toolbox / Last account*).

Implementation: the Toolbox is a plain `UltraCanvasContainer` with a
grid of `CreateIconButton(...)` tiles (or a `UltraCanvasToolbar` in
`Docked` style with large icons); badges use the toolbar's built-in
`badgeText` support.

### 2.2 Account setup wizard

One small modal dialog (`UltraCanvasModalDialog`), three steps, most
users only ever see step 1:

**Step 1 — identity.**
```
  Your name       [ Erika Example              ]   (prefilled from OS user)
  Email address   [ erika@example.com          ]   CreateEmailInput
  Password        [ ••••••••••                 ]   CreatePasswordInput
                                    [ Cancel ] [ Continue ]
```

**Step 2 — automatic discovery** (progress screen, usually 1–3 s).
The discovery pipeline runs in the engine thread and tries, in order:

1. **Provider presets** — a small built-in table of the most common
   providers (Gmail, Outlook/Office365, Yahoo, iCloud, GMX, web.de,
   mail.ru, Posteo, mailbox.org, …) keyed by address domain. Instant.
2. **Autoconfig lookup** — `UltraNet_HttpRequestAsync` to
   `https://autoconfig.<domain>/mail/config-v1.1.xml` and the Mozilla
   ISPDB (`https://autoconfig.thunderbird.net/v1.1/<domain>`). Covers
   most hosted domains.
3. **DNS probing** — `UltraNet_DnsResolveAsync` for SRV records
   (`_imaps._tcp.<domain>`, `_submission._tcp.<domain>`, RFC 6186) and
   MX as a hint for the mail host.
4. **Educated guessing** — try `imap.<domain>`/`mail.<domain>` on 993
   and `smtp.<domain>`/`mail.<domain>` on 465/587 with the mail plugins,
   accepting the first combination whose TLS handshake and login
   succeed.

The wizard then verifies the discovered settings with one real IMAP
login and one SMTP authentication (no message is sent). On success:

**Step 3 — done.** "Your account is ready." The wizard closes, the
account tile appears, and a first synchronization of the inbox starts
immediately in the background.

If discovery or verification fails, the wizard drops into the
**manual settings** page (incoming server/port/TLS/IMAP-or-POP3,
outgoing server/port/TLS, username) with everything it *did* find
prefilled — the user corrects one field instead of typing seven.

### 2.3 What the user never has to do

No port numbers, no "SSL vs STARTTLS" choice, no separate username
field (defaults to the address), no folder mapping (Sent/Drafts/Trash
detected via IMAP SPECIAL-USE with name-based fallback), no manual
"test settings" button — verification is part of the flow.

## 3. Main window

Classic three-pane layout built from nested split panes:

```
┌───────────────────────────────────────────────────────────────────┐
│ ☰  🏠  ✎ Write   ⟳ Get Mail   ↩ Reply  ↪ Forward  🗑  [Search… 🔍] │ ← UltraCanvasToolbar
├───────────────────────────────────────────────────────────────────┤
│ ┌── erika ──────┐ ┌── work ───────┐ ┌── shop ───────┐             │
│ │ ✉ 3 new       │ │ ✉ 12 new      │ │ ✉ 0 new       │   ← account │
│ │ ↩ 2 to answer │ │ ↩ 5 to answer │ │ ✓ all clear   │     bar (3.0)│
│ └───────────────┘ └───────────────┘ └───────────────┘             │
├──────────────┬────────────────────────────────────────────────────┤
│ Unified Inbox│  ● From          Subject                 Date    📎 │
│ ▾ erika@…    │  ○ Anna Schmidt  Re: Meeting notes       14:02     │
│    Inbox (3) │  ● ULTRA Store   Your order shipped      11:47   📎 │ ← UltraCanvasListView
│    Drafts    │  ○ Max Weber     Lunch on Friday?        Yesterday │
│    Sent      ├────────────────────────────────────────────────────┤
│    Junk      │  Re: Meeting notes                                 │
│    Trash     │  Anna Schmidt <anna@…>          Tue 14:02  [Reply] │
│  ▸ Archive   │ ────────────────────────────────────────────────── │
│ ▸ max@work…  │  Hi Erika,                                         │ ← preview pane
│              │  here are the notes from …                         │   (HTMLReader)
├──────────────┴────────────────────────────────────────────────────┤
│ ⟳ Synchronizing erika@… Inbox (12/340)              3 new messages │ ← status bar
└───────────────────────────────────────────────────────────────────┘
```

### 3.0 Account info bar

Once one or more accounts are set up, a horizontal strip of **account
info tiles** sits at the top of the main window, directly under the
toolbar — one tile per configured account, always visible. Each tile is
a compact status card, not a big launcher square (those live on the
Toolbox start screen, 2.1); here the point is at-a-glance triage across
all accounts at once.

Each tile shows three things:

- **Short account name** — a user-editable nickname (`erika`, `work`,
  `shop`), defaulting to the local-part of the address. Kept short so
  many tiles fit; full address in the tooltip.
- **New unread** — count of unread messages across the account's inbox
  (folders the user marks as "watched" can be included). "`✉ 3 new`".
- **Needs answer** — count of messages that were addressed to the user
  and have not yet been replied to. "`↩ 2 to answer`". When both counts
  are zero the tile shows a calm "`✓ all clear`" state.

Behaviour:

- **Colour / attention state.** A tile with mail needing an answer is
  accented (warm accent); unread-only is a lighter accent; all-clear is
  neutral. This makes "which account needs me right now" readable
  without counting.
- **Click a tile** → selects that account and jumps its Inbox into the
  list pane (folder tree scrolls/expands to it). **Click the "needs
  answer" line** → applies a *Needs answer* filter to the list (a saved
  search over that account). This turns the tiles into one-click triage
  entry points, not just indicators.
- **Live updates.** Counts update from the SyncEngine as new mail
  arrives (IMAP IDLE) and as the user reads/replies — the engine pushes
  deltas to the UI via `PostToUIThread`. The same numbers drive the
  Toolbox tile badges (2.1) and the OS window/taskbar badge, computed
  once in the engine.
- **Overflow.** With more accounts than fit, the strip scrolls
  horizontally (or the tiles shrink to name + two small numbers); a
  settings option collapses the bar to a single row of counters or
  hides it for single-account users.

Implementation: a horizontal `UltraCanvasContainer` (or a `UltraCanvasToolbar`
in `Sidebar`/`Docked` style laid out horizontally) holding one
delegate-drawn tile per account; tiles are `CreateIconButton`-style
clickable panels with two badge lines. The bar is a natural first
consumer of a future `UltraCanvasTileGrid`/tile component (see open
question 1), but v1 hand-lays the row — the count is small.

**Defining "needs answer".** A message counts as *needs answer* when
all of these hold, computed by the engine and cached in the message
index:

1. It is **incoming** (not sent by one of the user's own addresses).
2. The user is a **direct recipient** — their address is in `To:`
   (optionally `Cc:`); pure bulk/list mail where they are neither is
   excluded.
3. It is **not yet answered** — this maps directly onto the IMAP
   `\Answered` system flag, which the server sets when a reply is sent
   and which the proposed `IMailboxProtocolPlugin::StoreFlags` already
   manages. UltraMail sets `\Answered` locally the moment the user sends
   a reply (matched by `In-Reply-To`/`References`), so the count drops
   instantly and stays correct across devices.
4. It is **not obviously automated** — messages with
   `List-Id`/`List-Unsubscribe`, `Precedence: bulk/list`, or
   `Auto-Submitted` headers, and mail in Junk/Trash, are excluded.
5. Optionally, not older than a configurable window (default: no age
   limit; a "hide older than 30 days" option keeps the count actionable).

The user can always override per message: a **"Mark as answered / needs
answer"** command toggles the `\Answered` flag by hand (for mail
answered by phone, or that needs a follow-up). "Needs answer" is thus a
real, synced mailbox state, not a fragile local guess.

- **Folder pane** — `UltraCanvasTreeView`; one root node per account
  plus a *Unified Inbox* node when more than one account exists; unread
  counts rendered in the node label; context menu for folder
  operations (new/rename/delete/empty trash).
- **Message list** — `UltraCanvasListView` with delegate-drawn rows
  (unread dot, sender, subject, smart date, attachment glyph, flag
  star); sortable; multi-select; context menu (reply, forward, move,
  mark, delete). Optional conversation grouping in v1.x.
- **Preview pane** — HTML mail rendered with the `HTMLReader` subsystem
  (`HTMLParser`/`HTMLDocument`/`CSSStyleSheet`) inside a scrollable
  view; plain-text mail in a read-only `UltraCanvasTextArea`. A yellow
  info bar offers "Load remote images" per message/sender. Attachment
  strip with icon, name, size — open (via FileLoader/OS) or save.
- **Toolbar** — built with `UltraCanvasToolbarBuilder`: home (back to
  Toolbox), write, get-mail, reply/reply-all/forward, delete, junk,
  and an `AddSearchBox` searching the local store as you type.
- **Layout** — `CreateHorizontalSplitPane(folders, CreateVerticalSplitPane(list, preview))`;
  divider positions persisted. A settings option switches to a wide
  "list left / mail right" arrangement.

### 3.1 Compose window

A separate window (`CreateWindow`) per draft, Texter-style:

- **To/Cc/Bcc** rows using `CreateEmailInput` with autocomplete fed by
  the collected-addresses book (3.2); recipients render as removable
  chips; Cc/Bcc rows hidden until requested.
- **Body** — `UltraCanvasTextArea` in `MarkdownHybrid` mode: the user
  writes comfortable rich text (bold, lists, links, quoting) and
  UltraMail sends `multipart/alternative` with a generated HTML part
  (via the existing Markdown→HTML serializer of `UCRichDocument`) plus
  the plain-text source. A "plain text only" toggle is one click.
- **Attachments** — add via button (`CreateFileDialog`) or drag & drop;
  shown as a chip strip with sizes and a total-size warning.
- **Signatures** — per account, appended automatically, editable in
  settings.
- **Drafts** — autosaved to the local store every few seconds and on
  close; **Send** puts the message into the **Outbox** queue, which the
  engine flushes when online (so send never blocks and survives
  restarts); optional "undo send" delay of 0–30 s.

### 3.2 Everyday comfort features (v1.0)

- Reply / reply-all / forward with proper quoting and subject prefixes
- Mark read/unread, star/flag, move to folder (drag & drop onto tree)
- Junk folder support; delete-to-Trash with empty-trash command
- Fast local search (from/subject/body) over the message index
- Collected address book: every address the user writes to or receives
  from is remembered for autocomplete (explicit contacts app and
  LDAP/CardDAV integration come later, see roadmap)
- Multiple accounts + unified inbox
- New-mail notifications (window badge + system notification) and
  unread badges on the Toolbox tiles
- Keyboard shortcuts (N new, R reply, Del delete, Ctrl+Enter send, …)
- Dark/light theme following the framework theme, HiDPI-ready

## 4. Architecture

```
┌────────────────────────────────────────────────────────────┐
│ UltraMail UI (UI thread)                                   │
│  Toolbox · MailWindow (3-pane) · ComposeWindow · Wizard    │
│  UltraCanvas widgets, HTMLReader for message rendering     │
├────────────────────────────────────────────────────────────┤
│ UltraMail Engine (worker thread(s))                        │
│  AccountManager      accounts, settings, credentials       │
│  AutoDiscovery       presets → autoconfig → DNS → probing  │
│  SyncEngine          per-account folder/message sync, IDLE │
│  LocalStore          .eml files + UltraDatabase index/meta │
│  MimeCodec           MIME parse/build, encodings, HTML/text│
│  Outbox              persistent send queue, retries        │
├──────────────────────────────┬─────────────────────────────┤
│ UltraNet                     │ UltraDatabase               │
│  IMailProtocolPlugin: smtp · │  local SQLite connection    │
│  imap · pop3 (libcurl)       │  per account: index, flags, │
│  UltraNet_Dns* (SRV/MX)      │  needs-answer, search, meta │
│  UltraNet_Http* (autoconfig) │  UltraDb_Query/Exec/Migrate │
│  TLS on by default           │  parameterized, transactional│
└──────────────────────────────┴─────────────────────────────┘
```

**Threading.** All protocol work runs on engine worker threads (one
sync worker per account, one shared discovery/outbox worker). Results
are marshalled to the UI with
`UltraCanvasApplication::PostToUIThread(fn)` — the same pattern UltraNet
prescribes for its async callbacks, which fire on its worker thread.
The UI never blocks on the network.

**Engine ↔ UltraNet.** The engine resolves plugins through the
registry: `UltraNet_GetPlugin("imaps")` /
`UltraNet_GetPlugin("smtps")`, then calls the `IMailProtocolPlugin`
interface with `UltraNetMailOptions` (credentials, `serverUrl`,
TLS mode, timeouts) exactly as defined in
`UltraCanvas/include/UltraNet/UltraNetPlugins.h`. Error handling
follows the framework convention: every operation returns an
`UltraNetResult`; the UI presents `result.message` with a retry option
and maps common codes (`AuthenticationFailed`,
`TlsCertificateInvalid`, `HostNotFound`) to friendly texts.

**Local store.** One directory per account under the user data dir
(via VirtualFS):

```
~/.local/share/UltraMail/           (per-platform user-data dir)
  accounts.json                     account list & settings (no secrets)
  <account-id>/
    folders.json                    folder tree, UIDVALIDITY, sync state
    <folder>/cur/<uid>.eml          raw RFC 5322 messages (Maildir-like)
    index.db                        flat message index: envelope fields,
                                    flags (incl. \Answered), needs-answer
                                    bit, threading refs, search tokens
  status.json                       per-account rollups: unread & needs-
                                    answer counts driving the account bar
  outbox/                           queued outgoing messages
```

Raw `.eml` files keep the store transparent, portable and crash-safe;
the compact index makes list rendering and search fast without loading
message bodies. The index, flags, needs-answer bits, threading data,
search tokens and per-account status rollups all live in a per-account
**UltraDatabase** connection (a local SQLite database, `driver =
"sqlite"`), registered by name (`"mail-<account-id>"`) and queried
through the `UltraDb_*` API — see `Docs/Modules/UltraDatabase/README.md`.
This gives UltraMail parameterized queries, transactional sync updates
and fast indexed search for free, and means a power user could later
point the store at a networked engine without changing UltraMail's
query code. The raw messages stay as `.eml` files on disk; only the
index/metadata lives in the database.

**Credentials** go into the OS keychain (libsecret on Linux, Windows
Credential Manager, macOS Keychain) behind a small
`UltraMailCredentialVault` seam per `OS/` platform; fallback is an
encrypted file with a clear warning. `accounts.json` never contains
passwords.

## 5. Application skeleton and file layout

Follows the Texter pattern (app manager class owning windows, config
structs, `onXxx` callbacks):

```
Apps/UltraMail/
  main.cpp                        app init, splash, Toolbox vs. mail window
  UltraMailApp.{h,cpp}            manager class (accounts, windows, engine)
  ui/
    UltraMailToolbox.{h,cpp}      square-tile start screen (2.1)
    UltraMailWindow.{h,cpp}       3-pane main window (3)
    UltraMailComposer.{h,cpp}     compose window (3.1)
    UltraMailAccountWizard.{h,cpp}setup wizard (2.2)
    UltraMailSettings.{h,cpp}     settings dialog
  engine/
    UltraMailAccountManager.{h,cpp}
    UltraMailAutoDiscovery.{h,cpp}
    UltraMailSyncEngine.{h,cpp}
    UltraMailLocalStore.{h,cpp}
    UltraMailMimeCodec.{h,cpp}
    UltraMailOutbox.{h,cpp}
    UltraMailCredentialVault.{h,cpp}
  CMakeLists.txt                  links UltraCanvas + UltraNet (+ mail plugins)
media/appicon/UltraMail.png       app icon (used by SetDefaultWindowIcon)
Docs/UltraMail/Concept.md         this document
Docs/UltraMail/CHANGELOG.md       once implementation starts
```

## 6. Required UltraNet extensions (gap analysis)

> **Implemented (2026-07):** the `IMailboxProtocolPlugin` extension below
> is now in UltraNet — `UltraCanvas/include/UltraNet/UltraNetPlugins.h`
> defines the interface plus `UltraNetMailFolder` / `UltraNetMailEnvelope`
> / `UltraNetMailboxStatus` / `UltraNetMailFlags`, and the IMAP plug-in
> (`Plugins/UltraNet/imap/`) implements all of it over libcurl
> (`LIST`, `STATUS`, `UID SEARCH`/`FETCH`/`STORE`/`MOVE`, `APPEND`), with
> **XOAUTH2** bearer auth for Gmail/Microsoft. Wire parsing is factored
> into a pure header (`ImapParse.h`) and unit-tested in
> `Tests/UltraNet/test_imap_mailbox.cpp`. The signatures below are the
> original proposal; the shipped API takes the server URL + folder as
> separate arguments and adds `GetMailboxStatus` (poll-based change
> detection) in place of a push `IdleStart` (IDLE remains a future
> optimisation).

The pre-existing mail plugins were deliberately minimal (~300 lines each):
`SmtpPlugin` sends a complete message, `ImapPlugin.FetchMessages` does
`SEARCH ALL` + `FETCH BODY[]` of the N most recent UIDs into
`UltraNetMailMessage` structs, `Pop3Plugin` similarly. That is enough
for the wizard's login verification and a first read-only inbox, but a
real client needs more — so the mail plugin surface was extended with a
second interface (keeping `IMailProtocolPlugin` intact for
compatibility):

```cpp
// UltraNetPlugins.h — proposed
class IMailboxProtocolPlugin : public IMailProtocolPlugin {
public:
    virtual UltraNetResult ListFolders(std::vector<UltraNetMailFolder>& out,
                                       const UltraNetMailOptions&) = 0;      // LIST + SPECIAL-USE
    virtual UltraNetResult FetchEnvelopes(const std::string& folder,
                                          uint32_t sinceUid,
                                          std::vector<UltraNetMailEnvelope>& out,
                                          const UltraNetMailOptions&) = 0;   // headers/flags only
    virtual UltraNetResult FetchMessage(const std::string& folder, uint32_t uid,
                                        std::string& outRaw,
                                        const UltraNetMailOptions&) = 0;     // full RFC 5322 body
    virtual UltraNetResult StoreFlags(const std::string& folder, uint32_t uid,
                                      const std::string& flags, bool set,
                                      const UltraNetMailOptions&) = 0;       // \Seen \Flagged \Deleted
    virtual UltraNetResult MoveMessage(const std::string& srcFolder, uint32_t uid,
                                       const std::string& dstFolder,
                                       const UltraNetMailOptions&) = 0;
    virtual UltraNetResult AppendMessage(const std::string& folder,
                                         const std::string& raw,
                                         const UltraNetMailOptions&) = 0;    // Sent/Drafts upload
    virtual UltraNetHandle IdleStart(const std::string& folder,
                                     std::function<void()> onChange,
                                     const UltraNetMailOptions&) = 0;        // IMAP IDLE push
    virtual UltraNetResult IdleStop(UltraNetHandle) = 0;
};
```

All of this maps onto libcurl custom IMAP commands the same way the
current plugin implements `SEARCH`/`FETCH`, so it stays within the
established implementation approach. Additional gaps to close:

- **MIME handling** — `FetchMessages` currently returns raw bodies;
  full MIME parsing/building (multipart, encodings, inline images,
  attachments) lives in UltraMail's `MimeCodec` so the plugins stay
  thin transport.
- **OAuth2 / XOAUTH2** — required for Gmail and Microsoft accounts.
  `UltraNetCredentials` gains a bearer-token field (libcurl supports
  `CURLOPT_XOAUTH2_BEARER`); the browser-based token flow is
  implemented in UltraMail using `UltraNet_Http*` plus a localhost
  redirect listener on `UltraNet_TcpListen`.
- **Cancellation/progress** for long fetches — reuse the existing
  `UltraNet_CancelRequest`/transfer-callback pattern.

POP3 accounts are supported as leave-on-server single-folder accounts
(Inbox only, sent mail stored locally); IMAP is the recommended and
default path.

## 7. Implementation roadmap

| Phase | Deliverable |
|---|---|
| **1 — Walking skeleton** | App skeleton, Toolbox with the single "Add email account" tile, setup wizard with discovery pipeline, credential vault; verify login; read-only inbox using existing `FetchMessages`; plain-text preview; SMTP send with a minimal composer. *Usable end-to-end.* |
| **2 — Real mail client** | `IMailboxProtocolPlugin` in UltraNet (folders, envelopes, flags incl. `\Answered`, move, append, IDLE); SyncEngine + LocalStore with offline cache; account info bar (3.0) with unread + needs-answer counts and one-click triage; folder tree, flags, delete/junk/move; MimeCodec; HTML rendering with remote-image blocking; attachments both directions. |
| **3 — Comfort** | Unified inbox, local search, drafts/outbox/undo-send, signatures, address autocomplete, notifications and badges, settings dialog, keyboard shortcuts, conversation view. |
| **4 — Ecosystem** | OAuth2 (Gmail/Microsoft), address book app integration (LDAP via UltraNet's `IDirectoryProtocolPlugin`, CardDAV), filter rules, PGP/S-MIME signing & encryption, calendar-invite (.ics) preview, mobile-profile export for ULTRA OS on Android. |

Phases 1–2 are the minimum for a public preview; phase 3 completes the
"comfortable mail sending and managing" goal of this concept.

## 8. Open questions

1. Should the Toolbox tile grid become a reusable UltraCanvas component
   (`UltraCanvasTileGrid`)? Other ULTRA OS apps (and the future OS
   launcher itself) will want the same square-icon visual language.
2. ~~Message index backend: flat files vs SQLite?~~ **Resolved:** the
   index/metadata uses a per-account SQLite connection via the new
   **UltraDatabase** module (`Docs/Modules/UltraDatabase/README.md`);
   raw messages stay as `.eml` files. `LocalStore` wraps the `UltraDb_*`
   calls so the engine choice stays swappable.
3. Conversation threading in v1.0 message list or v1.x?
4. Does the ISPDB lookup need a privacy switch (it reveals the user's
   mail domain to Mozilla's service)? Suggested default: on, with an
   opt-out in the wizard's "manual settings" page.

## See Also

- `Docs/Modules/UltraNet/README.md` — UltraNet module overview
- `UltraCanvas/include/UltraNet/UltraNetPlugins.h` — `IMailProtocolPlugin`,
  `UltraNetMailMessage`, `UltraNetMailOptions`
- `UltraCanvas/Plugins/UltraNet/{smtp,imap,pop3}/` — existing mail plugins
- `Apps/Texter/` — reference application structure
- `Docs/Modules/UltraDatabase/README.md` — local index/metadata store
- `Docs/Modules/ULTRA-OS/README.md` — ULTRA OS ecosystem vision
