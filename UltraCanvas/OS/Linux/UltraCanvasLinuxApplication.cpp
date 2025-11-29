// OS/Linux/UltraCanvasLinuxApplication.cpp
// Complete Linux application implementation with all methods
// Version: 1.3.0 - Complete implementation
// Last Modified: 2025-07-16
// Author: UltraCanvas Framework

#include "UltraCanvasWindow.h"
#include "UltraCanvasLinuxClipboard.h"
#include "UltraCanvasApplication.h"
#include <iostream>
#include <algorithm>
#include <sys/select.h>
#include <vips/vips8>
#include <errno.h>

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
            , focusedWindow(nullptr)
            , deltaTime(1.0/60.0)
            , targetFPS(60)
            , vsyncEnabled(false)
            , eventThreadRunning(false) {
        instance = this;
        std::cout << "UltraCanvas: Linux Application created" << std::endl;
    }

    UltraCanvasLinuxApplication::~UltraCanvasLinuxApplication() {
        std::cout << "UltraCanvas: Linux Application destructor called" << std::endl;

        if (initialized) {
            Exit();
        }
    }

// ===== INITIALIZATION =====
    bool UltraCanvasLinuxApplication::InitializeNative() {
        if (initialized) {
            std::cout << "UltraCanvas: Already initialized" << std::endl;
            return true;
        }

        std::cout << "UltraCanvas: Initializing Linux Application..." << std::endl;

        try {
            VIPS_INIT(appName.c_str());

            // STEP 1: Initialize X11 display connection
            if (!InitializeX11()) {
                std::cerr << "UltraCanvas: Failed to initialize X11" << std::endl;
                return false;
            }

            // STEP 2: Initialize GLX (optional)
            if (!InitializeGLX()) {
                std::cerr << "UltraCanvas: Failed to initialize GLX (non-critical)" << std::endl;
            }

            // STEP 3: Initialize window manager atoms
            InitializeAtoms();

            // STEP 4: Set up timing
            lastFrameTime = std::chrono::steady_clock::now();

            // STEP 5: Mark as initialized
            initialized = true;
            running = false;

            std::cout << "UltraCanvas: Linux Application initialized successfully" << std::endl;
            return true;

        } catch (const std::exception& e) {
            std::cerr << "UltraCanvas: Exception during initialization: " << e.what() << std::endl;
            CleanupX11();
            return false;
        }
    }

    bool UltraCanvasLinuxApplication::ShutdownNative() {
        vips_shutdown();
        return true;
    }

    bool UltraCanvasLinuxApplication::InitializeX11() {
        // Initialize X11 threading support
        if (!XInitThreads()) {
            std::cerr << "UltraCanvas: XInitThreads() failed" << std::endl;
            return false;
        }

        // Connect to X server
        display = XOpenDisplay(nullptr);
        if (!display) {
            std::cerr << "UltraCanvas: Cannot connect to X server" << std::endl;
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
            std::cerr << "UltraCanvas: Invalid root window" << std::endl;
            XCloseDisplay(display);
            display = nullptr;
            return false;
        }

        if (!visual) {
            std::cerr << "UltraCanvas: Invalid visual" << std::endl;
            XCloseDisplay(display);
            display = nullptr;
            return false;
        }

        std::cout << "X11 Display: " << DisplayString(display) << std::endl;
        std::cout << "Screen: " << screen << ", Depth: " << depth << std::endl;

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
        wmProtocols = XInternAtom(display, "WM_PROTOCOLS", False);
        wmState = XInternAtom(display, "_NET_WM_STATE", False);
        wmStateFullscreen = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", False);
        wmStateMaximizedHorz = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_HORZ", False);
        wmStateMaximizedVert = XInternAtom(display, "_NET_WM_STATE_MAXIMIZED_VERT", False);
        wmStateMinimized = XInternAtom(display, "_NET_WM_STATE_HIDDEN", False);
    }

