// include/UltraCanvasWindowBase.h
// Enhanced abstract base window interface inheriting from UltraCanvasContainer
// Version: 2.1.0 - cross-platform HiDPI/deviceScale scaling in base
// Last Modified: 2026-07-03
// Author: UltraCanvas Framework

#pragma once

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasNativeHandle.h"
#include "UltraCanvasEvent.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasDirtyRectManager.h"
#include <string>
#include <functional>
#include <memory>
#include <vector>
#include <unordered_set>
#include <cmath>

namespace UltraCanvas {
    class UltraCanvasWindowBase;

// ===== WINDOW CONFIGURATION =====
    enum class WindowType {
        Standard, Dialog, Popup, Tool, 
        Fullscreen, Borderless, Overlay
    };

    enum class WindowState {
        Normal, Minimized, Maximized, Fullscreen, Hidden, Closing, Closed
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

        UltraCanvasWindowBase* parentWindow = nullptr;
        bool modal = false;
        bool deleteOnClose = true;

        // Container-specific window settings
        bool enableWindowScrolling = false;
        bool autoResizeToContent = false;

        // Window icon (empty = use application default)
        std::string iconPath;

        // Window class suffix (Windows only; empty = use "Main" default class)
        // Examples: "Dialog", "Tool", "Popup"
        std::string windowClassSuffix;
    };

    struct PopupElement {
        UltraCanvasUIElement* element;
        PopupElementSettings settings;
        UltraCanvasDirtyRectManager dirtyRectManager;
    };

    struct FilterFunction {
        std::string id;
        std::function<bool(const UCEvent&)> func;
    };

// ===== ENHANCED BASE WINDOW (INHERITS FROM CONTAINER) =====
    class UltraCanvasWindowBase : public UltraCanvasContainer {
    friend UltraCanvasApplicationBase;
    protected:
//        std::unique_ptr<UltraCanvasSelectiveRenderer> selectiveRenderer = nullptr;
//        bool useSelectiveRendering = false;
        WindowConfig config_;
        WindowState _state = WindowState::Normal;
        bool _created = false;
        bool _windowVisible = false;
        bool _needsResize = false;
        bool _needsPopupGeometry = false;
        bool _needsWindowComposition = true;

        UltraCanvasDirtyRectManager dirtyRectManager;

        std::unordered_map<UCEventType, std::vector<FilterFunction>> eventFilters = {};
        bool HandleEventFilters(const UCEvent& ev);

        virtual bool CreateNative() = 0;
        virtual void DestroyNative() = 0;
        virtual void DoResizeNative() = 0;


        std::list<PopupElement> popupElements = {};

        UltraCanvasUIElement* _focusedElement = nullptr;  // Current focused element in this window

        UCMouseCursor currentMouseCursor = UCMouseCursor::Default;

        NativeSurfacePtr nativeSurface;

        // ===== HIDPI / DPI-AWARE SCALING =====
        // Logical→physical multiplier for the display this window is on.
        // 1.0 = standard, 2.0 = Retina/200%, 1.5 = 150%, etc. config_.width/height
        // stay LOGICAL on every platform; nativeSurface + all renderContexts are
        // built at PHYSICAL px (logical × deviceScale) via cairo device scale.
        float deviceScale = 1.0f;

        // OS override: query the live native backing scale for this window
        // (macOS: [nsWindow backingScaleFactor]; Windows: GetDpiForWindow()/96;
        // Linux: env / Xft.dpi / XRandR per-monitor DPI). Default 1.0 keeps
        // non-HiDPI back-ends correct.
        virtual float QueryNativeDeviceScale() const { return 1.0f; }

        // OS override: (re)build nativeSurface at the current deviceScale (physical
        // pixel dimensions + cairo_surface_set_device_scale). Returns false on
        // failure. Called by DoResizeNative() and HandleDeviceScaleChange().
        virtual bool RecreateNativeSurface() = 0;

    public:
        // Window-specific callbacks
        std::function<bool()> onWindowClosing;
        std::function<void()> onWindowClosed;
        std::function<void(int, int)> onWindowResize;
        std::function<void(int, int)> onWindowMove;
        std::function<void()> onWindowFocus;
        std::function<void()> onWindowBlur;
        std::function<void()> onWindowMinimize;
        std::function<void()> onWindowMaximize;
        std::function<void()> onWindowRestore;
        std::function<void()> onWindowShow;
        std::function<void()> onWindowHide;

        // ===== CONSTRUCTOR & DESTRUCTOR =====
        UltraCanvasWindowBase();
        virtual ~UltraCanvasWindowBase();

        NativeSurfacePtr GetNativeSurface() { return nativeSurface; };

