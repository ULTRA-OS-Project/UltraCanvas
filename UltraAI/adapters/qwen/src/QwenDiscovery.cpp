// UltraAI/adapters/qwen/src/QwenDiscovery.cpp
// Implementation of local-endpoint discovery for the Qwen adapter.
// Version: 0.1.0
// Last Modified: 2026-08-24
// Author: UltraAI Module

#include "QwenDiscovery.h"

#include "UltraAIQwen.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <utility>

namespace UltraAI {

std::vector<std::string> QwenDefaultEndpoints() {
    // Ordered by how likely a desktop has them running. Every entry speaks
    // the OpenAI-compatible surface at {endpoint}/v1.
    return {
        "http://localhost:11434",   // Ollama
        "http://localhost:8000",    // vLLM
        "http://localhost:8080",    // llama.cpp server
        "http://localhost:1234",    // LM Studio
    };
}

namespace qwen_detail {

namespace {

using nlohmann::json;

std::string Lowered(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    return value;
}

std::string TrimTrailingSlashes(std::string value) {
    while (!value.empty() && value.back() == '/') value.pop_back();
    return value;
}

// GET {base}/v1/models. Returns false when the endpoint did not answer with
// a usable model list.
bool FetchModels(const std::string& baseUrl, const std::string& apiKey,
                 const std::shared_ptr<ITransport>& transport, int timeoutMs,
                 std::vector<ServedModel>& outModels) {
    TransportRequest net;
    net.method    = "GET";
    net.url       = baseUrl + "/v1/models";
    net.timeoutMs = timeoutMs;
    if (!apiKey.empty()) {
        net.headers.push_back({"authorization", "Bearer " + apiKey});
    }

    Error transportError;
    TransportResponse resp = transport->Request(net, &transportError);
    if (!transportError.IsOk() || resp.statusCode >= 400) return false;

    json body = json::parse(resp.body, nullptr, /*allow_exceptions=*/false);
    if (body.is_discarded() || !body.is_object() ||
        !body.contains("data") || !body["data"].is_array()) {
        return false;
    }

    for (const auto& entry : body["data"]) {
        if (!entry.is_object()) continue;
        const std::string id = entry.value("id", "");
        if (id.empty()) continue;
        const std::string lowered = Lowered(id);
        ServedModel model;
        model.id                 = id;
        model.looksLikeQwen      = lowered.find("qwen") != std::string::npos;
        model.looksLikeEmbedding = lowered.find("embed") != std::string::npos;
        outModels.push_back(std::move(model));
    }
    return true;
}

} // namespace

std::string PickDefaultModel(const std::vector<ServedModel>& models,
                             bool preferEmbedding) {
    // Best match first: a Qwen model of the right kind, then any model of
    // the right kind, then any Qwen model, then anything at all.
    const ServedModel* qwenOfKind = nullptr;
    const ServedModel* anyOfKind  = nullptr;
    const ServedModel* anyQwen    = nullptr;

    for (const auto& model : models) {
        const bool rightKind = model.looksLikeEmbedding == preferEmbedding;
        if (rightKind && model.looksLikeQwen && !qwenOfKind) qwenOfKind = &model;
        if (rightKind && !anyOfKind)                         anyOfKind  = &model;
        if (model.looksLikeQwen && !anyQwen)                 anyQwen    = &model;
    }
    if (qwenOfKind) return qwenOfKind->id;
    if (anyOfKind)  return anyOfKind->id;
    if (anyQwen)    return anyQwen->id;
    return models.empty() ? std::string() : models.front().id;
}

bool DiscoveryCache::Resolve(const std::string& explicitBaseUrl,
                             const std::string& apiKey,
                             const std::shared_ptr<ITransport>& transport,
                             int timeoutMs, Discovery& out, Error* outError) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!resolved_) {
        resolved_ = true;
        Discovery discovery;

        if (!explicitBaseUrl.empty()) {
            discovery.baseUrl = TrimTrailingSlashes(explicitBaseUrl);
            // A configured endpoint is used whether or not it lists models:
            // vLLM behind auth, or a server that only implements
            // /v1/chat/completions, still serves requests that name a model.
            discovery.serverAnswered = FetchModels(discovery.baseUrl, apiKey,
                                                   transport, timeoutMs,
                                                   discovery.models);
            ok_    = true;
            value_ = std::move(discovery);
        } else {
            std::string tried;
            for (const std::string& candidate : QwenDefaultEndpoints()) {
                std::vector<ServedModel> models;
                if (FetchModels(candidate, apiKey, transport, timeoutMs,
                                models)) {
                    discovery.baseUrl        = candidate;
                    discovery.models         = std::move(models);
                    discovery.serverAnswered = true;
                    break;
                }
                if (!tried.empty()) tried += ", ";
                tried += candidate;
            }
            if (discovery.serverAnswered) {
                ok_    = true;
                value_ = std::move(discovery);
            } else {
                ok_          = false;
                error_.code  = ErrorCode::NetworkError;
                error_.message =
                    "no local OpenAI-compatible server answered GET /v1/models "
                    "at " + tried + " — start Ollama, vLLM, llama.cpp server "
                    "or LM Studio, or set ProviderConfig::baseUrl";
            }
        }
    }

    if (!ok_) {
        if (outError) *outError = error_;
        return false;
    }
    out = value_;
    return true;
}

} // namespace qwen_detail
} // namespace UltraAI
