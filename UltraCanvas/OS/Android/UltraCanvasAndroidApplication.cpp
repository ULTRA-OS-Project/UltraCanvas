// OS/Android/UltraCanvasAndroidApplication.cpp
// Android platform implementation for UltraCanvas Framework.
// Version: 1.0.0
// Last Modified: 2026-08-10
// Author: UltraCanvas Framework

// The public headers first: they define the base classes and then pull in the
// OS headers through their platform alias chain (same pattern as Linux).
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasAndroidApplication.h"
#include "UltraCanvasAndroidWindow.h"
#include "UltraCanvasDebug.h"

#include <android/configuration.h>
#include <android/input.h>
#include <android/keycodes.h>
#include <android/looper.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

#include <fontconfig/fontconfig.h>
#include <pango/pangocairo.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <limits>

namespace UltraCanvas {

    UltraCanvasAndroidApplication* UltraCanvasAndroidApplication::instance = nullptr;
    android_app* UltraCanvasAndroidApplication::androidApp = nullptr;

    UltraCanvasAndroidApplication::UltraCanvasAndroidApplication() {
        instance = this;
    }

    ANativeWindow* UltraCanvasAndroidApplication::GetNativeWindow() const {
        return androidApp ? androidApp->window : nullptr;
    }

    UltraCanvasAndroidWindow* UltraCanvasAndroidApplication::GetPrimaryWindow() {
        if (windows.empty()) return nullptr;
        return static_cast<UltraCanvasAndroidWindow*>(windows.front().get());
    }

    // ===== INITIALIZATION =====

    bool UltraCanvasAndroidApplication::InitializeNative() {
        if (!androidApp) {
            debugOutput << "UltraCanvas Android: no android_app - the process must "
                           "be started through the android_main glue "
                           "(UltraCanvasAndroidMain.cpp), which calls SetAndroidApp()"
                        << std::endl;
            return false;
        }
        androidApp->userData = this;
        androidApp->onAppCmd = HandleAppCmdThunk;
        androidApp->onInputEvent = HandleInputEventThunk;
        return true;
    }

    void UltraCanvasAndroidApplication::ShutdownNative() {
        if (androidApp && androidApp->userData == this) {
            androidApp->onAppCmd = nullptr;
            androidApp->onInputEvent = nullptr;
            androidApp->userData = nullptr;
        }
        if (instance == this) instance = nullptr;
    }

    // ===== EVENT PUMP =====

    void UltraCanvasAndroidApplication::CollectAndProcessNativeEvents() {
        if (!androidApp) return;

        // Block until an activity command, input event, or the next timer.
        auto timeout = GetTimeUntilNextTimer();
        int timeoutMs = -1;
        if (timeout != std::chrono::milliseconds::max()) {
            auto count = timeout.count();
            if (count < 0) count = 0;
            if (count > std::numeric_limits<int>::max()) count = std::numeric_limits<int>::max();
            timeoutMs = static_cast<int>(count);
        }

        int events = 0;
        android_poll_source* source = nullptr;
        int ident = ALooper_pollOnce(timeoutMs, nullptr, &events,
                                     reinterpret_cast<void**>(&source));
        if (ident >= 0 && source) {
            source->process(androidApp, source);
        }

        // Drain whatever else is already queued without blocking again.
        while (!androidApp->destroyRequested &&
               (ident = ALooper_pollOnce(0, nullptr, &events,
                                         reinterpret_cast<void**>(&source))) >= 0) {
            if (source) source->process(androidApp, source);
        }

        if (androidApp->destroyRequested) {
            // The activity is being destroyed underneath us; leave Run().
            Exit();
        }
    }

    // ===== WAKEUP MECHANISM =====
    // ALooper_wake() is thread-safe and targets the glue thread's looper
    // directly, so no eventfd bookkeeping is needed.

    void UltraCanvasAndroidApplication::InitializeWakeUp() {}
    void UltraCanvasAndroidApplication::ShutdownWakeUp() {}

    void UltraCanvasAndroidApplication::WakeUpEventLoop() {
        if (androidApp && androidApp->looper) {
            ALooper_wake(androidApp->looper);
        }
    }

    // ===== MOUSE CAPTURE / CURSORS =====
    // Touch input is implicitly grabbed for the whole gesture and there is no
    // persistent pointer cursor, so all four are accepted no-ops.

    void UltraCanvasAndroidApplication::CaptureMouseNative() {}
    void UltraCanvasAndroidApplication::ReleaseMouseNative() {}

