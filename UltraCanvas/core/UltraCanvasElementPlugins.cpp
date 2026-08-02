// core/UltraCanvasElementPlugins.cpp
// Element plugin registry and DSO loader
// Version: 1.0.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework

#include "UltraCanvasElementPlugins.h"
#include "UltraCanvasPluginLoader.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace UltraCanvas {

namespace {

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// First whitespace-delimited word of a text definition, lowercased - the
// dispatch key for CreateFromText ("kanban ..." -> "kanban").
std::string FirstWord(const std::string& text) {
    size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    size_t end = text.find_first_of(" \t\r\n", begin);
    if (end == std::string::npos) end = text.size();
    return ToLower(text.substr(begin, end - begin));
}

void DefaultLog(int level, const char* message) {
    static const char* prefixes[] = {"info", "warning", "error"};
    const int clamped = (level < 0) ? 0 : (level > 2 ? 2 : level);
    std::fprintf(stderr, "[UltraCanvas plugins] %s: %s\n", prefixes[clamped], message);
}

struct Registry {
    std::mutex mutex;
    // Ordered so List() output is stable for pickers and tests.
    std::map<std::string, UCElementDescriptor> byTypeName;
    std::unordered_map<std::string, std::string> byKeyword;   // keyword -> typeName
    std::function<bool(const std::string&)> missingTypeResolver;

    // Loader state.
    std::vector<std::string> pluginDirectories;
    bool envDirectoriesAdded = false;
    std::unordered_map<std::string, UCPluginLibHandle> loaded;  // canonical path -> handle
    // Plugins that refused to load or had no entry symbol: remembered so a
    // later refresh does not reopen and re-log them every time.
    std::unordered_set<std::string> refused;
    std::string lastError;
};

Registry& Reg() {
    static Registry r;
    return r;
}

void HostRegisterElement(const UCElementDescriptor* descriptor) {
    if (descriptor) UltraCanvasElementRegistry::Register(*descriptor);
}

// Appends ULTRACANVAS_PLUGIN_PATH once. Caller holds the mutex.
void EnsureEnvDirectories(Registry& r) {
    if (r.envDirectoriesAdded) return;
    r.envDirectoriesAdded = true;
    const char* env = std::getenv("ULTRACANVAS_PLUGIN_PATH");
    if (!env || !*env) return;
#if defined(_WIN32) || defined(_WIN64)
    const char separator = ';';
#else
    const char separator = ':';
#endif
    std::string path = env;
    size_t begin = 0;
    while (begin <= path.size()) {
        size_t end = path.find(separator, begin);
        if (end == std::string::npos) end = path.size();
        std::string dir = path.substr(begin, end - begin);
        if (!dir.empty() &&
            std::find(r.pluginDirectories.begin(), r.pluginDirectories.end(), dir) ==
                r.pluginDirectories.end()) {
            r.pluginDirectories.push_back(dir);
        }
        begin = end + 1;
    }
}

} // namespace

// =============================================================================
// REGISTRY
// =============================================================================

void UltraCanvasElementRegistry::Register(const UCElementDescriptor& descriptor) {
    if (descriptor.typeName.empty() || !descriptor.create) {
        DefaultLog(1, "descriptor rejected: empty typeName or no create factory");
        return;
    }
    Registry& r = Reg();
    std::lock_guard<std::mutex> lock(r.mutex);
    if (r.byTypeName.count(descriptor.typeName)) {
        DefaultLog(1, ("duplicate registration ignored: " + descriptor.typeName).c_str());
        return;
    }
    r.byTypeName[descriptor.typeName] = descriptor;
    for (const std::string& keyword : descriptor.textKeywords) {
        r.byKeyword.emplace(ToLower(keyword), descriptor.typeName);
    }
}

void UltraCanvasElementRegistry::Unregister(const std::string& typeName) {
    Registry& r = Reg();
    std::lock_guard<std::mutex> lock(r.mutex);
    auto it = r.byTypeName.find(typeName);
    if (it == r.byTypeName.end()) return;
    for (const std::string& keyword : it->second.textKeywords) {
        auto kw = r.byKeyword.find(ToLower(keyword));
        if (kw != r.byKeyword.end() && kw->second == typeName) r.byKeyword.erase(kw);
    }
    r.byTypeName.erase(it);
}

