// UltraCanvasApplication.h
// Main UltraCanvas App
// Version: 2.1.2
// Last Modified: 2025-01-07
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_APPLICATION_H
#define ULTRACANVAS_APPLICATION_H

// ===== PLATFORM-SPECIFIC HEADERS (NOT .core files) =====
#ifdef __linux__
    #include "../OS/Linux/UltraCanvasLinuxApplication.h"
    #define UltraCanvasNativeApplication UltraCanvasLinuxApplication
//    #include "../OS/Linux/UltraCanvasLinuxIntegration.h"
//#elif defined(_WIN32)
//#include "../OS/Windows/UltraCanvasSupport.h"
//    #include "../OS/MSWindows/UltraCanvasSupport.h"
//#elif defined(__APPLE__)
//#include "../OS/MacOS/UltraCanvasSupport.h"
#else
#error "No supported platform defined"
#endif

namespace UltraCanvas {

// ===== FRAMEWORK INITIALIZATION =====
    class UltraCanvasApplication : public UltraCanvasNativeApplication {
    private:
        static UltraCanvasApplication* instance;
    public:
        UltraCanvasApplication() : UltraCanvasNativeApplication() {
            UltraCanvasApplication::instance = this;
        }

        static UltraCanvasApplication* GetInstance() {
            return UltraCanvasApplication::instance;
        };

        virtual void RunInEventLoop() override;
    };

} // namespace UltraCanvas
#endif