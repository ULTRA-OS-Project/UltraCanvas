// OS/Linux/UltraCanvasLinuxApplication.cpp
// Complete Linux application implementation with all methods
// Version: 1.11.0 - the fonts and the keysym table shared with the Wayland backend (UltraCanvasLinuxFonts, UltraCanvasLinuxKeys)
// Version: 1.10.0 - a held key is one press, its repeats, and one release: detectable
//                   auto-repeat, and the fake releases dropped where the server lacks it
// Version: 1.9.0 - Property events of a clipboard copy sent in pieces go to the clipboard
// Last Modified: 2026-10-17
// Author: UltraCanvas Framework

#include "UltraCanvasLinuxAccessibility.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasLinuxClipboard.h"
#include "UltraCanvasLinuxFonts.h"
#include "UltraCanvasLinuxKeys.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasEmojiSettings.h"
#include <iostream>
#include <algorithm>
#include <filesystem>
#include <sstream>
#include <sys/select.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <PixelFX/PixelFX.h>
#include <errno.h>
#include <clocale>  // For setlocale
#include <fontconfig/fontconfig.h>
#include "UltraCanvasDebug.h"
#include <X11/XKBlib.h>   // XkbSetDetectableAutoRepeat

namespace UltraCanvas {
    UltraCanvasLinuxApplication* UltraCanvasLinuxApplication::instance = nullptr;

    // ===== CONSTRUCTOR & DESTRUCTOR =====
    UltraCanvasLinuxApplication::UltraCanvasLinuxApplication()
            : display(nullptr)
            , screen(0)
            , rootWindow(0)
            , visual(nullptr)
            , colormap(0)
            , depth(0)
            , glxSupported(false)
            , xim(nullptr)           // XIM initialization
            , eventThreadRunning(false) {
        instance = this;
        debugOutput << "UltraCanvas: Linux Application created" << std::endl;
    }

    UltraCanvasLinuxApplication::~UltraCanvasLinuxApplication() {
        // Clear the singleton pointer so GetInstance() returns nullptr once the
        // app is gone; widget destructors that run later (e.g. at static exit)
        // then skip CleanupElementReferences instead of locking a dead mutex.
        if (instance == this) instance = nullptr;
    }

// ===== INITIALIZATION =====
    bool UltraCanvasLinuxApplication::InitializeNative() {
        if (initialized) {
            debugOutput << "UltraCanvas: Already initialized" << std::endl;
            return true;
        }

        debugOutput << "UltraCanvas: Initializing Linux Application..." << std::endl;

        try {
            // STEP 1: Initialize X11 display connection
            if (!InitializeX11()) {
                debugOutput << "UltraCanvas: Failed to initialize X11" << std::endl;
                return false;
            }

            // STEP 2: Initialize GLX (optional)
            if (!InitializeGLX()) {
                debugOutput << "UltraCanvas: Failed to initialize GLX (non-critical)" << std::endl;
            }

            // STEP 3: Initialize window manager atoms
            InitializeAtoms();

            // STEP 4: Initialize X Input Method (XIM) for UTF-8 support
            if (!InitializeXIM()) {
                debugOutput << "UltraCanvas: Failed to initialize XIM (non-critical, falling back to basic input)" << std::endl;
            }

            // STEP 5: Initialize wakeup mechanism for cross-thread signaling
            InitializeWakeUp();

            // STEP 5b: Publish the UI to screen readers (AT-SPI) when the
            // desktop has accessibility on; services D-Bus through an fd watch.
            LinuxAccessibility::Start();

            // STEP 6: Mark as initialized
            initialized = true;
            running = false;

            debugOutput << "UltraCanvas: Linux Application initialized successfully" << std::endl;
            return true;

        } catch (const std::exception& e) {
            debugOutput << "UltraCanvas: Exception during initialization: " << e.what() << std::endl;
            ShutdownNative();
            return false;
        }
    }

    void UltraCanvasLinuxApplication::ShutdownNative() {
        LinuxAccessibility::Stop();

        // Clean up wakeup mechanism
        ShutdownWakeUp();

        // Clean up XIM
        ShutdownXIM();

        for (auto cursor : cursors) {
            XFreeCursor(display, cursor.second);
        }
        imageCursors.Clear();

        if (display) {
            debugOutput << "UltraCanvas: Closing X11 display..." << std::endl;
            XCloseDisplay(display);
            display = nullptr;
        }
    }

