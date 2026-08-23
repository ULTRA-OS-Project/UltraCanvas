# UltraCleaner

## Overview

UltraCleaner finds and removes the files macOS, Windows and Linux leave
behind — temporary files, application and browser caches, logs, crash
reports, thumbnail databases, package-manager downloads, developer build
leftovers and the trash — and shows exactly which paths it proposes to
remove before it touches anything. It is built entirely from UltraCanvas
elements and ships as `Apps/UltraCleaner`, with a headless engine
(`UltraCleanerEngine`) that the GUI, the command line and the test suite all
share.

The app opens on an **Overview** page rather than on its rule table: a
circular chart per mounted drive, coloured by how full it is, so a user can
see whether a clean-up is worth starting before choosing what to clean. Two
buttons there lead into the two things it can do — system junk, and photo
albums.

The application deletes files, so the design question is not what it can
find but what it can be talked into destroying. The answer is the rule table
and the path guard described below: there is no code path that removes a
path some rule did not name, and every candidate is checked twice — once
when it is found and again when it is removed.

- Sources: `Apps/UltraCleaner/{engine,ui}`, entry point `Apps/UltraCleaner/main.cpp`
- Targets: `UltraCleanerEngine` (static, headless), `UltraCleaner` (GUI)
- Tests: `Tests/UltraCleaner`, target `UltraCleanerEngineTests`
- Namespace: `UltraCleaner`

## Building

UltraCleaner builds with the rest of the tree and is on by default:

```bash
mkdir build && cd build
cmake ..                                     # -DBUILD_ULTRACLEANER=OFF to skip
cmake --build . --target UltraCleaner
```

The engine has no UltraCanvas, UltraNet or UltraDatabase dependency — it is
`std::filesystem` plus, on Windows, the shell APIs for the recycle bin — so
it also builds on its own:

```bash
cmake .. -DULTRACANVAS_BUILD_ULTRACLEANER_TESTS=ON
cmake --build . --target UltraCleanerEngineTests
ctest -R UltraCleanerEngine
```

## Photo albums

Alongside the rule-driven system clean-up, UltraCleaner finds pictures in a
photo album that are the same, or shots of the same moment. This half is
content-driven rather than path-driven: it decodes what it finds and compares
it, so it lives in its own engine (`UltraCleanerAlbum`) which — unlike the
rule engine — links the UltraCanvas core for image decoding.

### Two questions, kept apart

| Kind | What it means | Certainty |
|---|---|---|
| Identical files | The same bytes | A fact. Removing one loses nothing. |
| Identical images | Different files, same decoded pixels | A fact. |
| Similar | Alike to within the chosen level | A judgement. Never pre-selected. |

### How similarity is measured

Each picture reduces to four signals taken from one small decode:

| Signal | What it captures |
|---|---|
| **pHash** (64 bits) | Sign pattern of the low-frequency DCT coefficients — composition. Survives rescaling and re-encoding. |
| **dHash** (64 bits) | Horizontal brightness gradients. Cheap, kept for reporting. |
| **Colour grid** (4×4) | Mean colour per cell. pHash is blind to colour; a global histogram is blind to *where* the colour is. |
| **Sharpness** | Laplacian variance, used to choose a keeper — not to compare. |

**Both pHash and the colour grid must agree.** Either alone is not enough:
measured on a 60-photograph reference album, pHash could not separate a true
burst member from an unrelated photo of the same person — both sat 14 bits
away — while the colour grid put them at 100% and 81%.

The grid is scored as the **fraction of cells that agree**, not a summed
distance. A burst often has a changing background behind a fixed subject —
people walking past — which spoils three or four cells while the rest hold. A
sum would be dominated by exactly the part that legitimately moved.

### The three levels

| Level | pHash | Grid | What it finds |
|---|---|---|---|
| **Duplicates only** | ≤ 6 | ≥ 90% | The same photo again: a copy, a rescale, a small edit |
| **Same moment** (default) | ≤ 14 | ≥ 85% | A burst, or several tries at one picture |
| **Same scene** | ≤ 22 | ≥ 80% | Anything of the same scene or subject |

On the reference album, *Same moment* reproduced every known group with no
false positive; *Same scene* added two plausible groups and one contaminated
one. The window between them is narrow — a true pair sat at 88% grid
agreement and a false one at 81% — so these numbers are a working setting
measured on one album, not a universal constant. Re-check them against a
larger library before trusting them to delete anything.

**Changing the level costs no rescan.** Describing the pictures is the
expensive part and does not depend on the level, so re-grouping is only the
comparisons — the UI offers it as an instant control.

