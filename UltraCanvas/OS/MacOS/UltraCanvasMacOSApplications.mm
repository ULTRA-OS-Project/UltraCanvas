// OS/MacOS/UltraCanvasMacOSApplications.mm
// The installed applications on macOS, for UltraCanvasDesktopShell::
// ListApplications and LaunchApplication: macOS keeps no freedesktop
// desktop entries, so the core asks the backend (ListNativeApplications),
// and this file answers from the application bundles.
//
//   - Where: /Applications, /Applications/Utilities, /System/Applications,
//     /System/Applications/Utilities and ~/Applications, and one level of
//     subfolders below them (/Applications/Adobe Photoshop 2025/...). A
//     bundle that declares itself an agent or a background-only program
//     (LSUIElement, LSBackgroundOnly) is not one a person starts, and is
//     left out; so is a second copy of a bundle identifier.
//   - The entry: the name Finder shows (localized), CFBundleName as the
//     generic name, the bundle's path as program, exec and shortcutPath -
//     so LaunchApplication opens it here (LaunchShortcut), and a subfolder
//     holding several applications reads as that folder's - and
//     categories = {LSApplicationCategoryType} when the bundle declares one
//     ("public.app-category.productivity").
//   - The icon: the one Finder shows (NSWorkspace iconForFile:), drawn into
//     a PNG of twice the size asked (for a 2x display) in the disk cache's
//     "desktop-icons" folder (~/Library/Caches/UltraCanvas), named after the
//     bundle and the change time of its Info.plist, stamped on every use and
//     swept by the shared disk-cache rule as on Windows.
//
// Only the application list and launch are native on macOS
// (ULTRACANVAS_DESKTOPSHELL_NATIVE_APPLICATIONS); the window list and the
// rest of the backend stay the core's fallback. The calls run on any thread
// - UltraDesktop's Apps window reads the list on a worker - which is why the
// icon is drawn into a bitmap of its own and never onto a window.
// Version: 1.0.0
// Last Modified: 2026-11-01
// Author: UltraCanvas Framework

#include "UltraCanvasDesktopShellBackend.h"
#include "UltraCanvasDiskCache.h"
#include "UltraCanvasDebug.h"

