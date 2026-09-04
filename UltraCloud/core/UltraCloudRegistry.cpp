// UltraCloud/core/UltraCloudRegistry.cpp
// Version: 0.1.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#include <UltraCloud/UltraCloudProvider.h>
#include <UltraCloud/UltraCloudMemory.h>
#include <UltraCloud/UltraCloudNextcloud.h>
#include <UltraCloud/UltraCloudWebDav.h>

#include <UltraCloud/UltraCloudDropbox.h>
#include <UltraCloud/UltraCloudGoogleDrive.h>
#include <UltraCloud/UltraCloudOneDrive.h>

#include <UltraCanvasUtils.h>   // GetExecutableDir

#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace UltraCloud {

namespace {
std::mutex& RegistryMutex() { static std::mutex m; return m; }
std::vector<std::shared_ptr<ICloudProvider>>& Providers() {
    static std::vector<std::shared_ptr<ICloudProvider>> v;
    return v;
}
} // namespace

void RegisterProvider(std::shared_ptr<ICloudProvider> provider) {
    if (!provider) return;
    std::lock_guard<std::mutex> lock(RegistryMutex());
    for (auto& p : Providers())
        if (p->Id() == provider->Id()) { p = std::move(provider); return; }
    Providers().push_back(std::move(provider));
}

std::shared_ptr<ICloudProvider> GetProvider(const std::string& providerId) {
    std::lock_guard<std::mutex> lock(RegistryMutex());
    for (const auto& p : Providers())
        if (p->Id() == providerId) return p;
    return nullptr;
}

std::vector<std::shared_ptr<ICloudProvider>> ListProviders() {
    std::lock_guard<std::mutex> lock(RegistryMutex());
    return Providers();
}

void RegisterBuiltInProviders() {
    if (!GetProvider("nextcloud"))   RegisterProvider(std::make_shared<NextcloudProvider>());
    if (!GetProvider("webdav"))      RegisterProvider(std::make_shared<WebDavProvider>());
    if (!GetProvider("dropbox"))     RegisterProvider(std::make_shared<DropboxProvider>());
    if (!GetProvider("onedrive"))    RegisterProvider(std::make_shared<OneDriveProvider>());
    if (!GetProvider("googledrive")) RegisterProvider(std::make_shared<GoogleDriveProvider>());
    // The in-process fake is registered too so demos and tests can pick it;
    // the account dialog lists it last.
    if (!GetProvider("memory"))    RegisterProvider(std::make_shared<MemoryProvider>());
}

// ---- Plug-in DSOs ------------------------------------------------------------

namespace {
std::string& PluginDir() { static std::string dir; return dir; }

bool IsPluginLibrary(const std::filesystem::path& p) {
    const std::string ext = p.extension().string();
#if defined(_WIN32)
    return ext == ".dll";
#elif defined(__APPLE__)
    return ext == ".dylib" || ext == ".so";
#else
    return ext == ".so";
#endif
}
} // namespace

std::string GetPluginDirectory() {
    if (!PluginDir().empty()) return PluginDir();
    if (const char* env = std::getenv("ULTRACLOUD_PLUGIN_DIR"); env && *env) return env;
    return UltraCanvas::GetExecutableDir() + "/plugins/ultracloud";
}

void SetPluginDirectory(const std::string& path) { PluginDir() = path; }

int LoadProviderPlugins() {
    using InitFn = void (*)(const UltraCloudPluginHost*);
    static UltraCloudPluginHost host;   // outlives every plug-in
    host.hostVersion = 1;
    host.registerProvider = [](std::shared_ptr<ICloudProvider> p) { RegisterProvider(std::move(p)); };

    std::error_code ec;
    const std::filesystem::path dir = GetPluginDirectory();
    if (!std::filesystem::is_directory(dir, ec)) return 0;

    int loaded = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file(ec) || !IsPluginLibrary(entry.path())) continue;
        const std::size_t before = ListProviders().size();
#if defined(_WIN32)
        HMODULE lib = LoadLibraryA(entry.path().string().c_str());
        if (!lib) continue;
        auto init = reinterpret_cast<InitFn>(GetProcAddress(lib, "UltraCloud_PluginInit"));
#else
        void* lib = dlopen(entry.path().string().c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!lib) continue;
        auto init = reinterpret_cast<InitFn>(dlsym(lib, "UltraCloud_PluginInit"));
#endif
        if (!init) continue;   // the library stays loaded; nothing to unload safely
        init(&host);
        if (ListProviders().size() > before) ++loaded;
    }
    return loaded;
}

} // namespace UltraCloud
