// include/UltraCanvasRemovableDrives.h
// The drives a person plugs in and pulls out - USB sticks, external disks,
// memory cards - and "Safely remove": every volume on the drive unmounted
// (after the system has written out what it still held), then the drive
// powered off or ejected, so it can be pulled without losing data.
//
// doc-check: std::string error;
// doc-check: example
//   for (const RemovableDrive& drive : UltraCanvasRemovableDrives::List()) {
//       if (!UltraCanvasRemovableDrives::SafelyRemove(drive.id, &error)) {
//           // error: "A program still has a file open on /media/bob/STICK."
//       }
//   }
//
// Distinct from UltraCanvasVolumeMonitor, which lists every mounted volume
// (and says when that set changes) - a desktop's USB panel lists the drives
// from here and re-reads them when the monitor fires - and from
// UltraCanvasHardwareInfo, which describes the storage devices.
//
// Both calls are synchronous: listing is quick (a system-service query),
// removing takes as long as the system needs to write out the drive's
// cached data, which on a slow stick is seconds. Call them from a worker
// thread.
//
// Backends: Linux (UDisks2 on the system bus: Filesystem.Unmount, then
// Drive.PowerOff or Eject), Windows (SetupAPI and the storage ioctls to
// find the drives, CM_Request_Device_Eject on the drive's removable parent
// to remove one - Windows' own "Safely Remove Hardware"), macOS (the
// mounted volumes' resource values; NSWorkspace's unmountAndEjectDevice).
// Elsewhere List() is empty and SafelyRemove fails saying so.
// Version: 1.0.0
// Last Modified: 2026-10-31
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASREMOVABLEDRIVES_H
#define ULTRACANVASREMOVABLEDRIVES_H

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

    struct RemovableDrive {
        // The handle SafelyRemove takes: the UDisks2 drive object on Linux,
        // the disk's device instance id on Windows, the volume's path on
        // macOS. Stable while the drive stays plugged in.
        std::string id;
        std::string name;                       // "SanDisk Ultra", the vendor and model
        std::string bus;                        // "USB", "SD card", "FireWire", "External", ""
        uint64_t sizeBytes = 0;                 // 0 when the system does not say
        // Where its volumes are mounted ("/media/bob/STICK", "E:\\") and
        // their labels, in the same order; empty for a drive with nothing
        // mounted (a blank card), which can still be removed.
        std::vector<std::string> mountPoints;
        std::vector<std::string> labels;
    };

    class UltraCanvasRemovableDrives {
    public:
        // "udisks2" on Linux, "win32" on Windows, "macos" on macOS, "null" elsewhere.
        static std::string GetBackendName();
        // The drives plugged in now, by name. The system's own drive is never
        // listed, even when it is a USB disk the computer started from.
        // `error` is set when the system service did not answer.
        static std::vector<RemovableDrive> List(std::string* error = nullptr);
        // Unmount the drive's volumes and power it off or eject it. False
        // with a sentence for the person in `error` - most often that a
        // program still has a file open on it.
        static bool SafelyRemove(const std::string& id, std::string* error = nullptr);
    };

} // namespace UltraCanvas

#endif // ULTRACANVASREMOVABLEDRIVES_H
