// OS/Linux/UltraCanvasLinuxDesktopShell.cpp
// The X11 backend of UltraCanvasDesktopShell: the window list and the virtual
// desktops through the EWMH properties every window manager on ULTRA OS and
// the Linux desktops maintains (_NET_CLIENT_LIST_STACKING, _NET_ACTIVE_WINDOW,
// _NET_WM_DESKTOP, _NET_CURRENT_DESKTOP, ...), window actions as the client
// messages the specification prescribes (so the window manager, not this
// module, decides how a window is raised or closed), the screenshot through
// XGetImage on the root window written by cairo's PNG writer, and the device
// activity from procfs and sysfs:
//
//   - /proc/asound/card*/pcm*{c,p}/sub*/status says "state: RUNNING" while a
//     capture (c) or playback (p) stream is open and running - the microphone
//     and speaker markers, without a sound-server dependency.
//   - /proc/<pid>/fd/* linking to /dev/video* is a process holding a camera;
//     only the user's own processes are readable, which is exactly the set
//     that can use the user's camera.
//   - /sys/class/power_supply/*/{type,capacity,status} is the battery.
//   - The keyboard layout comes from XKB: the layouts the server was
//     configured with (_XKB_RULES_NAMES) indexed by the group in effect.
//
// The queries open a connection of their own rather than borrowing the
// application's: they run from any thread the caller likes, and a shell that
// has no UltraCanvas window yet (a headless --list) still gets an answer.
// The monitor keeps a second connection with PropertyChangeMask on the root
// window and sleeps in poll() on it and on a wake pipe, so Stop() returns
// promptly and nothing spins.
// Version: 1.1.0
// Last Modified: 2026-09-30
// Author: UltraCanvas Framework

#include "UltraCanvasDesktopShellBackend.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <new>
#include <sstream>

#include <dirent.h>
#include <atomic>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <cairo/cairo.h>

namespace fs = std::filesystem;

namespace UltraCanvas {
namespace DesktopShellBackend {

namespace {

    // ===== ONE CONNECTION FOR THE QUERIES =====
    // Opened on first use and kept; Xlib is not thread-safe per connection,
    // so every query holds the mutex for its duration.
    std::mutex g_queryMutex;
    Display* g_queryDisplay = nullptr;
    bool g_queryTried = false;

    // Xlib reports a bad window (one that closed between the list and the
    // query) through the error handler, which by default exits the process.
    // The queries install a handler that only notes the error.
    int IgnoreXError(Display*, XErrorEvent*) { return 0; }

    Display* QueryDisplay() {
        if (!g_queryDisplay && !g_queryTried) {
            g_queryTried = true;
            g_queryDisplay = XOpenDisplay(nullptr);
        }
        return g_queryDisplay;
    }

    struct ScopedErrorHandler {
        XErrorHandler previous;
        ScopedErrorHandler() : previous(XSetErrorHandler(IgnoreXError)) {}
        ~ScopedErrorHandler() { XSetErrorHandler(previous); }
    };

    Atom InternAtom(Display* d, const char* name) { return XInternAtom(d, name, False); }

    // A property as raw bytes plus its type and format; empty when absent.
    struct PropertyData {
        Atom type = None;
        int format = 0;
        std::vector<unsigned char> bytes;
        unsigned long items = 0;
    };

    bool GetProperty(Display* d, Window w, Atom property, Atom requestedType, PropertyData& out) {
        Atom actualType = None;
        int actualFormat = 0;
        unsigned long items = 0, bytesAfter = 0;
        unsigned char* data = nullptr;
        const int status = XGetWindowProperty(d, w, property, 0, 1L << 20, False, requestedType,
                                              &actualType, &actualFormat, &items, &bytesAfter, &data);
        if (status != Success || !data) {
            if (data) XFree(data);
            return false;
        }
        if (actualType == None || items == 0) {
            XFree(data);
            return false;
        }
        out.type = actualType;
        out.format = actualFormat;
        out.items = items;
        // Format 32 items are longs in memory whatever the platform's long is.
        const size_t itemBytes = actualFormat == 32 ? sizeof(long) : static_cast<size_t>(actualFormat / 8);
        out.bytes.assign(data, data + items * itemBytes);
        XFree(data);
        return true;
    }

    std::vector<long> GetLongs(Display* d, Window w, Atom property, Atom type) {
        PropertyData data;
        std::vector<long> values;
        if (!GetProperty(d, w, property, type, data) || data.format != 32) return values;
        values.resize(data.items);
        std::memcpy(values.data(), data.bytes.data(), data.items * sizeof(long));
        return values;
    }

