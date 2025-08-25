// UltraCanvasBaseWindow.h
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

namespace UltraCanvas {

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

        void* parentWindow = nullptr;
        bool modal = false;

        // Container-specific window settings
        bool enableWindowScrolling = false;
        bool autoResizeToContent = false;
    };

// ===== ENHANCED BASE WINDOW (INHERITS FROM CONTAINER) =====
    class UltraCanvasBaseWindow : public UltraCanvasContainer {
    protected:
        WindowConfig config_;
        WindowState state_ = WindowState::Normal;
        bool created_ = false;
        bool visible_ = false;
        bool needsRedraw_ = true;
        UltraCanvasElement *activePopupElement = nullptr;

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

        // Platform-specific
        virtual void* GetNativeHandle() const = 0;
        virtual void SwapBuffers() = 0;

        void SetActivePopupElement(UltraCanvasElement* element) {
            if (!element) return;
            activePopupElement = element;
        }

        void ClearActivePopupElement(UltraCanvasElement* element) {
            if (!element) return;
            if (activePopupElement == element) {
                activePopupElement = nullptr;
            }
        }

        // ===== Z-ORDER MANAGEMENT =====
//        void BringElementToFront(UltraCanvasElement* element) {
//            if (!element) return;
//
//            // Find the element and move it to the end of children vector (top z-order)
//            auto& children = const_cast<std::vector<std::shared_ptr<UltraCanvasElement>>&>(GetChildren());
//            auto it = std::find_if(children.begin(), children.end(),
//                                   [element](const std::shared_ptr<UltraCanvasElement>& child) {
//                                       return child.get() == element;
//                                   });
//
//            if (it != children.end()) {
//                auto elementPtr = *it;
//                children.erase(it);
//                children.push_back(elementPtr);
//                needsRedraw_ = true;
//            }
//        }
//
//        void SendElementToBack(UltraCanvasElement* element) {
//            if (!element) return;
//
//            // Find the element and move it to the beginning of children vector (bottom z-order)
//            auto& children = const_cast<std::vector<std::shared_ptr<UltraCanvasElement>>&>(GetChildren());
//            auto it = std::find_if(children.begin(), children.end(),
//                                   [element](const std::shared_ptr<UltraCanvasElement>& child) {
//                                       return child.get() == element;
//                                   });
//
//            if (it != children.end()) {
//                auto elementPtr = *it;
//                children.erase(it);
//                children.insert(children.begin(), elementPtr);
//                needsRedraw_ = true;
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

        bool IsVisible() const { return visible_; }
        bool IsMinimized() const { return state_ == WindowState::Minimized; }
        bool IsMaximized() const { return state_ == WindowState::Maximized; }
        bool IsFullscreen() const { return state_ == WindowState::Fullscreen; }
        WindowState GetState() const { return state_; }

        const WindowConfig& GetConfig() const { return config_; }

        // ===== ENHANCED RENDERING AND EVENTS =====
        virtual void Render() override;
        virtual bool OnEvent(const UCEvent& event) override;
        virtual void RenderCustomContent() {}

        bool NeedsRedraw() const { return needsRedraw_; }
        void SetNeedsRedraw(bool val) { needsRedraw_ = val; }

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
        bool HandleWindowEvent(const UCEvent &event);
        void HandleCloseEvent();
        void HandleResizeEvent(int width, int height);
        void HandleMoveEvent(int x, int y);
        void HandleFocusEvent(bool focused);

        // ===== PROTECTED HELPER METHODS =====
        void UpdateWindowSize(int width, int height) {
            config_.width = width;
            config_.height = height;
            SetSize(width, height); // Update container size

            if (onWindowResize) {
                onWindowResize(width, height);
            }

            needsRedraw_ = true;
        }

        void UpdateWindowPosition(int x, int y) {
            config_.x = x;
            config_.y = y;
            SetPosition(x, y);

            if (onWindowMove) {
                onWindowMove(x, y);
            }
        }

        void SetWindowState(WindowState newState) {
            if (state_ != newState) {
                WindowState oldState = state_;
                state_ = newState;

                // Trigger appropriate callbacks
                switch (newState) {
                    case WindowState::Minimized:
                        if (onWindowMinimize) onWindowMinimize();
                        break;
                    case WindowState::Maximized:
                        if (onWindowMaximize) onWindowMaximize();
                        break;
                    case WindowState::Normal:
                        if (oldState == WindowState::Minimized || oldState == WindowState::Maximized) {
                            if (onWindowRestore) onWindowRestore();
                        }
                        break;
                    default:
                        break;
                }
            }
        }

        void RenderWindowBackground() {
            // Default implementation - clear to background color
            // OS-specific implementations can override
        }

        void RenderWindowChrome() {
            // Default implementation - no chrome
            // OS-specific implementations can add title bars, etc.
        }

    };

} // namespace UltraCanvas