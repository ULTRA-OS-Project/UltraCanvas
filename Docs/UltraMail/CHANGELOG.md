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
