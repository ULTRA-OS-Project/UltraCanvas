# UltraCanvasFileAssociations — "Open with" Cross-OS Proposal

Status: **Proposal — nothing implemented yet.**

Author: UltraCanvas Framework
Last Modified: 2026-08-10

---

## 1. Problem

`UltraCanvasFilerWidget`'s file context menu carries an **"Open with >"**
submenu, but it is fed exclusively by hand:

```cpp
struct FilerOpenWithApp {
    std::string label;
    std::string iconPath;
    std::function<void(const std::vector<FilerEntry>&)> onOpen;
};
void AddOpenWithApp(const FilerOpenWithApp& app);
```

Nothing anywhere queries the operating system for the applications actually
registered for a file type. UltraFiler registers no apps at all, so its users
permanently see the disabled *"(no applications)"* placeholder. Double-click
(`onFileActivated`) is equally unlinked: UltraFiler routes media files into
the preview pane and silently ignores every other file kind — a `.odt` or
`.html` cannot be opened from the file manager at all.

Precedents already in the tree, but scattered and single-purpose:

- `Apps/UltraFiler/UltraFilerPrompt.cpp` — detached process launch
  (double-`fork` + `setsid` + `execvp` on POSIX, `ShellExecuteExW` on
  Windows, `open -a` for macOS bundles).
- `UltraCanvas/core/UltraCanvasUtils.cpp` (`OpenURL`) — `ShellExecuteW` /
  `open` / `xdg-open` for URLs.

Neither can answer *"which applications can open this file, and what are
their names and icons?"* — that is the missing piece.

## 2. Goal and scope

One framework service that, on every supported desktop OS, provides:

1. **Enumerate** the applications registered for a given file (display name,
   icon, default-flag), in the OS's own preference order.
2. **Launch** a file (or several) with its **default** application.
3. **Launch** files with a **specific** enumerated application, detached from
   the calling process.
4. **Pick another application** ("Other application…") via the native
   chooser where the OS has one, or a file-dialog fallback where it does not.

Non-goals (possible later phases, see §8): changing the system default
association, MIME-type *registration* of UltraCanvas apps, and the ULTRA OS
backend (stub until its application registry exists).

## 3. Architecture

Per the platform-separation rule (AGENTS.md): one shared public header +
dispatch in `core/`, one implementation file per platform in
`OS/<Platform>/`. Same layout as clipboard, drag & drop and native dialogs.

```
UltraCanvas/include/UltraCanvasFileAssociations.h      // public API
UltraCanvas/OS/Linux/UltraCanvasLinuxFileAssociations.cpp
UltraCanvas/OS/MSWindows/UltraCanvasWindowsFileAssociations.cpp
UltraCanvas/OS/MacOS/UltraCanvasMacOSFileAssociations.mm
UltraCanvas/OS/WASM/…                                  // stubs: empty list
```

The detached-launch helper currently private to `UltraFilerPrompt.cpp`
(POSIX double-fork / `ShellExecuteExW`) moves into the service (or a shared
`core` process utility) so UltraFiler's prompt launcher and "Open with" use
one tested code path.

### Public API sketch

```cpp
namespace UltraCanvas {

struct FileAssociationApp {
    std::string id;        // .desktop id / ProgID / bundle path — stable key
    std::string name;      // user-visible name, e.g. "LibreOffice Writer"
    std::string iconPath;  // resolved icon image file; empty when none found
    bool isDefault = false;
};

namespace FileAssociations {

    // Applications registered for ALL of the given files (intersection),
    // default app of the first file listed first, OS preference order after.
    std::vector<FileAssociationApp> GetApplicationsForFiles(
            const std::vector<std::string>& paths);

    // Launch with the OS default application (Explorer/Finder double-click
    // semantics). Detached: closing the caller never kills the child.
    bool OpenWithDefaultApplication(const std::vector<std::string>& paths,
                                    std::string& outError);

    // Launch with one specific enumerated application.
    bool OpenWithApplication(const FileAssociationApp& app,
                             const std::vector<std::string>& paths,
                             std::string& outError);

    // "Other application…": native chooser (Windows), file dialog restricted
    // to applications elsewhere. Returns false on cancel.
    bool OpenWithChooser(const std::vector<std::string>& paths,
                         std::string& outError);

} // namespace FileAssociations
} // namespace UltraCanvas
```