#ifdef ULTRACANVAS_DESKTOPSHELL_NATIVE_APPLICATIONS

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace DesktopShellBackend {

namespace {

std::string ToUtf8(NSString* text) {
    return text ? std::string([text UTF8String] ?: "") : std::string();
}

NSString* ToNS(const std::string& text) {
    return [NSString stringWithUTF8String:text.c_str()] ?: @"";
}

// An Info.plist value when it is a non-empty string, else nil.
NSString* StringOf(id value) {
    return [value isKindOfClass:[NSString class]] && [(NSString*)value length] ? (NSString*)value : nil;
}

bool IsTrue(id value) {
    if ([value isKindOfClass:[NSNumber class]]) return [(NSNumber*)value boolValue];
    if ([value isKindOfClass:[NSString class]]) {
        NSString* text = [(NSString*)value lowercaseString];
        return [text isEqualToString:@"1"] || [text isEqualToString:@"yes"] || [text isEqualToString:@"true"];
    }
    return false;
}

std::string HashKey(const std::string& text) {
    uint64_t hash = 1469598103934665603ull;   // FNV-1a, 64 bit
    for (unsigned char c : text) {
        hash ^= static_cast<uint64_t>(c);
        hash *= 1099511628211ull;
    }
    char buffer[17] = {};
    std::snprintf(buffer, sizeof buffer, "%016llx", static_cast<unsigned long long>(hash));
    return buffer;
}

std::string IconCacheDirectory() {
    const std::string dir = DiskCache::Directory("desktop-icons");
    if (dir.empty()) return dir;
    static std::once_flag sweepOnce;
    std::call_once(sweepOnce, [dir]() { DiskCache::Sweep(dir, {".png", ".tmp"}); });
    return dir;
}

// The bundle's icon as a PNG of `pixels` square; "" when there is no cache
// or nothing could be drawn.
std::string BundleIconFile(NSString* bundlePath, NSDate* changed, int pixels) {
    const std::string dir = IconCacheDirectory();
    if (dir.empty() || pixels <= 0) return {};
    const double stamp = changed ? [changed timeIntervalSince1970] : 0.0;
    const std::string key = ToUtf8(bundlePath) + "|" + std::to_string(static_cast<long long>(stamp)) + "|" +
                            std::to_string(pixels);
    const std::string file = dir + "/" + HashKey(key) + ".png";
    NSString* path = ToNS(file);
    if ([[NSFileManager defaultManager] fileExistsAtPath:path]) {
        DiskCache::Touch(file);
        return file;
    }
    NSImage* icon = [[NSWorkspace sharedWorkspace] iconForFile:bundlePath];
    if (!icon) return {};
    NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:nullptr
                                                                       pixelsWide:pixels
                                                                       pixelsHigh:pixels
                                                                    bitsPerSample:8
                                                                  samplesPerPixel:4
                                                                         hasAlpha:YES
                                                                         isPlanar:NO
                                                                   colorSpaceName:NSDeviceRGBColorSpace
                                                                      bytesPerRow:0
                                                                     bitsPerPixel:0];
    if (!bitmap) return {};
    [bitmap setSize:NSMakeSize(pixels, pixels)];
    NSGraphicsContext* context = [NSGraphicsContext graphicsContextWithBitmapImageRep:bitmap];
    bool written = false;
    if (context) {
        // The current context is per thread: this does not touch the UI's.
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:context];
        [icon drawInRect:NSMakeRect(0, 0, pixels, pixels)
                fromRect:NSZeroRect
               operation:NSCompositingOperationCopy
                fraction:1.0];
        [context flushGraphics];
        [NSGraphicsContext restoreGraphicsState];
        NSData* png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        written = png && [png writeToFile:path atomically:YES];
    }
#if !__has_feature(objc_arc)
    [bitmap release];   // with or without ARC: CMake's flag for *.mm may not apply
#endif
    return written ? file : std::string();
}

// The bundles in `folder`, and in its subfolders when `descend` is set
// (one level: a maker's folder of applications).
void CollectBundles(NSString* folder, bool descend, NSMutableArray<NSString*>* out) {
    NSFileManager* files = [NSFileManager defaultManager];
    NSArray<NSString*>* names = [files contentsOfDirectoryAtPath:folder error:nil];
    for (NSString* name in names) {
        if ([name hasPrefix:@"."]) continue;
        NSString* path = [folder stringByAppendingPathComponent:name];
        if ([[name pathExtension] caseInsensitiveCompare:@"app"] == NSOrderedSame) {
            [out addObject:path];
            continue;
        }
        BOOL isFolder = NO;
        if (!descend || ![files fileExistsAtPath:path isDirectory:&isFolder] || !isFolder) continue;
        // Utilities is listed as a place of its own.
        if ([name isEqualToString:@"Utilities"]) continue;
        CollectBundles(path, false, out);
    }
}

} // namespace

