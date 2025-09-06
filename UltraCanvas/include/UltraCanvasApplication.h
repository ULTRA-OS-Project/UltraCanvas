// include/UltraCanvasApplication.h
// Main UltraCanvas App - Complete Cross-Platform Support
// Version: 2.1.3
// Last Modified: 2025-09-04
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_APPLICATION_H
#define ULTRACANVAS_APPLICATION_H

// ===== PLATFORM-SPECIFIC HEADERS =====
#ifdef __linux__
    #include "../OS/Linux/UltraCanvasLinuxApplication.h"
    #define UltraCanvasNativeApplication UltraCanvasLinuxApplication
#elif defined(_WIN32) || defined(_WIN64)
    #include "../OS/MSWindows/UltraCanvasWindowsApplication.h"
    #define UltraCanvasNativeApplication UltraCanvasWindowsApplication
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_MAC && !TARGET_OS_IPHONE
        // macOS
        #include "../OS/MacOS/UltraCanvasMacOSApplication.h"
        #define UltraCanvasNativeApplication UltraCanvasMacOSApplication
    #elif TARGET_OS_IPHONE
        // iOS
        #include "../OS/iOS/UltraCanvasiOSApplication.h"
        #define UltraCanvasNativeApplication UltraCanvasiOSApplication
    #else
        #error "Unsupported Apple platform"
    #endif
#elif defined(__ANDROID__)
    #include "../OS/Android/UltraCanvasAndroidApplication.h"
    #define UltraCanvasNativeApplication UltraCanvasAndroidApplication
#elif defined(__EMSCRIPTEN__)
    // Web/WASM
    #include "../OS/Web/UltraCanvasWebApplication.h"
    #define UltraCanvasNativeApplication UltraCanvasWebApplication
#elif defined(__unix__) || defined(__unix)
    // Generic Unix (FreeBSD, OpenBSD, etc.)
    #include "../OS/Unix/UltraCanvasUnixApplication.h"
    #define UltraCanvasNativeApplication UltraCanvasUnixApplication
#else
    #error "No supported platform defined. Supported platforms: Linux, Windows, macOS, iOS, Android, Web/WASM, Unix"
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

        // Platform identification
        static std::string GetPlatformName() {
#ifdef __linux__
            return "Linux";
#elif defined(_WIN32) || defined(_WIN64)
            return "Windows";
#elif defined(__APPLE__)
    #if TARGET_OS_MAC && !TARGET_OS_IPHONE
            return "macOS";
    #elif TARGET_OS_IPHONE
        #if TARGET_OS_SIMULATOR
            return "iOS Simulator";
        #else
            return "iOS";
        #endif
    #endif
#elif defined(__ANDROID__)
            return "Android";
#elif defined(__EMSCRIPTEN__)
            return "Web/WASM";
#elif defined(__unix__) || defined(__unix)
            return "Unix";
#else
            return "Unknown";
#endif
        }

        // Platform capabilities
        static bool IsDesktopPlatform() {
#if defined(__linux__) || defined(_WIN32) || defined(_WIN64) || (defined(__APPLE__) && TARGET_OS_MAC && !TARGET_OS_IPHONE) || defined(__unix__)
            return true;
#else
            return false;
#endif
        }

        static bool IsMobilePlatform() {
#if defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
            return true;
#else
            return false;
#endif
        }

        static bool IsWebPlatform() {
#ifdef __EMSCRIPTEN__
            return true;
#else
            return false;
#endif
        }

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

        virtual void RunInEventLoop() override;
    };

} // namespace UltraCanvas
#endif