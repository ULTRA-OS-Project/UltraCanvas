// include/UltraCanvasEmbeddedWindowManager.h
// A window manager that runs inside the program: it keeps the windows of one
// output (a screen the program draws itself, with no window manager of the
// platform's to place them) stacked, focused, moved and resized, and
// composites their surfaces into that output with a pointer drawn on top.
// Used by the Android backend's desktop mode (MobilePC: UltraDesktop on a TV
// or monitor); platform-neutral, so any backend with one surface for many
// windows can use it.
// Version: 1.0.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_EMBEDDED_WINDOW_MANAGER_H
#define ULTRACANVAS_EMBEDDED_WINDOW_MANAGER_H

#include "UltraCanvasCommonTypes.h"

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

typedef struct _cairo_surface cairo_surface_t;
typedef struct _cairo cairo_t;

namespace UltraCanvas {

    // Bands of the stack, bottom first. A window is raised within its band
    // only: a dialog never covers a menu, a click on the desktop never brings
    // it above the windows.
    enum class EmbeddedWindowLayer : int {
        Desktop = 0,
        Normal = 1,
        AlwaysOnTop = 2,
        Notification = 3,
        Popup = 4,
        Tooltip = 5,
        Lock = 6
    };

    // What a point of the output is over.
    enum class EmbeddedWindowPart {
        Nothing,
        Content,
        ResizeLeft, ResizeRight, ResizeTop, ResizeBottom,
        ResizeTopLeft, ResizeTopRight, ResizeBottomLeft, ResizeBottomRight
    };

    // How a window joins the manager. Every rectangle is the window's
    // content in the output's logical pixels; a decorated window's title bar
    // (its frame window, AttachFrame) sits above that rectangle.
    struct EmbeddedWindowSpec {
        // x or y below 0: the manager places it (centred over transientFor,
        // else cascaded in the work area).
        Rect2Di rect = Rect2Di(-1, -1, 640, 480);
        EmbeddedWindowLayer layer = EmbeddedWindowLayer::Normal;
        // A title bar above it and resize edges around it.
        bool decorated = true;
        bool resizable = true;
        // Takes the keyboard when clicked. A frame window, a popup, a
        // tooltip and a notification do not.
        bool focusable = true;
        // The whole output, whatever its size: a desktop or a fullscreen
        // window. Never moved, never decorated.
        bool fillsOutput = false;
        // A dialog's window: kept above it, raised with it, centred over it.
        uint64_t transientFor = 0;
        Size2Di minSize = Size2Di(1, 1);
        Size2Di maxSize = Size2Di(-1, -1);   // -1: no limit
        std::string title;
    };

    // A window as the manager has it.
    struct EmbeddedWindowInfo {
        uint64_t id = 0;
        Rect2Di rect;
        EmbeddedWindowLayer layer = EmbeddedWindowLayer::Normal;
        bool visible = false;
        bool minimized = false;
        bool maximized = false;
        bool decorated = false;
        bool focusable = true;
        bool fillsOutput = false;
        uint64_t transientFor = 0;
        uint64_t frameId = 0;    // its title bar's window, 0 if none
        uint64_t frameOf = 0;    // for a frame window: the window it decorates
        std::string title;
    };

    // Where a pointer event goes.
    struct EmbeddedPointerRoute {
        // Deliver the event to this window at `local` (its own logical
        // coordinates, which lie outside it while a button held down in it
        // drags out of it). 0: the manager used the event itself (a move, a
        // resize) or nothing is there.
        uint64_t windowId = 0;
        Point2Di local;
        // The pointer left this window for another one (or for nothing):
        // the caller tells it so (MouseLeave), as a platform would.
        uint64_t leftWindowId = 0;
        // The shape the pointer has over an edge the manager resizes by;
        // over a window's content the window's own choice applies.
        bool managerCursor = false;
        UCMouseCursor cursor = UCMouseCursor::Default;
    };

    class UltraCanvasEmbeddedWindowManager {
    public:
        // ===== APPEARANCE (logical px) =====
        int titleBarHeight = 32;
        // How far outside a resizable window's edge the pointer still takes
        // the edge.
        int resizeGrip = 6;
        // At least this much of a window stays on the output when it is
        // dragged, so it can always be taken back.
        int keepVisible = 48;
        // The output's colour where no window covers it (no desktop window).
        uint32_t backgroundRgb = 0x202428;

