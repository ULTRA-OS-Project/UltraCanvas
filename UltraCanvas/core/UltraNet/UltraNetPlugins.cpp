// core/UltraNet/UltraNetPlugins.cpp
// Plugin registry. Plug-ins register themselves via UltraNet_RegisterPlugin
// at static-init time (or at runtime via the future RefreshPlugins loader).
// The registry maintains two indexes: by plug-in name and by URL scheme.
// Version: 0.3.0 (Stage 3)
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraNet/UltraNetPlugins.h"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

struct Registry {
    std::mutex mutex;
    std::unordered_map<std::string, std::shared_ptr<IUltraNetPlugin>> byName;
    std::unordered_map<std::string, std::shared_ptr<IUltraNetPlugin>> byScheme;
    std::string pluginDirectory = "Plugins/UltraNet";
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
    // Reserved for dynamic loading (dlopen / LoadLibrary) — not yet wired.
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
