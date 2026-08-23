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
#include "UltraCanvasAndroidJni.h"
#include "UltraCanvasCaret.h"
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

    // ===== JNI CACHES (glue thread only; released in ShutdownNative) =====
    namespace {

        // InputMethodManager plumbing for the soft keyboard.
        struct ImeJniCache {
            bool triedInit = false;
            jobject imm = nullptr;                  // global ref
            jmethodID midShowSoftInput = nullptr;   // (Landroid/view/View;I)Z
            jmethodID midHideSoftInput = nullptr;   // (Landroid/os/IBinder;I)Z
            jmethodID midGetWindow = nullptr;       // Activity.getWindow()
            jmethodID midGetDecorView = nullptr;    // Window.getDecorView()
            jmethodID midGetWindowToken = nullptr;  // View.getWindowToken()
        };
        ImeJniCache imeJni;

        bool EnsureImeJni(JNIEnv* env, jobject activity) {
            if (imeJni.imm) return true;
            if (imeJni.triedInit) return false;   // failed once; don't re-log
            imeJni.triedInit = true;

            jclass activityClass = env->GetObjectClass(activity);
            jmethodID midGetSystemService = env->GetMethodID(
                    activityClass, "getSystemService",
                    "(Ljava/lang/String;)Ljava/lang/Object;");
            imeJni.midGetWindow = env->GetMethodID(activityClass, "getWindow",
                    "()Landroid/view/Window;");
            env->DeleteLocalRef(activityClass);

            jstring serviceName = env->NewStringUTF("input_method");   // Context.INPUT_METHOD_SERVICE
            jobject imm = env->CallObjectMethod(activity, midGetSystemService, serviceName);
            env->DeleteLocalRef(serviceName);
            if (AndroidJni::ClearException(env, "getSystemService(input_method)") || !imm) {
                return false;
            }
            imeJni.imm = env->NewGlobalRef(imm);
            env->DeleteLocalRef(imm);

            jclass immClass = env->GetObjectClass(imeJni.imm);
            imeJni.midShowSoftInput = env->GetMethodID(immClass, "showSoftInput",
                    "(Landroid/view/View;I)Z");
            imeJni.midHideSoftInput = env->GetMethodID(immClass, "hideSoftInputFromWindow",
                    "(Landroid/os/IBinder;I)Z");
            env->DeleteLocalRef(immClass);

            jclass windowClass = env->FindClass("android/view/Window");
            imeJni.midGetDecorView = env->GetMethodID(windowClass, "getDecorView",
                    "()Landroid/view/View;");
            env->DeleteLocalRef(windowClass);

            jclass viewClass = env->FindClass("android/view/View");
            imeJni.midGetWindowToken = env->GetMethodID(viewClass, "getWindowToken",
                    "()Landroid/os/IBinder;");
            env->DeleteLocalRef(viewClass);

            if (AndroidJni::ClearException(env, "IME method lookup")) {
                env->DeleteGlobalRef(imeJni.imm);
                imeJni.imm = nullptr;
                return false;
            }
            return true;
        }

        // Not cached across calls: the decor view can be replaced on
        // configuration changes.
        jobject GetDecorView(JNIEnv* env, jobject activity) {
            jobject window = env->CallObjectMethod(activity, imeJni.midGetWindow);
            if (AndroidJni::ClearException(env, "Activity.getWindow") || !window) {
                return nullptr;
            }
            jobject decor = env->CallObjectMethod(window, imeJni.midGetDecorView);
            env->DeleteLocalRef(window);
            if (AndroidJni::ClearException(env, "Window.getDecorView")) return nullptr;
            return decor;
        }

        // KeyCharacterMap plumbing for layout-aware key translation.
        struct KcmJniCache {
            bool triedInit = false;
            jclass cls = nullptr;        // global ref android.view.KeyCharacterMap
            jmethodID midLoad = nullptr; // static (I)Landroid/view/KeyCharacterMap;
            jmethodID midGet = nullptr;  // (II)I
            jobject map = nullptr;       // global ref, for deviceId below
            int32_t deviceId = INT32_MIN;
        };
        KcmJniCache kcmJni;

        void ReleaseJniCaches() {
            JNIEnv* env = AndroidJni::GetEnv();
            if (env) {
                if (imeJni.imm) env->DeleteGlobalRef(imeJni.imm);
                if (kcmJni.map) env->DeleteGlobalRef(kcmJni.map);
                if (kcmJni.cls) env->DeleteGlobalRef(kcmJni.cls);
            }
            imeJni = ImeJniCache{};
            kcmJni = KcmJniCache{};
        }

    } // namespace

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

        // A widget claiming/releasing the caret is the framework-wide signal
        // that text editing started/stopped - drive the soft keyboard off it.
        UltraCanvasCaret::GetInstance().onTextEditingChanged =
                [this](bool active) { OnTextEditingChanged(active); };
        return true;
    }

    void UltraCanvasAndroidApplication::ShutdownNative() {
        UltraCanvasCaret::GetInstance().onTextEditingChanged = nullptr;
        ReleaseJniCaches();
        AndroidJni::DetachCurrentThread();
        if (androidApp && androidApp->userData == this) {
            androidApp->onAppCmd = nullptr;
            androidApp->onInputEvent = nullptr;
            androidApp->userData = nullptr;
        }
        if (instance == this) instance = nullptr;
    }

    // ===== SOFT KEYBOARD =====

    void UltraCanvasAndroidApplication::ShowSoftKeyboard() {
        JNIEnv* env = AndroidJni::GetEnv();
        jobject activity = AndroidJni::GetActivity();
        if (!env || !activity || !EnsureImeJni(env, activity)) return;

        jobject decor = GetDecorView(env, activity);
        if (!decor) return;
        env->CallBooleanMethod(imeJni.imm, imeJni.midShowSoftInput, decor, 0);
        AndroidJni::ClearException(env, "showSoftInput");
        env->DeleteLocalRef(decor);
    }

    void UltraCanvasAndroidApplication::HideSoftKeyboard() {
        JNIEnv* env = AndroidJni::GetEnv();
        jobject activity = AndroidJni::GetActivity();
        if (!env || !activity || !EnsureImeJni(env, activity)) return;

        jobject decor = GetDecorView(env, activity);
        if (!decor) return;
        jobject token = env->CallObjectMethod(decor, imeJni.midGetWindowToken);
        if (!AndroidJni::ClearException(env, "getWindowToken") && token) {
            env->CallBooleanMethod(imeJni.imm, imeJni.midHideSoftInput, token, 0);
            AndroidJni::ClearException(env, "hideSoftInputFromWindow");
            env->DeleteLocalRef(token);
        }
        env->DeleteLocalRef(decor);
    }

    void UltraCanvasAndroidApplication::OnTextEditingChanged(bool active) {
        if (active) {
            pendingImeHide = false;
            ShowSoftKeyboard();
        } else {
            // Deferred to the next CollectAndProcessNativeEvents turn: focus
            // moving from one text widget to another fires false-then-true
            // within the same event batch, and an immediate hide would
            // flicker the keyboard.
            pendingImeHide = true;
        }
    }

    // ===== EVENT PUMP =====

    void UltraCanvasAndroidApplication::CollectAndProcessNativeEvents() {
        if (!androidApp) return;

        // Text editing stopped last turn and nothing re-claimed the caret
        // since: retire the soft keyboard now.
        if (pendingImeHide) {
            pendingImeHide = false;
            HideSoftKeyboard();
        }

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
            const int32_t codePoint = GetUnicodeCharacter(
                    AInputEvent_getDeviceId(keyEvent), keyCode, metaState);
            if (codePoint >= 32 && codePoint != 127) {
                // UTF-8 encode into event.text (the field text widgets
                // prefer); event.character keeps the ASCII subset.
                char utf8[5] = {};
                if (codePoint < 0x80) {
                    utf8[0] = static_cast<char>(codePoint);
                    event.character = utf8[0];
                } else if (codePoint < 0x800) {
                    utf8[0] = static_cast<char>(0xC0 | (codePoint >> 6));
                    utf8[1] = static_cast<char>(0x80 | (codePoint & 0x3F));
                } else if (codePoint < 0x10000) {
                    utf8[0] = static_cast<char>(0xE0 | (codePoint >> 12));
                    utf8[1] = static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                    utf8[2] = static_cast<char>(0x80 | (codePoint & 0x3F));
                } else {
                    utf8[0] = static_cast<char>(0xF0 | (codePoint >> 18));
                    utf8[1] = static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
                    utf8[2] = static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
                    utf8[3] = static_cast<char>(0x80 | (codePoint & 0x3F));
                }
                event.text = utf8;
            } else if (codePoint == 0) {
                // JNI unavailable (or non-printing key): US-layout fallback.
                char c = DeriveAsciiCharacter(ucKey, event.shift);
                if (c) {
                    event.character = c;
                    event.text = std::string(1, c);
                }
            }
        }

        PushEvent(event);
        return 1;
    }

    int32_t UltraCanvasAndroidApplication::GetUnicodeCharacter(int32_t deviceId,
                                                               int32_t keyCode,
                                                               int32_t metaState) {
        JNIEnv* env = AndroidJni::GetEnv();
        if (!env) return 0;

        if (!kcmJni.cls) {
            if (kcmJni.triedInit) return 0;
            kcmJni.triedInit = true;
            jclass local = env->FindClass("android/view/KeyCharacterMap");
            if (AndroidJni::ClearException(env, "FindClass(KeyCharacterMap)") || !local) {
                return 0;
            }
            kcmJni.cls = static_cast<jclass>(env->NewGlobalRef(local));
            env->DeleteLocalRef(local);
            kcmJni.midLoad = env->GetStaticMethodID(kcmJni.cls, "load",
                    "(I)Landroid/view/KeyCharacterMap;");
            kcmJni.midGet = env->GetMethodID(kcmJni.cls, "get", "(II)I");
            if (AndroidJni::ClearException(env, "KeyCharacterMap method lookup")) {
                env->DeleteGlobalRef(kcmJni.cls);
                kcmJni.cls = nullptr;
                return 0;
            }
        }

        if (!kcmJni.map || kcmJni.deviceId != deviceId) {
            jobject map = env->CallStaticObjectMethod(kcmJni.cls, kcmJni.midLoad, deviceId);
            if (AndroidJni::ClearException(env, "KeyCharacterMap.load") || !map) {
                return 0;
            }
            if (kcmJni.map) env->DeleteGlobalRef(kcmJni.map);
            kcmJni.map = env->NewGlobalRef(map);
            env->DeleteLocalRef(map);
            kcmJni.deviceId = deviceId;
        }

        const jint unicode = env->CallIntMethod(kcmJni.map, kcmJni.midGet,
                                                keyCode, metaState);
        if (AndroidJni::ClearException(env, "KeyCharacterMap.get")) return 0;
        // COMBINING_ACCENT flag (bit 31): a dead key awaiting composition -
        // treat as non-printing (no composing-text support yet).
        if (unicode < 0) return 0;
        return unicode;
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
