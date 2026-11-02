// OS/MacOS/UltraCanvasMacOSRemovableDrives.mm
// The macOS backend of UltraCanvasRemovableDrives: the mounted volumes'
// resource values say which are removable, ejectable or external;
// DiskArbitration says which disk each is on, so the volumes of one drive
// are one entry, named after the drive (its vendor and model), with its
// size and bus. NSWorkspace's unmountAndEjectDeviceAtURL then does what
// Finder's Eject does - every volume of the device unmounted, then the
// device ejected.
//
// The id is the path of one of the drive's volumes (ejecting one ejects
// them all). The volumes of an APFS container are grouped by the physical
// disk under the container (the IOKit service plane, from the container's
// synthesized disk up), so a drive with two containers is one entry; a
// build without DiskArbitration lists one entry per volume, as 1.0.0 did.
// Version: 1.2.0 - APFS: grouped by the physical disk, not the container
// Version: 1.1.0 - one entry per drive (DiskArbitration's whole disk), named
//                  by its vendor and model, with its size and bus
// Version: 1.0.0
// Last Modified: 2026-10-31
// Author: UltraCanvas Framework

#include "UltraCanvasRemovableDrivesBackend.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#if __has_include(<DiskArbitration/DiskArbitration.h>)
    #import <DiskArbitration/DiskArbitration.h>
    #include <IOKit/IOKitLib.h>
    #include <IOKit/IOBSD.h>
    #define ULTRACANVAS_DRIVES_HAS_DISKARBITRATION 1
#endif

#include <map>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace RemovableDrivesBackend {

namespace {
    std::string ToUtf8(NSString* text) {
        return text ? std::string([text UTF8String] ?: "") : std::string();
    }

    NSNumber* NumberOf(NSURL* url, NSURLResourceKey key) {
        id value = nil;
        [url getResourceValue:&value forKey:key error:nil];
        return [value isKindOfClass:[NSNumber class]] ? (NSNumber*)value : nil;
    }

    // What DiskArbitration says about the disk a volume is on.
    struct WholeDisk {
        std::string bsdName;        // "disk4": the key the volumes are grouped by
        std::string name;           // "SanDisk Ultra"
        std::string bus;
        uint64_t size = 0;
    };

    std::string BusOfProtocol(const std::string& protocol) {
        if (protocol == "USB") return "USB";
        if (protocol == "Secure Digital") return "SD card";
        if (protocol == "FireWire") return "FireWire";
        return "External";
    }

#ifdef ULTRACANVAS_DRIVES_HAS_DISKARBITRATION
    std::string StringOfKey(CFDictionaryRef description, CFStringRef key) {
        CFTypeRef value = CFDictionaryGetValue(description, key);
        if (!value || CFGetTypeID(value) != CFStringGetTypeID()) return "";
        NSString* text = (__bridge NSString*)value;
        return std::string([[text stringByTrimmingCharactersInSet:[NSCharacterSet whitespaceCharacterSet]] UTF8String] ?: "");
    }

    // The physical disk under `bsdName`: for an APFS container's
    // synthesized disk ("disk5") the disk that holds its physical store
    // ("disk4"), found by walking the IOKit service plane up from the
    // container's IOMedia to the topmost whole IOMedia; any other whole
    // disk is its own. "" when IOKit does not know the name.
    std::string PhysicalWholeDisk(const std::string& bsdName) {
        CFMutableDictionaryRef matching = IOBSDNameMatching(MACH_PORT_NULL, 0, bsdName.c_str());
        if (!matching) return "";
        io_registry_entry_t entry = IOServiceGetMatchingService(MACH_PORT_NULL, matching);   // consumes `matching`
        if (!entry) return "";
        std::string top = bsdName;
        for (;;) {
            io_registry_entry_t parent = 0;
            if (IORegistryEntryGetParentEntry(entry, kIOServicePlane, &parent) != KERN_SUCCESS) break;
            IOObjectRelease(entry);
            entry = parent;
            if (!IOObjectConformsTo(entry, "IOMedia")) continue;
            CFTypeRef whole = IORegistryEntryCreateCFProperty(entry, CFSTR("Whole"), kCFAllocatorDefault, 0);
            const bool isWhole = whole && CFGetTypeID(whole) == CFBooleanGetTypeID() &&
                                 CFBooleanGetValue(static_cast<CFBooleanRef>(whole));
            if (whole) CFRelease(whole);
            if (!isWhole) continue;
            CFTypeRef name = IORegistryEntryCreateCFProperty(entry, CFSTR("BSD Name"), kCFAllocatorDefault, 0);
            if (name && CFGetTypeID(name) == CFStringGetTypeID()) {
                top = std::string([(__bridge NSString*)name UTF8String] ?: "");
            }
            if (name) CFRelease(name);
        }
        IOObjectRelease(entry);
        return top;
    }