namespace {

// Copies the factory out under the lock; the factory itself runs unlocked so
// an element constructor (or a plugin init re-entering Register) cannot
// deadlock the registry.
std::function<std::shared_ptr<UltraCanvasUIElement>(
        const std::string&, int, int, int, int)>
FindCreateFactory(const std::string& typeName) {
    Registry& r = Reg();
    std::lock_guard<std::mutex> lock(r.mutex);
    auto it = r.byTypeName.find(typeName);
    return (it != r.byTypeName.end()) ? it->second.create : nullptr;
}

// On a miss: the resolver if one is set, otherwise a refresh of the plugin
// directories. Returns true when a retry is worthwhile. Runs unlocked.
bool TryResolveMissingType(const std::string& typeName) {
    std::function<bool(const std::string&)> resolver;
    {
        Registry& r = Reg();
        std::lock_guard<std::mutex> lock(r.mutex);
        resolver = r.missingTypeResolver;
    }
    if (resolver) return resolver(typeName);
    return UltraCanvas_RefreshElementPlugins() > 0;
}

} // namespace

std::shared_ptr<UltraCanvasUIElement> UltraCanvasElementRegistry::Create(
        const std::string& typeName, const std::string& id,
        int x, int y, int w, int h) {
    auto factory = FindCreateFactory(typeName);
    if (!factory) {
        if (TryResolveMissingType(typeName)) factory = FindCreateFactory(typeName);
    }
    if (!factory) {
        Registry& r = Reg();
        std::lock_guard<std::mutex> lock(r.mutex);
        r.lastError = "no element registered for type '" + typeName + "'";
        return nullptr;
    }
    return factory(id, x, y, w, h);
}

std::shared_ptr<UltraCanvasUIElement> UltraCanvasElementRegistry::CreateFromText(
        const std::string& textDefinition, const std::string& id,
        int x, int y, int w, int h, std::string* error) {
    const std::string keyword = FirstWord(textDefinition);
    if (keyword.empty()) {
        if (error) *error = "empty text definition";
        return nullptr;
    }

    auto findByKeyword = [&]() -> const std::string* {
        Registry& r = Reg();
        std::lock_guard<std::mutex> lock(r.mutex);
        auto it = r.byKeyword.find(keyword);
        return (it != r.byKeyword.end()) ? &it->second : nullptr;
    };

    const std::string* typeName = findByKeyword();
    if (!typeName && TryResolveMissingType(keyword)) typeName = findByKeyword();
    if (!typeName) {
        if (error) *error = "no element handles text starting with '" + keyword + "'";
        return nullptr;
    }

    std::function<std::shared_ptr<UltraCanvasUIElement>(
            const std::string&, int, int, int, int,
            const std::string&, std::string*)> fromText;
    {
        Registry& r = Reg();
        std::lock_guard<std::mutex> lock(r.mutex);
        auto it = r.byTypeName.find(*typeName);
        if (it != r.byTypeName.end()) fromText = it->second.createFromText;
    }
    if (!fromText) {
        if (error) *error = "element '" + *typeName + "' has no text loader";
        return nullptr;
    }
    return fromText(id, x, y, w, h, textDefinition, error);
}

const UCElementDescriptor* UltraCanvasElementRegistry::Find(const std::string& typeName) {
    Registry& r = Reg();
    std::lock_guard<std::mutex> lock(r.mutex);
    auto it = r.byTypeName.find(typeName);
    // Pointer stays valid: descriptors are only removed by Unregister/Clear,
    // and std::map never relocates nodes.
    return (it != r.byTypeName.end()) ? &it->second : nullptr;
}

bool UltraCanvasElementRegistry::IsRegistered(const std::string& typeName) {
    Registry& r = Reg();
    std::lock_guard<std::mutex> lock(r.mutex);
    return r.byTypeName.count(typeName) != 0;
}

std::vector<UCElementDescriptor> UltraCanvasElementRegistry::List(UCPluginCategory category) {
    Registry& r = Reg();
    std::lock_guard<std::mutex> lock(r.mutex);
    std::vector<UCElementDescriptor> out;
    for (const auto& kv : r.byTypeName) {
        if (kv.second.category == category) out.push_back(kv.second);
    }
    return out;
}

std::vector<UCElementDescriptor> UltraCanvasElementRegistry::ListAll() {
    Registry& r = Reg();
    std::lock_guard<std::mutex> lock(r.mutex);
    std::vector<UCElementDescriptor> out;
    out.reserve(r.byTypeName.size());
    for (const auto& kv : r.byTypeName) out.push_back(kv.second);
    return out;
}

