// OS/Linux/UltraCanvasLinuxApplication.cpp
// Complete Linux application implementation with all methods
// Version: 1.4.0 - Added XIM support for UTF-8/international character input
// Last Modified: 2025-01-30
// Author: UltraCanvas Framework

#include "UltraCanvasWindow.h"
#include "UltraCanvasLinuxClipboard.h"
#include "UltraCanvasApplication.h"
#include <iostream>
#include <algorithm>
#include <sys/select.h>
#include <sys/eventfd.h>
#include <unistd.h>
#include <PixelFX/PixelFX.h>
#include <errno.h>
#include <clocale>  // For setlocale
#include "UltraCanvasDebug.h"

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
        // Clean up wakeup mechanism
        ShutdownWakeUp();

        // Clean up XIM
        ShutdownXIM();

        for (auto cursor : cursors) {
            XFreeCursor(display, cursor.second);
        }

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

        // 3. Build select() fd_set with X11 fd and wakeup fd
        fd_set readfds;
        FD_ZERO(&readfds);
        int x11Fd = ConnectionNumber(display);
        FD_SET(x11Fd, &readfds);
        int maxFd = x11Fd;
        if (wakeupFd >= 0) {
            FD_SET(wakeupFd, &readfds);
            if (wakeupFd > maxFd) maxFd = wakeupFd;
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

        // 5. Wait for OS events, wakeup signal, or timer expiry
        int result = select(maxFd + 1, &readfds, nullptr, nullptr, tvPtr);

        if (result < 0 && errno != EINTR) {
            debugOutput << "CollectAndProcessNativeEvents: select() error" << std::endl;
        }

        // 6. Drain wakeup fd if signaled
        if (result > 0 && wakeupFd >= 0 && FD_ISSET(wakeupFd, &readfds)) {
            uint64_t val;
            (void)read(wakeupFd, &val, sizeof(val));
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
        if (!display || !focusedWindow) {
            debugOutput << "UltraCanvas: Cannot capture mouse - no display or focused window" << std::endl;
            return;
        }

        auto* linuxWindow = dynamic_cast<UltraCanvasLinuxWindow*>(focusedWindow);
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

    void UltraCanvasLinuxApplication::ProcessXEvent(XEvent& xEvent) {
        // Find the window that owns this event
        if (xEvent.type == SelectionRequest || xEvent.type == SelectionNotify || xEvent.type == SelectionClear) {
            // For SelectionNotify, let the window's drag-drop handler try first
            // (XDnD drop uses SelectionNotify to deliver file data)
            if (xEvent.type == SelectionNotify) {
                auto window = static_cast<UltraCanvasLinuxWindow*>(FindWindow(xEvent.xany.window));
                if (window && window->HandleXEvent(xEvent)) {
                    return;
                }
            }
            UltraCanvasLinuxClipboard::ProcessClipboardEvent(xEvent);
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
        event.targetWindow = targetWindow;

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

                // Fallback to XLookupString if XIM is not available or for KeyRelease
                if (len == 0 && xEvent.type == KeyPress) {
                    len = XLookupString(const_cast<XKeyEvent*>(&xEvent.xkey), 
                                       buffer, sizeof(buffer) - 1, 
                                       &keysym, nullptr);
                }

                if (len > 0) {
                    buffer[len] = '\0';  // Ensure null termination
                    
                    // For single-byte ASCII, set character field
                    if (len == 1 && (unsigned char)buffer[0] < 128) {
                        event.character = buffer[0];
                    } else {
                        // For multi-byte UTF-8, character field is less meaningful
                        // but we can set it to the first byte or 0
                        event.character = 0;  // Indicate multi-byte sequence
                    }
                    
                    // Always set the full UTF-8 text
                    event.text = std::string(buffer, len);
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

                event.x = event.windowX = xEvent.xbutton.x;
                event.y = event.windowY = xEvent.xbutton.y;
                event.globalX = xEvent.xbutton.x_root;
                event.globalY = xEvent.xbutton.y_root;
                event.shift = (xEvent.xbutton.state & ShiftMask) != 0;
                event.ctrl = (xEvent.xbutton.state & ControlMask) != 0;
                event.alt = (xEvent.xbutton.state & Mod1Mask) != 0;
                event.meta = (xEvent.xbutton.state & Mod4Mask) != 0;
                event.button = ConvertXButtonToUCButton(xButton);

                // Handle wheel events (Button4 = WheelUp, Button5 = WheelDown)
                if (xButton == Button4 || xButton == Button5) {
                    if (xEvent.type == ButtonPress) {
                        event.type = UCEventType::MouseWheel;
                        event.wheelDelta = (xButton == Button4) ? 5 : -5;
                        // Set modifier keys
                    } else {
                        event.type = UCEventType::Unknown;
                    }
                }
                    // Handle horizontal wheel events
                else if (xButton == 6 || xButton == 7) {
                    if (xEvent.type == ButtonPress) {
                        event.type = UCEventType::MouseWheelHorizontal;
                        event.wheelDelta = (xButton == 7) ? 5 : -5;
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
                event.x = event.windowX = xEvent.xmotion.x;
                event.y = event.windowY = xEvent.xmotion.y;
                event.globalX = xEvent.xmotion.x_root;
                event.globalY = xEvent.xmotion.y_root;
                event.shift = (xEvent.xmotion.state & ShiftMask) != 0;
                event.ctrl = (xEvent.xmotion.state & ControlMask) != 0;
                event.alt = (xEvent.xmotion.state & Mod1Mask) != 0;
                event.meta = (xEvent.xmotion.state & Mod4Mask) != 0;
                break;
            }

            case ConfigureNotify: {
                event.type = UCEventType::WindowResize;
                event.width = xEvent.xconfigure.width;
                event.height = xEvent.xconfigure.height;
                event.x = event.windowX = xEvent.xconfigure.x;
                event.y = event.windowY = xEvent.xconfigure.y;
                break;
            }

            case Expose: {
                event.type = UCEventType::WindowRepaint;
                if (xEvent.xexpose.count == 0) {
                    event.x = event.windowX = xEvent.xexpose.x;
                    event.y = event.windowY = xEvent.xexpose.y;
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

            case FocusIn: {
                debugOutput << "focus xwindow=" << xEvent.xany.window << std::endl;
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
                debugOutput << "blur xwindow=" << xEvent.xany.window << std::endl;
                event.type = UCEventType::WindowBlur;
                
                // Unset XIC focus when window loses focus
                if (targetWindow) {
                    XIC xic = targetWindow->GetXIC();
                    if (xic) {
                        XUnsetICFocus(xic);
                    }
                }
                break;
            }

            case EnterNotify: {
                event.type = UCEventType::MouseEnter;
                event.x = event.windowX = xEvent.xcrossing.x;
                event.y = event.windowY = xEvent.xcrossing.y;
                event.globalX = xEvent.xcrossing.x_root;
                event.globalY = xEvent.xcrossing.y_root;
                break;
            }

            case LeaveNotify: {
                event.type = UCEventType::MouseLeave;
                event.x = event.windowX = xEvent.xcrossing.x;
                event.y = event.windowY = xEvent.xcrossing.y;
                event.globalX = xEvent.xcrossing.x_root;
                event.globalY = xEvent.xcrossing.y_root;
                break;
            }

            default:
                event.type = UCEventType::Unknown;
                break;
        }

        return event;
    }

// ===== KEY AND MOUSE CONVERSION =====
    UCKeys UltraCanvasLinuxApplication::ConvertXKeyToUCKey(KeySym keysym) {
        switch (keysym) {
            // Special keys
            case XK_Return: return UCKeys::Enter;
            case XK_Escape: return UCKeys::Escape;
            case XK_space: return UCKeys::Space;
            case XK_BackSpace: return UCKeys::Backspace;
            case XK_Tab: return UCKeys::Tab;
            case XK_Delete: return UCKeys::Delete;
            case XK_Insert: return UCKeys::Insert;

                // Arrow keys
            case XK_Left: return UCKeys::Left;
            case XK_Right: return UCKeys::Right;
            case XK_Up: return UCKeys::Up;
            case XK_Down: return UCKeys::Down;

                // Navigation
            case XK_Home: return UCKeys::Home;
            case XK_End: return UCKeys::End;
            case XK_Page_Up: return UCKeys::PageUp;
            case XK_Page_Down: return UCKeys::PageDown;

                // Function keys
            case XK_F1: return UCKeys::F1;
            case XK_F2: return UCKeys::F2;
            case XK_F3: return UCKeys::F3;
            case XK_F4: return UCKeys::F4;
            case XK_F5: return UCKeys::F5;
            case XK_F6: return UCKeys::F6;
            case XK_F7: return UCKeys::F7;
            case XK_F8: return UCKeys::F8;
            case XK_F9: return UCKeys::F9;
            case XK_F10: return UCKeys::F10;
            case XK_F11: return UCKeys::F11;
            case XK_F12: return UCKeys::F12;

                // Modifier keys
            case XK_Shift_L: return UCKeys::LeftShift;
            case XK_Shift_R: return UCKeys::RightShift;
            case XK_Control_L: return UCKeys::LeftCtrl;
            case XK_Control_R: return UCKeys::RightCtrl;
            case XK_Alt_L: return UCKeys::LeftAlt;
            case XK_Alt_R: return UCKeys::RightAlt;
            case XK_Super_L: return UCKeys::LeftMeta;
            case XK_Super_R: return UCKeys::RightMeta;

                // Number pad
            case XK_Num_Lock: return UCKeys::NumLock;
            case XK_KP_Home: return UCKeys::NumPadHome;
            case XK_KP_Up: return UCKeys::NumPadUp;
            case XK_KP_Page_Up: return UCKeys::NumPadPageUp;
            case XK_KP_Left: return UCKeys::NumPadLeft;
            case XK_KP_Right: return UCKeys::NumPadRight;
            case XK_KP_End: return UCKeys::NumPadEnd;
            case XK_KP_Down: return UCKeys::NumPadDown;
            case XK_KP_Page_Down: return UCKeys::NumPadPageDown;
            case XK_KP_0: return UCKeys::NumPad0;
            case XK_KP_1: return UCKeys::NumPad1;
            case XK_KP_2: return UCKeys::NumPad2;
            case XK_KP_3: return UCKeys::NumPad3;
            case XK_KP_4: return UCKeys::NumPad4;
            case XK_KP_5: return UCKeys::NumPad5;
            case XK_KP_6: return UCKeys::NumPad6;
            case XK_KP_7: return UCKeys::NumPad7;
            case XK_KP_8: return UCKeys::NumPad8;
            case XK_KP_9: return UCKeys::NumPad9;
            case XK_KP_Add: return UCKeys::NumPadPlus;
            case XK_KP_Subtract: return UCKeys::NumPadMinus;
            case XK_KP_Divide: return UCKeys::NumPadDivide;
            case XK_KP_Enter: return UCKeys::NumPadEnter;
            case XK_KP_Multiply: return UCKeys::NumPadMultiply;
            case XK_KP_Decimal: return UCKeys::NumPadDecimal;
            case XK_KP_Insert: return UCKeys::NumPadInsert;
            case XK_KP_Delete: return UCKeys::NumPadDelete;

                // ASCII characters
            default:
                if (keysym >= 0x0020 && keysym <= 0x007E) {
                    return static_cast<UCKeys>(toupper((char)keysym));
                }
                return UCKeys::Unknown;
        }
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

} // namespace UltraCanvas