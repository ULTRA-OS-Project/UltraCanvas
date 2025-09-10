// include/UltraCanvasWindow.h
// Cross-platform window management system with comprehensive features
// Version: 1.1.3
// Last Modified: 2025-09-04
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderContext.h"
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

        static bool HasNativeWindowDecorations() {
#if defined(__linux__) || defined(_WIN32) || defined(_WIN64) || (defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IPHONE)
            return true;
#else
            return false; // Mobile and web platforms typically don't have traditional window decorations
#endif
        }

        static bool SupportsMultipleWindows() {
#if defined(__linux__) || defined(_WIN32) || defined(_WIN64) || (defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IPHONE)
            return true;
#elif defined(__EMSCRIPTEN__)
            return false; // Web typically single window
#else
            return false; // Mobile platforms typically single window
#endif
        }
        // Platform-specific feature detection
        static bool SupportsOpenGL() {
#if defined(__linux__) || defined(_WIN32) || defined(_WIN64) || defined(__APPLE__) || defined(__ANDROID__)
            return true;
#else
            return false;
#endif
        }

        static bool SupportsVulkan() {
#if defined(__linux__) || defined(_WIN32) || defined(_WIN64) || defined(__ANDROID__)
            return true; // macOS would need MoltenVK
#else
            return false;
#endif
        }

        static bool SupportsMetal() {
#if defined(__APPLE__)
            return true;
#else
            return false;
#endif
        }

        static bool SupportsDirectX() {
#if defined(_WIN32) || defined(_WIN64)
            return true;
#else
            return false;
#endif
        }

        static bool SupportsWebGL() {
#ifdef __EMSCRIPTEN__
            return true;
#else
            return false;
#endif
        }

    };
} // namespace UltraCanvas