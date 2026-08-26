# UltraCanvasFolderWatcher

Watches **one directory** and reports when its content changes — an entry
created, deleted, renamed, or written to, by this process or any other. Not
recursive: it answers *"did the folder I am showing change"*, which is what a
file display needs.

```cpp
#include "UltraCanvasFolderWatcher.h"

UltraCanvasFolderWatcher watcher;
if (!watcher.Watch(folder, [this]() { dirty.store(true); })) {
    // No backend on this platform — fall back to polling.
}
```

## The callback runs on the watcher's thread

It must do nothing but hand the news over — set an atomic, post an event — and
return. Anything else races the UI.

One user action can produce **several** callbacks: saving a file is typically a
create, one or more writes, and a rename. Coalesce on the receiving side; the
watcher deliberately does not debounce for you, because how long to wait is the
caller's decision, not the filesystem's.

## API

| Call | Meaning |
|---|---|
| `Watch(path, onChanged)` | Watch `path`, replacing whatever was watched before. **False** when this build has no native backend, when `path` is empty or has no callback, or when the folder cannot be watched (gone, no permission, out of watch descriptors). |
| `Stop()` | Stop watching. Joins the backend thread, so **no callback runs after it returns** — which is what makes it safe for the callback to capture the caller. Safe to call twice, and safe if `Watch()` failed. |
| `IsWatching()` / `WatchedPath()` | What is being watched, if anything. |
| `NativeBackendAvailable()` | *static.* Whether this build has a backend at all. False means every `Watch()` will fail, so a caller can skip trying. |

The destructor calls `Stop()`.

## Backends

| Platform | Backend |
|---|---|
| Linux, BSD | `inotify` — one watch on the directory, a thread waiting on the queue and a self-pipe so `Stop()` wakes it immediately |
| Windows | `ReadDirectoryChangesW` — overlapped, on a directory handle opened with `FILE_SHARE_DELETE` so watching a folder never blocks renaming or deleting it |
| macOS, Android, WebAssembly | none yet — `Watch()` returns false and the caller polls |

Platform code lives under `UltraCanvas/OS/<Platform>/`; the core file holds no
operating system calls at all. A platform is wired up by dropping its backend
there and adding it to the `ULTRACANVAS_HAS_NATIVE_FOLDER_WATCH` condition in
the **top-level** `CMakeLists.txt`. That decision has to be made above the
subdirectories because the library and `Tests/` both consume it: without the
define the core file compiles a null-returning fallback, so the two scopes
disagreeing is a duplicate symbol at link time in one direction and a missing
one in the other.

## Falling back

A caller must handle `Watch()` returning false — it is the normal answer on a
platform without a backend, and a possible one everywhere (a folder can vanish
between being listed and being watched). The
[Filer widget](UltraCanvasFilerWidget.md#watching-the-shown-folder) shows the
shape: try the watcher, and keep a polling worker for when it says no.