    bool UltraCanvasLinuxApplication::InitializeX11() {
        // Initialize X11 threading support
        if (!XInitThreads()) {
            debugOutput << "UltraCanvas: XInitThreads() failed" << std::endl;
            return false;
        }

        // CRITICAL: Set locale for proper UTF-8 handling BEFORE opening display
        // This is essential for XIM to work correctly with international characters
        if (setlocale(LC_ALL, "") == nullptr) {
            debugOutput << "UltraCanvas: Warning - setlocale() failed, UTF-8 input may not work" << std::endl;
        }

        // Check if X supports the current locale
        if (!XSupportsLocale()) {
            debugOutput << "UltraCanvas: Warning - X does not support current locale" << std::endl;
        }

        // Set X locale modifiers (required for XIM)
        if (XSetLocaleModifiers("") == nullptr) {
            debugOutput << "UltraCanvas: Warning - XSetLocaleModifiers() failed" << std::endl;
        }

        // Connect to X server
        display = XOpenDisplay(nullptr);
        if (!display) {
            debugOutput << "UltraCanvas: Cannot connect to X server" << std::endl;
            return false;
        }

        // A held key: one KeyPress per repeat and one KeyRelease when it is
        // let go, as on Windows and macOS. X11's default sends a release
        // before every repeat, so a key-up handler ran once per repeat.
        Bool autoRepeatSupported = False;
        XkbSetDetectableAutoRepeat(display, True, &autoRepeatSupported);
        detectableAutoRepeat = autoRepeatSupported == True;

        // Get display information
        screen = DefaultScreen(display);
        rootWindow = RootWindow(display, screen);
        visual = DefaultVisual(display, screen);
        colormap = DefaultColormap(display, screen);
        depth = DefaultDepth(display, screen);

        // Validate critical resources
        if (rootWindow == 0) {
            debugOutput << "UltraCanvas: Invalid root window" << std::endl;
            XCloseDisplay(display);
            display = nullptr;
            return false;
        }

        if (!visual) {
            debugOutput << "UltraCanvas: Invalid visual" << std::endl;
            XCloseDisplay(display);
            display = nullptr;
            return false;
        }

        debugOutput << "X11 Display: " << DisplayString(display) << std::endl;
        debugOutput << "Screen: " << screen << ", Depth: " << depth << std::endl;

        // Set up error handling
        XSetErrorHandler(XErrorHandler);
        XSetIOErrorHandler(XIOErrorHandler);

        return true;
    }

    bool UltraCanvasLinuxApplication::InitializeGLX() {
        glxSupported = false;
        return true;
    }

    void UltraCanvasLinuxApplication::InitializeAtoms() {
        wmDeleteWindow = XInternAtom(display, "WM_DELETE_WINDOW", False);
    }

// ===== XIM (X INPUT METHOD) SUPPORT FOR UTF-8 =====
    bool UltraCanvasLinuxApplication::InitializeXIM() {
        if (!display) {
            debugOutput << "UltraCanvas: Cannot initialize XIM - no display" << std::endl;
            return false;
        }

        // Open the X Input Method
        xim = XOpenIM(display, nullptr, nullptr, nullptr);
        if (!xim) {
            debugOutput << "UltraCanvas: XOpenIM() failed - trying with @im=none" << std::endl;
            
            // Try with explicit fallback
            XSetLocaleModifiers("@im=none");
            xim = XOpenIM(display, nullptr, nullptr, nullptr);
            
            if (!xim) {
                debugOutput << "UltraCanvas: XOpenIM() failed completely" << std::endl;
                return false;
            }
        }

        debugOutput << "UltraCanvas: XIM initialized successfully" << std::endl;
        return true;
    }

    void UltraCanvasLinuxApplication::ShutdownXIM() {
        // Note: XIC contexts should be destroyed by their respective windows
        // before XIM is closed

        if (xim) {
            XCloseIM(xim);
            xim = nullptr;
            debugOutput << "UltraCanvas: XIM closed" << std::endl;
        }
    }

// ===== MAIN LOOP =====

