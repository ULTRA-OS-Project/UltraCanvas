// OS/MacOS/UltraCanvasMacOSApplication.mm
// Complete macOS application implementation with Cocoa/Cairo support
// Version: 2.0.0
// Last Modified: 2025-01-18
// Author: UltraCanvas Framework

#include "UltraCanvasMacOSApplication.h"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

#include <iostream>
#include <thread>
#include <chrono>

namespace UltraCanvas {

// ===== STATIC INSTANCE =====
    UltraCanvasMacOSApplication* UltraCanvasMacOSApplication::instance = nullptr;

// ===== CONSTRUCTOR & DESTRUCTOR =====
    UltraCanvasMacOSApplication::UltraCanvasMacOSApplication()
            : nsApplication(nullptr)
            , mainRunLoop(nullptr)
            , cairoSupported(false)
            , retinaSupported(false)
            , displayScaleFactor(1.0f)
            , eventThreadRunning(false)
            , mainThreadId(std::this_thread::get_id())
    {
        instance = this;
        std::cout << "UltraCanvas: macOS Application created" << std::endl;
    }

    UltraCanvasMacOSApplication::~UltraCanvasMacOSApplication() {
        std::cout << "UltraCanvas: macOS Application destructor called" << std::endl;

        if (initialized) {
            Exit();
        }
    }

// ===== INITIALIZATION =====
    bool UltraCanvasMacOSApplication::InitializeNative() {
        if (initialized) {
            std::cout << "UltraCanvas: Already initialized" << std::endl;
            return true;
        }

        std::cout << "UltraCanvas: Initializing macOS Application..." << std::endl;

        @autoreleasepool {
            // STEP 1: Initialize Cocoa
            if (!InitializeCocoa()) {
                std::cerr << "UltraCanvas: Failed to initialize Cocoa" << std::endl;
                return false;
            }

            // STEP 2: Initialize Cairo
            if (!InitializeCairo()) {
                std::cerr << "UltraCanvas: Failed to initialize Cairo" << std::endl;
                return false;
            }

            // STEP 3: Initialize display settings
            InitializeDisplaySettings();

            // STEP 4: Initialize menu bar
            InitializeMenuBar();

            // STEP 5: Start event thread
            //StartEventThread();

            initialized = true;
            running = true;

            std::cout << "UltraCanvas: macOS Application initialized successfully" << std::endl;
            return true;
        }
    }

    bool UltraCanvasMacOSApplication::InitializeCocoa() {
        std::cout << "UltraCanvas: Initializing Cocoa..." << std::endl;

        @autoreleasepool {
            // Create autorelease pool
            // Get shared application instance
            nsApplication = [NSApplication sharedApplication];
            if (!nsApplication) {
                std::cerr << "UltraCanvas: Failed to get NSApplication instance" << std::endl;
                return false;
            }

            // Set activation policy to regular app
            [nsApplication setActivationPolicy:NSApplicationActivationPolicyRegular];

            // Get main run loop
            mainRunLoop = [NSRunLoop mainRunLoop];

            std::cout << "UltraCanvas: Cocoa initialized successfully" << std::endl;
            return true;
        }
    }

    bool UltraCanvasMacOSApplication::InitializeCairo() {
        std::cout << "UltraCanvas: Initializing Cairo..." << std::endl;

        // Test if Cairo Quartz backend is available
        cairo_surface_t* test_surface = cairo_quartz_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
        if (test_surface) {
            cairo_status_t status = cairo_surface_status(test_surface);
            if (status == CAIRO_STATUS_SUCCESS) {
                cairoSupported = true;
                std::cout << "UltraCanvas: Cairo Quartz backend available" << std::endl;
            } else {
                std::cerr << "UltraCanvas: Cairo surface creation failed: "
                          << cairo_status_to_string(status) << std::endl;
                cairoSupported = false;
            }
            cairo_surface_destroy(test_surface);
        } else {
            std::cerr << "UltraCanvas: Cairo Quartz backend not available" << std::endl;
            cairoSupported = false;
        }

        return cairoSupported;
    }

