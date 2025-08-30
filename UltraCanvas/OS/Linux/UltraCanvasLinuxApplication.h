// UltraCanvasLinuxApplication.h
// Complete Linux platform implementation for UltraCanvas Framework
// Version: 1.2.0
// Last Modified: 2025-07-15
// Author: UltraCanvas Framework

#pragma once

#ifndef ULTRACANVAS_LINUX_APPLICATION_H
#define ULTRACANVAS_LINUX_APPLICATION_H

// ===== CORE INCLUDES =====
#include "../../include/UltraCanvasBaseApplication.h"
#include "../../include/UltraCanvasWindow.h"
#include "../../include/UltraCanvasEvent.h"
#include "../../include/UltraCanvasCommonTypes.h"

// ===== LINUX PLATFORM INCLUDES =====
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <pango/pangocairo.h>

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
    class UltraCanvasLinuxWindow;

// ===== LINUX APPLICATION CLASS =====
    class UltraCanvasLinuxApplication : public UltraCanvasBaseApplication {
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
        std::unordered_map<Window, UltraCanvasLinuxWindow*> windowMap;
        UltraCanvasLinuxWindow* focusedWindow;

        // ===== TIMING AND FRAME RATE =====
        std::chrono::steady_clock::time_point lastFrameTime;
        double deltaTime;
        int targetFPS;
        bool vsyncEnabled;

        // ===== GLOBAL EVENT HANDLING =====
        std::function<bool(const UCEvent&)> globalEventHandler;

        // ===== SYSTEM ATOMS =====
        Atom wmDeleteWindow;
        Atom wmProtocols;
        Atom wmState;
        Atom wmStateFullscreen;
        Atom wmStateMaximizedHorz;
        Atom wmStateMaximizedVert;
        Atom wmStateMinimized;

    public:
        // ===== CONSTRUCTOR & DESTRUCTOR =====
        UltraCanvasLinuxApplication();
        virtual ~UltraCanvasLinuxApplication();

        // ===== INHERITED FROM BASE APPLICATION =====
        virtual bool Initialize() override;
        virtual void Run() override;
        virtual void Exit() override;

        // ===== LINUX-SPECIFIC METHODS =====

        // Display and screen information
        Display* GetDisplay() const { return display; }
        int GetScreen() const { return screen; }
        Window GetRootWindow() const { return rootWindow; }
        Visual* GetVisual() const { return visual; }
        Colormap GetColormap() const { return colormap; }
        int GetDepth() const { return depth; }

        // OpenGL context management
        bool IsGLXSupported() const { return glxSupported; }
        bool CreateGLXContext();
        void DestroyGLXContext();

        // Event processing
        void ProcessEvents();
        void WaitForEvents(int timeoutMs = -1);
        void PushEvent(const UCEvent& event);
        bool PopEvent(UCEvent& event);

        // Window management

        // Frame rate and timing
        void SetTargetFPS(int fps) { targetFPS = fps; }
        int GetTargetFPS() const { return targetFPS; }
        void SetVSyncEnabled(bool enabled) { vsyncEnabled = enabled; }
        bool IsVSyncEnabled() const { return vsyncEnabled; }
        double GetDeltaTime() const { return deltaTime; }

        // Platform utilities
        std::string GetClipboardText();
        void SetClipboardText(const std::string& text);

        // Atom access
        Atom GetWMDeleteWindow() const { return wmDeleteWindow; }
        Atom GetWMProtocols() const { return wmProtocols; }
        Atom GetWMState() const { return wmState; }
        Atom GetWMStateFullscreen() const { return wmStateFullscreen; }
        Atom GetWMStateMaximizedHorz() const { return wmStateMaximizedHorz; }
        Atom GetWMStateMaximizedVert() const { return wmStateMaximizedVert; }
        Atom GetWMStateMinimized() const { return wmStateMinimized; }

        void ProcessXEvent(XEvent& xEvent);
    private:
        // ===== INTERNAL INITIALIZATION =====
        bool InitializeX11();
        bool InitializeGLX();
        void InitializeAtoms();

        // ===== EVENT PROCESSING INTERNALS =====
        void EventThreadFunction();
        UCEvent ConvertXEventToUCEvent(const XEvent& xEvent);
        void StartEventThread();
        void StopEventThread();

//        UltraCanvasLinuxWindow* GetWindowForEvent(const UCEvent& event);
//        bool IsEventForWindow(const UCEvent& event, UltraCanvasLinuxWindow* window);

        // ===== KEYBOARD AND MOUSE CONVERSION =====
        UCMouseButton ConvertXButtonToUCButton(unsigned int button);
        UCKeys ConvertXKeyToUCKey(KeySym keysym);

        // ===== CLEANUP =====
        void CleanupX11();

        // ===== FRAME RATE CONTROL =====
        void UpdateDeltaTime();
        void LimitFrameRate();

        // ===== ERROR HANDLING =====
        static int XErrorHandler(Display* display, XErrorEvent* event);
        static int XIOErrorHandler(Display* display);
        void LogXError(const std::string& context, int errorCode);
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_LINUX_APPLICATION_H