        // ===== HIDPI / DPI-AWARE SCALING (shared) =====
        // Current logical→physical multiplier for this window's display.
        float GetDeviceScale() const { return deviceScale; }

        // Re-read the native scale via QueryNativeDeviceScale(); store it when
        // valid (>0); returns true when it changed from the previous value.
        bool RefreshDeviceScale();

        // Shared DPI-change orchestration: rebuilds nativeSurface, renderContext,
        // and popup/tooltip contexts at the new deviceScale, then re-lays-out and
        // repaints. Call after RefreshDeviceScale() reports a change.
        void HandleDeviceScaleChange();

        // Logical <-> physical pixel conversions using deviceScale. Round (not
        // truncate) so fractional scales map back cleanly.
        int LogicalToPhysical(int v) const { return static_cast<int>(std::lround(v * deviceScale)); }
        int PhysicalToLogical(int v) const {
            return static_cast<int>(std::lround(v / (deviceScale > 0.0f ? deviceScale : 1.0f)));
        }
        Size2Di  LogicalToPhysical(const Size2Di& s)  const { return { LogicalToPhysical(s.width),  LogicalToPhysical(s.height) }; }
        Size2Di  PhysicalToLogical(const Size2Di& s)  const { return { PhysicalToLogical(s.width),  PhysicalToLogical(s.height) }; }
        Point2Di PhysicalToLogical(const Point2Di& p) const { return { PhysicalToLogical(p.x),      PhysicalToLogical(p.y) }; }
        Point2Di LogicalToPhysical(const Point2Di& p) const { return { LogicalToPhysical(p.x),      LogicalToPhysical(p.y) }; }

        // Weak reference to this window as UltraCanvasWindowBase. Returns an empty
        // weak_ptr when the window is not (yet) owned by a shared_ptr. Used to store
        // non-owning references (event.targetWindow, application focusedWindow) that
        // must not dangle when the window is destroyed.
        std::weak_ptr<UltraCanvasWindowBase> GetWindowWeakPtr() {
            return std::static_pointer_cast<UltraCanvasWindowBase>(weak_from_this().lock());
        }

        // Window lifecycle
        bool Create(const WindowConfig& config);
        virtual bool Create();
        virtual bool Close();
        virtual void PerformClose();

        UCMouseCursor GetCurrentMouseCursor() { return currentMouseCursor; };
        bool SelectMouseCursor(UCMouseCursor ptr);
        bool SelectMouseCursor(UCMouseCursor ptr, const char* filename, int hotspotX, int hotspotY);

        // Sample the colour of the window pixel at (x, y) in LOGICAL window
        // coordinates from the window's rendered surface (the last presented
        // frame). Used by colour-picking tools (eyedropper). Returns false when
        // no surface exists or the position cannot be read.
        virtual bool GetPixelColor(int x, int y, Color& out);

        bool IsWindowVisible() { return _windowVisible; }
        virtual void Show() = 0;
        virtual void Hide() = 0;
        // Window state
        virtual void RaiseAndFocus() = 0;
        virtual void SetWindowTitle(const std::string& title) = 0;
        virtual void SetWindowIcon(const std::string& iconPath) = 0;
        virtual void SetWindowPosition(int x, int y) = 0;
        virtual void SetWindowSize(int width, int height) = 0;

        // Window operations
        virtual void Minimize() = 0;
        virtual void Maximize() = 0;
        virtual void Restore() = 0;
        virtual void SetFullscreen(bool fullscreen) = 0;
        virtual void SetResizable(bool resizable) = 0;
        virtual void GetScreenSize(int& width, int& height) const = 0;

        // Full bounds of the screen/monitor most relevant to this window (origin x/y
        // plus width/height, in the same coordinate space that SetWindowPosition uses).
        // When the native window exists, returns the monitor it currently resides on;
        // otherwise returns the primary monitor. Default implementation delegates to
        // GetScreenSize with a (0,0) origin, which is correct on single-monitor setups
        // but ignores secondary-monitor offsets — backends should override this when
        // they can query per-monitor bounds.
        virtual void GetScreenBounds(int& x, int& y, int& width, int& height) const;

// ===== FOCUS MANAGEMENT PUBLIC INTERFACE =====
        bool IsWindowFocused() const;
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

        void CleanupElementReferences(UltraCanvasUIElement* element);

        // Platform-specific
        virtual NativeWindowHandle GetNativeHandle() const = 0;
        virtual void InvalidateWindowNative() = 0;

