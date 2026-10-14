// OS/WASM/UltraCanvasWASMApplication.cpp
// WebAssembly (Emscripten) platform application implementation
// Version: 2.2.0 - input method composition: a transparent textarea takes the
//   keyboard while the focused element draws compositions; its composition
//   events become TextComposition events
// Version: 2.1.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework

#include "../../include/UltraCanvasApplication.h"
#include "UltraCanvasWASMApplication.h"
#include "UltraCanvasWASMWindow.h"
#include "UltraCanvasWASMClipboard.h"
#include "../../include/UltraCanvasClipboard.h"
#include "../../include/UltraCanvasImage.h"
#include "../../include/UltraCanvasDebug.h"

#include <fontconfig/fontconfig.h>

#include <sys/select.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>

// ===== PASTE BRIDGE =====
// The browser only exposes clipboard contents synchronously inside a `paste`
// event, and it only fires that event when the page lets the Ctrl/Cmd+V
// keydown through (OnKeyCallback does). The DOM listener registered in
// InitializeNative() lands here with the pasted text: hand it to the
// clipboard backend first, then queue the Ctrl+V key event the application
// was waiting for, so the text field's paste handler finds the text in place.
extern "C" EMSCRIPTEN_KEEPALIVE void UltraCanvasWasmOnPaste(char* text) {
    if (text) {
        UltraCanvas::UltraCanvasWASMClipboard::OfferText(text);
        free(text);
    }
    auto* app = UltraCanvas::UltraCanvasWASMApplication::GetInstance();
    if (!app) return;
    UltraCanvas::UCEvent event;
    event.type = UltraCanvas::UCEventType::KeyDown;
    event.virtualKey = static_cast<UltraCanvas::UCKeys>('V');
    event.nativeKeyCode = 'V';
    event.ctrl = true;
    app->PushEvent(event);
}

// ===== INPUT METHOD BRIDGE =====
// The hidden textarea's composition and input events (registered in
// InitializeNative()) land here; each string was allocated with
// stringToNewUTF8 and is freed here.
namespace {
    std::string TakeImeString(char* text) {
        std::string result = text ? std::string(text) : std::string();
        free(text);
        return result;
    }
}

extern "C" EMSCRIPTEN_KEEPALIVE void UltraCanvasWasmOnImeCompositionStart() {
    if (auto* app = UltraCanvas::UltraCanvasWASMApplication::GetInstance()) app->OnImeCompositionStart();
}

extern "C" EMSCRIPTEN_KEEPALIVE void UltraCanvasWasmOnImeComposition(char* text, int cursorUtf16) {
    const std::string composing = TakeImeString(text);
    if (auto* app = UltraCanvas::UltraCanvasWASMApplication::GetInstance()) app->OnImeComposition(composing, cursorUtf16);
}

extern "C" EMSCRIPTEN_KEEPALIVE void UltraCanvasWasmOnImeCompositionEnd(char* text) {
    const std::string committed = TakeImeString(text);
    if (auto* app = UltraCanvas::UltraCanvasWASMApplication::GetInstance()) app->OnImeCompositionEnd(committed);
}

extern "C" EMSCRIPTEN_KEEPALIVE void UltraCanvasWasmOnImeText(char* text) {
    const std::string typed = TakeImeString(text);
    if (auto* app = UltraCanvas::UltraCanvasWASMApplication::GetInstance()) app->OnImeText(typed);
}

extern "C" EMSCRIPTEN_KEEPALIVE void UltraCanvasWasmOnImeFocus(int focused) {
    if (auto* app = UltraCanvas::UltraCanvasWASMApplication::GetInstance()) app->OnImeFocus(focused != 0);
}

namespace UltraCanvas {

    UltraCanvasWASMApplication* UltraCanvasWASMApplication::instance = nullptr;

    namespace {
        // Ctrl+V / Cmd+V: the one shortcut the browser must see, so that it
        // fires `paste` (see UltraCanvasWasmOnPaste above).
        bool IsPasteChord(const EmscriptenKeyboardEvent* keyEvent) {
            if (!(keyEvent->ctrlKey || keyEvent->metaKey) || keyEvent->altKey) return false;
            return keyEvent->key[1] == 0 && (keyEvent->key[0] == 'v' || keyEvent->key[0] == 'V');
        }
    }

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

