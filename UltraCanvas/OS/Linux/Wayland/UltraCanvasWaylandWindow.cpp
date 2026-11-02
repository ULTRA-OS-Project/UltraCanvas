// OS/Linux/Wayland/UltraCanvasWaylandWindow.cpp
// A window on Wayland. See the header.
// Version: 0.1.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework

#include "UltraCanvasWaylandWindow.h"
#include "UltraCanvasWaylandApplication.h"
#include "UltraCanvasApplication.h"
#include "UltraCanvasDebug.h"

#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"

#include <sys/mman.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>

namespace UltraCanvas {

    // ===== THE LISTENERS =====
    struct WaylandWindowListeners {
        static UltraCanvasWaylandWindow* Win(void* data) { return static_cast<UltraCanvasWaylandWindow*>(data); }

        static void SurfaceEnter(void* data, wl_surface*, wl_output* output) { Win(data)->HandleOutputEnter(output); }
        static void SurfaceLeave(void* data, wl_surface*, wl_output* output) { Win(data)->HandleOutputLeave(output); }

        static void XdgSurfaceConfigure(void* data, xdg_surface*, uint32_t serial) {
            Win(data)->HandleSurfaceConfigure(serial);
        }
        static void ToplevelConfigure(void* data, xdg_toplevel*, int32_t width, int32_t height, wl_array* states) {
            std::vector<uint32_t> list;
            if (states && states->size) {
                const auto* begin = static_cast<const uint32_t*>(states->data);
                list.assign(begin, begin + states->size / sizeof(uint32_t));
            }
            Win(data)->HandleToplevelConfigure(width, height, list);
        }
        static void ToplevelClose(void* data, xdg_toplevel*) { Win(data)->HandleToplevelClose(); }
        static void ToplevelConfigureBounds(void*, xdg_toplevel*, int32_t, int32_t) {}
        static void ToplevelWmCapabilities(void*, xdg_toplevel*, wl_array*) {}
        static void DecorationConfigure(void*, zxdg_toplevel_decoration_v1*, uint32_t) {}
    };

    namespace {
        const wl_surface_listener kSurfaceListener = {
                WaylandWindowListeners::SurfaceEnter,
                WaylandWindowListeners::SurfaceLeave,
        };
        const xdg_surface_listener kXdgSurfaceListener = {
                WaylandWindowListeners::XdgSurfaceConfigure,
        };
        const xdg_toplevel_listener kToplevelListener = {
                WaylandWindowListeners::ToplevelConfigure,
                WaylandWindowListeners::ToplevelClose,
                WaylandWindowListeners::ToplevelConfigureBounds,
                WaylandWindowListeners::ToplevelWmCapabilities,
        };
        const zxdg_toplevel_decoration_v1_listener kDecorationListener = {
                WaylandWindowListeners::DecorationConfigure,
        };
        const wl_buffer_listener kBufferListener = {
                UltraCanvasWaylandWindow::OnBufferRelease,
        };

        UltraCanvasWaylandApplication* App() { return UltraCanvasWaylandApplication::GetInstance(); }

        // The app id xdg-shell shows the compositor: the program's name, as
        // a desktop entry would be called.
        std::string AppId() {
            auto* app = App();
            std::string name = app ? app->GetAppName() : "";
            if (name.empty()) name = "ultracanvas";
            for (char& c : name) {
                if (c == ' ') c = '-';
            }
            return name;
        }
    } // namespace

    // ===== CONSTRUCTION =====
    UltraCanvasWaylandWindow::UltraCanvasWaylandWindow() = default;

    UltraCanvasWaylandWindow::~UltraCanvasWaylandWindow() {
        if (surface) DestroyNative();
    }

    bool UltraCanvasWaylandWindow::WantsServerDecoration() const {
        switch (config_.type) {
            case WindowType::Borderless:
            case WindowType::Desktop:
            case WindowType::Notification:
            case WindowType::Overlay:
            case WindowType::Popup:
            case WindowType::Fullscreen:
                return false;
            default:
                return true;
        }
    }

