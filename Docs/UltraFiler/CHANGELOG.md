#### 2026-09-01 *1.15.1*
- **Fix: "UltraFiler crashed: exception 0xC00000FF … in ntdll.dll" /
  "exception 0x20474343 … in KERNELBASE.dll" on opening a folder.** Both
  dialogs are the Windows crash reporter's view of one thing: a C++ exception
  thrown where nothing could catch it — inside the window procedure, where an
  event handler on x64 cannot unwind back to `main()`'s `try`/`catch`, or on a
  background thread. Framework 0.3.89 catches the first at the window
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