        // Paste bridge (no Emscripten HTML5 wrapper exists for clipboard
        // events). The canvas has focus while the app is used, and `paste`
        // bubbles from it to the document.
        EM_ASM({
            if (window.__ultracanvasOnPaste) {
                document.removeEventListener('paste', window.__ultracanvasOnPaste);
            }
            window.__ultracanvasOnPaste = function(e) {
                var data = e.clipboardData || window.clipboardData;
                var text = '';
                try { text = data ? (data.getData('text/plain') || '') : ''; } catch (err) {}
                e.preventDefault();
                _UltraCanvasWasmOnPaste(stringToNewUTF8(text));
            };
            document.addEventListener('paste', window.__ultracanvasOnPaste);
        });

        // Input method bridge: a transparent textarea that takes the
        // keyboard while the focused element draws compositions (see
        // UpdateImeTarget). Keys still reach OnKeyCallback on the window;
        // the textarea only adds the browser's composition events. It never
        // keeps text: what it would hold is delivered and cleared.
        EM_ASM({
            if (window.__ultracanvasIme && window.__ultracanvasIme.parentNode) {
                window.__ultracanvasIme.parentNode.removeChild(window.__ultracanvasIme);
            }
            var ta = document.createElement('textarea');
            ta.setAttribute('autocomplete', 'off');
            ta.setAttribute('autocorrect', 'off');
            ta.setAttribute('autocapitalize', 'off');
            ta.setAttribute('spellcheck', 'false');
            ta.setAttribute('aria-hidden', 'true');
            ta.tabIndex = -1;
            var st = ta.style;
            st.position = 'absolute';
            st.left = '0px';
            st.top = '0px';
            st.width = '1px';
            st.height = '16px';
            st.margin = '0';
            st.padding = '0';
            st.border = '0';
            st.outline = 'none';
            st.resize = 'none';
            st.overflow = 'hidden';
            st.whiteSpace = 'nowrap';
            st.opacity = '0';
            st.color = 'transparent';
            st.background = 'transparent';
            st.caretColor = 'transparent';
            st.pointerEvents = 'none';
            st.zIndex = '2147483647';
            document.body.appendChild(ta);
            window.__ultracanvasIme = ta;

            var sentText = null;
            var sentCursor = -1;
            var composing = '';
            var send = function(text, cursor) {
                if (text === sentText && cursor === sentCursor) return;
                sentText = text;
                sentCursor = cursor;
                _UltraCanvasWasmOnImeComposition(stringToNewUTF8(text), cursor);
            };
            ta.addEventListener('compositionstart', function() {
                composing = '';
                sentText = null;
                sentCursor = -1;
                _UltraCanvasWasmOnImeCompositionStart();
            });
            ta.addEventListener('compositionupdate', function(e) {
                composing = e.data || '';
                send(composing, composing.length);
            });
            ta.addEventListener('compositionend', function(e) {
                var text = e.data || '';
                composing = '';
                sentText = null;
                sentCursor = -1;
                ta.value = '';
                _UltraCanvasWasmOnImeCompositionEnd(stringToNewUTF8(text));
            });
            ta.addEventListener('input', function(e) {
                if (e.isComposing) {
                    if (ta.value === composing) {
                        var caret = ta.selectionEnd;
                        if (typeof caret !== 'number' || caret > composing.length) caret = composing.length;
                        send(composing, caret);
                    }
                    return;
                }
                if (e.inputType === 'insertText' && e.data) {
                    _UltraCanvasWasmOnImeText(stringToNewUTF8(e.data));
                }
                ta.value = '';
            });
            ta.addEventListener('focus', function() { _UltraCanvasWasmOnImeFocus(1); });
            ta.addEventListener('blur', function() { _UltraCanvasWasmOnImeFocus(0); });
        });

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
        imeSwitchingFocus = true;     // removing a focused textarea blurs it: not the window's doing
        EM_ASM({
            if (window.__ultracanvasOnPaste) {
                document.removeEventListener('paste', window.__ultracanvasOnPaste);
                window.__ultracanvasOnPaste = null;
            }
            if (window.__ultracanvasIme) {
                if (window.__ultracanvasIme.parentNode) {
                    window.__ultracanvasIme.parentNode.removeChild(window.__ultracanvasIme);
                }
                window.__ultracanvasIme = null;
            }
        });
        imeSwitchingFocus = false;
        imeWindow.reset();
        imeCanvasId.clear();
        imeActive = false;
        imeComposing = false;
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
        UpdateImeTarget();
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
            case UCMouseCursor::AppStarting:  return "progress";
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