    bool UltraCanvasAndroidApplication::SelectMouseCursorNative(
            UltraCanvasWindowBase*, UCMouseCursor) {
        return true;
    }

    bool UltraCanvasAndroidApplication::SelectMouseCursorNative(
            UltraCanvasWindowBase*, UCMouseCursor, const char*, int, int) {
        return true;
    }

    // ===== ACTIVITY COMMANDS =====

    void UltraCanvasAndroidApplication::HandleAppCmdThunk(android_app* app, int32_t cmd) {
        auto* self = static_cast<UltraCanvasAndroidApplication*>(app->userData);
        if (self) self->HandleAppCmd(cmd);
    }

    void UltraCanvasAndroidApplication::PushWindowEvent(UCEventType type) {
        auto* win = GetPrimaryWindow();
        if (!win) return;
        UCEvent event;
        event.type = type;
        event.targetWindow = win->GetWindowWeakPtr();
        event.nativeWindowHandle = win->GetNativeHandle();
        PushEvent(event);
    }

    void UltraCanvasAndroidApplication::HandleAppCmd(int32_t cmd) {
        switch (cmd) {
            case APP_CMD_INIT_WINDOW: {
                auto* win = GetPrimaryWindow();
                if (win && androidApp->window) {
                    win->HandleNativeSurfaceCreated(androidApp->window);
                }
                break;
            }
            case APP_CMD_TERM_WINDOW: {
                auto* win = GetPrimaryWindow();
                if (win) win->HandleNativeSurfaceDestroyed();
                break;
            }
            case APP_CMD_WINDOW_RESIZED:
            case APP_CMD_CONFIG_CHANGED: {
                auto* win = GetPrimaryWindow();
                if (win) win->HandleNativeSurfaceResized();
                break;
            }
            case APP_CMD_GAINED_FOCUS:
                PushWindowEvent(UCEventType::WindowFocus);
                break;
            case APP_CMD_LOST_FOCUS:
                PushWindowEvent(UCEventType::WindowBlur);
                break;
            // PAUSE/STOP/START/RESUME need no extra work: TERM_WINDOW /
            // INIT_WINDOW carry the surface lifecycle (the window auto-hides
            // while the surface is gone, so no rendering happens in the
            // background), and GAINED/LOST_FOCUS carry activation. DESTROY is
            // observed as destroyRequested in CollectAndProcessNativeEvents,
            // which exits Run().
            case APP_CMD_PAUSE:
            case APP_CMD_STOP:
            case APP_CMD_START:
            case APP_CMD_RESUME:
            default:
                break;
        }
    }

    // ===== INPUT =====

    int32_t UltraCanvasAndroidApplication::HandleInputEventThunk(android_app* app,
                                                                 AInputEvent* event) {
        auto* self = static_cast<UltraCanvasAndroidApplication*>(app->userData);
        return self ? self->HandleInputEvent(event) : 0;
    }

    int32_t UltraCanvasAndroidApplication::HandleInputEvent(AInputEvent* event) {
        switch (AInputEvent_getType(event)) {
            case AINPUT_EVENT_TYPE_MOTION: return HandleMotionEvent(event);
            case AINPUT_EVENT_TYPE_KEY:    return HandleKeyEvent(event);
            default:                       return 0;
        }
    }

