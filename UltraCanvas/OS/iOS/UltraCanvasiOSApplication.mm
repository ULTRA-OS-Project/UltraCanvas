// OS/iOS/UltraCanvasiOSApplication.mm
// iOS platform implementation for UltraCanvas Framework.
// Version: 1.1.0 - the software and hardware keyboards, host appearance
// Version: 1.0.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasEmojiSettings.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasiOSApplication.h"
#include "UltraCanvasiOSWindow.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasCaret.h"
#include "UltraCanvasHostAppearance.h"
#include "UltraCanvasUIElement.h"

#import <UIKit/UIKit.h>
#import <QuartzCore/QuartzCore.h>

#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>

namespace UltraCanvas {

    UltraCanvasiOSApplication* UltraCanvasiOSApplication::instance = nullptr;

    UltraCanvasiOSApplication::UltraCanvasiOSApplication() {
        instance = this;
    }

    UltraCanvasiOSApplication::~UltraCanvasiOSApplication() {
        if (instance == this) instance = nullptr;
    }

    UltraCanvasiOSWindow* UltraCanvasiOSApplication::GetPrimaryWindow() {
        if (windows.empty()) return nullptr;
        return static_cast<UltraCanvasiOSWindow*>(windows.front().get());
    }

    // ===== INITIALIZATION =====

    bool UltraCanvasiOSApplication::InitializeNative() {
        if (!GetiOSRootView()) {
            debugOutput << "UltraCanvas iOS: no root view - the process must start "
                           "through the backend's main (OS/iOS/UltraCanvasiOSMain.mm), "
                           "which calls the application's ultracanvas_app_main after "
                           "launch" << std::endl;
            return false;
        }
        InitializeWakeUp();
        // A widget claiming or releasing the caret is the framework's signal
        // that text editing started or stopped: the software keyboard
        // follows it.
        UltraCanvasCaret::GetInstance().onTextEditingChanged =
                [this](bool active) { OnTextEditingChanged(active); };
        // What Run() checks, as every other backend sets it here.
        initialized = true;
        running = false;
        return true;
    }

    void UltraCanvasiOSApplication::ShutdownNative() {
        UltraCanvasCaret::GetInstance().onTextEditingChanged = nullptr;
        SetiOSKeyboardWanted(false);
        ShutdownWakeUp();
        running = false;
    }

    // ===== MAIN LOOP =====

