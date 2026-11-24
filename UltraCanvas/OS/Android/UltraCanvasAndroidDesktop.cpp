// OS/Android/UltraCanvasAndroidDesktop.cpp
// Desktop mode: the embedded window manager on the phone's screen or on an
// external display, the pointer driven by a mouse or a touchpad, and the
// composite presented onto the output's ANativeWindow.
// Version: 1.0.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasAndroidDesktop.h"
#include "UltraCanvasAndroidApplication.h"
#include "UltraCanvasAndroidWindow.h"
#include "UltraCanvasAndroidJni.h"
#include "UltraCanvasEmbeddedWindowFrame.h"
#include "UltraCanvasDesktopShellBackend.h"
#include "UltraCanvasHostedApps.h"
#include "UltraCanvasDebug.h"

#include <android/configuration.h>
#include <android/input.h>
#include <android/log.h>
#include <android/looper.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <android_native_app_glue.h>
#include <jni.h>

#include <cairo/cairo.h>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <string>

namespace UltraCanvas {

    namespace {
        constexpr int64_t kDoubleClickMs = 400;
        constexpr int kDoubleClickDistance = 6;   // logical px

        int64_t NowNs() {
            return std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count();
        }

        // A void activity method taking one boolean; false when the activity
        // has none (a plain NativeActivity) or it threw.
        bool CallActivityBool(const char* name, bool value) {
            JNIEnv* env = AndroidJni::GetEnv();
            jobject activity = AndroidJni::GetActivity();
            if (!env || !activity) return false;
            jclass cls = env->GetObjectClass(activity);
            jmethodID mid = cls ? env->GetMethodID(cls, name, "(Z)V") : nullptr;
            if (!mid) {
                env->ExceptionClear();
                if (cls) env->DeleteLocalRef(cls);
                return false;
            }
            env->CallVoidMethod(activity, mid, value ? JNI_TRUE : JNI_FALSE);
            const bool failed = AndroidJni::ClearException(env, name);
            env->DeleteLocalRef(cls);
            return !failed;
        }

        void WakeGlueThread() {
            android_app* app = UltraCanvasAndroidApplication::GetAndroidApp();
            if (app && app->looper) ALooper_wake(app->looper);
        }

        // The phone's density as a scale, as the phone backend reads it.
        float PhoneScale() {
            android_app* app = UltraCanvasAndroidApplication::GetAndroidApp();
            if (app && app->config) {
                const int32_t density = AConfiguration_getDensity(app->config);
                if (density >= 60 && density <= 1000) return static_cast<float>(density) / 160.0f;
            }
            return 1.0f;
        }

        void Log(int priority, const std::string& line) {
            __android_log_write(priority, "UltraCanvas", line.c_str());
        }
    }

    // ===== THE SHELL'S ANSWERS =====

    class UltraCanvasAndroidDesktop::ShellHost : public DesktopShellBackend::EmbeddedHost {
    public:
        explicit ShellHost(UltraCanvasAndroidDesktop& d) : desktop(d) {}

        std::string Name() override { return "embedded (Android desktop mode)"; }

        std::vector<DesktopWindowInfo> ListWindows() override {
            std::vector<DesktopWindowInfo> out;
            const std::vector<HostedAppInfo> apps = UltraCanvasHostedApps::List();
            const uint64_t focused = desktop.wm.GetFocused();
            for (const EmbeddedWindowInfo& w : desktop.wm.ListWindows()) {
                if (w.frameOf) continue;   // a title bar is part of its window
                DesktopWindowInfo info;
                info.id = w.id;
                info.title = w.title;
                const auto app = desktop.appOf.find(w.id);
                const std::string appId = app != desktop.appOf.end() ? app->second : std::string();
                for (const HostedAppInfo& a : apps) {
                    if (a.id == appId) {
                        info.appClass = a.name;
                        info.iconFile = a.iconFile;
                        break;
                    }
                }
                if (info.appClass.empty()) info.appClass = appId.empty() ? w.title : appId;
                info.appName = info.appClass;
                std::transform(info.appName.begin(), info.appName.end(), info.appName.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                info.virtualDesktop = 0;
                info.active = w.id == focused;
                info.minimized = w.minimized;
                info.maximized = w.maximized;
                info.skipTaskbar = !w.focusable || (w.layer != EmbeddedWindowLayer::Normal &&
                                                     w.layer != EmbeddedWindowLayer::AlwaysOnTop);
                out.push_back(info);
            }
            return out;
        }

        uint64_t GetActiveWindow() override { return desktop.wm.GetFocused(); }

        bool ActivateWindow(uint64_t id) override {
            if (!desktop.wm.HasWindow(id)) return false;
            desktop.wm.Activate(id);
            return true;
        }

        bool MinimizeWindow(uint64_t id) override {
            if (!desktop.wm.HasWindow(id)) return false;
            desktop.wm.Minimize(id);
            return true;
        }

        bool CloseWindow(uint64_t id) override {
            UltraCanvasAndroidWindow* window = desktop.WindowOf(id);
            if (!window) return false;
            // In front first: a "Save the changes?" must be seen.
            desktop.wm.Activate(id);
            desktop.PushSimpleEvent(window, static_cast<int>(UCEventType::WindowCloseRequest));
            return true;
        }

        bool GetWindowBounds(uint64_t id, int& x, int& y, int& width, int& height) override {
            EmbeddedWindowInfo info;
            if (!desktop.wm.GetInfo(id, info)) return false;
            const float s = desktop.outputScale;
            x = static_cast<int>(std::lround(info.rect.x * s));
            y = static_cast<int>(std::lround(info.rect.y * s));
            width = static_cast<int>(std::lround(info.rect.width * s));
            height = static_cast<int>(std::lround(info.rect.height * s));
            return true;
        }

        bool MoveWindowTo(uint64_t id, int x, int y) override {
            UltraCanvasAndroidWindow* window = desktop.WindowOf(id);
            if (!window) return false;
            window->SetWindowPosition(x, y);
            return true;
        }

        bool GetScreenSize(int& width, int& height) override {
            const Size2Di size = desktop.GetOutputPhysicalSize();
            width = size.width;
            height = size.height;
            return width > 0 && height > 0;
        }

        std::vector<DesktopScreenInfo> ListScreens() override {
            DesktopScreenInfo screen;
            const Size2Di size = desktop.GetOutputPhysicalSize();
            screen.width = size.width;
            screen.height = size.height;
            screen.primary = true;
            screen.number = 1;
            return {screen};
        }

        bool ReserveScreenEdges(uint64_t id, int left, int right, int top, int bottom) override {
            const float s = desktop.outputScale > 0 ? desktop.outputScale : 1.0f;
            auto toLogical = [s](int v) { return static_cast<int>(std::ceil(v / s)); };
            desktop.wm.ReserveEdges(id, toLogical(left), toLogical(right), toLogical(top), toLogical(bottom));
            return true;
        }

        bool ListApplications(int, std::vector<UCDesktopEntry>& out) override {
            for (const HostedAppInfo& app : UltraCanvasHostedApps::List()) {
                UCDesktopEntry entry;
                entry.kind = UCDesktopEntry::Kind::Application;
                entry.name = app.name;
                entry.comment = app.comment;
                entry.iconName = app.iconFile;
                entry.iconFile = app.iconFile;
                entry.categories = app.categories;
                entry.startupWMClass = app.name;
                entry.shortcutPath = UltraCanvasHostedApps::PathFor(app.id);
                out.push_back(entry);
            }
            return true;
        }

        bool LaunchShortcut(const std::string& shortcutPath, std::string& error) override {
            const std::string id = UltraCanvasHostedApps::IdFromPath(shortcutPath);
            if (id.empty()) return false;   // not ours: the default application's
            return UltraCanvasHostedApps::Launch(id, {}, &error);
        }

    private:
        UltraCanvasAndroidDesktop& desktop;
    };

