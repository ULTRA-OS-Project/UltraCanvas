// include/UltraCanvasWindowBase.h
// Enhanced abstract base window interface inheriting from UltraCanvasContainer
// Version: 2.0.0
// Last Modified: 2025-08-24
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasContainer.h"
#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <unordered_set>

namespace UltraCanvas {
    class UltraCanvasWindowBase;

// ===== WINDOW CONFIGURATION =====
    enum class WindowType {
        Standard, Dialog, Popup, Tool, Splash,
        Fullscreen, Borderless, Overlay
    };

    enum class WindowState {
        Normal, Minimized, Maximized, Fullscreen, Hidden, Closing, DeleteRequested, Deleted
    };

    struct WindowConfig {
        std::string title = "UltraCanvas Window";
        int width = 800;
        int height = 600;
        int x = -1; // -1 = use system positioning
        int y = -1;

        WindowType type = WindowType::Standard;
        bool resizable = true;
        bool minimizable = true;
        bool maximizable = true;
        bool closable = true;
        bool deleteOnClose = false;
        bool alwaysOnTop = false;

        Color backgroundColor = Colors::WindowBackground;
        int minWidth = 200;
        int minHeight = 150;
        int maxWidth = -1; // -1 = no limit
        int maxHeight = -1;
        float opacity = 1.0f;

        UltraCanvasWindowBase* parentWindow = nullptr;
        bool modal = false;

        // Container-specific window settings
        bool enableWindowScrolling = false;
        bool autoResizeToContent = false;
    };

// ===== ENHANCED BASE WINDOW (INHERITS FROM CONTAINER) =====
    class UltraCanvasWindowBase : public UltraCanvasContainer, public std::enable_shared_from_this<UltraCanvasWindowBase> {
    protected:
//        std::unique_ptr<UltraCanvasSelectiveRenderer> selectiveRenderer = nullptr;
//        bool useSelectiveRendering = false;

        WindowConfig config_;
        WindowState _state = WindowState::Normal;
        bool _created = false;
        bool _focused = false;
        bool _needsRedraw = true;

        virtual bool CreateNative() = 0;
        virtual void DestroyNative() = 0;

        std::vector<UltraCanvasUIElement *> activePopups;
        std::unordered_set<UltraCanvasUIElement *> popupsToRemove;
        UltraCanvasUIElement* _focusedElement = nullptr;  // Current focused element in this window

        // Window-specific callbacks
        std::function<void()> onWindowClose;
        std::function<void(int, int)> onWindowResize;
        std::function<void(int, int)> onWindowMove;
        std::function<void()> onWindowMinimize;
        std::function<void()> onWindowMaximize;
        std::function<void()> onWindowRestore;
        std::function<void()> onWindowFocus;
        std::function<void()> onWindowBlur;
        std::function<void()> onWindowShow;
        std::function<void()> onWindowHide;

    public:
        // ===== CONSTRUCTOR & DESTRUCTOR =====
        UltraCanvasWindowBase();

        // ===== PURE VIRTUAL INTERFACE =====
        // Window lifecycle
        void Create(const WindowConfig& config);
        void Close();
        void Destroy();
        void RequestDelete();

        virtual void Show() = 0;
        virtual void Hide() = 0;
        // Window state
        virtual void SetWindowTitle(const std::string& title) = 0;
        virtual void SetWindowPosition(int x, int y) = 0;
        virtual void SetWindowSize(int width, int height) = 0;

        // Window operations
        virtual void Minimize() = 0;
        virtual void Maximize() = 0;
        virtual void Restore() = 0;
        virtual void SetFullscreen(bool fullscreen) = 0;
        virtual void SetResizable(bool resizable) = 0;
// ===== FOCUS MANAGEMENT PUBLIC INTERFACE =====
        bool IsWindowFocused() const { return _focused; }
        // Set focus to a specific element in this window
        virtual void SetFocusedElement(UltraCanvasUIElement* element);
        // Get the currently focused element in this window
        UltraCanvasUIElement* GetFocusedElement() const { return _focusedElement; }

        // Clear focus from current element
        virtual void ClearFocus();

        // Focus navigation within window
        virtual void FocusNextElement();
        virtual void FocusPreviousElement();

        // Check if this window has focus (any element focused)
        bool HasFocus() const { return _focusedElement != nullptr; }
        // Internal method for elements to request focus (called by element's SetFocus)
        virtual bool RequestElementFocus(UltraCanvasUIElement* element);


        // Platform-specific
        virtual unsigned long GetNativeHandle() const = 0;
        virtual void Flush() = 0;

        void AddPopupElement(UltraCanvasUIElement* element);

        // Unregister popup element
        void RemovePopupElement(UltraCanvasUIElement* element);
        void CleanupRemovedPopupElements();

        std::vector<UltraCanvasUIElement *>& GetActivePopups() { return activePopups; }
        bool HasActivePopups() { return !activePopups.empty(); }

        // ===== ENHANCED WINDOW PROPERTIES =====
        std::string GetWindowTitle() const { return config_.title; }

        void GetWindowPosition(int& x, int& y) const {
            x = config_.x;
            y = config_.y;
        }

        void GetWindowSize(int& w, int& h) const {
            w = config_.width;
            h = config_.height;
        }

        bool IsCreated() const { return _created; }
        bool IsMinimized() const { return _state == WindowState::Minimized; }
        bool IsMaximized() const { return _state == WindowState::Maximized; }
        bool IsFullscreen() const { return _state == WindowState::Fullscreen; }

        WindowState GetState() const { return _state; }

        const WindowConfig& GetConfig() const { return config_; }

