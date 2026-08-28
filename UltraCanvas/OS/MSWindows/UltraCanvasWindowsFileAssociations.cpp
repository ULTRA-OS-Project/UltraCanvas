// OS/MSWindows/UltraCanvasWindowsFileAssociations.cpp
// Windows backend of UltraCanvasFileAssociations, built on the shell
// association APIs: SHAssocEnumHandlers(".ext", ASSOC_FILTER_RECOMMENDED)
// enumerates exactly the handlers Explorer offers under "Open with", their
// display names come from IAssocHandler::GetUIName, and their icons from
// IAssocHandler::GetIconLocation, extracted once into PNG files under
// %LOCALAPPDATA%\UltraCanvas\openwith-icons (the shared menu API is
// image-file based, and the cache survives restarts).
// AssocQueryStringW marks which of the handlers is the current default and
// lifts it to the front of the list.
// Launching a specific handler goes through IAssocHandler::CreateInvoker /
// Invoke on an IDataObject built from the whole selection — the same path
// Explorer takes, so per-app quirks (DDE, UWP handlers, single-instance
// apps) are the shell's problem, not ours; a handler that cannot take the
// selection at once is invoked per file, and a plain executable handler
// falls back to a detached "app.exe file…" launch. Default open stays
// ShellExecuteExW's "open" verb, i.e. a double-click in Explorer.
// COM is initialized per call (apartment-threaded, balanced), because the
// core calls this backend from both the UI thread and its prewarm worker.
// All entry points are serialized by the core's backend mutex (see
// UltraCanvasFileAssociationsBackend.h) — no locking here.
// Version: 1.1.0
// Last Modified: 2026-08-24
// Author: UltraCanvas Framework

// SHAssocEnumHandlers / IAssocHandler are Vista+ and the mingw-w64 headers
// hide them below that; the default target there is still Server 2003.
#if !defined(_WIN32_WINNT) || _WIN32_WINNT < 0x0600
#  undef _WIN32_WINNT
#  define _WIN32_WINNT 0x0600
#endif
// Keep NTDDI_VERSION consistent with _WIN32_WINNT: the Windows SDK's sdkddkver.h errors on a
// mismatch when the host build already sets a higher _WIN32_WINNT (e.g. Ladybird's 0x0A00). NTDDI's
// high word IS the _WIN32_WINNT value, so derive it the way the SDK does by default; the floor above
// keeps _WIN32_WINNT (hence NTDDI) >= Vista. Works under both MinGW-w64 and MSVC/clang-cl.
#if !defined(NTDDI_VERSION) || (NTDDI_VERSION >> 16) < _WIN32_WINNT
#  undef NTDDI_VERSION
#  define NTDDI_VERSION (_WIN32_WINNT << 16)
#endif

#include "UltraCanvasFileAssociationsBackend.h"
#include "UltraCanvasUtils.h"   // Utf8ToWide, WideToUtf8, LaunchDetachedProcess
#include "UltraCanvasWindowsIcons.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>     // SHParseDisplayName
#include <shobjidl.h>   // SHAssocEnumHandlers, IAssocHandler
#include <shlwapi.h>    // AssocQueryStringW
#include <objbase.h>

#include <cairo/cairo.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace UltraCanvas {
namespace FileAssociationsBackend {

namespace {

    // Menu icons draw at 16 logical pixels; extracting 32 keeps them sharp
    // on the 150-200% displays Windows ships with by default.
    constexpr int kIconPixels = 32;

    // An extension nothing is registered for falls back to the unfiltered
    // handler list, which on a well-stocked machine is every application
    // that ever registered itself. A submenu does not scroll and every entry
    // costs an icon extraction, so the list is cut off — as far down as the
    // longest "Open with" flyout Explorer itself puts on screen.
    constexpr size_t kMaxCandidates = 20;

    // The association database is a live registry view with no change
    // notification worth polling, so entries simply expire: after this long
    // the next lookup re-reads them (§7.4 of the proposal). Icons stay on
    // disk across expiries, so re-resolution costs registry reads only.
    constexpr ULONGLONG kCacheLifetimeMs = 60 * 1000;