All results are computed synchronously but must be cheap (§7 caching);
launching never blocks on the launched application.

## 4. Platform backends

### 4.1 Linux / BSD — freedesktop.org, no new dependencies

Everything needed is plain-text parsing; no GIO/GTK dependency required.

1. **File type:** map the file to a MIME type via shared-mime-info glob
   files (`$XDG_DATA_HOME`/`$XDG_DATA_DIRS` → `mime/globs2`, longest-glob
   wins, case handling per spec). Extension-keyed cache. Fallback
   `application/octet-stream`.
2. **Candidates:** per the mime-apps spec, merge in order:
   `~/.config/mimeapps.list` (`[Default Applications]`, `[Added
   Associations]`, `[Removed Associations]`), `$XDG_CURRENT_DESKTOP`-prefixed
   variants, `/etc/xdg/mimeapps.list`, then each applications directory's
   `mimeinfo.cache`.
3. **App info:** parse the matched `.desktop` files — `Name` (localised),
   `Icon`, `Exec`, `TryExec`, `NoDisplay`, `Terminal`. Icon name → file via
   hicolor/current-theme lookup (reuse the framework's icon loading; a plain
   `hicolor` + `pixmaps` search is enough for phase 1).
4. **Launch:** expand `Exec` field codes (`%f %F %u %U`, strip the rest),
   spawn with the shared detached-launch helper. Default open = the
   `[Default Applications]` entry, falling back to the first candidate, then
   to `xdg-open`.

### 4.2 Windows — Shell association APIs

1. **Candidates:** `SHAssocEnumHandlers(L".ext", ASSOC_FILTER_RECOMMENDED)`
   → `IAssocHandler` list: `GetUIName` (display name), `GetIconLocation`
   (icon resource), `Invoke`/`CreateInvoker` (launch). This is exactly the
   list Explorer shows in its own "Open with".
2. **Default:** `AssocQueryString(ASSOCSTR_FRIENDLYAPPNAME /
   ASSOCSTR_EXECUTABLE)` marks which handler is the default.
3. **Icons:** `GetIconLocation` → `SHDefExtractIconW`/`ExtractIconExW` →
   HICON → 32-bit DIB written once into the icon cache directory as PNG
   (the menu API takes an `iconPath`; converting HICONs to files keeps the
   shared API image-based). Cache keyed by icon location + index.
4. **Launch:** `IAssocHandler::Invoke` per selected file (the interface is
   single-item; loop over the selection). Default open =
   `ShellExecuteExW(verb "open")` — already the pattern used by
   UltraFilerPrompt. "Other application…" = `SHOpenWithDialog(OAIF_EXEC)`,
   the real Explorer dialog.

### 4.3 macOS — NSWorkspace / Launch Services

1. **Candidates:** `-[NSWorkspace URLsForApplicationsToOpenURL:]`
   (macOS 12+; `LSCopyApplicationURLsForURL` fallback for older targets) —
   bundle URLs of every capable app.
2. **Default:** `-[NSWorkspace URLForApplicationToOpenURL:]`.
3. **App info:** name from the bundle (`CFBundleDisplayName` /
   `CFBundleName`), icon via `-[NSWorkspace iconForFile:]` → NSImage →
   PNG in the icon cache (same image-file contract as Windows).
4. **Launch:** `-[NSWorkspace openURLs:withApplicationAtURL:configuration:]`
   — takes the whole selection in one call. Default open = `openURLs:` with
   the default app. "Other application…": no public system dialog — use the
   framework's file dialog filtered to `.app` under `/Applications`
   (precedent: `UltraFilerPrompt::GetApplicationFilter()`).

### 4.4 WASM / ULTRA OS

Stubs: empty candidate list, launch functions return `false` with a clear
`outError`. The menu then shows the existing disabled placeholder — no
`#ifdef`s in widget or app code.

## 5. Widget integration (`UltraCanvasFilerWidget`)