    bool WholeDiskOf(DASessionRef session, NSURL* volume, WholeDisk& out) {
        DADiskRef disk = DADiskCreateFromVolumePath(kCFAllocatorDefault, session, (__bridge CFURLRef)volume);
        if (!disk) return false;
        DADiskRef whole = DADiskCopyWholeDisk(disk);
        CFRelease(disk);
        if (!whole) return false;
        const char* bsd = DADiskGetBSDName(whole);
        out.bsdName = bsd ? bsd : "";
        // An APFS container's volumes sit on a synthesized disk: name, size
        // and bus are the physical disk's, and so is the grouping, so that
        // two containers on one drive are one entry.
        const std::string physical = out.bsdName.empty() ? "" : PhysicalWholeDisk(out.bsdName);
        if (!physical.empty() && physical != out.bsdName) {
            if (DADiskRef store = DADiskCreateFromBSDName(kCFAllocatorDefault, session, physical.c_str())) {
                CFRelease(whole);
                whole = store;
                out.bsdName = physical;
            }
        }
        CFDictionaryRef description = DADiskCopyDescription(whole);
        if (description) {
            const std::string vendor = StringOfKey(description, kDADiskDescriptionDeviceVendorKey);
            const std::string model = StringOfKey(description, kDADiskDescriptionDeviceModelKey);
            out.name = vendor.empty() ? model : model.empty() ? vendor : vendor + " " + model;
            out.bus = BusOfProtocol(StringOfKey(description, kDADiskDescriptionDeviceProtocolKey));
            CFTypeRef size = CFDictionaryGetValue(description, kDADiskDescriptionMediaSizeKey);
            if (size && CFGetTypeID(size) == CFNumberGetTypeID()) {
                out.size = [(__bridge NSNumber*)size unsignedLongLongValue];
            }
            CFRelease(description);
        }
        CFRelease(whole);
        return !out.bsdName.empty();
    }
#endif
} // namespace

std::string BackendName() { return "macos"; }

std::vector<RemovableDrive> List(std::string& error) {
    (void)error;
    std::vector<RemovableDrive> out;
    std::map<std::string, size_t> byDisk;   // whole disk -> index in `out`
    @autoreleasepool {
#ifdef ULTRACANVAS_DRIVES_HAS_DISKARBITRATION
        DASessionRef session = DASessionCreate(kCFAllocatorDefault);
#endif
        NSArray<NSURLResourceKey>* keys = @[ NSURLVolumeNameKey, NSURLVolumeIsRemovableKey, NSURLVolumeIsEjectableKey,
                                             NSURLVolumeIsInternalKey, NSURLVolumeIsLocalKey,
                                             NSURLVolumeTotalCapacityKey, NSURLVolumeIsRootFileSystemKey ];
        NSArray<NSURL*>* volumes =
            [[NSFileManager defaultManager] mountedVolumeURLsIncludingResourceValuesForKeys:keys
                                                                                     options:NSVolumeEnumerationSkipHiddenVolumes];
        for (NSURL* volume in volumes) {
            if ([NumberOf(volume, NSURLVolumeIsRootFileSystemKey) boolValue]) continue;
            if (![NumberOf(volume, NSURLVolumeIsLocalKey) boolValue]) continue;   // network shares
            const bool removable = [NumberOf(volume, NSURLVolumeIsRemovableKey) boolValue];
            const bool ejectable = [NumberOf(volume, NSURLVolumeIsEjectableKey) boolValue];
            NSNumber* internal = NumberOf(volume, NSURLVolumeIsInternalKey);
            const bool external = internal && ![internal boolValue];
            if (!removable && !ejectable && !external) continue;
            id name = nil;
            [volume getResourceValue:&name forKey:NSURLVolumeNameKey error:nil];
            const std::string label =
                [name isKindOfClass:[NSString class]] ? ToUtf8((NSString*)name) : ToUtf8(volume.lastPathComponent);
            const std::string path = ToUtf8(volume.path);
            const uint64_t volumeSize = [NumberOf(volume, NSURLVolumeTotalCapacityKey) unsignedLongLongValue];

            WholeDisk disk;
#ifdef ULTRACANVAS_DRIVES_HAS_DISKARBITRATION
            if (session) WholeDiskOf(session, volume, disk);
#endif
            auto known = disk.bsdName.empty() ? byDisk.end() : byDisk.find(disk.bsdName);
            if (known != byDisk.end()) {
                RemovableDrive& drive = out[known->second];
                drive.mountPoints.push_back(path);
                drive.labels.push_back(label);
                if (disk.size == 0) drive.sizeBytes += volumeSize;
                continue;
            }
            RemovableDrive drive;
            drive.id = path;
            drive.name = disk.name.empty() ? label : disk.name;
            drive.bus = disk.bus.empty() ? "External" : disk.bus;
            drive.sizeBytes = disk.size != 0 ? disk.size : volumeSize;
            drive.mountPoints.push_back(path);
            drive.labels.push_back(label);
            if (!disk.bsdName.empty()) byDisk[disk.bsdName] = out.size();
            out.push_back(drive);
        }
#ifdef ULTRACANVAS_DRIVES_HAS_DISKARBITRATION
        if (session) CFRelease(session);
#endif
    }
    return out;
}

bool SafelyRemove(const std::string& id, std::string& error) {
    @autoreleasepool {
        NSURL* url = [NSURL fileURLWithPath:@(id.c_str()) isDirectory:YES];
        if (![[NSFileManager defaultManager] fileExistsAtPath:url.path]) {
            error = "The drive is no longer plugged in.";
            return false;
        }
        NSError* failure = nil;
        if ([[NSWorkspace sharedWorkspace] unmountAndEjectDeviceAtURL:url error:&failure]) return true;
        const std::string reason = failure ? ToUtf8(failure.localizedDescription) : std::string();
        error = "The drive could not be ejected" + (reason.empty() ? std::string(".") : ": " + reason) +
                " If a program still has a file open on it, close it and try again.";
        return false;
    }
}

} // namespace RemovableDrivesBackend
} // namespace UltraCanvas