### Screenshots are excluded from similarity

Screenshots share their whole layout with every other capture of the same
application and differ only in text, which these descriptors discard. On a
real set of 73 screenshots, two unrelated bank transfers scored **8 bits
apart** while the same transfer captured twice, one of them cropped, scored
**36**. The ranking inverts, so no threshold helps. They are classified out —
by filename, by the absence of any EXIF camera tag, and by lossless formats at
exact screen widths — and matched only when byte-identical.

### Known limitations

- **Cropping defeats pHash.** A head-and-shoulders crop of a portrait sits
  more than 26 bits from its original. pHash describes the whole frame;
  catching crops needs local keypoint matching, which this does not do.
- **Capture time cannot be relied on.** Every photograph in the reference
  album carried either no EXIF or a zeroed `0000:00:00` timestamp. The
  optional time gate (`--within`) therefore applies *only* where both
  pictures know when they were taken — a missing timestamp never excludes a
  match.

### Using it

```bash
UltraCleaner --album ~/Pictures                      # list the groups
UltraCleaner --album ~/Pictures --level duplicates   # only certain matches
UltraCleaner --album ~/Pictures --level scenes       # cast wider
UltraCleaner --album ~/Pictures --within 600         # …taken within 10 minutes
UltraCleaner ~/Pictures                              # open the window on it
```

In the window, the **Photo albums** tab reviews by *group*, not by file: each
group shows every picture in it with the suggested keeper marked, and one
checkbox for the whole group. Nothing is ticked for you. Removal goes through
the same Remover as the rest of the app, so it inherits the same PathGuard,
the same trash support and the same simulate-by-default posture.

## Using it

### The window

Three tabs: **Overview**, **System junk** and **Photo albums**.

Overview is what the app opens on. It draws one
`UltraCanvasCircularProgressChart` per mounted volume — green below 75%
used, amber to 90%, red above — with the drive's name, how full it is, what
is free of what, and where it is mounted. Underneath, a single line names
the fullest drive and says whether it is worth doing anything about, and
two buttons ("Clean system junk", "Find duplicate photos") switch to the
tab that does it. The volume list comes from `ListVolumes()` in
`engine/UltraCleanerVolumes.h`: `/proc/self/mounts` on Linux, `getmntinfo`
on macOS and `GetLogicalDriveStringsW` on Windows, with capacity from
`std::filesystem::space()` and pseudo filesystems (proc, sysfs, cgroup,
tmpfs, snap loopbacks, …) left out. It is read once when the page is built
and again on `HomeView::Refresh()`.

On the System junk tab, the toolbar runs a scan, chooses what should happen to what the scan found
(simulate / move to trash / delete permanently) and starts the cleanup. The
left panel lists one row per category — an `UltraCanvasCheckbox` for
"clean this" and an `UltraCanvasBadge` with the recoverable size. The right
panel is an `UltraCanvasColumnsTreeView` naming every path that would go, its
size, when it last changed and which rule proposed it; double-clicking a row
keeps or drops that single path, which puts the category checkbox into its
indeterminate state.

Scanning and cleaning both block on the disk, so both run on a worker
thread and marshal their results back through a queue drained by a UI timer.
The Stop button cancels either.

Nothing is pre-ticked unless its rule says so. Categories whose removal
costs the user something they may not expect — emptying the trash, dropping
a Maven repository, sweeping old installers out of Downloads — arrive
unticked and have to be chosen deliberately.

### The command line

The same engine, no display needed:

```bash
UltraCleaner --scan                 # list what could go, change nothing
UltraCleaner --rules                # print the rule table for this platform
UltraCleaner --clean                # simulate a cleanup and report the totals
UltraCleaner --clean --trash        # move it to the trash / recycle bin
UltraCleaner --clean --delete --yes # delete it permanently
UltraCleaner --clean --trash --all  # include the normally-unticked categories
```

`--clean` simulates unless it is given `--trash` or `--delete`, and
`--delete` additionally requires `--yes`. There is no way to reach a
permanent delete with a single flag.

## What it looks for

Every location UltraCleaner examines comes from a `CleanRule` in
`engine/UltraCleanerRules.cpp`. Reviewing that one table is enough to know
what the application can touch on each operating system. Rules name their
roots with tokens rather than literal paths, so a single row covers all
three platforms wherever only the base directory differs:

