// Apps/UltraStore/core/Launcher.cpp
// The application-menu entry of an installed program (Launcher.h).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "Launcher.h"

#include "UltraCanvasPathUtf8.h"

#include <filesystem>
#include <fstream>
#include <system_error>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <objbase.h>
#  include <shlobj.h>
#endif

namespace UltraStore {

namespace fs = std::filesystem;
using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

namespace {

// A file name out of a display name: the characters no file system takes
// become '_'.
std::string SafeFileName(const std::string& name) {
    std::string out;
    for (char c : name) {
        const bool bad = static_cast<unsigned char>(c) < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' ||
                         c == '?' || c == '"' || c == '<' || c == '>' || c == '|';
        out += bad ? '_' : c;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    return out;
}

// Exec= quoting of the Desktop Entry Specification: the argument in double
// quotes, with `"`, '`', `$` and `\` escaped, and a literal % doubled.
std::string QuoteExec(const std::string& path) {
    std::string out = "\"";
    for (char c : path) {
        if (c == '"' || c == '`' || c == '$' || c == '\\') out += '\\';
        if (c == '%') out += '%';
        out += c;
    }
    out += '"';
    return out;
}

// A value on one line: a line break would start a new key.
std::string OneLine(const std::string& text) {
    std::string out;
    for (char c : text) out += (c == '\n' || c == '\r') ? ' ' : c;
    return out;
}

#if defined(_WIN32)
fs::path SystemFolder() {
    PWSTR raw = nullptr;
    fs::path folder;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &raw)) && raw) folder = fs::path(raw);   // path-string-ok: wide, from the system
    if (raw) CoTaskMemFree(raw);
    return folder;
}
#elif defined(__APPLE__)
fs::path SystemFolder() {
    const std::string home = UltraCanvas::GetEnvUtf8("HOME");   // path-string-ok: GetEnvUtf8 is the UTF-8 read
    return home.empty() ? fs::path() : PathFromUtf8(home) / "Applications";
}

// The bundle a program file is in: the nearest folder named *.app above it.
fs::path BundleOf(const fs::path& executable) {
    for (fs::path p = executable.parent_path(); !p.empty() && p != p.root_path(); p = p.parent_path()) {
        if (p.extension() == ".app") return p;
    }
    return {};
}
#else
fs::path SystemFolder() {
    const std::string data = UltraCanvas::GetEnvUtf8("XDG_DATA_HOME");   // path-string-ok: GetEnvUtf8 is the UTF-8 read
    if (!data.empty()) return PathFromUtf8(data) / "applications";
    const std::string home = UltraCanvas::GetEnvUtf8("HOME");            // path-string-ok: GetEnvUtf8 is the UTF-8 read
    return home.empty() ? fs::path() : PathFromUtf8(home) / ".local" / "share" / "applications";
}
#endif

fs::path FolderOf(const LauncherEntry& entry) {
    return entry.folderOverride.empty() ? SystemFolder() : PathFromUtf8(entry.folderOverride);
}

fs::path PathOf(const LauncherEntry& entry) {
    const fs::path folder = FolderOf(entry);
    if (folder.empty()) return {};
#if defined(_WIN32)
    const std::string file = SafeFileName(entry.name.empty() ? entry.id : entry.name);
    return file.empty() ? fs::path() : folder / PathFromUtf8(file + ".lnk");
#elif defined(__APPLE__)
    if (BundleOf(PathFromUtf8(entry.executable)).empty()) return {};
    const std::string file = SafeFileName(entry.name.empty() ? entry.id : entry.name);
    return file.empty() ? fs::path() : folder / PathFromUtf8(file + ".app");
#else
    const std::string file = SafeFileName(entry.id.empty() ? entry.name : entry.id);
    return file.empty() ? fs::path() : folder / PathFromUtf8(file + ".desktop");
#endif
}