    void UltraCanvasMacOSApplication::InitializeMenuBar() {
        std::cout << "UltraCanvas: Initializing menu bar..." << std::endl;

        @autoreleasepool {
            // Create main menu
            NSMenu* mainMenu = [[NSMenu alloc] initWithTitle:@""];

            // Application menu
            NSMenuItem* appMenuItem = [[NSMenuItem alloc] init];
            NSMenu* appMenu = [[NSMenu alloc] initWithTitle:@""];

            // About item
            NSString* appName = [[NSProcessInfo processInfo] processName];
            NSString* aboutTitle = [@"About " stringByAppendingString:appName];
            NSMenuItem* aboutItem = [[NSMenuItem alloc] initWithTitle:aboutTitle
                                                               action:@selector(orderFrontStandardAboutPanel:)
                                                        keyEquivalent:@""];
            [appMenu addItem:aboutItem];

            [appMenu addItem:[NSMenuItem separatorItem]];

            // Quit item
            NSString* quitTitle = [@"Quit " stringByAppendingString:appName];
            NSMenuItem* quitItem = [[NSMenuItem alloc] initWithTitle:quitTitle
                                                              action:@selector(terminate:)
                                                       keyEquivalent:@"q"];
            [appMenu addItem:quitItem];

            [appMenuItem setSubmenu:appMenu];
            [mainMenu addItem:appMenuItem];

            // Set as main menu
            [nsApplication setMainMenu:mainMenu];

            std::cout << "UltraCanvas: Menu bar initialized" << std::endl;
        }
    }

    void UltraCanvasMacOSApplication::InitializeDisplaySettings() {
        std::cout << "UltraCanvas: Initializing display settings..." << std::endl;

        @autoreleasepool {
            NSScreen* mainScreen = [NSScreen mainScreen];
            if (mainScreen) {
                displayScaleFactor = [mainScreen backingScaleFactor];
                retinaSupported = (displayScaleFactor > 1.0f);

                std::cout << "UltraCanvas: Display scale factor: " << displayScaleFactor << std::endl;
                std::cout << "UltraCanvas: Retina display: " << (retinaSupported ? "Yes" : "No") << std::endl;
            }
        }
    }

    bool UltraCanvasMacOSApplication::ShutdownNative() {
        std::cout << "UltraCanvas: Shutting down macOS Application..." << std::endl;

        vips_shutdown();

        //StopEventThread();
        CleanupCocoa();

        running = false;

        std::cout << "UltraCanvas: macOS Application shut down" << std::endl;
        return true;
    }

