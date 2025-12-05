// OS/MacOS/UltraCanvasSupport.cpp
// macOS platform implementation for UltraCanvas Framework with Cairo
// Version: 2.0.0
// Last Modified: 2025-01-18
// Author: UltraCanvas Framework

#include "UltraCanvasMacOSApplication.h"
#include "UltraCanvasMacOSWindow.h"
#include "../../include/UltraCanvasApplication.h"
#include "../../include/UltraCanvasWindow.h"

#import <Cocoa/Cocoa.h>
#import <Foundation/Foundation.h>
#import <QuartzCore/QuartzCore.h>
#include <mach/mach.h>
#include <sys/sysctl.h>

// ===== MACOS PLATFORM FACTORY FUNCTIONS =====

namespace UltraCanvas {

// ===== PLATFORM-SPECIFIC APPLICATION FACTORY =====
    std::unique_ptr<UltraCanvasBaseApplication> CreatePlatformApplication() {
        return std::make_unique<UltraCanvasMacOSApplication>();
    }

// ===== PLATFORM-SPECIFIC WINDOW FACTORY =====
    std::unique_ptr<UltraCanvasWindowBase> CreatePlatformWindow(const WindowConfig& config) {
        return std::make_unique<UltraCanvasMacOSWindow>(config);
    }

// ===== PLATFORM IDENTIFICATION =====
    std::string GetPlatformName() {
        return "macOS";
    }

    std::string GetPlatformVersion() {
        NSOperatingSystemVersion version = [[NSProcessInfo processInfo] operatingSystemVersion];
        return std::to_string(version.majorVersion) + "." +
               std::to_string(version.minorVersion) + "." +
               std::to_string(version.patchVersion);
    }

    std::string GetPlatformArchitecture() {
#if defined(__x86_64__)
        return "x86_64";
#elif defined(__aarch64__) || defined(__arm64__)
        return "arm64";
    #else
        return "unknown";
#endif
    }

// ===== SYSTEM INFORMATION =====
    int GetCPUCoreCount() {
        return static_cast<int>([[NSProcessInfo processInfo] processorCount]);
    }

    size_t GetTotalSystemMemory() {
        int mib[2] = {CTL_HW, HW_MEMSIZE};
        int64_t memSize = 0;
        size_t length = sizeof(memSize);

        if (sysctl(mib, 2, &memSize, &length, nullptr, 0) == 0) {
            return static_cast<size_t>(memSize);
        }

        return 0;
    }

    size_t GetAvailableSystemMemory() {
        vm_statistics64_data_t vmStats;
        mach_msg_type_number_t infoCount = HOST_VM_INFO64_COUNT;
        kern_return_t kernReturn = host_statistics64(mach_host_self(),
                                                     HOST_VM_INFO64,
                                                     (host_info64_t)&vmStats,
                                                     &infoCount);

        if (kernReturn == KERN_SUCCESS) {
            return static_cast<size_t>(vmStats.free_count * vm_page_size);
        }

        return 0;
    }

    size_t GetProcessMemoryUsage() {
        struct mach_task_basic_info info;
        mach_msg_type_number_t size = MACH_TASK_BASIC_INFO_COUNT;

        kern_return_t kerr = task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                                       (task_info_t)&info, &size);

        if (kerr == KERN_SUCCESS) {
            return static_cast<size_t>(info.resident_size);
        }

        return 0;
    }

// ===== FILE SYSTEM UTILITIES =====
    std::string GetResourcesPath() {
        NSBundle* mainBundle = [NSBundle mainBundle];
        NSString* resourcePath = [mainBundle resourcePath];

        if (resourcePath) {
            return std::string([resourcePath UTF8String]);
        }

        return "./";
    }

    std::string GetDocumentsPath() {
        NSArray* paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory,
                                                             NSUserDomainMask, YES);
        if ([paths count] > 0) {
            NSString* documentsPath = [paths objectAtIndex:0];
            return std::string([documentsPath UTF8String]);
        }

        return "./";
    }

    std::string GetApplicationSupportPath() {
        NSArray* paths = NSSearchPathForDirectoriesInDomains(NSApplicationSupportDirectory,
                                                             NSUserDomainMask, YES);
        if ([paths count] > 0) {
            NSString* appSupportPath = [paths objectAtIndex:0];
            NSString* bundleID = [[NSBundle mainBundle] bundleIdentifier];
            if (bundleID) {
                appSupportPath = [appSupportPath stringByAppendingPathComponent:bundleID];
            }
            return std::string([appSupportPath UTF8String]);
        }

        return "./";
    }

    std::string GetCachePath() {
        NSArray* paths = NSSearchPathForDirectoriesInDomains(NSCachesDirectory,
                                                             NSUserDomainMask, YES);
        if ([paths count] > 0) {
            NSString* cachePath = [paths objectAtIndex:0];
            return std::string([cachePath UTF8String]);
        }

        return "./";
    }

    std::string GetTempPath() {
        NSString* tempPath = NSTemporaryDirectory();
        return std::string([tempPath UTF8String]);
    }

    std::string GetHomePath() {
        NSString* homePath = NSHomeDirectory();
        return std::string([homePath UTF8String]);
    }