    void UltraCanvasLinuxApplication::CollectAndProcessNativeEvents() {
        // 1. Drain all pending X11 events (non-blocking)
        while (XPending(display) > 0) {
            XEvent xEvent;
            XNextEvent(display, &xEvent);
            if (xim && XFilterEvent(&xEvent, None)) {
                continue;
            }
            ProcessXEvent(xEvent);
        }

        // 2. Compute wait timeout from timer system
        auto timeout = GetTimeUntilNextTimer();

        // 3. Build select() fd_set with X11 fd, wakeup fd, and any external fd
        //    watches (AddFdWatch) — e.g. a host integration's IPC sockets.
        fd_set readfds, writefds;
        FD_ZERO(&readfds);
        FD_ZERO(&writefds);
        int x11Fd = ConnectionNumber(display);
        FD_SET(x11Fd, &readfds);
        int maxFd = x11Fd;
        if (wakeupFd >= 0) {
            FD_SET(wakeupFd, &readfds);
            if (wakeupFd > maxFd) maxFd = wakeupFd;
        }

        auto fdWatchKeys = SnapshotFdWatchKeys();
        for (const auto& key : fdWatchKeys) {
            if (key.fd < 0) continue;
            FD_SET(key.fd, key.type == FdWatchType::Write ? &writefds : &readfds);
            if (key.fd > maxFd) maxFd = key.fd;
        }

        // 4. Set up timeout (nullptr = infinite wait)
        struct timeval tv;
        struct timeval* tvPtr = nullptr;
        if (timeout != std::chrono::milliseconds::max()) {
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(timeout);
            tv.tv_sec = static_cast<long>(us.count() / 1'000'000);
            tv.tv_usec = static_cast<long>(us.count() % 1'000'000);
            tvPtr = &tv;
        }

        // 5. Wait for OS events, wakeup signal, watched fds, or timer expiry
        int result = select(maxFd + 1, &readfds, &writefds, nullptr, tvPtr);

        if (result < 0 && errno != EINTR) {
            debugOutput << "CollectAndProcessNativeEvents: select() error" << std::endl;
        }

        // 6. Drain wakeup fd if signaled
        if (result > 0 && wakeupFd >= 0 && FD_ISSET(wakeupFd, &readfds)) {
            uint64_t val;
            (void)read(wakeupFd, &val, sizeof(val));
        }

        // 6b. Fire any external fd watches that became ready. Done before draining
        //     X11 so IPC replies are processed promptly. FireFdWatch() invokes the
        //     callback unlocked, so a handler may add/remove watches re-entrantly.
        if (result > 0) {
            for (const auto& key : fdWatchKeys) {
                if (key.fd < 0) continue;
                fd_set* set = key.type == FdWatchType::Write ? &writefds : &readfds;
                if (FD_ISSET(key.fd, set))
                    FireFdWatch(key.id);
            }
        }

        // 7. Drain any new X11 events that arrived during wait
        while (XPending(display) > 0) {
            XEvent xEvent;
            XNextEvent(display, &xEvent);
            if (xim && XFilterEvent(&xEvent, None)) {
                continue;
            }
            ProcessXEvent(xEvent);
        }
    }

    // ===== WAKEUP MECHANISM =====
    void UltraCanvasLinuxApplication::InitializeWakeUp() {
        wakeupFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (wakeupFd < 0) {
            debugOutput << "UltraCanvas: Failed to create eventfd for wakeup" << std::endl;
        }
    }

    void UltraCanvasLinuxApplication::ShutdownWakeUp() {
        if (wakeupFd >= 0) {
            close(wakeupFd);
            wakeupFd = -1;
        }
    }

    void UltraCanvasLinuxApplication::WakeUpEventLoop() {
        if (wakeupFd >= 0) {
            uint64_t val = 1;
            (void)write(wakeupFd, &val, sizeof(val));
        }
    }


    // ===== MOUSE CAPTURE SUPPORT =====
    void UltraCanvasLinuxApplication::CaptureMouseNative() {
        if (!display || !GetFocusedWindow()) {
            debugOutput << "UltraCanvas: Cannot capture mouse - no display or focused window" << std::endl;
            return;
        }

        auto* linuxWindow = dynamic_cast<UltraCanvasLinuxWindow*>(GetFocusedWindow());
        if (!linuxWindow) {
            debugOutput << "UltraCanvas: Cannot capture mouse - invalid window type" << std::endl;
            return;
        }

        Window xWindow = (Window) linuxWindow->GetNativeHandle();
        if (xWindow == 0) {
            debugOutput << "UltraCanvas: Cannot capture mouse - invalid X11 window handle" << std::endl;
            return;
        }

        int grabResult = XGrabPointer(
                display,
                xWindow,
                True,                                    // owner_events
                ButtonPressMask | ButtonReleaseMask | PointerMotionMask | EnterWindowMask | LeaveWindowMask,
                GrabModeAsync,                           // pointer_mode
                GrabModeAsync,                           // keyboard_mode
                None,                                    // confine_to (no confinement)
                None,                                    // cursor (use current)
                CurrentTime
        );

        if (grabResult != GrabSuccess) {
            debugOutput << "UltraCanvas: XGrabPointer failed with code: " << grabResult << std::endl;
        } else {
            debugOutput << "UltraCanvas: Mouse captured successfully" << std::endl;
        }
    }

