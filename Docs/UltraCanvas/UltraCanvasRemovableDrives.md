# UltraCanvasRemovableDrives

<!-- doc-check: std::string error; -->

The drives a person plugs in and pulls out - USB sticks, external disks,
memory cards - and "Safely remove": every volume on the drive unmounted, after
the system has written out what it still held, then the drive powered off or
ejected, so it can be pulled without losing data.

```cpp
#include "UltraCanvasRemovableDrives.h"

for (const RemovableDrive& drive : UltraCanvasRemovableDrives::List()) {
    // drive.name "SanDisk Ultra", drive.bus "USB", drive.mountPoints {"/media/bob/STICK"}
    if (!UltraCanvasRemovableDrives::SafelyRemove(drive.id, &error)) {
        // error: "A program still has a file open on /media/bob/STICK. Close it and try again."
    }
}
```

It is what the ULTRA OS desktop's USB panel ([UltraDesktop](../UltraDesktop/README.md))
offers "Safely remove" with. Distinct from
[UltraCanvasVolumeMonitor](UltraCanvasVolumeMonitor.md), which lists every
mounted volume and says when that set changes - a panel re-reads the drives
when the monitor fires - and from
[UltraCanvasHardwareInfo](UltraCanvasHardwareInfo.md), which describes the
storage devices.

Headers: `UltraCanvasRemovableDrives.h`; `UltraCanvasRemovableDrivesBackend.h`
is the internal contract with the platform backends.

## API

| Call | What it does |
|---|---|
| `List(&error)` | The drives plugged in now, by name: `RemovableDrive { id, name, bus, sizeBytes, mountPoints, labels }`. `bus` is `"USB"`, `"SD card"`, `"FireWire"`, `"External"` or `""`; `labels[i]` names `mountPoints[i]`. A drive with nothing mounted (a blank card) is listed too. The system's own drive never is, even a USB disk the computer started from. `error` is set when the system service did not answer. |
| `SafelyRemove(id, &error)` | Unmount the drive's volumes and power it off or eject it. False with a sentence for the person - most often that a program still has a file open on it. |
| `GetBackendName()` | `"udisks2"`, `"win32"`, `"macos"` or `"null"`. |

Both calls are synchronous. Listing is a quick system-service query; removing
takes as long as the system needs to write out the drive's cached data -
seconds on a slow stick - so call `SafelyRemove` from a worker thread.

## Platforms

| | List | Safely remove |
|---|---|---|
| Linux | UDisks2's drives on a USB, SD/MMC or FireWire bus or calling themselves removable, with their blocks' mount points (through an unlocked encrypted container too); `id` is the drive's UDisks2 object | `Filesystem.Unmount` on each mounted filesystem, `Encrypted.Lock` on each unlocked container, then `Drive.PowerOff` (or `Eject`). polkit lets the active local session do it without a password |
| Windows | SetupAPI's disks on a USB, SD, MMC or FireWire bus or with removable media, and their drive letters (only removable and fixed letters are opened: no optical drive spins up, no network mapping is waited for); `id` is the disk's device instance id | The volumes flushed, then `CM_Request_Device_Eject` on the disk's parent - Windows' own "Safely Remove Hardware". Windows refuses while a file is open, and the error says so |
| macOS | The mounted local volumes that are removable, ejectable or external, grouped by their disk (DiskArbitration's whole disk; the volumes of an APFS container by the container) into one entry named by the drive's vendor and model, with its size and bus; `id` is one of its volumes' mount path | `NSWorkspace unmountAndEjectDeviceAtURL`, as Finder's Eject: every volume of the device, then the device |
| Elsewhere | empty, with `error` saying so | fails saying so |