    int32_t UltraCanvasAndroidApplication::HandleMotionEvent(AInputEvent* motionEvent) {
        auto* win = GetPrimaryWindow();
        if (!win) return 0;

        const int32_t action = AMotionEvent_getAction(motionEvent);
        const int32_t maskedAction = action & AMOTION_EVENT_ACTION_MASK;

        // Pointer 0 only: the primary finger (or the mouse) is translated to
        // mouse events so every existing widget works. Real multi-touch is a
        // deliberate core extension for a later phase (UCEvent has no
        // pointer-ID slot yet).
        const int physX = static_cast<int>(std::lround(AMotionEvent_getX(motionEvent, 0)));
        const int physY = static_cast<int>(std::lround(AMotionEvent_getY(motionEvent, 0)));

        UCEvent event;
        event.targetWindow = win->GetWindowWeakPtr();
        event.nativeWindowHandle = win->GetNativeHandle();
        event.pointerWindow = win->PhysicalToLogical(Point2Di{physX, physY});
        event.pointer = event.pointerWindow;
        event.pointerGlobal = event.pointerWindow;  // single fullscreen surface
        event.pressure = AMotionEvent_getPressure(motionEvent, 0);
        event.button = UCMouseButton::Left;

        switch (maskedAction) {
            case AMOTION_EVENT_ACTION_DOWN: {
                // Double-tap synthesis, same rules as the Linux backend.
                const int64_t timeNs = AMotionEvent_getEventTime(motionEvent);
                const int64_t deltaMs = (timeNs - lastTapTimeNs) / 1'000'000;
                if (lastTapTimeNs != 0 &&
                    deltaMs <= doubleClickTimeMs &&
                    std::abs(physX - lastTapX) <= doubleClickDistance &&
                    std::abs(physY - lastTapY) <= doubleClickDistance) {
                    event.type = UCEventType::MouseDoubleClick;
                    lastTapTimeNs = 0;   // no triple-click chaining
                } else {
                    event.type = UCEventType::MouseDown;
                    lastTapTimeNs = timeNs;
                    lastTapX = physX;
                    lastTapY = physY;
                }
                break;
            }
            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_CANCEL:
                event.type = UCEventType::MouseUp;
                break;
            case AMOTION_EVENT_ACTION_MOVE:
            case AMOTION_EVENT_ACTION_HOVER_MOVE:
                event.type = UCEventType::MouseMove;
                break;
            case AMOTION_EVENT_ACTION_SCROLL: {
                // A real wheel (bluetooth mouse, rotary input).
                const float v = AMotionEvent_getAxisValue(motionEvent,
                                    AMOTION_EVENT_AXIS_VSCROLL, 0);
                const float h = AMotionEvent_getAxisValue(motionEvent,
                                    AMOTION_EVENT_AXIS_HSCROLL, 0);
                if (v != 0.0f) {
                    event.type = UCEventType::MouseWheel;
                    event.wheelDelta = (v > 0.0f) ? 1 : -1;
                } else if (h != 0.0f) {
                    event.type = UCEventType::MouseWheelHorizontal;
                    event.wheelDelta = (h > 0.0f) ? 1 : -1;
                } else {
                    return 0;
                }
                event.button = UCMouseButton::NoneButton;
                break;
            }
            default:
                // Secondary-finger transitions (POINTER_DOWN/UP) and other
                // actions are ignored in the touch→mouse translation.
                return 0;
        }

        PushEvent(event);
        return 1;
    }

    int32_t UltraCanvasAndroidApplication::HandleKeyEvent(AInputEvent* keyEvent) {
        auto* win = GetPrimaryWindow();
        if (!win) return 0;

        const int32_t action = AKeyEvent_getAction(keyEvent);
        const int32_t keyCode = AKeyEvent_getKeyCode(keyEvent);
        const int32_t metaState = AKeyEvent_getMetaState(keyEvent);

        // The system back button maps to a window close request (the
        // framework's onWindowClosing hook can veto it as usual).
        if (keyCode == AKEYCODE_BACK) {
            if (action == AKEY_EVENT_ACTION_UP) {
                PushWindowEvent(UCEventType::WindowCloseRequest);
            }
            return 1;
        }

        if (action != AKEY_EVENT_ACTION_DOWN && action != AKEY_EVENT_ACTION_UP) {
            return 0;
        }

        UCKeys ucKey = ConvertAndroidKeyToUCKey(keyCode);
        if (ucKey == UCKeys::Unknown) {
            return 0;   // let the system handle volume, camera, ... keys
        }

        UCEvent event;
        event.type = (action == AKEY_EVENT_ACTION_DOWN) ? UCEventType::KeyDown
                                                        : UCEventType::KeyUp;
        event.targetWindow = win->GetWindowWeakPtr();
        event.nativeWindowHandle = win->GetNativeHandle();
        event.nativeKeyCode = keyCode;
        event.virtualKey = ucKey;
        event.shift = (metaState & AMETA_SHIFT_ON) != 0;
        event.ctrl = (metaState & AMETA_CTRL_ON) != 0;
        event.alt = (metaState & AMETA_ALT_ON) != 0;
        event.meta = (metaState & AMETA_META_ON) != 0;

        if (event.type == UCEventType::KeyDown && !event.ctrl && !event.alt) {
            char c = DeriveAsciiCharacter(ucKey, event.shift);
            if (c) {
                event.character = c;
                event.text = std::string(1, c);
            }
        }

        PushEvent(event);
        return 1;
    }