    void UltraCanvasAndroidDesktop::WindowsChanged() {
        DesktopShellBackend::NotifyEmbeddedChange();
    }

    UltraCanvasAndroidDesktop& UltraCanvasAndroidDesktop::Get() {
        static UltraCanvasAndroidDesktop desktop;
        return desktop;
    }

    UltraCanvasAndroidDesktop::UltraCanvasAndroidDesktop() {
        wm.onGeometryChanged = [this](uint64_t id, const Rect2Di& rect) { OnGeometryChanged(id, rect); };
        wm.onFocusChanged = [this](uint64_t lost, uint64_t gained) { OnFocusChanged(lost, gained); };
        wm.onStateChanged = [this](uint64_t id) { OnStateChanged(id); };
    }

    UltraCanvasAndroidDesktop::~UltraCanvasAndroidDesktop() {
        if (shellHost && DesktopShellBackend::GetEmbeddedHost() == shellHost.get()) {
            DesktopShellBackend::SetEmbeddedHost(nullptr);
        }
    }

    // ===== THE APPLICATION'S CALLS =====

    void UltraCanvasAndroidDesktop::Enable() {
        if (enabled) return;
        enabled = true;
        shellHost = std::make_unique<ShellHost>(*this);
        DesktopShellBackend::SetEmbeddedHost(shellHost.get());
        auto* app = UltraCanvasAndroidApplication::GetInstance();
        phone.window = app ? app->GetNativeWindow() : nullptr;
        ChooseOutput();
        // UltraCanvasActivity looks for a presentation display from now on
        // and reports its surface; a plain NativeActivity has no such method
        // and the desktop stays on the phone.
        if (!CallActivityBool("setDesktopMode", true)) {
            Log(ANDROID_LOG_WARN, "desktop mode: the activity has no setDesktopMode - "
                                  "no external display (UltraCanvasActivity is needed)");
        }
        Log(ANDROID_LOG_INFO, "desktop mode enabled");
    }

    std::shared_ptr<UltraCanvasWindowBase> UltraCanvasAndroidDesktop::CreatePhoneWindow(const WindowConfig& config) {
        creatingPhoneWindow = true;
        std::shared_ptr<UltraCanvasWindowBase> window = CreateWindow(config);
        creatingPhoneWindow = false;
        return window;
    }

    bool UltraCanvasAndroidDesktop::IsPhoneWindow(const UltraCanvasWindowBase* window) const {
        return window && phoneWindow && window == static_cast<const UltraCanvasWindowBase*>(phoneWindow);
    }

    bool UltraCanvasAndroidDesktop::TakesNewWindow() const {
        return enabled && !creatingPhoneWindow;
    }

    void UltraCanvasAndroidDesktop::SetExternalScale(float scale) {
        externalScaleSetting = scale > 0.0f ? scale : 0.0f;
        if (desktopOutput == Output::External) {
            desktopOutput = Output::Nowhere;   // forces the rebuild
            ChooseOutput();
        }
    }

    Size2Di UltraCanvasAndroidDesktop::GetOutputLogicalSize() const {
        return wm.GetOutputSize();
    }

    Size2Di UltraCanvasAndroidDesktop::GetOutputPhysicalSize() const {
        const OutputSurface& out = desktopOutput == Output::External ? external : phone;
        if (out.width > 0 && out.height > 0) return Size2Di(out.width, out.height);
        const Size2Di logical = wm.GetOutputSize();
        return Size2Di(static_cast<int>(std::lround(logical.width * outputScale)),
                       static_cast<int>(std::lround(logical.height * outputScale)));
    }

    UltraCanvasAndroidWindow* UltraCanvasAndroidDesktop::WindowOf(uint64_t id) const {
        auto it = windows.find(id);
        return it == windows.end() ? nullptr : it->second;
    }

    // ===== OUTPUTS =====