// ===== FONT SYSTEM =====
    std::vector<std::string> GetSystemFonts() {
        std::vector<std::string> fonts;

        NSArray* fontFamilies = [[NSFontManager sharedFontManager] availableFontFamilies];
        for (NSString* fontFamily in fontFamilies) {
            fonts.push_back(std::string([fontFamily UTF8String]));
        }

        return fonts;
    }

    std::string GetDefaultSystemFont() {
        NSFont* systemFont = [NSFont systemFontOfSize:[NSFont systemFontSize]];
        return std::string([[systemFont fontName] UTF8String]);
    }

    float GetDefaultSystemFontSize() {
        return static_cast<float>([NSFont systemFontSize]);
    }

    std::string GetMonospacedSystemFont() {
        if (@available(macOS 10.15, *)) {
            NSFont* monoFont = [NSFont monospacedSystemFontOfSize:[NSFont systemFontSize]
            weight:NSFontWeightRegular];
            return std::string([[monoFont fontName] UTF8String]);
        }

        // Fallback for older macOS versions
        return "Menlo";
    }

// ===== THEME AND APPEARANCE =====
    bool IsDarkModeEnabled() {
        if (@available(macOS 10.14, *)) {
            NSAppearanceName appearanceName = [[NSApp effectiveAppearance] bestMatchFromAppearancesWithNames:@[
                    NSAppearanceNameAqua,
                    NSAppearanceNameDarkAqua
            ]];
            return [appearanceName isEqualToString:NSAppearanceNameDarkAqua];
        }

        return false;
    }

    Color GetSystemAccentColor() {
        if (@available(macOS 10.14, *)) {
            NSColor* accentColor = [NSColor controlAccentColor];

            // Convert to RGB color space
            NSColor* rgbColor = [accentColor colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
            if (rgbColor) {
                CGFloat r, g, b, a;
                [rgbColor getRed:&r green:&g blue:&b alpha:&a];

                return Color(
                        static_cast<uint8_t>(r * 255),
                        static_cast<uint8_t>(g * 255),
                        static_cast<uint8_t>(b * 255),
                        static_cast<uint8_t>(a * 255)
                );
            }
        }

        // Default blue accent color
        return Color(0, 122, 255, 255);
    }

    Color GetSystemBackgroundColor() {
        NSColor* backgroundColor = [NSColor windowBackgroundColor];
        NSColor* rgbColor = [backgroundColor colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];

        if (rgbColor) {
            CGFloat r, g, b, a;
            [rgbColor getRed:&r green:&g blue:&b alpha:&a];

            return Color(
                    static_cast<uint8_t>(r * 255),
                    static_cast<uint8_t>(g * 255),
                    static_cast<uint8_t>(b * 255),
                    static_cast<uint8_t>(a * 255)
            );
        }

        // Default light background
        return Color(255, 255, 255, 255);
    }

    Color GetSystemTextColor() {
        NSColor* textColor = [NSColor textColor];
        NSColor* rgbColor = [textColor colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];

        if (rgbColor) {
            CGFloat r, g, b, a;
            [rgbColor getRed:&r green:&g blue:&b alpha:&a];

            return Color(
                    static_cast<uint8_t>(r * 255),
                    static_cast<uint8_t>(g * 255),
                    static_cast<uint8_t>(b * 255),
                    static_cast<uint8_t>(a * 255)
            );
        }

        // Default black text
        return Color(0, 0, 0, 255);
    }

// ===== DISPLAY INFORMATION =====
    Rect2Di GetPrimaryDisplayBounds() {
        NSScreen* mainScreen = [NSScreen mainScreen];
        if (mainScreen) {
            NSRect frame = [mainScreen frame];
            return Rect2Di(
                    static_cast<int>(frame.origin.x),
                    static_cast<int>(frame.origin.y),
                    static_cast<int>(frame.size.width),
                    static_cast<int>(frame.size.height)
            );
        }

        return Rect2Di(0, 0, 1920, 1080);
    }

    std::vector<Rect2Di> GetAllDisplayBounds() {
        std::vector<Rect2Di> displays;

        NSArray* screens = [NSScreen screens];
        for (NSScreen* screen in screens) {
            NSRect frame = [screen frame];
            displays.push_back(Rect2Di(
                    static_cast<int>(frame.origin.x),
                    static_cast<int>(frame.origin.y),
                    static_cast<int>(frame.size.width),
                    static_cast<int>(frame.size.height)
            ));
        }

        return displays;
    }

    float GetDisplayScaleFactor() {
        NSScreen* mainScreen = [NSScreen mainScreen];
        if (mainScreen) {
            return static_cast<float>([mainScreen backingScaleFactor]);
        }

        return 1.0f;
    }

// ===== CLIPBOARD OPERATIONS =====
    bool SetClipboardText(const std::string& text) {
        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        [pasteboard clearContents];

        NSString* nsText = [NSString stringWithUTF8String:text.c_str()];
        return [pasteboard setString:nsText forType:NSPasteboardTypeString];
    }

    std::string GetClipboardText() {
        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        NSString* text = [pasteboard stringForType:NSPasteboardTypeString];

        if (text) {
            return std::string([text UTF8String]);
        }

        return "";
    }

    bool HasClipboardText() {
        NSPasteboard* pasteboard = [NSPasteboard generalPasteboard];
        NSArray* types = [pasteboard types];
        return [types containsObject:NSPasteboardTypeString];
    }

// ===== UTILITY FUNCTIONS =====
    void ShowMessageBox(const std::string& title, const std::string& message, MessageBoxType type) {
        @autoreleasepool {
                NSAlert* alert = [[NSAlert alloc] init];
                [alert setMessageText:[NSString stringWithUTF8String:title.c_str()]];
                [alert setInformativeText:[NSString stringWithUTF8String:message.c_str()]];

                switch (type) {
                    case MessageBoxType::Info:
                    [alert setAlertStyle:NSAlertStyleInformational];
                    break;
                    case MessageBoxType::Warning:
                    [alert setAlertStyle:NSAlertStyleWarning];
                    break;
                    case MessageBoxType::Error:
                    [alert setAlertStyle:NSAlertStyleCritical];
                    break;
                }

                [alert runModal];
                [alert release];
        }
    }

    void OpenURL(const std::string& url) {
        NSString* urlString = [NSString stringWithUTF8String:url.c_str()];
        NSURL* nsURL = [NSURL URLWithString:urlString];

        if (nsURL) {
            [[NSWorkspace sharedWorkspace] openURL:nsURL];
        }
    }

    void OpenFileInDefaultApp(const std::string& filePath) {
        NSString* path = [NSString stringWithUTF8String:filePath.c_str()];
        NSURL* fileURL = [NSURL fileURLWithPath:path];

        if (fileURL) {
            [[NSWorkspace sharedWorkspace] openURL:fileURL];
        }
    }

    bool FileExists(const std::string& filePath) {
        NSString* path = [NSString stringWithUTF8String:filePath.c_str()];
        return [[NSFileManager defaultManager] fileExistsAtPath:path];
    }

    bool DirectoryExists(const std::string& dirPath) {
        NSString* path = [NSString stringWithUTF8String:dirPath.c_str()];
        BOOL isDirectory = NO;
        BOOL exists = [[NSFileManager defaultManager] fileExistsAtPath:path isDirectory:&isDirectory];
        return exists && isDirectory;
    }

    bool CreateDirectory(const std::string& dirPath) {
        NSString* path = [NSString stringWithUTF8String:dirPath.c_str()];
        NSError* error = nil;
        BOOL success = [[NSFileManager defaultManager] createDirectoryAtPath:path
        withIntermediateDirectories:YES
        attributes:nil
        error:&error];

        if (!success && error) {
            NSLog(@"Failed to create directory: %@", [error localizedDescription]);
        }

        return success;
    }

// ===== PERFORMANCE MONITORING =====
    double GetCurrentTime() {
        return [[NSDate date] timeIntervalSince1970];
    }

    uint64_t GetPerformanceCounter() {
        return mach_absolute_time();
    }

    double GetPerformanceFrequency() {
        static mach_timebase_info_data_t timebase;
        static dispatch_once_t onceToken;

        dispatch_once(&onceToken, ^{
            mach_timebase_info(&timebase);
        });

        return 1e9 * timebase.denom / timebase.numer;
    }

} // namespace UltraCanvas