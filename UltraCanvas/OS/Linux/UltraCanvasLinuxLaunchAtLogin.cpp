// OS/Linux/UltraCanvasLinuxLaunchAtLogin.cpp
// UltraCanvasLaunchAtLogin on Linux and the BSDs: an XDG autostart entry
// (the Desktop Application Autostart Specification) in
// $XDG_CONFIG_HOME/autostart, which every freedesktop session starts at
// sign-in. An entry of the same name in a system folder ($XDG_CONFIG_DIRS,
// /etc/xdg/autostart) is turned off by a Hidden=true entry in the user's.
// Version: 1.0.0
// Last Modified: 2026-11-26
// Author: UltraCanvas Framework
#include "UltraCanvasLaunchAtLogin.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include <unistd.h>

namespace fs = std::filesystem;

namespace UltraCanvas {

namespace {

std::string Env(const char* name) {
    const char* value = std::getenv(name);
    return value ? std::string(value) : std::string();
}

// A Flatpak's or a Snap's XDG_CONFIG_HOME is a folder of its own, which no
// session reads autostart entries from.
bool Sandboxed() {
    if (!Env("FLATPAK_ID").empty() || !Env("SNAP_NAME").empty()) return true;
    std::error_code ec;
    return fs::exists("/.flatpak-info", ec);
}

fs::path UserConfigDir() {
    std::string dir = Env("XDG_CONFIG_HOME");
    if (dir.empty() || dir.front() != '/') {
        const std::string home = Env("HOME");
        if (home.empty()) return {};
        return PathFromUtf8(home) / ".config";
    }
    return PathFromUtf8(dir);
}

fs::path UserEntryPath(const std::string& id) {
    const fs::path config = UserConfigDir();
    if (config.empty()) return {};
    return config / "autostart" / PathFromUtf8(id + ".desktop");
}

// The system's entry of this name, the first in $XDG_CONFIG_DIRS that has one.
fs::path SystemEntryPath(const std::string& id) {
    std::string dirs = Env("XDG_CONFIG_DIRS");
    if (dirs.empty()) dirs = "/etc/xdg";
    std::stringstream list(dirs);
    std::string dir;
    while (std::getline(list, dir, ':')) {
        if (dir.empty() || dir.front() != '/') continue;
        const fs::path candidate = PathFromUtf8(dir) / "autostart" / PathFromUtf8(id + ".desktop");
        std::error_code ec;
        if (fs::is_regular_file(candidate, ec)) return candidate;
    }
    return {};
}

// Whether an entry file starts its program: not Hidden, not switched off
// with GNOME's X-GNOME-Autostart-enabled=false.
bool EntryStarts(const fs::path& file) {
    std::ifstream in(file);
    if (!in) return false;
    bool inMainGroup = false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        if (line.front() == '[') {
            inMainGroup = line == "[Desktop Entry]";
            continue;
        }
        if (!inMainGroup) continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        while (!key.empty() && key.back() == ' ') key.pop_back();
        while (!value.empty() && value.front() == ' ') value.erase(value.begin());
        if (key == "Hidden" && value == "true") return false;
        if (key == "X-GNOME-Autostart-enabled" && value == "false") return false;
    }
    return true;
}

std::string ThisProgram() {
    // Inside an AppImage /proc/self/exe is the mounted copy, gone when the
    // program ends; the AppImage itself is what is started again.
    const std::string appImage = Env("APPIMAGE");
    if (!appImage.empty() && appImage.front() == '/') return appImage;
    for (const char* link : {"/proc/self/exe", "/proc/curproc/file", "/proc/curproc/exe"}) {
        std::error_code ec;
        const fs::path target = fs::read_symlink(link, ec);
        if (!ec && !target.empty()) return PathToUtf8(target);
    }
    return {};
}

// One argument of Exec=, quoted where the specification asks: inside double
// quotes ", `, $ and \ take a backslash. A literal % is written %%.
std::string QuoteExecArgument(const std::string& arg) {
    static const std::string kReserved = " \t\n\"'\\><~|&;$*?#()`";
    std::string out;
    const bool quote = arg.empty() || arg.find_first_of(kReserved) != std::string::npos;
    if (quote) out += '"';
    for (char c : arg) {
        if (quote && (c == '"' || c == '`' || c == '$' || c == '\\')) out += '\\';
        if (c == '%') out += '%';
        out += c;
    }
    if (quote) out += '"';
    return out;
}

// A value of type string: a backslash, a newline and a tab escaped. Applied
// to Exec= over its own quoting, so a backslash inside quotes becomes four.
std::string EscapeValue(const std::string& value) {
    std::string out;
    for (char c : value) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else if (c == '\t') out += "\\t";
        else if (c == '\r') out += "\\r";
        else out += c;
    }
    return out;
}

