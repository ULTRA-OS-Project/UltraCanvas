# UltraMail

The ULTRA OS mail application. Full concept and design:
[`Docs/UltraMail/Concept.md`](../../Docs/UltraMail/Concept.md).

This app versions itself: [`Docs/UltraMail/CHANGELOG.md`](../../Docs/UltraMail/CHANGELOG.md).

UltraMail is built on **UltraCanvas** (UI) and the **UltraNet** (SMTP/IMAP/POP3)
and **UltraDatabase** (local store) modules.

> **Status (Phase 1–2, in progress):** the headless **engine** is implemented
> and tested — LocalStore, MIME codec + attachment cache, contact store,
> account auto-discovery + credential vault, and the **SyncEngine** that drives
> an IMAP mailbox into the store (folders, incremental envelopes with
> needs-answer, cached bodies, flag mirroring). The **UI** has the main window
> (a first-run **start page** — logo, title, "Add email account" — until the
> first account exists; then the **account bar** — one summary strip with the
> provider initial, the account name and the New today / Unread / Waiting for
> reply counters, or a tile per account — next to the New email / Reload email
> / Contacts / Add account buttons, and below it the **inbox list | message
> details** split), the setup wizard (with discovery), the attachment strip →
> MediaViewer, the contact manager, and the **composer** (New email / Reply,
> Send through a **persistent outbox**). On startup the app
> brings up the UltraNet plug-in registry (SMTP/IMAP DSOs load if on the path;
> `ULTRAMAIL_PLUGIN_DIR` overrides). A **background-sync scheduler** (per-account
> intervals) drives the SyncService on a UI timer once the IMAP plug-in is
> present, and the address book **auto-collects** the people you correspond with.
> HTML message bodies are **rendered natively** in the preview through the
> HTMLReader element builder over the UltraCanvas **CSSLayout** engine (block +
> inline layout, headings, lists, links, colors — no web view); plain-text
> bodies show in a read-only text area. Still to come: a live login-verify
> against a real server.

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
    UltraMailComposer.{h,cpp}     Draft model + Reply/Forward/New builders
                                  (Re:/Fwd:, quoting, threading headers)
    UltraMailSender.{h,cpp}       send a Draft via the SMTP plug-in
                                  (IMailProtocolPlugin)
    UltraMailOutbox.{h,cpp}       persistent send queue on UltraDatabase:
                                  Enqueue + Flush (sent->remove, fail->retry)
    UltraMailSyncService.{h,cpp}  full-account sync (folders+inbox+bodies) over
                                  the SyncEngine, sync + background-thread variants
    UltraMailSyncScheduler.{h,cpp} per-account interval tracking; DueAccounts(now)
    UltraMailContactCollector.{h,cpp} auto-add mail senders/recipients to the
                                  address book (Other section) if new
  ui/                             UltraCanvas UI layer
    UltraMailApp.{h,cpp}          app manager: owns store + window, wires it up;
                                  shows the start page or the account view
                                  (actions column · account bar · mail view)
    UltraMailStartPage.{h,cpp}    first-run page: logo + app title + "Add email
                                  account" button, nothing else (no account yet)
    UltraMailAccountBar.{h,cpp}   one account: summary strip (provider initial ·
                                  name · New today / Unread / Waiting for reply
                                  badges); several: a clickable tile per account
    UltraMailMailView.{h,cpp}     split pane: "Inbox" group box with the message
                                  list (ColumnsTreeView: From · Subject · Date) |
                                  "Message" group box with the preview
    UltraMailMessagePreview.{h,cpp} message details: headers, Reply, body (HTML via
                                  HTMLReader/CSSLayout, text in a read-only area),
                                  attachment strip
    UltraMailAccountWizard.{h,cpp} setup wizard dialog (identity step)
    UltraMailAttachmentStrip.{h,cpp} attachment chips; double-click or right-click
                                  (Open / Save As…) opens content in UltraCanvasMediaViewer
    UltraMailContactsView.{h,cpp} contact manager: section sidebar (with counts) +
                                  contact list; add/edit dialog; delete via context menu
    UltraMailComposeWindow.{h,cpp} compose surface: To/Cc/Subject/Body, attachment
                                  strip, Send / Attach file / Attach cloud link
                                  (UltraCloud picker → share link into the body)
  main.cpp                        entry point: init app, open store, show window
  CMakeLists.txt                  UltraMailEngine static library
```

**Attachments:** a message's MIME parts are decoded by `MimeCodec` (over
`UltraNet_MimeParse`); the attachment strip under the message body shows one
chip per part. Double-clicking a chip — or the right-click **Open** — writes
the bytes to the cache and opens them in **`UltraCanvasMediaViewer`** (images,
PDF, text, audio/video, …). Try it: run with `ULTRAMAIL_DEMO_MAIL=1`, which
seeds a demo inbox (two messages dated today, one with an attachment) so the
whole main window can be exercised without a live sync.

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

**Attaching:** "Attach file…" reads a local file into the draft (the chips
under the body show what is attached). "Attach cloud link…" goes through the
**UltraCloud** module (`Docs/Modules/UltraCloud/README.md`): pick a cloud
account (the default is preselected), browse it or upload the file into the
current folder, and the share link lands in the body. Run with
`ULTRAMAIL_DEMO_CLOUD=1` for a demo account with files and an open composer.

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
  data behind the account bar's "Waiting for reply" counter.
- **Per-account status rollup** (`GetAccountStatus`) — short name, email,
  unread (total, today, before today) and needs-answer counts across all
  accounts, in one query. Directly powers the account bar's counters.

## Building & testing

The engine is headless, so it builds and tests without a display:

```sh
cmake -S . -B build -DULTRACANVAS_BUILD_ULTRAMAIL_TESTS=ON
cmake --build build --target UltraMailEngineTests
ctest --test-dir build -R UltraMailEngine --output-on-failure
```