    float UltraCanvasAndroidDesktop::ScaleFor(const OutputSurface& surface, bool isPhone) const {
        if (isPhone) return PhoneScale();
        if (externalScaleSetting > 0.0f) return externalScaleSetting;
        // A TV is far away and a monitor near, and neither says which: by the
        // number of lines, in quarter steps - 1 for 1080 lines or fewer, 1.25
        // at 1440, 2 at 2160.
        const double lines = std::max(1, surface.height);
        const double quarter = std::round(lines / 1080.0 * 4.0) / 4.0;
        return static_cast<float>(std::clamp(quarter, 1.0, 3.0));
    }

    void UltraCanvasAndroidDesktop::PhoneSurfaceChanged(ANativeWindow* window) {
        phone.window = window;
        if (!window) {
            phone.width = phone.height = 0;
        }
        if (!enabled) return;
        if (desktopOutput == Output::Phone || !window) {
            desktopOutput = Output::Nowhere;   // re-measure the phone's surface
        }
        ChooseOutput();
    }

    void UltraCanvasAndroidDesktop::ChooseOutput() {
        if (!enabled) return;
        const Output wanted = external.window ? Output::External
                                              : (phone.window ? Output::Phone : Output::Nowhere);
        if (wanted == Output::Phone) {
            // The phone's own size: a buffer geometry set on the window hides
            // it (the phone backend's note on rotation).
            ANativeWindow_setBuffersGeometry(phone.window, 0, 0, WINDOW_FORMAT_RGBX_8888);
            phone.width = ANativeWindow_getWidth(phone.window);
            phone.height = ANativeWindow_getHeight(phone.window);
        }
        const Output previous = desktopOutput;
        OutputSurface& out = wanted == Output::External ? external : phone;
        const float scale = wanted == Output::Nowhere ? outputScale : ScaleFor(out, wanted == Output::Phone);
        const bool sameSize = composed && out.width == cairo_image_surface_get_width(composed) &&
                              out.height == cairo_image_surface_get_height(composed);
        if (wanted == previous && scale == outputScale && (wanted == Output::Nowhere || sameSize)) {
            if (wanted == Output::Phone) {
                ANativeWindow_setBuffersGeometry(phone.window, phone.width, phone.height,
                                                 WINDOW_FORMAT_RGBX_8888);
            }
            return;
        }
        desktopOutput = wanted;
        const bool scaleChanged = scale != outputScale;
        outputScale = scale;

        if (wanted != Output::Nowhere && out.width > 0 && out.height > 0) {
            wm.SetOutputSize(std::max(1, static_cast<int>(std::floor(out.width / scale))),
                             std::max(1, static_cast<int>(std::floor(out.height / scale))));
        }
        RebuildComposedSurface();
        if (scaleChanged) {
            for (auto& [id, window] : windows) window->DesktopScaleChanged();
        }
        wm.DamageAll();
        presentedOnce = false;

        if (previous != wanted) {
            std::string where = wanted == Output::External ? "the external display" :
                                wanted == Output::Phone ? "the phone's screen" : "nowhere";
            Log(ANDROID_LOG_INFO, "desktop output: " + where + " " + std::to_string(out.width) + "x" +
                                  std::to_string(out.height) + " px, scale " +
                                  std::to_string(static_cast<int>(std::lround(scale * 100))) + "%");
            // The phone shows its own window again once the desktop left it.
            if (phoneWindow && previous == Output::Phone && wanted != Output::Phone) {
                phoneWindow->HandleNativeSurfaceResized();
                phoneWindow->AddDirtyRectangle(Rect2Di(0, 0, phoneWindow->GetConfig().width,
                                                       phoneWindow->GetConfig().height));
                phoneWindow->RequestWindowComposition();
            }
            wm.SetPointerVisible(wanted == Output::External);
            UpdatePointerCapture();
            if (onOutputChanged) onOutputChanged(wanted);
        }
    }

    void UltraCanvasAndroidDesktop::RebuildComposedSurface() {
        if (composed) {
            cairo_surface_destroy(composed);
            composed = nullptr;
        }
        const OutputSurface& out = desktopOutput == Output::External ? external : phone;
        if (desktopOutput == Output::Nowhere || !out.window || out.width <= 0 || out.height <= 0) return;
        ANativeWindow_setBuffersGeometry(out.window, out.width, out.height, WINDOW_FORMAT_RGBX_8888);
        composed = cairo_image_surface_create(CAIRO_FORMAT_RGB24, out.width, out.height);
        if (cairo_surface_status(composed) != CAIRO_STATUS_SUCCESS) {
            cairo_surface_destroy(composed);
            composed = nullptr;
        }
    }