    // COM per call, apartment-threaded like the shell expects. The UI thread
    // is already OLE-initialized (UltraCanvasWindowsApplication) — that comes
    // back as S_FALSE and stays balanced by the matching CoUninitialize; a
    // worker thread that asked for MTA first gets RPC_E_CHANGED_MODE, where
    // COM is usable but must not be torn down here.
    class ComScope {
    public:
        ComScope() : hr(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
        ~ComScope() { if (SUCCEEDED(hr)) CoUninitialize(); }
        ComScope(const ComScope&) = delete;
        ComScope& operator=(const ComScope&) = delete;
    private:
        HRESULT hr;
    };

    // Shell strings come out of CoTaskMemAlloc'd buffers the caller frees.
    std::string TakeShellString(LPWSTR text) {
        if (!text) return {};
        std::string result = WideToUtf8(text);
        CoTaskMemFree(text);
        return result;
    }

    // ".txt" for anything with an extension; empty for extension-less names
    // ("Makefile"), which Windows cannot associate at all.
    std::wstring ExtensionOf(const std::string& fileName) {
        const size_t slash = fileName.find_last_of("/\\");
        const std::string name = slash == std::string::npos
                                 ? fileName : fileName.substr(slash + 1);
        const size_t dot = name.find_last_of('.');
        if (dot == std::string::npos || dot == 0 || dot + 1 >= name.size())
            return {};
        return Utf8ToWide(name.substr(dot));
    }

