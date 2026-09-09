# UltraCanvasVolumeMonitor

The mounted volumes of this machine, and a notification when that set changes —
a USB stick plugged in or pulled out, a card reader, an optical disc, a network
share mapped or dropped, a disk image attached.

```cpp
#include "UltraCanvasVolumeMonitor.h"

for (const MountedVolume& v : ListMountedVolumes())
    AddDriveRow(v.path, v.label);

UltraCanvasVolumeMonitor monitor;
monitor.Start([this]() { volumesDirty.store(true); });  // background thread!
```

Two halves that are useful apart. `ListMountedVolumes()` answers *what is
mounted right now* and is the single enumeration the whole framework uses —
the folder tree of a file manager, the "Computer" dropdown of a
[path strip](UltraCanvasBreadcrumbExamples.md), a places list. The monitor says
*when to ask again*. A caller that only lists at start-up needs the first alone.

## Why one enumeration

The Filer's folder tree and the path strip's drive dropdown used to answer the
question separately, and disagreed. One scanned `/media` and `/mnt`, the other
`/media`, `/mnt` and `/Volumes`, and **neither looked at `/run/media`**, where
udisks2 mounts on Fedora, RHEL, Arch and openSUSE — so on those distributions a
USB stick was invisible in both places, restart or not. The strip also offered
any directory it found as a drive, including the empty mount-point folder an
unmount leaves behind, which then led nowhere.

## The callback runs on the monitor's thread

(On macOS, on the main thread.) It must do nothing but hand the news over — set
an atomic, post to the UI thread — and return.

One insertion produces **several** callbacks: the device, then the volume, then
the mount. Coalesce on the receiving side; the monitor deliberately does not
debounce for you, because how long to wait is the caller's decision.

## API

| Call | Meaning |
|---|---|
| `ListMountedVolumes()` | Every volume mounted right now, system root first, the rest sorted by path. Each is a `MountedVolume { path, label, isSystemRoot }`. |
| `ListVolumeRoots()` | The same list, mount points only — the shape a caller that just navigates wants. |
| `Start(onChanged)` | Report mounts and unmounts until `Stop()`. Uses the platform's notification where there is one and a polling thread where there is not, so it only fails on a missing callback. |
| `Stop()` | Stop reporting. Joins whatever is running, so **no callback runs after it returns** — which is what makes it safe for the callback to capture the caller. Safe to call twice. |
| `IsRunning()` / `IsNative()` | Whether it is watching, and whether the operating system is doing the reporting rather than the fallback thread. |
| `SetPollIntervalMs(ms)` / `GetPollIntervalMs()` | How often the fallback thread re-lists (default 2000, floor 250). Ignored while a native backend runs; takes effect on the next `Start()`. |
| `NativeBackendAvailable()` | *static.* Whether this build has a backend at all. |

The destructor calls `Stop()`.

`MountedVolume::label` is derived from the path: the drive letter with its colon
on Windows (`"C:"`), the mount point's own name elsewhere (`"USB STICK"`). The
system root is labelled `"/"` — an application that calls it something
friendlier ("File System", "Computer") supplies that word itself rather than
having it baked into the framework.

## What it never does

It does not read volume labels off the medium. `GetVolumeInformationW` and
friends touch the disk, which spins up an empty optical drive and waits out the
timeout of every disconnected network mapping — seconds of stall on a list that
is rebuilt on every navigation. The mount table alone answers the question.

## Backends

| Platform | Backend |
|---|---|
| Linux, BSD | `poll()` on `/proc/self/mountinfo` — the kernel reports `POLLPRI` when the mount table changes. A thread waits on that plus a self-pipe so `Stop()` wakes it immediately |
| Windows | `WM_DEVICECHANGE` on a hidden top-level window with its own thread and message loop. It has to be a *real* top-level window: broadcast messages are not delivered to message-only (`HWND_MESSAGE`) windows |
| macOS | `NSWorkspace` mount / unmount / rename notifications, on its own notification centre (the default one does not carry them) |
| Android, WebAssembly, anything else | none — `Start()` runs the polling thread instead, so callers never need a fallback of their own |

Platform code lives under `UltraCanvas/OS/<Platform>/`; the core file's only
operating system calls are the ones the mount test needs. A platform is wired up
by dropping its backend there and adding it to the
`ULTRACANVAS_HAS_NATIVE_VOLUME_MONITOR` condition in the **top-level**
`CMakeLists.txt` — that decision is made above the subdirectories because the
library and `Tests/` both consume it, and without the define the core file
compiles null-returning fallbacks, so the two scopes disagreeing is a duplicate
symbol at link time in one direction and a missing one in the other.

## Deciding what is a mount point

Two tests, either of which is enough:

- **The platform's mount table** (`ListPlatformMountPoints()`, also per
  platform: `/proc/self/mounts` on Linux, `getmntinfo()` on macOS,
  `GetLogicalDrives()` on Windows). Exact, and touches no volume — which
  matters, because stat'ing a wedged network mount blocks.
- **A device that differs from the parent directory's.** Needs no table, but
  cannot see a mount that shares a device with what it is mounted on — a bind
  mount from the same filesystem is exactly that.

Neither produces a false positive (a differing device *is* a mount), so the
union of the two is used. This is what keeps the empty placeholder directories
udisks and hand-made `/mnt` entries leave behind out of the drive list.

## Where volumes are looked for

`/media` and `/run/media` (both also one level down, for the per-user directory
udisks creates: `/media/bob/USB STICK`), plus `/Volumes` and `/mnt` flat. On
Windows the mount table is the drive letters, and there is nothing to scan.

## Reacting to a change

The change notification says only *something moved* — diff the new
`ListMountedVolumes()` against the last one you saw. UltraFiler's
`RefreshDriveNodes()` is the worked example: it adds a tree row for every volume
that appeared, removes the rows of volumes that are gone along with everything
the tree remembered about them, and moves any tab that was inside a vanished
volume back to the home folder.