    void UltraCanvasAndroidDesktop::ComposeAndPresent() {
        if (desktopOutput == Output::Nowhere || !composed || !wm.HasDamage()) return;
        const OutputSurface& out = desktopOutput == Output::External ? external : phone;
        if (!out.window) return;

        const std::vector<Rect2Di> damage = wm.TakeDamage();
        const Rect2Di drawn = wm.Compose(composed, outputScale, [this](uint64_t id) -> cairo_surface_t* {
            // The window manager knows which windows are shown.
            UltraCanvasAndroidWindow* window = WindowOf(id);
            return window ? static_cast<cairo_surface_t*>(window->GetNativeSurface()) : nullptr;
        }, damage);
        if (drawn.width <= 0 || drawn.height <= 0) return;

        const int surfW = cairo_image_surface_get_width(composed);
        const int surfH = cairo_image_surface_get_height(composed);
        ARect dirty;
        if (presentedOnce) {
            dirty.left = drawn.x;
            dirty.top = drawn.y;
            dirty.right = drawn.x + drawn.width;
            dirty.bottom = drawn.y + drawn.height;
        } else {
            dirty.left = 0;
            dirty.top = 0;
            dirty.right = surfW;
            dirty.bottom = surfH;
        }
        ANativeWindow_Buffer buffer;
        if (ANativeWindow_lock(out.window, &buffer, &dirty) != 0) return;

        // The lock may hand back a larger rectangle than asked (a buffer whose
        // old content is not kept): the composed surface holds every pixel,
        // so that rectangle is copied whole.
        const unsigned char* src = cairo_image_surface_get_data(composed);
        const int srcStride = cairo_image_surface_get_stride(composed);
        const int right = std::min({static_cast<int>(dirty.right), surfW, static_cast<int>(buffer.width)});
        const int bottom = std::min({static_cast<int>(dirty.bottom), surfH, static_cast<int>(buffer.height)});
        auto* dst = static_cast<uint32_t*>(buffer.bits);
        for (int y = std::max(0, static_cast<int>(dirty.top)); y < bottom; ++y) {
            const auto* srcRow = reinterpret_cast<const uint32_t*>(src + y * srcStride);
            uint32_t* dstRow = dst + y * buffer.stride;
            for (int x = std::max(0, static_cast<int>(dirty.left)); x < right; ++x) {
                const uint32_t px = srcRow[x];
                dstRow[x] = (px & 0xFF00FF00u) | ((px & 0x00FF0000u) >> 16) | ((px & 0x000000FFu) << 16);
            }
        }
        ANativeWindow_unlockAndPost(out.window);

        if (!presentedOnce) {
            presentedOnce = true;
            // For the emulator smoke test, which waits for this line.
            Log(ANDROID_LOG_INFO, std::string("desktop frame presented on ") +
                    (desktopOutput == Output::External ? "external display" : "phone") + ": " +
                    std::to_string(surfW) + "x" + std::to_string(surfH) + ", " +
                    std::to_string(wm.ListApplicationWindows().size()) + " window(s)");
        }
    }

    void UltraCanvasAndroidDesktop::UpdatePointerCapture() {
        const bool want = enabled && desktopOutput == Output::External;
        if (want == pointerCaptureWanted) return;
        pointerCaptureWanted = want;
        CallActivityBool("setPointerCaptureWanted", want);
    }

    // ===== THE EVENT LOOP'S TURN =====

    void UltraCanvasAndroidDesktop::Pump() {
        if (!enabled) return;
        std::vector<PendingOp> ops;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            ops.swap(pending);
        }
        for (const PendingOp& op : ops) ApplyPending(op);
        ComposeAndPresent();
    }

