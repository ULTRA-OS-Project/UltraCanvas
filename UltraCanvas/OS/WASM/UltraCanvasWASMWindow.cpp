// OS/WASM/UltraCanvasWASMWindow.cpp
// WebAssembly (Emscripten) platform window implementation
// Version: 2.0.0
// Last Modified: 2026-08-12
// Author: UltraCanvas Framework

#include "../../include/UltraCanvasApplication.h"
#include "UltraCanvasWASMWindow.h"
#include "UltraCanvasWASMApplication.h"
#include "../../include/UltraCanvasDebug.h"

#include <cmath>
#include <cstring>

namespace UltraCanvas {

    unsigned long UltraCanvasWASMWindow::lastWindowId = 0;

// ===== CONSTRUCTOR / DESTRUCTOR =====

    UltraCanvasWASMWindow::UltraCanvasWASMWindow() = default;

    UltraCanvasWASMWindow::~UltraCanvasWASMWindow() {
        DestroyNative();
    }

// ===== WINDOW LIFECYCLE =====

    bool UltraCanvasWASMWindow::CreateNative() {
        if (canvasCreated) {
            return true;
        }

        windowId = ++lastWindowId;
        canvasId = "ultracanvas-window-" + std::to_string(windowId);
        canvasSelector = "#" + canvasId;

        RefreshDeviceScale();

        if (config_.type == WindowType::Fullscreen) {
            GetScreenSize(config_.width, config_.height);
            config_.x = 0;
            config_.y = 0;
        }

        CreateCanvasElement();
        canvasCreated = true;

        if (!RecreateNativeSurface()) {
            DestroyCanvasElement();
            canvasCreated = false;
            return false;
        }

        RegisterEventCallbacks();
        return true;
    }

    void UltraCanvasWASMWindow::DestroyNative() {
        if (!canvasCreated) {
            return;
        }
        UnregisterEventCallbacks();
        if (nativeSurface) {
            cairo_surface_destroy(static_cast<cairo_surface_t*>(nativeSurface));
            nativeSurface = nullptr;
        }
        DestroyCanvasElement();
        canvasCreated = false;
    }

// ===== CANVAS ELEMENT =====

    void UltraCanvasWASMWindow::CreateCanvasElement() {
        const int x = (config_.x >= 0) ? config_.x : 0;
        const int y = (config_.y >= 0) ? config_.y : 0;
        EM_ASM({
            var id = UTF8ToString($0);
            var canvas = document.getElementById(id);
            if (!canvas) {
                canvas = document.createElement('canvas');
                canvas.id = id;
                document.body.appendChild(canvas);
            }
            canvas.tabIndex = 0;                 // focusable, receives focus on click
            canvas.style.position = 'absolute';
            canvas.style.left = $1 + 'px';
            canvas.style.top = $2 + 'px';
            canvas.style.outline = 'none';
            canvas.style.touchAction = 'none';   // gestures go to the app, not the page
            canvas.style.display = 'none';       // shown by Show()
            window.__ultracanvasZ = (window.__ultracanvasZ || 1000) + 1;
            canvas.style.zIndex = window.__ultracanvasZ;
            canvas.oncontextmenu = function(e) { e.preventDefault(); };
        }, canvasId.c_str(), x, y);
        UpdateCanvasElementGeometry();
    }

    void UltraCanvasWASMWindow::DestroyCanvasElement() {
        EM_ASM({
            var canvas = document.getElementById(UTF8ToString($0));
            if (canvas && canvas.parentNode) {
                canvas.parentNode.removeChild(canvas);
            }
        }, canvasId.c_str());
    }