        // ===== CALLBACKS (to the backend that owns the windows) =====
        // A window's rectangle changed because the manager moved, resized,
        // maximized or restored it, or placed it on AddWindow. The backend
        // resizes the window's surface when the size changed.
        std::function<void(uint64_t id, const Rect2Di& rect)> onGeometryChanged;
        // The keyboard focus moved (either id 0 for none).
        std::function<void(uint64_t lostId, uint64_t gainedId)> onFocusChanged;
        // A window was minimized, maximized or restored by the manager.
        std::function<void(uint64_t id)> onStateChanged;

        // ===== OUTPUT =====
        // The output's size in logical pixels. Windows that fill it follow;
        // maximized ones take the new work area; the rest are kept on it.
        void SetOutputSize(int width, int height);
        Size2Di GetOutputSize() const { return outputSize; }
        // Strips along the output's edges kept free for a window - a
        // desktop's bars - so maximized windows and placement stop short of
        // them. Four zeros release them; they go with the window.
        void ReserveEdges(uint64_t id, int left, int right, int top, int bottom);
        // The output less every reserved strip.
        Rect2Di GetWorkArea() const;

        // ===== WINDOWS =====
        // Joins hidden; Show makes it visible. Returns the placed rectangle.
        Rect2Di AddWindow(uint64_t id, const EmbeddedWindowSpec& spec);
        void RemoveWindow(uint64_t id);
        bool HasWindow(uint64_t id) const { return windows.count(id) != 0; }
        // `frameId` (already added, undecorated) becomes `clientId`'s title
        // bar: kept just above it, as wide as it, moved, raised, shown and
        // removed with it.
        void AttachFrame(uint64_t clientId, uint64_t frameId);
        bool GetInfo(uint64_t id, EmbeddedWindowInfo& out) const;
        // The windows in stacking order, bottom first, frames included.
        std::vector<EmbeddedWindowInfo> ListWindows() const;
        // The application windows: not frames, not popups, tooltips or
        // notifications, not the desktop - what a taskbar lists.
        std::vector<EmbeddedWindowInfo> ListApplicationWindows() const;

        void Show(uint64_t id);
        void Hide(uint64_t id);
        void SetTitle(uint64_t id, const std::string& title);
        // The window's own request (its SetWindowPosition / SetWindowSize):
        // honoured as far as the output and its limits allow. A maximized
        // window is restored first; one that fills the output stays put.
        void SetRect(uint64_t id, const Rect2Di& rect);
        void Raise(uint64_t id);
        // Raise and give it the keyboard (its client's, for a frame).
        void Activate(uint64_t id);
        void Minimize(uint64_t id);
        void Maximize(uint64_t id);
        void Restore(uint64_t id);
        void ToggleMaximize(uint64_t id);
        uint64_t GetFocused() const { return focusedId; }
        // The window keyboard events go to: the focused one, else the
        // topmost focusable visible one.
        uint64_t GetKeyboardTarget() const;
        // The topmost visible window of the Normal band or above that can
        // take the focus, other than `except`.
        uint64_t TopmostFocusable(uint64_t except = 0) const;

        // ===== POINTER =====
        // The pointer is the manager's: it is drawn by Compose. Positions in
        // output logical pixels.
        void SetPointerPosition(Point2Di position);
        // Moves the pointer by a relative motion (a touchpad, a captured
        // mouse) kept on the output; returns the new position.
        Point2Di MovePointerBy(double dx, double dy);
        Point2Di GetPointerPosition() const { return pointer; }
        void SetPointerVisible(bool visible);
        bool IsPointerVisible() const { return pointerVisible; }
        // The shape a window chose for itself (its SelectMouseCursor); shown
        // while the pointer is over that window's content.
        void SetWindowCursor(uint64_t id, UCMouseCursor cursor);

        EmbeddedWindowPart HitTest(Point2Di position, uint64_t* windowId = nullptr) const;