    UCKeys UltraCanvasAndroidApplication::ConvertAndroidKeyToUCKey(int32_t keyCode) {
        // Letters / digits are contiguous ranges in both encodings.
        if (keyCode >= AKEYCODE_A && keyCode <= AKEYCODE_Z) {
            return static_cast<UCKeys>(UCKeys::A + (keyCode - AKEYCODE_A));
        }
        if (keyCode >= AKEYCODE_0 && keyCode <= AKEYCODE_9) {
            return static_cast<UCKeys>(UCKeys::Key0 + (keyCode - AKEYCODE_0));
        }
        if (keyCode >= AKEYCODE_F1 && keyCode <= AKEYCODE_F12) {
            return static_cast<UCKeys>(UCKeys::F1 + (keyCode - AKEYCODE_F1));
        }
        if (keyCode >= AKEYCODE_NUMPAD_0 && keyCode <= AKEYCODE_NUMPAD_9) {
            return static_cast<UCKeys>(UCKeys::NumPad0 + (keyCode - AKEYCODE_NUMPAD_0));
        }

        switch (keyCode) {
            case AKEYCODE_SPACE:          return UCKeys::Space;
            case AKEYCODE_ENTER:          return UCKeys::Return;
            case AKEYCODE_TAB:            return UCKeys::Tab;
            case AKEYCODE_DEL:            return UCKeys::Backspace;
            case AKEYCODE_FORWARD_DEL:    return UCKeys::Delete;
            case AKEYCODE_ESCAPE:         return UCKeys::Escape;

            case AKEYCODE_DPAD_UP:        return UCKeys::Up;
            case AKEYCODE_DPAD_DOWN:      return UCKeys::Down;
            case AKEYCODE_DPAD_LEFT:      return UCKeys::Left;
            case AKEYCODE_DPAD_RIGHT:     return UCKeys::Right;
            case AKEYCODE_PAGE_UP:        return UCKeys::PageUp;
            case AKEYCODE_PAGE_DOWN:      return UCKeys::PageDown;
            case AKEYCODE_MOVE_HOME:      return UCKeys::Home;
            case AKEYCODE_MOVE_END:       return UCKeys::End;
            case AKEYCODE_INSERT:         return UCKeys::Insert;

            case AKEYCODE_SHIFT_LEFT:     return UCKeys::LeftShift;
            case AKEYCODE_SHIFT_RIGHT:    return UCKeys::RightShift;
            case AKEYCODE_CTRL_LEFT:      return UCKeys::LeftCtrl;
            case AKEYCODE_CTRL_RIGHT:     return UCKeys::RightCtrl;
            case AKEYCODE_ALT_LEFT:       return UCKeys::LeftAlt;
            case AKEYCODE_ALT_RIGHT:      return UCKeys::RightAlt;
            case AKEYCODE_META_LEFT:      return UCKeys::LeftMeta;
            case AKEYCODE_META_RIGHT:     return UCKeys::RightMeta;

            case AKEYCODE_SEMICOLON:      return UCKeys::Semicolon;
            case AKEYCODE_EQUALS:         return UCKeys::Equal;
            case AKEYCODE_COMMA:          return UCKeys::Comma;
            case AKEYCODE_MINUS:          return UCKeys::Minus;
            case AKEYCODE_PERIOD:         return UCKeys::Period;
            case AKEYCODE_SLASH:          return UCKeys::Slash;
            case AKEYCODE_GRAVE:          return UCKeys::Grave;
            case AKEYCODE_LEFT_BRACKET:   return UCKeys::LeftBracket;
            case AKEYCODE_BACKSLASH:      return UCKeys::Backslash;
            case AKEYCODE_RIGHT_BRACKET:  return UCKeys::RightBracket;
            case AKEYCODE_APOSTROPHE:     return UCKeys::Quote;

            case AKEYCODE_NUM_LOCK:       return UCKeys::NumLock;
            case AKEYCODE_NUMPAD_DIVIDE:  return UCKeys::NumPadDivide;
            case AKEYCODE_NUMPAD_MULTIPLY:return UCKeys::NumPadMultiply;
            case AKEYCODE_NUMPAD_SUBTRACT:return UCKeys::NumPadMinus;
            case AKEYCODE_NUMPAD_ADD:     return UCKeys::NumPadPlus;
            case AKEYCODE_NUMPAD_DOT:     return UCKeys::NumPadDecimal;
            case AKEYCODE_NUMPAD_ENTER:   return UCKeys::NumPadEnter;

            case AKEYCODE_CAPS_LOCK:      return UCKeys::CapsLock;
            case AKEYCODE_SCROLL_LOCK:    return UCKeys::ScrollLock;
            case AKEYCODE_BREAK:          return UCKeys::Pause;
            case AKEYCODE_SYSRQ:          return UCKeys::PrintScreen;
            case AKEYCODE_MENU:           return UCKeys::Menu;

            case AKEYCODE_VOLUME_UP:      return UCKeys::VolumeUp;
            case AKEYCODE_VOLUME_DOWN:    return UCKeys::VolumeDown;
            case AKEYCODE_VOLUME_MUTE:    return UCKeys::VolumeMute;
            case AKEYCODE_MEDIA_PLAY_PAUSE: return UCKeys::MediaPlay;
            case AKEYCODE_MEDIA_STOP:     return UCKeys::MediaStop;
            case AKEYCODE_MEDIA_PREVIOUS: return UCKeys::MediaPrevious;
            case AKEYCODE_MEDIA_NEXT:     return UCKeys::MediaNext;

            default:                      return UCKeys::Unknown;
        }
    }

