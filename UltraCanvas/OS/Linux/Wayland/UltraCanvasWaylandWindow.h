// OS/Linux/Wayland/UltraCanvasWaylandWindow.h
// A window on Wayland: a wl_surface with an xdg_toplevel role, drawn into
// shared-memory buffers. The framework renders into `nativeSurface` as it
// does everywhere (a cairo image surface at the physical size, with the
// device scale set), and InvalidateWindowNative copies it into a free
// wl_buffer, attaches, damages and commits. Sizes the compositor sends are
// logical (surface coordinates); the buffer is logical × scale, with
// wl_surface.set_buffer_scale telling the compositor so.
//
// A WindowType::Desktop window on a compositor with the layer shell
// (wlr-layer-shell-unstable-v1: every wlroots compositor, KWin) is not an
// xdg_toplevel but a zwlr_layer_surface_v1 on the background layer of the
// output its desktopArea names, anchored to all four edges, so the
// compositor sizes it to that output and keeps it under every window.
// The edges a desktop shell reserves for its bars (ReserveScreenEdges)
// become one more layer surface per edge on the top layer: fully
// transparent, no input region, with the edge's exclusive zone - the
// layer shell's form of an X11 strut, since one layer surface carries one
// exclusive zone on one edge and the desktop's own is anchored to all four.
// Without the layer shell (GNOME's compositor) a Desktop window stays a
// maximized toplevel and reserves nothing.
//
// A window with a parent (config.parentWindow) of type Popup, Borderless,
// Tool or Notification is placed where config.x / config.y say, relative
// to that parent - the one thing a Wayland client may position: a child of
// a layer-shell desktop (UltraDesktop's panels beside its bars) is a layer
// surface of its own on the top layer, anchored to the output's corner
// with the position as its margins, taking the keys when clicked and
// giving them back (a blur) when the desktop is clicked; a child of a
// toplevel is an xdg_popup with a positioner (anchored at the position,
// slid onto the screen), made when the window is shown, since the position
// is set between creation and showing, and remade on every show, as a
// popup is dismissed for good. A child without the layer shell or an
// xdg_surface parent is an ordinary toplevel the compositor places.
//
// A WindowType::SessionLock window is an ext-session-lock-v1 lock surface
// on its output: the application holds the one lock for all of them (the
// first asks, the last releases); the compositor shows nothing else while
// it is held. Without the protocol it is a fullscreen toplevel.
// Version: 0.4.0 - panels of a layer-shell desktop as layer surfaces, popups of
//                  a toplevel as xdg_popup; the session lock surface; file drags
// Version: 0.3.0 - RaiseAndFocus through xdg-activation
// Version: 0.2.0 - Desktop windows on the layer shell; the reserved edges
// Version: 0.1.0
// Last Modified: 2026-11-04
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVAS_WAYLAND_WINDOW_H
#define ULTRACANVAS_WAYLAND_WINDOW_H

// The public header defines the base class and, at its end, aliases
// UltraCanvasWindow to this class; the forward declaration lets either
// header be included first.
namespace UltraCanvas { class UltraCanvasWaylandWindow; }
#include "../../../include/UltraCanvasWindow.h"
#include "../../../include/UltraCanvasRenderContext.h"

#include <wayland-client.h>
#include <cairo/cairo.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct xdg_surface;
struct xdg_toplevel;
struct zxdg_toplevel_decoration_v1;
struct zwlr_layer_surface_v1;
struct xdg_activation_token_v1;
struct xdg_popup;
struct xdg_positioner;
struct ext_session_lock_surface_v1;

namespace UltraCanvas {

    class UltraCanvasWaylandApplication;

    class UltraCanvasWaylandWindow : public UltraCanvasWindowBase {
    public:
        UltraCanvasWaylandWindow();
        ~UltraCanvasWaylandWindow() override;

        void Show() override;
        void Hide() override;
        void RaiseAndFocus() override;
        void SetWindowTitle(const std::string& title) override;
        void SetWindowIcon(const std::string& iconPath) override;
        void SetWindowSize(int width, int height) override;
        void SetWindowPosition(int x, int y) override;
        void SetResizable(bool resizable) override;
        void Minimize() override;
        void Maximize() override;
        void Restore() override;
        void SetFullscreen(bool fullscreen) override;
        void InvalidateWindowNative() override;
        NativeWindowHandle GetNativeHandle() const override;
        void GetWindowPosition(int& x, int& y) const override;
        void GetScreenSize(int& width, int& height) const override;
        void GetScreenBounds(int& x, int& y, int& width, int& height) const override;
        void SetTransientParent(UltraCanvasWindowBase* parent) override;
        bool StartNativeFileDrag(const std::vector<std::string>& filePaths,
                                 std::function<void(bool accepted, bool moved)> onFinished = nullptr) override;
        bool StartNativeSaveDrag(const std::string& fileName,
                                 std::function<void(const std::string& folder)> onFolder) override;

        wl_surface* GetSurface() const { return surface; }
        xdg_toplevel* GetToplevel() const { return toplevel; }
        xdg_surface* GetXdgSurface() const { return xdgSurface; }
        // True for a Desktop window the layer shell placed (see the head);
        // a panel of one is a layer surface too, but no desktop.
        bool IsLayerSurface() const { return layerSurface != nullptr && !panelLayer; }
        bool IsPanelLayer() const { return panelLayer; }
        bool IsPopup() const { return popup != nullptr || popupWanted; }
        bool IsLockSurface() const { return lockSurface != nullptr; }
        wl_output* GetLayerOutput() const { return layerOutput; }
        // The screen edges this desktop window reserves for its bars, in
        // logical pixels of its output, 0 for an edge that reserves nothing
        // (the desktop shell backend's ReserveScreenEdges). False for a
        // window that is no layer surface. UI thread.
        bool SetReservedEdges(int left, int right, int top, int bottom);
        // The output whose scale the window draws at: the largest of those it
        // is on. 1 before it is on any.
        int GetOutputScale() const;

