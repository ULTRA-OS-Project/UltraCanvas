// OS/MacOS/UltraCanvasMacOSFileAssociations.mm
// macOS backend of UltraCanvasFileAssociations, built on NSWorkspace /
// Launch Services: URLsForApplicationsToOpenContentType: enumerates every
// application registered for a file type and URLForApplicationToOpenContentType:
// names the one Finder would use, which is listed first. Display names come
// from the bundle (CFBundleDisplayName / CFBundleName, localized name as a
// fallback) and icons from -[NSWorkspace iconForFile:], written once as PNG
// files into ~/Library/Caches/UltraCanvas/openwith-icons (the shared menu
// API is image-file based, and the cache survives restarts). Because it
// survives restarts it also has to be swept: an entry is keyed by the
// application's bundle path, so an application that is moved or removed
// orphans its PNG for good. Each file carries the day it was last served as
// its modification time, and the first lookup of a process deletes
// everything not served for two weeks.
// Launching a specific application hands the whole selection to
// -[NSWorkspace openURLs:withApplicationAtURL:configuration:completionHandler:],
// exactly like a Finder "Open With"; default open stays /usr/bin/open, and
// a user-picked application launches through `open -a` for a bundle or a
// direct detached exec otherwise.
// Enumeration needs the type-by-extension lookup that arrived with macOS 12
// (UTType); on anything older the backend reports no candidates and the
// Filer menu falls back to its manual entries plus the picker.
// All entry points are serialized by the core's backend mutex (see
// UltraCanvasFileAssociationsBackend.h) — no locking here.
// Version: 1.2.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework

#include "UltraCanvasFileAssociationsBackend.h"
#include "UltraCanvasUtils.h"   // ToLowerCase, LaunchDetachedProcess

