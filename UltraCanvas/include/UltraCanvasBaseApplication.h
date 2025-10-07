// include/UltraCanvasBaseApplication.h
// Main UltraCanvas Framework Entry Point - Unified System
// Version: 1.0.0
// Last Modified: 2025-01-07
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_BASE_APPLICATION_H
#define ULTRACANVAS_BASE_APPLICATION_H

#include "UltraCanvasEvent.h"
#include <vector>
#include <algorithm>
#include <functional>
#include <iostream>
#include <chrono>

namespace UltraCanvas {
    class UltraCanvasWindow;
    class UltraCanvasUIElement;
    struct WindowConfig;

    class UltraCanvasBaseApplication {
    protected:
        bool volatile running = false;
        bool volatile initialized = false;

        std::unordered_map<unsigned long, UltraCanvasWindow*> windowMap;
        std::vector<UltraCanvasWindow*> windows;

        UltraCanvasWindow* focusedWindow = nullptr;
        UltraCanvasUIElement* hoveredElement = nullptr;
        UltraCanvasUIElement* capturedElement = nullptr;
        UltraCanvasUIElement* draggedElement = nullptr;

        std::vector<std::function<bool(const UCEvent&)>> globalEventHandlers;
        std::function<void()> eventLoopCallback;

        UCEvent lastMouseEvent;
        UCEvent currentEvent;
        std::chrono::steady_clock::time_point lastClickTime;
        const float DOUBLE_CLICK_TIME = 0;
        const int DOUBLE_CLICK_DISTANCE = 0;

        // Keyboard state
        bool keyStates[256];
        bool shiftHeld = false;
        bool ctrlHeld = false;
        bool altHeld = false;
        bool metaHeld = false;

    public:
        virtual ~UltraCanvasBaseApplication() = default;
        void RegisterWindow(UltraCanvasWindow* window);
        void UnregisterWindow(UltraCanvasWindow* window);

        void DispatchEvent(const UCEvent &event);
        bool HandleEventWithBubbling(const UCEvent &event, UltraCanvasUIElement* elem);
        void RegisterGlobalEventHandler(std::function<bool(const UCEvent&)> handler);
        void ClearGlobalEventHandlers() { globalEventHandlers.clear(); }
        void RegisterEventLoopRunCallback(std::function<void()> callback);

        bool IsKeyPressed(int keyCode);

        bool IsShiftHeld() { return shiftHeld; }
        bool IsCtrlHeld() { return ctrlHeld; }
        bool IsAltHeld() { return altHeld; }
        bool IsMetaHeld() { return metaHeld; }

        UltraCanvasWindow* GetFocusedWindow() { return focusedWindow; }
        UltraCanvasUIElement* GetFocusedElement();
        UltraCanvasUIElement* GetHoveredElement() { return hoveredElement; }
        UltraCanvasUIElement* GetCapturedElement() { return capturedElement; }

        UltraCanvasWindow* FindWindow(unsigned long nativeHandle);

        const UCEvent& GetCurrentEvent() { return currentEvent; }

        bool DispatchEventToElement(UltraCanvasUIElement* elem, const UCEvent &event);

        virtual void FocusNextElement();
        virtual void FocusPreviousElement();

        // ===== MOUSE CAPTURE =====
        void CaptureMouse(UltraCanvasUIElement* element) { capturedElement = element; }
        void ReleaseMouse(UltraCanvasUIElement* element) { if (element && element == capturedElement) capturedElement = nullptr; }

        void Run();
        bool Initialize();
        virtual bool InitializeNative() = 0;
        virtual void RunNative() = 0;
        virtual void RunInEventLoop() {};
        virtual void Exit() = 0;

        bool IsInitialized() const { return initialized; }
        bool IsRunning() const { return running; }

    protected:
        bool IsDoubleClick(const UCEvent &event);
        bool HandleWindowFocus(UltraCanvasWindow* window);

    };
}

#endif