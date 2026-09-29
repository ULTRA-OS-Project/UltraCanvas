// core/UltraCanvasDesktopShell.cpp
// The platform-neutral half of UltraCanvasDesktopShell: the application list
// from desktop entries, the launcher, the notices, the screenshot path, the
// network / USB / Bluetooth part of the device activity (read from
// UltraCanvasHardwareInfo), the monitor's thread - and the fallback backend
// for platforms that have none. The window system itself is in
// OS/<Platform>/UltraCanvas*DesktopShell.cpp.
// Version: 1.0.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework

#include "UltraCanvasDesktopShellBackend.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasHardwareInfo.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasUtils.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>

namespace fs = std::filesystem;

namespace UltraCanvas {

namespace {

    std::string EnvString(const char* name) {
        const char* value = std::getenv(name);
        return value ? std::string(value) : std::string();
    }

    std::vector<std::string> SplitPathList(const std::string& list, char separator) {
        std::vector<std::string> parts;
        std::string current;
        for (char c : list) {
            if (c == separator) {
                if (!current.empty()) parts.push_back(current);
                current.clear();
            } else {
                current += c;
            }
        }
        if (!current.empty()) parts.push_back(current);
        return parts;
    }

    std::string HomeDirectory() {
        std::string home = EnvString("HOME");
        if (home.empty()) home = EnvString("USERPROFILE");
        return home;
    }

    // The applications directories, the user's first so that a user's own
    // entry with the same file name overrides the system's.
    std::vector<fs::path> ApplicationDirectories() {
        std::vector<fs::path> dirs;
        std::string dataHome = EnvString("XDG_DATA_HOME");
        const std::string home = HomeDirectory();
        if (dataHome.empty() && !home.empty()) dataHome = home + "/.local/share";
        if (!dataHome.empty()) dirs.push_back(PathFromUtf8(dataHome) / "applications");
        std::string dataDirs = EnvString("XDG_DATA_DIRS");
        if (dataDirs.empty()) dataDirs = "/usr/local/share:/usr/share";
        for (const std::string& dir : SplitPathList(dataDirs, ':'))
            dirs.push_back(PathFromUtf8(dir) / "applications");
        return dirs;
    }

    // The desktop-file id of an entry under `root`: "org.example.App.desktop",
    // subfolders joined with '-' as the specification says.
    std::string DesktopFileId(const fs::path& root, const fs::path& file) {
        std::error_code ec;
        const fs::path relative = fs::relative(file, root, ec);
        if (ec) return PathToUtf8(file.filename());
        std::string id;
        for (const auto& part : relative) {
            if (!id.empty()) id += '-';
            id += PathToUtf8(part);
        }
        return id;
    }

    bool IsExecutableFile(const fs::path& p) {
        std::error_code ec;
        if (!fs::is_regular_file(p, ec)) return false;
        const auto perms = fs::status(p, ec).permissions();
        if (ec) return false;
        return (perms & (fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec))
               != fs::perms::none;
    }

    std::string TimestampForFileName() {
        const std::time_t now = std::time(nullptr);
        std::tm local{};
#if defined(_WIN32)
        localtime_s(&local, &now);
#else
        localtime_r(&now, &local);
#endif
        char buffer[64];
        // Dots rather than colons: a colon is not a file-name character on
        // Windows, and this name is what the user sees in a file manager.
        std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H.%M.%S", &local);
        return buffer;
    }

    std::string NoticeFileName(const std::string& application) {
        // The application's name as a file name: letters, digits, '-', '_'
        // and '.'; anything else becomes '_' so that a name can never leave
        // the directory or collide with the JSON suffix.
        std::string name;
        for (unsigned char c : application) {
            const bool keep = std::isalnum(c) || c == '-' || c == '_' || c == '.';
            name += keep ? static_cast<char>(c) : '_';
        }
        if (name.empty() || name == "." || name == "..") name = "_";
        return name + ".json";
    }

    bool ReadNoticeFile(const fs::path& file, DesktopNotice& out) {
        JSONParseResult result;
        JSONValue root = JSON::ParseFile(PathToUtf8(file), &result);
        if (!result.success || !root.IsObject()) return false;
        out.application = root.Get("application").GetString();
        out.count = static_cast<int>(root.Get("count").GetInteger(0));
        out.text = root.Get("text").GetString();
        out.updatedUnixSeconds = root.Get("updated").GetInteger(0);
        if (out.application.empty()) out.application = PathToUtf8(file.stem());
        return true;
    }