// ===== MAIN LOOP =====
    void UltraCanvasLinuxApplication::RunNative() {
        if (!initialized) {
            std::cerr << "UltraCanvas: Cannot run - application not initialized" << std::endl;
            return;
        }

        running = true;
        std::cout << "UltraCanvas: Starting Linux main loop..." << std::endl;

        // Start the event processing thread
        //StartEventThread();

        try {
            while (running && !windows.empty()) {
                // Update timing
                UpdateDeltaTime();
                if (XPending(display) > 0) {
                    while (XPending(display) > 0) {
                        XEvent xEvent;
                        XNextEvent(display, &xEvent);

                        ProcessXEvent(xEvent);
                    }
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
                        std::cerr << "UltraCanvas: select() error in event thread" << std::endl;
                        break;
                    }
                }

                // Process all pending events
                ProcessEvents();
//                if (!running || !initialized) {
//                    break;
//                }

                // Check for visible windows
                bool hasVisibleWindows = false;
                for (auto& window : windows) {
                    if (window && window->IsVisible()) {
                        hasVisibleWindows = true;
                        break;
                    }
                }

                if (!hasVisibleWindows) {
                    std::cout << "UltraCanvas: No visible windows, exiting..." << std::endl;
                    break;
                }

                // Update and render all windows
                for (auto& window : windows) {
                    if (window && window->IsVisible() && window->IsNeedsRedraw()) {
                        auto ctx = window->GetRenderContext();
                        if (ctx) {
                            window->Render(ctx);
                            window->Flush();
                            window->ClearRequestRedraw();
                        }
                    }
                }
                // Frame rate control
                //LimitFrameRate();
                RunInEventLoop();
            }

        } catch (const std::exception& e) {
            std::cerr << "UltraCanvas: Exception in main loop: " << e.what() << std::endl;
        }

        // Clean shutdown
        std::cout << "UltraCanvas: Main loop ended, performing cleanup..." << std::endl;
        //StopEventThread();

        std::cout << "UltraCanvas: Destroying all windows..." << std::endl;
        while (!windows.empty()) {
            try {
                auto window = windows.back();
                windows.pop_back();
                window->Destroy();
            } catch (const std::exception& e) {
                std::cerr << "UltraCanvas: Exception destroying window: " << e.what() << std::endl;
            }
        }

        // Cleanup X11 resources
        CleanupX11();
        initialized = false;

        std::cout << "UltraCanvas: Linux main loop completed" << std::endl;
    }

    void UltraCanvasLinuxApplication::Exit() {
        std::cout << "UltraCanvas: Linux application exit requested" << std::endl;
        running = false;
    }

