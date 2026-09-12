// OS/MSWindows/UltraCanvasWindowsFileAssociations.cpp
// Windows backend of UltraCanvasFileAssociations, built on the shell
// association APIs: SHAssocEnumHandlers(".ext", ASSOC_FILTER_RECOMMENDED)
// enumerates exactly the handlers Explorer offers under "Open with", their
// display names come from IAssocHandler::GetUIName, and their icons from
// IAssocHandler::GetIconLocation, extracted once into PNG files under
// %LOCALAPPDATA%\UltraCanvas\openwith-icons (the shared menu API is
// image-file based, and the cache survives restarts). Because it survives
// restarts it also has to be swept: an entry is keyed by the icon's location,
// so upgrading or uninstalling an application orphans its PNG for good. Each
// file carries the day it was last served as its modification time, and the
// first lookup of a process deletes everything not served for two weeks.
// AssocQueryStringW marks which of the handlers is the current default and
// lifts it to the front of the list; when the enumeration does not contain
// that program at all - a ProgID carrying a shell\open command but no
// OpenWithProgids registration is not a handler the enumerator reports - the
// one the registry names is added in front of the list instead, so "is a
// program assigned to this file type" has a truthful answer. The chooser
// stub OpenWith.exe is never that program: the shell names it exactly when
// nothing is registered.
// Launching a specific handler goes through IAssocHandler::CreateInvoker /
// Invoke on an IDataObject built from the whole selection — the same path
// Explorer takes, so per-app quirks (DDE, UWP handlers, single-instance
// apps) are the shell's problem, not ours; a handler that cannot take the
// selection at once is invoked per file, and a plain executable handler
// falls back to a detached "app.exe file…" launch. Default open stays
// ShellExecuteExW's "open" verb, i.e. a double-click in Explorer: on a full
// native path (the shell resolves neither a relative name nor a "C:/x/y"
// spelling), with the file type's registered shell extension free to run
// because COM is initialized around the call, and with the shell's own
// "How do you want to open this file?" chooser (the "openas" verb) put up
// when the type turns out to have no handler at all - what Explorer does
// instead of failing. A launch that still fails reports the reason the
// shell gave rather than "could not open".
// COM is initialized per call (apartment-threaded, balanced), because the
// core calls this backend from both the UI thread and its prewarm worker.
// All entry points are serialized by the core's backend mutex (see
// UltraCanvasFileAssociationsBackend.h) — no locking here.
// Version: 1.3.0
// Last Modified: 2026-09-12
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
#include <mutex>
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

    // ===== PATHS THE SHELL ACCEPTS =====

    // Every shell entry point below — ShellExecuteEx, SHParseDisplayName, and
    // the DDE conversations older associations still run on — wants a full
    // native path. A relative name resolves against the process's working
    // directory rather than the folder on screen, and a path spelled with
    // forward slashes ("C:/Users/me/notes.txt") is not a path to the shell at
    // all: the association lookup never gets as far as the extension and the
    // call comes back "no application is associated", for a file type that is
    // registered perfectly well. std::filesystem hands both spellings through
    // — a listing keeps the separators the folder it was opened with carried —
    // so the conversion happens here, once, for every caller.
    std::wstring NativeShellPath(const std::string& path) {
        std::wstring wide = Utf8ToWide(path);
        for (wchar_t& c : wide) if (c == L'/') c = L'\\';
        if (wide.empty()) return wide;
        const DWORD needed = GetFullPathNameW(wide.c_str(), 0, nullptr, nullptr);
        if (needed == 0) return wide;   // not a file-system path: leave it be
        std::wstring full(needed, L'\0');
        const DWORD written =
                GetFullPathNameW(wide.c_str(), needed, &full[0], nullptr);
        if (written == 0 || written >= needed) return wide;
        full.resize(written);
        return full;
    }

    // The containing folder as the launched program's working directory, in
    // the same native spelling.
    std::wstring NativeParentDirectory(const std::wstring& nativeFile) {
        const size_t slash = nativeFile.find_last_of(L'\\');
        if (slash == std::wstring::npos || slash == 0) return {};
        return nativeFile.substr(0, slash);
    }

    // ===== WHAT A FAILED LAUNCH MEANS =====

    // An extension nothing is registered for does not leave the shell without
    // an answer: AssocQueryString names %SystemRoot%\system32\OpenWith.exe,
    // the chooser Explorer puts up. Taking that for the registered application
    // is what makes "nothing is registered" look like "a program is assigned",
    // so it is filtered wherever the default is asked for.
    bool IsOpenWithStub(const std::string& executable) {
        return EqualsIgnoreCase(FileNameOf(executable), "openwith.exe");
    }

    // ShellExecuteEx reports the association cases in hInstApp (an SE_ERR_*
    // code below 32, kept for compatibility with the 16-bit original) and the
    // rest through GetLastError. Both are needed: the caller's message says
    // what actually went wrong instead of "could not open".
    struct ShellExecuteOutcome {
        bool     ok = false;
        INT_PTR  code = 0;                     // hInstApp: SE_ERR_* on failure
        DWORD    lastError = ERROR_SUCCESS;
    };

    bool IsNoAssociationFailure(const ShellExecuteOutcome& outcome) {
        return outcome.code == SE_ERR_NOASSOC ||
               outcome.code == SE_ERR_ASSOCINCOMPLETE ||
               outcome.lastError == ERROR_NO_ASSOCIATION;
    }

    std::string ShellExecuteFailureText(const ShellExecuteOutcome& outcome) {
        const INT_PTR code = outcome.code;
        const DWORD lastError = outcome.lastError;
        switch (code) {
            case SE_ERR_FNF:            return "the file was not found";
            case SE_ERR_PNF:            return "the folder was not found";
            case SE_ERR_ACCESSDENIED:   return "access was denied";
            case SE_ERR_OOM:            return "there is not enough memory";
            case SE_ERR_SHARE:          return "another program is holding it";
            case SE_ERR_NOASSOC:
            case SE_ERR_ASSOCINCOMPLETE:
                return "no program is registered for this file type";
            case SE_ERR_DDETIMEOUT:
            case SE_ERR_DDEFAIL:
            case SE_ERR_DDEBUSY:
                return "the registered program did not answer";
            default: break;
        }
        switch (lastError) {
            case ERROR_NO_ASSOCIATION:
                return "no program is registered for this file type";
            case ERROR_FILE_NOT_FOUND:  return "the file was not found";
            case ERROR_PATH_NOT_FOUND:  return "the folder was not found";
            case ERROR_ACCESS_DENIED:   return "access was denied";
            case ERROR_CANCELLED:       return "the launch was cancelled";
            case ERROR_SUCCESS:         return "the registered program did not start";
            default: break;
        }
        return "Windows reported error " + std::to_string(lastError);
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
        // The cache survives restarts, so it also has to be expired: see
        // SweepIconCache in UltraCanvasFileAssociationsBackend.h. Once per
        // process, on the first lookup — cheap next to the shell enumeration
        // and icon extraction this same call is about to do, and off the UI
        // thread whenever the core's prewarm worker gets here first.
        static std::once_flag sweepOnce;
        std::call_once(sweepOnce, SweepIconCache, dir);
        const std::string key = HashKey(WideToUtf8(location) + "|" +
                                        std::to_string(index));
        const std::string path = dir + "\\" + key + ".png";
        std::error_code ec;
        if (fs::exists(PathFromUtf8(path), ec) && !ec) {
            StampIconCacheFile(path);   // keeps it out of the next sweep
            return path;
        }
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
        wchar_t buffer[2048] = {};
        DWORD size = static_cast<DWORD>(std::size(buffer));
        // S_FALSE means "the buffer was too small", and leaves it unwritten -
        // a success test that lets that through returns whatever was on the
        // stack.
        if (AssocQueryStringW(ASSOCF_NONE, what, extension.c_str(),
                              nullptr, buffer, &size) != S_OK)
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
    std::string defaultExecutable = AssocString(extension, ASSOCSTR_EXECUTABLE);
    std::string defaultName = AssocString(extension, ASSOCSTR_FRIENDLYAPPNAME);
    // "Registered: OpenWith.exe" is the shell saying nothing is registered.
    // Both strings describe the same chooser, so both go.
    if (IsOpenWithStub(defaultExecutable)) {
        defaultExecutable.clear();
        defaultName.clear();
    }

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
    bool haveDefault = false;
    for (size_t i = 0; i < apps.size(); ++i) {
        if (!IsDefaultHandler(apps[i], defaultExecutable, defaultName)) continue;
        apps[i].isDefault = true;
        haveDefault = true;
        if (i != 0) std::rotate(apps.begin(), apps.begin() + i,
                                apps.begin() + i + 1);
        break;
    }

    // The enumeration missed the program a double-click actually starts. That
    // happens: a ProgID with a shell\open command but no OpenWithProgids
    // registration is not a handler the enumerator reports, and an extension
    // with no handlers at all falls back to the unfiltered list - every
    // application on the machine that ever registered itself - where the real
    // default is simply not among them. The registry named it either way, so
    // it goes in front, flagged: it is what "open" will run, and it is what
    // tells a caller that this file type is assigned to a program at all.
    std::error_code ec;
    if (!haveDefault && !defaultExecutable.empty() &&
        fs::is_regular_file(PathFromUtf8(defaultExecutable), ec) && !ec) {
        FileAssociationApp app;
        app.id = defaultExecutable;
        app.name = defaultName.empty() ? FileNameOf(defaultExecutable)
                                       : defaultName;
        app.iconPath = CachedIconFile(Utf8ToWide(defaultExecutable), 0);
        app.isDefault = true;
        // It may already be somewhere further down the list under a
        // different spelling of the same executable; one entry per program.
        apps.erase(std::remove_if(apps.begin(), apps.end(),
                                  [&app](const FileAssociationApp& other) {
                                      return EqualsIgnoreCase(other.id, app.id);
                                  }),
                   apps.end());
        apps.insert(apps.begin(), std::move(app));
        if (apps.size() > kMaxCandidates) apps.resize(kMaxCandidates);
    }
    return apps;
}