    void UltraCanvasWASMWindow::UpdateCanvasElementGeometry() {
        // Backing store in physical px, CSS box in logical px — the same
        // logical/physical split every backend uses for HiDPI.
        const int physW = LogicalToPhysical(config_.width);
        const int physH = LogicalToPhysical(config_.height);
        EM_ASM({
            var canvas = document.getElementById(UTF8ToString($0));
            if (!canvas) return;
            canvas.width = $1;
            canvas.height = $2;
            canvas.style.width = $3 + 'px';
            canvas.style.height = $4 + 'px';
        }, canvasId.c_str(), physW, physH, config_.width, config_.height);
    }

// ===== NATIVE SURFACE =====

    float UltraCanvasWASMWindow::QueryNativeDeviceScale() const {
        double ratio = emscripten_get_device_pixel_ratio();
        return (ratio > 0.0) ? static_cast<float>(ratio) : 1.0f;
    }

    bool UltraCanvasWASMWindow::RecreateNativeSurface() {
        const int physW = LogicalToPhysical(config_.width);
        const int physH = LogicalToPhysical(config_.height);

        // Build the new surface before destroying the old one (Linux/Windows
        // do the same, so a failed resize keeps the last good surface).
        cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, physW, physH);
        if (cairo_surface_status(s) != CAIRO_STATUS_SUCCESS) {
            debugOutput << "UltraCanvas WASM: failed to create " << physW << "x" << physH
                        << " image surface" << std::endl;
            cairo_surface_destroy(s);
            return false;
        }
        cairo_surface_set_device_scale(s, deviceScale, deviceScale);
        cairo_surface_set_fallback_resolution(s, 96.0, 96.0);

        auto* old = static_cast<cairo_surface_t*>(nativeSurface);
        nativeSurface = s;
        if (old) {
            cairo_surface_destroy(old);
        }

        UpdateCanvasElementGeometry();
        return true;
    }

    void UltraCanvasWASMWindow::DoResizeNative() {
        // The base resized the window render context already; presentation
        // follows in the same UpdateAndRender() pass once the full dirty
        // rectangle it seeds has been re-rendered and composited.
        RecreateNativeSurface();
    }

// ===== PRESENTATION =====

    void UltraCanvasWASMWindow::InvalidateWindowNative() {
        PresentToCanvas();
    }

    void UltraCanvasWASMWindow::PresentToCanvas() {
        if (!nativeSurface || !canvasCreated) {
            return;
        }
        auto* surface = static_cast<cairo_surface_t*>(nativeSurface);
        cairo_surface_flush(surface);

        const unsigned char* data = cairo_image_surface_get_data(surface);
        const int w = cairo_image_surface_get_width(surface);
        const int h = cairo_image_surface_get_height(surface);
        const int stride = cairo_image_surface_get_stride(surface);
        if (!data || w <= 0 || h <= 0) {
            return;
        }

        // cairo RGB24 stores each pixel as a native-endian 32-bit xRGB word;
        // wasm is little-endian, so the bytes are B,G,R,x. Canvas ImageData
        // wants R,G,B,A.
        presentBuffer.resize(static_cast<size_t>(w) * h * 4);
        for (int y = 0; y < h; y++) {
            const unsigned char* src = data + static_cast<size_t>(y) * stride;
            uint8_t* dst = presentBuffer.data() + static_cast<size_t>(y) * w * 4;
            for (int x = 0; x < w; x++) {
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                dst[3] = 255;
                src += 4;
                dst += 4;
            }
        }

        EM_ASM({
            var canvas = document.getElementById(UTF8ToString($0));
            if (!canvas) return;
            var ctx = canvas.getContext('2d');
            if (!ctx) return;
            // With -pthread the heap is a SharedArrayBuffer, and ImageData
            // rejects views on shared buffers — copy into a fresh array.
            var pixels = new Uint8ClampedArray($2 * $3 * 4);
            pixels.set(new Uint8Array(HEAPU8.buffer, $1, $2 * $3 * 4));
            ctx.putImageData(new ImageData(pixels, $2, $3), 0, 0);
        }, canvasId.c_str(), reinterpret_cast<uintptr_t>(presentBuffer.data()), w, h);
    }

