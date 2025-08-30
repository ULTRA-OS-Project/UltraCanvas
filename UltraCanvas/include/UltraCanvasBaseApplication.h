// UltraCanvasBaseApplication.h
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
    class UltraCanvasElement;
    struct WindowConfig;

    class UltraCanvasBaseApplication {
    protected:
        bool volatile running = false;
        bool volatile initialized = false;

        std::unordered_map<unsigned long, UltraCanvasWindow*> windowMap;
        std::vector<UltraCanvasWindow*> windows;

        UltraCanvasWindow* focusedWindow = nullptr;
        UltraCanvasElement* hoveredElement = nullptr;
        UltraCanvasElement* capturedElement = nullptr;
        UltraCanvasElement* draggedElement = nullptr;

        std::vector<std::function<bool(const UCEvent&)>> globalEventHandlers;

        UCEvent lastMouseEvent;
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

        bool DispatchEvent(const UCEvent &event);
        bool HandleEventWithBubbling(const UCEvent &event, UltraCanvasElement* elem);
        void RegisterGlobalEventHandler(std::function<bool(const UCEvent&)> handler);
        void ClearGlobalEventHandlers() { globalEventHandlers.clear(); }

        bool IsKeyPressed(int keyCode);

        bool IsShiftHeld() { return shiftHeld; }
        bool IsCtrlHeld() { return ctrlHeld; }
        bool IsAltHeld() { return altHeld; }
        bool IsMetaHeld() { return metaHeld; }

        UltraCanvasWindow* GetFocusedWindow() { return focusedWindow; }
        UltraCanvasElement* GetFocusedElement();
        UltraCanvasElement* GetHoveredElement() { return hoveredElement; }
        UltraCanvasElement* GetCapturedElement() { return capturedElement; }

        UltraCanvasWindow* FindWindow(unsigned long nativeHandle);

        virtual void FocusNextElement();
        virtual void FocusPreviousElement();

        // ===== MOUSE CAPTURE =====
        void CaptureMouse(UltraCanvasElement* element) { capturedElement = element; }
        void ReleaseMouse(UltraCanvasElement* element) { if (element && element == capturedElement) capturedElement = nullptr; }

        virtual bool Initialize() = 0;
        virtual void Run() = 0;
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