    void UltraCanvasAndroidDesktop::QueueExternalSurface(ANativeWindow* window, int width, int height,
                                                         int densityDpi) {
        PendingOp op;
        op.kind = PendingOp::Kind::ExternalSurface;
        op.window = window;
        op.width = width;
        op.height = height;
        op.densityDpi = densityDpi;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            pending.push_back(op);
        }
        WakeGlueThread();
    }

    void UltraCanvasAndroidDesktop::QueueExternalSurfaceGone() {
        PendingOp op;
        op.kind = PendingOp::Kind::ExternalGone;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            pending.push_back(op);
        }
        WakeGlueThread();
    }

    void UltraCanvasAndroidDesktop::QueueCapturedPointer(int action, int actionButton, int buttonState,
                                                         float dx, float dy, float vscroll, float hscroll) {
        PendingOp op;
        op.kind = PendingOp::Kind::Pointer;
        op.action = action;
        op.actionButton = actionButton;
        op.buttonState = buttonState;
        op.dx = dx;
        op.dy = dy;
        op.vscroll = vscroll;
        op.hscroll = hscroll;
        {
            std::lock_guard<std::mutex> lock(pendingMutex);
            // Consecutive motions are summed rather than queued one by one.
            if (!pending.empty() && pending.back().kind == PendingOp::Kind::Pointer &&
                pending.back().buttonState == buttonState && vscroll == 0 && hscroll == 0 &&
                pending.back().vscroll == 0 && pending.back().hscroll == 0) {
                pending.back().dx += dx;
                pending.back().dy += dy;
            } else {
                pending.push_back(op);
            }
        }
        WakeGlueThread();
    }

    void UltraCanvasAndroidDesktop::ApplyPending(const PendingOp& op) {
        switch (op.kind) {
            case PendingOp::Kind::ExternalSurface: {
                if (external.window && external.window != op.window && external.owned) {
                    ANativeWindow_release(external.window);
                } else if (external.window == op.window && external.owned && op.window) {
                    ANativeWindow_release(op.window);   // the same window again: one reference is enough
                }
                external.window = op.window;
                external.width = op.width;
                external.height = op.height;
                external.densityDpi = op.densityDpi;
                external.owned = true;
                if (desktopOutput == Output::External) desktopOutput = Output::Nowhere;   // rebuild at the new size
                ChooseOutput();
                break;
            }
            case PendingOp::Kind::ExternalGone:
                if (external.window && external.owned) ANativeWindow_release(external.window);
                external = OutputSurface();
                if (composed && desktopOutput == Output::External) {
                    cairo_surface_destroy(composed);
                    composed = nullptr;
                }
                ChooseOutput();
                break;
            case PendingOp::Kind::Pointer: {
                if (desktopOutput != Output::External) break;
                wm.SetPointerVisible(true);
                if (op.dx != 0.0f || op.dy != 0.0f) MovePointerBy(op.dx, op.dy);
                // Buttons by what changed in the state: primary, secondary,
                // tertiary (MotionEvent.BUTTON_*).
                const int changed = op.buttonState ^ capturedButtons;
                const struct { int bit; UCMouseButton button; } map[] = {
                    {1, UCMouseButton::Left}, {2, UCMouseButton::Right}, {4, UCMouseButton::Middle}};
                for (const auto& m : map) {
                    if (changed & m.bit) PointerButton(m.button, (op.buttonState & m.bit) != 0);
                }
                capturedButtons = op.buttonState;
                if (op.vscroll != 0.0f || op.hscroll != 0.0f) {
                    Scroll(op.vscroll > 0 ? 1 : (op.vscroll < 0 ? -1 : 0),
                           op.hscroll > 0 ? 1 : (op.hscroll < 0 ? -1 : 0));
                }
                break;
            }
        }
    }

    // ===== WINDOWS =====

    EmbeddedWindowSpec UltraCanvasAndroidDesktop::SpecFor(UltraCanvasAndroidWindow* window) const {
        const WindowConfig& cfg = window->GetConfig();
        EmbeddedWindowSpec spec;
        const float scale = outputScale > 0 ? outputScale : 1.0f;
        spec.rect = Rect2Di(cfg.x >= 0 ? static_cast<int>(std::lround(cfg.x / scale)) : -1,
                            cfg.y >= 0 ? static_cast<int>(std::lround(cfg.y / scale)) : -1,
                            cfg.width, cfg.height);
        spec.title = cfg.title;
        spec.resizable = cfg.resizable;
        spec.minSize = Size2Di(std::max(1, cfg.minWidth), std::max(1, cfg.minHeight));
        spec.maxSize = Size2Di(cfg.maxWidth, cfg.maxHeight);
        switch (cfg.type) {
            case WindowType::Desktop:
                spec.layer = EmbeddedWindowLayer::Desktop;
                spec.fillsOutput = true;
                spec.decorated = false;
                break;
            case WindowType::Fullscreen:
                spec.fillsOutput = true;
                spec.decorated = false;
                break;
            case WindowType::Overlay:
            case WindowType::SessionLock:
                spec.layer = EmbeddedWindowLayer::Lock;
                spec.fillsOutput = true;
                spec.decorated = false;
                break;
            case WindowType::Popup:
                spec.layer = EmbeddedWindowLayer::Popup;
                spec.decorated = false;
                spec.focusable = false;
                break;
            case WindowType::Tooltip:
                spec.layer = EmbeddedWindowLayer::Tooltip;
                spec.decorated = false;
                spec.focusable = false;
                break;
            case WindowType::Notification:
                spec.layer = EmbeddedWindowLayer::Notification;
                spec.decorated = false;
                spec.focusable = false;
                break;
            case WindowType::Borderless:
                spec.decorated = false;
                break;
            default:   // Standard, Dialog, Tool
                spec.decorated = true;
                break;
        }
        if (window->IsDesktopFrame()) {
            // Placed by AttachFrame: never cascaded like a window of its own.
            spec.rect.x = 0;
            spec.rect.y = 0;
            spec.decorated = false;
            spec.focusable = false;
            spec.resizable = false;
        }
        if (cfg.alwaysOnTop && spec.layer == EmbeddedWindowLayer::Normal) {
            spec.layer = EmbeddedWindowLayer::AlwaysOnTop;
        }
        if (cfg.parentWindow) {
            const uint64_t parentId = IdOf(cfg.parentWindow);
            if (windows.count(parentId)) spec.transientFor = parentId;
        }
        return spec;
    }

    Rect2Di UltraCanvasAndroidDesktop::WindowCreated(UltraCanvasAndroidWindow* window) {
        const uint64_t id = IdOf(window);
        const EmbeddedWindowSpec spec = SpecFor(window);
        windows[id] = window;
        // Whose window: the application being started, else that of the
        // window it belongs to.
        std::string app = UltraCanvasHostedApps::Launching();
        if (app.empty() && spec.transientFor) {
            auto parent = appOf.find(spec.transientFor);
            if (parent != appOf.end()) app = parent->second;
        }
        appOf[id] = app;
        return wm.AddWindow(id, spec);
    }

    void UltraCanvasAndroidDesktop::WindowDestroyed(UltraCanvasAndroidWindow* window) {
        if (window == phoneWindow) {
            phoneWindow = nullptr;
            return;
        }
        const uint64_t id = IdOf(window);
        if (!windows.count(id)) return;

        // Its title bar goes with it.
        auto frameIt = frames.find(id);
        if (frameIt != frames.end()) {
            std::shared_ptr<UltraCanvasEmbeddedWindowFrame> frame = frameIt->second;
            frames.erase(frameIt);
            if (frame) frame->Close();
        }
        // A title bar closed by itself (the program shutting down) is
        // forgotten by its client.
        for (auto it = frames.begin(); it != frames.end(); ++it) {
            if (it->second && it->second->GetWindow() == window) {
                frames.erase(it);
                break;
            }
        }
        destroyingId = id;
        wm.RemoveWindow(id);
        destroyingId = 0;
        windows.erase(id);
        appOf.erase(id);
        WindowsChanged();
    }

    void UltraCanvasAndroidDesktop::MakeFrame(UltraCanvasAndroidWindow* window) {
        const uint64_t id = IdOf(window);
        EmbeddedWindowInfo info;
        if (!wm.GetInfo(id, info) || !info.decorated || info.frameId) return;
        const WindowConfig& cfg = window->GetConfig();

        UltraCanvasEmbeddedWindowFrame::Actions actions;
        actions.beginMove = [this, id]() { wm.BeginMove(id); };
        actions.minimize = [this, id]() { wm.Minimize(id); };
        actions.toggleMaximize = [this, id]() { wm.ToggleMaximize(id); };
        actions.close = [this, id]() {
            if (UltraCanvasAndroidWindow* w = WindowOf(id)) {
                PushSimpleEvent(w, static_cast<int>(UCEventType::WindowCloseRequest));
            }
        };
        auto frame = UltraCanvasEmbeddedWindowFrame::Create(cfg.title, info.rect.width, wm.titleBarHeight,
                                                            cfg.minimizable, cfg.maximizable && cfg.resizable,
                                                            cfg.closable, actions);
        if (!frame || !frame->GetWindow()) return;
        frames[id] = frame;
        wm.AttachFrame(id, IdOf(frame->GetWindow()));
        frame->SetActive(wm.GetFocused() == id);
        frame->SetMaximized(info.maximized);
    }

    void UltraCanvasAndroidDesktop::WindowShown(UltraCanvasAndroidWindow* window) {
        const uint64_t id = IdOf(window);
        if (!windows.count(id)) return;
        if (!window->IsDesktopFrame()) MakeFrame(window);
        wm.Show(id);
        auto frameIt = frames.find(id);
        if (frameIt != frames.end() && frameIt->second && frameIt->second->GetWindow()) {
            frameIt->second->GetWindow()->Show();
        }
        WindowsChanged();
    }

    void UltraCanvasAndroidDesktop::WindowHidden(UltraCanvasAndroidWindow* window) {
        const uint64_t id = IdOf(window);
        if (!windows.count(id)) return;
        wm.Hide(id);
        auto frameIt = frames.find(id);
        if (frameIt != frames.end() && frameIt->second && frameIt->second->GetWindow()) {
            frameIt->second->GetWindow()->Hide();
        }
        WindowsChanged();
    }

    void UltraCanvasAndroidDesktop::WindowPresented(UltraCanvasAndroidWindow* window) {
        wm.WindowPresented(IdOf(window));
    }

    void UltraCanvasAndroidDesktop::WindowTitleChanged(UltraCanvasAndroidWindow* window) {
        const uint64_t id = IdOf(window);
        wm.SetTitle(id, window->GetConfig().title);
        auto frameIt = frames.find(id);
        if (frameIt != frames.end() && frameIt->second) frameIt->second->SetTitle(window->GetConfig().title);
        WindowsChanged();
    }

    void UltraCanvasAndroidDesktop::WindowCursorChanged(UltraCanvasAndroidWindow* window, UCMouseCursor cursor) {
        wm.SetWindowCursor(IdOf(window), cursor);
    }

    void UltraCanvasAndroidDesktop::OnGeometryChanged(uint64_t id, const Rect2Di& rect) {
        if (UltraCanvasAndroidWindow* window = WindowOf(id)) window->DesktopGeometryChanged(rect);
    }

    void UltraCanvasAndroidDesktop::OnFocusChanged(uint64_t lostId, uint64_t gainedId) {
        if (lostId && lostId != destroyingId) {
            if (UltraCanvasAndroidWindow* w = WindowOf(lostId)) {
                PushSimpleEvent(w, static_cast<int>(UCEventType::WindowBlur));
            }
            auto it = frames.find(lostId);
            if (it != frames.end() && it->second) it->second->SetActive(false);
        }
        if (gainedId) {
            if (UltraCanvasAndroidWindow* w = WindowOf(gainedId)) {
                PushSimpleEvent(w, static_cast<int>(UCEventType::WindowFocus));
            }
            auto it = frames.find(gainedId);
            if (it != frames.end() && it->second) it->second->SetActive(true);
        }
        WindowsChanged();
    }

    void UltraCanvasAndroidDesktop::OnStateChanged(uint64_t id) {
        EmbeddedWindowInfo info;
        if (!wm.GetInfo(id, info)) return;
        if (UltraCanvasAndroidWindow* w = WindowOf(id)) {
            w->SetDesktopState(info.minimized ? WindowState::Minimized
                               : info.maximized ? WindowState::Maximized : WindowState::Normal);
        }
        auto it = frames.find(id);
        if (it != frames.end() && it->second) it->second->SetMaximized(info.maximized);
        WindowsChanged();
    }

    UltraCanvasAndroidWindow* UltraCanvasAndroidDesktop::GetKeyboardWindow() const {
        if (UltraCanvasAndroidWindow* w = WindowOf(wm.GetKeyboardTarget())) return w;
        return nullptr;
    }

    void UltraCanvasAndroidDesktop::ActivityFocusChanged(bool focused) {
        if (!enabled) return;
        if (desktopOutput == Output::External) {
            if (phoneWindow) {
                PushSimpleEvent(phoneWindow, static_cast<int>(focused ? UCEventType::WindowFocus
                                                                      : UCEventType::WindowBlur));
            }
        } else if (UltraCanvasAndroidWindow* w = GetKeyboardWindow()) {
            PushSimpleEvent(w, static_cast<int>(focused ? UCEventType::WindowFocus : UCEventType::WindowBlur));
        }
        if (focused) {
            pointerCaptureWanted = !pointerCaptureWanted;   // asks again: a focus loss drops the capture
            UpdatePointerCapture();
        }
    }

    // ===== POINTER EVENTS =====

    void UltraCanvasAndroidDesktop::PushSimpleEvent(UltraCanvasAndroidWindow* window, int type) {
        auto* app = UltraCanvasAndroidApplication::GetInstance();
        if (!app || !window) return;
        UCEvent event;
        event.type = static_cast<UCEventType>(type);
        event.targetWindow = window->GetWindowWeakPtr();
        event.nativeWindowHandle = window->GetNativeHandle();
        app->PushEvent(event);
    }

    void UltraCanvasAndroidDesktop::DeliverRoute(const EmbeddedPointerRoute& route, int type,
                                                 UCMouseButton button, int wheelDelta, bool doubleClick) {
        auto* app = UltraCanvasAndroidApplication::GetInstance();
        if (!app) return;
        if (route.leftWindowId) {
            if (UltraCanvasAndroidWindow* left = WindowOf(route.leftWindowId)) {
                PushSimpleEvent(left, static_cast<int>(UCEventType::MouseLeave));
            }
        }
        UltraCanvasAndroidWindow* window = WindowOf(route.windowId);
        if (!window) return;
        UCEvent event;
        event.type = doubleClick ? UCEventType::MouseDoubleClick : static_cast<UCEventType>(type);
        event.targetWindow = window->GetWindowWeakPtr();
        event.nativeWindowHandle = window->GetNativeHandle();
        event.pointerWindow = route.local;
        event.pointer = route.local;
        const Point2Di at = wm.GetPointerPosition();
        event.pointerGlobal = Point2Di(static_cast<int>(std::lround(at.x * outputScale)),
                                       static_cast<int>(std::lround(at.y * outputScale)));
        event.button = button;
        event.wheelDelta = wheelDelta;
        app->PushEvent(event);

        // A press on a title bar: the application saw a press in the frame's
        // window and moved its focus there; the window it decorates keeps it.
        if (window->IsDesktopFrame() && event.type == UCEventType::MouseDown) {
            if (UltraCanvasAndroidWindow* client = GetKeyboardWindow()) {
                PushSimpleEvent(client, static_cast<int>(UCEventType::WindowFocus));
            }
        }
    }

    void UltraCanvasAndroidDesktop::PointerMoveTo(Point2Di position) {
        const EmbeddedPointerRoute route = wm.PointerMove(position);
        DeliverRoute(route, static_cast<int>(UCEventType::MouseMove), UCMouseButton::NoneButton, 0, false);
    }

    void UltraCanvasAndroidDesktop::PointerPressAt(Point2Di position, UCMouseButton button, bool down,
                                                   int64_t timeNs) {
        const EmbeddedPointerRoute route = wm.PointerButton(position, down);
        bool doubleClick = false;
        if (down && button == UCMouseButton::Left) {
            const int64_t deltaMs = (timeNs - lastPressNs) / 1'000'000;
            if (lastPressNs != 0 && deltaMs <= kDoubleClickMs &&
                std::abs(position.x - lastPressAt.x) <= kDoubleClickDistance &&
                std::abs(position.y - lastPressAt.y) <= kDoubleClickDistance) {
                doubleClick = true;
                lastPressNs = 0;
            } else {
                lastPressNs = timeNs;
                lastPressAt = position;
            }
        }
        DeliverRoute(route, static_cast<int>(down ? UCEventType::MouseDown : UCEventType::MouseUp),
                     button, 0, doubleClick);
    }

    void UltraCanvasAndroidDesktop::MovePointerBy(float dx, float dy) {
        if (!enabled) return;
        PointerMoveTo(wm.MovePointerBy(dx, dy));
    }

    void UltraCanvasAndroidDesktop::PointerButton(UCMouseButton button, bool down) {
        if (!enabled) return;
        PointerPressAt(wm.GetPointerPosition(), button, down, NowNs());
    }

    void UltraCanvasAndroidDesktop::Click(UCMouseButton button) {
        PointerButton(button, true);
        PointerButton(button, false);
    }

    void UltraCanvasAndroidDesktop::Scroll(int vertical, int horizontal) {
        if (!enabled) return;
        const EmbeddedPointerRoute route = wm.PointerMove(wm.GetPointerPosition());
        if (vertical != 0) {
            DeliverRoute(route, static_cast<int>(UCEventType::MouseWheel), UCMouseButton::NoneButton,
                         vertical, false);
        }
        if (horizontal != 0) {
            EmbeddedPointerRoute again = route;
            again.leftWindowId = 0;
            DeliverRoute(again, static_cast<int>(UCEventType::MouseWheelHorizontal), UCMouseButton::NoneButton,
                         horizontal, false);
        }
    }

    bool UltraCanvasAndroidDesktop::HandleMotionEvent(AInputEvent* event) {
        if (!enabled) return false;
        const int32_t source = AInputEvent_getSource(event);
        const bool isMouse = (source & AINPUT_SOURCE_MOUSE) == AINPUT_SOURCE_MOUSE;
        const int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        const float rawX = AMotionEvent_getX(event, 0);
        const float rawY = AMotionEvent_getY(event, 0);
        const int64_t timeNs = AMotionEvent_getEventTime(event);
        const int32_t buttons = AMotionEvent_getButtonState(event);
        const UCMouseButton pressed = (buttons & AMOTION_EVENT_BUTTON_SECONDARY) ? UCMouseButton::Right
                                    : (buttons & AMOTION_EVENT_BUTTON_TERTIARY) ? UCMouseButton::Middle
                                    : UCMouseButton::Left;

        if (desktopOutput == Output::Phone) {
            // The desktop is on this screen: the finger or the mouse is where
            // it points.
            const Point2Di at(static_cast<int>(std::lround(rawX / outputScale)),
                              static_cast<int>(std::lround(rawY / outputScale)));
            wm.SetPointerVisible(isMouse);
            switch (action) {
                case AMOTION_EVENT_ACTION_DOWN:
                    PointerMoveTo(at);
                    PointerPressAt(at, isMouse ? pressed : UCMouseButton::Left, true, timeNs);
                    lastMouseButton = isMouse ? pressed : UCMouseButton::Left;
                    break;
                case AMOTION_EVENT_ACTION_UP:
                case AMOTION_EVENT_ACTION_CANCEL:
                    PointerPressAt(at, lastMouseButton, false, timeNs);
                    break;
                case AMOTION_EVENT_ACTION_MOVE:
                case AMOTION_EVENT_ACTION_HOVER_MOVE:
                    PointerMoveTo(at);
                    break;
                case AMOTION_EVENT_ACTION_SCROLL: {
                    PointerMoveTo(at);
                    const float v = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_VSCROLL, 0);
                    const float h = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HSCROLL, 0);
                    Scroll(v > 0 ? 1 : (v < 0 ? -1 : 0), h > 0 ? 1 : (h < 0 ? -1 : 0));
                    break;
                }
                default:
                    break;   // more fingers: the desktop takes the first one only
            }
            return true;
        }

        if (desktopOutput == Output::External && isMouse) {
            // A mouse on the phone that is not captured (the capture was
            // refused or not granted yet): its motion moves the TV's pointer.
            wm.SetPointerVisible(true);
            if (haveMouseOnPhone) MovePointerBy(rawX - lastMouseX, rawY - lastMouseY);
            haveMouseOnPhone = true;
            lastMouseX = rawX;
            lastMouseY = rawY;
            switch (action) {
                case AMOTION_EVENT_ACTION_DOWN:
                    lastMouseButton = pressed;
                    PointerButton(pressed, true);
                    break;
                case AMOTION_EVENT_ACTION_UP:
                case AMOTION_EVENT_ACTION_CANCEL:
                    PointerButton(lastMouseButton, false);
                    break;
                case AMOTION_EVENT_ACTION_SCROLL: {
                    const float v = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_VSCROLL, 0);
                    const float h = AMotionEvent_getAxisValue(event, AMOTION_EVENT_AXIS_HSCROLL, 0);
                    Scroll(v > 0 ? 1 : (v < 0 ? -1 : 0), h > 0 ? 1 : (h < 0 ? -1 : 0));
                    break;
                }
                default:
                    break;
            }
            return true;
        }

        if (desktopOutput == Output::External && !phoneWindow) {
            // No window of the phone's own: its screen is the touchpad.
            Touchpad(event);
            return true;
        }
        return false;
    }

    // ===== TOUCHPAD =====

    void UltraCanvasAndroidDesktop::TouchpadInput(TouchPhase phase, int fingers, float x, float y, int64_t timeMs) {
        if (!enabled) return;
        wm.SetPointerVisible(true);
        switch (phase) {
            case TouchPhase::Down:
                if (pad.fingers == 0) {
                    // A second press soon after a tap, on the same spot: the
                    // button stays down while the finger moves (a drag).
                    pad.dragging = pad.lastTapMs != 0 && timeMs - pad.lastTapMs < 300;
                    if (pad.dragging) PointerButton(UCMouseButton::Left, true);
                    pad.downMs = timeMs;
                    pad.travelled = 0;
                    pad.maxFingers = fingers;
                }
                pad.fingers = fingers;
                pad.maxFingers = std::max(pad.maxFingers, fingers);
                pad.lastX = x;
                pad.lastY = y;
                pad.scrollCarry = 0;
                break;
            case TouchPhase::Move: {
                const float dx = x - pad.lastX, dy = y - pad.lastY;
                pad.lastX = x;
                pad.lastY = y;
                pad.travelled += std::fabs(dx) + std::fabs(dy);
                if (fingers >= 2) {
                    // Two fingers scroll, a notch every 24 px.
                    pad.scrollCarry += dy;
                    while (pad.scrollCarry >= 24) { Scroll(1, 0); pad.scrollCarry -= 24; }
                    while (pad.scrollCarry <= -24) { Scroll(-1, 0); pad.scrollCarry += 24; }
                } else {
                    // A little acceleration: slow motions stay precise.
                    const float speed = std::hypot(dx, dy);
                    const float gain = speed > 12 ? 2.2f : speed > 4 ? 1.6f : 1.1f;
                    MovePointerBy(dx * gain, dy * gain);
                }
                break;
            }
            case TouchPhase::Up:
                pad.fingers = fingers;
                // The centre of the fingers still down moved: no motion.
                pad.lastX = x;
                pad.lastY = y;
                if (fingers > 0) break;
                if (pad.dragging) {
                    PointerButton(UCMouseButton::Left, false);
                    pad.dragging = false;
                    pad.lastTapMs = 0;
                } else if (timeMs - pad.downMs < 250 && pad.travelled < 12) {
                    // A tap clicks; a tap of two fingers is a right click.
                    Click(pad.maxFingers >= 2 ? UCMouseButton::Right : UCMouseButton::Left);
                    pad.lastTapMs = pad.maxFingers >= 2 ? 0 : timeMs;
                } else {
                    pad.lastTapMs = 0;
                }
                break;
        }
    }

    void UltraCanvasAndroidDesktop::Touchpad(AInputEvent* event) {
        const int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
        const int fingers = static_cast<int>(AMotionEvent_getPointerCount(event));
        const int64_t ms = AMotionEvent_getEventTime(event) / 1'000'000;
        // The centre of the fingers, so a two-finger scroll is smooth.
        float x = 0, y = 0;
        for (int i = 0; i < fingers; ++i) {
            x += AMotionEvent_getX(event, i);
            y += AMotionEvent_getY(event, i);
        }
        x /= std::max(1, fingers);
        y /= std::max(1, fingers);
        const float s = PhoneScale();
        switch (action) {
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_POINTER_DOWN:
                TouchpadInput(TouchPhase::Down, fingers, x / s, y / s, ms);
                break;
            case AMOTION_EVENT_ACTION_MOVE:
                TouchpadInput(TouchPhase::Move, fingers, x / s, y / s, ms);
                break;
            case AMOTION_EVENT_ACTION_POINTER_UP:
                TouchpadInput(TouchPhase::Up, fingers - 1, x / s, y / s, ms);
                break;
            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_CANCEL:
                TouchpadInput(TouchPhase::Up, 0, x / s, y / s, ms);
                break;
            default:
                break;
        }
    }

} // namespace UltraCanvas