        // Let the paste chord through untouched: the browser answers it with a
        // `paste` event, and UltraCanvasWasmOnPaste() queues the KeyDown from
        // there, after the clipboard text has been captured. (The KeyUp still
        // arrives through the normal path below.)
        if (eventType == EMSCRIPTEN_EVENT_KEYDOWN && IsPasteChord(keyEvent)) {
            return EM_FALSE;
        }

        // While the input method textarea has the keyboard, a key the input
        // method is processing (keyCode 229, or any key mid-composition) is
        // its business: not delivered as a key press too - its text comes
        // from compositionend - and not cancelled, or the composition
        // breaks. A dead key is let through so the browser can compose its
        // accent.
        if (app->imeActive) {
            constexpr unsigned long kImeProcessKeyCode = 229;
            if (keyEvent->keyCode == kImeProcessKeyCode || app->imeComposing) {
                return EM_FALSE;
            }
            if (eventType == EMSCRIPTEN_EVENT_KEYDOWN && strcmp(keyEvent->key, "Dead") == 0) {
                return EM_FALSE;
            }
        }

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

// ===== INPUT METHOD COMPOSITION =====

    namespace {
        // A UTF-16 offset into UTF-8 text (what the DOM counts) as a byte
        // offset, never inside a character.
        int Utf8OffsetOfUtf16(const std::string& text, int units) {
            size_t i = 0;
            int counted = 0;
            while (i < text.size() && counted < units) {
                const unsigned char c = static_cast<unsigned char>(text[i]);
                const size_t length = c < 0x80 ? 1 : (c >> 5) == 0x06 ? 2 : (c >> 4) == 0x0E ? 3 : 4;
                counted += (length == 4) ? 2 : 1;
                i += length;
            }
            return static_cast<int>(std::min(i, text.size()));
        }
    }

    void UltraCanvasWASMApplication::UpdateImeTarget() {
        UltraCanvasWindow* window = GetFocusedWindow();
        UltraCanvasUIElement* focused = window ? window->GetFocusedElement() : nullptr;
        const bool wanted = window && window->IsWindowVisible() && focused && focused->DrawsTextComposition();
        auto current = imeWindow.lock();

        // No window active (the tab or the page lost the keyboard): the
        // textarea stays the window's keyboard, so its focus coming back
        // reactivates the window (OnImeFocus), as the canvas's would.
        if (!window && current) return;

        if (wanted) {
            if (imeActive && current.get() == window) return;
            // Take the keyboard from our own canvases (or from nobody), never
            // from a page element the user is typing into.
            const int mayTake = EM_ASM_INT({
                var a = document.activeElement;
                return (!a || a === document.body || a === window.__ultracanvasIme ||
                        (a.id && a.id.indexOf('ultracanvas-window-') === 0)) ? 1 : 0;
            }, 0);
            if (!mayTake) return;
            imeWindow = window->GetWindowWeakPtr();
            imeCanvasId = window->GetCanvasId();
            imeActive = true;
            PositionImeInput();
            SetImeFocus(true, imeCanvasId);
            return;
        }

        if (!imeActive) return;
        // The focused element no longer draws compositions: the canvas gets
        // the keyboard back (a composition in progress ends as typed text,
        // which the browser does when the textarea loses focus).
        imeActive = false;
        SetImeFocus(false, current ? imeCanvasId : std::string());
        if (imeComposing) {
            imeComposing = false;
            UCEvent end;
            end.type = UCEventType::TextComposition;
            end.compositionCursor = 0;
            PushImeEvent(end);
        }
        imeWindow.reset();
        imeCanvasId.clear();
    }

    void UltraCanvasWASMApplication::SetImeFocus(bool toInput, const std::string& canvasId) {
        imeSwitchingFocus = true;
        EM_ASM({
            var ta = window.__ultracanvasIme;
            if (!ta) return;
            if ($0) {
                if (document.activeElement !== ta) ta.focus({ preventScroll: true });
                return;
            }
            if (document.activeElement !== ta) return;
            var canvas = document.getElementById(UTF8ToString($1));
            if (canvas) {
                canvas.focus({ preventScroll: true });
            } else {
                ta.blur();
            }
        }, toInput ? 1 : 0, canvasId.c_str());
        imeSwitchingFocus = false;
    }

