// OS/MacOS/UltraCanvasMacOSRemovableDrives.mm
// The macOS backend of UltraCanvasRemovableDrives: the mounted volumes'
// resource values say which are removable, ejectable or external, and
// NSWorkspace's unmountAndEjectDeviceAtURL does what Finder's Eject does -
// every volume of the device unmounted, then the device ejected.
//
// One entry per volume: a drive with several partitions shows each, and
// removing any of them removes the whole device. The id is the volume's
// mount path.
// Version: 1.0.0
// Last Modified: 2026-10-31
// Author: UltraCanvas Framework

#include "UltraCanvasRemovableDrivesBackend.h"

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

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
} // namespace

std::string BackendName() { return "macos"; }

std::vector<RemovableDrive> List(std::string& error) {
    (void)error;
    std::vector<RemovableDrive> out;
    @autoreleasepool {
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
            RemovableDrive drive;
            drive.id = ToUtf8(volume.path);
            drive.name = [name isKindOfClass:[NSString class]] ? ToUtf8((NSString*)name) : ToUtf8(volume.lastPathComponent);
            drive.bus = "External";
            drive.sizeBytes = [NumberOf(volume, NSURLVolumeTotalCapacityKey) unsignedLongLongValue];
            drive.mountPoints.push_back(drive.id);
            drive.labels.push_back(drive.name);
            out.push_back(drive);
        }
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
