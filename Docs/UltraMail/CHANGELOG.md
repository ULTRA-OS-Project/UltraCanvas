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