bool ListNativeApplications(int iconSize, std::vector<UCDesktopEntry>& out) {
    out.clear();
    @autoreleasepool {
        NSMutableArray<NSString*>* bundles = [NSMutableArray array];
        // The user's own first: a copy there wins over the system's.
        CollectBundles([NSHomeDirectory() stringByAppendingPathComponent:@"Applications"], true, bundles);
        CollectBundles(@"/Applications", true, bundles);
        CollectBundles(@"/Applications/Utilities", false, bundles);
        CollectBundles(@"/System/Applications", true, bundles);
        CollectBundles(@"/System/Applications/Utilities", false, bundles);

        NSFileManager* files = [NSFileManager defaultManager];
        std::set<std::string> seen;   // bundle identifiers, else paths
        for (NSString* path in bundles) {
            NSBundle* bundle = [NSBundle bundleWithPath:path];
            NSDictionary* info = bundle ? [bundle infoDictionary] : nil;
            if (!info) continue;
            if (IsTrue(info[@"LSUIElement"]) || IsTrue(info[@"LSBackgroundOnly"])) continue;
            NSString* identifier = [bundle bundleIdentifier];
            const std::string key = identifier ? ToUtf8([identifier lowercaseString]) : ToUtf8(path);
            if (!seen.insert(key).second) continue;

            UCDesktopEntry entry;
            entry.kind = UCDesktopEntry::Kind::Application;
            // The name Finder shows, in the person's language; the bundle's
            // own names as the generic name.
            NSString* shown = [files displayNameAtPath:path];
            if ([[shown pathExtension] caseInsensitiveCompare:@"app"] == NSOrderedSame) {
                shown = [shown stringByDeletingPathExtension];
            }
            NSDictionary* localized = [bundle localizedInfoDictionary];
            NSString* displayName = StringOf(localized[@"CFBundleDisplayName"]) ?: StringOf(info[@"CFBundleDisplayName"]);
            NSString* bundleName = StringOf(localized[@"CFBundleName"]) ?: StringOf(info[@"CFBundleName"]);
            entry.name = ToUtf8(StringOf(shown) ?: (displayName ?: bundleName));
            if (entry.name.empty()) entry.name = ToUtf8([[path lastPathComponent] stringByDeletingPathExtension]);
            if (bundleName.length && ToUtf8(bundleName) != entry.name) entry.genericName = ToUtf8(bundleName);

            const std::string bundlePath = ToUtf8(path);
            entry.program = bundlePath;
            entry.exec = "open -a \"" + bundlePath + "\"";
            entry.shortcutPath = bundlePath;   // LaunchShortcut opens it; its folder may be a category
            if (identifier) entry.startupWMClass = ToUtf8(identifier);
            if (NSString* category = StringOf(info[@"LSApplicationCategoryType"])) {
                entry.categories.push_back(ToUtf8(category));
            }

            NSDictionary* attributes =
                [files attributesOfItemAtPath:[path stringByAppendingPathComponent:@"Contents/Info.plist"] error:nil];
            entry.iconFile = BundleIconFile(path, attributes[NSFileModificationDate],
                                            std::max(16, iconSize) * 2);
            out.push_back(std::move(entry));
        }
    }
    return true;
}

bool LaunchShortcut(const std::string& shortcutPath, std::string& error) {
    @autoreleasepool {
        NSString* path = ToNS(shortcutPath);
        if (![[NSFileManager defaultManager] fileExistsAtPath:path]) {
            error = shortcutPath + " is not there any more.";
            return false;
        }
        NSURL* url = [NSURL fileURLWithPath:path];
        if ([[path pathExtension] caseInsensitiveCompare:@"app"] != NSOrderedSame) {
            // Not a bundle: opened as Finder opens it.
            if ([[NSWorkspace sharedWorkspace] openURL:url]) return true;
            error = "macOS could not open " + shortcutPath + ".";
            return false;
        }
        // Started as Finder starts it, and brought to the front. The answer
        // comes later, on a queue of its own: a failure is logged there.
        NSWorkspaceOpenConfiguration* configuration = [NSWorkspaceOpenConfiguration configuration];
        configuration.activates = YES;
        const std::string name = shortcutPath;
        [[NSWorkspace sharedWorkspace]
            openApplicationAtURL:url
                   configuration:configuration
               completionHandler:^(NSRunningApplication* /*application*/, NSError* failure) {
                   if (failure) {
                       debugOutput << "UltraCanvas macOS: could not start " << name << ": "
                                   << ToUtf8(failure.localizedDescription) << std::endl;
                   }
               }];
        return true;
    }
}

} // namespace DesktopShellBackend
} // namespace UltraCanvas

#endif // ULTRACANVAS_DESKTOPSHELL_NATIVE_APPLICATIONS
