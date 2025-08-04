// UltraCanvasBaseApplication.h
// Main UltraCanvas Framework Entry Point - Unified System
// Version: 1.0.0
// Last Modified: 2025-01-07
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_BASE_APPLICATION_H
#define ULTRACANVAS_BASE_APPLICATION_H

#include "vector"
#include "memory"

namespace UltraCanvas {
    class UltraCanvasWindow;
    struct WindowConfig;

    class UltraCanvasBaseApplication {
    protected:
        bool volatile running = false;
        bool volatile initialized = false;

        std::vector<UltraCanvasWindow*> windows;
    public:
        virtual ~UltraCanvasBaseApplication() = default;
        virtual void RegisterWindow(UltraCanvasWindow* window) = 0;
        virtual void UnregisterWindow(UltraCanvasWindow* window) = 0;

        virtual bool Initialize() = 0;
        virtual void Run() = 0;
        virtual void Exit() = 0;

        bool IsInitialized() const { return initialized; }
        bool IsRunning() const { return running; }

    };
}

#endif