    void UltraCanvasLinuxApplication::ReleaseMouseNative() {
        if (!display) {
            return;
        }

        XUngrabPointer(display, CurrentTime);
        XFlush(display);
        debugOutput << "UltraCanvas: Mouse released" << std::endl;
    }

// ===== EVENT PROCESSING =====

    bool UltraCanvasLinuxApplication::IsAutoRepeatRelease(const XEvent& xEvent) {
        if (detectableAutoRepeat) return false;
        if (XEventsQueued(display, QueuedAfterReading) == 0) return false;
        XEvent next;
        XPeekEvent(display, &next);
        return next.type == KeyPress && next.xkey.window == xEvent.xkey.window &&
               next.xkey.keycode == xEvent.xkey.keycode && next.xkey.time == xEvent.xkey.time;
    }

    void UltraCanvasLinuxApplication::ProcessXEvent(XEvent& xEvent) {
        // The release half of an auto-repeat is no release at all.
        if (xEvent.type == KeyRelease && IsAutoRepeatRelease(xEvent)) return;
        // Find the window that owns this event
        if (xEvent.type == SelectionRequest || xEvent.type == SelectionNotify || xEvent.type == SelectionClear) {
            // An active XDnD source drag owns XdndSelection: it answers the
            // target's data requests (SelectionRequest) and reacts to losing
            // the selection (SelectionClear).
            if (UltraCanvasLinuxDragDrop::HandleSourceSelectionEvent(xEvent)) {
                return;
            }
            // For SelectionNotify, let the window's drag-drop handler try first
            // (XDnD drop uses SelectionNotify to deliver file data)
            if (xEvent.type == SelectionNotify) {
                auto window = static_cast<UltraCanvasLinuxWindow*>(FindWindow(xEvent.xany.window));
                if (window && window->HandleXEvent(xEvent)) {
                    return;
                }
            }
            UltraCanvasLinuxClipboard::ProcessClipboardEvent(xEvent);
        } else if (xEvent.type == PropertyNotify &&
                   UltraCanvasLinuxClipboard::ProcessClipboardPropertyEvent(xEvent)) {
            // A piece of a clipboard copy sent or received in pieces (INCR)
            return;
        } else {
            auto window = static_cast<UltraCanvasLinuxWindow*>(FindWindow(xEvent.xany.window));

            if (window) {
                // Let the window handle the X11 event first
                if (window->HandleXEvent(xEvent)) {
                    return;
                }
            }

            // Convert to UCEvent and queue it
            UCEvent ucEvent = ConvertXEventToUCEvent(xEvent);

            if (ucEvent.type != UCEventType::Unknown) {
                PushEvent(ucEvent);
            }
        }
    }