    long GetLong(Display* d, Window w, Atom property, Atom type, long fallback) {
        const std::vector<long> values = GetLongs(d, w, property, type);
        return values.empty() ? fallback : values[0];
    }

    std::string GetUtf8String(Display* d, Window w, Atom property, Atom utf8) {
        PropertyData data;
        if (GetProperty(d, w, property, utf8, data) && data.format == 8) {
            return std::string(data.bytes.begin(), data.bytes.end());
        }
        return "";
    }

    std::string GetLatin1String(Display* d, Window w, Atom property) {
        PropertyData data;
        if (GetProperty(d, w, property, XA_STRING, data) && data.format == 8) {
            std::string text(data.bytes.begin(), data.bytes.end());
            // WM_NAME is Latin-1 by definition; the bytes above 127 need to
            // become UTF-8 to be a std::string the framework can draw.
            std::string utf8;
            for (unsigned char c : text) {
                if (c < 0x80) utf8 += static_cast<char>(c);
                else { utf8 += static_cast<char>(0xC0 | (c >> 6)); utf8 += static_cast<char>(0x80 | (c & 0x3F)); }
            }
            return utf8;
        }
        return "";
    }

    Window RootOf(Display* d) { return DefaultRootWindow(d); }

    // The EWMH client message to the root window; the manager acts on it.
    bool SendRootMessage(Display* d, Window target, const char* messageType,
                         long l0 = 0, long l1 = 0, long l2 = 0, long l3 = 0, long l4 = 0) {
        XEvent event;
        std::memset(&event, 0, sizeof(event));
        event.xclient.type = ClientMessage;
        event.xclient.window = target;
        event.xclient.message_type = InternAtom(d, messageType);
        event.xclient.format = 32;
        event.xclient.data.l[0] = l0;
        event.xclient.data.l[1] = l1;
        event.xclient.data.l[2] = l2;
        event.xclient.data.l[3] = l3;
        event.xclient.data.l[4] = l4;
        const Status ok = XSendEvent(d, RootOf(d), False,
                                     SubstructureRedirectMask | SubstructureNotifyMask, &event);
        XFlush(d);
        return ok != 0;
    }

    // ===== PROCFS / SYSFS READS =====

    std::string ReadTextFile(const std::string& path) {
        std::ifstream in(PathFromUtf8(path));
        if (!in) return "";
        std::stringstream buffer;
        buffer << in.rdbuf();
        std::string text = buffer.str();
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) text.pop_back();
        return text;
    }

    std::vector<std::string> ListDirectory(const std::string& path) {
        std::vector<std::string> names;
        DIR* dir = ::opendir(path.c_str());
        if (!dir) return names;
        while (dirent* entry = ::readdir(dir)) {
            const std::string name = entry->d_name;
            if (name == "." || name == "..") continue;
            names.push_back(name);
        }
        ::closedir(dir);
        std::sort(names.begin(), names.end());
        return names;
    }

    bool StreamRunning(const std::string& pcmDir) {
        // pcm0c/sub0/status, pcm0c/sub1/status, ...
        for (const std::string& sub : ListDirectory(pcmDir)) {
            if (sub.rfind("sub", 0) != 0) continue;
            const std::string status = ReadTextFile(pcmDir + "/" + sub + "/status");
            if (status.find("state: RUNNING") != std::string::npos) return true;
        }
        return false;
    }

    void ProbeAudio(DesktopDeviceActivity& activity) {
        const std::string root = "/proc/asound";
        bool anyCard = false;
        for (const std::string& card : ListDirectory(root)) {
            if (card.rfind("card", 0) != 0) continue;
            anyCard = true;
            for (const std::string& pcm : ListDirectory(root + "/" + card)) {
                if (pcm.rfind("pcm", 0) != 0 || pcm.size() < 5) continue;
                const char kind = pcm.back();   // 'c' capture, 'p' playback
                if (kind != 'c' && kind != 'p') continue;
                if (!StreamRunning(root + "/" + card + "/" + pcm)) continue;
                if (kind == 'c') activity.microphoneInUse = true;
                else activity.speakerPlaying = true;
            }
        }
        if (!anyCard) {
            activity.warnings.push_back("No sound card is listed under /proc/asound, so the "
                                        "microphone and speaker state is unknown.");
        }
    }