#if defined(_WIN32)
// The shortcut through the shell's own object, so the Start menu reads it
// like any installer's.
bool WriteShortcut(const fs::path& link, const LauncherEntry& entry, std::string& error) {
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool uninit = SUCCEEDED(init);   // S_OK or S_FALSE: balanced below; RPC_E_CHANGED_MODE: COM is up in another mode
    bool ok = false;
    IShellLinkW* shellLink = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW,
                                  reinterpret_cast<void**>(&shellLink));
    if (SUCCEEDED(hr) && shellLink) {
        const fs::path executable = PathFromUtf8(entry.executable);
        shellLink->SetPath(executable.c_str());
        shellLink->SetWorkingDirectory(executable.parent_path().c_str());
        if (!entry.comment.empty()) shellLink->SetDescription(PathFromUtf8(OneLine(entry.comment)).c_str());   // path-string-ok: UTF-8 to wide, not a path
        const fs::path icon = entry.icon.empty() ? executable : PathFromUtf8(entry.icon);
        shellLink->SetIconLocation(icon.c_str(), 0);
        IPersistFile* file = nullptr;
        hr = shellLink->QueryInterface(IID_IPersistFile, reinterpret_cast<void**>(&file));
        if (SUCCEEDED(hr) && file) {
            hr = file->Save(link.c_str(), TRUE);
            ok = SUCCEEDED(hr);
            file->Release();
        }
        shellLink->Release();
    }
    if (!ok) error = "the shell refused the shortcut (HRESULT " + std::to_string(static_cast<long>(hr)) + ")";
    if (uninit) CoUninitialize();
    return ok;
}
#endif

} // namespace

std::string DesktopEntryText(const LauncherEntry& entry) {
    std::string text = "[Desktop Entry]\nType=Application\nVersion=1.0\n";
    text += "Name=" + OneLine(entry.name.empty() ? entry.id : entry.name) + "\n";
    if (!entry.comment.empty()) text += "Comment=" + OneLine(entry.comment) + "\n";
    text += "Exec=" + QuoteExec(entry.executable) + " %u\n";
    text += "TryExec=" + OneLine(entry.executable) + "\n";
    if (!entry.icon.empty()) text += "Icon=" + OneLine(entry.icon) + "\n";
    if (!entry.categories.empty()) text += "Categories=" + OneLine(entry.categories) + "\n";
    if (!entry.mimeTypes.empty()) text += "MimeType=" + OneLine(entry.mimeTypes) + "\n";
    if (!entry.windowClass.empty()) text += "StartupWMClass=" + OneLine(entry.windowClass) + "\n";
    text += "Terminal=false\nStartupNotify=true\n";
    return text;
}

std::string LauncherPath(const LauncherEntry& entry) {
    const fs::path path = PathOf(entry);
    return path.empty() ? std::string() : PathToUtf8(path);
}

Result<std::string> WriteLauncher(const LauncherEntry& entry) {
    using R = Result<std::string>;
    if (entry.executable.empty()) return R::Failure(ErrorCode::InvalidArgument, "The menu entry names no program");
    const fs::path path = PathOf(entry);
    if (path.empty()) return R::Failure(ErrorCode::NotAvailable, "This system has no application menu folder for the entry");
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) return R::Failure(ErrorCode::IoError, "The application menu folder could not be created", PathToUtf8(path.parent_path()));
#if defined(_WIN32)
    std::string error;
    if (!WriteShortcut(path, entry, error)) return R::Failure(ErrorCode::IoError, "The Start menu shortcut could not be written", error);
#elif defined(__APPLE__)
    const fs::path bundle = BundleOf(PathFromUtf8(entry.executable));
    fs::remove(path, ec);
    ec.clear();
    fs::create_directory_symlink(bundle, path, ec);
    if (ec) return R::Failure(ErrorCode::IoError, "The link in Applications could not be made", ec.message());
#else
    // Written beside and renamed over: the menu never reads half an entry.
    const fs::path temp = path.parent_path() / PathFromUtf8(PathToUtf8(path.filename()) + ".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << DesktopEntryText(entry);
        if (!out) {
            fs::remove(temp, ec);
            return R::Failure(ErrorCode::IoError, "The menu entry could not be written", PathToUtf8(path));
        }
    }
    fs::permissions(temp, fs::perms::owner_exec, fs::perm_options::add, ec);   // some desktops start only trusted entries
    ec.clear();
    fs::rename(temp, path, ec);
    if (ec) {
        std::error_code ignore;
        fs::remove(temp, ignore);
        return R::Failure(ErrorCode::IoError, "The menu entry could not be written", ec.message());
    }
#endif
    return R::Succeed(PathToUtf8(path));
}

Result<void> RemoveLauncher(const LauncherEntry& entry) {
    const fs::path path = PathOf(entry);
    if (path.empty()) return Result<void>::Succeed();
    std::error_code ec;
    fs::remove(path, ec);
    if (ec && fs::exists(path, ec)) return Result<void>::Failure(ErrorCode::IoError, "The menu entry could not be removed", PathToUtf8(path));
    return Result<void>::Succeed();
}

} // namespace UltraStore