    UCEvent UltraCanvasLinuxApplication::ConvertXEventToUCEvent(const XEvent& xEvent) {
        UCEvent event;
        event.timestamp = std::chrono::steady_clock::now();

        // Store the X11 window handle that generated this event
        event.nativeWindowHandle = xEvent.xany.window;

        // Find and store the corresponding UltraCanvas window
        auto targetWindow = static_cast<UltraCanvasLinuxWindow*>(FindWindow(xEvent.xany.window));
        if (targetWindow) {
            event.targetWindow = targetWindow->GetWindowWeakPtr();
        }

        switch (xEvent.type) {
            case KeyPress:
            case KeyRelease: {
                event.type = (xEvent.type == KeyPress) ?
                             UCEventType::KeyDown : UCEventType::KeyUp;
                event.nativeKeyCode = xEvent.xkey.keycode;
                event.virtualKey = ConvertXKeyToUCKey(XLookupKeysym(const_cast<XKeyEvent*>(&xEvent.xkey), 0));

                // Get character representation with proper UTF-8 support
                char buffer[64] = {0};
                KeySym keysym;
                Status status;
                int len = 0;

                // Get the XIC from the target window for proper UTF-8 lookup
                XIC xic = nullptr;
                if (targetWindow) {
                    xic = targetWindow->GetXIC();
                }

                if (xic && xEvent.type == KeyPress) {
                    // Use Xutf8LookupString for proper UTF-8/international character support
                    len = Xutf8LookupString(xic, 
                                            const_cast<XKeyEvent*>(&xEvent.xkey),
                                            buffer, sizeof(buffer) - 1,
                                            &keysym, &status);
                    
                    if (status == XBufferOverflow) {
                        // Buffer too small - rare for single characters, but handle it
                        debugOutput << "UltraCanvas: Xutf8LookupString buffer overflow" << std::endl;
                        len = 0;
                    } else if (status == XLookupNone) {
                        len = 0;
                    } else if (status == XLookupKeySym) {
                        // Only keysym returned, no character
                        len = 0;
                    }
                    // XLookupChars or XLookupBoth - we have characters
                }

                // Fallback to XLookupString if XIM is not available or for KeyRelease.
                // It answers in Latin-1, not UTF-8 (that is what the X11 API
                // specifies), so anything above ASCII has to be re-encoded
                // below - handing those bytes on as they are would put half a
                // character into every widget that received them.
                bool latin1 = false;
                if (len == 0 && xEvent.type == KeyPress) {
                    len = XLookupString(const_cast<XKeyEvent*>(&xEvent.xkey), 
                                       buffer, sizeof(buffer) - 1, 
                                       &keysym, nullptr);
                    latin1 = (len > 0);
                }

                if (len > 0) {
                    buffer[len] = '\0';  // Ensure null termination

                    std::string typed(buffer, len);
                    if (latin1) {
                        // Latin-1 -> UTF-8: each byte is its own codepoint.
                        std::string utf8;
                        utf8.reserve(typed.size() * 2);
                        for (unsigned char c : typed) {
                            if (c < 0x80) {
                                utf8.push_back(static_cast<char>(c));
                            } else {
                                utf8.push_back(static_cast<char>(0xC0 | (c >> 6)));
                                utf8.push_back(static_cast<char>(0x80 | (c & 0x3F)));
                            }
                        }
                        typed.swap(utf8);
                    }

                    // For single-byte ASCII, set character field
                    if (typed.size() == 1 && (unsigned char)typed[0] < 128) {
                        event.character = typed[0];
                    } else {
                        // For multi-byte UTF-8, character field is less meaningful
                        // but we can set it to the first byte or 0
                        event.character = 0;  // Indicate multi-byte sequence
                    }

                    // Always set the full UTF-8 text
                    event.text = std::move(typed);
                } else {
                    event.character = 0;
                    event.text.clear();
                }

                // Modifier keys
                event.shift = (xEvent.xkey.state & ShiftMask) != 0;
                event.ctrl = (xEvent.xkey.state & ControlMask) != 0;
                event.alt = (xEvent.xkey.state & Mod1Mask) != 0;
                event.meta = (xEvent.xkey.state & Mod4Mask) != 0;
                break;
            }

            case ButtonPress:
            case ButtonRelease: {
                // ===== FIXED X11 WHEEL EVENTS MAPPING =====
                unsigned int xButton = xEvent.xbutton.button;

                event.pointerWindow = { xEvent.xbutton.x, xEvent.xbutton.y };
                event.pointer = event.pointerWindow;
                event.pointerGlobal = { xEvent.xbutton.x_root, xEvent.xbutton.y_root };
                event.shift = (xEvent.xbutton.state & ShiftMask) != 0;
                event.ctrl = (xEvent.xbutton.state & ControlMask) != 0;
                event.alt = (xEvent.xbutton.state & Mod1Mask) != 0;
                event.meta = (xEvent.xbutton.state & Mod4Mask) != 0;
                event.button = ConvertXButtonToUCButton(xButton);

                // Handle wheel events (Button4 = WheelUp, Button5 = WheelDown)
                if (xButton == Button4 || xButton == Button5) {
                    if (xEvent.type == ButtonPress) {
                        event.type = UCEventType::MouseWheel;
                        // One wheel notch = ±1; consumers decide how many
                        // lines/pixels a notch scrolls.
                        event.wheelDelta = (xButton == Button4) ? 1 : -1;
                        // Set modifier keys
                    } else {
                        event.type = UCEventType::Unknown;
                    }
                }
                    // Handle horizontal wheel events
                else if (xButton == 6 || xButton == 7) {
                    if (xEvent.type == ButtonPress) {
                        event.type = UCEventType::MouseWheelHorizontal;
                        event.wheelDelta = (xButton == 7) ? 1 : -1;
                    } else {
                        event.type = UCEventType::Unknown;
                    }
                }
                    // Handle regular mouse button events
                else {
// Check for double-click on ButtonPress
                    bool isDoubleClick = false;

                    if (xEvent.type == ButtonPress) {
                        // Calculate time difference
                        Time timeDiff = xEvent.xbutton.time - mouseClickInfo.lastClickTime;

                        // Calculate position difference
                        int xDiff = abs(xEvent.xbutton.x - mouseClickInfo.lastClickX);
                        int yDiff = abs(xEvent.xbutton.y - mouseClickInfo.lastClickY);

                        // Check if this qualifies as a double-click
                        if (mouseClickInfo.window == xEvent.xbutton.window &&
                            mouseClickInfo.lastButton == xButton &&
                            timeDiff <= mouseClickInfo.doubleClickTime &&
                            xDiff <= mouseClickInfo.doubleClickDistance &&
                            yDiff <= mouseClickInfo.doubleClickDistance) {

                            isDoubleClick = true;

                            // Reset tracker to prevent triple-click detection
                            mouseClickInfo.lastClickTime = 0;
                            mouseClickInfo.window = 0;
                        } else {
                            // Update tracker for next potential double-click
                            mouseClickInfo.window = xEvent.xbutton.window;
                            mouseClickInfo.lastClickTime = xEvent.xbutton.time;
                            mouseClickInfo.lastClickX = xEvent.xbutton.x;
                            mouseClickInfo.lastClickY = xEvent.xbutton.y;
                            mouseClickInfo.lastButton = xButton;
                        }
                    }

                    // Set event type
                    if (isDoubleClick) {
                        event.type = UCEventType::MouseDoubleClick;
                    } else {
                        event.type = (xEvent.type == ButtonPress) ?
                                     UCEventType::MouseDown : UCEventType::MouseUp;
                    }
                }
                break;
            }

            case MotionNotify: {
                event.type = UCEventType::MouseMove;
                event.pointerWindow = { xEvent.xmotion.x, xEvent.xmotion.y };
                event.pointer = event.pointerWindow;
                event.pointerGlobal = { xEvent.xmotion.x_root, xEvent.xmotion.y_root };
                event.shift = (xEvent.xmotion.state & ShiftMask) != 0;
                event.ctrl = (xEvent.xmotion.state & ControlMask) != 0;
                event.alt = (xEvent.xmotion.state & Mod1Mask) != 0;
                event.meta = (xEvent.xmotion.state & Mod4Mask) != 0;
                break;
            }

            case ConfigureNotify: {
                event.type = UCEventType::WindowResize;
                event.width = xEvent.xconfigure.width;    // PHYSICAL px
                event.height = xEvent.xconfigure.height;
                event.pointerWindow = { xEvent.xconfigure.x, xEvent.xconfigure.y };
                event.pointer = event.pointerWindow;
                // The window may have moved to a display with a different DPI.
                // Re-query the per-monitor scale; if it changed, rebuild the
                // surface/context at the new scale before the resize propagates.
                if (targetWindow && targetWindow->RefreshDeviceScale()) {
                    targetWindow->HandleDeviceScaleChange();
                    // The window keeps the cursor defined on it, and a cursor
                    // drawn from a picture was drawn for the old scaling: put
                    // up the one for this screen.
                    SelectMouseCursorNative(targetWindow, targetWindow->GetCurrentMouseCursor());
                }
                break;
            }

            case Expose: {
                event.type = UCEventType::WindowRepaint;
                if (xEvent.xexpose.count == 0) {
                    event.pointerWindow = { xEvent.xexpose.x, xEvent.xexpose.y };
                    event.pointer = event.pointerWindow;
                    event.width = xEvent.xexpose.width;
                    event.height = xEvent.xexpose.height;
                } else {
                    event.type = UCEventType::Unknown;
                }
                break;
            }

            case ClientMessage: {
                if (xEvent.xclient.data.l[0] == static_cast<long>(wmDeleteWindow)) {
                    event.type = UCEventType::WindowCloseRequest;
                } else {
                    event.type = UCEventType::Unknown;
                }
                break;
            }

            case MapNotify: {
                event.type = UCEventType::WindowRepaint;
                break;
            }

            case PropertyNotify: {
                // ICCCM: the window manager records iconification in the
                // WM_STATE property (IconicState / NormalState). Watching that
                // property, rather than UnmapNotify, is what tells a
                // user-initiated minimise apart from Hide(), which also unmaps.
                // Only a change *into* the iconic state, or *out of it* while
                // we believe the window is minimized, becomes an event, so the
                // initial NormalState written at first map is not a "restore".
                event.type = UCEventType::Unknown;
                static Atom wmStateAtom = XInternAtom(GetDisplay(), "WM_STATE", False);
                if (targetWindow && xEvent.xproperty.atom == wmStateAtom &&
                    xEvent.xproperty.state == PropertyNewValue) {
                    Atom actualType = None;
                    int actualFormat = 0;
                    unsigned long items = 0, remaining = 0;
                    unsigned char* data = nullptr;
                    if (XGetWindowProperty(GetDisplay(), xEvent.xproperty.window,
                                           wmStateAtom, 0, 2, False, wmStateAtom,
                                           &actualType, &actualFormat, &items,
                                           &remaining, &data) == Success && data) {
                        if (actualType == wmStateAtom && actualFormat == 32 && items >= 1) {
                            const long wmState = reinterpret_cast<long*>(data)[0];
                            const bool minimizedNow = targetWindow->IsMinimized();
                            if (wmState == IconicState) {
                                event.type = UCEventType::WindowMinimize;
                            } else if (wmState == NormalState && minimizedNow) {
                                event.type = UCEventType::WindowRestore;
                            }
                        }
                        XFree(data);
                    }
                }
                break;
            }

            case FocusIn: {
                // A keyboard grab starting or ending (a global shortcut held
                // down, a window manager's key binding) moves the focus away
                // and back for its own duration; the window keeps it. A real
                // change during a grab arrives as NotifyWhileGrabbed.
                if (xEvent.xfocus.mode == NotifyGrab || xEvent.xfocus.mode == NotifyUngrab) {
                    event.type = UCEventType::Unknown;
                    break;
                }
                event.type = UCEventType::WindowFocus;
                
                // Set XIC focus when window gains focus
                if (targetWindow) {
                    XIC xic = targetWindow->GetXIC();
                    if (xic) {
                        XSetICFocus(xic);
                    }
                }
                break;
            }

            case FocusOut: {
                // See FocusIn: the grab's own focus changes are not a blur.
                // A popup that closes when its window loses the focus (the
                // desktop's clipboard panel) shut whenever Super+V was
                // pressed over it.
                if (xEvent.xfocus.mode == NotifyGrab || xEvent.xfocus.mode == NotifyUngrab) {
                    event.type = UCEventType::Unknown;
                    break;
                }
                event.type = UCEventType::WindowBlur;
                
                // Unset XIC focus when window loses focus
                if (targetWindow) targetWindow->UnfocusXICs();
                break;
            }

            case EnterNotify: {
                event.type = UCEventType::MouseEnter;
                event.pointerWindow = { xEvent.xcrossing.x, xEvent.xcrossing.y };
                event.pointer = event.pointerWindow;
                event.pointerGlobal = { xEvent.xcrossing.x_root, xEvent.xcrossing.y_root };
                break;
            }

            case LeaveNotify: {
                event.type = UCEventType::MouseLeave;
                event.pointerWindow = { xEvent.xcrossing.x, xEvent.xcrossing.y };
                event.pointer = event.pointerWindow;
                event.pointerGlobal = { xEvent.xcrossing.x_root, xEvent.xcrossing.y_root };
                break;
            }

            default:
                event.type = UCEventType::Unknown;
                break;
        }

        // ===== HiDPI: PHYSICAL px -> LOGICAL =====
        // X11 always reports coordinates in physical device px. Convert to the
        // target window's logical space (divide by deviceScale) so the framework
        // hit-tests and lays out in device-independent units. macOS/Windows do
        // the equivalent conversion in their own event layers. Non-pointer events
        // carry a {0,0} pointer, so the conversion is a harmless no-op there.
        if (targetWindow) {
            float s = targetWindow->GetDeviceScale();
            if (s != 1.0f) {
                event.pointerWindow = targetWindow->PhysicalToLogical(event.pointerWindow);
                event.pointer       = event.pointerWindow;
                event.pointerGlobal = targetWindow->PhysicalToLogical(event.pointerGlobal);
                if (event.type == UCEventType::WindowResize ||
                    event.type == UCEventType::WindowRepaint) {
                    event.width  = targetWindow->PhysicalToLogical(event.width);
                    event.height = targetWindow->PhysicalToLogical(event.height);
                }
            }
        }

        return event;
    }

// ===== KEY AND MOUSE CONVERSION =====
    UCKeys UltraCanvasLinuxApplication::ConvertXKeyToUCKey(KeySym keysym) {
        // The table is shared with the Wayland backend (UltraCanvasLinuxKeys.cpp).
        return UCKeyFromKeysym(static_cast<unsigned long>(keysym));
    }