namespace {

    // One ShellExecuteEx call with everything the shell expects of it: the
    // owner window of this thread (so a UAC prompt, or an error box of the
    // program that starts, comes up in front of the file manager rather than
    // behind it), a full native path, and the file's own folder as the
    // working directory.
    ShellExecuteOutcome ShellExecuteVerb(const wchar_t* verb,
                                         const std::wstring& nativeFile,
                                         bool allowUI) {
        const std::wstring directory = NativeParentDirectory(nativeFile);
        SHELLEXECUTEINFOW info = {};
        info.cbSize = sizeof(info);
        // SEE_MASK_NOASYNC: an association that still runs on DDE (plenty of
        // them do) needs the conversation finished before the call returns.
        info.fMask = SEE_MASK_NOASYNC | (allowUI ? 0u : SEE_MASK_FLAG_NO_UI);
        info.hwnd = GetActiveWindow();
        info.lpVerb = verb;
        info.lpFile = nativeFile.c_str();
        info.lpDirectory = directory.empty() ? nullptr : directory.c_str();
        info.nShow = SW_SHOWNORMAL;
        SetLastError(ERROR_SUCCESS);
        ShellExecuteOutcome outcome;
        outcome.ok = ShellExecuteExW(&info) != FALSE;
        outcome.code = reinterpret_cast<INT_PTR>(info.hInstApp);
        if (!outcome.ok) outcome.lastError = GetLastError();
        return outcome;
    }

} // namespace

