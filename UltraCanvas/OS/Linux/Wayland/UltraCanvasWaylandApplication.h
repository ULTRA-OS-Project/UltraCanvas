// OS/Linux/Wayland/UltraCanvasWaylandApplication.h
// The Wayland windowing backend's application: the connection to the
// compositor, the globals (compositor, shm, seat, outputs, xdg_wm_base,
// decorations, data devices), the event loop, and the seat's keyboard and
// pointer turned into UCEvents. Built in place of the X11 backend when the
// framework is configured with ULTRACANVAS_WAYLAND=ON (the public headers
// then alias UltraCanvasApplication to this class). Everything a client may
// do with the core protocol and xdg-shell lives here; the desktop shell's
// own protocols (layer-shell, foreign-toplevel) belong to the desktop shell
// backend, see Docs/UltraCanvas/WaylandDesktopShellProposal.md.
// Version: 0.1.0
// Last Modified: 2026-10-30
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVAS_WAYLAND_APPLICATION_H
#define ULTRACANVAS_WAYLAND_APPLICATION_H

// The public header defines the base class and, at its end, aliases
// UltraCanvasApplication to this class; the forward declaration lets
// either header be included first.
namespace UltraCanvas { class UltraCanvasWaylandApplication; }
#include "../../../include/UltraCanvasApplication.h"
#include "../../../include/UltraCanvasWindow.h"
#include "../../../include/UltraCanvasEvent.h"
#include "../../../include/UltraCanvasCommonTypes.h"

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

struct xdg_wm_base;
struct zxdg_decoration_manager_v1;
struct wl_cursor_theme;

namespace UltraCanvas {

    class UltraCanvasWaylandWindow;
    class UltraCanvasWaylandClipboard;

    // One wl_output as the compositor describes it. Positions and sizes are
    // in the compositor's logical space (the mode's pixels over the scale),
    // which is what every other backend reports as "screen pixels".
    struct WaylandOutput {
        wl_output* output = nullptr;
        uint32_t registryName = 0;
        int x = 0;
        int y = 0;
        int pixelWidth = 0;      // the current mode
        int pixelHeight = 0;
        int scale = 1;
        std::string name;        // "DP-1"; "" from a compositor older than wl_output v4
        std::string description;
        bool done = false;       // the first wl_output.done has arrived
        int Width() const { return scale > 0 ? pixelWidth / scale : pixelWidth; }
        int Height() const { return scale > 0 ? pixelHeight / scale : pixelHeight; }
    };

    class UltraCanvasWaylandApplication : public UltraCanvasApplicationBase {
    public:
        UltraCanvasWaylandApplication();
        ~UltraCanvasWaylandApplication() override;

        static UltraCanvasWaylandApplication* GetInstance() { return instance; }

        // ----- the globals, for the window, the clipboard and the cursor -----
        wl_display* GetDisplay() const { return display; }
        wl_compositor* GetCompositor() const { return compositor; }
        wl_shm* GetShm() const { return shm; }
        wl_seat* GetSeat() const { return seat; }
        xdg_wm_base* GetXdgWmBase() const { return xdgWmBase; }
        zxdg_decoration_manager_v1* GetDecorationManager() const { return decorationManager; }
        wl_data_device_manager* GetDataDeviceManager() const { return dataDeviceManager; }
        wl_data_device* GetDataDevice() const { return dataDevice; }
        // The serial of the last key or button the seat gave us: what
        // set_selection, popups and the like must quote.
        uint32_t GetLastInputSerial() const { return lastInputSerial; }
        uint32_t GetPointerEnterSerial() const { return pointerEnterSerial; }

        // The outputs, in the order the compositor announced them; the first
        // is taken as the primary (Wayland has no notion of one).
        const std::vector<WaylandOutput>& GetOutputs() const { return outputs; }
        const WaylandOutput* FindOutput(wl_output* output) const;
        // The largest output, or 0x0 before the compositor has described one.
        void GetScreenSize(int& width, int& height) const;

        // Where the pointer last was, in the logical pixels of the window it
        // is over (Wayland tells a client nothing about the screen). False
        // when the pointer has never entered one of our windows.
        bool GetLastPointerPosition(int& x, int& y) const;

        // The keyboard layout in use ("us", "de"), from the keymap the seat
        // handed over; "" before any.
        std::string GetKeyboardLayoutName() const;

        // Same meaning as on X11: the window between two presses of the same
        // button, and how far apart they may be, for a double click.
        void SetDoubleClickTime(unsigned int milliseconds) { doubleClickTimeMs = milliseconds; }
        void SetDoubleClickDistance(int pixels) { doubleClickDistance = pixels; }

        bool SelectMouseCursorNative(UltraCanvasWindowBase* win, UCMouseCursor cur) override;
        bool SelectMouseCursorNative(UltraCanvasWindowBase* win, UCMouseCursor cur,
                                     const char* filename, int hotspotX, int hotspotY) override;