void UltraCanvasElementRegistry::SetMissingTypeResolver(
        std::function<bool(const std::string&)> resolver) {
    Registry& r = Reg();
    std::lock_guard<std::mutex> lock(r.mutex);
    r.missingTypeResolver = std::move(resolver);
}

void UltraCanvasElementRegistry::Clear() {
    Registry& r = Reg();
    std::lock_guard<std::mutex> lock(r.mutex);
    r.byTypeName.clear();
    r.byKeyword.clear();
    r.missingTypeResolver = nullptr;
    r.lastError.clear();
    // Loaded DSO handles are deliberately kept: their code may still back
    // live elements (no-unload policy, proposal §6.4).
}

// =============================================================================
// LOADER
// =============================================================================

bool UltraCanvas_AddPluginDirectory(const std::string& directory) {
    if (directory.empty()) return false;
    Registry& r = Reg();
    std::lock_guard<std::mutex> lock(r.mutex);
    if (std::find(r.pluginDirectories.begin(), r.pluginDirectories.end(), directory) ==
        r.pluginDirectories.end()) {
        r.pluginDirectories.push_back(directory);
    }
    std::error_code ec;
    return std::filesystem::is_directory(directory, ec);
}

int UltraCanvas_RefreshElementPlugins() {
    // Snapshot the directory list and the already-loaded set under the lock,
    // then load without it: plugin init calls straight back into Register.
    std::vector<std::string> directories;
    {
        Registry& r = Reg();
        std::lock_guard<std::mutex> lock(r.mutex);
        EnsureEnvDirectories(r);
        directories = r.pluginDirectories;
    }

    static UltraCanvasPluginHost host = {
        ULTRACANVAS_PLUGIN_HOST_ABI_VERSION,
#ifdef ULTRACANVAS_VERSION
        ULTRACANVAS_VERSION,
#else
        "unknown",
#endif
        ULTRACANVAS_FRAMEWORK_ABI_TAG,
        &HostRegisterElement,
        &DefaultLog,
    };

    int loadedCount = 0;
    for (const std::string& directory : directories) {
        std::error_code ec;
        std::filesystem::directory_iterator it(directory, ec);
        if (ec) continue;                       // absent directory: fine, skip
        for (const auto& entry : it) {
            if (!entry.is_regular_file(ec) || !UCIsPluginFile(entry.path())) continue;
            const std::string canonical = UCPluginCanonicalPath(entry.path());

            {
                Registry& r = Reg();
                std::lock_guard<std::mutex> lock(r.mutex);
                if (r.loaded.count(canonical) || r.refused.count(canonical)) continue;
                // Claim the slot before opening so concurrent refreshes cannot
                // double-load; replaced with the real handle below.
                r.loaded[canonical] = nullptr;
            }

            std::string openError;
            UCPluginLibHandle handle = UCPluginOpen(entry.path().string(), &openError);
            if (!handle) {
                Registry& r = Reg();
                std::lock_guard<std::mutex> lock(r.mutex);
                r.loaded.erase(canonical);
                r.lastError = canonical + ": " + openError;
                DefaultLog(2, r.lastError.c_str());
                continue;
            }

            auto init = reinterpret_cast<UltraCanvas_PluginInitFn>(
                UCPluginSymbol(handle, "UltraCanvas_PluginInit"));
            bool accepted = false;
            if (init) {
                accepted = init(&host);         // plugin's own fence catches throws
            }

            Registry& r = Reg();
            std::lock_guard<std::mutex> lock(r.mutex);
            if (accepted) {
                r.loaded[canonical] = handle;   // resident for the process lifetime
                ++loadedCount;
            } else {
                r.loaded.erase(canonical);
                r.refused.insert(canonical);
                r.lastError = canonical + (init ? ": plugin refused to load"
                                                : ": no UltraCanvas_PluginInit symbol");
                DefaultLog(1, r.lastError.c_str());
                // Nothing registered (the contract requires refusal before
                // registration), so closing is safe.
                UCPluginClose(handle);
            }
        }
    }
    return loadedCount;
}

std::string UltraCanvas_GetLastPluginError() {
    Registry& r = Reg();
    std::lock_guard<std::mutex> lock(r.mutex);
    return r.lastError;
}

} // namespace UltraCanvas
