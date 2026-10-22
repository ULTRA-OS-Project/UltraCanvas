// core/UltraNet/UltraNetPlugins.cpp
// Plugin registry. Plug-ins register through UltraNet_RegisterPlugin - built-in
// ones directly, DSOs through the host table UltraNet_RefreshPlugins hands to
// their UltraNet_PluginInit. The registry maintains two indexes: by plug-in
// name and by URL scheme.
// Version: 0.5.1 - plug-ins are loaded RTLD_LOCAL. 0.5.0: only
//                  UltraNet_PluginInit is loaded (the POSIX-only v1 entry is
//                  gone). 0.4.0: host ABI 2 - the core functions a plug-in
//                  calls travel in the host table.
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetPlugins.h"
#include "UltraNet/UltraNetCore.h"
#include "UltraNet/UltraNetHttp.h"
#include "UltraNet/UltraNetMime.h"
#include "UltraNet/UltraNetSse.h"
#include "UltraNet/UltraNetUrl.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

#if defined(_WIN32) || defined(_WIN64)
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  using PluginLibHandle = HMODULE;
  // The path is UTF-8; LoadLibraryA would read it in the ANSI code page.
  static PluginLibHandle PluginOpen(const char* path)   {
      return LoadLibraryW(UltraCanvas::PathFromUtf8(path).c_str());
  }
  static void*           PluginSym (PluginLibHandle h,
                                    const char* sym)    { return reinterpret_cast<void*>(GetProcAddress(h, sym)); }
#else
  #include <dlfcn.h>
  #if defined(__APPLE__)
    #include <mach-o/dyld.h>
  #endif
  using PluginLibHandle = void*;
  // RTLD_LOCAL: a plug-in takes nothing from the host's symbol table any more
  // (everything comes through the host table), so nothing it exports needs to
  // join the process-wide scope - where it would bind the symbols of every
  // library loaded after it, another plug-in's included (two plug-ins built
  // from the same helper source export the same names). The plug-in still
  // resolves its own references against the host first, so the type
  // information behind the host's dynamic_cast is unaffected.
  static PluginLibHandle PluginOpen(const char* path)   { return dlopen(path, RTLD_NOW | RTLD_LOCAL); }
  static void*           PluginSym (PluginLibHandle h,
                                    const char* sym)    { return dlsym(h, sym); }
#endif

// Accept a plug-in file by extension. CMake builds our plug-ins as MODULE
// libraries, which use the .so suffix on BOTH Linux and macOS; a SHARED build
// would be .dylib on macOS. Accept both on POSIX so discovery never depends on
// the build style. (The old code hard-coded .dylib on __APPLE__ and silently
// skipped the .so files CMake actually produces there.)
static bool IsPluginFile(const std::filesystem::path& p) {
    const std::string ext = PathToUtf8(p.extension());
#if defined(_WIN32) || defined(_WIN64)
    return ext == ".dll";
#else
    return ext == ".so" || ext == ".dylib";
#endif
}

