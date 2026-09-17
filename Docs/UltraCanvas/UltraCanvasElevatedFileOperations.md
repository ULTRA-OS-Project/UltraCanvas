# UltraCanvasElevatedFileOperations

**"Delete as administrator"** — the retry Explorer offers when a delete answers
*You need permission to perform this action*: Windows asks for consent (the
UAC prompt) and the operation runs again with administrator rights.

```cpp
#include "UltraCanvasElevatedFileOperations.h"

// 1. At the very top of main(), before any UI:
int helperExit = 0;
if (ElevatedFileOperations::RunHelperIfRequested(argc, argv, helperExit))
    return helperExit;

// 2. Where a delete fails:
std::error_code ec;
std::filesystem::remove_all(path, ec);
if (ec && ElevatedFileOperations::IsAvailable() &&
    ElevatedFileOperations::IsPermissionFailure(ec)) {
    auto result = ElevatedFileOperations::DeleteElevated({path});   // blocks
    if (result.outcome == ElevatedFileOperations::ElevatedOutcome::Completed &&
        result.failures.empty()) { /* gone */ }
}
```

`UltraCanvasFilerWidget` does step 2 for you: its delete-problem dialog gains a
**Delete as administrator** choice wherever step 1 has been done (see
[Delete problems](UltraCanvasFilerWidget.md#delete-problems-locked--failing-entries)).
UltraFiler does step 1 in its `main.cpp`.

## Why a second process

A running process cannot raise its own rights. What Explorer's shield button
does — and what this module does — is start **a second copy of the same
executable** through the shell's `runas` verb. Windows shows its consent prompt
on the secure desktop (nothing in the application can answer it), and on *Yes*
the copy starts elevated, sees the helper flag on its command line, deletes what
it was told to, and exits without ever creating a window.

`RunHelperIfRequested` is the host's half of that contract. Called first thing
in `main()`, it turns the elevated copy into that helper before any UI exists.
It is also what makes `IsAvailable()` true: a host that never calls it never
offers the retry, because relaunching an executable that would open a second
main window instead of deleting anything helps nobody. The DemoApp's Filer
page, for example, keeps the plain *Try again / Skip* choice.

## What it does not do

- **Change the user's rights for anything else.** The elevated process lives
  for the one operation and exits. The application itself stays a standard
  user's process.
- **Remember the consent.** Every retry asks again, exactly as Explorer does.
- **Delete anything the user did not name.** The helper takes the absolute
  paths on its command line and nothing else; a relative path is refused and
  reported, not resolved against whatever the elevated process's working
  directory happens to be. The one file it reads is one it writes — the
  report — and only after the deletes.
- **Get past a sharing violation.** "In use by another program" is not a
  permission problem; `IsPermissionFailure` says no for it and the widget keeps
  offering *Try again* there.
- **Help when already elevated.** When the process runs as administrator and
  the system still says no (a file owned by TrustedInstaller, say), asking
  again cannot change the answer, so `IsAvailable()` is false then.

## API

| Function | Purpose |
|---|---|
| `RunHelperIfRequested(argc, argv, exitCode)` | Host side, first in `main()`. True: this process was the helper, the work is done, return `exitCode`. False: an ordinary start. Calling it at all installs the retry. |
| `IsAvailable()` | A backend exists, the helper is installed, and this process is not already elevated. |
| `ProcessIsElevated()` | The process token's elevation state (Windows); false elsewhere. |
| `IsPermissionFailure(ec)` | Is this `std::error_code` from a filesystem call the refusal administrator rights may resolve (`ERROR_ACCESS_DENIED` / `EACCES`)? |
| `DeleteElevated(paths)` | Delete these entries (files, or folders with everything in them) through the helper. **Blocks** until the consent prompt is answered and the helper has exited — run it off the UI thread. One prompt for the whole list. |

`DeleteElevated` returns an `ElevatedDeleteResult`:

| `outcome` | Meaning |
|---|---|
| `Completed` | The helper ran. `failures` lists what it still could not delete, each with the system's own reason (*"The process cannot access the file because it is being used by another process"*). Empty = everything went. |
| `Declined` | The user answered *No* to the consent prompt (or it timed out). Nothing was touched. |
| `Failed` | The helper could not be started; `error` says why. |
| `Unavailable` | No backend on this platform, the host never called `RunHelperIfRequested`, or the process is already elevated. |

What the helper *did* delete is not reported: the caller reads it off the disk
(`std::filesystem::exists`), which is also the truth when the helper died
before it could write anything.

## The helper's contract

```
<exe> --uc-elevated-delete <report file> <absolute path>...
```

The helper deletes each path in order, lifting read-only attributes first (on
the entry and, for a folder, on everything in it — a read-only file cannot be
removed on Windows, and getting things out of the way is the point). It writes
one `<path>\t<reason>` line per failure into the report file (UTF-8, LF) —
an empty file for a clean run, so the caller can tell *nothing failed* from
*the helper never got this far* — and exits `0` when everything went, `1` when
something is left, `2` when the arguments made no sense.

The caller creates the report file in its own temp folder before the launch
(`GetTempFileName`), passes its name, reads and deletes it afterwards. The
command line is the only input: paths are quoted the way
`CommandLineToArgvW` unquotes them (`QuoteCommandLineArgument`), and a list too
long for one command line is split (`SplitIntoCommandLines`) — each part is
one launch and therefore one consent prompt, which only a selection of many
thousands of protected entries ever reaches.

The helper's argv comes from `GetCommandLineW`, not from the C runtime's narrow
`argv`: on a Windows older than 1903, where the manifest's UTF-8 code page does
not apply, the narrow one mangles a path with characters outside the system
code page.

## Backends

| Platform | Backend |
|---|---|
| Windows | `ShellExecuteEx("runas")` + the process token's `TokenElevation`, in `OS/MSWindows/UltraCanvasWindowsElevatedFileOperations.cpp`. |
| Linux, macOS, WebAssembly | None: `IsAvailable()` is false and `DeleteElevated` answers `Unavailable`. The platform-free half (the helper's delete, the encodings) lives in `core/UltraCanvasElevatedFileOperations.cpp` and is what `Tests/ElevatedFileOperationsTest.cpp` exercises everywhere. |

A `pkexec` / authorization-services backend for the Unix desktops would slot in
behind the same four `Native*` functions.
