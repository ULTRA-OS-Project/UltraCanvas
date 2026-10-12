# UltraCanvasInstanceLock

Lets an application run **one copy per user**: the lock a program takes at
start so that a second start can tell the first one is still running.

```cpp
#include "UltraCanvasInstanceLock.h"

InstanceLock lock;   // keep it alive as long as the application runs
if (lock.Acquire(dataDir + "/instance.lock") == InstanceLockResult::HeldElsewhere) {
    // Another copy is running: hand it whatever this start was asked to do
    // (UltraMessage is the usual way), then quit.
    return 0;
}
```

<!-- doc-check: std::string dataDir -->

## The lock is not the file

The file only gives the lock somewhere to live. The lock itself is an
operating-system lock on that file, and **the system drops it when the process
ends, however it ends** — a normal exit, a crash, `kill -9`, a power cut. A
crashed application therefore never leaves a stale lock that stops the next
start, and nothing has to delete or clean up the file; it stays and is reused.
On POSIX the file holds the holder's process id, for a person looking at it;
nothing reads it back.

## API

| Call | Meaning |
|---|---|
| `InstanceLock::Acquire(path, error = nullptr)` | Tries, **without waiting**, to take the lock on the file at `path` (UTF-8), creating the file and its folder when missing. Returns an `InstanceLockResult`; `error` receives the reason on `Failed`. Again while held: `Acquired`. |
| `InstanceLock::Release()` | Lets go. The destructor does the same. |
| `InstanceLock::IsHeld()` | Whether this object holds the lock. |
| `InstanceLockAvailable()` | Whether this build can lock at all. False means `Acquire()` always answers `Unsupported`. |

| `InstanceLockResult` | Meaning |
|---|---|
| `Acquired` | This process holds the lock. |
| `HeldElsewhere` | Another process holds it: another copy is running. |
| `Failed` | The file could not be opened or locked (an unwritable folder). Usually best treated as "run anyway": better two copies than none. |
| `Unsupported` | No backend on this platform. |

The lock is per **file**, so it is per user exactly when the path is: put it
in the user's data folder. Two `InstanceLock` objects in one process also
exclude each other, which is what the test relies on.

## Backends

| Platform | Backend |
|---|---|
| Linux, the BSDs, macOS | `flock` with `LOCK_EX \| LOCK_NB` on the file opened `O_CLOEXEC`, so a program the application starts does not inherit the lock. |
| Windows | `LockFileEx` with `LOCKFILE_EXCLUSIVE_LOCK \| LOCKFILE_FAIL_IMMEDIATELY` on the first byte. The file is opened sharing read, write and delete, so a virus scanner or backup program that opens it is never mistaken for a second copy. |
| Android, WebAssembly | None: one process per application there anyway. `Acquire()` answers `Unsupported`. |

The platform decision is the single `ULTRACANVAS_HAS_NATIVE_INSTANCE_LOCK`
condition in the top-level `CMakeLists.txt`; the library and
`Tests/InstanceLockTest.cpp` (ctest: `InstanceLockTest`) both follow it.

## Who uses it

UltraMail (`Apps/UltraMail/ui/UltraMailSingleInstance.h`): a second start asks
the running copy, over UltraMessage, to bring its window forward, and quits.