// The running executable's folder, or an empty path where the platform does
// not say. UltraNet does not link the UltraCanvas core, so it asks the system
// itself, as UltraNetHttpEasy.cpp does for the CA bundle beside the app.
static std::filesystem::path ExecutableFolder() {
#if defined(_WIN32) || defined(_WIN64)
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    buffer.resize(length);
    return std::filesystem::path(buffer).parent_path();   // path-string-ok: a wide string
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (size == 0 || _NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    buffer.resize(std::char_traits<char>::length(buffer.c_str()));
    return PathFromUtf8(buffer).parent_path();
#elif defined(__linux__)
    std::error_code ec;
    const std::filesystem::path exe = std::filesystem::read_symlink("/proc/self/exe", ec);
    return ec ? std::filesystem::path() : exe.parent_path();
#else
    return {};
#endif
}

// Where plug-ins are looked for until UltraNet_SetPluginDirectory() names
// another folder: Plugins/UltraNet beside the executable, or one or two
// folders above it (the build tree has the executable at <build>/, an
// installed one may sit in bin/), the first of those that holds a plug-in;
// else the one beside the executable. Only with no executable path at all is
// it the folder of that name in the working directory - never first, because
// the working directory is wherever the application was started from: System32
// for a packaged Windows application (MSIX), any folder for one started from
// a shortcut or by another program. It used to be that relative path alone,
// so every application that did not set a folder of its own - DeviceExplorer's
// printer and scanner discovery, which loads the mDNS plug-in - found no
// plug-in unless it happened to be started in its own folder. UltraMail and
// EmailCleaner resolved it themselves the same way.
static std::string DefaultPluginDirectory() {
    namespace fs = std::filesystem;
    const fs::path relative = PathFromUtf8("Plugins") / "UltraNet";
    const fs::path exeFolder = ExecutableFolder();
    if (exeFolder.empty()) return PathToUtf8(relative);

    auto holdsPlugin = [](const fs::path& folder) {
        std::error_code ec;
        if (!fs::is_directory(folder, ec) || ec) return false;
        for (const auto& entry : fs::directory_iterator(folder, ec)) {
            if (ec) return false;
            if (IsPluginFile(entry.path())) return true;
        }
        return false;
    };
    fs::path base = exeFolder;
    for (int up = 0; up < 3; ++up) {
        if (holdsPlugin(base / relative)) return PathToUtf8((base / relative).lexically_normal());
        if (!base.has_parent_path() || base.parent_path() == base) break;
        base = base.parent_path();
    }
    return PathToUtf8((exeFolder / relative).lexically_normal());
}

// Plug-in DSO contract — see UltraNetPlugins.h for the full description. The
// one entry point is UltraNet_PluginInit(host). The v1 entry
// (UltraNet_PluginRegister, which resolved UltraNet_RegisterPlugin from the
// host's symbol table) is no longer loaded: it only ever worked on POSIX, and
// only when the host happened to carry every core function the plug-in called.
static constexpr const char* kPluginEntry = "UltraNet_PluginInit";

// The table handed to every v2 plug-in (see UltraNetPlugins.h). Everything a
// plug-in needs from the core goes through it, so a plug-in DSO has no
// undefined core symbols. Because the loader passes this table's address,
// every host that loads plug-ins also links every function named in it -
// which is what makes plug-ins load into an app on a static core.
namespace {
void HttpHeadersSet(UltraNetHttpHeaders& headers, const std::string& name,
                    const std::string& value) {
    headers.Set(name, value);
}
} // namespace

static const UltraNetPluginHost g_pluginHost = {
    ULTRANET_PLUGIN_HOST_ABI_VERSION,
    &UltraNet_RegisterPlugin,
    &UltraNet_ParseUrl,
    &UltraNet_UrlEncode,
    &UltraNet_UrlDecode,
    &UltraNet_ResolveCaBundlePath,
    &UltraNet_DescribeTrustRoots,
    &UltraNet_DescribePlatform,
    &UltraNet_MimeBuild,
    &UltraNet_HttpGet,
    &UltraNet_HttpRequest,
    &HttpHeadersSet,
    // ABI 3
    &UltraNet_MimeBuildMailMessage,
    &UltraNet_SseStreamAsync,
    &UltraNet_CancelRequest,
    &UltraNet_IsRequestActive,
};

const UltraNetPluginHost* UltraNet_GetPluginHost() {
    return &g_pluginHost;
}

namespace {

struct Registry {
    std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<IUltraNetPlugin>> byName;
    std::unordered_map<std::string, std::shared_ptr<IUltraNetPlugin>> byScheme;
    std::string pluginDirectory = DefaultPluginDirectory();

    // Tracks plugin libraries we've already dlopen'd so RefreshPlugins()
    // is idempotent. Keyed by the canonical filesystem path of the DSO.
    std::unordered_map<std::string, PluginLibHandle> loaded;
};

Registry& Reg() {
    static Registry r;
    return r;
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Schemes implemented in the UltraNet core (no plugin needed). Kept in sync
// with the protocols this module actually speaks today.
const std::vector<std::string> kCoreSchemes = {
    "http", "https", "ws", "wss",
    "ftp", "ftps", "sftp",
    "tcp", "udp", "tls", "dns"
};

} // namespace

void UltraNet_RegisterPlugin(std::shared_ptr<IUltraNetPlugin> plugin) {
    if (!plugin) return;
    Registry& r = Reg();
    const std::string name = plugin->GetName();
    if (name.empty()) return;

    std::lock_guard<std::mutex> lk(r.mutex);
    r.byName[name] = plugin;
    for (const auto& s : plugin->GetSupportedSchemes()) {
        r.byScheme[ToLower(s)] = plugin;
    }
}

void UltraNet_UnregisterPlugin(const std::string& pluginName) {
    Registry& r = Reg();
    std::shared_ptr<IUltraNetPlugin> p;
    {
        std::lock_guard<std::mutex> lk(r.mutex);
        auto it = r.byName.find(pluginName);
        if (it == r.byName.end()) return;
        p = it->second;
        r.byName.erase(it);
        // Drop every scheme entry that points at this plugin.
        for (auto sit = r.byScheme.begin(); sit != r.byScheme.end(); ) {
            if (sit->second == p) sit = r.byScheme.erase(sit);
            else ++sit;
        }
    }
    if (p) p->Shutdown();
}

std::shared_ptr<IUltraNetPlugin> UltraNet_GetPlugin(const std::string& scheme) {
    Registry& r = Reg();
    std::lock_guard<std::mutex> lk(r.mutex);
    auto it = r.byScheme.find(ToLower(scheme));
    return it == r.byScheme.end() ? nullptr : it->second;
}

std::vector<std::shared_ptr<IUltraNetPlugin>> UltraNet_GetAllPlugins() {
    Registry& r = Reg();
    std::lock_guard<std::mutex> lk(r.mutex);
    std::vector<std::shared_ptr<IUltraNetPlugin>> out;
    out.reserve(r.byName.size());
    for (const auto& [_, p] : r.byName) out.push_back(p);
    return out;
}

void UltraNet_RefreshPlugins() {
    Registry& r = Reg();

    std::string dir;
    {
        std::lock_guard<std::mutex> lk(r.mutex);
        dir = r.pluginDirectory;
    }
    if (dir.empty()) return;

    std::error_code ec;
    auto it = std::filesystem::directory_iterator(UltraCanvas::PathFromUtf8(dir), ec);
    if (ec) return;

    for (const auto& entry : it) {
        if (!entry.is_regular_file(ec) || ec) continue;
        const auto& path = entry.path();
        if (!IsPluginFile(path)) continue;

        const std::string canonical =
            PathToUtf8(std::filesystem::weakly_canonical(UltraCanvas::PathFromUtf8(path), ec));
        if (ec || canonical.empty()) continue;

        {
            std::lock_guard<std::mutex> lk(r.mutex);
            if (r.loaded.count(canonical)) continue;   // already loaded
        }
        PluginLibHandle h = PluginOpen(canonical.c_str());
        if (!h) continue;

        // The host-table entry point, the same on every platform. A library
        // without it is not an UltraNet plug-in (a v1-only one is refused).
        auto init = reinterpret_cast<UltraNet_PluginInitFn>(PluginSym(h, kPluginEntry));
        if (!init) continue;   // leave lib loaded; later refresh may need it

        {
            std::lock_guard<std::mutex> lk(r.mutex);
            r.loaded[canonical] = h;
        }
        init(&g_pluginHost);
    }
}

std::string UltraNet_GetPluginDirectory() {
    Registry& r = Reg();
    std::lock_guard<std::mutex> lk(r.mutex);
    return r.pluginDirectory;
}

void UltraNet_SetPluginDirectory(const std::string& path) {
    Registry& r = Reg();
    std::lock_guard<std::mutex> lk(r.mutex);
    r.pluginDirectory = path;
}

std::vector<std::string> UltraNet_GetSupportedSchemes() {
    std::vector<std::string> out = kCoreSchemes;
    Registry& r = Reg();
    {
        std::lock_guard<std::mutex> lk(r.mutex);
        for (const auto& [scheme, _] : r.byScheme) out.push_back(scheme);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}
