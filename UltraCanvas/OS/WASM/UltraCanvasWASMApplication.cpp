// OS/WASM/UltraCanvasWASMApplication.cpp
// WebAssembly (Emscripten) platform application implementation
// Version: 2.0.0
// Last Modified: 2026-08-12
// Author: UltraCanvas Framework

#include "../../include/UltraCanvasApplication.h"
#include "UltraCanvasWASMApplication.h"
#include "UltraCanvasWASMWindow.h"
#include "../../include/UltraCanvasClipboard.h"
#include "../../include/UltraCanvasImage.h"
#include "../../include/UltraCanvasDebug.h"

#include <fontconfig/fontconfig.h>

#include <sys/select.h>
#include <cctype>
#include <cstring>
#include <filesystem>

namespace UltraCanvas {

    UltraCanvasWASMApplication* UltraCanvasWASMApplication::instance = nullptr;

// ===== CONSTRUCTOR / DESTRUCTOR =====

    UltraCanvasWASMApplication::UltraCanvasWASMApplication() {
        instance = this;
    }

    UltraCanvasWASMApplication::~UltraCanvasWASMApplication() {
        instance = nullptr;
    }

// ===== INITIALIZATION =====

    bool UltraCanvasWASMApplication::InitializeNative() {
        debugOutput << "UltraCanvas WASM: Initializing browser application..." << std::endl;

        // Keyboard events are registered once on the browser window, not per
        // canvas: DispatchEvent() routes KeyDown/KeyUp to the focused window
        // when the event carries no target (see UltraCanvasApplication.cpp).
        emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, EM_TRUE, OnKeyCallback);
        emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, this, EM_TRUE, OnKeyCallback);

        emscripten_set_visibilitychange_callback(this, EM_TRUE, OnVisibilityChange);
        emscripten_set_beforeunload_callback(this, OnBeforeUnload);

        InitializeWakeUp();

        running = false;
        initialized = true;
        return true;
    }

    void UltraCanvasWASMApplication::ShutdownNative() {
        // Must be safe on a partially-initialized state (called from error
        // paths too). All windows are already destroyed by the shutdown tail.
        ShutdownWakeUp();
        emscripten_set_keydown_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, nullptr);
        emscripten_set_keyup_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, EM_TRUE, nullptr);
        emscripten_set_visibilitychange_callback(nullptr, EM_TRUE, nullptr);
        emscripten_set_beforeunload_callback(nullptr, nullptr);
    }

// ===== MAIN LOOP =====

    void UltraCanvasWASMApplication::RunBeforeMainLoop() {
        // The browser owns the event loop, so the blocking while-loop in
        // UltraCanvasApplicationBase::Run() can never be entered. Run() calls
        // this hook right before that loop; emscripten_set_main_loop with
        // simulate_infinite_loop=1 never returns — it unwinds the C stack with
        // a JS exception the Emscripten runtime swallows — and from then on
        // each browser animation frame calls MainLoopTick() instead.
        emscripten_set_main_loop(&UltraCanvasWASMApplication::MainLoopTick, 0, 1);
    }

    void UltraCanvasWASMApplication::MainLoopTick() {
        auto* app = instance;
        if (!app) {
            emscripten_cancel_main_loop();
            return;
        }
        if (app->running && !app->windows.empty()) {
            app->RunOnce();
        } else {
            app->ShutdownAfterMainLoop();
        }
    }

    void UltraCanvasWASMApplication::ShutdownAfterMainLoop() {
        // Same shutdown tail as UltraCanvasApplicationBase::Run() runs after
        // its loop exits; Run() itself never reaches it on this platform.
        debugOutput << "UltraCanvas WASM: main loop ended, performing cleanup..." << std::endl;
        while (!windows.empty()) {
            try {
                auto window = windows.back();
                window->PerformClose();
                windows.pop_back();
            } catch (const std::exception& e) {
                debugOutput << "UltraCanvas WASM: Exception destroying window: " << e.what() << std::endl;
            }
        }

        if (onApplicationExit) {
            onApplicationExit();
        }

        initialized = false;

        ShutdownClipboard();
        ShutdownNative();
        UCImage::ShutdownImageSubsysterm();

        emscripten_cancel_main_loop();
    }

    void UltraCanvasWASMApplication::CollectAndProcessNativeEvents() {
        // Browser input arrives through DOM callbacks between animation
        // frames, so by the time RunOnce() runs the UCEvents are already
        // queued — there is nothing to collect and this must never block.
        // Registered fd watches (WebSocket-proxied sockets etc.) are still
        // polled with a zero timeout so AddFdWatch() clients keep working.
        auto keys = SnapshotFdWatchKeys();
        if (keys.empty()) {
            return;
        }

        fd_set readfds, writefds;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        int maxFd = -1;
        for (const auto& key : keys) {
            if (key.fd < 0) continue;
            if (key.type == FdWatchType::Write) {
                FD_SET(key.fd, &writefds);
            } else {
                FD_SET(key.fd, &readfds);
            }
            if (key.fd > maxFd) maxFd = key.fd;
        }
        if (maxFd < 0) {
            return;
        }

        struct timeval zeroTimeout = {0, 0};
        int result = select(maxFd + 1, &readfds, &writefds, nullptr, &zeroTimeout);
        if (result > 0) {
            for (const auto& key : keys) {
                if (key.fd < 0) continue;
                fd_set* set = (key.type == FdWatchType::Write) ? &writefds : &readfds;
                if (FD_ISSET(key.fd, set)) {
                    FireFdWatch(key.id);
                }
            }
        }
    }

