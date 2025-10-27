// OS/BSD/UltraCanvasBSDApplication.h
// Complete BSD platform implementation for UltraCanvas Framework
// Version: 1.0.0
// Last Modified: 2025-10-08
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_BSD_APPLICATION_H
#define ULTRACANVAS_BSD_APPLICATION_H

// ===== CORE INCLUDES =====
#include "../../include/UltraCanvasWindow.h"
#include "../../include/UltraCanvasEvent.h"
#include "../../include/UltraCanvasCommonTypes.h"

// ===== BSD PLATFORM INCLUDES =====
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <pango/pangocairo.h>

// ===== BSD-SPECIFIC INCLUDES =====
#ifdef __OpenBSD__
#include <unistd.h>  // For pledge() and unveil()
#endif

// ===== STANDARD INCLUDES =====
#include <memory>
#include <vector>
#include <unordered_map>
#include <functional>
#include <thread>
#include <chrono>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace UltraCanvas {

// Forward declarations
class UltraCanvasBSDWindow;

// ===== BSD APPLICATION CLASS =====
class UltraCanvasBSDApplication : public UltraCanvasBaseApplication {
private:
    // ===== X11 DISPLAY SYSTEM =====
    Display* display;
    int screen;
    Window rootWindow;
    Visual* visual;
    Colormap colormap;
    int depth;

    // ===== OPENGL CONTEXT =====
    bool glxSupported;

    // ===== EVENT SYSTEM =====
    std::queue<UCEvent> eventQueue;
    std::mutex eventQueueMutex;
    std::condition_variable eventCondition;
    bool eventThreadRunning;
    std::thread eventThread;

    // ===== WINDOW MANAGEMENT =====
    std::unordered_map<Window, UltraCanvasBSDWindow*> windowMap;
    UltraCanvasBSDWindow* focusedWindow;

    // ===== TIMING AND FRAME RATE =====
    std::chrono::steady_clock::time_point lastFrameTime;
    double deltaTime;
    int targetFPS;
    bool vsyncEnabled;

    // ===== WINDOW MANAGER ATOMS =====
    Atom wmDeleteWindow;
    Atom wmProtocols;
    Atom wmState;
    Atom wmStateFullscreen;
    Atom wmStateMaximizedHorz;
    Atom wmStateMaximizedVert;
    Atom wmStateMinimized;

    // ===== INTERNAL METHODS =====
    bool InitializeX11();
    bool InitializeGLX();
    void InitializeAtoms();
    void CleanupX11();
    
    #ifdef __OpenBSD__
    bool InitializeOpenBSDSecurity();
    #endif

    void ProcessXEvent(const XEvent& event);
    UCEvent ConvertXEventToUCEvent(const XEvent& event);
    
    void StartEventThread();
    void StopEventThread();
    void EventThreadFunction();
    
    void UpdateDeltaTime();
    void LimitFrameRate();

    // ===== ERROR HANDLERS =====
    static int XErrorHandler(Display* display, XErrorEvent* event);
    static int XIOErrorHandler(Display* display);
    void LogXError(const std::string& context, int errorCode);

public:
    // ===== CONSTRUCTOR & DESTRUCTOR =====
    UltraCanvasBSDApplication();
    virtual ~UltraCanvasBSDApplication() override;

    // ===== PLATFORM INITIALIZATION =====
    bool InitializeNative() override;
    void RunNative() override;
    void ExitNative() override;

    // ===== WINDOW MANAGEMENT =====
    void RegisterWindow(Window xWindow, UltraCanvasBSDWindow* window);
    void UnregisterWindow(Window xWindow);
    UltraCanvasBSDWindow* GetWindowFromXWindow(Window xWindow);

    // ===== DISPLAY ACCESS =====
    Display* GetDisplay() const { return display; }
    int GetScreen() const { return screen; }
    Window GetRootWindow() const { return rootWindow; }
    Visual* GetVisual() const { return visual; }
    Colormap GetColormap() const { return colormap; }
    int GetDepth() const { return depth; }

    // ===== WINDOW MANAGER ATOMS =====
    Atom GetWMDeleteWindow() const { return wmDeleteWindow; }
    Atom GetWMProtocols() const { return wmProtocols; }

    // ===== TIMING =====
    double GetDeltaTime() const { return deltaTime; }
    void SetTargetFPS(int fps) { targetFPS = fps; }
    int GetTargetFPS() const { return targetFPS; }
    void SetVSync(bool enabled) { vsyncEnabled = enabled; }
    bool GetVSync() const { return vsyncEnabled; }

    // ===== CLIPBOARD SUPPORT =====
    std::string GetClipboardText() override;
    void SetClipboardText(const std::string& text) override;
};

} // namespace UltraCanvas

#endif // ULTRACANVAS_BSD_APPLICATION_H