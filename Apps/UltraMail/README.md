# UltraMail

The ULTRA OS mail application. Full concept and design:
[`Docs/UltraMail/Concept.md`](../../Docs/UltraMail/Concept.md).

UltraMail is built on **UltraCanvas** (UI) and the **UltraNet** (SMTP/IMAP/POP3)
and **UltraDatabase** (local store) modules.

> **Status (Phase 1–2, in progress):** the headless **engine** is implemented
> and tested — LocalStore, MIME codec + attachment cache, contact store, and
> the **SyncEngine** that drives an IMAP mailbox into the store (folders,
> incremental envelopes with needs-answer, cached bodies, flag mirroring). The
> **UI** has the main window (Toolbox + account info-tile bar), the setup
> wizard, the attachment strip → MediaViewer, and the contact manager. Still to
> come: the discovery/verify pipeline + credential vault behind the wizard, the
> three-pane reading view, and the composer.

## Layout

```
Apps/UltraMail/
  engine/                         headless — depends on UltraDatabase + UltraNet
    UltraMailTypes.{h,cpp}        Account / Folder / MessageEnvelope, flags,
                                  FolderRole, AccountStatus (info-tile rollup)
    UltraMailLocalStore.{h,cpp}   account/folder/message index on UltraDatabase:
                                  schema migrations, upserts, flag updates,
                                  the needs-answer rule, per-account rollups
    UltraMailMimeCodec.{h,cpp}    raw message -> display body + attachments
                                  (wraps UltraNet_MimeParse)
    UltraMailAttachmentCache.{h,cpp} attachment bytes -> sanitised cache file
                                  (so a path-based viewer can open it)
    UltraMailContacts.{h,cpp}     Contact / email / phone types + sections
                                  (Family / Friends / Work / Leisure / Services)
    UltraMailContactStore.{h,cpp} the address book on UltraDatabase: sectioned
                                  contacts, emails/phones, counts, search
    UltraMailSyncEngine.{h,cpp}   drives an IMailboxProtocolPlugin (IMAP) into
                                  LocalStore: folders, incremental envelopes,
                                  .eml body cache, two-sided flag changes
    UltraMailDiscovery.{h,cpp}    account auto-discovery: provider presets +
                                  Mozilla-autoconfig XML (over UltraNet HTTP)
    UltraMailCredentialVault.{h,cpp} per-account secrets out of the config
                                  (obfuscated file backend; OS-keychain-ready)
  ui/                             UltraCanvas UI layer
    UltraMailApp.{h,cpp}          app manager: owns store + window, wires it up
    UltraMailToolbox.{h,cpp}      start-screen grid: account tiles + "Add
                                  email account" square (the only tile at first use)
    UltraMailInfoTileBar.{h,cpp}  per-account status strip (short name · unread
                                  · needs-answer), fed by GetAccountStatus
    UltraMailAccountWizard.{h,cpp} setup wizard dialog (identity step)
    UltraMailAttachmentStrip.{h,cpp} attachment chips; double-click or right-click
                                  (Open / Save As…) opens content in UltraCanvasMediaViewer
    UltraMailContactsView.{h,cpp} contact manager: section sidebar (with counts) +
                                  contact list; add/edit dialog; delete via context menu
  main.cpp                        entry point: init app, open store, show window
  CMakeLists.txt                  UltraMailEngine static library
```

**Attachments:** a message's MIME parts are decoded by `MimeCodec` (over
`UltraNet_MimeParse`); the attachment strip shows one chip per part.
Double-clicking a chip — or the right-click **Open** — writes the bytes to the
cache and opens them in **`UltraCanvasMediaViewer`** (images, PDF, text,
audio/video, …). Try it: run with `ULTRAMAIL_DEMO=1` (and
`ULTRAMAIL_DEMO_OPEN=1` to auto-open) to exercise the flow without a live sync.

**Contacts:** the address book (`ContactStore` on UltraDatabase) organises
contacts into **Family / Friends / Work / Leisure / Services** sections, each
contact carrying name, any number of emails and phones, an organization and
notes. Open it from the main window's **Contacts** button: a section sidebar
with live counts, the contact list for the selected section (name · email ·
phone), an add/edit dialog (name / email / phone / organization / notes), and
delete from a row's right-click menu. `ULTRAMAIL_DEMO_CONTACTS=1` seeds a few
contacts and opens the view.
It is a global (account-independent) store, so it can be promoted to a shared
`UltraContacts` module later if a dialer / calendar wants it.

The GUI executable target `UltraMail` (root CMake, `-DBUILD_ULTRAMAIL_APP=ON`)
links the full UltraCanvas UI library. The engine and its tests build without a
display; the GUI needs the UI toolkit.

**Account setup:** the wizard collects name / email / password; on submit,
`AutoDiscovery` resolves the incoming (IMAP) and outgoing (SMTP) servers from
the address — instant offline provider presets (Gmail, Outlook, Yahoo, iCloud,
GMX, web.de, mailbox.org, Posteo, …), falling back to a Mozilla-autoconfig /
ISPDB lookup over UltraNet HTTP. The password (or OAuth token) is stored in the
`CredentialVault`, never in the config. Try it: run with
`ULTRAMAIL_DEMO_ADD=you@gmail.com` to exercise discovery + vault + the result
dialog.

## What the engine provides now

- **Accounts / folders / messages** persisted through UltraDatabase (SQLite),
  schema managed by versioned migrations.
- **"Needs answer" state** — a message counts when it is addressed to the
  account owner, is not automated/bulk, sits in the inbox, and carries no
  `\Answered` flag. Sending a reply (or `MarkAnswered`) clears it. This is the
  data behind the account info-tile bar's "↩ N to answer" line.
- **Per-account status rollup** (`GetAccountStatus`) — short name, unread and
  needs-answer counts across all accounts, in one query. Directly powers the
  info-tile bar and drives the Toolbox tile badges / OS taskbar badge.

## Building & testing

The engine is headless, so it builds and tests without a display:

```sh
cmake -S . -B build -DULTRACANVAS_BUILD_ULTRAMAIL_TESTS=ON
cmake --build build --target UltraMailEngineTests
ctest --test-dir build -R UltraMailEngine --output-on-failure
```