// ===== WAKEUP =====
// A frame is always coming via requestAnimationFrame (setTimeout while the
// page is hidden), so cross-thread wakeups need no signalling primitive.

    void UltraCanvasWASMApplication::WakeUpEventLoop() {}
    void UltraCanvasWASMApplication::InitializeWakeUp() {}
    void UltraCanvasWASMApplication::ShutdownWakeUp() {}

// ===== MOUSE CAPTURE =====

    void UltraCanvasWASMApplication::CaptureMouseNative() {
        // No browser-global mouse capture without the Pointer Events API;
        // drags that leave the canvas stop receiving moves until re-entry.
        // The framework-level capture (CaptureMouse) still routes events that
        // do arrive to the captured element.
    }

    void UltraCanvasWASMApplication::ReleaseMouseNative() {}

// ===== MOUSE CURSOR =====

    static const char* CSSCursorName(UCMouseCursor cur) {
        switch (cur) {
            case UCMouseCursor::Default:      return "default";
            case UCMouseCursor::NoCursor:     return "none";
            case UCMouseCursor::Hand:         return "pointer";
            case UCMouseCursor::Text:         return "text";
            case UCMouseCursor::Wait:         return "wait";
            case UCMouseCursor::Cross:        return "crosshair";
            case UCMouseCursor::Help:         return "help";
            case UCMouseCursor::NotAllowed:   return "not-allowed";
            case UCMouseCursor::LookingGlass: return "zoom-in";
            case UCMouseCursor::SizeAll:      return "move";
            case UCMouseCursor::SizeNS:       return "ns-resize";
            case UCMouseCursor::SizeWE:       return "ew-resize";
            case UCMouseCursor::SizeNWSE:     return "nwse-resize";
            case UCMouseCursor::SizeNESW:     return "nesw-resize";
            case UCMouseCursor::ContextMenu:  return "context-menu";
            default:                          return nullptr;
        }
    }

    bool UltraCanvasWASMApplication::SelectMouseCursorNative(UltraCanvasWindowBase* win, UCMouseCursor cur) {
        auto* wasmWin = static_cast<UltraCanvasWASMWindow*>(win);
        if (!wasmWin) return false;
        const char* css = CSSCursorName(cur);
        if (!css) return false;
        EM_ASM({
            var canvas = document.getElementById(UTF8ToString($0));
            if (canvas) canvas.style.cursor = UTF8ToString($1);
        }, wasmWin->GetCanvasId().c_str(), css);
        return true;
    }

    bool UltraCanvasWASMApplication::SelectMouseCursorNative(UltraCanvasWindowBase* win, UCMouseCursor cur,
                                                             const char* filename, int hotspotX, int hotspotY) {
        // Custom cursor images live in the Emscripten virtual FS, which CSS
        // url() cannot reference; fall back to the standard cursor.
        return SelectMouseCursorNative(win, cur);
    }