| Token | Linux | macOS | Windows |
|---|---|---|---|
| `{HOME}` | `$HOME` | `$HOME` | `%USERPROFILE%` |
| `{TEMP}` | `$TMPDIR` or `/tmp` | `$TMPDIR` | `%TEMP%` |
| `{SYSTEMTEMP}` | `/var/tmp` | `/private/var/tmp` | `%SystemRoot%\Temp` |
| `{CACHE}` | `$XDG_CACHE_HOME` or `~/.cache` | `~/Library/Caches` | `%LOCALAPPDATA%` |
| `{DATA}` | `$XDG_DATA_HOME` or `~/.local/share` | `~/Library/Application Support` | `%APPDATA%` |
| `{STATE}` | `$XDG_STATE_HOME` or `~/.local/state` | `~/Library/Logs` | `%LOCALAPPDATA%` |
| `{LOCALAPPDATA}` / `{APPDATA}` | — | — | `%LOCALAPPDATA%` / `%APPDATA%` |
| `{WINDIR}` | — | — | `%SystemRoot%` |
| `{DOWNLOADS}` | `~/Downloads` | `~/Downloads` | `%USERPROFILE%\Downloads` |
| `{TRASH}` | `~/.local/share/Trash` | `~/.Trash` | shell-managed |

A token that has no meaning on the running system resolves to the empty
string, and its whole root is skipped — a Windows-only rule cannot
accidentally point at `/Microsoft/Windows` on Linux.

### Categories

| Category | What it covers |
|---|---|
| Temporary files | The user and shared temp directories; `.DS_Store` files on macOS |
| Application caches | `~/.cache`, `~/Library/Caches`, sandboxed container caches, Flatpak caches, `INetCache`, shader caches |
| Browser caches | Chrome, Chromium, Edge, Brave, Vivaldi, Opera, Firefox, Safari — cache directories only; logins, bookmarks and history are never named |
| Thumbnail caches | The freedesktop thumbnail cache, Quick Look, Explorer's `thumbcache_*.db` / `iconcache_*.db` |
| Log files | `~/Library/Logs`, `~/.local/state/**/*.log`, `.xsession-errors`, Windows CBS/DISM logs |
| Crash reports | Diagnostic reports, `CrashReporter`, apport, core dumps, `CrashDumps`, Windows Error Reporting queues |
| Package manager caches | npm, Yarn, pip, Gradle, Cargo, Go, Composer, Maven, NuGet |
| Developer leftovers | Xcode derived data and archives, iOS Simulator caches, JetBrains caches, VS Code caches |
| Installers and updates | Windows Update downloads, iOS software updates, old `.dmg`/`.pkg`/`.msi`/`.exe`/`.deb`/`.rpm`/AppImage files in Downloads |
| Broken shortcuts | Symbolic links whose target no longer exists |
| Trash | The desktop trash, or the Windows recycle bin through the shell |

### Rule modes

| Mode | Behaviour |
|---|---|
| `ClearDirectoryContents` | Everything directly inside the root is removable; the root itself stays. One item is reported per top-level entry, sized recursively. |
| `RemoveMatchingFiles` | Files matching `namePatterns`, found by walking the root down to `maxDepth` levels. Never reports a directory. |
| `RemoveMatchingDirectories` | Directories directly inside the root whose name matches. |
| `RemoveDanglingSymlinks` | Symbolic links under the root whose target no longer exists. |
| `WindowsRecycleBin` | The shell-managed recycle bin: sized with `SHQueryRecycleBinW`, emptied with `SHEmptyRecycleBinW`. Ignored on other platforms. |