    bool EqualsIgnoreCase(const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i]))) return false;
        }
        return true;
    }

    std::string FileNameOf(const std::string& path) {
        const size_t slash = path.find_last_of("/\\");
        return slash == std::string::npos ? path : path.substr(slash + 1);
    }

    // ===== ICON CACHE =====

    // %LOCALAPPDATA%\UltraCanvas\openwith-icons — created on demand. TEMP
    // covers the (rare) account without a local app-data directory.
    std::string IconCacheDir() {
        static const std::string dir = []() -> std::string {
            const char* roots[] = { std::getenv("LOCALAPPDATA"),
                                    std::getenv("TEMP"),
                                    std::getenv("TMP") };
            for (const char* root : roots) {
                if (!root || !*root) continue;
                const std::string candidate =
                        std::string(root) + "\\UltraCanvas\\openwith-icons";
                std::error_code ec;
                fs::create_directories(PathFromUtf8(candidate), ec);
                if (!ec) return candidate;
            }
            return {};
        }();
        return dir;
    }

    std::string HashKey(const std::string& text) {
        uint64_t hash = 1469598103934665603ull;          // FNV-1a, 64 bit
        for (unsigned char c : text) {
            hash ^= static_cast<uint64_t>(std::tolower(c));
            hash *= 1099511628211ull;
        }
        char buffer[17] = {};
        std::snprintf(buffer, sizeof buffer, "%016llx",
                      static_cast<unsigned long long>(hash));
        return buffer;
    }

    cairo_status_t WritePngChunk(void* closure, const unsigned char* data,
                                 unsigned int length) {
        FILE* file = static_cast<FILE*>(closure);
        return std::fwrite(data, 1, length, file) == length
               ? CAIRO_STATUS_SUCCESS : CAIRO_STATUS_WRITE_ERROR;
    }

    bool WritePng(const std::shared_ptr<UCPixmap>& pixmap,
                  const std::string& path) {
        if (!pixmap || !pixmap->IsValid()) return false;
        cairo_surface_t* surface = pixmap->GetSurface();
        if (!surface) return false;
        cairo_surface_flush(surface);
        // Write beside the target and rename: two processes extracting the
        // same icon at once must never leave a half-written PNG behind.
        const std::string temp = path + "." +
                std::to_string(static_cast<unsigned long>(GetCurrentProcessId()))
                + ".tmp";
        // Through a stream, not cairo_surface_write_to_png: that one opens
        // the narrow path in the process code page, which mangles a cache
        // directory under a user name outside it.
        FILE* file = _wfopen(Utf8ToWide(temp).c_str(), L"wb");
        if (!file) return false;
        const cairo_status_t status =
                cairo_surface_write_to_png_stream(surface, WritePngChunk, file);
        std::fclose(file);
        if (status != CAIRO_STATUS_SUCCESS) {
            std::error_code ec;
            fs::remove(PathFromUtf8(temp), ec);
            return false;
        }
        std::error_code ec;
        fs::rename(PathFromUtf8(temp), PathFromUtf8(path), ec);
        if (ec) {
            // Another process won the race: its file is just as good.
            fs::remove(PathFromUtf8(temp), ec);
            return fs::exists(PathFromUtf8(path), ec);
        }
        return true;
    }

    // Icon `index` of `location` as a PNG file the menu can draw. Cached on
    // disk by location+index, so the extraction happens once per machine.
    std::string CachedIconFile(const std::wstring& location, int index) {
        if (location.empty()) return {};
        const std::string dir = IconCacheDir();
        if (dir.empty()) return {};
        const std::string key = HashKey(WideToUtf8(location) + "|" +
                                        std::to_string(index));
        const std::string path = dir + "\\" + key + ".png";
        std::error_code ec;
        if (fs::exists(PathFromUtf8(path), ec) && !ec) return path;
        std::shared_ptr<UCPixmap> pixmap =
                WindowsIcons::LoadIconResourcePixmap(location, index, kIconPixels);
        if (!pixmap) return {};
        return WritePng(pixmap, path) ? path : std::string();
    }

    // ===== HANDLER ENUMERATION =====

    // Explorer's own "Open with" list is the recommended one; the unfiltered
    // list is only worth asking for when that comes back empty.
    bool EnumerateHandlers(const std::wstring& extension, ASSOC_FILTER filter,
                           const std::function<bool(IAssocHandler*)>& visit) {
        IEnumAssocHandlers* enumerator = nullptr;
        if (FAILED(SHAssocEnumHandlers(extension.c_str(), filter, &enumerator)) ||
            !enumerator)
            return false;
        bool any = false;
        for (;;) {
            IAssocHandler* handler = nullptr;
            ULONG fetched = 0;
            if (enumerator->Next(1, &handler, &fetched) != S_OK ||
                fetched != 1 || !handler)
                break;
            any = true;
            const bool keepGoing = visit(handler);
            handler->Release();
            if (!keepGoing) break;
        }
        enumerator->Release();
        return any;
    }

    void ForEachHandler(const std::wstring& extension,
                        const std::function<bool(IAssocHandler*)>& visit) {
        if (extension.empty()) return;
        if (!EnumerateHandlers(extension, ASSOC_FILTER_RECOMMENDED, visit))
            EnumerateHandlers(extension, ASSOC_FILTER_NONE, visit);
    }

    std::string AssocString(const std::wstring& extension, ASSOCSTR what) {
        wchar_t buffer[2048];
        DWORD size = static_cast<DWORD>(std::size(buffer));
        if (FAILED(AssocQueryStringW(ASSOCF_NONE, what, extension.c_str(),
                                     nullptr, buffer, &size)))
            return {};
        return WideToUtf8(buffer);
    }

    // A handler is the default when the shell names its executable (or its
    // friendly name) as the one a double-click would run.
    bool IsDefaultHandler(const FileAssociationApp& app,
                          const std::string& defaultExecutable,
                          const std::string& defaultName) {
        if (!defaultExecutable.empty() && !app.id.empty()) {
            if (EqualsIgnoreCase(app.id, defaultExecutable)) return true;
            if (EqualsIgnoreCase(FileNameOf(app.id),
                                 FileNameOf(defaultExecutable))) return true;
        }
        return !defaultName.empty() && EqualsIgnoreCase(app.name, defaultName);
    }

} // namespace

// ===== BACKEND ENTRY POINTS =====

bool RefreshGlobalIndex() {
    // There is no database to parse up front on Windows — everything is
    // resolved per extension. What this does own is the expiry: the first
    // call starts the clock, every call after the lifetime restarts it and
    // reports "rebuilt" so the core drops its now-stale candidate cache.
    static ULONGLONG lastRefresh = 0;
    const ULONGLONG now = GetTickCount64();
    if (lastRefresh == 0) {
        lastRefresh = now;
        return false;      // nothing cached yet — dropping would be pointless
    }
    if (now - lastRefresh < kCacheLifetimeMs) return false;
    lastRefresh = now;
    return true;
}

