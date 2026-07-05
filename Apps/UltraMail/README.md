# UltraMail

The ULTRA OS mail application. Full concept and design:
[`Docs/UltraMail/Concept.md`](../../Docs/UltraMail/Concept.md).

UltraMail is built on **UltraCanvas** (UI) and the **UltraNet** (SMTP/IMAP/POP3)
and **UltraDatabase** (local store) modules.

> **Status (Phase 1, in progress):** the headless **engine** layer is
> implemented and tested. The display-bound UI (Toolbox start screen with the
> "Add email account" tile, account-setup wizard, three-pane main window,
> account info-tile bar, composer) is the next slice.

## Layout

```
Apps/UltraMail/
  engine/                         headless — depends only on UltraDatabase
    UltraMailTypes.{h,cpp}        Account / Folder / MessageEnvelope, flags,
                                  FolderRole, AccountStatus (info-tile rollup)
    UltraMailLocalStore.{h,cpp}   account/folder/message index on UltraDatabase:
                                  schema migrations, upserts, flag updates,
                                  the needs-answer rule, per-account rollups
  CMakeLists.txt                  UltraMailEngine static library
  (ui/  — added in a later phase: Toolbox, wizard, window, composer)
```

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
