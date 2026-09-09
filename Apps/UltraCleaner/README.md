# UltraCleaner

Removes the files macOS, Windows and Linux leave behind — temporary files,
application and browser caches, logs, crash reports, thumbnail databases,
package-manager downloads, developer build leftovers and the trash — and
shows exactly which paths it proposes to remove before it touches anything.

Full documentation, including the complete rule table and the safety model:
[`Docs/UltraCleaner/README.md`](../../Docs/UltraCleaner/README.md).

## Layout

| Path | What is in it |
|---|---|
| `engine/UltraCleanerTypes.*` | Categories, `CleanItem`, `ScanReport`, byte formatting |
| `engine/UltraCleanerPaths.*` | Per-platform directories, `{TOKEN}` expansion, glob matching |
| `engine/UltraCleanerRules.*` | **The rule table** — every location the application can touch |
| `engine/UltraCleanerSafety.*` | The path guard: protected locations, root containment, symlink escapes |
| `engine/UltraCleanerScanner.*` | Rule table → `ScanReport`, with progress and cancellation |
| `engine/UltraCleanerRemover.*` | Simulate / move to trash / delete permanently |
| `ui/UltraCleanerWindow.*` | The main window: toolbar, detail table, worker-thread plumbing |
| `ui/UltraCleanerCategoryPanel.*` | The category rows (checkbox + size badge) |
| `main.cpp` | GUI bootstrap, and the `--scan` / `--clean` / `--rules` command line |

The engine is headless — `std::filesystem` plus the Windows shell APIs for
the recycle bin, no UltraCanvas dependency — so it links into the command
line and the test suite (`Tests/UltraCleaner`, target
`UltraCleanerEngineTests`) on its own.

## Safety in one paragraph

There is no code path that removes a path some rule did not name. Every
candidate passes `PathGuard::Check` twice — once when the scan finds it and
again when the removal runs, with the guard rebuilt from the scan's own
allowed roots rather than from the item list. A root is a container, never a
target; the user's home and the directories in it that hold work are
protected both as themselves and as ancestors; a symlink cannot redirect a
delete out of an allowed root; and sockets, fifos and device nodes are never
touched. Removal simulates unless the caller explicitly asks for the trash or
a permanent delete.