        // The pointer moved there / a button went down or up there. The
        // pointer position is updated by each.
        EmbeddedPointerRoute PointerMove(Point2Di position);
        EmbeddedPointerRoute PointerButton(Point2Di position, bool down);
        // A frame window's title was taken with the button down: the window
        // it decorates follows the pointer until the button goes up.
        void BeginMove(uint64_t clientId);
        bool IsMovingOrResizing() const { return interaction != Interaction::Idle; }

        // ===== COMPOSITING =====
        // What needs drawing again, in output logical pixels. Windows that
        // presented a frame add their rectangle (WindowPresented); geometry
        // and pointer changes add theirs.
        void AddDamage(const Rect2Di& rect);
        void DamageAll();
        void WindowPresented(uint64_t id);
        bool HasDamage() const { return !damage.empty(); }
        // The damaged rectangles, merged, and clears them.
        std::vector<Rect2Di> TakeDamage();

        // A window's surface: its content at the output's scale. Null skips
        // it (not drawn yet).
        using SurfaceProvider = std::function<cairo_surface_t*(uint64_t id)>;
        // Draws the damaged rectangles of the output into `output` (an image
        // surface at the output's physical size, `scale` physical pixels a
        // logical one): the background, every visible window bottom first
        // with a shadow under the decorated ones, and the pointer on top.
        // Returns the physical rectangle drawn (empty when nothing was).
        Rect2Di Compose(cairo_surface_t* output, float scale,
                        const SurfaceProvider& surfaceOf,
                        const std::vector<Rect2Di>& damaged);

        // The rectangle the pointer is drawn over, for damage.
        Rect2Di PointerBounds() const;
        static void DrawPointer(cairo_t* cr, UCMouseCursor cursor, double x, double y, double size);

    private:
        enum class Interaction { Idle, Moving, Resizing };

        struct Window {
            EmbeddedWindowInfo info;
            Rect2Di restoreRect;
            Size2Di minSize = Size2Di(1, 1);
            Size2Di maxSize = Size2Di(-1, -1);
            bool resizable = true;
            UCMouseCursor cursor = UCMouseCursor::Default;
        };

        std::map<uint64_t, Window> windows;
        std::vector<uint64_t> stack;   // bottom first
        std::map<uint64_t, Rect2Di> reserved;   // left, right, top, bottom in x,y,w,h
        Size2Di outputSize = Size2Di(1280, 720);
        uint64_t focusedId = 0;
        int cascade = 0;

        Point2Di pointer = Point2Di(0, 0);
        bool pointerVisible = true;
        uint64_t hoverId = 0;
        uint64_t grabId = 0;
        int buttonsDown = 0;

        Interaction interaction = Interaction::Idle;
        uint64_t interactionId = 0;
        EmbeddedWindowPart resizePart = EmbeddedWindowPart::Nothing;
        Point2Di interactionAnchor;
        Rect2Di interactionStart;

        std::vector<Rect2Di> damage;

        Window* Find(uint64_t id);
        const Window* Find(uint64_t id) const;
        uint64_t ClientOf(uint64_t id) const;   // a frame's client, else itself
        bool IsShown(const Window& w) const { return w.info.visible && !w.info.minimized; }
        // The client's rectangle with its title bar above it.
        Rect2Di OuterRect(const Window& w) const;
        // Outer rect grown by the shadow, for damage.
        Rect2Di DamageRect(const Window& w) const;
        void Restack(uint64_t id);          // into its band, at the top
        void PlaceInStack(uint64_t id);     // new window: top of its band
        void ApplyRect(Window& w, const Rect2Di& rect, bool notify);
        void SyncFrame(Window& client, bool notify);
        Rect2Di ClampToOutput(const Window& w, Rect2Di rect) const;
        Rect2Di Constrain(const Window& w, Rect2Di rect) const;
        void SetFocus(uint64_t id);
        void FocusSomethingElse(uint64_t leaving);
        EmbeddedPointerRoute RouteTo(uint64_t id, Point2Di position, EmbeddedPointerRoute route) const;
        static UCMouseCursor CursorFor(EmbeddedWindowPart part);
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_EMBEDDED_WINDOW_MANAGER_H