    void UltraCanvasWASMApplication::PositionImeInput() {
        // The browser puts its candidate window at the textarea's caret, so
        // the textarea sits on the element's caret, one line high.
        auto window = imeWindow.lock();
        if (!window) return;
        UltraCanvasUIElement* focused = window->GetFocusedElement();
        if (!focused) return;
        Rect2Df caret = focused->GetBoundsInWindow();
        if (IAccessibleText* text = focused->GetAccessibleTextInterface()) {
            const Rect2Df at = text->GetCharacterBounds(text->GetCaretOffset());
            if (at.height > 0.0f) caret = at;
        }
        EM_ASM({
            var ta = window.__ultracanvasIme;
            var canvas = document.getElementById(UTF8ToString($0));
            if (!ta || !canvas) return;
            var box = canvas.getBoundingClientRect();
            var line = Math.max(1, Math.round($3));
            ta.style.left = Math.round(box.left + window.scrollX + $1) + 'px';
            ta.style.top = Math.round(box.top + window.scrollY + $2) + 'px';
            ta.style.height = line + 'px';
            ta.style.fontSize = line + 'px';
            ta.style.lineHeight = line + 'px';
        }, imeCanvasId.c_str(), caret.x, caret.y, caret.height);
    }

    void UltraCanvasWASMApplication::PushImeEvent(UCEvent& event) {
        auto window = imeWindow.lock();
        if (!window) return;
        event.targetWindow = window;
        event.nativeWindowHandle = window->GetNativeHandle();
        PushEvent(event);
    }

    bool UltraCanvasWASMApplication::PrepareImeForClick(UltraCanvasWindowBase* window) {
        if (!imeActive) return false;
        if (imeComposing) {
            // A click ends the composition, keeping what was composed:
            // taking the focus away and back makes the browser commit it
            // (compositionend), before the click is delivered.
            imeSwitchingFocus = true;
            EM_ASM({
                var ta = window.__ultracanvasIme;
                if (ta && document.activeElement === ta) {
                    ta.blur();
                    ta.focus({ preventScroll: true });
                }
            }, 0);
            imeSwitchingFocus = false;
        }
        auto current = imeWindow.lock();
        if (!window || current.get() != window) return false;
        SetImeFocus(true, imeCanvasId);
        return true;
    }

    void UltraCanvasWASMApplication::OnImeCompositionStart() {
        imeComposing = true;
        PositionImeInput();
    }

    void UltraCanvasWASMApplication::OnImeComposition(const std::string& text, int cursorUtf16) {
        UCEvent event;
        event.type = UCEventType::TextComposition;
        event.text = text;
        event.compositionCursor = Utf8OffsetOfUtf16(text, std::max(0, cursorUtf16));
        PushImeEvent(event);
    }

    void UltraCanvasWASMApplication::OnImeCompositionEnd(const std::string& text) {
        imeComposing = false;
        UCEvent end;
        end.type = UCEventType::TextComposition;
        end.compositionCursor = 0;
        PushImeEvent(end);
        OnImeText(text);
    }

    void UltraCanvasWASMApplication::OnImeText(const std::string& text) {
        // Committed text: a KeyDown with `text`, like a typed character.
        if (text.empty()) return;
        UCEvent event;
        event.type = UCEventType::KeyDown;
        event.virtualKey = UCKeys::Unknown;
        event.text = text;
        event.character = (text.size() == 1 && static_cast<unsigned char>(text[0]) < 128) ? text[0] : 0;
        PushImeEvent(event);
    }

    void UltraCanvasWASMApplication::OnImeFocus(bool focused) {
        // The textarea stands in for the canvas: the page or tab losing and
        // regaining the keyboard is the window deactivating and activating,
        // as a canvas focus change is (OnFocusCallback).
        if (imeSwitchingFocus) return;
        UCEvent event;
        event.type = focused ? UCEventType::WindowFocus : UCEventType::WindowBlur;
        PushImeEvent(event);
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

    bool UltraCanvasWASMApplication::RegisterFontFileNative(const std::string& fontFilePath) {
        // An application font: added to this process's FcConfig only, so
        // nothing is installed for the user or for other applications.
        FcConfig* cfg = FcConfigGetCurrent();
        if (!cfg) {
            debugOutput << "UltraCanvas: RegisterFontFileNative: no current "
                           "fontconfig config" << std::endl;
            return false;
        }
        if (!FcConfigAppFontAddFile(
                cfg, reinterpret_cast<const FcChar8*>(fontFilePath.c_str()))) {
            debugOutput << "UltraCanvas: FcConfigAppFontAddFile failed for "
                        << fontFilePath << std::endl;
            return false;
        }
        return true;
    }

} // namespace UltraCanvas
