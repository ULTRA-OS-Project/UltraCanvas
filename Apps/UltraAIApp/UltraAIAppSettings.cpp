// Apps/UltraAIApp/UltraAIAppSettings.cpp
// Version: 0.1.0
// Last Modified: 2026-09-24

#include "UltraAIAppSettings.h"

#include "UltraAI.h"
#include "UltraAIRouting.h"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace UltraAIApp {

using namespace UltraAI;

namespace {

std::string Trim(const std::string& s) {
    const size_t first = s.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    const size_t last = s.find_last_not_of(" \t");
    return s.substr(first, last - first + 1);
}

constexpr const char* kCloudFallbackKey  = "routing.cloud.fallback";
constexpr const char* kDefaultKeyPrefix  = "routing.default.";

} // namespace

const char* RoutingCapabilityName(AICapability cap) {
    switch (cap) {
        case AICapability::Chat:         return "textllm";
        case AICapability::Embeddings:   return "embeddings";
        case AICapability::SpeechToText: return "speechtotext";
        case AICapability::TextToSpeech: return "texttospeech";
        case AICapability::Translation:  return "translator";
        case AICapability::ImageGen:     return "imagegen";
        case AICapability::Vision:       return "visionanalyzer";
        case AICapability::VideoGen:     return "videogen";
        case AICapability::MusicGen:     return "musicgen";
        case AICapability::CodeAssist:   return "codeassist";
    }
    return "";
}

std::vector<std::string> ProvidersFor(AICapability cap) {
    switch (cap) {
        case AICapability::Chat:         return ListTextLLMProviders();
        case AICapability::Embeddings:   return ListEmbeddingsProviders();
        case AICapability::SpeechToText: return ListSpeechToTextProviders();
        case AICapability::TextToSpeech: return ListTextToSpeechProviders();
        case AICapability::Translation:  return ListTranslatorProviders();
        case AICapability::ImageGen:     return ListImageGenProviders();
        case AICapability::Vision:       return ListVisionAnalyzerProviders();
        case AICapability::VideoGen:     return ListVideoGenProviders();
        case AICapability::MusicGen:     return ListMusicGenProviders();
        case AICapability::CodeAssist:   return ListCodeAssistProviders();
    }
    return {};
}

std::string ProviderKind(const std::string& providerId) {
    if (providerId == "mock") return "test double";
    return IsLocalProvider(providerId) ? "local" : "cloud";
}

std::string ResolvedDefaultProvider(AICapability cap) {
    return ResolveProviderId(RoutingCapabilityName(cap), ProvidersFor(cap));
}

UltraAIAppSettings& UltraAIAppSettings::Instance() {
    static UltraAIAppSettings instance;
    return instance;
}

std::string UltraAIAppSettings::DefaultProviderFor(AICapability cap) const {
    auto it = defaultProviders.find(CapabilityId(cap));
    return it == defaultProviders.end() ? std::string() : it->second;
}

void UltraAIAppSettings::SetDefaultProviderFor(AICapability cap,
                                               const std::string& providerId) {
    if (providerId.empty()) {
        defaultProviders.erase(CapabilityId(cap));
    } else {
        defaultProviders[CapabilityId(cap)] = providerId;
    }
}

void UltraAIAppSettings::Apply() const {
    SetCloudFallbackAllowed(allowCloudFallback);
    for (const auto& info : AllCapabilities()) {
        SetDefaultProvider(RoutingCapabilityName(info.cap),
                           DefaultProviderFor(info.cap));
    }
}

std::string UltraAIAppSettings::ConfigPath() {
#if defined(_WIN32)
    return EndpointStore::ConfigDir() + "\\config.ini";
#else
    return EndpointStore::ConfigDir() + "/config.ini";
#endif
}

bool UltraAIAppSettings::Load() {
    std::ifstream file(ConfigPath());
    if (!file.is_open()) return true;   // no file yet: the defaults stand

    std::map<std::string, std::string> kv;
    std::string line;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv[Trim(line.substr(0, eq))] = Trim(line.substr(eq + 1));
    }

    if (auto it = kv.find(kCloudFallbackKey); it != kv.end()) {
        allowCloudFallback =
            it->second == "true" || it->second == "1" || it->second == "yes";
    }
    defaultProviders.clear();
    for (const auto& info : AllCapabilities()) {
        auto it = kv.find(std::string(kDefaultKeyPrefix) + info.id);
        if (it != kv.end() && !it->second.empty() && it->second != "auto") {
            defaultProviders[info.id] = it->second;
        }
    }
    return true;
}

bool UltraAIAppSettings::Save() const {
    std::error_code ec;
    std::filesystem::create_directories(EndpointStore::ConfigDir(), ec);
    if (ec) return false;

    std::ofstream file(ConfigPath());
    if (!file.is_open()) return false;

    file << "# UltraAI Configuration\n\n";
    file << kCloudFallbackKey << " = "
         << (allowCloudFallback ? "true" : "false") << "\n";
    for (const auto& info : AllCapabilities()) {
        const std::string provider = DefaultProviderFor(info.cap);
        file << kDefaultKeyPrefix << info.id << " = "
             << (provider.empty() ? "auto" : provider) << "\n";
    }
    return file.good();
}

} // namespace UltraAIApp