The widget keeps zero platform code — it calls the core service, which is
allowed everywhere (`core` → `core`; the OS split lives inside the service).

Menu built per open, from the current selection:

```
Open with >   Writer            ← default app, listed first
              Code — OSS
              GIMP
              ──────────────
              MyCustomTool      ← AddOpenWithApp() entries, unchanged API
              ──────────────
              Other application…
```

- **OS section:** `GetApplicationsForFiles(selection)` mapped straight to
  menu items (`MenuItemData::Action(label, iconPath, cb)` — icon support
  already exists). Multi-selection shows the intersection; an empty
  intersection shows only the manual entries and the chooser.
- **Manual section:** `AddOpenWithApp()` keeps working unchanged — entries
  appear below the OS apps, so existing embedders (DemoApp, Texter) are
  unaffected.
- **Chooser:** "Other application…" → `OpenWithChooser(selection)`.
- **Opt-out:** `SetSystemOpenWithEnabled(bool)` (default **on**) for
  embedders that want the old manual-only behaviour.
- **Activation fallback:** new optional behaviour
  `SetActivateOpensWithDefaultApp(bool)` — when set and `onFileActivated`
  is not consumed by the host, double-click / Enter calls
  `OpenWithDefaultApplication`. (UltraFiler will use its own callback, see
  §6, so this is for simple embedders.)

## 6. UltraFiler integration

- **Double-click / Enter:** keep the current media-preview behaviour, and
  for non-previewable files call `OpenWithDefaultApplication` — Explorer
  semantics at last. The existing history recording
  (`RecordEntryInHistory`) already fires in `onFileActivated`, so opened
  files keep flowing into the History **Files** tab.
- **History Apps tab:** when a launch resolves a concrete application, record
  it (`UltraFilerHistoryKind::App`) — the tab exists and is fed today only by
  activating executables directly.
- **Settings:** optional later — "remember last chooser pick per extension"
  stored in `UltraFilerSettings`, surfaced at the top of the OS section.

## 7. Performance and caching

The submenu is built synchronously while opening a context menu, so:

- **Candidate cache** keyed by extension (Windows/macOS) or MIME type
  (Linux), invalidated by the source timestamps (`mimeapps.list`,
  `mimeinfo.cache` mtimes on Linux) or a short TTL (~60 s) elsewhere.
  First lookup per extension does the real work; subsequent menu opens are
  hash-map hits.
- **Icon cache** on disk (`<config>/UltraCanvas/openwith-icons/`) for the
  HICON/NSImage extractions; `.desktop` icons resolve to existing theme
  files and need no extraction.
- Menu construction never launches processes and never touches the network.

## 8. Phasing

| Phase | Deliverable |
|---|---|
| **P1** | Service API + **Linux** backend; shared detached-launch helper unified with UltraFilerPrompt; widget menu integration (OS section + chooser + opt-out); UltraFiler double-click default-open. |
| **P2** | **Windows** backend (SHAssocEnumHandlers + SHOpenWithDialog + icon extraction). |
| **P3** | **macOS** backend (NSWorkspace + `.app` picker fallback). |
| **P4** | Extras: remember chooser picks per extension, History Apps recording from launches, "Set as default" (xdg-mime / `OAIF_REGISTER_EXT` / `LSSetDefaultRoleHandlerForContentType`). |
| **P5** | ULTRA OS backend once its application registry lands. |

Each phase updates `Docs/UltraCanvas/UltraCanvasFilerWidget.md` (menu
section) and adds `Docs/UltraCanvas/UltraCanvasFileAssociations.md` with the
service API once P1 ships.

## 9. Open questions

1. **Terminal=true desktop entries** (Linux): skip them in phase 1, or launch
   through the detected terminal from `UltraFilerPrompt::DetectSystemPrompt`?
   Proposal: skip — Explorer and Finder don't offer them either.
2. **Intersection vs. union for multi-selection:** intersection matches
   Explorer/Finder behaviour and is proposed; union with per-app partial
   launches is more powerful but surprising.
3. **Async icon fill-in:** if Windows icon extraction proves slow on cold
   cache, populate menu items without icons and let the cache warm up in a
   worker thread — the menu API would need a per-item icon refresh hook.