// ===== EVENT PROCESSING =====

    void UltraCanvasLinuxApplication::ProcessXEvent(XEvent& xEvent) {
        // Find the window that owns this event
        if (xEvent.type == SelectionRequest || xEvent.type == SelectionNotify || xEvent.type == SelectionClear) {
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

//    bool UltraCanvasLinuxApplication::IsEventForWindow(const UCEvent& event, UltraCanvasLinuxWindow* window) {
//        if (!window) return false;
//
//        UltraCanvasLinuxWindow* eventWindow = GetWindowForEvent(event);
//        return eventWindow == window;
//    }

    UCEvent UltraCanvasLinuxApplication::ConvertXEventToUCEvent(const XEvent& xEvent) {
        UCEvent event;
        event.timestamp = std::chrono::steady_clock::now();
        //event.nativeEvent = reinterpret_cast<unsigned long>(&xEvent);

        // ===== NEW: SET TARGET WINDOW INFORMATION =====
        // Store the X11 window handle that generated this event
        event.nativeWindowHandle = xEvent.xany.window;

        // Find and store the corresponding UltraCanvas window
        auto targetWindow = static_cast<UltraCanvasLinuxWindow*>(FindWindow(xEvent.xany.window));
        event.targetWindow = static_cast<void*>(targetWindow);

        switch (xEvent.type) {
            case KeyPress:
            case KeyRelease: {
                event.type = (xEvent.type == KeyPress) ?
                             UCEventType::KeyDown : UCEventType::KeyUp;
                event.nativeKeyCode = xEvent.xkey.keycode;
                event.virtualKey = ConvertXKeyToUCKey(XLookupKeysym(const_cast<XKeyEvent*>(&xEvent.xkey), 0));

                // Get character representation
                char buffer[32] = {0};
                KeySym keysym;
                int len = XLookupString(const_cast<XKeyEvent*>(&xEvent.xkey), buffer, sizeof(buffer), &keysym, nullptr);
                if (len > 0) {
                    event.character = buffer[0];
                    event.text = std::string(buffer, len);
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
                    event.type = UCEventType::WindowClose;
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
                event.type = UCEventType::WindowFocus;
                break;
            }

            case FocusOut: {
                event.type = UCEventType::WindowBlur;
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

        std::cout << "UltraCanvas: Starting event processing thread..." << std::endl;

        eventThreadRunning = true;
        eventThread = std::thread([this]() {
            EventThreadFunction();
        });

        std::cout << "UltraCanvas: Event thread started successfully" << std::endl;
    }

    void UltraCanvasLinuxApplication::StopEventThread() {
        if (!eventThreadRunning) {
            return;
        }

        std::cout << "UltraCanvas: Stopping event thread..." << std::endl;

        eventThreadRunning = false;
        eventCondition.notify_all();

        if (eventThread.joinable()) {
            try {
                eventThread.join();
                std::cout << "UltraCanvas: Event thread stopped successfully" << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "UltraCanvas: Exception stopping event thread: " << e.what() << std::endl;
            }
        }
    }

    void UltraCanvasLinuxApplication::EventThreadFunction() {
        std::cout << "UltraCanvas: Event thread running..." << std::endl;

        while (eventThreadRunning && display) {
            try {
                if (XPending(display) > 0) {
                    XEvent xEvent;
                    XNextEvent(display, &xEvent);
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
                        std::cerr << "UltraCanvas: select() error in event thread" << std::endl;
                        break;
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "UltraCanvas: Exception in event thread: " << e.what() << std::endl;
                break;
            }
        }

        std::cout << "UltraCanvas: Event thread ended" << std::endl;
    }

// ===== TIMING AND FRAME RATE =====
    void UltraCanvasLinuxApplication::UpdateDeltaTime() {
        auto currentTime = std::chrono::steady_clock::now();

        if (lastFrameTime.time_since_epoch().count() == 0) {
            deltaTime = 1.0 / 60.0;
        } else {
            auto frameDuration = currentTime - lastFrameTime;
            deltaTime = std::chrono::duration<double>(frameDuration).count();
        }

        deltaTime = std::min(deltaTime, 1.0 / 30.0);
        lastFrameTime = currentTime;
    }

    void UltraCanvasLinuxApplication::LimitFrameRate() {
        if (targetFPS <= 0) return;

        auto targetFrameTime = std::chrono::microseconds(1000000 / targetFPS);
        auto currentTime = std::chrono::steady_clock::now();
        auto actualFrameTime = currentTime - lastFrameTime;

        if (actualFrameTime < targetFrameTime) {
            auto sleepTime = targetFrameTime - actualFrameTime;
            std::this_thread::sleep_for(sleepTime);
        }
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
        std::cerr << "X11 Error: " << errorText
                  << " (code: " << (int)event->error_code << ")" << std::endl;
        return 0;
    }

    int UltraCanvasLinuxApplication::XIOErrorHandler(Display* display) {
        std::cerr << "X11 IO Error: Connection to X server lost" << std::endl;
        exit(1);
        return 0;
    }

    void UltraCanvasLinuxApplication::LogXError(const std::string& context, int errorCode) {
        std::cerr << "UltraCanvas X11 Error in " << context << ": code " << errorCode << std::endl;
    }

// ===== CLEANUP =====
    void UltraCanvasLinuxApplication::CleanupX11() {
        if (display) {
            std::cout << "UltraCanvas: Closing X11 display..." << std::endl;
            XCloseDisplay(display);
            display = nullptr;
        }
    }

} // namespace UltraCanvas