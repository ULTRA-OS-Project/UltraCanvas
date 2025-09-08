// UltraCanvasBaseWindow.h
// Enhanced abstract base window interface inheriting from UltraCanvasContainer
// Version: 2.0.0
// Last Modified: 2025-08-24
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasSelectiveRenderer.h"
#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <unordered_set>

namespace UltraCanvas {
    class UltraCanvasWindow;

// ===== WINDOW CONFIGURATION =====
    enum class WindowType {
        Standard, Dialog, Popup, Tool, Splash,
        Fullscreen, Borderless, Overlay
    };

    enum class WindowState {
        Normal, Minimized, Maximized, Fullscreen, Hidden, Closing
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
        bool alwaysOnTop = false;

        Color backgroundColor = Colors::WindowBackground;
        int minWidth = 200;
        int minHeight = 150;
        int maxWidth = -1; // -1 = no limit
        int maxHeight = -1;
        float opacity = 1.0f;

        UltraCanvasWindow* parentWindow = nullptr;
        bool modal = false;

        // Container-specific window settings
        bool enableWindowScrolling = false;
        bool autoResizeToContent = false;
    };

// ===== ENHANCED BASE WINDOW (INHERITS FROM CONTAINER) =====
    class UltraCanvasBaseWindow : public UltraCanvasContainer {
    protected:
//        std::unique_ptr<UltraCanvasSelectiveRenderer> selectiveRenderer = nullptr;
//        bool useSelectiveRendering = false;

        WindowConfig config_;
        WindowState _state = WindowState::Normal;
        bool _created = false;
        bool _visible = false;
        bool _focused = false;
        bool _needsRedraw = true;

        std::unordered_set<UltraCanvasElement *> activePopups;
        UltraCanvasElement* _focusedElement = nullptr;  // Current focused element in this window

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
        UltraCanvasBaseWindow(const WindowConfig& config = WindowConfig());

        virtual ~UltraCanvasBaseWindow() = default;

        // ===== PURE VIRTUAL INTERFACE =====
        // Window lifecycle
        virtual bool Create(const WindowConfig& config) = 0;
        virtual void Destroy() = 0;
        virtual void Show() = 0;
        virtual void Hide() = 0;
        virtual void Close() = 0;

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
        virtual void SetFocusedElement(UltraCanvasElement* element);
        // Get the currently focused element in this window
        UltraCanvasElement* GetFocusedElement() const { return _focusedElement; }

        // Clear focus from current element
        virtual void ClearFocus();

        // Focus navigation within window
        virtual void FocusNextElement();
        virtual void FocusPreviousElement();

        // Check if this window has focus (any element focused)
        bool HasFocus() const { return _focusedElement != nullptr; }
        // Internal method for elements to request focus (called by element's SetFocus)
        virtual bool RequestElementFocus(UltraCanvasElement* element);


        // Platform-specific
        virtual unsigned long GetNativeHandle() const = 0;
        virtual void Flush() = 0;

        void AddPopupElement(UltraCanvasElement* element);

        // Unregister popup element
        void RemovePopupElement(UltraCanvasElement* element);

        std::unordered_set<UltraCanvasElement *>& GetActivePopups() { return activePopups; }

//        void SetActivePopupElement(UltraCanvasElement* element) {
//            if (!element) return;
//            activePopupElement = element;
//        }
//
//        void ClearActivePopupElement(UltraCanvasElement* element) {
//            if (!element) return;
//            if (activePopupElement == element) {
//                activePopupElement = nullptr;
//            }
//        }

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

        bool IsVisible() const { return _visible; }
        bool IsMinimized() const { return _state == WindowState::Minimized; }
        bool IsMaximized() const { return _state == WindowState::Maximized; }
        bool IsFullscreen() const { return _state == WindowState::Fullscreen; }
        WindowState GetState() const { return _state; }

        const WindowConfig& GetConfig() const { return config_; }

        // ===== ENHANCED RENDERING AND EVENTS =====
        virtual void Render() override;
        virtual bool OnEvent(const UCEvent& event) override;
        virtual void RenderCustomContent() {}

