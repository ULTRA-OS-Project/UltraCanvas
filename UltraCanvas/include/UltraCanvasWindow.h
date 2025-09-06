// include/UltraCanvasWindow.h
// Cross-platform window management system with comprehensive features
// Version: 1.1.3
// Last Modified: 2025-09-04
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderInterface.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasBaseWindow.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <chrono>

// ===== PLATFORM-SPECIFIC NATIVE IMPLEMENTATIONS =====

// Include platform-specific headers based on build configuration
#ifdef __linux__
#include "../OS/Linux/UltraCanvasLinuxWindow.h"
namespace UltraCanvas {
    using UltraCanvasNativeWindow = UltraCanvasLinuxWindow;
}
#elif defined(_WIN32) || defined(_WIN64)
#include "../OS/MSWindows/UltraCanvasWindowsWindow.h"
    namespace UltraCanvas {
        using UltraCanvasNativeWindow = UltraCanvasWindowsWindow;
    }
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_MAC && !TARGET_OS_IPHONE
        // macOS
        #include "../OS/MacOS/UltraCanvasMacOSWindow.h"
        namespace UltraCanvas {
            using UltraCanvasNativeWindow = UltraCanvasMacOSWindow;
        }
    #elif TARGET_OS_IPHONE
        // iOS
        #include "../OS/iOS/UltraCanvasiOSWindow.h"
        namespace UltraCanvas {
            using UltraCanvasNativeWindow = UltraCanvasiOSWindow;
        }
    #else
        #error "Unsupported Apple platform"
    #endif
#elif defined(__ANDROID__)
    #include "../OS/Android/UltraCanvasAndroidWindow.h"
    namespace UltraCanvas {
        using UltraCanvasNativeWindow = UltraCanvasAndroidWindow;
    }
#elif defined(__EMSCRIPTEN__)
    // Web/WASM
    #include "../OS/Web/UltraCanvasWebWindow.h"
    namespace UltraCanvas {
        using UltraCanvasNativeWindow = UltraCanvasWebWindow;
    }
#elif defined(__unix__) || defined(__unix)
    // Generic Unix (FreeBSD, OpenBSD, etc.)
    #include "../OS/Unix/UltraCanvasUnixWindow.h"
    namespace UltraCanvas {
        using UltraCanvasNativeWindow = UltraCanvasUnixWindow;
    }
#else
#error "No supported platform defined. Supported platforms: Linux, Windows, macOS, iOS, Android, Web/WASM, Unix"
#endif

// ===== PUBLIC CROSS-PLATFORM WINDOW CLASS =====
namespace UltraCanvas {

    class UltraCanvasWindow : public UltraCanvasNativeWindow {
    public:
        UltraCanvasWindow() : UltraCanvasNativeWindow() { SetWindow(this); };

        explicit UltraCanvasWindow(const WindowConfig &config) :
                UltraCanvasNativeWindow(config)
        {
            SetWindow(this);
            if (!Create(config)) {
                throw std::runtime_error("UltraCanvasWindow Create failed");
            }
        }

        virtual ~UltraCanvasWindow() {
            std::cout << "~UltraCanvasWindow(): Destroying window..." << std::endl;
            Destroy();
        }

        virtual bool Create(const WindowConfig& config) override;
        virtual void Destroy() override;
    };
} // namespace UltraCanvas