std::string ExecLine(const LaunchAtLoginEntry& entry) {
    const std::string program = entry.executable.empty() ? ThisProgram() : entry.executable;
    if (program.empty()) return {};
    std::string line = QuoteExecArgument(program);
    for (const std::string& arg : entry.arguments) line += " " + QuoteExecArgument(arg);
    return line;
}

bool WriteFileAtomically(const fs::path& file, const std::string& text, std::string& error) {
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    if (ec) {
        error = TrF("The startup list could not be changed: {0}", PathToUtf8(file.parent_path()) + ": " + ec.message());
        return false;
    }
    fs::path temp = file;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << text;
        out.close();
        if (!out) {
            fs::remove(temp, ec);
            error = TrF("The startup list could not be changed: {0}", PathToUtf8(temp));
            return false;
        }
    }
    fs::rename(temp, file, ec);
    if (ec) {
        std::error_code ignored;
        fs::remove(temp, ignored);
        error = TrF("The startup list could not be changed: {0}", PathToUtf8(file) + ": " + ec.message());
        return false;
    }
    return true;
}

} // namespace

namespace LaunchAtLoginBackend {

bool Available() {
    return !Sandboxed() && !UserConfigDir().empty();
}

bool IsEnabled(const std::string& id) {
    const fs::path user = UserEntryPath(id);
    std::error_code ec;
    if (!user.empty() && fs::is_regular_file(user, ec)) return EntryStarts(user);
    const fs::path system = SystemEntryPath(id);
    return !system.empty() && EntryStarts(system);
}

bool Set(const LaunchAtLoginEntry& entry, bool enabled, std::string& error) {
    const fs::path user = UserEntryPath(entry.id);
    if (user.empty()) {
        error = Tr("This computer cannot start programs when you sign in.");
        return false;
    }
    std::error_code ec;
    if (!enabled) {
        // A system entry of this name would start the program all the same:
        // the user's Hidden=true one overrides it. Otherwise ours goes.
        const fs::path system = SystemEntryPath(entry.id);
        if (!system.empty() && EntryStarts(system)) {
            return WriteFileAtomically(user, "[Desktop Entry]\nType=Application\nName=" +
                                       EscapeValue(entry.displayName.empty() ? entry.id : entry.displayName) +
                                       "\nHidden=true\n", error);
        }
        fs::remove(user, ec);
        if (ec) {
            error = TrF("The startup list could not be changed: {0}", PathToUtf8(user) + ": " + ec.message());
            return false;
        }
        return true;
    }

    const std::string exec = ExecLine(entry);
    if (exec.empty()) {
        error = Tr("Where this program is installed could not be found.");
        return false;
    }
    std::string text = "[Desktop Entry]\n"
                       "Type=Application\n"
                       "Version=1.5\n";
    text += "Name=" + EscapeValue(entry.displayName.empty() ? entry.id : entry.displayName) + "\n";
    if (!entry.comment.empty()) text += "Comment=" + EscapeValue(entry.comment) + "\n";
    if (!entry.iconName.empty()) text += "Icon=" + EscapeValue(entry.iconName) + "\n";
    text += "Exec=" + EscapeValue(exec) + "\n";
    text += "Terminal=false\n"
            "X-GNOME-Autostart-enabled=true\n";
    return WriteFileAtomically(user, text, error);
}

std::string Command(const LaunchAtLoginEntry& entry) {
    const std::string program = entry.executable.empty() ? ThisProgram() : entry.executable;
    if (program.empty()) return {};
    std::string line = program;
    for (const std::string& arg : entry.arguments) line += " " + arg;
    return line;
}

} // namespace LaunchAtLoginBackend

} // namespace UltraCanvas