        bool NeedsRedraw() const { return _needsRedraw; }
        void RequestRedraw(bool val) { _needsRedraw = val; }
//        void RequestFullRedraw() { useSelectiveRendering = false; _needsRedraw = true; }
        void MarkElementDirty(UltraCanvasElement* element, bool isOverlay = false);
//        bool IsSelectiveRenderingActive();
        virtual IRenderContext* GetRenderContext() const = 0;

        // ===== ENHANCED WINDOW CALLBACKS =====
        void SetWindowCloseCallback(std::function<void()> callback) { onWindowClose = callback; }
        void SetWindowResizeCallback(std::function<void(int, int)> callback) { onWindowResize = callback; }
        void SetWindowMoveCallback(std::function<void(int, int)> callback) { onWindowMove = callback; }
        void SetWindowMinimizeCallback(std::function<void()> callback) { onWindowMinimize = callback; }
        void SetWindowMaximizeCallback(std::function<void()> callback) { onWindowMaximize = callback; }
        void SetWindowRestoreCallback(std::function<void()> callback) { onWindowRestore = callback; }

        // ===== UTILITY METHODS =====
        void CenterOnScreen();
        void CenterOnParent(UltraCanvasBaseWindow* parent);

        // Method chaining for fluent interface
        UltraCanvasBaseWindow& Title(const std::string& title) {
            SetWindowTitle(title);
            return *this;
        }

        UltraCanvasBaseWindow& Size(int w, int h) {
            SetWindowSize(w, h);
            return *this;
        }

        UltraCanvasBaseWindow& Position(int x, int y) {
            SetWindowPosition(x, y);
            return *this;
        }

        // ===== DEBUG METHODS =====
        void DebugPrintElements();
        std::string GetElementTypeName(UltraCanvasElement* element);

    protected:
        virtual bool HandleWindowEvent(const UCEvent &event);
        virtual void HandleCloseEvent();
        virtual void HandleResizeEvent(int width, int height);
        virtual void HandleMoveEvent(int x, int y);
        virtual void HandleFocusEvent(bool focused);

        // ===== PROTECTED HELPER METHODS =====
//        void UpdateWindowSize(int width, int height) {
//            config_.width = width;
//            config_.height = height;
//            SetSize(width, height); // Update container size
//
//            if (onWindowResize) {
//                onWindowResize(width, height);
//            }
//
//            _needsRedraw = true;
//        }
//
//        void UpdateWindowPosition(int x, int y) {
//            config_.x = x;
//            config_.y = y;
//            SetPosition(x, y);
//
//            if (onWindowMove) {
//                onWindowMove(x, y);
//            }
//        }

//        void SetWindowState(WindowState newState) {
//            if (_state != newState) {
//                WindowState oldState = _state;
//                _state = newState;
//
//                // Trigger appropriate callbacks
//                switch (newState) {
//                    case WindowState::Minimized:
//                        if (onWindowMinimize) onWindowMinimize();
//                        break;
//                    case WindowState::Maximized:
//                        if (onWindowMaximize) onWindowMaximize();
//                        break;
//                    case WindowState::Normal:
//                        if (oldState == WindowState::Minimized || oldState == WindowState::Maximized) {
//                            if (onWindowRestore) onWindowRestore();
//                        }
//                        break;
//                    default:
//                        break;
//                }
//            }
//        }

        virtual void RenderWindowBackground() {
            // Default implementation - clear to background color
            // OS-specific implementations can override
        }

        virtual void RenderWindowChrome() {
            // Default implementation - no chrome
            // OS-specific implementations can add title bars, etc.
        }
        void RenderActivePopups();

        // ===== FOCUS UTILITY METHODS =====

        // Get all focusable elements in this window (recursive search)
        std::vector<UltraCanvasElement*> GetFocusableElements();

        // Collect focusable elements from a container recursively
        void CollectFocusableElements(UltraCanvasContainer* container,
                                      std::vector<UltraCanvasElement*>& elements);

        // Find next/previous focusable element in tab order
        UltraCanvasElement* FindNextFocusableElement(UltraCanvasElement* current);
        UltraCanvasElement* FindPreviousFocusableElement(UltraCanvasElement* current);

        // Send focus events to elements
        void SendFocusGainedEvent(UltraCanvasElement* element);
        void SendFocusLostEvent(UltraCanvasElement* element);
    };

} // namespace UltraCanvas