        // ===== ENHANCED RENDERING AND EVENTS =====
        virtual void Render(IRenderContext* ctx) override;
        virtual bool OnEvent(const UCEvent& event) override;
        virtual void RenderCustomContent() {}

        bool IsNeedsRedraw() const { return _needsRedraw; }
        void RequestRedraw() { _needsRedraw = true; }
        void ClearRequestRedraw() { _needsRedraw = false; }

//        void RequestFullRedraw() { useSelectiveRendering = false; _needsRedraw = true; }
        void MarkElementDirty(UltraCanvasUIElement* element, bool isOverlay = false);
//        bool IsSelectiveRenderingActive();
        virtual IRenderContext* GetRenderContext() const = 0;

        // ===== ENHANCED WINDOW CALLBACKS =====
        void SetWindowCloseCallback(std::function<void()> callback) { onWindowClose = callback; }
        void SetWindowResizeCallback(std::function<void(int, int)> callback) { onWindowResize = callback; }
        void SetWindowMoveCallback(std::function<void(int, int)> callback) { onWindowMove = callback; }
        void SetWindowMinimizeCallback(std::function<void()> callback) { onWindowMinimize = callback; }
        void SetWindowMaximizeCallback(std::function<void()> callback) { onWindowMaximize = callback; }
        void SetWindowRestoreCallback(std::function<void()> callback) { onWindowRestore = callback; }
        void SetWindowBlurCallback(std::function<void()> callback) { onWindowBlur = callback; }
        void SetWindowFocusCallback(std::function<void()> callback) { onWindowFocus = callback; }

        // ===== UTILITY METHODS =====
        void CenterOnScreen();
        void CenterOnParent(UltraCanvasWindowBase* parent);

        // Method chaining for fluent interface
        UltraCanvasWindowBase& Title(const std::string& title) {
            SetWindowTitle(title);
            return *this;
        }

        UltraCanvasWindowBase& Size(int w, int h) {
            SetWindowSize(w, h);
            return *this;
        }

        UltraCanvasWindowBase& Position(int x, int y) {
            SetWindowPosition(x, y);
            return *this;
        }

        // ===== DEBUG METHODS =====
        void DebugPrintElements();
        std::string GetElementTypeName(UltraCanvasUIElement* element);

    protected:
        virtual bool HandleWindowEvent(const UCEvent &event);
//        virtual void HandleCloseEvent();
        virtual void HandleResizeEvent(int width, int height);
        virtual void HandleMoveEvent(int x, int y);
        virtual void HandleFocusEvent(bool focused);

        // ===== PROTECTED HELPER METHODS =====
        virtual void RenderWindowBackground(IRenderContext* ctx) {
            // Default implementation - clear to background color
            // OS-specific implementations can override
        }

        virtual void RenderWindowChrome(IRenderContext* ctx) {
            // Default implementation - no chrome
            // OS-specific implementations can add title bars, etc.
        }
        void RenderActivePopups(IRenderContext* ctx);

        // ===== FOCUS UTILITY METHODS =====

        // Get all focusable elements in this window (recursive search)
        std::vector<UltraCanvasUIElement*> GetFocusableElements();

        // Collect focusable elements from a container recursively
        void CollectFocusableElements(UltraCanvasContainer* container,
                                      std::vector<UltraCanvasUIElement*>& elements);

        // Find next/previous focusable element in tab order
        UltraCanvasUIElement* FindNextFocusableElement(UltraCanvasUIElement* current);
        UltraCanvasUIElement* FindPreviousFocusableElement(UltraCanvasUIElement* current);

        // Send focus events to elements
        void SendFocusGainedEvent(UltraCanvasUIElement* element);
        void SendFocusLostEvent(UltraCanvasUIElement* element);
    };

} // namespace UltraCanvas

#if defined(__linux__) || defined(__unix__) || defined(__unix)
#include "../OS/Linux/UltraCanvasLinuxWindow.h"
namespace UltraCanvas {
    using UltraCanvasWindow = UltraCanvasLinuxWindow;
}
#elif defined(_WIN32) || defined(_WIN64)
#include "../OS/MSWindows/UltraCanvasWindowsWindow.h"
    namespace UltraCanvas {
        using UltraCanvasWindow = UltraCanvasWindowsWindow;
    }
#elif defined(__APPLE__)
    #include <TargetConditionals.h>
    #if TARGET_OS_MAC && !TARGET_OS_IPHONE
        // macOS
        #include "../OS/MacOS/UltraCanvasMacOSWindow.h"
        namespace UltraCanvas {
            using UltraCanvasWindow = UltraCanvasMacOSWindow;
        }
    #elif TARGET_OS_IPHONE
        // iOS
        #include "../OS/iOS/UltraCanvasiOSWindow.h"
        namespace UltraCanvas {
            using UltraCanvasWindow = UltraCanvasiOSWindow;
        }
    #else
        #error "Unsupported Apple platform"
    #endif
#elif defined(__ANDROID__)
    #include "../OS/Android/UltraCanvasAndroidWindow.h"
    namespace UltraCanvas {
        using UltraCanvasWindow = UltraCanvasAndroidWindow;
    }
#elif defined(__WASM__)
    // Web/WASM
    #include "../OS/Web/UltraCanvasWebWindow.h"
    namespace UltraCanvas {
        using UltraCanvasWindow = UltraCanvasWebWindow;
    }
#else
#error "No supported platform defined. Supported platforms: Linux, Windows, macOS, iOS, Android, Web/WASM, Unix"
#endif

namespace UltraCanvas {
    std::shared_ptr<UltraCanvasWindow> CreateWindow(const WindowConfig& config);
    std::shared_ptr<UltraCanvasWindow> CreateWindow();
}