// ===== JNI (UltraCanvasActivity, Java UI thread) =====

extern "C" JNIEXPORT void JNICALL
Java_org_ultraos_ultracanvas_UltraCanvasActivity_nativeOnExternalSurface(
        JNIEnv* env, jclass, jobject surface, jint width, jint height, jint densityDpi) {
    ANativeWindow* window = surface ? ANativeWindow_fromSurface(env, surface) : nullptr;   // a reference
    if (!window) return;
    UltraCanvas::UltraCanvasAndroidDesktop::Get().QueueExternalSurface(window, width, height, densityDpi);
}

extern "C" JNIEXPORT void JNICALL
Java_org_ultraos_ultracanvas_UltraCanvasActivity_nativeOnExternalSurfaceGone(JNIEnv*, jclass) {
    UltraCanvas::UltraCanvasAndroidDesktop::Get().QueueExternalSurfaceGone();
}

extern "C" JNIEXPORT void JNICALL
Java_org_ultraos_ultracanvas_UltraCanvasActivity_nativeOnCapturedPointer(
        JNIEnv*, jclass, jint action, jint actionButton, jint buttonState,
        jfloat dx, jfloat dy, jfloat vscroll, jfloat hscroll) {
    UltraCanvas::UltraCanvasAndroidDesktop::Get().QueueCapturedPointer(action, actionButton, buttonState,
                                                                      dx, dy, vscroll, hscroll);
}