    void UltraCanvasiOSApplication::CollectAndProcessNativeEvents() {
        // Text editing stopped last turn and nothing claimed the caret since:
        // lower the keyboard now.
        if (pendingKeyboardHide) {
            pendingKeyboardHide = false;
            SetiOSKeyboardWanted(false);
        }
        @autoreleasepool {
            // One turn of the main run loop: UIKit's touch, lifecycle and
            // layout sources fire inside it and queue UCEvents through the
            // view (OS/iOS/UltraCanvasiOSMain.mm). It returns after the first
            // source handled, at the next timer's time, or when
            // WakeUpEventLoop signals from another thread.
            auto timeout = GetTimeUntilNextTimer();
            CFTimeInterval waitSeconds;
            if (timeout == std::chrono::milliseconds::max()) {
                waitSeconds = 1e10;   // CFRunLoop treats this as indefinite
            } else {
                waitSeconds = timeout.count() / 1000.0;
                if (waitSeconds < 0) waitSeconds = 0;
            }
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, waitSeconds, true);
            // And whatever else is already waiting, without blocking again -
            // a bounded number of sources, so one that keeps firing cannot
            // hold the loop away from rendering.
            for (int i = 0; i < 64; ++i) {
                if (CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0, true) !=
                    kCFRunLoopRunHandledSource) break;
            }
        }
    }

    // The tail of every main-loop iteration, after the windows rendered: the
    // layer contents they set land on screen only when the CoreAnimation
    // transaction commits, and the run-loop observer that would commit it
    // need not fire in a turn that returned after one source (the macOS
    // backend's RunInEventLoop, for the same reason).
    void UltraCanvasiOSApplication::RunInEventLoop() {
        @autoreleasepool {
            [CATransaction flush];
        }
    }

    // ===== WAKEUP =====

    static void WakeUpSourceCallback(void* /*info*/) {
        // Nothing to do: the source exists to end CFRunLoopRunInMode.
    }

    void UltraCanvasiOSApplication::InitializeWakeUp() {
        if (wakeupSource) return;
        CFRunLoopSourceContext ctx = {};
        ctx.perform = WakeUpSourceCallback;
        CFRunLoopSourceRef source = CFRunLoopSourceCreate(kCFAllocatorDefault, 0, &ctx);
        if (source) {
            CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopDefaultMode);
            wakeupSource = source;
        } else {
            debugOutput << "UltraCanvas iOS: failed to create the wakeup run-loop source"
                        << std::endl;
        }
    }

    void UltraCanvasiOSApplication::ShutdownWakeUp() {
        if (wakeupSource) {
            auto source = static_cast<CFRunLoopSourceRef>(wakeupSource);
            CFRunLoopRemoveSource(CFRunLoopGetMain(), source, kCFRunLoopDefaultMode);
            CFRelease(source);
            wakeupSource = nullptr;
        }
    }

    void UltraCanvasiOSApplication::WakeUpEventLoop() {
        if (wakeupSource) {
            CFRunLoopSourceSignal(static_cast<CFRunLoopSourceRef>(wakeupSource));
            CFRunLoopWakeUp(CFRunLoopGetMain());
        }
    }

    // ===== MOUSE CAPTURE / CURSORS =====
    // A touch is implicitly grabbed for its whole gesture, and there is no
    // pointer cursor: all four are accepted no-ops.

    void UltraCanvasiOSApplication::CaptureMouseNative() {}
    void UltraCanvasiOSApplication::ReleaseMouseNative() {}

    bool UltraCanvasiOSApplication::SelectMouseCursorNative(UltraCanvasWindowBase*, UCMouseCursor) {
        return true;
    }

    bool UltraCanvasiOSApplication::SelectMouseCursorNative(UltraCanvasWindowBase*, UCMouseCursor,
                                                            const char*, int, int) {
        return true;
    }

    // ===== LIFECYCLE =====

    void UltraCanvasiOSApplication::PushWindowEvent(UCEventType type) {
        auto* win = GetPrimaryWindow();
        if (!win) return;
        UCEvent event;
        event.type = type;
        event.targetWindow = win->GetWindowWeakPtr();
        event.nativeWindowHandle = win->GetNativeHandle();
        PushEvent(event);
    }

    void UltraCanvasiOSApplication::HandleDidEnterBackground() {
        if (auto* win = GetPrimaryWindow()) win->HandleEnteredBackground();
        // A gesture the system took over can end without its last touch.
        multiTouchGesture = false;
        touchesDown = 0;
    }

    void UltraCanvasiOSApplication::HandleWillEnterForeground() {
        if (auto* win = GetPrimaryWindow()) win->HandleEnteredForeground();
    }

    void UltraCanvasiOSApplication::HandleDidBecomeActive() {
        PushWindowEvent(UCEventType::WindowFocus);
    }

    void UltraCanvasiOSApplication::HandleWillResignActive() {
        PushWindowEvent(UCEventType::WindowBlur);
    }

    // ===== TOUCH =====

    void UltraCanvasiOSApplication::PushPointerEvent(UltraCanvasiOSWindow* win, UCEventType type,
                                                     int x, int y, float pressure,
                                                     int fingerId, int fingersDown) {
        UCEvent event;
        event.type = type;
        event.targetWindow = win->GetWindowWeakPtr();
        event.nativeWindowHandle = win->GetNativeHandle();
        // Points are the framework's logical units: no conversion.
        event.pointerWindow = Point2Di{x, y};
        event.pointer = event.pointerWindow;
        event.pointerGlobal = event.pointerWindow;   // one full-screen surface
        event.pressure = pressure;
        event.pointerId = fingerId;
        event.touchPointCount = fingersDown;
        if (type == UCEventType::MouseDown || type == UCEventType::MouseUp ||
            type == UCEventType::MouseMove || type == UCEventType::MouseDoubleClick) {
            event.button = UCMouseButton::Left;
        }
        PushEvent(event);
    }

    void UltraCanvasiOSApplication::HandleTouch(int phase, int fingerId, float x, float y,
                                                float pressure, int fingersDown, double timestamp) {
        auto* win = GetPrimaryWindow();
        if (!win) return;
        const int px = static_cast<int>(std::lround(x));
        const int py = static_cast<int>(std::lround(y));
        touchesDown = fingersDown;

        // ---- Every finger, as touch events ----
        const UCEventType touchType = phase == 0 ? UCEventType::TouchStart
                                    : phase == 1 ? UCEventType::TouchMove
                                                 : UCEventType::TouchEnd;
        PushPointerEvent(win, touchType, px, py, pressure, fingerId, fingersDown);

        // ---- The primary finger, as mouse events ----
        // Every existing widget is written against the mouse, so a one-finger
        // gesture drives the mouse path unchanged. When a second finger
        // joins, the gesture belongs to whoever handles touch: one MouseUp
        // ends the mouse interaction and synthesis stays off until every
        // finger has lifted (the Android backend's rule).
        if (phase == 0 && fingersDown >= 2) {
            if (!multiTouchGesture) {
                multiTouchGesture = true;
                PushPointerEvent(win, UCEventType::MouseUp, px, py, pressure, fingerId, fingersDown);
            }
            return;
        }
        if (multiTouchGesture) {
            if ((phase == 2 || phase == 3) && fingersDown == 0) multiTouchGesture = false;
            return;
        }

        switch (phase) {
            case 0: {
                // Double-tap synthesis, the Linux and Android rules.
                const bool doubleTap = lastTapTime > 0 &&
                        timestamp - lastTapTime <= doubleClickTime &&
                        std::abs(px - lastTapX) <= doubleClickDistance &&
                        std::abs(py - lastTapY) <= doubleClickDistance;
                if (doubleTap) {
                    lastTapTime = 0;   // no triple-click chaining
                    PushPointerEvent(win, UCEventType::MouseDoubleClick, px, py, pressure,
                                     fingerId, fingersDown);
                } else {
                    lastTapTime = timestamp;
                    lastTapX = px;
                    lastTapY = py;
                    PushPointerEvent(win, UCEventType::MouseDown, px, py, pressure,
                                     fingerId, fingersDown);
                }
                break;
            }
            case 1:
                PushPointerEvent(win, UCEventType::MouseMove, px, py, pressure, fingerId, fingersDown);
                break;
            default:
                PushPointerEvent(win, UCEventType::MouseUp, px, py, pressure, fingerId, fingersDown);
                break;
        }
    }

    // ===== KEYBOARD =====

    void UltraCanvasiOSApplication::OnTextEditingChanged(bool active) {
        if (active) {
            pendingKeyboardHide = false;
            SetiOSKeyboardWanted(true);
        } else {
            pendingKeyboardHide = true;
        }
    }

    void UltraCanvasiOSApplication::HandleKey(bool down, UCKeys key, int nativeKeyCode, bool shift,
                                              bool ctrl, bool alt, bool meta,
                                              const std::string& text) {
        auto* win = GetPrimaryWindow();
        if (!win) return;
        UCEvent event;
        event.type = down ? UCEventType::KeyDown : UCEventType::KeyUp;
        event.targetWindow = win->GetWindowWeakPtr();
        event.nativeWindowHandle = win->GetNativeHandle();
        event.nativeKeyCode = nativeKeyCode;
        event.virtualKey = key;
        event.shift = shift;
        event.ctrl = ctrl;
        event.alt = alt;
        event.meta = meta;
        // Text only on the press, and not for a shortcut (Ctrl or Command
        // held): as the desktop backends deliver it.
        if (down && !ctrl && !meta && !text.empty()) {
            event.text = text;
            if (text.size() == 1 && static_cast<unsigned char>(text[0]) < 0x80) {
                event.character = text[0];
            }
        }
        PushEvent(event);
    }

    void UltraCanvasiOSApplication::HandleInsertText(const std::string& utf8) {
        if (utf8.empty()) return;
        if (utf8 == "\n" || utf8 == "\r") {
            HandleKey(true, UCKeys::Return, 0, false, false, false, false, "");
            HandleKey(false, UCKeys::Return, 0, false, false, false, false, "");
            return;
        }
        auto* win = GetPrimaryWindow();
        if (!win) return;
        // What the keyboard settled on belongs to no single key, so
        // virtualKey stays Unknown and only `text` means anything - the shape
        // the Linux backend delivers for its input method, which every text
        // widget already handles.
        UCEvent event;
        event.type = UCEventType::KeyDown;
        event.targetWindow = win->GetWindowWeakPtr();
        event.nativeWindowHandle = win->GetNativeHandle();
        event.text = utf8;
        if (utf8.size() == 1 && static_cast<unsigned char>(utf8[0]) < 0x80) {
            event.character = utf8[0];
        }
        PushEvent(event);
    }

    void UltraCanvasiOSApplication::HandleDeleteBackward() {
        // The framework has no "delete one character" event; text widgets
        // implement Backspace, so the deletion is a press of it.
        HandleKey(true, UCKeys::Backspace, 0, false, false, false, false, "");
        HandleKey(false, UCKeys::Backspace, 0, false, false, false, false, "");
    }

    // ===== HOST APPEARANCE =====
    //   dark          UITraitCollection userInterfaceStyle
    //   high contrast accessibilityContrast, or Darken Colors
    //   motion        Reduce Motion
    //   accent        the app's tint colour (iOS has no system-wide accent)
    //   text scale    Dynamic Type: the body font's size over its 17 pt default
    //   fonts         the bundled families (DetectSystemFontStyleNative)

    HostAppearance UltraCanvasiOSApplication::DetectHostAppearanceNative() {
        HostAppearance host = DefaultHostAppearance();
        host.source = "ios-uikit";
        @autoreleasepool {
            UIView* view = (__bridge UIView*)GetiOSRootView();
            UITraitCollection* traits = view ? view.traitCollection
                                             : [UITraitCollection currentTraitCollection];
            host.dark = traits.userInterfaceStyle == UIUserInterfaceStyleDark;
            host.highContrast = traits.accessibilityContrast == UIAccessibilityContrastHigh ||
                                UIAccessibilityDarkerSystemColorsEnabled();
            host.reduceMotion = UIAccessibilityIsReduceMotionEnabled();
            UIColor* tint = view.tintColor;
            CGFloat r = 0, g = 0, b = 0, a = 0;
            if (tint && [tint getRed:&r green:&g blue:&b alpha:&a]) {
                host.accent = Color(static_cast<uint8_t>(std::clamp(r, 0.0, 1.0) * 255.0 + 0.5),
                                    static_cast<uint8_t>(std::clamp(g, 0.0, 1.0) * 255.0 + 0.5),
                                    static_cast<uint8_t>(std::clamp(b, 0.0, 1.0) * 255.0 + 0.5), 255);
            }
            UIFont* body = [UIFont preferredFontForTextStyle:UIFontTextStyleBody
                                 compatibleWithTraitCollection:traits];
            if (body && body.pointSize > 0) host.textScale = body.pointSize / 17.0;
        }
        const FontStyle ui = DetectSystemFontStyleNative();
        const FontStyle mono = DetectMonospacedFontStyleNative();
        if (!ui.fontFamily.empty() && ui.fontSize > 0) host.uiFont = ui;
        if (!mono.fontFamily.empty() && mono.fontSize > 0) host.monoFont = mono;
        return host;
    }

    void UltraCanvasiOSApplication::HandleTraitsChanged() {
        ScheduleHostAppearanceRefresh();
    }

    // ===== FONTS =====
    // Pango is fontconfig and FreeType on iOS, as on Android: the bundled
    // fonts (the app bundle's share/media/fonts) are registered as
    // application fonts. iOS's own system fonts are reached through CoreText
    // only, which is a later decision (iOSPortInvestigation.md 3.6).

    FontStyle UltraCanvasiOSApplication::DetectSystemFontStyleNative() {
        FontStyle result;
        result.fontFamily = "Ubuntu";
        result.fontSize = 12.0;
        return result;
    }

    FontStyle UltraCanvasiOSApplication::DetectMonospacedFontStyleNative() {
        FontStyle result;
        result.fontFamily = "Ubuntu Mono";
        result.fontSize = 12.0;
        return result;
    }

    // The bundled fonts for Pango - the one registration an application's
    // start (LoadBundledFontsNative) and a tool without one
    // (RegisterBundledFontsWithoutApplication) both make.
    int RegisterPlatformBundledFonts(int systemFontsWaitMs) {
        (void)systemFontsWaitMs;   // no system font scan on iOS
        FcConfig* cfg = FcConfigGetCurrent();
        if (!cfg) {
            debugOutput << "UltraCanvas iOS: FcConfigGetCurrent() returned null; "
                           "bundled fonts not registered" << std::endl;
            return 0;
        }
        int registered = 0;
        const std::string dir = GetBundledFontsDir();
        for (size_t i = 0; i < kEmbeddedAllFontsCount; ++i) {
            const std::string path = dir + kEmbeddedAllFonts[i];
            std::error_code ec;
            if (!std::filesystem::exists(PathFromUtf8(path), ec)) {
                debugOutput << "UltraCanvas: bundled font missing: " << path << std::endl;
                continue;
            }
            if (!FcConfigAppFontAddFile(cfg, reinterpret_cast<const FcChar8*>(path.c_str()))) {
                debugOutput << "UltraCanvas: FcConfigAppFontAddFile failed for " << path << std::endl;
            } else {
                ++registered;
            }
        }
        ApplyEmojiSettings();
        FcConfigBuildFonts(cfg);

        PangoFontMap* fcFm = pango_cairo_font_map_new_for_font_type(CAIRO_FONT_TYPE_FT);
        if (fcFm) {
            pango_cairo_font_map_set_default(PANGO_CAIRO_FONT_MAP(fcFm));
            g_object_unref(fcFm);
        } else {
            debugOutput << "UltraCanvas: pango_cairo_font_map_new_for_font_type(FT) "
                           "returned null" << std::endl;
            pango_cairo_font_map_set_default(nullptr);
        }
        return registered;
    }

    void UltraCanvasiOSApplication::LoadBundledFontsNative() {
        RegisterPlatformBundledFonts(0);
    }

    bool UltraCanvasiOSApplication::RegisterFontFileNative(const std::string& fontFilePath) {
        // An application font: this process's FcConfig only.
        FcConfig* cfg = FcConfigGetCurrent();
        if (!cfg) return false;
        return FcConfigAppFontAddFile(cfg, reinterpret_cast<const FcChar8*>(fontFilePath.c_str())) != 0;
    }

    // ===== OpenURL's iOS half (core/UltraCanvasUtils.cpp) =====

    void OpenURLiOS(const std::string& url) {
        @autoreleasepool {
            NSString* text = [NSString stringWithUTF8String:url.c_str()];
            NSURL* target = text ? [NSURL URLWithString:text] : nil;
            if (!target) {
                debugOutput << "UltraCanvas iOS: OpenURL: not a URL: " << url << std::endl;
                return;
            }
            [[UIApplication sharedApplication] openURL:target options:@{} completionHandler:nil];
        }
    }

} // namespace UltraCanvas
