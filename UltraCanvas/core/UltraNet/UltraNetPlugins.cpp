// core/UltraNet/UltraNetPlugins.cpp
// Plugin registry. Plug-ins register themselves via UltraNet_RegisterPlugin
// at static-init time (or at runtime via the future RefreshPlugins loader).
// The registry maintains two indexes: by plug-in name and by URL scheme.
// Version: 0.3.1 (Stage 3)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetPlugins.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
  using PluginLibHandle = HMODULE;
  static PluginLibHandle PluginOpen(const char* path)   { return LoadLibraryA(path); }
  static void*           PluginSym (PluginLibHandle h,
                                    const char* sym)    { return reinterpret_cast<void*>(GetProcAddress(h, sym)); }
#else
  #include <dlfcn.h>
  using PluginLibHandle = void*;
  static PluginLibHandle PluginOpen(const char* path)   { return dlopen(path, RTLD_NOW | RTLD_GLOBAL); }
  static void*           PluginSym (PluginLibHandle h,
                                    const char* sym)    { return dlsym(h, sym); }
#endif

// Accept a plug-in file by extension. CMake builds our plug-ins as MODULE
// libraries, which use the .so suffix on BOTH Linux and macOS; a SHARED build
// would be .dylib on macOS. Accept both on POSIX so discovery never depends on
// the build style. (The old code hard-coded .dylib on __APPLE__ and silently
// skipped the .so files CMake actually produces there.)
static bool IsPluginFile(const std::filesystem::path& p) {
    const std::string ext = p.extension().string();
#if defined(_WIN32) || defined(_WIN64)
    return ext == ".dll";
#else
    return ext == ".so" || ext == ".dylib";
#endif
}

// Plug-in DSO contract — see UltraNetPlugins.h for the full description.
// We resolve v2 (UltraNet_PluginInit) first, fall back to v1
// (UltraNet_PluginRegister) for backward compatibility with POSIX plug-ins
// built before the host-vtable contract.
using UltraNet_PluginRegisterFn = void (*)();   // v1 (POSIX-only)
static constexpr const char* kPluginEntryV1 = "UltraNet_PluginRegister";
static constexpr const char* kPluginEntryV2 = "UltraNet_PluginInit";

// Host vtable handed to v2 plug-ins. RegisterPlugin needs a non-template
// wrapper for the function-pointer slot (UltraNet_RegisterPlugin is a free
// function, not a template, so this just takes its address).
static UltraNetPluginHost g_pluginHost = {
    ULTRANET_PLUGIN_HOST_ABI_VERSION,
    &UltraNet_RegisterPlugin
};

namespace {

struct Registry {
    std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<IUltraNetPlugin>> byName;
    std::unordered_map<std::string, std::shared_ptr<IUltraNetPlugin>> byScheme;
    std::string pluginDirectory = "Plugins/UltraNet";

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
    auto it = std::filesystem::directory_iterator(dir, ec);
    if (ec) return;

    for (const auto& entry : it) {
        if (!entry.is_regular_file(ec) || ec) continue;
        const auto& path = entry.path();
        if (!IsPluginFile(path)) continue;

        const std::string canonical =
            std::filesystem::weakly_canonical(path, ec).string();
        if (ec || canonical.empty()) continue;

        {
            std::lock_guard<std::mutex> lk(r.mutex);
            if (r.loaded.count(canonical)) continue;   // already loaded
        }
        PluginLibHandle h = PluginOpen(canonical.c_str());
        if (!h) continue;

        // Prefer the v2 entry point (host-vtable injection — works on
        // Windows too); fall back to v1 (POSIX-only symbol resolution).
        auto init = reinterpret_cast<UltraNet_PluginInitFn>(
            PluginSym(h, kPluginEntryV2));
        UltraNet_PluginRegisterFn reg = nullptr;
        if (!init) {
            reg = reinterpret_cast<UltraNet_PluginRegisterFn>(
                PluginSym(h, kPluginEntryV1));
        }
        if (!init && !reg) continue;   // leave lib loaded; later refresh may need it

        {
            std::lock_guard<std::mutex> lk(r.mutex);
            r.loaded[canonical] = h;
        }
        if (init) init(&g_pluginHost);
        else      reg();
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