    void ProbeCamera(DesktopDeviceActivity& activity) {
        // Any process of ours with a /dev/video* descriptor. Other users'
        // processes are not readable and cannot open our camera either.
        bool anyDevice = false;
        for (const std::string& dev : ListDirectory("/dev")) {
            if (dev.rfind("video", 0) == 0) { anyDevice = true; break; }
        }
        if (!anyDevice) return;
        const uid_t me = ::getuid();
        for (const std::string& pid : ListDirectory("/proc")) {
            if (pid.empty() || !std::isdigit(static_cast<unsigned char>(pid[0]))) continue;
            const std::string fdDir = "/proc/" + pid + "/fd";
            struct stat st;
            if (::stat(fdDir.c_str(), &st) != 0 || st.st_uid != me) continue;
            DIR* dir = ::opendir(fdDir.c_str());
            if (!dir) continue;
            bool found = false;
            while (dirent* entry = ::readdir(dir)) {
                if (entry->d_name[0] == '.') continue;
                char target[256];
                const std::string link = fdDir + "/" + entry->d_name;
                const ssize_t n = ::readlink(link.c_str(), target, sizeof(target) - 1);
                if (n <= 0) continue;
                target[n] = '\0';
                if (std::strncmp(target, "/dev/video", 10) == 0) { found = true; break; }
            }
            ::closedir(dir);
            if (found) { activity.webcamInUse = true; return; }
        }
    }

    void ProbeBattery(DesktopDeviceActivity& activity) {
        const std::string root = "/sys/class/power_supply";
        for (const std::string& supply : ListDirectory(root)) {
            const std::string dir = root + "/" + supply;
            if (ReadTextFile(dir + "/type") != "Battery") continue;
            const std::string present = ReadTextFile(dir + "/present");
            if (present == "0") continue;
            activity.batteryPresent = true;
            const std::string capacity = ReadTextFile(dir + "/capacity");
            if (!capacity.empty()) {
                try { activity.batteryPercent = std::stoi(capacity); } catch (...) {}
            }
            const std::string status = ReadTextFile(dir + "/status");
            activity.batteryCharging = (status == "Charging" || status == "Full");
            return;   // the first battery is the one a panel shows
        }
    }

    // The layouts the server was configured with ("us,de") and the group in
    // effect. Held under the query mutex because it uses the query display.
    void ProbeKeyboardLayout(DesktopDeviceActivity& activity) {
        Display* d = QueryDisplay();
        if (!d) return;
        ScopedErrorHandler guard;
        PropertyData names;
        const Atom rulesAtom = InternAtom(d, "_XKB_RULES_NAMES");
        if (!GetProperty(d, RootOf(d), rulesAtom, XA_STRING, names) || names.format != 8) return;
        // Five NUL-separated strings: rules, model, layout, variant, options.
        std::vector<std::string> parts;
        std::string current;
        for (unsigned char c : names.bytes) {
            if (c == '\0') { parts.push_back(current); current.clear(); }
            else current += static_cast<char>(c);
        }
        if (!current.empty()) parts.push_back(current);
        if (parts.size() < 3 || parts[2].empty()) return;
        std::vector<std::string> layouts;
        std::stringstream list(parts[2]);
        std::string item;
        while (std::getline(list, item, ',')) layouts.push_back(item);
        if (layouts.empty()) return;

        XkbStateRec state;
        int group = 0;
        if (XkbGetState(d, XkbUseCoreKbd, &state) == Success) group = state.group;
        if (group < 0 || group >= static_cast<int>(layouts.size())) group = 0;
        activity.keyboardLayout = layouts[static_cast<size_t>(group)];
    }

    // ===== ONE WINDOW =====

    struct Atoms {
        Atom utf8, netWmName, wmClass, netWmDesktop, netWmPid, netWmState, stateHidden,
             stateSkipTaskbar, netWmWindowType, typeNormal, typeDialog, typeUtility,
             clientListStacking, clientList, activeWindow, currentDesktop, numberOfDesktops;
        explicit Atoms(Display* d)
            : utf8(InternAtom(d, "UTF8_STRING")),
              netWmName(InternAtom(d, "_NET_WM_NAME")),
              wmClass(XA_WM_CLASS),
              netWmDesktop(InternAtom(d, "_NET_WM_DESKTOP")),
              netWmPid(InternAtom(d, "_NET_WM_PID")),
              netWmState(InternAtom(d, "_NET_WM_STATE")),
              stateHidden(InternAtom(d, "_NET_WM_STATE_HIDDEN")),
              stateSkipTaskbar(InternAtom(d, "_NET_WM_STATE_SKIP_TASKBAR")),
              netWmWindowType(InternAtom(d, "_NET_WM_WINDOW_TYPE")),
              typeNormal(InternAtom(d, "_NET_WM_WINDOW_TYPE_NORMAL")),
              typeDialog(InternAtom(d, "_NET_WM_WINDOW_TYPE_DIALOG")),
              typeUtility(InternAtom(d, "_NET_WM_WINDOW_TYPE_UTILITY")),
              clientListStacking(InternAtom(d, "_NET_CLIENT_LIST_STACKING")),
              clientList(InternAtom(d, "_NET_CLIENT_LIST")),
              activeWindow(InternAtom(d, "_NET_ACTIVE_WINDOW")),
              currentDesktop(InternAtom(d, "_NET_CURRENT_DESKTOP")),
              numberOfDesktops(InternAtom(d, "_NET_NUMBER_OF_DESKTOPS")) {}
    };