    bool UltraCanvasWaylandWindow::CreateNative() {
        if (_created) return true;
        auto* app = App();
        if (!app || !app->IsInitialized() || !app->GetCompositor() || !app->GetXdgWmBase()) {
            debugOutput << "UltraCanvas Wayland: cannot create a window - the application is not ready" << std::endl;
            return false;
        }
        surface = wl_compositor_create_surface(app->GetCompositor());
        if (!surface) return false;
        wl_surface_add_listener(surface, &kSurfaceListener, this);

        xdgSurface = xdg_wm_base_get_xdg_surface(app->GetXdgWmBase(), surface);
        xdg_surface_add_listener(xdgSurface, &kXdgSurfaceListener, this);
        toplevel = xdg_surface_get_toplevel(xdgSurface);
        xdg_toplevel_add_listener(toplevel, &kToplevelListener, this);
        xdg_toplevel_set_title(toplevel, config_.title.c_str());
        xdg_toplevel_set_app_id(toplevel, AppId().c_str());
        if (config_.parentWindow) SetTransientParent(config_.parentWindow);
        ApplySizeLimits();
        ApplyDecoration();
        ApplyType();

        // A desktop window takes the screen (the largest output) as its size.
        if (config_.type == WindowType::Desktop) {
            int w = 0, h = 0;
            app->GetScreenSize(w, h);
            if (w > 0 && h > 0) {
                config_.width = w;
                config_.height = h;
                SetBounds(Rect2Di(0, 0, w, h));
            }
        }

        RefreshDeviceScale();
        if (!CreateNativeCairoSurface()) {
            DestroyNative();
            return false;
        }
        debugOutput << "UltraCanvas Wayland: window created (" << config_.width << "x" << config_.height << ")" << std::endl;
        return true;
    }

    void UltraCanvasWaylandWindow::ApplyDecoration() {
        auto* app = App();
        if (!app || !toplevel) return;
        if (app->GetDecorationManager() && !decoration) {
            decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(app->GetDecorationManager(), toplevel);
            zxdg_toplevel_decoration_v1_add_listener(decoration, &kDecorationListener, this);
        }
        if (decoration) {
            // Without the manager the compositor decides; with it, the
            // compositor draws the frame of an ordinary window and none for
            // a borderless one (this backend draws no frames of its own).
            zxdg_toplevel_decoration_v1_set_mode(decoration, WantsServerDecoration()
                    ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE
                    : ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE);
        }
    }

    void UltraCanvasWaylandWindow::ApplySizeLimits() {
        if (!toplevel) return;
        if (!config_.resizable) {
            xdg_toplevel_set_min_size(toplevel, config_.width, config_.height);
            xdg_toplevel_set_max_size(toplevel, config_.width, config_.height);
            return;
        }
        xdg_toplevel_set_min_size(toplevel, std::max(0, config_.minWidth), std::max(0, config_.minHeight));
        xdg_toplevel_set_max_size(toplevel, std::max(0, config_.maxWidth), std::max(0, config_.maxHeight));
    }

    void UltraCanvasWaylandWindow::ApplyType() {
        if (!toplevel) return;
        if (config_.type == WindowType::Fullscreen) {
            xdg_toplevel_set_fullscreen(toplevel, nullptr);
        } else if (config_.type == WindowType::Desktop) {
            // Until the desktop shell backend puts it on the layer shell, a
            // desktop window is a maximized toplevel.
            xdg_toplevel_set_maximized(toplevel);
        }
    }

