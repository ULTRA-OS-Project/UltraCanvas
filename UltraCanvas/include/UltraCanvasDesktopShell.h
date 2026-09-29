// include/UltraCanvasDesktopShell.h
// The running desktop as a shell sees it: the windows other applications
// have open and which one is active, the virtual desktops, the installed
// applications a launcher lists, a screenshot of the screen, the live state
// of the devices an info panel shows (webcam on, microphone on, speaker
// playing, Bluetooth powered, Wi-Fi joined, VPN up, traffic flowing, USB
// devices attached, battery level, keyboard layout), and the counts an
// application publishes for the desktop to show beside its icon.
//
//   for (const DesktopWindowInfo& w : UltraCanvasDesktopShell::ListWindows())
//       taskbar->AddToggleButton(std::to_string(w.id), "", w.iconFile, ...);
//   UltraCanvasDesktopShell::ActivateWindow(id);
//   UltraCanvasDesktopShell::SetCurrentVirtualDesktop(2);
//   UltraCanvasDesktopShell::CaptureScreen(UltraCanvasDesktopShell::DefaultScreenshotPath());
//   DesktopDeviceActivity now = UltraCanvasDesktopShell::ReadDeviceActivity();
//
//   UltraCanvasDesktopShellMonitor monitor;
//   monitor.Start([this]() { windowsDirty.store(true); });   // background thread!
//
// This is the module the ULTRA OS desktop (Apps/UltraDesktop) is built on,
// and the reason it is a module rather than desktop code: any application
// that wants a window list, a virtual-desktop switch or a screenshot gets
// the same one, and a second implementation of EWMH cannot disagree with
// the first about which windows exist.
//
// Distinct from UltraCanvasHardwareInfo, which describes the machine (and
// which this module reads for the network, USB and Bluetooth state), and
// from IODeviceManager, which operates peripherals: this module reports
// what is *happening* on the desktop right now and acts on windows, never
// on devices.
//
// Backends: Linux/BSD (EWMH over X11 for windows and desktops, XGetImage
// for the screenshot, procfs and sysfs for the device activity). Where none
// exists every query answers empty with IsAvailable() false, the monitor
// polls nothing, and the platform-neutral parts - the application list from
// desktop entries, the notices, the launcher - still work.
// Version: 1.0.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasDesktopEntry.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace UltraCanvas {

    // ===== A WINDOW ANOTHER APPLICATION HAS OPEN =====
    struct DesktopWindowInfo {
        uint64_t    id = 0;             // the window system's id (an X11 Window)
        std::string title;              // _NET_WM_NAME, else WM_NAME
        std::string appClass;           // WM_CLASS class, e.g. "UltraFiler"
        std::string appName;            // WM_CLASS instance, e.g. "ultrafiler"
        std::string iconFile;           // the app's icon, resolved from appClass through the
                                        // icon themes; "" when none is installed
        int  virtualDesktop = -1;       // 0-based; -1 = on every desktop
        int  processId = 0;             // _NET_WM_PID, 0 when the window does not say
        bool active = false;            // the focused window
        bool minimized = false;
        bool skipTaskbar = false;       // asked to stay out of taskbars: panels, docks,
                                        // the desktop itself, tooltips
    };

    // ===== WHAT THE DEVICES ARE DOING RIGHT NOW =====
    // Every field is best-effort: `warnings` says in words what could not be
    // read, and a value that could not be read is left at its default rather
    // than made up. Traffic is reported as running totals - the caller keeps
    // the previous reading and shows the difference.
    struct DesktopDeviceActivity {
        bool webcamInUse = false;       // a process holds a video capture device open
        bool microphoneInUse = false;   // an audio capture stream is running
        bool speakerPlaying = false;    // an audio playback stream is running

        bool bluetoothPresent = false;
        bool bluetoothPowered = false;
        int  bluetoothConnectedDevices = 0;

        bool wifiPresent = false;
        bool wifiConnected = false;
        std::string wifiSsid;
        bool lanConnected = false;      // a wired interface with carrier
        bool vpnConnected = false;      // a tunnel interface that is up
        bool internetInterfaceUp = false;  // any non-loopback interface with carrier

        uint64_t bytesReceived = 0;     // over every non-loopback interface
        uint64_t bytesSent = 0;

        int usbDeviceCount = 0;         // attached devices, hubs not counted

        bool batteryPresent = false;
        int  batteryPercent = -1;
        bool batteryCharging = false;

        std::string keyboardLayout;     // "us", "de", ... the layout in use; "" if unknown

        std::vector<std::string> warnings;
    };

    // ===== A COUNT AN APPLICATION PUBLISHES FOR THE DESKTOP =====
    // UltraMail publishes its unread count under "UltraMail"; the desktop
    // shows it as the badge on the mail icon. The transport is a small JSON
    // file per application in NoticesDirectory(), so a publisher needs no
    // link to the desktop and the desktop needs no link to the publisher.
    struct DesktopNotice {
        std::string application;        // the publisher's name, as it was given
        int         count = 0;
        std::string text;               // optional: "14 unread, 3 today"
        int64_t     updatedUnixSeconds = 0;
    };

    class UltraCanvasDesktopShell {
    public:
        // The platform backend: "x11" on Linux/BSD, "null" where there is none.
        static std::string GetBackendName();
        static bool        IsAvailable();

        // ===== WINDOWS =====
        // Every managed window in stacking order (bottom first), the desktop's
        // own and other skip-taskbar windows included so a caller can tell
        // them apart; a taskbar lists the ones with skipTaskbar false.
        static std::vector<DesktopWindowInfo> ListWindows();
        static uint64_t GetActiveWindow();
        // Raise, un-minimize and focus. Switches to the window's desktop.
        static bool ActivateWindow(uint64_t id);
        static bool MinimizeWindow(uint64_t id);
        // Ask the window to close (WM_DELETE_WINDOW through the manager), never kill.
        static bool CloseWindow(uint64_t id);

        // ===== VIRTUAL DESKTOPS =====
        static int  GetVirtualDesktopCount();
        static int  GetCurrentVirtualDesktop();          // 0-based, -1 when unknown
        static bool SetCurrentVirtualDesktop(int index);
        static bool SetVirtualDesktopCount(int count);   // 1..32
        static bool MoveWindowToVirtualDesktop(uint64_t id, int index);   // -1 = every desktop

        // ===== THE SCREEN =====
        static bool GetScreenSize(int& width, int& height);
        // The whole screen, written as PNG to `pngPath`. The directory is
        // created. False with the reason in `error`.
        static bool CaptureScreen(const std::string& pngPath, std::string* error = nullptr);
        // "<Pictures>/Screenshots/Screenshot 2026-09-29 14.05.31.png" - the
        // user's Pictures folder through GetWellKnownUserFolders, the home
        // directory when there is none. Only names the file; CaptureScreen
        // creates the directory.
        static std::string DefaultScreenshotPath();

        // ===== DEVICES =====
        // Cheap enough to poll once a second: procfs and sysfs reads, and one
        // UltraCanvasHardwareInfo network / USB / Bluetooth listing.
        static DesktopDeviceActivity ReadDeviceActivity();

        // ===== APPLICATIONS =====
        // The installed applications a launcher lists: every menu-visible
        // desktop entry in the standard directories (XDG_DATA_HOME and
        // XDG_DATA_DIRS' applications folders), one per desktop-file id (a
        // user's override wins over the system's), sorted by name, with
        // iconFile resolved at `iconSize` px. Empty on a platform without
        // desktop entries.
        static std::vector<UCDesktopEntry> ListApplications(int iconSize = 48);
        // Start an application from its entry, detached: the launcher is never
        // its parent and never waits for it. False with the reason in `error`.
        static bool LaunchApplication(const UCDesktopEntry& entry,
                                      const std::vector<std::string>& files = {},
                                      std::string* error = nullptr);
        // Start a program by name - "UltraFiler" - looked up next to this
        // executable first (a build tree, a bundle), then on PATH.
        static bool LaunchProgram(const std::string& program,
                                  const std::vector<std::string>& arguments = {},
                                  std::string* error = nullptr);
        // Where LaunchProgram would find `program`; "" when it would not.
        static std::string FindProgram(const std::string& program);

        // ===== NOTICES =====
        // Publish `count` (and an optional line of text) under `application`.
        // Zero keeps the notice with a zero count, so a desktop can tell "no
        // mail" from "no mail client"; RemoveNotice withdraws it. Safe from
        // any thread; the file is replaced atomically.
        static bool PublishNotice(const std::string& application, int count,
                                  const std::string& text = "");
        static bool RemoveNotice(const std::string& application);
        static std::vector<DesktopNotice> ReadNotices();
        static DesktopNotice ReadNotice(const std::string& application);
        // "$XDG_RUNTIME_DIR/ultraos/notices", else "$XDG_CACHE_HOME/ultraos/notices",
        // else "~/.cache/ultraos/notices". Created on the first publish.
        static std::string NoticesDirectory();
    };

    // ===== THE MONITOR =====
    // Reports when the window list, the active window, the current virtual
    // desktop or the number of desktops changes. The callback runs on the
    // monitor's own thread and must do nothing but hand the news over (set an
    // atomic, post to the UI thread). One change can produce several
    // callbacks; the receiver coalesces. Where the platform cannot notify,
    // Start() succeeds and nothing is ever reported: IsNative() says which.
    class UltraCanvasDesktopShellMonitor {
    public:
        using ChangedCallback = std::function<void()>;

        UltraCanvasDesktopShellMonitor();
        ~UltraCanvasDesktopShellMonitor();

        UltraCanvasDesktopShellMonitor(const UltraCanvasDesktopShellMonitor&) = delete;
        UltraCanvasDesktopShellMonitor& operator=(const UltraCanvasDesktopShellMonitor&) = delete;

        bool Start(ChangedCallback onChanged);
        // Joins the thread, so no callback runs after this returns. Safe twice.
        void Stop();

        bool IsRunning() const { return running.load(); }
        bool IsNative() const { return native; }

    private:
        std::atomic<bool> running{false};
        bool native = false;
        std::thread worker;
        ChangedCallback callback;
        // The backend's own state (its connection, its wake pipe).
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

} // namespace UltraCanvas
