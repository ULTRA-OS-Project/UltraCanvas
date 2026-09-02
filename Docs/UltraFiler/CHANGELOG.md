#### 2026-09-01 *1.16.0*
- **The sub-folder search runs in the background and shows its matches while
  it walks.** Pressing "Search in sub folders" started a
  `recursive_directory_iterator` walk on the **UI thread**: the window stopped
  answering for as long as the tree took, which on a large volume was long
  enough for the desktop to report the application as not responding — and the
  walk followed reparse points, so a Windows profile's compatibility junctions
  ("Documents and Settings", "All Users") could send it round in circles until
  it fell over. The scan is now a worker thread with an explicit folder stack:
  symlinks and junctions are never entered, there is a depth cap on top of
  that, every directory error is contained rather than thrown, and the walk
  stops at 20 000 matches. Matches reach the display in batches roughly five
  times a second through the new
  `UltraCanvasFilerWidget::AppendToFileList()`, which keeps the scroll
  position and the selection — so results can be opened while the rest is
  still being found. The status bar counts matches and scanned folders as they
  arrive.
- **A "Scan sub folder" button inside the search field.** It appears as soon
  as there is something to search for and starts the same scan as Enter; while
  the scan runs it reads **Stop** and ends it, keeping what was found. The
  folder display's centered escalation button now carries the same wording.
  Editing the query, navigating, switching or closing the tab and clearing the
  field all end a running scan.
- Fixed: pressing Enter in the search field emptied the field. Dropping the
  as-you-type filter made the widget report a refresh, and the handler for
  that copied the widget's (now empty) filter text back into the field,
  because the tab was not yet marked as showing a search.
#### 2026-09-01 *1.15.1*
- **Fix: "UltraFiler crashed: exception 0xC00000FF … in ntdll.dll" /
  "exception 0x20474343 … in KERNELBASE.dll" on opening a folder.** Both
  dialogs are the Windows crash reporter's view of one thing: a C++ exception
  thrown where nothing could catch it — inside the window procedure, where an
  event handler on x64 cannot unwind back to `main()`'s `try`/`catch`, or on a
  background thread. Framework 0.3.90 catches the first at the window
  procedure and guards the filer widget's workers; this version does the same
  for UltraFiler's own threads. The subfolder probe that decides which tree
  folders get an expand button, the cloud-storage discovery and the Windows
  program launcher each catch what their work throws, log it (with the folder
  it happened on) and carry on. The probe and the tree's subfolder listing
  also step their directory iterators with the error-code overload: the
  range-for form throws when a read fails part-way through, which a removable
  or network drive can do at any time. An error in an event handler is now
  reported once in a dialog and logged after that; set
  `ULTRACANVAS_DEBUG_LOG` to a file path to capture the operation, file and
  error text if it recurs.

#### 2026-08-31 *1.15.0*
- **UltraFiler keeps its own changelog from here.** Everything up to and
  including this version shipped as part of a framework release and is recorded
  in [`Docs/UltraCanvas/CHANGELOG.md`](../UltraCanvas/CHANGELOG.md) — nothing
  was rewritten or moved, so that history stays where it was published. From
  now on a change to the file manager (`Apps/UltraFiler`) is described here and
  carries this file's version, and UltraFiler no longer moves when the
  framework releases.
- A framework change UltraFiler needs still belongs in the framework changelog.
  Cross-reference it from here when a release depends on it; never describe one
  change in two files under two version numbers.

<!--
Version source of truth: the first line of this file, format
`#### YYYY-MM-DD *x.y.z*`, read by cmake/UltraCanvasVersion.cmake.
-->
