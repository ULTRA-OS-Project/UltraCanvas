// OS/Linux/Wayland/UltraCanvasWaylandWindow.h
// A window on Wayland: a wl_surface with an xdg_toplevel role, drawn into
// shared-memory buffers. The framework renders into `nativeSurface` as it
// does everywhere (a cairo image surface at the physical size, with the
// device scale set), and InvalidateWindowNative copies it into a free
// wl_buffer, attaches, damages and commits. Sizes the compositor sends are
// logical (surface coordinates); the buffer is logical × scale, with
// wl_surface.set_buffer_scale telling the compositor so.
// Version: 0.1.0
// Last Modified: 2026-10-30
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
        // The output whose scale the window draws at: the largest of those it
        // is on. 1 before it is on any.
        int GetOutputScale() const;

        // ----- called by the application's listeners -----
        void HandleSurfaceConfigure(uint32_t serial);
        void HandleToplevelConfigure(int32_t width, int32_t height, const std::vector<uint32_t>& states);
        void HandleToplevelClose();
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

        bool CreateNativeCairoSurface();
        void DestroyNativeCairoSurface();
        void ApplyDecoration();
        void ApplySizeLimits();
        void ApplyType();
        bool WantsServerDecoration() const;
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_WAYLAND_WINDOW_H
