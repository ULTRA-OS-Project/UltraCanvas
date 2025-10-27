// OS/MacOS/UltraCanvasSupport.cpp
// macOS platform implementation for UltraCanvas Framework
// Version: 1.0.0
// Last Modified: 2024-12-19
// Author: UltraCanvas Framework

#include "UltraCanvasMacOSApplication.h"
#include "UltraCanvasMacOSWindow.h"
#include "../../include/UltraCanvasApplication.h"
#include "../../include/UltraCanvasWindow.h"
#import <Cocoa/Cocoa.h>
#import <CoreGraphics/CoreGraphics.h>

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

// ===== SYSTEM INFORMATION =====
bool IsPlatformSupported() {
    // Check if we're running on macOS 10.12 or later (minimum requirement)
    NSOperatingSystemVersion minVersion = {10, 12, 0};
    return [[NSProcessInfo processInfo] isOperatingSystemAtLeastVersion:minVersion];
}

std::string GetSystemInfo() {
    NSString* osVersion = [[NSProcessInfo processInfo] operatingSystemVersionString];
    NSString* modelName = [NSString stringWithUTF8String:[[NSHost currentHost] name].UTF8String];
    
    return std::string([osVersion UTF8String]) + " on " + std::string([modelName UTF8String]);
}

// ===== GRAPHICS SYSTEM INFORMATION =====
bool IsHardwareAccelerated() {
    // Check if Metal is available (indicates modern graphics hardware)
    return MTLCreateSystemDefaultDevice() != nil;
}

std::vector<std::string> GetAvailableDisplays() {
    std::vector<std::string> displays;
    
    NSArray* screens = [NSScreen screens];
    for (NSUInteger i = 0; i < [screens count]; i++) {
        NSScreen* screen = [screens objectAtIndex:i];
        NSRect frame = [screen frame];
        
        std::string displayInfo = "Display " + std::to_string(i) + 
                                 " (" + std::to_string((int)frame.size.width) + 
                                 "x" + std::to_string((int)frame.size.height) + ")";
        
        if ([screen isEqual:[NSScreen mainScreen]]) {
            displayInfo += " [Main]";
        }
        
        displays.push_back(displayInfo);
    }
    
    return displays;
}

// ===== MEMORY INFORMATION =====
size_t GetAvailableMemory() {
    NSUInteger physicalMemory = [[NSProcessInfo processInfo] physicalMemory];
    return static_cast<size_t>(physicalMemory);
}

size_t GetUsedMemory() {
    // Get current memory usage
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

std::string GetTempPath() {
    NSString* tempPath = NSTemporaryDirectory();
    return std::string([tempPath UTF8String]);
}

// ===== FONT SYSTEM =====
std::vector<std::string> GetSystemFonts() {
    std::vector<std::string> fonts;
    
    NSArray* fontNames = [[NSFontManager sharedFontManager] availableFonts];
    for (NSString* fontName in fontNames) {
        fonts.push_back(std::string([fontName UTF8String]));
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
        CGFloat red, green, blue, alpha;
        
        // Convert to RGB color space
        NSColor* rgbColor = [accentColor colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
        [rgbColor getRed:&red green:&green blue:&blue alpha:&alpha];
        
        return Color(
            static_cast<float>(red),
            static_cast<float>(green),
            static_cast<float>(blue),
            static_cast<float>(alpha)
        );
    }
    
    // Default blue accent color for older macOS versions
    return Color(0.0f, 0.478f, 1.0f, 1.0f);
}

// ===== PERFORMANCE MONITORING =====
double GetCPUUsage() {
    host_cpu_load_info_data_t cpuinfo;
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO, 
                       (host_info_t)&cpuinfo, &count) == KERN_SUCCESS) {
        
        unsigned long totalTicks = 0;
        for (int i = 0; i < CPU_STATE_MAX; i++) {
            totalTicks += cpuinfo.cpu_ticks[i];
        }
        
        unsigned long idleTicks = cpuinfo.cpu_ticks[CPU_STATE_IDLE];
        double usage = 1.0 - (double(idleTicks) / double(totalTicks));
        
        return usage * 100.0;
    }
    
    return 0.0;
}

// ===== DISPLAY PROPERTIES =====
float GetDisplayScaleFactor() {
    NSScreen* mainScreen = [NSScreen mainScreen];
    return static_cast<float>([mainScreen backingScaleFactor]);
}

bool IsRetinaDisplay() {
    return GetDisplayScaleFactor() > 1.0f;
}

// ===== POWER MANAGEMENT =====
bool IsOnBattery() {
    CFTypeRef powerSourceInfo = IOPSCopyPowerSourcesInfo();
    if (powerSourceInfo) {
        CFArrayRef powerSources = IOPSCopyPowerSourcesList(powerSourceInfo);
        if (powerSources) {
            for (CFIndex i = 0; i < CFArrayGetCount(powerSources); i++) {
                CFTypeRef powerSource = CFArrayGetValueAtIndex(powerSources, i);
                CFDictionaryRef description = IOPSGetPowerSourceDescription(powerSourceInfo, powerSource);
                
                if (description) {
                    CFStringRef powerState = (CFStringRef)CFDictionaryGetValue(description, CFSTR(kIOPSPowerSourceStateKey));
                    if (powerState && CFStringCompare(powerState, CFSTR(kIOPSBatteryPowerValue), 0) == kCFCompareEqualTo) {
                        CFRelease(powerSources);
                        CFRelease(powerSourceInfo);
                        return true;
                    }
                }
            }
            CFRelease(powerSources);
        }
        CFRelease(powerSourceInfo);
    }
    return false;
}

// ===== ACCESSIBILITY =====
bool IsAccessibilityEnabled() {
    return AXIsProcessTrustedWithOptions(nil);
}

bool IsVoiceOverEnabled() {
    return [[NSWorkspace sharedWorkspace] isVoiceOverEnabled];
}

// ===== NOTIFICATION SUPPORT =====
bool SupportsNotifications() {
    return NSClassFromString(@"NSUserNotificationCenter") != nil ||
           NSClassFromString(@"UNUserNotificationCenter") != nil;
}

// ===== INPUT DEVICE INFORMATION =====
bool HasTouchBar() {
    if (@available(macOS 10.12.2, *)) {
        return [NSClassFromString(@"NSTouchBar") isAvailable];
    }
    return false;
}

bool IsTrackpadAvailable() {
    // Check if trackpad gestures are available
    return [[NSUserDefaults standardUserDefaults] boolForKey:@"com.apple.trackpad.scaling"];
}

} // namespace UltraCanvas