        // ===== NATIVE FILE DRAG SOURCE =====
        // Start a native OS drag-and-drop of the given files out of this
        // window (into file managers, editors, other windows of this app,
        // ...). Must be called while a mouse button is held down — the usual
        // "pressed on an item and moved" gesture. Returns immediately; the
        // drag then runs inside the normal event loop and onFinished (when
        // given) reports the outcome: accepted = a target took the drop,
        // moved = the target chose the "move" action (it performs the file
        // operation itself; the source only needs to refresh its view).
        // Platforms without an implementation return false.
        virtual bool StartNativeFileDrag(const std::vector<std::string>& filePaths,
                                         std::function<void(bool accepted, bool moved)> onFinished = nullptr) {
            (void)filePaths; (void)onFinished;
            return false;
        }

        // Overlay elements
        void OpenPopup(const Point2Di& pos, UltraCanvasUIElement& element, const PopupElementSettings& settings);
        bool ClosePopup(UltraCanvasUIElement& element, ClosePopupReason reason=ClosePopupReason::Manual);
        PopupElement* GetActivePopupElement();
        void CloseAllPopups();

        // event filters
        void InstallEventFilter(const std::string& uniqueFilterId, const std::function<bool(const UCEvent&)>& filterFunc, const std::vector<UCEventType>& interestedEvents);
        void UnInstallWindowEventFilter(const std::string& uniqueFilterId);

        // ===== ENHANCED WINDOW PROPERTIES =====
        std::string GetWindowTitle() const { return config_.title; }

        /// Get the actual screen position of the window in native screen
        /// coordinates. Platform backends override this to query the live
        /// geometry (the cached config position can be stale on platforms that
        /// don't report window moves, e.g. X11); the default returns the last
        /// configured position.
        virtual void GetWindowPosition(int& x, int& y) const {
            x = config_.x;
            y = config_.y;
        }

        void GetWindowSize(int& w, int& h) const {
            w = config_.width;
            h = config_.height;
        }

        // Window outer size in the platform's NATIVE window-geometry space —
        // the space SetWindowPosition() and GetScreenBounds() operate in. On
        // X11/Windows that is PHYSICAL px (logical × deviceScale); on macOS the
        // window geometry is in logical points, so it equals the logical size
        // (macOS overrides this). Used by the CenterOn* helpers so the size and
        // screen-origin terms are in the same units on HiDPI displays.
        virtual void GetNativeWindowSize(int& w, int& h) const {
            w = LogicalToPhysical(config_.width);
            h = LogicalToPhysical(config_.height);
        }

        void DoResize();

        bool IsCreated() const { return _created; }
        bool IsMinimized() const { return _state == WindowState::Minimized; }
        bool IsMaximized() const { return _state == WindowState::Maximized; }
        bool IsFullscreen() const { return _state == WindowState::Fullscreen; }

        UltraCanvasWindowBase* GetParentWindow();

        WindowState GetState() const { return _state; }

        const WindowConfig& GetConfig() const { return config_; }

        virtual bool OnEvent(const UCEvent& event) override;

        // derived classes may override it to render something within the dirty area
        virtual void RenderCustomContent(IRenderContext* ctx, const Rect2Di& dirtyRect) {}

        // Adds a dirty rectangle (in window coords) to the window's render queue.
        void AddDirtyRectangle(const Rect2Di& windowRect);
        // Adds a dirty rectangle (in popup-local coords) to the named popup's queue.
        void AddPopupDirtyRect(UltraCanvasUIElement* popup, const Rect2Di& popupLocalRect);

        void RequestPopupGeometry() { _needsPopupGeometry = true; }
        void RequestWindowComposition() { _needsWindowComposition = true; }
        void UpdateAndRender();

        bool IsNeedsResize() const { return _needsResize; }

        // ===== UTILITY METHODS =====
        void CenterOnScreen();
        void CenterOnParent(UltraCanvasWindowBase* parent);
        // Center this window on the screen/monitor that the given reference window
        // currently resides on (not centered on the reference window itself).
        // Falls back to CenterOnScreen() when referenceWindow is null.
        void CenterOnScreenOfWindow(UltraCanvasWindowBase* referenceWindow);

        // Declare this window a transient child of `parent` (dialogs, alerts).
        // Records the parent in the config; platform backends additionally tell
        // the window manager (X11 WM_TRANSIENT_FOR) so the window is kept above
        // its parent and treated as belonging to the parent's monitor.
        virtual void SetTransientParent(UltraCanvasWindowBase* parent);

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
        bool HandleWindowEvent(const UCEvent &event);
//        virtual void HandleCloseEvent();
        void HandleResizeEvent(int width, int height);
        void HandleMoveEvent(int x, int y);

        // ===== PROTECTED HELPER METHODS =====
        virtual void RenderWindowBackground(IRenderContext* ctx) {
            // Default implementation - clear to background color
            // OS-specific implementations can override
        }

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
