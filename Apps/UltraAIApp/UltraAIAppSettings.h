// Apps/UltraAIApp/UltraAIAppSettings.h
// Persistent application settings for the UltraAI dashboard, following the
// UltraFiler config pattern: a simple key=value file in the platform config
// directory, beside endpoints.json (~/.config/UltraAI/config.ini on Linux,
// %APPDATA%\UltraAI\config.ini on Windows,
// ~/Library/Application Support/UltraAI/config.ini on macOS). The settings
// window applies every change live and saves it straight away.
//
// What is stored is the routing policy UltraAI's "(default route)" follows:
// whether it may fall back to cloud (paid, online) services, and an optional
// default provider per service. Out of the box every service routes locally.
// Version: 0.1.0
// Last Modified: 2026-09-24
// Author: UltraAI Module
#pragma once

#include "UltraAIEndpoints.h"   // AICapability

#include <map>
#include <string>
#include <vector>

namespace UltraAIApp {

// UltraAI's routing name for a dashboard capability ("textllm",
// "texttospeech", ...), as UltraAIRouting.h and SetDefaultProvider use it.
const char* RoutingCapabilityName(AICapability cap);

// The providers registered for a capability in this build.
std::vector<std::string> ProvidersFor(AICapability cap);

// "local", "cloud" or "test double" — how a provider id is shown.
std::string ProviderKind(const std::string& providerId);

// What an empty providerId resolves to for `cap` under the current routing
// policy; empty when nothing is routable.
std::string ResolvedDefaultProvider(AICapability cap);

class UltraAIAppSettings {
public:
    // Services > Local & cloud: may the default route use a cloud service
    // when no local provider is available? Off: local AI is the default, and
    // a paid service is only used when it is picked explicitly.
    bool allowCloudFallback = false;

    // Services > Default providers: the provider each service's default
    // route starts with, by capability serialization id ("chat", "tts",
    // ...). Absent or empty means "Automatic — local first".
    std::map<std::string, std::string> defaultProviders;

    static UltraAIAppSettings& Instance();

    std::string DefaultProviderFor(AICapability cap) const;
    void SetDefaultProviderFor(AICapability cap, const std::string& providerId);

    // Hand the settings to UltraAI's routing (SetCloudFallbackAllowed,
    // SetDefaultProvider). Called at startup and after every change.
    void Apply() const;

    // Load() on a missing file keeps the defaults and returns true.
    bool Load();
    bool Save() const;

    static std::string ConfigPath();   // EndpointStore::ConfigDir() + config.ini

private:
    UltraAIAppSettings() = default;
};

} // namespace UltraAIApp
