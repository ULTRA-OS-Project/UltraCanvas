// UltraAI/src/TextLLMFactory.cpp
// Implementation of UltraAI::CreateTextLLM and UltraAI::ListTextLLMProviders.
// Adapters self-register via UltraAI::RegisterTextLLMProvider; the mock
// adapter installs itself the first time CreateTextLLM is called so callers
// always have a baseline provider available.
// Version: 0.1.0
// Last Modified: 2026-05-08
// Author: UltraAI Module

#include "UltraAITextLLM.h"
#include "UltraAIRouting.h"

#ifdef ULTRAAI_HAS_MOCK_ADAPTER
#include "UltraAIMockTextLLM.h"
#endif
#ifdef ULTRAAI_HAS_ANTHROPIC_ADAPTER
#include "UltraAIAnthropicTextLLM.h"
#endif
#ifdef ULTRAAI_HAS_OPENAI_ADAPTER
#include "UltraAIOpenAI.h"
#endif
#ifdef ULTRAAI_HAS_LLAMACPP_ADAPTER
#include "UltraAILlamaTextLLM.h"
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
#ifdef ULTRAAI_HAS_ANTHROPIC_ADAPTER
        if (providers.find("anthropic") == providers.end()) {
            providers["anthropic"] =
                [](const TextLLMConfig& cfg, Error* err) {
                    return CreateAnthropicTextLLM(cfg, err);
                };
        }
#endif
#ifdef ULTRAAI_HAS_OPENAI_ADAPTER
        if (providers.find("openai") == providers.end()) {
            providers["openai"] =
                [](const TextLLMConfig& cfg, Error* err) {
                    return CreateOpenAITextLLM(cfg, err);
                };
        }
#endif
#ifdef ULTRAAI_HAS_LLAMACPP_ADAPTER
        if (providers.find("llama-cpp") == providers.end()) {
            providers["llama-cpp"] =
                [](const TextLLMConfig& cfg, Error* err) {
                    return CreateLlamaTextLLM(cfg, err);
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

    Error tmp;
    Error* err = outError ? outError : &tmp;

    if (!config.providerId.empty()) {
        auto it = r.providers.find(config.providerId);
        if (it == r.providers.end()) {
            err->code    = ErrorCode::ModelNotFound;
            err->message = "No TextLLM provider registered for id '" +
                           config.providerId + "'";
            return nullptr;
        }
        auto instance = it->second(config, err);
        if (!instance && err->code == ErrorCode::None) {
            err->code    = ErrorCode::ProviderError;
            err->message = "Provider '" + config.providerId +
                           "' factory returned null";
        }
        return instance;
    }

    // Default route (UltraAIRouting.h): walk the preference order —
    // explicit SetDefaultProvider, ULTRAAI_DEFAULT_TEXTLLM environment,
    // local-first, real providers, then the mock — falling through
    // providers that cannot construct in this build.
    std::vector<std::string> registered;
    registered.reserve(r.providers.size());
    for (const auto& kv : r.providers) registered.push_back(kv.first);
    for (const std::string& id : ResolveProviderOrder("textllm", registered)) {
        *err = {};
        auto instance = r.providers[id](config, err);
        if (instance) return instance;
    }
    if (err->code == ErrorCode::None) {
        err->code    = ErrorCode::ModelNotFound;
        err->message = "No TextLLM provider registered";
    }
    return nullptr;
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