        // ----- called by the application's listeners -----
        void HandleSurfaceConfigure(uint32_t serial);
        void HandleToplevelConfigure(int32_t width, int32_t height, const std::vector<uint32_t>& states);
        void HandleToplevelClose();
        void HandleLayerConfigure(uint32_t serial, uint32_t width, uint32_t height);
        // The activation token the compositor issued (RaiseAndFocus).
        void HandleActivationToken(const char* token);
        // An xdg_popup's place and size, and its dismissal.
        void HandlePopupConfigure(int32_t x, int32_t y, int32_t width, int32_t height);
        void HandlePopupDone();
        // A lock surface's size.
        void HandleLockConfigure(uint32_t serial, uint32_t width, uint32_t height);
        void HandleOutputEnter(wl_output* output);
        void HandleOutputLeave(wl_output* output);
        // An output's scale changed, or one went away.
        void HandleOutputsChanged();
        // The compositor is done with a buffer (the wl_buffer listener).
        static void OnBufferRelease(void* data, wl_buffer* buffer);

    protected:
        bool CreateNative() override;
        void DestroyNative() override;
        void DoResizeNative() override;
        float QueryNativeDeviceScale() const override;
        bool RecreateNativeSurface() override;

    private:
        friend struct WaylandWindowListeners;

        wl_surface* surface = nullptr;
        xdg_surface* xdgSurface = nullptr;
        xdg_toplevel* toplevel = nullptr;
        zxdg_toplevel_decoration_v1* decoration = nullptr;
        xdg_activation_token_v1* activationToken = nullptr;   // a RaiseAndFocus in flight
        zwlr_layer_surface_v1* layerSurface = nullptr;   // a Desktop window on the layer shell, or a panel of one
        wl_output* layerOutput = nullptr;                // the output it was asked for (may be null: the compositor's choice)
        bool panelLayer = false;                         // the layer surface is a panel, not the desktop
        xdg_popup* popup = nullptr;                      // a child of a toplevel
        bool popupWanted = false;                        // ... made at Show, remade after a Hide
        uint32_t repositionToken = 0;
        ext_session_lock_surface_v1* lockSurface = nullptr;   // a SessionLock window
        std::vector<wl_output*> enteredOutputs;
        bool configured = false;        // the first configure has been acked
        bool pendingResize = false;     // a configure's size waits for the base
        int pendingWidth = 0, pendingHeight = 0;
        bool presentWanted = false;     // a frame waited for a free buffer
        uint32_t lastConfigureSerial = 0;

        // ----- the buffers -----
        struct ShmBuffer {
            wl_buffer* buffer = nullptr;
            void* map = nullptr;
            size_t size = 0;
            int width = 0, height = 0, stride = 0;   // pixels
            bool busy = false;   // attached and not yet released
        };
        std::vector<std::unique_ptr<ShmBuffer>> buffers;
        std::mutex surfaceMutex;
        bool CreateShmBuffer(ShmBuffer& out, int width, int height);
        void DestroyShmBuffer(ShmBuffer& buffer);
        void DestroyAllBuffers();
        ShmBuffer* FreeBuffer(int width, int height);
        void Present();
        // What a configure ends with, xdg or layer: the resize the
        // compositor asked for, else the frame.
        void FinishConfigure(bool first);

        // ----- the reserved edges (see the head) -----
        struct EdgeStrut {
            UltraCanvasWaylandWindow* window = nullptr;
            int edge = 0;                       // 0 left, 1 right, 2 top, 3 bottom
            int thickness = 0;                  // logical pixels; 0 = none
            wl_surface* surface = nullptr;
            zwlr_layer_surface_v1* layer = nullptr;
            ShmBuffer buffer;                   // transparent, the configured size
        };
        EdgeStrut struts[4];
        bool CreateLayerSurface(UltraCanvasWaylandApplication* app);
        // ----- a child placed by its parent (see the head) -----
        UltraCanvasWaylandWindow* PlacingParent() const;
        bool CreatePanelLayer(UltraCanvasWaylandApplication* app, UltraCanvasWaylandWindow* parent);
        void ApplyPanelPosition();
        bool CreatePopupRole(UltraCanvasWaylandApplication* app);
        void DestroyPopupRole();
        xdg_positioner* MakePositioner(UltraCanvasWaylandApplication* app, UltraCanvasWaylandWindow* parent) const;
        // ----- the session lock -----
        bool CreateLockSurface(UltraCanvasWaylandApplication* app);
        bool CreateStrut(EdgeStrut& strut, int thickness);
        void ApplyStrut(EdgeStrut& strut, int thickness);
        void DestroyStrut(EdgeStrut& strut);
        void HandleStrutConfigure(EdgeStrut& strut, uint32_t serial, uint32_t width, uint32_t height);

        bool CreateNativeCairoSurface();
        void DestroyNativeCairoSurface();
        void ApplyDecoration();
        void ApplySizeLimits();
        void ApplyType();
        bool WantsServerDecoration() const;
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_WAYLAND_WINDOW_H
