// include/UltraCanvasDesktopShell.h
// The running desktop as a shell sees it: the windows other applications
// have open and which one is active, the virtual desktops, the installed
// applications a launcher lists, a screenshot of the screen, the live state
// of the devices an info panel shows (webcam on, microphone on, speaker
// playing, Bluetooth powered, Wi-Fi joined, VPN up, traffic flowing, USB
// devices attached, battery level, keyboard layout), and the counts an
// application publishes for the desktop to show beside its icon.
//
// doc-check: std::shared_ptr<UltraCanvasToolbar> taskbar; std::atomic<bool> windowsDirty;
// doc-check: example
//   for (const DesktopWindowInfo& w : UltraCanvasDesktopShell::ListWindows()) {
//       if (w.skipTaskbar) continue;
//       const uint64_t id = w.id;
//       taskbar->AddToggleButton("win-" + std::to_string(id), "", w.iconFile, [id](bool on) {
//           if (on) UltraCanvasDesktopShell::ActivateWindow(id);
//           else UltraCanvasDesktopShell::MinimizeWindow(id);
//       });
//   }
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
// for the screenshot, procfs and sysfs for the device activity) and Windows
// (EnumWindows filtered as Explorer's taskbar filters, appbars for the screen
// edges, BitBlt for the screenshot, WinEvent hooks for the monitor,
// RegisterHotKey for shortcuts, the Start menu for the applications, the
// notification area's Shell_TrayWnd for the tray icons). Where
// none exists every query answers empty with IsAvailable() false, the
// monitor polls nothing, and the platform-neutral parts - the application
// list from desktop entries, the notices, the launcher - still work.
// Version: 1.5.0 - ListScreens; Microsoft Store applications on Windows;
//                  "Super" alone, a tap of the Win key
// Version: 1.4.0 - instead of the system's desktop (DesktopReplacement::Instead),
//                  the notification area (UltraCanvasTrayHost), EndSession
// Version: 1.3.0 - in place of the system's desktop (ReplaceSystemDesktop)
// Last Modified: 2026-10-15
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
        uint64_t    id = 0;             // the window system's id (an X11 Window, a HWND)
        std::string title;              // _NET_WM_NAME, else WM_NAME
        std::string appClass;           // WM_CLASS class, e.g. "UltraFiler"; on
                                        // Windows the program's name
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

    // ===== ONE SCREEN (A MONITOR) =====
    // Physical pixels of the whole virtual screen. On Windows the primary
    // monitor's top-left corner is 0,0, and a monitor left of or above it
    // lies at negative coordinates; on X11 they are the root window's.
    struct DesktopScreenInfo {
        int  x = 0;
        int  y = 0;
        int  width = 0;
        int  height = 0;
        bool primary = false;
        bool operator==(const DesktopScreenInfo& other) const {
            return x == other.x && y == other.y && width == other.width && height == other.height &&
                   primary == other.primary;
        }
    };

    // ===== A CAPTURE OF THE SCREEN, IN MEMORY =====
    // Rows top to bottom, `stride` bytes apart; four bytes per pixel in
    // memory order B, G, R, unused - the layout cairo calls RGB24 and the QR
    // scanner calls BGRA32 - so a row can be handed to either without a copy.
    struct DesktopScreenImage {
        int width  = 0;
        int height = 0;
        int stride = 0;                 // bytes between row starts, >= width * 4
        std::vector<uint8_t> pixels;    // height * stride bytes
        bool IsValid() const { return width > 0 && height > 0 && !pixels.empty(); }
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

    // ===== HOW A DESKTOP TAKES THE PLACE OF THE SYSTEM'S =====
    enum class DesktopReplacement {
        Off,        // the system's own desktop
        Beside,     // started at sign-in; the system's desktop keeps running
                    // beside it (Windows: Explorer, with its taskbar hidden)
        Instead     // the session's shell: the system's desktop does not
                    // start at all (Windows: the account's own shell setting)
    };

    // ===== AN ICON IN THE NOTIFICATION AREA =====
    // What a program puts in the system tray (Shell_NotifyIcon on Windows),
    // as the desktop hosting the tray sees it.
    struct DesktopTrayIcon {
        std::string id;                 // stable while the icon exists
        std::string tooltip;            // UTF-8; "" when the program gave none
        std::string iconFile;           // the icon as a PNG; "" when it has no image
        std::string appClass;           // the program's name, as DesktopWindowInfo's
        int processId = 0;
        // The last balloon the program showed ("Backup finished"). A new one
        // gets a new serial: a desktop shows each serial once.
        std::string balloonTitle;
        std::string balloonText;
        uint64_t    balloonSerial = 0;
    };

    enum class TrayClick { Left, Right, Middle };

    // ===== THE END OF THE SESSION =====
    enum class SessionEnd { SignOut, Restart, ShutDown };

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
        // The platform backend: "x11" on Linux/BSD, "win32" on Windows, "null"
        // where there is none.
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
        // Every monitor, the primary first (Windows: EnumDisplayMonitors;
        // X11: RandR's monitors, where no primary chosen means the one at the
        // origin). Where the platform tells none apart, the one screen
        // GetScreenSize reports; empty where it reports none either.
        // A desktop covers the ones its own window does not with windows of
        // WindowType::Desktop and WindowConfig::desktopArea.
        static std::vector<DesktopScreenInfo> ListScreens();
        // Reserve strips along the screen's edges for `windowId` - a desktop's
        // bars, a dock - so that maximised and tiled windows stop short of
        // them (EWMH _NET_WM_STRUT_PARTIAL on X11, an appbar per edge on
        // Windows - call it there from the window's own thread). Widths in physical
        // pixels, 0 for an edge that reserves nothing; the strip runs the
        // whole length of its edge. Call it again to change or, with four
        // zeros, to release. `windowId` is the window's native handle.
        static bool ReserveScreenEdges(uint64_t windowId, int left, int right, int top, int bottom);
        // The whole screen, written as PNG to `pngPath`. The directory is
        // created. False with the reason in `error`.
        static bool CaptureScreen(const std::string& pngPath, std::string* error = nullptr);
        // The whole screen as pixels in memory, for a caller that must not
        // leave a file behind: UltraAuthenticator reads an enrolment QR code
        // off the screen this way, and a screenshot of that QR on disk would
        // be the account's seed in the clear. Nothing is written anywhere.
        // False with the reason in `error`; `out` is then empty.
        static bool CaptureScreenImage(DesktopScreenImage& out, std::string* error = nullptr);
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
        // iconFile resolved at `iconSize` px. On Windows the Start menu's
        // program shortcuts, the user's shadowing the machine's (shortcutPath
        // set). Empty on a platform with neither.
        static std::vector<UCDesktopEntry> ListApplications(int iconSize = 48);
        // The application a window belongs to, among `applications`
        // (ListApplications): the entry whose StartupWMClass is the window's
        // WM_CLASS, else whose program, icon name or name the window's class
        // or instance spells (case aside: "Gimp-2.10" is gimp-2.10's).
        // nullptr when none matches.
        static const UCDesktopEntry* MatchApplication(const DesktopWindowInfo& window,
                                                      const std::vector<UCDesktopEntry>& applications);
        // Start an application from its entry, detached: the launcher is never
        // its parent and never waits for it. An entry from a Windows shortcut
        // opens the shortcut, as Explorer does. False with the reason in `error`.
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

        // ===== IN PLACE OF THE SYSTEM'S DESKTOP =====
        // Whether this user's sign-in starts `desktopProgram` (UltraDesktop)
        // as the desktop, and the way back. Per user: no other account
        // changes and no administrator rights are needed. Where the session
        // itself decides which desktop runs (ULTRA OS, Linux) or the platform
        // has no such switch, CanReplaceSystemDesktop() is false and the
        // calls fail saying so.
        //
        // Beside (Windows): the program starts with the session, given
        // `arguments`, and Explorer's taskbar hides itself; Explorer keeps
        // running, so its tray, its notifications and the programs that need
        // it go on working. It takes effect at once.
        //
        // Instead (Windows): the account's shell setting (Winlogon's Shell
        // under HKEY_CURRENT_USER) names UltraCanvasShellGuard, which must
        // sit beside `desktopProgram`; Explorer does not start at sign-in.
        // The guard starts the program with `arguments`, restarts it when it
        // crashes, and starts Explorer for the session when it crashes three
        // times in a minute, cannot start at all, or exits - so a broken
        // desktop never leaves the screen empty. It also starts the programs
        // Explorer would have started (the Run keys, the Startup folders).
        // It takes effect at the next sign-in.
        //
        // Restoring removes either, and gives the taskbar back the state it
        // had before the first switch - that state, not a default.
        static bool CanReplaceSystemDesktop(DesktopReplacement how = DesktopReplacement::Beside);
        static DesktopReplacement GetSystemDesktopReplacement();
        // Any replacement, Beside or Instead (not Off).
        static bool IsSystemDesktopReplaced();
        // `desktopProgram` is an absolute path to the executable.
        static bool ReplaceSystemDesktop(const std::string& desktopProgram,
                                         const std::vector<std::string>& arguments = {},
                                         std::string* error = nullptr,
                                         DesktopReplacement how = DesktopReplacement::Beside);
        // Safe to call when nothing was replaced: true, nothing changes.
        static bool RestoreSystemDesktop(std::string* error = nullptr);
        // Whether the system's own desktop runs in this session right now
        // (Windows: Explorer's taskbar). False under a desktop that replaced
        // it Instead - the session a tray host is for.
        static bool IsSystemDesktopRunning();

        // ===== THE END OF THE SESSION =====
        // What a desktop's "Sign out", "Restart" and "Shut down" do: the
        // system asks every program to close, and a program may still
        // refuse. False with the reason in `error`; CanEndSession() is false
        // where this backend has no such call.
        static bool CanEndSession();
        static bool EndSession(SessionEnd how, std::string* error = nullptr);

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

    // ===== A SHORTCUT FOR THE WHOLE DESKTOP =====
    // A key combination that reaches this program whichever window has the
    // focus - the desktop's Super+V for its clipboard panel. On X11 a passive
    // grab on the root window, on a connection and a thread of its own; on
    // Windows a RegisterHotKey on a thread of its own (Explorer keeps most
    // Win-key combinations for itself while it runs). The
    // callback runs on that thread: hand it to the UI thread
    // (UltraCanvasApplicationBase::PostToUIThread). Elsewhere Start fails and
    // says why.
    class UltraCanvasGlobalShortcut {
    public:
        using PressedCallback = std::function<void()>;

        UltraCanvasGlobalShortcut();
        ~UltraCanvasGlobalShortcut();

        UltraCanvasGlobalShortcut(const UltraCanvasGlobalShortcut&) = delete;
        UltraCanvasGlobalShortcut& operator=(const UltraCanvasGlobalShortcut&) = delete;

        // "Super+V", "Ctrl+Alt+H": Super, Ctrl, Alt and Shift, then one key.
        // False when the combination cannot be read, the platform has no
        // global shortcuts, or another program holds it; `error` says which.
        bool Start(const std::string& accelerator, PressedCallback onPressed, std::string* error = nullptr);
        // Joins the thread, so no callback runs after this returns. Safe twice.
        void Stop();
        bool IsRunning() const { return running.load(); }

    private:
        std::atomic<bool> running{false};
        std::thread worker;
        PressedCallback callback;
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

    // ===== THE NOTIFICATION AREA =====
    // The system tray, for a desktop that is the session's shell: on Windows
    // the window every program's Shell_NotifyIcon talks to (Shell_TrayWnd),
    // on a thread of its own, so a busy desktop never stalls a program
    // adding its icon. Starting it asks the programs already running to add
    // their icons again (TaskbarCreated).
    //
    //   UltraCanvasTrayHost tray;
    //   if (tray.Start([this]() { trayDirty.store(true); })) {   // background thread!
    //       for (const DesktopTrayIcon& icon : tray.ListIcons()) { ... }
    //       tray.Click(icon.id, TrayClick::Right);   // the program opens its menu
    //   }
    //
    // Only one notification area works at a time, so Start refuses while
    // another runs - Explorer's taskbar, when the desktop runs beside it -
    // and wherever the platform has none to host (X11 here; its trays are a
    // protocol of their own). The callback runs on the host's thread and must
    // only hand the news over.
    class UltraCanvasTrayHost {
    public:
        using ChangedCallback = std::function<void()>;

        UltraCanvasTrayHost();
        ~UltraCanvasTrayHost();

        UltraCanvasTrayHost(const UltraCanvasTrayHost&) = delete;
        UltraCanvasTrayHost& operator=(const UltraCanvasTrayHost&) = delete;

        // False with the reason in `error`: another notification area runs,
        // or this platform has none to host.
        bool Start(ChangedCallback onChanged, std::string* error = nullptr);
        // Joins the thread, so no callback runs after this returns. Safe twice.
        void Stop();
        bool IsRunning() const { return running.load(); }

        // The icons the programs show, in the order they were added; hidden
        // ones (a program may keep an icon and hide it) left out. Any thread.
        std::vector<DesktopTrayIcon> ListIcons() const;
        // A click on an icon, at the mouse pointer, handed to its program as
        // the notification area would: the left button usually opens the
        // program, the right one its menu; two left clicks within the
        // system's double-click time are a double click. False when the icon
        // is gone. Any thread.
        bool Click(const std::string& id, TrayClick button);

    private:
        std::atomic<bool> running{false};
        std::thread worker;
        ChangedCallback callback;
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

} // namespace UltraCanvas
