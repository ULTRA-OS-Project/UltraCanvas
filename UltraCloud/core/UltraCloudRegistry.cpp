// UltraCloud/core/UltraCloudRegistry.cpp
// Version: 0.1.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#include <UltraCloud/UltraCloudProvider.h>
#include <UltraCloud/UltraCloudMemory.h>
#include <UltraCloud/UltraCloudNextcloud.h>
#include <UltraCloud/UltraCloudWebDav.h>

#include <mutex>
#include <vector>

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
    if (!GetProvider("nextcloud")) RegisterProvider(std::make_shared<NextcloudProvider>());
    if (!GetProvider("webdav"))    RegisterProvider(std::make_shared<WebDavProvider>());
    // The in-process fake is registered too so demos and tests can pick it;
    // the account dialog lists it last.
    if (!GetProvider("memory"))    RegisterProvider(std::make_shared<MemoryProvider>());
}

} // namespace UltraCloud