    UCMouseButton UltraCanvasLinuxApplication::ConvertXButtonToUCButton(unsigned int button) {
        switch (button) {
            case Button1: return UCMouseButton::Left;
            case Button2: return UCMouseButton::Middle;
            case Button3: return UCMouseButton::Right;
            case Button4: return UCMouseButton::WheelUp;
            case Button5: return UCMouseButton::WheelDown;
            case 6: return UCMouseButton::WheelLeft;
            case 7: return UCMouseButton::WheelRight;
            case 8: return UCMouseButton::Back;
            case 9: return UCMouseButton::Forward;
            default: return UCMouseButton::Unknown;
        }
    }

// ===== EVENT THREAD MANAGEMENT =====
    void UltraCanvasLinuxApplication::StartEventThread() {
        if (eventThreadRunning) {
            return;
        }

        debugOutput << "UltraCanvas: Starting event processing thread..." << std::endl;

        eventThreadRunning = true;
        eventThread = std::thread([this]() {
            EventThreadFunction();
        });

        debugOutput << "UltraCanvas: Event thread started successfully" << std::endl;
    }

    void UltraCanvasLinuxApplication::StopEventThread() {
        if (!eventThreadRunning) {
            return;
        }

        debugOutput << "UltraCanvas: Stopping event thread..." << std::endl;

        eventThreadRunning = false;
        eventCondition.notify_all();

        if (eventThread.joinable()) {
            try {
                eventThread.join();
                debugOutput << "UltraCanvas: Event thread stopped successfully" << std::endl;
            } catch (const std::exception& e) {
                debugOutput << "UltraCanvas: Exception stopping event thread: " << e.what() << std::endl;
            }
        }
    }