bool LaunchDefault(const std::vector<std::string>& paths, std::string& outError) {
    // ShellExecuteEx runs the verb through the shell extension registered for
    // the file type, and those are COM objects: without an apartment on this
    // thread the call fails for exactly the file types that have a handler
    // beyond a plain command line. The UI thread is OLE-initialized already
    // (that comes back S_FALSE and is balanced by the matching teardown), so
    // this only adds one where the caller is a worker.
    ComScope com;

    bool allOk = true;
    for (const std::string& path : paths) {
        const std::wstring file = NativeShellPath(path);
        const ShellExecuteOutcome opened = ShellExecuteVerb(L"open", file, false);
        if (opened.ok) continue;

        // Nothing registered for this type: Explorer does not fail here, it
        // asks. The shell's own "How do you want to open this file?" chooser
        // opens the file with whatever is picked and registers that choice,
        // so the next double-click needs no chooser at all — and a chooser
        // closed without a choice is an answer, not an error to report.
        if (IsNoAssociationFailure(opened)) {
            const ShellExecuteOutcome chooser =
                    ShellExecuteVerb(L"openas", file, true);
            if (chooser.ok || chooser.lastError == ERROR_CANCELLED) continue;
        }

        allOk = false;
        if (!outError.empty()) outError += "\n";
        outError += "Could not open \"" + FileNameOf(path) + "\": " +
                    ShellExecuteFailureText(opened) + ".";
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
            // A display name, not a file-system path: the shell namespace
            // parser wants the native spelling and rejects everything else,
            // which would leave the data object — and the launch built on
            // it — empty.
            const std::wstring wide = NativeShellPath(path);
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