// ===== PAGE LIFECYCLE =====

    EM_BOOL UltraCanvasWASMApplication::OnVisibilityChange(int eventType,
                                                           const EmscriptenVisibilityChangeEvent* event,
                                                           void* userData) {
        // requestAnimationFrame stops in hidden tabs, which would stall the
        // framework's timers and PostToUIThread tasks. Fall back to a slow
        // setTimeout cadence while hidden.
        if (event->hidden) {
            emscripten_set_main_loop_timing(EM_TIMING_SETTIMEOUT, 250);
        } else {
            emscripten_set_main_loop_timing(EM_TIMING_RAF, 1);
        }
        return EM_TRUE;
    }

    const char* UltraCanvasWASMApplication::OnBeforeUnload(int eventType, const void* reserved, void* userData) {
        auto* app = static_cast<UltraCanvasWASMApplication*>(userData);
        if (app) {
            app->Exit();
        }
        return nullptr; // no "leave site?" prompt
    }

// ===== KEYBOARD =====

    UCKeys UltraCanvasWASMApplication::ConvertDomKey(const char* key, const char* code, unsigned long location) {
        // DOM_KEY_LOCATION_* values (KeyboardEvent.location)
        constexpr unsigned long kLocationRight = 2;
        constexpr unsigned long kLocationNumpad = 3;

        if (location == kLocationNumpad) {
            if (key[0] >= '0' && key[0] <= '9' && key[1] == 0) {
                return static_cast<UCKeys>(UCKeys::NumPad0 + (key[0] - '0'));
            }
            if (strcmp(key, ".") == 0) return UCKeys::NumPadDecimal;
            if (strcmp(key, "+") == 0) return UCKeys::NumPadPlus;
            if (strcmp(key, "-") == 0) return UCKeys::NumPadMinus;
            if (strcmp(key, "*") == 0) return UCKeys::NumPadMultiply;
            if (strcmp(key, "/") == 0) return UCKeys::NumPadDivide;
            if (strcmp(key, "Enter") == 0) return UCKeys::NumPadEnter;
        }

        // Printable ASCII: UCKeys values match the uppercased ASCII codes.
        if (key[0] != 0 && key[1] == 0) {
            unsigned char ch = static_cast<unsigned char>(key[0]);
            if (ch >= 0x20 && ch < 0x7F) {
                return static_cast<UCKeys>(std::toupper(ch));
            }
        }

        struct KeyMapEntry { const char* name; UCKeys value; };
        static const KeyMapEntry namedKeys[] = {
            {"Enter", UCKeys::Return}, {"Tab", UCKeys::Tab}, {"Escape", UCKeys::Escape},
            {"Backspace", UCKeys::Backspace}, {"Delete", UCKeys::Delete},
            {"ArrowLeft", UCKeys::Left}, {"ArrowUp", UCKeys::Up},
            {"ArrowRight", UCKeys::Right}, {"ArrowDown", UCKeys::Down},
            {"Home", UCKeys::Home}, {"End", UCKeys::End},
            {"PageUp", UCKeys::PageUp}, {"PageDown", UCKeys::PageDown},
            {"Insert", UCKeys::Insert},
            {"F1", UCKeys::F1}, {"F2", UCKeys::F2}, {"F3", UCKeys::F3}, {"F4", UCKeys::F4},
            {"F5", UCKeys::F5}, {"F6", UCKeys::F6}, {"F7", UCKeys::F7}, {"F8", UCKeys::F8},
            {"F9", UCKeys::F9}, {"F10", UCKeys::F10}, {"F11", UCKeys::F11}, {"F12", UCKeys::F12},
            {"CapsLock", UCKeys::CapsLock}, {"NumLock", UCKeys::NumLock},
            {"ScrollLock", UCKeys::ScrollLock}, {"Pause", UCKeys::Pause},
            {"PrintScreen", UCKeys::PrintScreen}, {"ContextMenu", UCKeys::Menu},
            {"AudioVolumeUp", UCKeys::VolumeUp}, {"AudioVolumeDown", UCKeys::VolumeDown},
            {"AudioVolumeMute", UCKeys::VolumeMute},
        };
        for (const auto& entry : namedKeys) {
            if (strcmp(key, entry.name) == 0) return entry.value;
        }

        if (strcmp(key, "Shift") == 0)   return (location == kLocationRight) ? UCKeys::RightShift : UCKeys::LeftShift;
        if (strcmp(key, "Control") == 0) return (location == kLocationRight) ? UCKeys::RightCtrl : UCKeys::LeftCtrl;
        if (strcmp(key, "Alt") == 0)     return (location == kLocationRight) ? UCKeys::RightAlt : UCKeys::LeftAlt;
        if (strcmp(key, "Meta") == 0)    return (location == kLocationRight) ? UCKeys::RightMeta : UCKeys::LeftMeta;

        return UCKeys::Unknown;
    }

    UCEvent UltraCanvasWASMApplication::ConvertKeyEvent(int eventType, const EmscriptenKeyboardEvent* keyEvent) {
        UCEvent event;
        event.type = (eventType == EMSCRIPTEN_EVENT_KEYDOWN) ? UCEventType::KeyDown : UCEventType::KeyUp;
        event.nativeKeyCode = static_cast<int>(keyEvent->keyCode);
        event.virtualKey = ConvertDomKey(keyEvent->key, keyEvent->code, keyEvent->location);
        event.shift = keyEvent->shiftKey;
        event.ctrl = keyEvent->ctrlKey;
        event.alt = keyEvent->altKey;
        event.meta = keyEvent->metaKey;

        // Text arrives on KeyDown, exactly like the desktop backends (no
        // separate TextInput event): KeyboardEvent.key is the typed grapheme
        // when it is not a named key ("Enter", "Shift", ...). Named keys are
        // multi-byte ASCII strings; a typed grapheme is either one ASCII byte
        // or a multi-byte UTF-8 sequence starting >= 0x80.
        if (eventType == EMSCRIPTEN_EVENT_KEYDOWN && !keyEvent->ctrlKey && !keyEvent->metaKey) {
            const char* key = keyEvent->key;
            size_t len = strlen(key);
            bool isTypedGrapheme = (len == 1) ||
                                   (len > 1 && (static_cast<unsigned char>(key[0]) & 0x80) != 0);
            if (isTypedGrapheme) {
                event.text.assign(key, len);
                if (len == 1 && static_cast<unsigned char>(key[0]) < 128) {
                    event.character = key[0];
                }
            }
        }
        return event;
    }

    EM_BOOL UltraCanvasWASMApplication::OnKeyCallback(int eventType, const EmscriptenKeyboardEvent* keyEvent,
                                                      void* userData) {
        auto* app = static_cast<UltraCanvasWASMApplication*>(userData);
        if (!app) return EM_FALSE;

        UCEvent event = app->ConvertKeyEvent(eventType, keyEvent);
        if (event.virtualKey != UCKeys::Unknown || !event.text.empty()) {
            app->PushEvent(event);
        }

        // Keep browser shortcuts for reload / fullscreen / devtools; consume
        // everything else so Tab, Backspace, arrows etc. reach the app instead
        // of scrolling or navigating the page.
        if (strcmp(keyEvent->key, "F5") == 0 || strcmp(keyEvent->key, "F11") == 0 ||
            strcmp(keyEvent->key, "F12") == 0) {
            return EM_FALSE;
        }
        return EM_TRUE;
    }