#import <Cocoa/Cocoa.h>
#if __has_include(<UniformTypeIdentifiers/UniformTypeIdentifiers.h>)
#  import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#  define ULTRACANVAS_HAS_UTTYPE 1
#endif

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace UltraCanvas {
namespace FileAssociationsBackend {

namespace {

    // Menu icons draw at 16 logical pixels; 32 keeps them sharp on Retina.
    constexpr int kIconPixels = 32;

    // A common type (plain text, an image) can name dozens of applications.
    // A submenu does not scroll and every entry costs an icon extraction, so
    // the list is cut off — the default application is kept either way.
    constexpr size_t kMaxCandidates = 20;

    // Launch Services is a live database with no change feed worth polling,
    // so entries simply expire and the next lookup re-reads them (§7.4 of
    // the proposal). Icons stay on disk across expiries.
    constexpr int64_t kCacheLifetimeMs = 60 * 1000;

    std::string ToStdString(NSString* text) {
        return text ? std::string([text UTF8String]) : std::string();
    }

    // The extension, lowercase and without the dot; empty for a name that
    // has none (Launch Services has nothing to look up then).
    std::string ExtensionOf(const std::string& fileName) {
        const size_t slash = fileName.find_last_of('/');
        const std::string name = slash == std::string::npos
                                 ? fileName : fileName.substr(slash + 1);
        const size_t dot = name.find_last_of('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 >= name.size())
            return {};
        return ToLowerCase(name.substr(dot + 1));
    }

    // ===== ICON CACHE =====

    std::string IconCacheDir() {
        static const std::string dir = []() -> std::string {
            NSArray<NSString*>* caches = NSSearchPathForDirectoriesInDomains(
                    NSCachesDirectory, NSUserDomainMask, YES);
            std::string root = caches.count > 0 ? ToStdString(caches[0])
                                                : std::string();
            if (root.empty()) {
                const char* home = std::getenv("HOME");
                if (!home || !*home) return {};
                root = std::string(home) + "/Library/Caches";
            }
            const std::string candidate = root + "/UltraCanvas/openwith-icons";
            std::error_code ec;
            fs::create_directories(candidate, ec);
            return ec ? std::string() : candidate;
        }();
        return dir;
    }

    std::string HashKey(const std::string& text) {
        uint64_t hash = 1469598103934665603ull;          // FNV-1a, 64 bit
        for (unsigned char c : text) {
            hash ^= static_cast<uint64_t>(c);
            hash *= 1099511628211ull;
        }
        char buffer[17] = {};
        std::snprintf(buffer, sizeof buffer, "%016llx",
                      static_cast<unsigned long long>(hash));
        return buffer;
    }

    // The application's icon as a PNG file the menu can draw, extracted once
    // per bundle. Drawing into an offscreen bitmap is the one piece of
    // AppKit rendering that is safe off the main thread, which is where the
    // prewarm worker calls this from.
    std::string CachedIconFile(NSURL* applicationURL) {
        if (!applicationURL) return {};
        const std::string bundlePath = ToStdString(applicationURL.path);
        const std::string dir = IconCacheDir();
        if (bundlePath.empty() || dir.empty()) return {};
        // The cache survives restarts, so it also has to be expired: see
        // SweepIconCache in UltraCanvasFileAssociationsBackend.h. Once per
        // process, on the first lookup — cheap next to the icon rendering
        // this same call is about to do, and on the prewarm worker rather
        // than the main thread whenever that gets here first.
        static std::once_flag sweepOnce;
        std::call_once(sweepOnce, SweepIconCache, dir);
        const std::string path = dir + "/" + HashKey(bundlePath) + ".png";
        std::error_code ec;
        if (fs::exists(path, ec) && !ec) {
            StampIconCacheFile(path);   // keeps it out of the next sweep
            return path;
        }

        NSImage* icon = [[NSWorkspace sharedWorkspace]
                iconForFile:applicationURL.path];
        if (!icon) return {};
        NSBitmapImageRep* rep = [[NSBitmapImageRep alloc]
                initWithBitmapDataPlanes:NULL
                              pixelsWide:kIconPixels
                              pixelsHigh:kIconPixels
                           bitsPerSample:8
                         samplesPerPixel:4
                                hasAlpha:YES
                                isPlanar:NO
                          colorSpaceName:NSCalibratedRGBColorSpace
                             bytesPerRow:0
                            bitsPerPixel:0];
        if (!rep) return {};
        NSGraphicsContext* context =
                [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
        if (!context) return {};
        [NSGraphicsContext saveGraphicsState];
        [NSGraphicsContext setCurrentContext:context];
        [icon drawInRect:NSMakeRect(0, 0, kIconPixels, kIconPixels)
                fromRect:NSZeroRect
               operation:NSCompositingOperationSourceOver
                fraction:1.0];
        [NSGraphicsContext restoreGraphicsState];

        NSData* png = [rep representationUsingType:NSBitmapImageFileTypePNG
                                        properties:@{}];
        if (!png) return {};
        // atomically: a half-written PNG must never be left for the menu.
        NSString* target = [NSString stringWithUTF8String:path.c_str()];
        return [png writeToFile:target atomically:YES] ? path : std::string();
    }

    // ===== APPLICATION INFO =====

    std::string DisplayNameFor(NSURL* applicationURL) {
        NSBundle* bundle = [NSBundle bundleWithURL:applicationURL];
        for (NSString* key in @[@"CFBundleDisplayName", @"CFBundleName"]) {
            NSString* name = [bundle objectForInfoDictionaryKey:key];
            if (name.length > 0) return ToStdString(name);
        }
        // Finder's own label: localized, and without the ".app" suffix.
        NSString* displayed = [[NSFileManager defaultManager]
                displayNameAtPath:applicationURL.path];
        if (displayed.length > 0) return ToStdString(displayed);
        return ToStdString(applicationURL.lastPathComponent);
    }

    FileAssociationApp AppFor(NSURL* applicationURL) {
        FileAssociationApp app;
        app.id = ToStdString(applicationURL.path);   // bundle path: stable key
        app.name = DisplayNameFor(applicationURL);
        app.iconPath = CachedIconFile(applicationURL);
        return app;
    }

} // namespace

// ===== BACKEND ENTRY POINTS =====

bool RefreshGlobalIndex() {
    // Nothing to parse up front — Launch Services answers per type. What
    // this owns is the expiry: the first call starts the clock, a later one
    // past the lifetime restarts it and reports "rebuilt" so the core drops
    // its now-stale candidate cache.
    static int64_t lastRefresh = 0;
    const int64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    if (lastRefresh == 0) {
        lastRefresh = now;
        return false;      // nothing cached yet — dropping would be pointless
    }
    if (now - lastRefresh < kCacheLifetimeMs) return false;
    lastRefresh = now;
    return true;
}

std::vector<FileAssociationApp> ResolveFile(const std::string& fileName) {
    std::vector<FileAssociationApp> apps;
#if defined(ULTRACANVAS_HAS_UTTYPE)
    if (@available(macOS 12.0, *)) {
        const std::string extension = ExtensionOf(fileName);
        if (extension.empty()) return apps;
        @autoreleasepool {
            UTType* type = [UTType typeWithFilenameExtension:
                    [NSString stringWithUTF8String:extension.c_str()]];
            if (!type) return apps;
            NSWorkspace* workspace = [NSWorkspace sharedWorkspace];
            NSURL* defaultURL = [workspace URLForApplicationToOpenContentType:type];
            const std::string defaultPath = ToStdString(defaultURL.path);

            bool haveDefault = false;
            for (NSURL* url in [workspace URLsForApplicationsToOpenContentType:type]) {
                FileAssociationApp app = AppFor(url);
                if (app.id.empty() || app.name.empty()) continue;
                if (!defaultPath.empty() && !haveDefault && app.id == defaultPath) {
                    // Finder lists the default application first.
                    app.isDefault = true;
                    haveDefault = true;
                    apps.insert(apps.begin(), std::move(app));
                } else {
                    apps.push_back(std::move(app));
                }
                if (apps.size() >= kMaxCandidates) break;
            }
            // Launch Services can name a default that its own candidate list
            // leaves out; the application a double-click would use belongs in
            // the menu either way.
            if (!haveDefault && defaultURL) {
                FileAssociationApp app = AppFor(defaultURL);
                if (!app.id.empty() && !app.name.empty()) {
                    app.isDefault = true;
                    apps.insert(apps.begin(), std::move(app));
                }
            }
        }
    }
#else
    (void)fileName;   // SDK without UniformTypeIdentifiers: no enumeration
#endif
    return apps;
}

bool LaunchDefault(const std::vector<std::string>& paths, std::string& outError) {
    std::vector<std::string> argv{"open"};
    argv.insert(argv.end(), paths.begin(), paths.end());
    const std::string workingDir = paths.empty()
            ? std::string() : fs::path(paths[0]).parent_path().string();
    return LaunchDetachedProcess(argv, workingDir, outError);
}

bool LaunchWith(const FileAssociationApp& app,
                const std::vector<std::string>& paths, std::string& outError) {
    if (paths.empty()) return false;
    std::error_code ec;
    if (app.id.empty() || !fs::exists(app.id, ec) || ec) {
        outError = "The application \"" + app.name + "\" is no longer installed.";
        return false;
    }
    @autoreleasepool {
        NSMutableArray<NSURL*>* urls = [NSMutableArray array];
        for (const std::string& path : paths) {
            NSString* text = [NSString stringWithUTF8String:path.c_str()];
            if (text) [urls addObject:[NSURL fileURLWithPath:text]];
        }
        if (urls.count == 0) return false;
        NSURL* applicationURL = [NSURL fileURLWithPath:
                [NSString stringWithUTF8String:app.id.c_str()]];
        // The whole selection in one call — one window, like Finder's
        // "Open With". Launching is asynchronous and outlives this process,
        // so nothing here waits for the application to come up.
        [[NSWorkspace sharedWorkspace]
                          openURLs:urls
              withApplicationAtURL:applicationURL
                     configuration:[NSWorkspaceOpenConfiguration configuration]
                 completionHandler:nil];
    }
    return true;
}

bool LaunchWithPath(const std::string& applicationPath,
                    const std::vector<std::string>& paths, std::string& outError) {
    std::vector<std::string> argv;
    const bool isBundle = applicationPath.size() >= 4 &&
            applicationPath.compare(applicationPath.size() - 4, 4, ".app") == 0;
    if (isBundle) {
        // `open -a <bundle> <files>` is how Finder hands files to a bundle.
        argv = {"open", "-a", applicationPath};
    } else {
        argv = {applicationPath};
    }
    argv.insert(argv.end(), paths.begin(), paths.end());
    const std::string workingDir = paths.empty()
            ? std::string() : fs::path(paths[0]).parent_path().string();
    return LaunchDetachedProcess(argv, workingDir, outError);
}

FileAssociations::ApplicationFilter GetApplicationFilter() {
    return {"Applications", {"app"}};
}

std::string GetApplicationsDirectory() {
    std::error_code ec;
    return fs::is_directory("/Applications", ec) ? "/Applications" : std::string();
}

} // namespace FileAssociationsBackend
} // namespace UltraCanvas