        // Called by the window when the keyboard or pointer focus it held is
        // gone with it, so no event names a destroyed surface.
        void ForgetWindow(UltraCanvasWaylandWindow* window);

        // Flush the request queue now (after a commit the compositor should
        // see before the loop next waits).
        void Flush();

    protected:
        bool InitializeNative() override;
        void ShutdownNative() override;
        void CaptureMouseNative() override;
        void ReleaseMouseNative() override;
        void CollectAndProcessNativeEvents() override;
        void WakeUpEventLoop() override;
        void InitializeWakeUp() override;
        void ShutdownWakeUp() override;
        FontStyle DetectSystemFontStyleNative() override;
        FontStyle DetectMonospacedFontStyleNative() override;
        void LoadBundledFontsNative() override;
        bool RegisterFontFileNative(const std::string& fontFilePath) override;

    private:
        friend struct WaylandListeners;

        static UltraCanvasWaylandApplication* instance;

        // ----- the connection -----
        wl_display* display = nullptr;
        wl_registry* registry = nullptr;
        wl_compositor* compositor = nullptr;
        wl_shm* shm = nullptr;
        wl_seat* seat = nullptr;
        uint32_t seatCapabilities = 0;
        xdg_wm_base* xdgWmBase = nullptr;
        zxdg_decoration_manager_v1* decorationManager = nullptr;
        wl_data_device_manager* dataDeviceManager = nullptr;
        wl_data_device* dataDevice = nullptr;
        std::vector<WaylandOutput> outputs;
        int wakeupFd = -1;

        // ----- the keyboard -----
        wl_keyboard* keyboard = nullptr;
        xkb_context* xkbContext = nullptr;
        xkb_keymap* xkbKeymap = nullptr;
        xkb_state* xkbState = nullptr;
        UltraCanvasWaylandWindow* keyboardWindow = nullptr;   // where the keyboard focus is
        uint32_t lastInputSerial = 0;
        bool modShift = false, modCtrl = false, modAlt = false, modMeta = false;
        int32_t repeatRate = 25;     // per second; 0 = no repeat
        int32_t repeatDelay = 600;   // ms
        uint32_t repeatKey = 0;      // the evdev key held, 0 = none
        TimerId repeatTimer = 0;
        void StartKeyRepeat(uint32_t key);
        void StopKeyRepeat();
        void PushKeyEvent(uint32_t key, bool pressed, bool repeat);

        // ----- the pointer -----
        wl_pointer* pointer = nullptr;
        UltraCanvasWaylandWindow* pointerWindow = nullptr;    // the surface it is over
        uint32_t pointerEnterSerial = 0;
        double pointerX = 0, pointerY = 0;    // logical, in pointerWindow
        bool pointerKnown = false;
        // What a wl_pointer frame has gathered so far (v5 compositors send
        // the parts of one motion or scroll as several events then a frame).
        double pendingAxisVertical = 0, pendingAxisHorizontal = 0;
        int pendingDiscreteVertical = 0, pendingDiscreteHorizontal = 0;
        bool pendingAxis = false;
        double residualAxisVertical = 0, residualAxisHorizontal = 0;
        void FlushPointerFrame();
        // Double clicks
        unsigned int doubleClickTimeMs = 250;
        int doubleClickDistance = 5;
        UltraCanvasWaylandWindow* lastClickWindow = nullptr;
        uint32_t lastClickTime = 0;
        uint32_t lastClickButton = 0;
        int lastClickX = 0, lastClickY = 0;

        // ----- the cursor -----
        wl_surface* cursorSurface = nullptr;
        std::unordered_map<int, wl_cursor_theme*> cursorThemes;   // by scale
        wl_cursor_theme* CursorThemeForScale(int scale);
        // A cursor drawn from an image file, as an shm buffer of its own.
        struct ImageCursor {
            wl_buffer* buffer = nullptr;
            void* map = nullptr;
            size_t mapSize = 0;
            int width = 0, height = 0, hotspotX = 0, hotspotY = 0, scale = 1;
        };
        std::unordered_map<std::string, ImageCursor> imageCursors;   // by "file@scale"
        ImageCursor* LoadImageCursor(const std::string& filename, int hotspotX, int hotspotY, int scale);
        void FreeImageCursors();
        bool ApplyCursor(UltraCanvasWaylandWindow* window);
        UCMouseCursor pointerCursor = UCMouseCursor::Default;
        std::string pointerCursorFile;
        int pointerCursorHotspotX = 0, pointerCursorHotspotY = 0;

        bool ConnectDisplay();
        void BindGlobal(uint32_t name, const char* interface, uint32_t version);
        void RemoveGlobal(uint32_t name);
        void UpdateSeatDevices();
        void DispatchPendingEvents();
        UCEvent MakeEvent(UltraCanvasWaylandWindow* window, UCEventType type) const;
        void FillModifiers(UCEvent& event) const;
    };

} // namespace UltraCanvas

#endif // ULTRACANVAS_WAYLAND_APPLICATION_H
