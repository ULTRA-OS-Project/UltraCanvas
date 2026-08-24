# UltraCanvasFileAssociations

Cross-platform "Open with" service: asks the operating system which
applications are registered for a file, and launches files with the default
or a chosen application — always detached, so closing the caller never takes
the launched program down. Design rationale and the phase plan live in
[`UltraCanvasFileAssociationsProposal.md`](UltraCanvasFileAssociationsProposal.md).

Header: `include/UltraCanvasFileAssociations.h` — everything lives in
`namespace UltraCanvas::FileAssociations` plus the `FileAssociationApp`
struct. The per-platform backends are internal
(`UltraCanvasFileAssociationsBackend.h`, implemented under `OS/<Platform>/`).

## Platform coverage

| Platform | Enumeration | Default open | Launch picked app |
|---|---|---|---|
| Linux / BSD | ✓ freedesktop (shared-mime-info, mimeapps.list, .desktop, mimeinfo.cache; plain text parsing, no GIO/GTK dependency) | ✓ registered handler, `xdg-open` fallback | ✓ |
| Windows | ✓ shell association APIs (`SHAssocEnumHandlers` + `IAssocHandler`, the list Explorer's own "Open with" shows; default from `AssocQueryString`) | ✓ ShellExecuteEx "open" | ✓ `IAssocHandler::Invoke` on the selection |
| macOS | ✓ NSWorkspace / Launch Services (`URLsForApplicationsToOpenContentType:`, default from `URLForApplicationToOpenContentType:`; needs macOS 12) | ✓ `/usr/bin/open` | ✓ `openURLs:withApplicationAtURL:` |
| WASM | — (empty) | — (error) | — (error) |

`Terminal=true` desktop entries are skipped on Linux (Explorer and Finder do
not offer terminal programs either). Windows and macOS resolve candidates
per file extension; a file without one (`Makefile`) has no associations
there, while Linux still types it through its literal-name globs. On macOS
before 12 the type-by-extension lookup does not exist, so enumeration
reports nothing and the menu falls back to the manual entries plus the
"Other application…" picker.

## API

```cpp
struct FileAssociationApp {
    std::string id;        // stable key: .desktop id / ProgID / bundle path
    std::string name;      // "LibreOffice Writer"
    std::string iconPath;  // resolved icon image file, may be empty
    bool isDefault;        // what a default open would launch
};
```

- `GetApplicationsForFiles(paths)` — the applications registered for **all**
  of the given files (intersection across their types), ordered as the OS
  prefers them for the first file, its default application first. Cache-served
  when prewarmed; a cold call resolves synchronously, bounded by the distinct
  file types in the selection.
- `OpenWithDefaultApplication(paths, outError)` — Explorer / Finder
  double-click semantics. A selection spanning several types launches each
  type's default handler once with its files.
- `OpenWithApplication(app, paths, outError)` — launch a specific enumerated
  application.
- `OpenWithApplicationPath(applicationPath, paths, outError)` — launch an
  application the user picked in a file dialog (an executable; a `.app`
  bundle on macOS). The picker UI lives with the caller — set it up with
  `GetApplicationFilter()` (platform file-type filter) and
  `GetApplicationsDirectory()` (where applications live).
- `PrewarmAsync()` — parse the association database on the service's
  background worker. The worker only exists once a `Prewarm*` function is
  called: applications that never use file associations pay nothing.
- `PrewarmExtensionsAsync(extensions)` — pre-resolve candidate lists (and
  icons) for these lowercase extensions on the worker.

All launches detach via `LaunchDetachedProcess`
(`UltraCanvasUtils.h`: POSIX double-fork + `setsid`, Windows
`CreateProcess` into a detached process group).

## Prewarm / caching model

Lookups are cached per extension and served under a mutex, so
`GetApplicationsForFiles` from the UI thread is an O(1) map read once warm.
`UltraCanvasFilerWidget` drives the warm-up automatically: constructing the
first widget calls `PrewarmAsync()`, and every folder scan passes the
folder's distinct extensions to `PrewarmExtensionsAsync()`. The Linux index
re-checks its source files' mtimes (`mimeapps.list`, `mimeinfo.cache`,
`globs2`, the applications directories) and rebuilds — dropping the
extension cache — when anything changed. Windows and macOS have no parseable
database to watch, so their entries expire after a minute instead: the next
lookup re-reads the registry / Launch Services, which is why a default
association changed while the application runs shows up in the menu shortly
after.

Application icons on those two platforms are extracted once and kept as PNG
files (`%LOCALAPPDATA%\UltraCanvas\openwith-icons`,
`~/Library/Caches/UltraCanvas/openwith-icons`), keyed by icon source, so the
extraction survives both the expiry above and a restart. Linux `.desktop`
icons already resolve to theme files and need no extraction.

## Example

```cpp
#include "UltraCanvasFileAssociations.h"
using namespace UltraCanvas;

std::vector<std::string> files = {"/home/me/report.odt"};

// Menu construction: enumerate, default first.
for (const FileAssociationApp& app :
     FileAssociations::GetApplicationsForFiles(files)) {
    printf("%s%s  (icon: %s)\n", app.name.c_str(),
           app.isDefault ? "  [default]" : "", app.iconPath.c_str());
}

// Double-click semantics.
std::string error;
if (!FileAssociations::OpenWithDefaultApplication(files, error))
    fprintf(stderr, "%s\n", error.c_str());
```

`UltraCanvasFilerWidget` wires all of this into its context menu — see
[`UltraCanvasFilerWidget.md`](UltraCanvasFilerWidget.md#context-menu) — so
applications embedding the filer get a working "Open with >" without any
code.

## Version

- 1.1.0 (2026-08-24): P2 + P3 — Windows (`SHAssocEnumHandlers` /
  `IAssocHandler`, icon extraction to PNG) and macOS (NSWorkspace /
  Launch Services) enumeration backends. "Open with >" now lists real
  applications on all three desktop platforms.
- 1.0.0 (2026-08-16): P1 — service + Linux/BSD backend, prewarm worker,
  Windows/macOS default-open placeholders.