std::vector<FileAssociationApp> ResolveFile(const std::string& fileName) {
    const std::wstring extension = ExtensionOf(fileName);
    if (extension.empty()) return {};   // no extension, no association

    ComScope com;
    const std::string defaultExecutable =
            AssocString(extension, ASSOCSTR_EXECUTABLE);
    const std::string defaultName =
            AssocString(extension, ASSOCSTR_FRIENDLYAPPNAME);

    std::vector<FileAssociationApp> apps;
    std::unordered_set<std::string> seen;
    ForEachHandler(extension, [&](IAssocHandler* handler) {
        LPWSTR raw = nullptr;
        FileAssociationApp app;
        if (SUCCEEDED(handler->GetName(&raw))) app.id = TakeShellString(raw);
        raw = nullptr;
        if (SUCCEEDED(handler->GetUIName(&raw))) app.name = TakeShellString(raw);
        if (app.id.empty()) app.id = app.name;
        if (app.name.empty()) app.name = FileNameOf(app.id);
        // A handler with neither a name nor an identity cannot be shown or
        // launched again later.
        if (app.name.empty()) return true;
        if (!seen.insert(app.id).second) return true;

        LPWSTR iconLocation = nullptr;
        int iconIndex = 0;
        if (SUCCEEDED(handler->GetIconLocation(&iconLocation, &iconIndex)) &&
            iconLocation) {
            app.iconPath = CachedIconFile(iconLocation, iconIndex);
            CoTaskMemFree(iconLocation);
        }
        if (app.iconPath.empty() && !app.id.empty()) {
            // No icon resource named: the handler's own executable carries
            // the icon Explorer would show for it.
            std::error_code ec;
            if (fs::is_regular_file(PathFromUtf8(app.id), ec) && !ec)
                app.iconPath = CachedIconFile(Utf8ToWide(app.id), 0);
        }
        apps.push_back(std::move(app));
        return apps.size() < kMaxCandidates;
    });

    // Default first, exactly as the "Open with" flyout orders it.
    for (size_t i = 0; i < apps.size(); ++i) {
        if (!IsDefaultHandler(apps[i], defaultExecutable, defaultName)) continue;
        apps[i].isDefault = true;
        if (i != 0) std::rotate(apps.begin(), apps.begin() + i,
                                apps.begin() + i + 1);
        break;
    }
    return apps;
}

bool LaunchDefault(const std::vector<std::string>& paths, std::string& outError) {
    bool allOk = true;
    for (const std::string& path : paths) {
        const std::wstring file = Utf8ToWide(path);
        const std::wstring directory =
                Utf8ToWide(PathToUtf8(PathFromUtf8(path).parent_path()));
        SHELLEXECUTEINFOW info = {};
        info.cbSize = sizeof(info);
        info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
        info.lpVerb = L"open";
        info.lpFile = file.c_str();
        info.lpDirectory = directory.empty() ? nullptr : directory.c_str();
        info.nShow = SW_SHOWNORMAL;
        if (ShellExecuteExW(&info)) continue;
        allOk = false;
        if (!outError.empty()) outError += "\n";
        outError += "Could not open \"" + path + "\".";
    }
    return allOk;
}

namespace {

    // The selection as one shell data object — what IAssocHandler::Invoke
    // takes, and what lets an application open several files in one window.
    IDataObject* CreateDataObject(const std::vector<std::string>& paths) {
        std::vector<PIDLIST_ABSOLUTE> ids;
        for (const std::string& path : paths) {
            PIDLIST_ABSOLUTE id = nullptr;
            const std::wstring wide = Utf8ToWide(path);
            if (SUCCEEDED(SHParseDisplayName(wide.c_str(), nullptr, &id, 0,
                                             nullptr)) && id)
                ids.push_back(id);
        }
        IShellItemArray* items = nullptr;
        HRESULT hr = E_FAIL;
        if (!ids.empty()) {
            // The array parameter is const-qualified differently between
            // the mingw-w64 and the MSVC (typed-pidl) headers — neither
            // conversion is implicit, and only a C cast spells both.
            hr = SHCreateShellItemArrayFromIDLists(
                    static_cast<UINT>(ids.size()),
                    (PCIDLIST_ABSOLUTE_ARRAY)ids.data(),
                    &items);
        }
        for (PIDLIST_ABSOLUTE id : ids) CoTaskMemFree(id);
        if (FAILED(hr) || !items) return nullptr;

        IDataObject* data = nullptr;
        hr = items->BindToHandler(nullptr, BHID_DataObject,
                                  IID_PPV_ARGS(&data));
        items->Release();
        return SUCCEEDED(hr) ? data : nullptr;
    }