    // ===== THE BUFFERS =====
    bool UltraCanvasWaylandWindow::CreateShmBuffer(ShmBuffer& out, int width, int height) {
        auto* app = App();
        if (!app || !app->GetShm() || width <= 0 || height <= 0) return false;
        const int stride = width * 4;
        const size_t size = static_cast<size_t>(stride) * height;
        int fd = memfd_create("ultracanvas-window", MFD_CLOEXEC);
        if (fd < 0) return false;
        if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
            close(fd);
            return false;
        }
        void* map = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (map == MAP_FAILED) {
            close(fd);
            return false;
        }
        wl_shm_pool* pool = wl_shm_create_pool(app->GetShm(), fd, static_cast<int32_t>(size));
        out.buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
        close(fd);
        if (!out.buffer) {
            munmap(map, size);
            return false;
        }
        wl_buffer_add_listener(out.buffer, &kBufferListener, &out);
        out.map = map;
        out.size = size;
        out.width = width;
        out.height = height;
        out.stride = stride;
        out.busy = false;
        return true;
    }

    void UltraCanvasWaylandWindow::DestroyShmBuffer(ShmBuffer& buffer) {
        if (buffer.buffer) wl_buffer_destroy(buffer.buffer);
        if (buffer.map) munmap(buffer.map, buffer.size);
        buffer = ShmBuffer{};
    }

    void UltraCanvasWaylandWindow::DestroyAllBuffers() {
        for (auto& buffer : buffers) DestroyShmBuffer(*buffer);
        buffers.clear();
    }

    void UltraCanvasWaylandWindow::OnBufferRelease(void* data, wl_buffer*) {
        auto* buffer = static_cast<ShmBuffer*>(data);
        buffer->busy = false;
    }

    UltraCanvasWaylandWindow::ShmBuffer* UltraCanvasWaylandWindow::FreeBuffer(int width, int height) {
        // Buffers of another size are stale: let them go when released.
        for (auto it = buffers.begin(); it != buffers.end();) {
            if (!(*it)->busy && ((*it)->width != width || (*it)->height != height)) {
                DestroyShmBuffer(**it);
                it = buffers.erase(it);
            } else {
                ++it;
            }
        }
        for (auto& buffer : buffers) {
            if (!buffer->busy) return buffer.get();
        }
        if (buffers.size() >= 3) return nullptr;   // the compositor holds them all; next release
        auto fresh = std::make_unique<ShmBuffer>();
        if (!CreateShmBuffer(*fresh, width, height)) return nullptr;
        buffers.push_back(std::move(fresh));
        return buffers.back().get();
    }

    // ===== THE DRAWING SURFACE =====
    bool UltraCanvasWaylandWindow::CreateNativeCairoSurface() {
        const int physW = std::max(1, LogicalToPhysical(config_.width));
        const int physH = std::max(1, LogicalToPhysical(config_.height));
        cairo_surface_t* created = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, physW, physH);
        if (!created || cairo_surface_status(created) != CAIRO_STATUS_SUCCESS) {
            debugOutput << "UltraCanvas Wayland: cairo_image_surface_create failed" << std::endl;
            if (created) cairo_surface_destroy(created);
            return false;
        }
        // 1 user unit = deviceScale device pixels: the UI keeps drawing in
        // logical units, as on every other backend.
        cairo_surface_set_device_scale(created, deviceScale, deviceScale);
        cairo_surface_set_fallback_resolution(created, 96, 96);
        nativeSurface = created;
        return true;
    }

    void UltraCanvasWaylandWindow::DestroyNativeCairoSurface() {
        if (nativeSurface) {
            cairo_surface_destroy(static_cast<cairo_surface_t*>(nativeSurface));
            nativeSurface = nullptr;
        }
    }

    bool UltraCanvasWaylandWindow::RecreateNativeSurface() {
        std::lock_guard<std::mutex> lock(surfaceMutex);
        auto* old = static_cast<cairo_surface_t*>(nativeSurface);
        if (!CreateNativeCairoSurface()) return false;
        if (old) cairo_surface_destroy(old);
        if (surface) wl_surface_set_buffer_scale(surface, std::max(1, static_cast<int>(deviceScale)));
        return true;
    }

    void UltraCanvasWaylandWindow::DoResizeNative() {
        RecreateNativeSurface();
        InvalidateWindowNative();
    }

    void UltraCanvasWaylandWindow::Present() {
        auto* app = App();
        if (!surface || !nativeSurface || !configured || !_windowVisible || !app) {
            presentWanted = presentWanted || (surface && !configured);
            return;
        }
        auto* image = static_cast<cairo_surface_t*>(nativeSurface);
        cairo_surface_flush(image);
        const int width = cairo_image_surface_get_width(image);
        const int height = cairo_image_surface_get_height(image);
        ShmBuffer* buffer = FreeBuffer(width, height);
        if (!buffer) {
            presentWanted = true;   // every buffer is with the compositor; OnBufferRelease frees one
            return;
        }
        presentWanted = false;
        const unsigned char* source = cairo_image_surface_get_data(image);
        const int sourceStride = cairo_image_surface_get_stride(image);
        if (sourceStride == buffer->stride) {
            std::memcpy(buffer->map, source, buffer->size);
        } else {
            for (int y = 0; y < height; ++y) {
                std::memcpy(static_cast<unsigned char*>(buffer->map) + y * buffer->stride,
                            source + y * sourceStride, static_cast<size_t>(width) * 4);
            }
        }
        buffer->busy = true;
        wl_surface_set_buffer_scale(surface, std::max(1, static_cast<int>(deviceScale)));
        wl_surface_attach(surface, buffer->buffer, 0, 0);
        wl_surface_damage_buffer(surface, 0, 0, width, height);
        wl_surface_commit(surface);
        app->Flush();
    }

    void UltraCanvasWaylandWindow::InvalidateWindowNative() {
        Present();
    }

    // ===== LIFETIME =====
    void UltraCanvasWaylandWindow::DestroyNative() {
        auto* app = App();
        if (app) app->ForgetWindow(this);
        renderContext.reset();
        DestroyNativeCairoSurface();
        DestroyAllBuffers();
        if (decoration) { zxdg_toplevel_decoration_v1_destroy(decoration); decoration = nullptr; }
        if (toplevel) { xdg_toplevel_destroy(toplevel); toplevel = nullptr; }
        if (xdgSurface) { xdg_surface_destroy(xdgSurface); xdgSurface = nullptr; }
        if (surface) { wl_surface_destroy(surface); surface = nullptr; }
        enteredOutputs.clear();
        configured = false;
        if (app) app->Flush();
    }

    void UltraCanvasWaylandWindow::Show() {
        if (!_created || _windowVisible || !surface) return;
        // The first commit without a buffer asks for the initial configure;
        // the first frame goes up once it is acked (HandleSurfaceConfigure).
        if (!configured) {
            wl_surface_commit(surface);
            if (auto* app = App()) app->Flush();
        }
        _windowVisible = true;
        presentWanted = true;
        HandleWindowShown();
        if (onWindowShow) onWindowShow();
        RequestRedraw();
    }

    void UltraCanvasWaylandWindow::Hide() {
        if (!_created || !_windowVisible || !surface) return;
        // A surface with no buffer is unmapped; the next Show commits anew
        // and the compositor configures it again.
        wl_surface_attach(surface, nullptr, 0, 0);
        wl_surface_commit(surface);
        configured = false;
        if (auto* app = App()) app->Flush();
        _windowVisible = false;
        HandleWindowHidden();
        if (onWindowHide) onWindowHide();
    }

    void UltraCanvasWaylandWindow::RaiseAndFocus() {
        // A client cannot raise or focus itself on Wayland without
        // xdg-activation, which a later version may add; the compositor
        // focuses a new toplevel by itself.
    }

    // ----- what the compositor says -----
    void UltraCanvasWaylandWindow::HandleSurfaceConfigure(uint32_t serial) {
        if (!xdgSurface) return;
        xdg_surface_ack_configure(xdgSurface, serial);
        lastConfigureSerial = serial;
        const bool first = !configured;
        configured = true;
        if (pendingResize) {
            pendingResize = false;
            if (pendingWidth != config_.width || pendingHeight != config_.height) {
                UCEvent event;
                event.type = UCEventType::WindowResize;
                event.timestamp = std::chrono::steady_clock::now();
                event.nativeWindowHandle = reinterpret_cast<NativeWindowHandle>(surface);
                event.targetWindow = GetWindowWeakPtr();
                event.width = pendingWidth;
                event.height = pendingHeight;
                if (auto* app = App()) app->PushEvent(event);
                return;   // the frame follows the resize
            }
        }
        if (first || presentWanted) {
            RequestRedraw();
            if (renderContext) Present();
        }
    }

    void UltraCanvasWaylandWindow::HandleToplevelConfigure(int32_t width, int32_t height, const std::vector<uint32_t>& states) {
        bool maximized = false, fullscreen = false;
        for (uint32_t state : states) {
            if (state == XDG_TOPLEVEL_STATE_MAXIMIZED) maximized = true;
            if (state == XDG_TOPLEVEL_STATE_FULLSCREEN) fullscreen = true;
        }
        if (fullscreen) _state = WindowState::Fullscreen;
        else if (maximized) _state = WindowState::Maximized;
        else if (_state == WindowState::Maximized || _state == WindowState::Fullscreen) _state = WindowState::Normal;
        // 0x0 leaves the size to the client; otherwise the compositor's size
        // is the window's (a non-resizable window has pinned min == max).
        if (width > 0 && height > 0) {
            pendingResize = true;
            pendingWidth = width;
            pendingHeight = height;
        }
    }

    void UltraCanvasWaylandWindow::HandleToplevelClose() {
        UCEvent event;
        event.type = UCEventType::WindowCloseRequest;
        event.timestamp = std::chrono::steady_clock::now();
        event.nativeWindowHandle = reinterpret_cast<NativeWindowHandle>(surface);
        event.targetWindow = GetWindowWeakPtr();
        if (auto* app = App()) app->PushEvent(event);
    }

    void UltraCanvasWaylandWindow::HandleOutputEnter(wl_output* output) {
        if (std::find(enteredOutputs.begin(), enteredOutputs.end(), output) == enteredOutputs.end()) {
            enteredOutputs.push_back(output);
        }
        HandleOutputsChanged();
    }

    void UltraCanvasWaylandWindow::HandleOutputLeave(wl_output* output) {
        enteredOutputs.erase(std::remove(enteredOutputs.begin(), enteredOutputs.end(), output), enteredOutputs.end());
        HandleOutputsChanged();
    }

    void UltraCanvasWaylandWindow::HandleOutputsChanged() {
        if (!_created) return;
        if (RefreshDeviceScale()) {
            HandleDeviceScaleChange();
            if (auto* app = App()) app->SelectMouseCursorNative(this, GetCurrentMouseCursor());
        }
    }

    int UltraCanvasWaylandWindow::GetOutputScale() const {
        auto* app = App();
        int scale = 1;
        if (!app) return scale;
        for (wl_output* output : enteredOutputs) {
            if (const WaylandOutput* info = app->FindOutput(output)) scale = std::max(scale, info->scale);
        }
        return scale;
    }

    float UltraCanvasWaylandWindow::QueryNativeDeviceScale() const {
        return static_cast<float>(GetOutputScale());
    }

    // ===== PROPERTIES =====
    void UltraCanvasWaylandWindow::SetWindowTitle(const std::string& title) {
        config_.title = title;
        if (toplevel) {
            xdg_toplevel_set_title(toplevel, title.c_str());
            if (auto* app = App()) app->Flush();
        }
    }

    void UltraCanvasWaylandWindow::SetWindowIcon(const std::string& iconPath) {
        // xdg-shell has no window icon; the compositor takes it from the
        // desktop entry the app id names (xdg-toplevel-icon may come later).
        (void)iconPath;
    }

    void UltraCanvasWaylandWindow::SetWindowSize(int width, int height) {
        config_.width = width;
        config_.height = height;
        if (_created) {
            ApplySizeLimits();
            _needsResize = true;
        } else {
            UltraCanvasWindowBase::SetSize(width, height);
        }
    }

    void UltraCanvasWaylandWindow::SetWindowPosition(int x, int y) {
        // A client does not place its own toplevels on Wayland; the
        // compositor does. The wish is kept for GetWindowPosition.
        config_.x = x;
        config_.y = y;
    }

    void UltraCanvasWaylandWindow::GetWindowPosition(int& x, int& y) const {
        x = std::max(0, config_.x);
        y = std::max(0, config_.y);
    }

    void UltraCanvasWaylandWindow::SetResizable(bool resizable) {
        config_.resizable = resizable;
        ApplySizeLimits();
    }

    void UltraCanvasWaylandWindow::Minimize() {
        if (!toplevel) return;
        xdg_toplevel_set_minimized(toplevel);
        // xdg-shell does not report a minimized state back; take it as done.
        _state = WindowState::Minimized;
        HandleWindowHidden();
        if (auto* app = App()) app->Flush();
    }

    void UltraCanvasWaylandWindow::Maximize() {
        if (!toplevel) return;
        xdg_toplevel_set_maximized(toplevel);
        if (auto* app = App()) app->Flush();
    }

    void UltraCanvasWaylandWindow::Restore() {
        if (!toplevel) return;
        if (_state == WindowState::Fullscreen) xdg_toplevel_unset_fullscreen(toplevel);
        xdg_toplevel_unset_maximized(toplevel);
        if (_state == WindowState::Minimized) {
            // Nothing in xdg-shell un-minimizes; a fresh commit puts the
            // window back where compositors let a client do that.
            _state = WindowState::Normal;
            HandleWindowShown();
            Present();
        }
        if (auto* app = App()) app->Flush();
    }

    void UltraCanvasWaylandWindow::SetFullscreen(bool fullscreen) {
        if (!toplevel) return;
        if (fullscreen) xdg_toplevel_set_fullscreen(toplevel, nullptr);
        else xdg_toplevel_unset_fullscreen(toplevel);
        if (auto* app = App()) app->Flush();
    }

    NativeWindowHandle UltraCanvasWaylandWindow::GetNativeHandle() const {
        return reinterpret_cast<NativeWindowHandle>(surface);
    }

    void UltraCanvasWaylandWindow::GetScreenSize(int& width, int& height) const {
        int x, y;
        GetScreenBounds(x, y, width, height);
    }

    void UltraCanvasWaylandWindow::GetScreenBounds(int& x, int& y, int& width, int& height) const {
        x = y = width = height = 0;
        auto* app = App();
        if (!app) return;
        // The output the window is on; the first output before it is on any.
        const WaylandOutput* info = nullptr;
        if (!enteredOutputs.empty()) info = app->FindOutput(enteredOutputs.front());
        if (!info && !app->GetOutputs().empty()) info = &app->GetOutputs().front();
        if (!info) return;
        x = info->x;
        y = info->y;
        width = info->Width();
        height = info->Height();
    }

    void UltraCanvasWaylandWindow::SetTransientParent(UltraCanvasWindowBase* parent) {
        if (!toplevel) return;
        auto* other = dynamic_cast<UltraCanvasWaylandWindow*>(parent);
        xdg_toplevel_set_parent(toplevel, other ? other->GetToplevel() : nullptr);
    }

    bool UltraCanvasWaylandWindow::StartNativeFileDrag(const std::vector<std::string>&,
                                                       std::function<void(bool, bool)>) {
        // wl_data_device drags are a later step of the backend.
        return false;
    }

    bool UltraCanvasWaylandWindow::StartNativeSaveDrag(const std::string&,
                                                       std::function<void(const std::string&)>) {
        return false;
    }

} // namespace UltraCanvas
