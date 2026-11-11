// OS/MacOS/UltraCanvasMacOSHostAppearance.mm
// The desktop's look on macOS, for
// UltraCanvasMacOSApplication::DetectHostAppearanceNative
// (UltraCanvasHostAppearance.h, phase T1 of the host theme proposal):
//   dark          NSApp.effectiveAppearance's best match of Aqua / DarkAqua (10.14)
//   high contrast NSWorkspace accessibilityDisplayShouldIncreaseContrast
//   motion        NSWorkspace accessibilityDisplayShouldReduceMotion (10.12)
//   accent        NSColor.controlAccentColor in sRGB (10.14)
//   fonts         the system font and the system monospace (UltraCanvasMacOSApplication.mm)
//   text scale    1.0 - macOS scales the display, not the text
// The change events, installed by InstallHostAppearanceObservers once
// NSApp exists and removed at shutdown: KVO on NSApp.effectiveAppearance
// (dark), NSSystemColorsDidChangeNotification (the accent) and
// NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification (contrast,
// motion); each calls ScheduleHostAppearanceRefresh, which reads all of
// this again once per burst and tells the handlers when something differs.
// Version: 1.0.0
// Last Modified: 2026-11-04
// Author: UltraCanvas Framework
#include "UltraCanvasMacOSApplication.h"
#include "UltraCanvasHostAppearance.h"

#import <Cocoa/Cocoa.h>

// Hears AppKit's appearance change on behalf of the application.
@interface UltraCanvasHostAppearanceObserver : NSObject
@property (nonatomic, assign) UltraCanvas::UltraCanvasMacOSApplication* application;
@end

@implementation UltraCanvasHostAppearanceObserver
- (void)observeValueForKeyPath:(NSString*)keyPath ofObject:(id)object change:(NSDictionary*)change context:(void*)context {
    (void)keyPath; (void)object; (void)change; (void)context;
    if (self.application) self.application->ScheduleHostAppearanceRefresh();
}
- (void)hostLookChanged:(NSNotification*)note {
    (void)note;
    if (self.application) self.application->ScheduleHostAppearanceRefresh();
}
@end

namespace UltraCanvas {

namespace {

UltraCanvasHostAppearanceObserver* g_hostAppearanceObserver = nil;
bool g_observingEffectiveAppearance = false;

}  // namespace

HostAppearance UltraCanvasMacOSApplication::DetectHostAppearanceNative() {
    HostAppearance host = DefaultHostAppearance();
    host.source = "macos-appkit";
    @autoreleasepool {
        NSApplication* app = [NSApplication sharedApplication];
        if (@available(macOS 10.14, *)) {
            if (app) {
                NSAppearanceName match = [app.effectiveAppearance
                    bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]];
                host.dark = match != nil && [match isEqualToString:NSAppearanceNameDarkAqua];
            }
            NSColor* accent = [[NSColor controlAccentColor] colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
            if (accent) {
                host.accent = Color(static_cast<uint8_t>(accent.redComponent * 255.0 + 0.5),
                                    static_cast<uint8_t>(accent.greenComponent * 255.0 + 0.5),
                                    static_cast<uint8_t>(accent.blueComponent * 255.0 + 0.5), 255);
            }
        }
        NSWorkspace* workspace = [NSWorkspace sharedWorkspace];
        host.highContrast = workspace.accessibilityDisplayShouldIncreaseContrast;
        if (@available(macOS 10.12, *)) {
            host.reduceMotion = workspace.accessibilityDisplayShouldReduceMotion;
        }
    }
    const FontStyle ui = DetectSystemFontStyleNative();
    const FontStyle mono = DetectMonospacedFontStyleNative();
    if (!ui.fontFamily.empty() && ui.fontSize > 0) host.uiFont = ui;
    if (!mono.fontFamily.empty() && mono.fontSize > 0) host.monoFont = mono;
    return host;
}

void UltraCanvasMacOSApplication::InstallHostAppearanceObservers() {
    if (g_hostAppearanceObserver) return;
    @autoreleasepool {
        g_hostAppearanceObserver = [[UltraCanvasHostAppearanceObserver alloc] init];
        g_hostAppearanceObserver.application = this;
        NSApplication* app = [NSApplication sharedApplication];
        if (@available(macOS 10.14, *)) {
            if (app) {
                [app addObserver:g_hostAppearanceObserver forKeyPath:@"effectiveAppearance"
                         options:NSKeyValueObservingOptionNew context:nullptr];
                g_observingEffectiveAppearance = true;
            }
        }
        [[NSNotificationCenter defaultCenter] addObserver:g_hostAppearanceObserver
                                                 selector:@selector(hostLookChanged:)
                                                     name:NSSystemColorsDidChangeNotification
                                                   object:nil];
        [[[NSWorkspace sharedWorkspace] notificationCenter] addObserver:g_hostAppearanceObserver
                                                               selector:@selector(hostLookChanged:)
                                                                   name:NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification
                                                                 object:nil];
    }
}

void UltraCanvasMacOSApplication::RemoveHostAppearanceObservers() {
    if (!g_hostAppearanceObserver) return;
    @autoreleasepool {
        if (g_observingEffectiveAppearance) {
            [[NSApplication sharedApplication] removeObserver:g_hostAppearanceObserver forKeyPath:@"effectiveAppearance"];
            g_observingEffectiveAppearance = false;
        }
        [[NSNotificationCenter defaultCenter] removeObserver:g_hostAppearanceObserver];
        [[[NSWorkspace sharedWorkspace] notificationCenter] removeObserver:g_hostAppearanceObserver];
        g_hostAppearanceObserver.application = nullptr;
        g_hostAppearanceObserver = nil;
    }
}

}  // namespace UltraCanvas