    void UltraCanvasLinuxApplication::EventThreadFunction() {
        debugOutput << "UltraCanvas: Event thread running..." << std::endl;

        while (eventThreadRunning && display) {
            try {
                if (XPending(display) > 0) {
                    XEvent xEvent;
                    XNextEvent(display, &xEvent);
                    
                    // Let XIM filter events first
                    if (xim && XFilterEvent(&xEvent, None)) {
                        continue;
                    }
                    
                    ProcessXEvent(xEvent);
                } else {
                    // Wait for events with timeout
                    struct timeval timeout;
                    timeout.tv_sec = 0;
                    timeout.tv_usec = 16666; // ~60 FPS

                    fd_set readfds;
                    FD_ZERO(&readfds);
                    FD_SET(ConnectionNumber(display), &readfds);

                    int result = select(ConnectionNumber(display) + 1, &readfds, nullptr, nullptr, &timeout);

                    if (result < 0 && errno != EINTR) {
                        debugOutput << "UltraCanvas: select() error in event thread" << std::endl;
                        break;
                    }
                }
            } catch (const std::exception& e) {
                debugOutput << "UltraCanvas: Exception in event thread: " << e.what() << std::endl;
                break;
            }
        }

        debugOutput << "UltraCanvas: Event thread ended" << std::endl;
    }

// ===== CLIPBOARD SUPPORT =====
    std::string UltraCanvasLinuxApplication::GetClipboardText() {
        // Simplified clipboard implementation
        return "";
    }