    // One handler, one set of files. `wholeSelection` says whether the files
    // may be handed over at once — an invoker that reports no selection
    // support gets them one at a time instead.
    bool InvokeHandler(IAssocHandler* handler,
                       const std::vector<std::string>& paths,
                       bool wholeSelection) {
        IDataObject* data = CreateDataObject(paths);
        if (!data) return false;
        bool ok = false;
        bool haveInvoker = false;
        IAssocHandlerInvoker* invoker = nullptr;
        if (SUCCEEDED(handler->CreateInvoker(data, &invoker)) && invoker) {
            haveInvoker = true;
            if (!wholeSelection || paths.size() == 1 ||
                invoker->SupportsSelection() == S_OK)
                ok = SUCCEEDED(invoker->Invoke());
            invoker->Release();
        }
        // Invoke() is the pre-invoker spelling of the same call — worth a
        // try when the handler has no invoker, but never after one told us
        // it cannot take this selection.
        if (!ok && !haveInvoker) ok = SUCCEEDED(handler->Invoke(data));
        data->Release();
        return ok;
    }

} // namespace

bool LaunchWith(const FileAssociationApp& app,
                const std::vector<std::string>& paths, std::string& outError) {
    if (paths.empty()) return false;
    ComScope com;

    bool launched = false;
    bool found = false;
    ForEachHandler(ExtensionOf(paths[0]), [&](IAssocHandler* handler) {
        LPWSTR raw = nullptr;
        std::string id;
        if (SUCCEEDED(handler->GetName(&raw))) id = TakeShellString(raw);
        if (id.empty()) {
            raw = nullptr;
            if (SUCCEEDED(handler->GetUIName(&raw))) id = TakeShellString(raw);
        }
        if (!EqualsIgnoreCase(id, app.id)) return true;   // keep looking
        found = true;
        launched = InvokeHandler(handler, paths, true);
        if (!launched) {
            // Some handlers refuse a multi-file data object outright; give
            // them the files one by one before declaring failure.
            launched = true;
            for (const std::string& path : paths)
                if (!InvokeHandler(handler, {path}, false)) launched = false;
        }
        return false;
    });

    if (launched) return true;

    // Last resort for a plain desktop application: run it with the files as
    // arguments. Covers handlers whose shell invocation failed as well as an
    // application that has since been re-registered under a different id.
    std::error_code ec;
    if (!app.id.empty() && fs::is_regular_file(PathFromUtf8(app.id), ec) && !ec) {
        std::vector<std::string> argv{app.id};
        argv.insert(argv.end(), paths.begin(), paths.end());
        std::string error;
        if (LaunchDetachedProcess(argv,
                                  PathToUtf8(PathFromUtf8(paths[0]).parent_path()),
                                  error))
            return true;
    }

    outError = found
            ? "\"" + app.name + "\" could not open the selection."
            : "The application \"" + app.name + "\" is no longer registered "
              "for this file type.";
    return false;
}

bool LaunchWithPath(const std::string& applicationPath,
                    const std::vector<std::string>& paths, std::string& outError) {
    std::vector<std::string> argv{applicationPath};
    argv.insert(argv.end(), paths.begin(), paths.end());
    const std::string workingDir = paths.empty()
            ? std::string()
            : PathToUtf8(PathFromUtf8(paths[0]).parent_path());
    return LaunchDetachedProcess(argv, workingDir, outError);
}

FileAssociations::ApplicationFilter GetApplicationFilter() {
    return {"Applications", {"exe", "com", "bat", "cmd"}};
}

std::string GetApplicationsDirectory() {
    const char* programFiles = std::getenv("ProgramFiles");
    std::error_code ec;
    if (programFiles && fs::is_directory(PathFromUtf8(programFiles), ec) && !ec)
        return programFiles;
    return {};
}

} // namespace FileAssociationsBackend
} // namespace UltraCanvas