// ===== FONTS =====

    FontStyle UltraCanvasWASMApplication::DetectSystemFontStyleNative() {
        // Text is rendered by Pango + Fontconfig against the bundled fonts
        // preloaded into the Emscripten virtual FS, so the defaults name the
        // bundled families rather than browser-only names like "system-ui".
        FontStyle result;
        result.fontFamily = "Ubuntu";
        result.fontSize = 12.0;
        return result;
    }

    FontStyle UltraCanvasWASMApplication::DetectMonospacedFontStyleNative() {
        FontStyle result;
        result.fontFamily = "Ubuntu Mono";
        result.fontSize = 12.0;
        return result;
    }

    void UltraCanvasWASMApplication::LoadBundledFontsNative() {
        // Same registration as the Linux backend: the bundled TTFs must be
        // preloaded into the virtual FS (--preload-file media/fonts@/...) so
        // GetBundledFontsDir() resolves. Missing files are skipped with a
        // warning, so an app that ships no fonts still starts (with whatever
        // fontconfig can find in the virtual FS).
        FcConfig* cfg = FcConfigGetCurrent();
        if (!cfg) {
            debugOutput << "UltraCanvas WASM: no fontconfig config, bundled fonts not registered" << std::endl;
            return;
        }

        const std::string dir = GetBundledFontsDir();
        for (size_t i = 0; i < kEmbeddedAllFontsCount; i++) {
            std::string path = dir + kEmbeddedAllFonts[i];
            if (!std::filesystem::exists(path)) {
                debugOutput << "UltraCanvas WASM: bundled font not found: " << path << std::endl;
                continue;
            }
            FcConfigAppFontAddFile(cfg, reinterpret_cast<const FcChar8*>(path.c_str()));
        }
        FcConfigBuildFonts(cfg);
    }

} // namespace UltraCanvas
