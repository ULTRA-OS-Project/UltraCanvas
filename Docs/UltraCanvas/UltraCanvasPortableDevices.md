# UltraCanvasPortableDevices

<!-- doc-check: void AddDeviceRow(const std::string& id, const std::string& name, PortableDeviceKind kind); -->
<!-- doc-check: std::atomic<bool> devicesDirty; std::string id; -->

Phones, cameras and media players plugged in over USB that are **not a drive**.
A device that speaks MTP (Android phones, players) or PTP (cameras, an iPhone's
photos) gets no drive letter and no mount point, so nothing that lists mounted
volumes ([`UltraCanvasVolumeMonitor`](UltraCanvasVolumeMonitor.md)) sees it.
Windows Explorer reaches such a device through Windows Portable Devices (WPD);
this is the same access for an UltraCanvas application: which devices are
connected, what is in a folder on one, and copying files off it and onto it.

```cpp
#include "UltraCanvasPortableDevices.h"

for (const PortableDeviceInfo& d : UltraCanvasPortableDevices::List())
    AddDeviceRow(d.id, d.name, d.kind);

std::vector<PortableDeviceEntry> entries;
std::string error;
UltraCanvasPortableDevices::ListFolder(id, "/Internal shared storage/DCIM",
                                       entries, error);

UltraCanvasPortableDeviceMonitor monitor;
monitor.Start([this]() { devicesDirty.store(true); });   // background thread!
```

## Paths, not object ids

A device is addressed by paths, the way a disk is: `"/"` is the device, its
folders are the **storages** it offers ("Internal shared storage", "SD card"),
and below them the folders and files as the device names them. WPD itself knows
objects by id; the backend resolves a path one name at a time from the device
object and keeps every id a listing taught it, so walking down a phone's
folders costs one listing per folder, as it would on a disk. A call that fails
on an id the cache handed it — the file was deleted on the phone meanwhile —
forgets the device's ids and resolves the path afresh, once.

Paths reach the backend normalised (`NormalizePortableDevicePath`): a leading
`'/'`, no trailing one, no empty segments, a backslash read as `'/'`.

## Every call blocks

A device answers over USB, which can take a while — a DCIM folder of thousands
of photos is a property read per photo, a video is hundreds of megabytes. Run
the calls off the UI thread; UltraFiler's drive worker does. They are safe from
any thread: the Windows backend runs every WPD call on **one COM thread of its
own** and the caller's thread waits for it. That gives the devices it opened one
owner — they stay open between calls, so a second folder on the same phone does
not cost a new MTP session — and a device answers one request at a time anyway.

## API

| Call | Meaning |
|---|---|
| `Available()` | Whether this build can reach portable devices at all. |
| `List()` | The devices connected now: `PortableDeviceInfo { id, name, manufacturer, description, kind, readOnly }`. `readOnly` marks a device files can only be copied off (macOS's Image Capture): `Upload`, `MakeFolder`, `Delete` and `Rename` fail on it, so a file manager does not offer them. `id` is what every other call takes — stable while the device stays plugged in, and the same the next time it is plugged into the same port. `kind` is `Phone`, `Camera`, `MediaPlayer` or `Other`, from what the device says it is. |
| `ListFolder(id, path, out, error)` | The entries of a folder: `PortableDeviceEntry { name, isDirectory, size, modifiedTime }` (`modifiedTime` UTC seconds, 0 when unknown). `"/"` lists the storages — a phone's other services (contacts, messages) are not folders and are left out. A root with no storage at all is an error that says what to do: a locked phone, or one left on "charging only", shows none. |
| `Download(id, path, localFile, error, progress)` | Copies a file off the device into `localFile` (UTF-8), replacing it; a copy that fails part way is removed. |
| `Upload(id, localFile, path, error, progress)` | Copies a local file onto the device as `path`. A file of that name already there is **replaced** — MTP would otherwise keep both under one name, so the caller asks first. |
| `MakeFolder(id, path, error)` | Creates a folder; its parent must exist. |
| `Delete(id, path, error)` | Deletes a file, or a folder with everything in it. The device and its storages refuse. |
| `Rename(id, path, newName, error)` | Renames in place; `newName` is a bare name. |
| `GetSpace(id, total, free, error)` | The device's capacity and free space in bytes, its storages added up — a phone's internal storage and its SD card are one device to the user. Read off the storage objects, so no folder is walked. Fails, with a reason, on a device that shows no storage (locked, or on "charging only"). |

`progress` is asked as a transfer moves — `(bytesDone, bytesTotal)`, total 0
when the device did not say — and returning `false` calls the transfer off; the
call then fails with `"cancelled"`. An upload called off leaves nothing on the
device: the object is only made when the copy is committed.

## The monitor

`UltraCanvasPortableDeviceMonitor` reports a device being plugged in or pulled
out. It compares the connected devices every interval (`SetPollIntervalMs`,
default 2000 ms): asking Windows which portable devices are present is a look at
its device list, the devices themselves are not opened for it, and it needs no
window to receive notifications on. The callback runs on the monitor's thread
and must only hand the news over. `Start()` takes the first look at once, so a
device already connected is not reported as arriving; it refuses when there is
no backend. `Stop()` joins, so no callback runs after it returns.

## Backends

| Platform | Backend |
|---|---|
| Windows | WPD (`IPortableDeviceManager`, `IPortableDevice`), built into Windows since 7 — no extra library. The class ids and property keys are defined by the backend itself (`initguid.h`), so no WPD GUID import library is linked; mingw-w64 ships none. |
| Linux | none — the desktop mounts such a device itself (GNOME's GVFS), and [`ListMountedVolumes`](UltraCanvasVolumeMonitor.md#gvfs-places) lists those places as volumes. |
| macOS | Image Capture (`ImageCaptureCore`) — an iPhone's or an iPad's photos and videos and a camera's card, the access Photos and Image Capture.app have. **Read-only** (`readOnly` set): copying off works; it cannot put a file on an iPhone, make or rename a folder, or say how full the device is (`GetSpace` fails). Image Capture answers on the main thread, so a call hands its work there and waits with a timeout — and a call made *on* the main thread fails at once rather than wait for an answer only it could deliver (`List()` excepted). The first look at a device opens a session; an iPhone must be unlocked and have accepted "Trust This Computer", and the error says so. macOS has no MTP of its own: an Android phone is not reachable. |
| Android, WebAssembly | none — `Available()` is false, `List()` is empty and every call says why. |

A platform is wired up by dropping its backend under `UltraCanvas/OS/<Platform>/`
and adding it to the `ULTRACANVAS_HAS_PORTABLE_DEVICES` condition in the
**top-level** `CMakeLists.txt`; without the define the core file compiles the
null backend. The backend object lives until the process ends on purpose: it
owns the thread the devices were opened on, and releasing them from a static
destructor, after that thread is gone, is how a program crashes on its way out.

## UltraFiler

UltraFiler carries each connected device as a drive of its remote-drive
machinery (`Apps/UltraFiler/UltraFilerDeviceProvider.h`): rows under *Computer*
and cards on the Computer page with the used and free space ring a drive card
has (`GetSpace`), the listing cache and its worker, preview
copies, copy off and onto the device, rename, delete and new folder — and the
device's account lives in a store that is never written to disk.