    void UltraCanvasLinuxApplication::SetClipboardText(const std::string& text) {
        // Simplified clipboard implementation
    }

// ===== ERROR HANDLING =====
    int UltraCanvasLinuxApplication::XErrorHandler(Display* display, XErrorEvent* event) {
        char errorText[256];
        XGetErrorText(display, event->error_code, errorText, sizeof(errorText));
        debugOutput << "X11 Error: " << errorText
                  << " (code: " << (int)event->error_code << ")" << std::endl;
        return 0;
    }

    int UltraCanvasLinuxApplication::XIOErrorHandler(Display* display) {
        debugOutput << "X11 IO Error: Connection to X server lost" << std::endl;
        exit(1);
        return 0;
    }

    void UltraCanvasLinuxApplication::LogXError(const std::string& context, int errorCode) {
        debugOutput << "UltraCanvas X11 Error in " << context << ": code " << errorCode << std::endl;
    }

    // The fonts are fontconfig's and Pango's, not the display's: shared with
    // the Wayland backend (UltraCanvasLinuxFonts.cpp).
    FontStyle UltraCanvasLinuxApplication::DetectSystemFontStyleNative() { return LinuxFonts::DetectSystemFontStyle(); }
    FontStyle UltraCanvasLinuxApplication::DetectMonospacedFontStyleNative() { return LinuxFonts::DetectMonospacedFontStyle(); }
    void UltraCanvasLinuxApplication::LoadBundledFontsNative() { LinuxFonts::LoadBundledFonts(); }
    bool UltraCanvasLinuxApplication::RegisterFontFileNative(const std::string& fontFilePath) {
        return LinuxFonts::RegisterFontFile(fontFilePath);
    }

} // namespace UltraCanvas