    bool IsTunnelInterface(const NetworkInterfaceInfo& adapter) {
        if (adapter.type == NetworkLinkType::Tunnel) return true;
        static const char* const prefixes[] = {"tun", "tap", "wg", "ppp", "vpn", "tailscale",
                                               "nordlynx", "proton", "ipsec", "utun", "zt"};
        for (const char* prefix : prefixes) {
            if (adapter.name.rfind(prefix, 0) == 0) return true;
        }
        return false;
    }

} // namespace

// ===== BACKEND =====

std::string UltraCanvasDesktopShell::GetBackendName() { return DesktopShellBackend::BackendName(); }
bool UltraCanvasDesktopShell::IsAvailable() { return DesktopShellBackend::IsAvailable(); }

// ===== WINDOWS AND DESKTOPS =====

std::vector<DesktopWindowInfo> UltraCanvasDesktopShell::ListWindows() {
    auto windows = DesktopShellBackend::ListWindows();
    // The icon: the application's own, as the desktop entry system knows it.
    // WM_CLASS is the name a desktop entry's StartupWMClass and Icon carry
    // for the applications this repository ships, and the usual convention
    // elsewhere; a lower-cased class catches "Firefox" -> "firefox".
    std::map<std::string, std::string> resolved;
    for (auto& w : windows) {
        if (!w.iconFile.empty() || w.appClass.empty()) continue;
        auto it = resolved.find(w.appClass);
        if (it == resolved.end()) {
            std::string icon = FindDesktopIconFile(w.appClass, 48);
            if (icon.empty()) {
                std::string lower = w.appClass;
                std::transform(lower.begin(), lower.end(), lower.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (lower != w.appClass) icon = FindDesktopIconFile(lower, 48);
            }
            if (icon.empty() && !w.appName.empty() && w.appName != w.appClass) {
                icon = FindDesktopIconFile(w.appName, 48);
            }
            it = resolved.emplace(w.appClass, icon).first;
        }
        w.iconFile = it->second;
    }
    return windows;
}

uint64_t UltraCanvasDesktopShell::GetActiveWindow() { return DesktopShellBackend::GetActiveWindow(); }
bool UltraCanvasDesktopShell::ActivateWindow(uint64_t id) { return DesktopShellBackend::ActivateWindow(id); }
bool UltraCanvasDesktopShell::MinimizeWindow(uint64_t id) { return DesktopShellBackend::MinimizeWindow(id); }
bool UltraCanvasDesktopShell::CloseWindow(uint64_t id) { return DesktopShellBackend::CloseWindow(id); }
int  UltraCanvasDesktopShell::GetVirtualDesktopCount() { return DesktopShellBackend::GetVirtualDesktopCount(); }
int  UltraCanvasDesktopShell::GetCurrentVirtualDesktop() { return DesktopShellBackend::GetCurrentVirtualDesktop(); }
bool UltraCanvasDesktopShell::SetCurrentVirtualDesktop(int index) {
    if (index < 0) return false;
    return DesktopShellBackend::SetCurrentVirtualDesktop(index);
}
bool UltraCanvasDesktopShell::SetVirtualDesktopCount(int count) {
    if (count < 1 || count > 32) return false;
    return DesktopShellBackend::SetVirtualDesktopCount(count);
}
bool UltraCanvasDesktopShell::MoveWindowToVirtualDesktop(uint64_t id, int index) {
    return DesktopShellBackend::MoveWindowToVirtualDesktop(id, index);
}

// ===== THE SCREEN =====

bool UltraCanvasDesktopShell::GetScreenSize(int& width, int& height) {
    return DesktopShellBackend::GetScreenSize(width, height);
}

bool UltraCanvasDesktopShell::CaptureScreen(const std::string& pngPath, std::string* error) {
    std::string reason;
    if (pngPath.empty()) {
        reason = "No file name for the screenshot.";
    } else {
        std::error_code ec;
        const fs::path target = PathFromUtf8(pngPath);
        if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
        if (ec) {
            reason = "Could not create \"" + PathToUtf8(target.parent_path()) + "\": " + ec.message();
        } else if (DesktopShellBackend::CaptureScreen(pngPath, reason)) {
            return true;
        }
    }
    if (error) *error = reason;
    return false;
}

std::string UltraCanvasDesktopShell::DefaultScreenshotPath() {
    std::string base;
    for (const UserFolderInfo& folder : GetWellKnownUserFolders()) {
        if (folder.kind == UserFolderKind::Pictures) { base = folder.path; break; }
    }
    if (base.empty()) base = HomeDirectory();
    if (base.empty()) base = ".";
    const fs::path file = PathFromUtf8(base) / "Screenshots" /
                          PathFromUtf8("Screenshot " + TimestampForFileName() + ".png");
    return PathToUtf8(file);
}

// ===== DEVICES =====

DesktopDeviceActivity UltraCanvasDesktopShell::ReadDeviceActivity() {
    DesktopDeviceActivity activity;

    // Network: what UltraCanvasHardwareInfo already knows how to read, so the
    // desktop and the hardware panel never disagree about an interface.
    for (const NetworkInterfaceInfo& adapter : UltraCanvasHardwareInfo::ListNetworkInterfaces()) {
        if (adapter.type == NetworkLinkType::Loopback || adapter.name == "lo") continue;
        activity.bytesReceived += adapter.bytesReceived;
        activity.bytesSent += adapter.bytesSent;
        const bool carrier = adapter.up && adapter.connected;
        if (carrier) activity.internetInterfaceUp = true;
        if (adapter.wifi || adapter.type == NetworkLinkType::WiFi) {
            activity.wifiPresent = true;
            const bool joined = adapter.wifi ? adapter.wifi->connected : carrier;
            if (joined) {
                activity.wifiConnected = true;
                if (adapter.wifi && activity.wifiSsid.empty()) activity.wifiSsid = adapter.wifi->ssid;
            }
        } else if (IsTunnelInterface(adapter)) {
            if (adapter.up) activity.vpnConnected = true;
        } else if (adapter.type == NetworkLinkType::Ethernet ||
                   adapter.type == NetworkLinkType::Unknown) {
            if (carrier) activity.lanConnected = true;
        }
    }

    for (const USBDeviceInfo& device : UltraCanvasHardwareInfo::ListUSBDevices()) {
        if (!device.isHub) ++activity.usbDeviceCount;
    }

    for (const BluetoothAdapterInfo& adapter : UltraCanvasHardwareInfo::ListBluetoothAdapters()) {
        activity.bluetoothPresent = true;
        if (adapter.powered) activity.bluetoothPowered = true;
        for (const BluetoothDeviceInfo& device : adapter.devices) {
            if (device.connected) ++activity.bluetoothConnectedDevices;
        }
    }

    DesktopShellBackend::ReadDeviceActivity(activity);
    return activity;
}

// ===== APPLICATIONS =====

std::vector<UCDesktopEntry> UltraCanvasDesktopShell::ListApplications(int iconSize) {
    std::map<std::string, UCDesktopEntry> byId;   // desktop-file id -> the entry that won
    std::error_code ec;
    for (const fs::path& root : ApplicationDirectories()) {
        if (!fs::is_directory(root, ec)) continue;
        for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            const fs::path file = it->path();
            if (!it->is_regular_file(ec)) continue;
            const std::string path = PathToUtf8(file);
            if (!IsDesktopEntryPath(path)) continue;
            const std::string id = DesktopFileId(root, file);
            if (byId.count(id)) continue;          // an earlier (user) directory won
            UCDesktopEntry entry;
            if (!ReadDesktopEntry(path, entry)) continue;
            // A hidden entry still claims its id: the user hid the system's
            // entry by shadowing it, and the system's must not come back.
            byId.emplace(id, std::move(entry));
        }
    }

    std::vector<UCDesktopEntry> apps;
    for (auto& [id, entry] : byId) {
        if (entry.kind != UCDesktopEntry::Kind::Application) continue;
        if (entry.hidden || entry.noDisplay || entry.exec.empty()) continue;
        if (!entry.tryExec.empty() && FindProgram(entry.tryExec).empty()) continue;
        if (entry.iconFile.empty() && !entry.iconName.empty()) {
            entry.iconFile = FindDesktopIconFile(entry.iconName, iconSize);
        }
        apps.push_back(std::move(entry));
    }
    std::sort(apps.begin(), apps.end(), [](const UCDesktopEntry& a, const UCDesktopEntry& b) {
        std::string la = a.name, lb = b.name;
        std::transform(la.begin(), la.end(), la.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        std::transform(lb.begin(), lb.end(), lb.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return la < lb;
    });
    return apps;
}

bool UltraCanvasDesktopShell::LaunchApplication(const UCDesktopEntry& entry,
                                                const std::vector<std::string>& files,
                                                std::string* error) {
    std::vector<std::string> argv = DesktopEntryCommand(entry, files);
    if (argv.empty()) {
        if (error) *error = "\"" + entry.name + "\" has no command to run.";
        return false;
    }
    if (entry.terminal) {
        // A terminal application: run it inside whichever terminal the
        // machine has, or at least the one the freedesktop alternatives name.
        static const char* const terminals[] = {"x-terminal-emulator", "gnome-terminal", "konsole",
                                                "xfce4-terminal", "alacritty", "kitty", "xterm"};
        for (const char* terminal : terminals) {
            if (FindProgram(terminal).empty()) continue;
            std::vector<std::string> wrapped{terminal, "-e"};
            wrapped.insert(wrapped.end(), argv.begin(), argv.end());
            argv = std::move(wrapped);
            break;
        }
    }
    std::string reason;
    const bool ok = LaunchDetachedProcess(argv, entry.workingDirectory, reason);
    if (!ok && error) *error = reason;
    return ok;
}

std::string UltraCanvasDesktopShell::FindProgram(const std::string& program) {
    if (program.empty()) return "";
    std::error_code ec;
    const fs::path given = PathFromUtf8(program);
    if (given.is_absolute() || program.find('/') != std::string::npos) {
        return IsExecutableFile(given) ? PathToUtf8(given) : "";
    }
    // Next to this executable first: a build tree, or a bundle where the
    // family of applications is installed together.
    const std::string here = GetExecutableDir();
    if (!here.empty()) {
        const fs::path sibling = PathFromUtf8(here) / given;
        if (IsExecutableFile(sibling)) return PathToUtf8(sibling);
#if defined(_WIN32)
        const fs::path siblingExe = PathFromUtf8(here) / PathFromUtf8(program + ".exe");
        if (IsExecutableFile(siblingExe)) return PathToUtf8(siblingExe);
#endif
    }
#if defined(_WIN32)
    const char separator = ';';
#else
    const char separator = ':';
#endif
    for (const std::string& dir : SplitPathList(EnvString("PATH"), separator)) {
        const fs::path candidate = PathFromUtf8(dir) / given;
        if (IsExecutableFile(candidate)) return PathToUtf8(candidate);
#if defined(_WIN32)
        const fs::path candidateExe = PathFromUtf8(dir) / PathFromUtf8(program + ".exe");
        if (IsExecutableFile(candidateExe)) return PathToUtf8(candidateExe);
#endif
    }
    return "";
}

bool UltraCanvasDesktopShell::LaunchProgram(const std::string& program,
                                            const std::vector<std::string>& arguments,
                                            std::string* error) {
    const std::string path = FindProgram(program);
    if (path.empty()) {
        if (error) *error = "\"" + program + "\" is not installed next to this application or on the PATH.";
        return false;
    }
    std::vector<std::string> argv{path};
    argv.insert(argv.end(), arguments.begin(), arguments.end());
    std::string reason;
    const bool ok = LaunchDetachedProcess(argv, "", reason);
    if (!ok && error) *error = reason;
    return ok;
}

// ===== NOTICES =====

std::string UltraCanvasDesktopShell::NoticesDirectory() {
    std::string base = EnvString("XDG_RUNTIME_DIR");
    if (base.empty()) base = EnvString("XDG_CACHE_HOME");
    if (base.empty()) {
        const std::string home = HomeDirectory();
        if (!home.empty()) base = home + "/.cache";
    }
    if (base.empty()) base = ".";
    return PathToUtf8(PathFromUtf8(base) / "ultraos" / "notices");
}

bool UltraCanvasDesktopShell::PublishNotice(const std::string& application, int count,
                                            const std::string& text) {
    if (application.empty()) return false;
    std::error_code ec;
    const fs::path dir = PathFromUtf8(NoticesDirectory());
    fs::create_directories(dir, ec);
    if (ec) return false;

    JSONValue root = JSONValue::MakeObject();
    root.Set("application", application);
    root.Set("count", count);
    root.Set("text", text);
    root.Set("updated", static_cast<int64_t>(std::time(nullptr)));
    JSONSerializeOptions options;
    options.pretty = true;

    // Written beside the target and renamed over it, so a reader never sees
    // half a file.
    const fs::path target = dir / PathFromUtf8(NoticeFileName(application));
    const fs::path temp = dir / PathFromUtf8(NoticeFileName(application) + ".tmp");
    if (!JSON::SerializeToFile(PathToUtf8(temp), root, options)) return false;
    fs::rename(temp, target, ec);
    if (ec) {
        fs::remove(temp, ec);
        return false;
    }
    return true;
}

bool UltraCanvasDesktopShell::RemoveNotice(const std::string& application) {
    if (application.empty()) return false;
    std::error_code ec;
    const fs::path target = PathFromUtf8(NoticesDirectory()) / PathFromUtf8(NoticeFileName(application));
    return fs::remove(target, ec) && !ec;
}

std::vector<DesktopNotice> UltraCanvasDesktopShell::ReadNotices() {
    std::vector<DesktopNotice> notices;
    std::error_code ec;
    const fs::path dir = PathFromUtf8(NoticesDirectory());
    if (!fs::is_directory(dir, ec)) return notices;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        if (PathToUtf8(it->path().extension()) != ".json") continue;
        DesktopNotice notice;
        if (ReadNoticeFile(it->path(), notice)) notices.push_back(std::move(notice));
    }
    std::sort(notices.begin(), notices.end(), [](const DesktopNotice& a, const DesktopNotice& b) {
        return a.application < b.application;
    });
    return notices;
}

DesktopNotice UltraCanvasDesktopShell::ReadNotice(const std::string& application) {
    DesktopNotice notice;
    notice.application = application;
    const fs::path file = PathFromUtf8(NoticesDirectory()) / PathFromUtf8(NoticeFileName(application));
    std::error_code ec;
    if (fs::is_regular_file(file, ec)) ReadNoticeFile(file, notice);
    return notice;
}

// ===== THE MONITOR =====

struct UltraCanvasDesktopShellMonitor::Impl {
    DesktopShellBackend::MonitorState* state = nullptr;
};

UltraCanvasDesktopShellMonitor::UltraCanvasDesktopShellMonitor() : impl(std::make_unique<Impl>()) {}

UltraCanvasDesktopShellMonitor::~UltraCanvasDesktopShellMonitor() {
    Stop();
}

bool UltraCanvasDesktopShellMonitor::Start(ChangedCallback onChanged) {
    if (!onChanged) return false;
    if (running.load()) return true;
    callback = std::move(onChanged);
    impl->state = DesktopShellBackend::MonitorOpen();
    native = impl->state != nullptr;
    running.store(true);
    if (!native) return true;   // nothing to watch here; Start() still succeeds

    worker = std::thread([this]() {
        while (running.load()) {
            if (!DesktopShellBackend::MonitorWait(impl->state)) break;
            if (!running.load()) break;
            if (callback) callback();
        }
    });
    return true;
}

void UltraCanvasDesktopShellMonitor::Stop() {
    if (!running.exchange(false)) return;
    if (impl->state) DesktopShellBackend::MonitorWake(impl->state);
    if (worker.joinable()) worker.join();
    if (impl->state) {
        DesktopShellBackend::MonitorClose(impl->state);
        impl->state = nullptr;
    }
    callback = nullptr;
    native = false;
}

// ===== FALLBACK BACKEND =====

#ifndef ULTRACANVAS_DESKTOPSHELL_NATIVE
namespace DesktopShellBackend {

    std::string BackendName() { return "null"; }
    bool IsAvailable() { return false; }

    std::vector<DesktopWindowInfo> ListWindows() { return {}; }
    uint64_t GetActiveWindow() { return 0; }
    bool ActivateWindow(uint64_t) { return false; }
    bool MinimizeWindow(uint64_t) { return false; }
    bool CloseWindow(uint64_t) { return false; }
    int  GetVirtualDesktopCount() { return 0; }
    int  GetCurrentVirtualDesktop() { return -1; }
    bool SetCurrentVirtualDesktop(int) { return false; }
    bool SetVirtualDesktopCount(int) { return false; }
    bool MoveWindowToVirtualDesktop(uint64_t, int) { return false; }

    bool GetScreenSize(int&, int&) { return false; }
    bool CaptureScreen(const std::string&, std::string& error) {
        error = "Screenshots are not available on this platform yet.";
        return false;
    }

    void ReadDeviceActivity(DesktopDeviceActivity& activity) {
        activity.warnings.push_back(
            "Camera, microphone, speaker, battery and keyboard state are not read on this platform yet.");
    }

    struct MonitorState {};
    MonitorState* MonitorOpen() { return nullptr; }
    bool MonitorWait(MonitorState*) { return false; }
    void MonitorWake(MonitorState*) {}
    void MonitorClose(MonitorState*) {}

} // namespace DesktopShellBackend
#endif // !ULTRACANVAS_DESKTOPSHELL_NATIVE

} // namespace UltraCanvas
