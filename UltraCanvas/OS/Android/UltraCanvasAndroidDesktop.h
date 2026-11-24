// OS/Android/UltraCanvasAndroidDesktop.h
// Desktop mode for the Android backend: many windows on one output, kept by
// UltraCanvasEmbeddedWindowManager, with title bars, a pointer and a desktop
// beneath - what MobilePC shows on a TV or monitor.
//
// The output is the external display when one is connected (a USB-C cable or
// dock, an OEM wireless display: any display Android offers for a
// Presentation), else the phone's own screen. While the desktop is on the
// external display, the phone shows the one window the app made with
// CreatePhoneWindow (MobilePC's control pad: a touchpad and keyboard), and a
// mouse is captured (View.requestPointerCapture) so its motion drives the
// pointer on the TV whichever display Android would put its own pointer on.
//
// Without Enable() nothing here runs and the backend is the one-window phone
// backend it always was.
//
// Version: 1.0.0
// Last Modified: 2026-11-24
// Author: UltraCanvas Framework
#pragma once

#ifndef ULTRACANVAS_ANDROID_DESKTOP_H
#define ULTRACANVAS_ANDROID_DESKTOP_H

#include "UltraCanvasCommonTypes.h"
#include "UltraCanvasEmbeddedWindowManager.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct ANativeWindow;
struct AInputEvent;
typedef struct _cairo_surface cairo_surface_t;

namespace UltraCanvas {

    class UltraCanvasAndroidWindow;
    class UltraCanvasEmbeddedWindowFrame;
    class UltraCanvasWindowBase;
    struct WindowConfig;
    enum class UCMouseButton;

    class UltraCanvasAndroidDesktop {
    public:
        enum class Output { None, Phone, External };

        static UltraCanvasAndroidDesktop& Get();

        // ===== FOR THE APPLICATION =====
        // Turns desktop mode on. Call after the application is initialised
        // and before the windows that belong on the desktop are made; every
        // window made afterwards is a window of the desktop, except the one
        // CreatePhoneWindow makes.
        void Enable();
        bool IsEnabled() const { return enabled; }
        // The window the phone shows while the desktop is on an external
        // display (hidden while the desktop is on the phone's own screen).
        std::shared_ptr<UltraCanvasWindowBase> CreatePhoneWindow(const WindowConfig& config);
        bool HasExternalDisplay() const { return external.window != nullptr; }
        Output GetDesktopOutput() const { return desktopOutput; }
        // Called on the UI thread when the desktop moves to the other output.
        std::function<void(Output)> onOutputChanged;
        // The external display's scale (physical pixels a logical one); 0
        // (the default) chooses by its height: 1 up to 1080 lines, 2 at 4K.
        void SetExternalScale(float scale);
        float GetOutputScale() const { return outputScale; }
        // The desktop's size in logical pixels.
        Size2Di GetOutputLogicalSize() const;
        // The desktop's size in physical pixels.
        Size2Di GetOutputPhysicalSize() const;

        // The pointer, driven by a touchpad (MobilePC's control pad): relative
        // motion in the desktop's logical pixels, buttons and the wheel.
        void MovePointerBy(float dx, float dy);
        void PointerButton(UCMouseButton button, bool down);
        void Click(UCMouseButton button);
        void Scroll(int vertical, int horizontal);

        // A touchpad's fingers (the phone's screen as one, or MobilePC's
        // control pad): one finger moves the pointer, a tap clicks, a tap of
        // two fingers right-clicks, two fingers moving scroll, and a tap
        // followed by a press drags. Positions in logical pixels of the
        // touchpad; `fingers` is how many are down after this change.
        enum class TouchPhase { Down, Move, Up };
        void TouchpadInput(TouchPhase phase, int fingers, float x, float y, int64_t timeMs);

        UltraCanvasEmbeddedWindowManager& GetWindowManager() { return wm; }
        // The window behind a window-manager id, or null.
        UltraCanvasAndroidWindow* WindowOf(uint64_t id) const;
        static uint64_t IdOf(const UltraCanvasWindowBase* window) {
            return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(window));
        }

        // ===== FOR THE BACKEND (glue thread) =====
        // Whether this window, about to be made, is one of the desktop's.
        bool TakesNewWindow() const;
        bool IsPhoneWindow(const UltraCanvasWindowBase* window) const { return window && window == phoneWindow; }
        UltraCanvasAndroidWindow* GetPhoneWindow() const { return phoneWindow; }
        void SetPhoneWindow(UltraCanvasAndroidWindow* window) { phoneWindow = window; }

