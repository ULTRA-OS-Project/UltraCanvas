// UltraCanvasBaseWindow.h
// Fixed abstract base window interface for cross-platform windowing
// Version: 1.2.0
// Last Modified: 2025-07-15
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasUIElement.h"
#include <string>
#include <functional>
#include <memory>
#include <vector>

namespace UltraCanvas {

// Forward declaration
    class UltraCanvasElement;
    class UltraCanvasApplication;

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
        int x = -1; // -1 = use WindowPosition
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
    };

// ===== ABSTRACT BASE WINDOW =====
    class UltraCanvasBaseWindow {
    protected:
        UltraCanvasApplication* application = nullptr;
        WindowConfig config_;
        WindowState state_ = WindowState::Normal;
        bool created_ = false;
        bool visible_ = false;
        bool needsRedraw_ = true;

        // UI element management
        std::vector<UltraCanvasElement*> elements;
        std::vector<std::shared_ptr<UltraCanvasElement>> sharedElements; // Keep shared_ptrs alive

    public:
        virtual ~UltraCanvasBaseWindow() = default;

        // ===== PURE VIRTUAL INTERFACE =====
        // Window lifecycle
        virtual bool Create(const WindowConfig& config) = 0;
        virtual void Destroy() = 0;
        virtual void Show() = 0;
        virtual void Hide() = 0;
        virtual void Close() = 0;

        // Window state
        virtual void SetTitle(const std::string& title) = 0;
        virtual void SetPosition(int x, int y) = 0;
        virtual void SetSize(int width, int height) = 0;

        // Window operations
        virtual void Minimize() = 0;
        virtual void Maximize() = 0;
        virtual void Restore() = 0;
        virtual void SetFullscreen(bool fullscreen) = 0;
        virtual void SetResizable(bool resizable) = 0;

        void BringElementToFront(UltraCanvasElement* element);
        void SendElementToBack(UltraCanvasElement* element);

        // Rendering and events
        virtual bool OnEvent(const UCEvent& event);
        virtual void* GetNativeHandle() const = 0;
        virtual void SwapBuffers() = 0;
        virtual void Render();
        virtual void RenderCustomContent()  {};

        bool NeedsRedraw() const { return needsRedraw_; }
        void SetNeedsRedraw(bool val) { needsRedraw_ = val;}

        // ===== IMPLEMENTED INTERFACE =====
        std::string GetTitle() { return config_.title; }
        void GetPosition(int& x, int& y) { x = config_.x, y = config_.y; };
        void GetSize(int& w, int& h) { w = config_.width, h = config_.height; };
        bool IsVisible() { return visible_; }
        bool IsMinimized() { return state_ == WindowState::Minimized; }
        bool IsMaximized() { return state_ == WindowState::Maximized; }
        bool IsFullscreen() { return state_ == WindowState::Fullscreen; }
        WindowState GetState() { return state_; }
        void SetApplication(UltraCanvasApplication* application);

        // ===== ELEMENT MANAGEMENT =====
        void AddElement(UltraCanvasElement* element);
        void AddElement(std::shared_ptr<UltraCanvasElement> element);
        void RemoveElement(UltraCanvasElement* element);
        void RemoveElement(std::shared_ptr<UltraCanvasElement> element);
        void ClearElements();
        UltraCanvasElement* FindElementById(const std::string& id);

        const std::vector<UltraCanvasElement*>& GetElements() const { return elements; }

        // ===== EVENT CALLBACKS =====
        std::function<void()> onWindowClosing;
        std::function<void(int width, int height)> onWindowResized;
        std::function<void(int x, int y)> onWindowMoved;
        std::function<void()> onWindowFocused;
        std::function<void()> onWindowBlurred;
        std::function<void()> onWindowShown;
        std::function<void()> onWindowHidden;
        std::function<void(UltraCanvasElement*)> onElementAdded;
        std::function<void(UltraCanvasElement*)> onElementRemoved;

    protected:
        // Helper for callbacks
        template<typename Func, typename... Args>
        void TriggerCallback(const Func& callback, Args&&... args) {
            if (callback) {
                callback(std::forward<Args>(args)...);
            }
        }

        std::string GetElementTypeName(UltraCanvasElement* element);
        void DebugPrintElements();
        void RenderMinimalDebug();  // Alternative render with less debug output
    };

} // namespace UltraCanvas