    DesktopWindowInfo DescribeWindow(Display* d, const Atoms& a, Window w, Window active) {
        DesktopWindowInfo info;
        info.id = static_cast<uint64_t>(w);
        info.title = GetUtf8String(d, w, a.netWmName, a.utf8);
        if (info.title.empty()) info.title = GetLatin1String(d, w, XA_WM_NAME);

        PropertyData cls;
        if (GetProperty(d, w, a.wmClass, XA_STRING, cls) && cls.format == 8) {
            // Two NUL-terminated strings: instance, then class.
            const std::string text(cls.bytes.begin(), cls.bytes.end());
            const size_t nul = text.find('\0');
            info.appName = text.substr(0, nul);
            if (nul != std::string::npos) {
                const std::string rest = text.substr(nul + 1);
                info.appClass = rest.substr(0, rest.find('\0'));
            }
        }

        const long desktop = GetLong(d, w, a.netWmDesktop, XA_CARDINAL, -2);
        if (desktop == -2) info.virtualDesktop = -1;
        else info.virtualDesktop = (desktop == 0xFFFFFFFFL || desktop == -1) ? -1 : static_cast<int>(desktop);
        info.processId = static_cast<int>(GetLong(d, w, a.netWmPid, XA_CARDINAL, 0));
        info.active = (w == active);

        for (long state : GetLongs(d, w, a.netWmState, XA_ATOM)) {
            if (static_cast<Atom>(state) == a.stateHidden) info.minimized = true;
            if (static_cast<Atom>(state) == a.stateSkipTaskbar) info.skipTaskbar = true;
        }
        // Docks, panels, the desktop, menus, tooltips: not taskbar material,
        // whether or not they set the state.
        const std::vector<long> types = GetLongs(d, w, a.netWmWindowType, XA_ATOM);
        if (!types.empty()) {
            const Atom type = static_cast<Atom>(types[0]);
            if (type != a.typeNormal && type != a.typeDialog && type != a.typeUtility) {
                info.skipTaskbar = true;
            }
        }
        return info;
    }

    // ===== THE MONITOR'S CONNECTION =====

