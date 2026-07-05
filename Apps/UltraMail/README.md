# UltraMail

The ULTRA OS mail application. Full concept and design:
[`Docs/UltraMail/Concept.md`](../../Docs/UltraMail/Concept.md).

UltraMail is built on **UltraCanvas** (UI) and the **UltraNet** (SMTP/IMAP/POP3)
and **UltraDatabase** (local store) modules.

> **Status (Phase 1, in progress):** the headless **engine** layer is
> implemented and tested, and the first **UI** slice is in — the main window
> with the Toolbox ("Add email account" tile + per-account tiles), the account
> info-tile bar, and the account-setup wizard (identity step). Still to come:
> the discovery/verify pipeline + credential vault behind the wizard, the
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
  ui/                             UltraCanvas UI layer
    UltraMailApp.{h,cpp}          app manager: owns store + window, wires it up
    UltraMailToolbox.{h,cpp}      start-screen grid: account tiles + "Add
                                  email account" square (the only tile at first use)
    UltraMailInfoTileBar.{h,cpp}  per-account status strip (short name · unread
                                  · needs-answer), fed by GetAccountStatus
    UltraMailAccountWizard.{h,cpp} setup wizard dialog (identity step)
    UltraMailAttachmentStrip.{h,cpp} attachment chips; double-click or right-click
                                  (Open / Save As…) opens content in UltraCanvasMediaViewer
  main.cpp                        entry point: init app, open store, show window
  CMakeLists.txt                  UltraMailEngine static library
```

**Attachments:** a message's MIME parts are decoded by `MimeCodec` (over
`UltraNet_MimeParse`); the attachment strip shows one chip per part.
Double-clicking a chip — or the right-click **Open** — writes the bytes to the
cache and opens them in **`UltraCanvasMediaViewer`** (images, PDF, text,
audio/video, …). Try it: run with `ULTRAMAIL_DEMO=1` (and
`ULTRAMAIL_DEMO_OPEN=1` to auto-open) to exercise the flow without a live sync.

The GUI executable target `UltraMail` (root CMake, `-DBUILD_ULTRAMAIL_APP=ON`)
links the full UltraCanvas UI library. The engine and its tests build without a
display; the GUI needs the UI toolkit.

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
