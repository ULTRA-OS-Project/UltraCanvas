// UltraAI/src/Routing.cpp
// Implementation of the default-provider routing policy.
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module

#include "UltraAIRouting.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <mutex>

namespace UltraAI {

namespace {

std::mutex g_mutex;
std::map<std::string, std::string> g_defaults;

bool Contains(const std::vector<std::string>& registered,
              const std::string& id) {
    return !id.empty() &&
           std::find(registered.begin(), registered.end(), id) !=
               registered.end();
}

// Known in-process/local providers per capability, in preference order.
std::vector<std::string> KnownLocalProviders(const std::string& capability) {
    if (capability == "textllm")      return {"llama-cpp"};
    if (capability == "embeddings")   return {"llama-cpp"};
    if (capability == "speechtotext") return {"whisper-cpp"};
    if (capability == "texttospeech") return {"piper"};
    if (capability == "imagegen")     return {"stable-diffusion-cpp"};
    return {};
}

std::string EnvDefault(const std::string& capability) {
    std::string name = "ULTRAAI_DEFAULT_";
    for (char c : capability) {
        name += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    const char* value = std::getenv(name.c_str());
    return value ? std::string(value) : std::string();
}

} // namespace

void SetDefaultProvider(const std::string& capability,
                        const std::string& providerId) {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (providerId.empty()) {
        g_defaults.erase(capability);
    } else {
        g_defaults[capability] = providerId;
    }
}

std::string GetDefaultProvider(const std::string& capability) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_defaults.find(capability);
    return it == g_defaults.end() ? std::string() : it->second;
}

void ClearDefaultProviders() {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_defaults.clear();
}

std::vector<std::string> ResolveProviderOrder(
    const std::string& capability,
    const std::vector<std::string>& registered) {
    std::vector<std::string> order;
    auto append = [&order](const std::string& id) {
        if (std::find(order.begin(), order.end(), id) == order.end()) {
            order.push_back(id);
        }
    };

    // 1. Explicit application / OS setting.
    if (const std::string configured = GetDefaultProvider(capability);
        Contains(registered, configured)) {
        append(configured);
    }
    // 2. Deployment configuration via environment.
    if (const std::string fromEnv = EnvDefault(capability);
        Contains(registered, fromEnv)) {
        append(fromEnv);
    }
    // 3. Local-first.
    for (const std::string& local : KnownLocalProviders(capability)) {
        if (Contains(registered, local)) append(local);
    }
    // 4. Remaining non-mock providers, in sorted order; 5. mock last.
    std::vector<std::string> sorted = registered;
    std::sort(sorted.begin(), sorted.end());
    for (const std::string& id : sorted) {
        if (id != "mock") append(id);
    }
    if (Contains(registered, "mock")) append("mock");
    return order;
}

std::string ResolveProviderId(const std::string& capability,
                              const std::vector<std::string>& registered) {
    const std::vector<std::string> order =
        ResolveProviderOrder(capability, registered);
    return order.empty() ? std::string() : order.front();
}

} // namespace UltraAI
