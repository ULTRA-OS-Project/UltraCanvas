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
        static std::unique_ptr<UltraCanvasApplication> instance;
    public:
        UltraCanvasApplication() : UltraCanvasNativeApplication() {
        }

        static UltraCanvasApplication* GetInstance() {
            if (!instance) {
                instance = std::make_unique<UltraCanvasApplication>();
            }
            return instance.get();
        };
        virtual void SetGlobalEventHandler(std::function<bool(const UCEvent&)> handler);
    };

} // namespace UltraCanvas
#endif