        // Joins the window manager; the rectangle it is given there, in the
        // desktop's logical pixels.
        Rect2Di WindowCreated(UltraCanvasAndroidWindow* window);
        void WindowDestroyed(UltraCanvasAndroidWindow* window);
        void WindowShown(UltraCanvasAndroidWindow* window);
        void WindowHidden(UltraCanvasAndroidWindow* window);
        void WindowPresented(UltraCanvasAndroidWindow* window);
        void WindowTitleChanged(UltraCanvasAndroidWindow* window);
        void WindowCursorChanged(UltraCanvasAndroidWindow* window, UCMouseCursor cursor);

        // The phone's surface came, changed size, or went (activity commands).
        void PhoneSurfaceChanged(ANativeWindow* window);
        // Whether the desktop is drawn on the phone's surface right now.
        bool DesktopOnPhone() const { return enabled && desktopOutput == Output::Phone; }

        // Input from the phone's own surface. Returns true when the desktop
        // took it (the desktop is on the phone, or it is a mouse).
        bool HandleMotionEvent(AInputEvent* event);
        // The window keyboard input goes to: the desktop's focused window.
        UltraCanvasAndroidWindow* GetKeyboardWindow() const;
        // The activity gained or lost the input focus.
        void ActivityFocusChanged(bool focused);

        // Once per turn of the event loop, before it waits: what the external
        // display and the captured mouse sent, then the composite.
        void Pump();

        // From the Java UI thread (JNI); queued for the glue thread.
        void QueueExternalSurface(ANativeWindow* window, int width, int height, int densityDpi);
        void QueueExternalSurfaceGone();
        void QueueCapturedPointer(int action, int actionButton, int buttonState,
                                  float dx, float dy, float vscroll, float hscroll);

    private:
        UltraCanvasAndroidDesktop();

        struct OutputSurface {
            ANativeWindow* window = nullptr;   // a reference held (external) or borrowed (phone)
            int width = 0, height = 0;         // physical px
            int densityDpi = 0;
            bool owned = false;
        };

        struct PendingOp {
            enum class Kind { ExternalSurface, ExternalGone, Pointer };
            Kind kind = Kind::Pointer;
            ANativeWindow* window = nullptr;
            int width = 0, height = 0, densityDpi = 0;
            int action = 0, actionButton = 0, buttonState = 0;
            float dx = 0, dy = 0, vscroll = 0, hscroll = 0;
        };

        bool enabled = false;
        bool creatingPhoneWindow = false;
        UltraCanvasAndroidWindow* phoneWindow = nullptr;
        UltraCanvasEmbeddedWindowManager wm;
        std::map<uint64_t, UltraCanvasAndroidWindow*> windows;
        std::map<uint64_t, std::shared_ptr<UltraCanvasEmbeddedWindowFrame>> frames;   // by client id

        OutputSurface phone;
        OutputSurface external;
        Output desktopOutput = Output::None;
        float outputScale = 1.0f;
        float externalScaleSetting = 0.0f;
        cairo_surface_t* composed = nullptr;   // the output's pixels, kept between frames
        bool presentedOnce = false;
        bool pointerCaptureWanted = false;
        int capturedButtons = 0;
        // The last absolute position of an uncaptured mouse on the phone.
        bool haveMouseOnPhone = false;
        float lastMouseX = 0, lastMouseY = 0;
        UCMouseButton lastMouseButton{};
        // Tap to double click, as the phone backend does.
        int64_t lastPressNs = 0;
        Point2Di lastPressAt;
        // The window being removed: no blur is sent to it.
        uint64_t destroyingId = 0;

        struct TouchpadState {
            int fingers = 0;
            int maxFingers = 0;
            float lastX = 0, lastY = 0;
            float travelled = 0;
            float scrollCarry = 0;
            int64_t downMs = 0;
            int64_t lastTapMs = 0;
            bool dragging = false;
        } pad;
        void Touchpad(AInputEvent* event);

        std::mutex pendingMutex;
        std::vector<PendingOp> pending;

        void ChooseOutput();
        float ScaleFor(const OutputSurface& surface, bool isPhone) const;
        void RebuildComposedSurface();
        void ComposeAndPresent();
        void ApplyPending(const PendingOp& op);
        void UpdatePointerCapture();

        EmbeddedWindowSpec SpecFor(UltraCanvasAndroidWindow* window) const;
        void MakeFrame(UltraCanvasAndroidWindow* window);
        void OnGeometryChanged(uint64_t id, const Rect2Di& rect);
        void OnFocusChanged(uint64_t lostId, uint64_t gainedId);
        void OnStateChanged(uint64_t id);

        // Pointer events into windows, as the manager routes them.
        void DeliverRoute(const EmbeddedPointerRoute& route, int type, UCMouseButton button,
                          int wheelDelta, bool doubleClick);
        void PointerMoveTo(Point2Di position);
        void PointerPressAt(Point2Di position, UCMouseButton button, bool down, int64_t timeNs);
        void PushSimpleEvent(UltraCanvasAndroidWindow* window, int type);
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_ANDROID_DESKTOP_H