    struct LinuxMonitorState {
        Display* display = nullptr;
        int wakePipe[2] = {-1, -1};
        Atom interesting[5] = {None, None, None, None, None};
    };

} // namespace

// ===== BACKEND =====

std::string BackendName() { return "x11"; }

bool IsAvailable() {
    std::lock_guard<std::mutex> lock(g_queryMutex);
    return QueryDisplay() != nullptr;
}

std::vector<DesktopWindowInfo> ListWindows() {
    std::vector<DesktopWindowInfo> windows;
    std::lock_guard<std::mutex> lock(g_queryMutex);
    Display* d = QueryDisplay();
    if (!d) return windows;
    ScopedErrorHandler guard;
    const Atoms a(d);
    std::vector<long> ids = GetLongs(d, RootOf(d), a.clientListStacking, XA_WINDOW);
    if (ids.empty()) ids = GetLongs(d, RootOf(d), a.clientList, XA_WINDOW);
    const Window active = static_cast<Window>(GetLong(d, RootOf(d), a.activeWindow, XA_WINDOW, 0));
    windows.reserve(ids.size());
    for (long id : ids) {
        if (id == 0) continue;
        windows.push_back(DescribeWindow(d, a, static_cast<Window>(id), active));
    }
    return windows;
}

uint64_t GetActiveWindow() {
    std::lock_guard<std::mutex> lock(g_queryMutex);
    Display* d = QueryDisplay();
    if (!d) return 0;
    ScopedErrorHandler guard;
    return static_cast<uint64_t>(GetLong(d, RootOf(d), InternAtom(d, "_NET_ACTIVE_WINDOW"), XA_WINDOW, 0));
}

bool ActivateWindow(uint64_t id) {
    std::lock_guard<std::mutex> lock(g_queryMutex);
    Display* d = QueryDisplay();
    if (!d || id == 0) return false;
    ScopedErrorHandler guard;
    const Window w = static_cast<Window>(id);
    // Follow the window to its desktop first, or the activation raises a
    // window the user cannot see.
    const long desktop = GetLong(d, w, InternAtom(d, "_NET_WM_DESKTOP"), XA_CARDINAL, -1);
    if (desktop >= 0 && desktop != 0xFFFFFFFFL) {
        const long current = GetLong(d, RootOf(d), InternAtom(d, "_NET_CURRENT_DESKTOP"), XA_CARDINAL, -1);
        if (current != desktop) SendRootMessage(d, RootOf(d), "_NET_CURRENT_DESKTOP", desktop, CurrentTime);
    }
    // Source indication 2 = a pager or taskbar, which the manager honours
    // without focus-stealing prevention.
    XMapRaised(d, w);
    return SendRootMessage(d, w, "_NET_ACTIVE_WINDOW", 2, CurrentTime, 0);
}

bool MinimizeWindow(uint64_t id) {
    std::lock_guard<std::mutex> lock(g_queryMutex);
    Display* d = QueryDisplay();
    if (!d || id == 0) return false;
    ScopedErrorHandler guard;
    const Status ok = XIconifyWindow(d, static_cast<Window>(id), DefaultScreen(d));
    XFlush(d);
    return ok != 0;
}

bool CloseWindow(uint64_t id) {
    std::lock_guard<std::mutex> lock(g_queryMutex);
    Display* d = QueryDisplay();
    if (!d || id == 0) return false;
    ScopedErrorHandler guard;
    return SendRootMessage(d, static_cast<Window>(id), "_NET_CLOSE_WINDOW", CurrentTime, 2);
}

int GetVirtualDesktopCount() {
    std::lock_guard<std::mutex> lock(g_queryMutex);
    Display* d = QueryDisplay();
    if (!d) return 0;
    ScopedErrorHandler guard;
    return static_cast<int>(GetLong(d, RootOf(d), InternAtom(d, "_NET_NUMBER_OF_DESKTOPS"), XA_CARDINAL, 0));
}

int GetCurrentVirtualDesktop() {
    std::lock_guard<std::mutex> lock(g_queryMutex);
    Display* d = QueryDisplay();
    if (!d) return -1;
    ScopedErrorHandler guard;
    return static_cast<int>(GetLong(d, RootOf(d), InternAtom(d, "_NET_CURRENT_DESKTOP"), XA_CARDINAL, -1));
}

bool SetCurrentVirtualDesktop(int index) {
    std::lock_guard<std::mutex> lock(g_queryMutex);
    Display* d = QueryDisplay();
    if (!d) return false;
    ScopedErrorHandler guard;
    return SendRootMessage(d, RootOf(d), "_NET_CURRENT_DESKTOP", index, CurrentTime);
}

bool SetVirtualDesktopCount(int count) {
    std::lock_guard<std::mutex> lock(g_queryMutex);
    Display* d = QueryDisplay();
    if (!d) return false;
    ScopedErrorHandler guard;
    return SendRootMessage(d, RootOf(d), "_NET_NUMBER_OF_DESKTOPS", count);
}

bool MoveWindowToVirtualDesktop(uint64_t id, int index) {
    std::lock_guard<std::mutex> lock(g_queryMutex);
    Display* d = QueryDisplay();
    if (!d || id == 0) return false;
    ScopedErrorHandler guard;
    const long desktop = index < 0 ? 0xFFFFFFFFL : index;
    return SendRootMessage(d, static_cast<Window>(id), "_NET_WM_DESKTOP", desktop, 2);
}

bool GetScreenSize(int& width, int& height) {
    std::lock_guard<std::mutex> lock(g_queryMutex);
    Display* d = QueryDisplay();
    if (!d) return false;
    const int screen = DefaultScreen(d);
    width = DisplayWidth(d, screen);
    height = DisplayHeight(d, screen);
    return width > 0 && height > 0;
}

bool ReserveScreenEdges(uint64_t id, int left, int right, int top, int bottom) {
    std::lock_guard<std::mutex> lock(g_queryMutex);
    Display* d = QueryDisplay();
    if (!d || id == 0) return false;
    ScopedErrorHandler guard;
    const int screen = DefaultScreen(d);
    const long width = DisplayWidth(d, screen);
    const long height = DisplayHeight(d, screen);
    // _NET_WM_STRUT_PARTIAL: left, right, top, bottom, then the start and end
    // coordinate of each strip along its edge. A strip that reserves nothing
    // has 0..0. _NET_WM_STRUT (the four widths alone) is set as well, for a
    // manager that reads only the older property.
    long partial[12] = {
        left, right, top, bottom,
        left > 0 ? 0 : 0,  left > 0 ? height - 1 : 0,
        right > 0 ? 0 : 0, right > 0 ? height - 1 : 0,
        top > 0 ? 0 : 0,   top > 0 ? width - 1 : 0,
        bottom > 0 ? 0 : 0, bottom > 0 ? width - 1 : 0,
    };
    const Window w = static_cast<Window>(id);
    XChangeProperty(d, w, InternAtom(d, "_NET_WM_STRUT_PARTIAL"), XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(partial), 12);
    XChangeProperty(d, w, InternAtom(d, "_NET_WM_STRUT"), XA_CARDINAL, 32, PropModeReplace,
                    reinterpret_cast<unsigned char*>(partial), 4);
    XFlush(d);
    return true;
}

// XGetImage on the root window into a BGRx buffer: the layout cairo's RGB24
// surfaces use, and the one the QR scanner reads as BGRA32, so the same rows
// serve the PNG writer and an in-memory consumer alike. Caller holds
// g_queryMutex.
bool CaptureScreenLocked(DesktopScreenImage& out, std::string& error) {
    out = DesktopScreenImage();
    Display* d = QueryDisplay();
    if (!d) {
        error = "No X display: the screen cannot be captured.";
        return false;
    }
    ScopedErrorHandler guard;
    const int screen = DefaultScreen(d);
    const int width = DisplayWidth(d, screen);
    const int height = DisplayHeight(d, screen);
    if (width <= 0 || height <= 0) {
        error = "The X server reports an empty screen.";
        return false;
    }
    XImage* image = XGetImage(d, RootOf(d), 0, 0, static_cast<unsigned>(width),
                              static_cast<unsigned>(height), AllPlanes, ZPixmap);
    if (!image) {
        error = "The X server refused to hand over the screen contents.";
        return false;
    }

    const int stride = width * 4;
    try {
        out.pixels.resize(static_cast<size_t>(height) * static_cast<size_t>(stride));
    } catch (const std::bad_alloc&) {
        XDestroyImage(image);
        out = DesktopScreenImage();
        error = "Not enough memory for a " + std::to_string(width) + "x" + std::to_string(height) + " image.";
        return false;
    }
    unsigned char* dst = out.pixels.data();
    const bool nativeLayout = image->bits_per_pixel == 32 && image->red_mask == 0xFF0000 &&
                              image->green_mask == 0x00FF00 && image->blue_mask == 0x0000FF &&
                              image->byte_order == LSBFirst;
    for (int y = 0; y < height; ++y) {
        uint32_t* row = reinterpret_cast<uint32_t*>(dst + static_cast<size_t>(y) * stride);
        if (nativeLayout) {
            std::memcpy(row, image->data + static_cast<size_t>(y) * image->bytes_per_line,
                        static_cast<size_t>(width) * 4);
        } else {
            for (int x = 0; x < width; ++x) {
                const unsigned long pixel = XGetPixel(image, x, y);
                const auto channel = [pixel](unsigned long mask) -> uint32_t {
                    if (mask == 0) return 0;
                    int shift = 0;
                    unsigned long m = mask;
                    while (!(m & 1)) { m >>= 1; ++shift; }
                    uint32_t value = static_cast<uint32_t>((pixel & mask) >> shift);
                    // Scale a narrow channel (5/6 bits) up to 8.
                    int bits = 0;
                    while (m) { m >>= 1; ++bits; }
                    if (bits < 8 && bits > 0) value = (value * 255) / ((1u << bits) - 1);
                    return value & 0xFF;
                };
                row[x] = (channel(image->red_mask) << 16) | (channel(image->green_mask) << 8) |
                         channel(image->blue_mask);
            }
        }
    }
    XDestroyImage(image);
    out.width = width;
    out.height = height;
    out.stride = stride;
    return true;
}

bool CaptureScreenImage(DesktopScreenImage& out, std::string& error) {
    std::lock_guard<std::mutex> lock(g_queryMutex);
    return CaptureScreenLocked(out, error);
}

bool CaptureScreen(const std::string& pngPath, std::string& error) {
    std::lock_guard<std::mutex> lock(g_queryMutex);
    DesktopScreenImage shot;
    if (!CaptureScreenLocked(shot, error)) return false;

    // The buffer is already in cairo's RGB24 layout, so the surface can sit
    // on it directly; nothing is copied.
    cairo_surface_t* surface = cairo_image_surface_create_for_data(
        shot.pixels.data(), CAIRO_FORMAT_RGB24, shot.width, shot.height, shot.stride);
    if (cairo_surface_status(surface) != CAIRO_STATUS_SUCCESS) {
        error = std::string("Could not wrap the screen image: ") +
                cairo_status_to_string(cairo_surface_status(surface));
        cairo_surface_destroy(surface);
        return false;
    }
    const cairo_status_t status = cairo_surface_write_to_png(surface, pngPath.c_str());
    cairo_surface_destroy(surface);
    if (status != CAIRO_STATUS_SUCCESS) {
        error = std::string("Could not write \"") + pngPath + "\": " + cairo_status_to_string(status);
        return false;
    }
    return true;
}

void ReadDeviceActivity(DesktopDeviceActivity& activity) {
    ProbeAudio(activity);
    ProbeCamera(activity);
    ProbeBattery(activity);
    std::lock_guard<std::mutex> lock(g_queryMutex);
    ProbeKeyboardLayout(activity);
}

// ===== THE MONITOR =====

struct MonitorState : LinuxMonitorState {};

MonitorState* MonitorOpen() {
    Display* d = XOpenDisplay(nullptr);
    if (!d) return nullptr;
    auto* state = new MonitorState();
    state->display = d;
    if (::pipe(state->wakePipe) != 0) {
        XCloseDisplay(d);
        delete state;
        return nullptr;
    }
    state->interesting[0] = InternAtom(d, "_NET_CLIENT_LIST_STACKING");
    state->interesting[1] = InternAtom(d, "_NET_CLIENT_LIST");
    state->interesting[2] = InternAtom(d, "_NET_ACTIVE_WINDOW");
    state->interesting[3] = InternAtom(d, "_NET_CURRENT_DESKTOP");
    state->interesting[4] = InternAtom(d, "_NET_NUMBER_OF_DESKTOPS");
    XSelectInput(d, RootOf(d), PropertyChangeMask);
    XFlush(d);
    return state;
}

bool MonitorWait(MonitorState* state) {
    if (!state || !state->display) return false;
    Display* d = state->display;
    for (;;) {
        // Drain what is already queued before sleeping on the socket.
        bool changed = false;
        while (XPending(d) > 0) {
            XEvent event;
            XNextEvent(d, &event);
            if (event.type != PropertyNotify) continue;
            for (Atom atom : state->interesting) {
                if (event.xproperty.atom == atom) { changed = true; break; }
            }
        }
        if (changed) return true;

        pollfd fds[2];
        fds[0].fd = ConnectionNumber(d);
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = state->wakePipe[0];
        fds[1].events = POLLIN;
        fds[1].revents = 0;
        const int n = ::poll(fds, 2, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (fds[1].revents & POLLIN) {
            char byte;
            while (::read(state->wakePipe[0], &byte, 1) > 0) {}
            return false;
        }
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) return false;
        // POLLIN on the connection: loop to drain the events that arrived.
    }
}

void MonitorWake(MonitorState* state) {
    if (!state || state->wakePipe[1] < 0) return;
    const char byte = 1;
    if (::write(state->wakePipe[1], &byte, 1) < 0) {
        // Nothing to do: the reader is gone, or the pipe is full of wakes.
    }
}

void MonitorClose(MonitorState* state) {
    if (!state) return;
    if (state->display) XCloseDisplay(state->display);
    if (state->wakePipe[0] >= 0) ::close(state->wakePipe[0]);
    if (state->wakePipe[1] >= 0) ::close(state->wakePipe[1]);
    delete state;
}

// ===== A SHORTCUT FOR THE WHOLE DESKTOP =====

namespace {
    // A grab another client already holds fails with BadAccess, reported
    // through the error handler after the request reached the server.
    std::atomic<int> g_grabError{0};
    int NoteGrabError(Display*, XErrorEvent* error) {
        g_grabError.store(error ? error->error_code : 1);
        return 0;
    }
} // namespace

struct ShortcutState {
    Display* display = nullptr;
    int wakePipe[2] = {-1, -1};
    KeyCode keycode = 0;
    unsigned int modifiers = 0;
    bool down = false;   // the combination was pressed and its key is not let go yet
};

ShortcutState* ShortcutOpen(const std::string& accelerator, std::string& error) {
    // "Super+V" -> Mod4Mask and the keysym "v".
    unsigned int modifiers = 0;
    std::string key;
    size_t start = 0;
    while (start <= accelerator.size()) {
        size_t plus = accelerator.find('+', start);
        if (plus == start && plus + 1 == accelerator.size()) plus = std::string::npos;   // "Ctrl++"
        std::string part = accelerator.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
        std::string lower = part;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (lower == "super" || lower == "win" || lower == "meta") modifiers |= Mod4Mask;
        else if (lower == "ctrl" || lower == "control") modifiers |= ControlMask;
        else if (lower == "alt") modifiers |= Mod1Mask;
        else if (lower == "shift") modifiers |= ShiftMask;
        else key = part;
        if (plus == std::string::npos) break;
        start = plus + 1;
    }
    if (key.empty()) {
        error = "\"" + accelerator + "\" names no key.";
        return nullptr;
    }
    KeySym keysym = XStringToKeysym(key.c_str());
    if (keysym == NoSymbol && key.size() == 1) {
        const std::string lower(1, static_cast<char>(std::tolower(static_cast<unsigned char>(key[0]))));
        keysym = XStringToKeysym(lower.c_str());
    }
    if (keysym == NoSymbol) {
        error = "\"" + key + "\" is not a key name.";
        return nullptr;
    }

    Display* d = XOpenDisplay(nullptr);
    if (!d) {
        error = "No X display for " + accelerator + ".";
        return nullptr;
    }
    const KeyCode keycode = XKeysymToKeycode(d, keysym);
    if (keycode == 0) {
        XCloseDisplay(d);
        error = "This keyboard has no \"" + key + "\" key.";
        return nullptr;
    }

    // The same combination with Caps Lock and Num Lock on, or it works only
    // while both are off.
    const unsigned int locks[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
    g_grabError.store(0);
    XErrorHandler previous = XSetErrorHandler(NoteGrabError);
    for (unsigned int lock : locks) {
        XGrabKey(d, keycode, modifiers | lock, DefaultRootWindow(d), True, GrabModeAsync, GrabModeAsync);
    }
    XSync(d, False);
    XSetErrorHandler(previous);
    if (g_grabError.load() != 0) {
        XCloseDisplay(d);
        error = accelerator + " is taken by another program.";
        return nullptr;
    }
    XSelectInput(d, DefaultRootWindow(d), KeyPressMask | KeyReleaseMask);
    // A key held down repeats as presses alone, not press-release pairs, so a
    // held combination is one press and one release.
    XkbSetDetectableAutoRepeat(d, True, nullptr);

    auto* state = new ShortcutState();
    state->display = d;
    state->keycode = keycode;
    state->modifiers = modifiers;
    if (::pipe(state->wakePipe) != 0) {
        XCloseDisplay(d);
        delete state;
        error = "No pipe for the shortcut's thread.";
        return nullptr;
    }
    return state;
}

// The shortcut counts when its key is let go, not when it goes down. While
// the key is down the passive grab holds the keyboard, and a window the
// shortcut opens and focuses in that time is handed focus events by the grab
// and its end (NotifyWhileGrabbed, NotifyUngrab) that the window manager may
// follow by giving the focus back: the clipboard panel opened on Super+V shut
// again at once, one press in two.
bool ShortcutWait(ShortcutState* state) {
    if (!state || !state->display) return false;
    Display* d = state->display;
    for (;;) {
        bool released = false;
        while (XPending(d) > 0) {
            XEvent event;
            XNextEvent(d, &event);
            if (event.xkey.keycode != state->keycode) continue;
            if (event.type == KeyPress) state->down = true;
            else if (event.type == KeyRelease && state->down) {
                state->down = false;
                released = true;
            }
        }
        if (released) return true;

        pollfd fds[2];
        fds[0].fd = ConnectionNumber(d);
        fds[0].events = POLLIN;
        fds[0].revents = 0;
        fds[1].fd = state->wakePipe[0];
        fds[1].events = POLLIN;
        fds[1].revents = 0;
        const int n = ::poll(fds, 2, -1);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (fds[1].revents & POLLIN) {
            char byte;
            while (::read(state->wakePipe[0], &byte, 1) > 0) {}
            return false;
        }
        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) return false;
    }
}

void ShortcutWake(ShortcutState* state) {
    if (!state || state->wakePipe[1] < 0) return;
    const char byte = 1;
    if (::write(state->wakePipe[1], &byte, 1) < 0) {
        // The reader is gone, or the pipe is full of wakes.
    }
}

void ShortcutClose(ShortcutState* state) {
    if (!state) return;
    if (state->display) {
        const unsigned int locks[] = {0, LockMask, Mod2Mask, LockMask | Mod2Mask};
        for (unsigned int lock : locks) {
            XUngrabKey(state->display, state->keycode, state->modifiers | lock, DefaultRootWindow(state->display));
        }
        XCloseDisplay(state->display);
    }
    if (state->wakePipe[0] >= 0) ::close(state->wakePipe[0]);
    if (state->wakePipe[1] >= 0) ::close(state->wakePipe[1]);
    delete state;
}

} // namespace DesktopShellBackend
} // namespace UltraCanvas
