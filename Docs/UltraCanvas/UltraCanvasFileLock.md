# UltraCanvasFileLock

Answers **"is another program holding this file right now"** — the question
behind every *"the action can't be completed because the file is open in
another program"*.

```cpp
#include "UltraCanvasFileLock.h"

FileLockInfo info = ProbeFileLock(path);
if (info.Blocks()) {
    // Replacing, renaming or deleting this file will fail until whoever
    // holds it lets go.
}
```

## The probe never touches the file

It asks the system for the access an overwrite would need, **granting every
sharing flag on its own side**, and closes the handle again. Nothing is
written, nothing is truncated, no lock of its own is taken and no deletion is
scheduled — so a probe is never what another program trips over, and probing a
whole folder cannot make that folder harder to use.

## What the answer means

`FileLockState` deliberately separates "somebody has it" from "you cannot have
it", because the two are the same thing on Windows and unrelated on Unix.

| State | Meaning |
|---|---|
| `Locked` | A write or a replace **would fail right now**: Windows sharing (a running `.exe` or `.dll`, a document a program holds open), or a mandatory write lock on Linux. |
| `OpenElsewhere` | Another process has the file open, and that stops nothing. The normal POSIX case, and an advisory lock — information, not a warning. |
| `Free` | Nothing is in the way. |
| `Unknown` | Not probed, no backend on this platform, a directory, or the file disappeared under the probe. |

`FileLockInfo` carries the state plus which access was refused
(`writeBlocked`, `replaceBlocked`) and, when asked for and available,
`holders` — the programs holding the file, as `"Firefox (1234)"`.
`InUse()` is `Locked || OpenElsewhere`; `Blocks()` is `Locked` alone.

## API

| Call | Meaning |
|---|---|
| `ProbeFileLock(path, wantHolders = false)` | One file. |
| `ProbeFileLocks(paths, wantHolders = false)` | A batch, in **one pass**, one answer per path in the order asked. |
| `FileLockProbeAvailable()` | Whether this build can answer at all. False means every probe returns `Unknown`, so a caller can skip both the call and the display. |
| `FileLockAttributeLetter(state)` | `'X'` for `Locked`, `'O'` for `OpenElsewhere`, `0` otherwise — for a compact `"DRHX"`-style attribute string. |
| `FileLockText(info)` | One sentence, or `""` for `Free` / `Unknown`: *"In use by Firefox (1234) (cannot be replaced)"*, *"Open in another program"*. |

**Ask for holders sparingly.** Naming the program costs a Restart Manager
session per file on Windows and a per-process scan on Linux — right for the
one file a properties dialog is open on, wrong for every row of a listing.
`UltraCanvasFilerWidget` follows exactly that split: the listing is probed
without holders, and UltraFiler's Attributes dialog asks for them for the one
file it shows.

**Prefer the batch for a folder.** On Linux the open-file information is
system-wide, so one `ProbeFileLocks` call for two hundred files walks `/proc`
once instead of two hundred times.

## Backends

| Platform | Backend |
|---|---|
| Windows | `CreateFileW` for `GENERIC_WRITE` and for `DELETE`, both with `FILE_SHARE_READ \| FILE_SHARE_WRITE \| FILE_SHARE_DELETE`. `ERROR_SHARING_VIOLATION` / `ERROR_LOCK_VIOLATION` is another program; `ERROR_ACCESS_DENIED` is **not** (a read-only file or an ACL), and is not reported as one. Holders come from the Restart Manager (`rstrtmgr.dll`, loaded at call time — nothing links against it). |
| Linux | `/proc/locks` for POSIX / OFD / flock locks, matched by device and inode, and `/proc/<pid>/fd` for who has the file open. |
| macOS, Android, WebAssembly | None. Every probe answers `Unknown` and `FileLockProbeAvailable()` is false. |

Adding a platform means dropping a `NativeProbeFileLocks` implementation under
`OS/<Platform>/` and adding it to the `ULTRACANVAS_HAS_NATIVE_FILE_LOCK`
condition in the top-level `CMakeLists.txt`; nothing above the header changes.

## What it cannot see

- **Directories.** A folder is held open by things this probe does not look
  at — above all by being some process's *current working directory*, which is
  why a folder can refuse to be replaced while every file in it looks free.
  Directories answer `Unknown`.
- **Other users' processes on Linux.** `/proc/<pid>/fd` is readable only for
  your own processes, so a file open only by another user's program reads as
  `Free`. Nothing here is a guarantee that an operation will succeed — it is
  an explanation of why one just failed, and a warning that one probably will.
- **This process.** The calling program's own handles are deliberately not
  reported: a file manager reading a thumbnail must not mark that file as
  busy. On Windows the API answers about sharing rather than about handles, so
  a file this process itself holds without sharing does report as `Locked`.
- **A file that is about to be opened.** The answer is a moment in time. A
  copy started a second later can still fail.

## Tests

`Tests/FileLockTest.cpp` (ctest: `FileLockTest`) builds from the probe's own
sources — no display, no UltraCanvas library — and checks that an untouched
file reads `Free`, that the probe changes neither size nor modification time
nor existence, that a missing file, a directory and an empty path answer
`Unknown`, that a batch answers one entry per path in order, and the wording.
On a platform without a backend the detection checks are skipped, not failed.