    char UltraCanvasAndroidApplication::DeriveAsciiCharacter(UCKeys key, bool shift) {
        if (key >= UCKeys::A && key <= UCKeys::Z) {
            return shift ? static_cast<char>(key)
                         : static_cast<char>(key - UCKeys::A + 'a');
        }
        if (key >= UCKeys::Key0 && key <= UCKeys::Key9) {
            if (!shift) return static_cast<char>(key);
            static const char shifted[] = ")!@#$%^&*(";
            return shifted[key - UCKeys::Key0];
        }
        if (key >= UCKeys::NumPad0 && key <= UCKeys::NumPad9) {
            return static_cast<char>('0' + (key - UCKeys::NumPad0));
        }
        switch (key) {
            case UCKeys::Space:          return ' ';
            case UCKeys::Return:         return '\r';
            case UCKeys::Tab:            return '\t';
            case UCKeys::Semicolon:      return shift ? ':' : ';';
            case UCKeys::Equal:          return shift ? '+' : '=';
            case UCKeys::Comma:          return shift ? '<' : ',';
            case UCKeys::Minus:          return shift ? '_' : '-';
            case UCKeys::Period:         return shift ? '>' : '.';
            case UCKeys::Slash:          return shift ? '?' : '/';
            case UCKeys::Grave:          return shift ? '~' : '`';
            case UCKeys::LeftBracket:    return shift ? '{' : '[';
            case UCKeys::Backslash:      return shift ? '|' : '\\';
            case UCKeys::RightBracket:   return shift ? '}' : ']';
            case UCKeys::Quote:          return shift ? '"' : '\'';
            case UCKeys::NumPadDivide:   return '/';
            case UCKeys::NumPadMultiply: return '*';
            case UCKeys::NumPadMinus:    return '-';
            case UCKeys::NumPadPlus:     return '+';
            case UCKeys::NumPadDecimal:  return '.';
            case UCKeys::NumPadEnter:    return '\r';
            default:                     return 0;
        }
    }

    // ===== FONTS =====

    FontStyle UltraCanvasAndroidApplication::DetectSystemFontStyleNative() {
        FontStyle result;
        result.fontFamily = "Roboto";
        result.fontSize = 12.0;
        return result;
    }

    FontStyle UltraCanvasAndroidApplication::DetectMonospacedFontStyleNative() {
        FontStyle result;
        result.fontFamily = "Droid Sans Mono";
        result.fontSize = 12.0;
        return result;
    }

    void UltraCanvasAndroidApplication::LoadBundledFontsNative() {
        // Same FontConfig registration as the Linux backend: add the bundled
        // fonts as app fonts and force a FreeType-backed Pango font map.
        FcConfig* cfg = FcConfigGetCurrent();
        if (!cfg) {
            debugOutput << "UltraCanvas Android: FcConfigGetCurrent() returned null; "
                           "bundled fonts not registered" << std::endl;
            return;
        }

        const std::string dir = GetBundledFontsDir();
        for (size_t i = 0; i < kEmbeddedAllFontsCount; ++i) {
            std::string path = dir + kEmbeddedAllFonts[i];
            if (!std::filesystem::exists(path)) {
                debugOutput << "UltraCanvas: bundled font missing: " << path << std::endl;
                continue;
            }
            if (!FcConfigAppFontAddFile(cfg,
                    reinterpret_cast<const FcChar8*>(path.c_str()))) {
                debugOutput << "UltraCanvas: FcConfigAppFontAddFile failed for " << path << std::endl;
            }
        }
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
    }

} // namespace UltraCanvas
