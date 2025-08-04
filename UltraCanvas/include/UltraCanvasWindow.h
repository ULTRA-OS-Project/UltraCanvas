// UltraCanvasWindow.h
// Cross-platform window management system with comprehensive features
// Version: 1.1.1
// Last Modified: 2025-07-09
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasUIElement.h"
#include "UltraCanvasRenderInterface.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasEventDispatcher.h"
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
//#elif defined(ULTRACANVAS_PLATFORM_WAYLAND)
//    #include "OS/Linux/UltraCanvasNativeWaylandWindow.h"
//    namespace UltraCanvas {
//        using UltraCanvasNativeWindow = UltraCanvasNativeWaylandWindow;
//    }
//#elif defined(ULTRACANVAS_PLATFORM_WIN32)
//    #include "OS/MSWindows/UltraCanvasNativeWin32Window.h"
//    namespace UltraCanvas {
//        using UltraCanvasNativeWindow = UltraCanvasNativeWin32Window;
//    }
//#elif defined(ULTRACANVAS_PLATFORM_COCOA)
//    #include "OS/MacOS/UltraCanvasNativeCocoaWindow.h"
//    namespace UltraCanvas {
//        using UltraCanvasNativeWindow = UltraCanvasNativeCocoaWindow;
//    }
#else
#error "No supported platform defined"
#endif

// ===== PUBLIC CROSS-PLATFORM WINDOW CLASS =====
namespace UltraCanvas {

    class UltraCanvasWindow : public UltraCanvasNativeWindow {
    public:
        // Constructors
        UltraCanvasWindow() : UltraCanvasNativeWindow() {};

        virtual ~UltraCanvasWindow() {
            std::cout << "~UltraCanvasWindow(): Destroying window..." << std::endl;
            Destroy();
        }

        explicit UltraCanvasWindow(const WindowConfig &config) :
            UltraCanvasNativeWindow()
        {
            Create(config);
        }

        virtual bool Create(const WindowConfig& config) override;
        virtual void Destroy() override;

        // Factory methods for common window types
        static std::unique_ptr <UltraCanvasWindow> CreateDialog(
                const std::string &title, int width = 400, int height = 300,
                UltraCanvasWindow *parent = nullptr) {

            WindowConfig config;
            config.title = title;
            config.width = width;
            config.height = height;
            config.type = WindowType::Dialog;
            config.resizable = false;
            config.parentWindow = parent ? parent->GetNativeHandle() : nullptr;
            config.modal = true;

            auto window = std::make_unique<UltraCanvasWindow>(config);
            return window;
        }

        static std::unique_ptr <UltraCanvasWindow> CreateTool(
                const std::string &title, int width = 300, int height = 400) {

            WindowConfig config;
            config.title = title;
            config.width = width;
            config.height = height;
            config.type = WindowType::Tool;
            config.alwaysOnTop = true;

            auto window = std::make_unique<UltraCanvasWindow>(config);
            return window;
        }

        static std::unique_ptr <UltraCanvasWindow> CreateFullscreen(
                const std::string &title) {

            WindowConfig config;
            config.title = title;
            config.type = WindowType::Fullscreen;
            config.resizable = false;
            config.minimizable = false;
            config.maximizable = false;

            auto window = std::make_unique<UltraCanvasWindow>(config);
            return window;
        }

        // Enhanced convenience methods
        void CenterOnScreen() {
            // Implementation would use platform-specific screen size queries
            // This demonstrates how the public API can provide enhanced functionality
            // while delegating to the native implementation
        }

        void CenterOnParent(UltraCanvasWindow *parent) {
            if (!parent) return;

            int parentX, parentY, parentW, parentH;
            parent->GetPosition(parentX, parentY);
            parent->GetSize(parentW, parentH);

            int myW, myH;
            GetSize(myW, myH);

            SetPosition(
                    parentX + (parentW - myW) / 2,
                    parentY + (parentH - myH) / 2
            );
        }

        // Chaining methods for fluent interface
        UltraCanvasWindow &Title(const std::string &title) {
            SetTitle(title);
            return *this;
        }

        UltraCanvasWindow &Size(int width, int height) {
            SetSize(width, height);
            return *this;
        }

        UltraCanvasWindow &Position(int x, int y) {
            SetPosition(x, y);
            return *this;
        }

        UltraCanvasWindow &Resizable(bool resizable = true) {
            SetResizable(resizable);
            return *this;
        }

//        UltraCanvasWindow &AlwaysOnTop(bool onTop = true) {
//            SetAlwaysOnTop(onTop);
//            return *this;
//        }

//        // Advanced event handling
//        void SetEventHandler(std::function<bool(const UCEvent &)> handler) {
//            OnEvent = [handler](const UCEvent &event) {
//                handler(event);
//            };
//        }
//
//        template<typename T>
//        void SetEventHandler(T *instance, bool (T::*method)(const UCEvent &)) {
//            OnEvent = [instance, method](const UCEvent &event) {
//                (instance->*method)(event);
//            };
//        }
    };
}