A rule also carries `minAgeDays` (skip anything modified more recently),
`excludeNames` (globs never removed inside the root — this is what keeps the
session's X11, D-Bus and PipeWire sockets in `/tmp` out of reach) and
`safeByDefault`.

## The safety model

`engine/UltraCleanerSafety.h` holds the whole of it, deliberately. A path is
removable only when all of these hold:

1. It is absolute and at least two levels below the filesystem root, so
   `/usr` and `C:\` cannot be reached whatever a rule says.
2. It lies **strictly inside** a root the current rule set resolved. A root
   is a container, never a target — rules clear a cache directory's contents,
   they never remove the cache directory.
3. Neither it nor its symlink-resolved form is a protected location, and it
   is not an ancestor of one. `ProtectedPaths()` covers the filesystem and OS
   roots, the user's home, and the directories inside it that hold work
   rather than junk (Documents, Desktop, Downloads, Pictures, `.ssh`,
   `.gnupg`, `.config`, `.local`, `Library`, `AppData`, the cloud-sync
   folders, …). Because protection is "is, or contains", a *file* inside a
   protected directory is still reachable when a rule names it — which is how
   the Downloads installer rule works while `~/Downloads` itself stays safe.
4. Following the symlinks in its parent chain still lands inside an allowed
   root, so a link planted in a cache cannot redirect a delete into the
   user's documents. The candidate itself may be a link: removing a link
   removes the link.
5. It is a regular file, a directory or a symlink — never a socket, a fifo or
   a device node.

The guard runs during the scan and again during the removal, rebuilt from
`ScanReport::allowedRoots` rather than from the item list. A report whose
items were tampered with between the two still cannot reach outside the
locations the rule table resolved; that case is covered by
`RemoverRefusesAnItemPathThatLeftTheAllowedRoots` in the test suite.

## Removal modes

| Mode | What happens |
|---|---|
| `Simulate` | Nothing is touched. Reports what would go and how much it would free. The default everywhere — the GUI's dropdown and the CLI's `--clean` both start here. |
| `MoveToTrash` | XDG trash on Linux (`files/` plus a `.trashinfo` record, so the desktop's "Restore" works), `~/.Trash` on macOS, `SHFileOperationW` with `FOF_ALLOWUNDO` on Windows. Falls back to copy-then-remove when the trash is on another filesystem. |
| `DeletePermanently` | `std::filesystem::remove_all`. |

Failures are collected rather than thrown: a report names each path that
would not go and why, capped by `RemovalOptions::failureLimit` so a wall of
permission errors does not bury the summary.

## Engine API

```cpp
#include "UltraCleanerScanner.h"
#include "UltraCleanerRemover.h"

using namespace UltraCleaner;

Scanner scanner;
ScanReport report = scanner.Scan(RulesForCurrentPlatform(), ScanOptions{},
                                 [](const ScanProgress& progress) {
                                     // called from the scanning thread
                                 });

for (auto& item : report.items) {
    if (item.category == CleanCategory::TrashBin) item.selected = false;
}

RemovalOptions options;
options.mode = RemovalMode::MoveToTrash;

Remover remover;
RemovalReport result = remover.Remove(report, options);
```

| Type | Purpose |
|---|---|
| `CleanCategory`, `CategoryTitle`, `CategoryDescription`, `CategoryKey` | The grouping the UI shows |
| `CleanItem`, `CategorySummary`, `ScanReport`, `SummarizeReport` | Scan results and their totals |
| `FormatByteSize` | Decimal byte formatting shared by the GUI and the CLI |
| `CleanRule`, `RuleMode`, `BuiltinRules`, `RulesForPlatform`, `FindRule` | The rule table |
| `HomeDir`, `UserCacheDir`, … , `ExpandTokens`, `ExpandWildcardDirectories`, `GlobMatch`, `IsPathInside` | Platform paths and pattern matching |
| `PathGuard`, `SafetyCheck`, `SafetyVerdict`, `ProtectedPaths`, `IsProtectedPath` | The safety guard |
| `Scanner`, `ScanOptions`, `ScanProgress` | Scanning, with progress and cancellation |
| `Remover`, `RemovalMode`, `RemovalOptions`, `RemovalReport`, `MoveToPlatformTrash` | Removal |

## Adding a rule

1. Add a row to `MakeRules()` in `engine/UltraCleanerRules.cpp` with a stable
   `id`, the narrowest root that does the job, and `safeByDefault = false`
   whenever removal costs the user something they may not expect.
2. Prefer tokens over literal paths, and let a root resolve to nothing on the
   platforms it does not apply to rather than adding a platform `#if`.
3. Run the suite: the structural tests check that ids are unique, that every
   root uses a known token and expands to an absolute path, that the
   `Matching*` modes carry patterns, and that the destructive categories stay
   unticked.
4. Add a row to the category table above and a changelog entry.

## UI elements used

UltraCleaner paints nothing itself. Its window is
`UltraCanvasWindow` + `UltraCanvasGroupBox` + `UltraCanvasContainer`
(flex layout) holding `UltraCanvasButton`, `UltraCanvasDropdown`,
`UltraCanvasCheckbox` (three-state), `UltraCanvasBadge`, `UltraCanvasLabel`,
`UltraCanvasColumnsTreeView`, `UltraCanvasTabbedContainer` and the Charts
plugin's `UltraCanvasCircularProgressChart`; confirmations and warnings go
through `UltraCanvasDialogManager`. See
[UltraCanvasUIElements.md](../UltraCanvas/UltraCanvasUIElements.md).
