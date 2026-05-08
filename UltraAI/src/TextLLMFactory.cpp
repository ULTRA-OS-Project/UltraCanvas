// UltraAI/src/TextLLMFactory.cpp
// Implementation of UltraAI::CreateTextLLM and UltraAI::ListTextLLMProviders.
// Adapters self-register via UltraAI::RegisterTextLLMProvider; the mock
// adapter installs itself the first time CreateTextLLM is called so callers
// always have a baseline provider available.
// Version: 0.1.0
// Last Modified: 2026-05-08
// Author: UltraAI Module

#include "UltraAITextLLM.h"

#ifdef ULTRAAI_HAS_MOCK_ADAPTER
#include "UltraAIMockTextLLM.h"
#endif

#include <map>
#include <mutex>

namespace UltraAI {

namespace {

using TextLLMFactoryFn =
    std::function<std::unique_ptr<ITextLLM>(const TextLLMConfig&, Error*)>;

struct Registry {
    std::mutex mu;
    std::map<std::string, TextLLMFactoryFn> providers;

    void EnsureBuiltins() {
#ifdef ULTRAAI_HAS_MOCK_ADAPTER
        if (providers.find("mock") == providers.end()) {
            providers["mock"] =
                [](const TextLLMConfig&, Error*) -> std::unique_ptr<ITextLLM> {
                    return CreateMockTextLLM();
                };
        }
#endif
    }
};

Registry& GetRegistry() {
    static Registry r;
    return r;
}

} // namespace

bool RegisterTextLLMProvider(
    const std::string& providerId,
    std::function<std::unique_ptr<ITextLLM>(const TextLLMConfig&, Error*)> factory) {
    auto& r = GetRegistry();
    std::lock_guard<std::mutex> lock(r.mu);
    r.providers[providerId] = std::move(factory);
    return true;
}

std::unique_ptr<ITextLLM> CreateTextLLM(const TextLLMConfig& config,
                                        Error* outError) {
    auto& r = GetRegistry();
    std::lock_guard<std::mutex> lock(r.mu);
    r.EnsureBuiltins();

    std::string id = config.providerId;
    if (id.empty()) {
        // Default route: prefer "mock" when no providers are configured;
        // a real UltraOS deployment will install a real default here.
        if (r.providers.count("mock")) id = "mock";
    }

    auto it = r.providers.find(id);
    if (it == r.providers.end()) {
        if (outError) {
            outError->code    = ErrorCode::ModelNotFound;
            outError->message = "No TextLLM provider registered for id '" + id + "'";
        }
        return nullptr;
    }

    Error tmp;
    Error* err = outError ? outError : &tmp;
    auto instance = it->second(config, err);
    if (!instance && err->code == ErrorCode::None) {
        err->code    = ErrorCode::ProviderError;
        err->message = "Provider '" + id + "' factory returned null";
    }
    return instance;
}

std::vector<std::string> ListTextLLMProviders() {
    auto& r = GetRegistry();
    std::lock_guard<std::mutex> lock(r.mu);
    r.EnsureBuiltins();

    std::vector<std::string> ids;
    ids.reserve(r.providers.size());
    for (const auto& kv : r.providers) ids.push_back(kv.first);
    return ids;
}

} // namespace UltraAI