// ===== WINDOW OPERATIONS =====

    void UltraCanvasWASMWindow::Show() {
        if (!_created || _windowVisible) {
            return;
        }
        EM_ASM({
            var canvas = document.getElementById(UTF8ToString($0));
            if (canvas) canvas.style.display = 'block';
        }, canvasId.c_str());

        if (config_.type == WindowType::Fullscreen) {
            SetFullscreen(true);
        }

        _windowVisible = true;
        HandleWindowShown();
        if (onWindowShow) {
            onWindowShow();
        }
        RequestRedraw();
    }

    void UltraCanvasWASMWindow::Hide() {
        if (!_created || !_windowVisible) {
            return;
        }
        EM_ASM({
            var canvas = document.getElementById(UTF8ToString($0));
            if (canvas) canvas.style.display = 'none';
        }, canvasId.c_str());

        _windowVisible = false;
        HandleWindowHidden();
        if (onWindowHide) {
            onWindowHide();
        }
    }

    void UltraCanvasWASMWindow::RaiseAndFocus() {
        EM_ASM({
            var canvas = document.getElementById(UTF8ToString($0));
            if (!canvas) return;
            window.__ultracanvasZ = (window.__ultracanvasZ || 1000) + 1;
            canvas.style.zIndex = window.__ultracanvasZ;
            canvas.focus();
        }, canvasId.c_str());
    }

    void UltraCanvasWASMWindow::SetWindowTitle(const std::string& title) {
        config_.title = title;
        EM_ASM({
            document.title = UTF8ToString($0);
        }, title.c_str());
    }

    void UltraCanvasWASMWindow::SetWindowIcon(const std::string& iconPath) {
        // Icon files live in the Emscripten virtual FS, which cannot back a
        // favicon <link>; only record the path so GetConfig() reports it.
        config_.iconPath = iconPath;
    }

    void UltraCanvasWASMWindow::SetWindowSize(int width, int height) {
        if (width == config_.width && height == config_.height) {
            return;
        }
        // Deliver the resize the same way a native window system would:
        // through the event queue, so HandleResizeEvent() runs and the base
        // performs DoResize() on the next UpdateAndRender().
        UCEvent event;
        event.type = UCEventType::WindowResize;
        event.width = width;
        event.height = height;
        event.targetWindow = GetWindowWeakPtr();
        event.nativeWindowHandle = windowId;
        PushToApplication(event);
    }

    void UltraCanvasWASMWindow::SetWindowPosition(int x, int y) {
        config_.x = x;
        config_.y = y;
        EM_ASM({
            var canvas = document.getElementById(UTF8ToString($0));
            if (!canvas) return;
            canvas.style.left = $1 + 'px';
            canvas.style.top = $2 + 'px';
        }, canvasId.c_str(), x, y);
    }

    void UltraCanvasWASMWindow::SetResizable(bool resizable) {
        // Canvas geometry is fully app-controlled in the browser; there is no
        // user-driven window frame to lock.
    }

    void UltraCanvasWASMWindow::Minimize() {
        // No browser equivalent of an iconified window; hide the canvas so
        // the app's minimize semantics (invisible, state preserved) hold.
        Hide();
        _state = WindowState::Minimized;
    }

    void UltraCanvasWASMWindow::Maximize() {
        int screenW = 0, screenH = 0;
        GetScreenSize(screenW, screenH);
        SetWindowPosition(0, 0);
        SetWindowSize(screenW, screenH);
        _state = WindowState::Maximized;
    }

    void UltraCanvasWASMWindow::Restore() {
        if (_state == WindowState::Minimized) {
            Show();
        }
        _state = WindowState::Normal;
    }

    void UltraCanvasWASMWindow::SetFullscreen(bool fullscreen) {
        if (fullscreen) {
            emscripten_request_fullscreen(canvasSelector.c_str(), EM_TRUE);
            _state = WindowState::Fullscreen;
        } else {
            emscripten_exit_fullscreen();
            _state = WindowState::Normal;
        }
    }

    NativeWindowHandle UltraCanvasWASMWindow::GetNativeHandle() const {
        return windowId;
    }

    void UltraCanvasWASMWindow::GetScreenSize(int& width, int& height) const {
        // The "screen" is the browser viewport, in CSS (= logical) pixels.
        // The unused $0 keeps the macro portable across compilers that treat
        // empty __VA_ARGS__ differently.
        width = EM_ASM_INT({ return window.innerWidth; }, 0);
        height = EM_ASM_INT({ return window.innerHeight; }, 0);
    }

    void UltraCanvasWASMWindow::GetNativeWindowSize(int& w, int& h) const {
        // Window geometry (SetWindowPosition, GetScreenSize/Bounds) operates
        // in CSS px, so the native size equals the logical size — like macOS,
        // unlike X11/Windows where it is physical px.
        w = config_.width;
        h = config_.height;
    }