    void UltraCanvasMacOSApplication::CleanupCocoa() {
        @autoreleasepool {
            nsApplication = nullptr;
            mainRunLoop = nullptr;
        }
    }

// ===== MAIN LOOP =====
    void UltraCanvasMacOSApplication::RunNative() {
        std::cout << "UltraCanvas: Starting macOS main loop..." << std::endl;

        @autoreleasepool {
            // Activate the application
            [nsApplication activateIgnoringOtherApps:YES];

            // Main event loop
            while (running) {
                @autoreleasepool {
                    // Process Cocoa events
                    NSEvent* event = [nsApplication nextEventMatchingMask:NSEventMaskAny
                                                                untilDate:[NSDate distantPast]
                                                                   inMode:NSDefaultRunLoopMode
                                                                  dequeue:YES];

                    if (event) {
                        ProcessCocoaEvent(event);
                        [nsApplication sendEvent:event];
                        [nsApplication updateWindows];
                    }

                    // Process UltraCanvas events
                    ProcessEvents();

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
            }
        }

        std::cout << "UltraCanvas: macOS main loop completed" << std::endl;
    }

    void UltraCanvasMacOSApplication::Exit() {
        std::cout << "UltraCanvas: macOS application exit requested" << std::endl;
        running = false;
    }

// ===== EVENT PROCESSING =====
    void UltraCanvasMacOSApplication::ProcessCocoaEvent(NSEvent* nsEvent) {
        if (!nsEvent) return;

        // Convert to UCEvent
        UCEvent ucEvent = ConvertNSEventToUCEvent(nsEvent);

        if (ucEvent.type != UCEventType::NoneEvent) {
            PushEvent(ucEvent);
        }
    }

    UCEvent UltraCanvasMacOSApplication::ConvertNSEventToUCEvent(NSEvent* nsEvent) {
        UCEvent event;
        event.timestamp = std::chrono::steady_clock::now();

        NSEventType eventType = [nsEvent type];

        // Find target window
        NSWindow* nsWindow = [nsEvent window];
        UltraCanvasMacOSWindow* targetWindow = nullptr;

        if (nsWindow) {
            targetWindow = static_cast<UltraCanvasMacOSWindow*>(FindWindow((unsigned long)nsWindow));
        }

        event.targetWindow = targetWindow;

        // Convert event based on type
        switch (eventType) {
            case NSEventTypeLeftMouseDown:
            case NSEventTypeRightMouseDown:
            case NSEventTypeOtherMouseDown:
                event.type = UCEventType::MouseDown;
                event.button = ConvertNSEventMouseButton([nsEvent buttonNumber]);
                event.x = [nsEvent locationInWindow].x;
                event.y = [nsEvent locationInWindow].y;
                break;

            case NSEventTypeLeftMouseUp:
            case NSEventTypeRightMouseUp:
            case NSEventTypeOtherMouseUp:
                event.type = UCEventType::MouseUp;
                event.button = ConvertNSEventMouseButton([nsEvent buttonNumber]);
                event.x = [nsEvent locationInWindow].x;
                event.y = [nsEvent locationInWindow].y;
                break;

            case NSEventTypeMouseMoved:
            case NSEventTypeLeftMouseDragged:
            case NSEventTypeRightMouseDragged:
            case NSEventTypeOtherMouseDragged:
                event.type = UCEventType::MouseMove;
                event.x = [nsEvent locationInWindow].x;
                event.y = [nsEvent locationInWindow].y;
                break;

            case NSEventTypeScrollWheel:
                event.type = UCEventType::MouseWheel;
                event.wheelDelta = [nsEvent scrollingDeltaY];
                event.x = [nsEvent locationInWindow].x;
                event.y = [nsEvent locationInWindow].y;
                break;

            case NSEventTypeKeyDown:
                event.type = UCEventType::KeyDown;
                event.virtualKey = ConvertNSEventKeyCode([nsEvent keyCode]);
                event.text = [[nsEvent characters] UTF8String] ?: "";
                event.ctrl = ([nsEvent modifierFlags] & NSEventModifierFlagControl) != 0;
                event.shift = ([nsEvent modifierFlags] & NSEventModifierFlagShift) != 0;
                event.alt = ([nsEvent modifierFlags] & NSEventModifierFlagOption) != 0;
                event.meta = ([nsEvent modifierFlags] & NSEventModifierFlagCommand) != 0;
                break;

            case NSEventTypeKeyUp:
                event.type = UCEventType::KeyUp;
                event.virtualKey = ConvertNSEventKeyCode([nsEvent keyCode]);
                event.ctrl = ([nsEvent modifierFlags] & NSEventModifierFlagControl) != 0;
                event.shift = ([nsEvent modifierFlags] & NSEventModifierFlagShift) != 0;
                event.alt = ([nsEvent modifierFlags] & NSEventModifierFlagOption) != 0;
                event.meta = ([nsEvent modifierFlags] & NSEventModifierFlagCommand) != 0;
                break;

            default:
                event.type = UCEventType::NoneEvent;
                break;
        }

        return event;
    }

// ===== KEY CODE CONVERSION =====
    UCKeys UltraCanvasMacOSApplication::ConvertNSEventKeyCode(unsigned short keyCode) {
        // macOS virtual key codes to UCKeys
        switch (keyCode) {
            // Letters
            case 0x00: return UCKeys::A;
            case 0x0B: return UCKeys::B;
            case 0x08: return UCKeys::C;
            case 0x02: return UCKeys::D;
            case 0x0E: return UCKeys::E;
            case 0x03: return UCKeys::F;
            case 0x05: return UCKeys::G;
            case 0x04: return UCKeys::H;
            case 0x22: return UCKeys::I;
            case 0x26: return UCKeys::J;
            case 0x28: return UCKeys::K;
            case 0x25: return UCKeys::L;
            case 0x2E: return UCKeys::M;
            case 0x2D: return UCKeys::N;
            case 0x1F: return UCKeys::O;
            case 0x23: return UCKeys::P;
            case 0x0C: return UCKeys::Q;
            case 0x0F: return UCKeys::R;
            case 0x01: return UCKeys::S;
            case 0x11: return UCKeys::T;
            case 0x20: return UCKeys::U;
            case 0x09: return UCKeys::V;
            case 0x0D: return UCKeys::W;
            case 0x07: return UCKeys::X;
            case 0x10: return UCKeys::Y;
            case 0x06: return UCKeys::Z;

                // Numbers
            case 0x1D: return UCKeys::NumPad0;
            case 0x12: return UCKeys::NumPad1;
            case 0x13: return UCKeys::NumPad2;
            case 0x14: return UCKeys::NumPad3;
            case 0x15: return UCKeys::NumPad4;
            case 0x17: return UCKeys::NumPad5;
            case 0x16: return UCKeys::NumPad6;
            case 0x1A: return UCKeys::NumPad7;
            case 0x1C: return UCKeys::NumPad8;
            case 0x19: return UCKeys::NumPad9;

                // Function keys
            case 0x7A: return UCKeys::F1;
            case 0x78: return UCKeys::F2;
            case 0x63: return UCKeys::F3;
            case 0x76: return UCKeys::F4;
            case 0x60: return UCKeys::F5;
            case 0x61: return UCKeys::F6;
            case 0x62: return UCKeys::F7;
            case 0x64: return UCKeys::F8;
            case 0x65: return UCKeys::F9;
            case 0x6D: return UCKeys::F10;
            case 0x67: return UCKeys::F11;
            case 0x6F: return UCKeys::F12;

                // Special keys
            case 0x33: return UCKeys::Backspace;
            case 0x30: return UCKeys::Tab;
            case 0x24: return UCKeys::Enter;
            case 0x35: return UCKeys::Escape;
            case 0x31: return UCKeys::Space;

                // Arrow keys
            case 0x7B: return UCKeys::Left;
            case 0x7C: return UCKeys::Right;
            case 0x7E: return UCKeys::Up;
            case 0x7D: return UCKeys::Down;

                // Navigation
            case 0x73: return UCKeys::Home;
            case 0x77: return UCKeys::End;
            case 0x74: return UCKeys::PageUp;
            case 0x79: return UCKeys::PageDown;
            case 0x75: return UCKeys::Delete;

            default: return UCKeys::Unknown;
        }
    }

    UCMouseButton UltraCanvasMacOSApplication::ConvertNSEventMouseButton(int buttonNumber) {
        switch (buttonNumber) {
            case 0: return UCMouseButton::Left;
            case 1: return UCMouseButton::Right;
            case 2: return UCMouseButton::Middle;
            default: return UCMouseButton::Unknown;
        }
    }

// ===== WINDOW MANAGEMENT =====
//    void UltraCanvasMacOSApplication::RegisterWindow(void* nsWindow, UltraCanvasMacOSWindow* window) {
//        std::lock_guard<std::mutex> lock(windowMapMutex);
//        windowMap[nsWindow] = window;
//        std::cout << "UltraCanvas: Window registered" << std::endl;
//    }
//
//    void UltraCanvasMacOSApplication::UnregisterWindow(void* nsWindow) {
//        std::lock_guard<std::mutex> lock(windowMapMutex);
//        windowMap.erase(nsWindow);
//        std::cout << "UltraCanvas: Window unregistered" << std::endl;
//    }
//
//    UltraCanvasMacOSWindow* UltraCanvasMacOSApplication::FindWindow(void* nsWindow) {
//        std::lock_guard<std::mutex> lock(windowMapMutex);
//        auto it = windowMap.find(nsWindow);
//        if (it != windowMap.end()) {
//            return it->second;
//        }
//        return nullptr;
//    }

// ===== EVENT THREAD =====
//    void UltraCanvasMacOSApplication::StartEventThread() {
//        if (eventThreadRunning) {
//            return;
//        }
//
//        std::cout << "UltraCanvas: Starting event thread..." << std::endl;
//
//        eventThreadRunning = true;
//        eventThread = std::thread([this]() {
//            EventThreadFunction();
//        });
//
//        std::cout << "UltraCanvas: Event thread started" << std::endl;
//    }
//
//    void UltraCanvasMacOSApplication::StopEventThread() {
//        if (!eventThreadRunning) {
//            return;
//        }
//
//        std::cout << "UltraCanvas: Stopping event thread..." << std::endl;
//
//        eventThreadRunning = false;
//
//        if (eventThread.joinable()) {
//            eventThread.join();
//        }
//
//        std::cout << "UltraCanvas: Event thread stopped" << std::endl;
//    }

//    void UltraCanvasMacOSApplication::EventThreadFunction() {
//        std::cout << "UltraCanvas: Event thread running..." << std::endl;
//
//        while (eventThreadRunning) {
//            // Background event processing if needed
//            std::this_thread::sleep_for(std::chrono::milliseconds(16));
//        }
//
//        std::cout << "UltraCanvas: Event thread ended" << std::endl;
//    }

// ===== TIMING =====
//    void UltraCanvasMacOSApplication::UpdateDeltaTime() {
//        auto currentTime = std::chrono::steady_clock::now();
//
//        if (lastFrameTime.time_since_epoch().count() == 0) {
//            deltaTime = 1.0 / 60.0;
//        } else {
//            auto frameDuration = currentTime - lastFrameTime;
//            deltaTime = std::chrono::duration<double>(frameDuration).count();
//        }
//
//        // Clamp to reasonable values
//        deltaTime = std::min(deltaTime, 1.0 / 30.0);
//        lastFrameTime = currentTime;
//    }

//    void UltraCanvasMacOSApplication::LimitFrameRate() {
//        if (targetFPS <= 0) return;
//
//        auto targetFrameTime = std::chrono::microseconds(1000000 / targetFPS);
//        auto currentTime = std::chrono::steady_clock::now();
//        auto actualFrameTime = currentTime - lastFrameTime;
//
//        if (actualFrameTime < targetFrameTime) {
//            auto sleepTime = targetFrameTime - actualFrameTime;
//            std::this_thread::sleep_for(sleepTime);
//        }
//    }

// ===== THREAD SAFETY =====
    bool UltraCanvasMacOSApplication::IsMainThread() const {
        return std::this_thread::get_id() == mainThreadId;
    }
} // namespace UltraCanvas