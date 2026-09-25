// UltraAI/src/Routing.cpp
// Implementation of the default-provider routing policy.
// Version: 0.2.0
// Last Modified: 2026-09-24
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
bool g_cloudFallback = false;

bool Contains(const std::vector<std::string>& registered,
              const std::string& id) {
    return !id.empty() &&
           std::find(registered.begin(), registered.end(), id) !=
               registered.end();
}

// Known local providers per capability, in preference order. "Local" means
// the model runs on this machine: in-process (llama-cpp) or behind a server
// the user runs (qwen against Ollama/vLLM, comfyui). In-process adapters
// rank first — they need nothing started beforehand.
std::vector<std::string> KnownLocalProviders(const std::string& capability) {
    if (capability == "textllm")      return {"llama-cpp", "qwen"};
    if (capability == "embeddings")   return {"llama-cpp", "qwen"};
    if (capability == "speechtotext") return {"whisper-cpp"};
    if (capability == "texttospeech") return {"piper"};
    if (capability == "imagegen")     return {"comfyui", "stable-diffusion-cpp"};
    if (capability == "videogen")     return {"comfyui"};
    return {};
}

bool EnvCloudFallback() {
    const char* value = std::getenv("ULTRAAI_ALLOW_CLOUD_FALLBACK");
    if (!value) return false;
    std::string v;
    for (const char* c = value; *c; ++c) {
        v += static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
    }
    return v == "1" || v == "true" || v == "yes" || v == "on";
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

void SetCloudFallbackAllowed(bool allowed) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cloudFallback = allowed;
}

bool IsCloudFallbackAllowed() {
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (g_cloudFallback) return true;
    }
    return EnvCloudFallback();
}

bool IsLocalProvider(const std::string& providerId) {
    static const char* kLocal[] = {"llama-cpp", "qwen", "whisper-cpp", "piper",
                                   "comfyui", "stable-diffusion-cpp"};
    for (const char* id : kLocal) {
        if (providerId == id) return true;
    }
    return false;
}

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
    // 3. Local-first: the capability's known local providers in preference
    // order, then any other local provider, sorted.
    for (const std::string& local : KnownLocalProviders(capability)) {
        if (Contains(registered, local)) append(local);
    }
    std::vector<std::string> sorted = registered;
    std::sort(sorted.begin(), sorted.end());
    for (const std::string& id : sorted) {
        if (IsLocalProvider(id)) append(id);
    }
    // 4. Cloud providers — only when the fallback is allowed.
    if (IsCloudFallbackAllowed()) {
        for (const std::string& id : sorted) {
            if (id != "mock" && !IsLocalProvider(id)) append(id);
        }
    }
    // 5. The mock last.
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