// ===== EVENT CALLBACKS =====

    UCEvent UltraCanvasWASMWindow::MakeInputEvent(UCEventType type) {
        UCEvent event;
        event.type = type;
        event.targetWindow = GetWindowWeakPtr();
        event.nativeWindowHandle = windowId;
        return event;
    }

    void UltraCanvasWASMWindow::PushToApplication(const UCEvent& event) {
        if (auto* app = UltraCanvasWASMApplication::GetInstance()) {
            app->PushEvent(event);
        }
    }

    void UltraCanvasWASMWindow::RegisterEventCallbacks() {
        const char* target = canvasSelector.c_str();
        emscripten_set_mousedown_callback(target, this, EM_TRUE, OnMouseCallback);
        emscripten_set_mouseup_callback(target, this, EM_TRUE, OnMouseCallback);
        emscripten_set_mousemove_callback(target, this, EM_TRUE, OnMouseCallback);
        emscripten_set_dblclick_callback(target, this, EM_TRUE, OnMouseCallback);
        emscripten_set_mouseenter_callback(target, this, EM_TRUE, OnMouseCallback);
        emscripten_set_mouseleave_callback(target, this, EM_TRUE, OnMouseCallback);
        emscripten_set_wheel_callback(target, this, EM_TRUE, OnWheelCallback);
        emscripten_set_touchstart_callback(target, this, EM_TRUE, OnTouchCallback);
        emscripten_set_touchmove_callback(target, this, EM_TRUE, OnTouchCallback);
        emscripten_set_touchend_callback(target, this, EM_TRUE, OnTouchCallback);
        emscripten_set_touchcancel_callback(target, this, EM_TRUE, OnTouchCallback);
        emscripten_set_focus_callback(target, this, EM_TRUE, OnFocusCallback);
        emscripten_set_blur_callback(target, this, EM_TRUE, OnFocusCallback);
    }

    void UltraCanvasWASMWindow::UnregisterEventCallbacks() {
        const char* target = canvasSelector.c_str();
        emscripten_set_mousedown_callback(target, nullptr, EM_TRUE, nullptr);
        emscripten_set_mouseup_callback(target, nullptr, EM_TRUE, nullptr);
        emscripten_set_mousemove_callback(target, nullptr, EM_TRUE, nullptr);
        emscripten_set_dblclick_callback(target, nullptr, EM_TRUE, nullptr);
        emscripten_set_mouseenter_callback(target, nullptr, EM_TRUE, nullptr);
        emscripten_set_mouseleave_callback(target, nullptr, EM_TRUE, nullptr);
        emscripten_set_wheel_callback(target, nullptr, EM_TRUE, nullptr);
        emscripten_set_touchstart_callback(target, nullptr, EM_TRUE, nullptr);
        emscripten_set_touchmove_callback(target, nullptr, EM_TRUE, nullptr);
        emscripten_set_touchend_callback(target, nullptr, EM_TRUE, nullptr);
        emscripten_set_touchcancel_callback(target, nullptr, EM_TRUE, nullptr);
        emscripten_set_focus_callback(target, nullptr, EM_TRUE, nullptr);
        emscripten_set_blur_callback(target, nullptr, EM_TRUE, nullptr);
    }

    static UCMouseButton ConvertDomMouseButton(unsigned short button) {
        switch (button) {
            case 0: return UCMouseButton::Left;
            case 1: return UCMouseButton::Middle;
            case 2: return UCMouseButton::Right;
            case 3: return UCMouseButton::Back;
            case 4: return UCMouseButton::Forward;
            default: return UCMouseButton::Unknown;
        }
    }

    EM_BOOL UltraCanvasWASMWindow::OnMouseCallback(int eventType, const EmscriptenMouseEvent* mouseEvent,
                                                   void* userData) {
        auto* win = static_cast<UltraCanvasWASMWindow*>(userData);
        if (!win) return EM_FALSE;

        UCEventType type;
        switch (eventType) {
            case EMSCRIPTEN_EVENT_MOUSEDOWN:  type = UCEventType::MouseDown; break;
            case EMSCRIPTEN_EVENT_MOUSEUP:    type = UCEventType::MouseUp; break;
            case EMSCRIPTEN_EVENT_MOUSEMOVE:  type = UCEventType::MouseMove; break;
            case EMSCRIPTEN_EVENT_DBLCLICK:   type = UCEventType::MouseDoubleClick; break;
            case EMSCRIPTEN_EVENT_MOUSEENTER: type = UCEventType::MouseEnter; break;
            case EMSCRIPTEN_EVENT_MOUSELEAVE: type = UCEventType::MouseLeave; break;
            default: return EM_FALSE;
        }

        UCEvent event = win->MakeInputEvent(type);
        // targetX/Y are CSS px relative to the canvas — already logical units,
        // so no PhysicalToLogical conversion here (unlike X11).
        event.pointerWindow = { static_cast<int>(mouseEvent->targetX), static_cast<int>(mouseEvent->targetY) };
        event.pointer = event.pointerWindow;
        event.pointerGlobal = { static_cast<int>(mouseEvent->clientX), static_cast<int>(mouseEvent->clientY) };
        event.shift = mouseEvent->shiftKey;
        event.ctrl = mouseEvent->ctrlKey;
        event.alt = mouseEvent->altKey;
        event.meta = mouseEvent->metaKey;
        if (type != UCEventType::MouseMove && type != UCEventType::MouseEnter &&
            type != UCEventType::MouseLeave) {
            event.button = ConvertDomMouseButton(mouseEvent->button);
        }
        win->PushToApplication(event);

        if (type == UCEventType::MouseDown) {
            // Keyboard focus follows the click (tabIndex makes the canvas
            // focusable, but not every browser focuses it automatically).
            EM_ASM({
                var canvas = document.getElementById(UTF8ToString($0));
                if (canvas) canvas.focus();
            }, win->canvasId.c_str());
        }
        return EM_TRUE;
    }

    EM_BOOL UltraCanvasWASMWindow::OnWheelCallback(int eventType, const EmscriptenWheelEvent* wheelEvent,
                                                   void* userData) {
        auto* win = static_cast<UltraCanvasWASMWindow*>(userData);
        if (!win) return EM_FALSE;

        // Normalise DOM deltas to wheel notches (±1 per notch, like every
        // desktop backend): ~100 px or 3 lines per notch, accumulated so
        // trackpad sub-notch deltas add up instead of being lost.
        double step;
        switch (wheelEvent->deltaMode) {
            case DOM_DELTA_LINE: step = 3.0; break;
            case DOM_DELTA_PAGE: step = 1.0; break;
            default:             step = 100.0; break; // DOM_DELTA_PIXEL
        }

        auto emit = [&](UCEventType type, int notches) {
            UCEvent event = win->MakeInputEvent(type);
            event.pointerWindow = { static_cast<int>(wheelEvent->mouse.targetX),
                                    static_cast<int>(wheelEvent->mouse.targetY) };
            event.pointer = event.pointerWindow;
            event.pointerGlobal = { static_cast<int>(wheelEvent->mouse.clientX),
                                    static_cast<int>(wheelEvent->mouse.clientY) };
            event.shift = wheelEvent->mouse.shiftKey;
            event.ctrl = wheelEvent->mouse.ctrlKey;
            event.alt = wheelEvent->mouse.altKey;
            event.meta = wheelEvent->mouse.metaKey;
            event.wheelDelta = notches;
            win->PushToApplication(event);
        };

        win->wheelAccumY += -wheelEvent->deltaY / step;   // DOM: positive = down; notches: positive = up
        int notchesY = static_cast<int>(std::trunc(win->wheelAccumY));
        if (notchesY != 0) {
            win->wheelAccumY -= notchesY;
            emit(UCEventType::MouseWheel, notchesY);
        }

        win->wheelAccumX += wheelEvent->deltaX / step;    // positive = right, matches X11 button 7
        int notchesX = static_cast<int>(std::trunc(win->wheelAccumX));
        if (notchesX != 0) {
            win->wheelAccumX -= notchesX;
            emit(UCEventType::MouseWheelHorizontal, notchesX);
        }
        return EM_TRUE;
    }

    EM_BOOL UltraCanvasWASMWindow::OnTouchCallback(int eventType, const EmscriptenTouchEvent* touchEvent,
                                                   void* userData) {
        auto* win = static_cast<UltraCanvasWASMWindow*>(userData);
        if (!win || touchEvent->numTouches <= 0) return EM_FALSE;

        const EmscriptenTouchPoint& touch = touchEvent->touches[0];

        UCEventType touchType;
        UCEventType mouseType;
        switch (eventType) {
            case EMSCRIPTEN_EVENT_TOUCHSTART:
                touchType = UCEventType::TouchStart;
                mouseType = UCEventType::MouseDown;
                break;
            case EMSCRIPTEN_EVENT_TOUCHMOVE:
                touchType = UCEventType::TouchMove;
                mouseType = UCEventType::MouseMove;
                break;
            case EMSCRIPTEN_EVENT_TOUCHEND:
            case EMSCRIPTEN_EVENT_TOUCHCANCEL:
                touchType = UCEventType::TouchEnd;
                mouseType = UCEventType::MouseUp;
                break;
            default:
                return EM_FALSE;
        }

        auto fill = [&](UCEvent& event) {
            event.pointerWindow = { static_cast<int>(touch.targetX), static_cast<int>(touch.targetY) };
            event.pointer = event.pointerWindow;
            event.pointerGlobal = { static_cast<int>(touch.clientX), static_cast<int>(touch.clientY) };
            event.shift = touchEvent->shiftKey;
            event.ctrl = touchEvent->ctrlKey;
            event.alt = touchEvent->altKey;
            event.meta = touchEvent->metaKey;
        };

        UCEvent event = win->MakeInputEvent(touchType);
        fill(event);
        win->PushToApplication(event);

        // No widget consumes Touch* events yet, so also synthesize the
        // equivalent left-button mouse event to make touch usable.
        UCEvent mouse = win->MakeInputEvent(mouseType);
        fill(mouse);
        if (mouseType != UCEventType::MouseMove) {
            mouse.button = UCMouseButton::Left;
        }
        win->PushToApplication(mouse);
        return EM_TRUE;
    }

    EM_BOOL UltraCanvasWASMWindow::OnFocusCallback(int eventType, const EmscriptenFocusEvent* focusEvent,
                                                   void* userData) {
        auto* win = static_cast<UltraCanvasWASMWindow*>(userData);
        if (!win) return EM_FALSE;

        UCEvent event = win->MakeInputEvent(
                (eventType == EMSCRIPTEN_EVENT_FOCUS) ? UCEventType::WindowFocus : UCEventType::WindowBlur);
        win->PushToApplication(event);
        return EM_TRUE;
    }

} // namespace UltraCanvas
