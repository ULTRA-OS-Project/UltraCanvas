// UltraAI/adapters/openai/src/OpenAIEmbeddings.cpp
// OpenAI IEmbeddings adapter (POST {baseUrl}/v1/embeddings). Batched input,
// optional truncated dimensions, float encoding. Works against self-hosted
// OpenAI-compatible servers via a custom baseUrl (keyless allowed there).
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module

#include "UltraAIOpenAI.h"

#include "OpenAIInternal.h"
#ifdef ULTRAAI_HAS_ULTRANET
#include "UltraAIUltraNetTransport.h"
#endif

#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace UltraAI {

namespace {

using nlohmann::json;
using namespace openai_detail;

// Safe fallback against the hosted API only — self-hosted servers name
// their models arbitrarily, so a custom baseUrl still requires a model.
constexpr const char* kDefaultEmbeddingModel = "text-embedding-3-small";

class OpenAIEmbeddings : public IEmbeddings {
public:
    OpenAIEmbeddings(EmbeddingsConfig config,
                     std::shared_ptr<ITransport> transport)
        : config_(std::move(config)), transport_(std::move(transport)),
          baseUrl_(NormalizeBaseUrl(config_.baseUrl)) {}

    EmbeddingProviderCapabilities GetCapabilities() const override {
        EmbeddingProviderCapabilities caps;
        caps.providerId   = "openai";
        caps.maxBatchSize = 2048;
        auto add = [&caps](const char* id, const char* name, int32_t dims,
                           bool variable) {
            EmbeddingModelInfo m;
            m.id = id; m.displayName = name;
            m.maxInputTokens = 8192;
            m.outputDimensions = dims;
            m.supportsVariableDimensions = variable;
            caps.models.push_back(std::move(m));
        };
        add("text-embedding-3-small", "Text Embedding 3 Small", 1536, true);
        add("text-embedding-3-large", "Text Embedding 3 Large", 3072, true);
        return caps;
    }

    EmbeddingResponse Embed(const EmbeddingRequest& request) override {
        EmbeddingResponse out;

        std::string model =
            !request.model.empty() ? request.model : config_.defaultModel;
        if (model.empty()) {
            if (baseUrl_ == kDefaultBaseUrl) {
                model = kDefaultEmbeddingModel;
            } else {
                out.error.code    = ErrorCode::InvalidRequest;
                out.error.message = "no model specified: set "
                                    "EmbeddingRequest::model or "
                                    "ProviderConfig::defaultModel";
                return out;
            }
        }
        if (request.input.empty()) {
            out.error.code    = ErrorCode::InvalidRequest;
            out.error.message = "EmbeddingRequest::input is empty";
            return out;
        }

        json body;
        body["model"]           = model;
        body["input"]           = request.input;
        body["encoding_format"] = "float";
        if (request.dimensions) body["dimensions"] = *request.dimensions;
        // EmbeddingTaskType has no Chat/embeddings API equivalent; dropped.
        ApplyOptions(body, config_.providerOptions);
        ApplyOptions(body, request.options);

        std::string key;
        if (!ResolveKey(key, &out.error)) return out;

        TransportRequest net;
        net.method    = "POST";
        net.url       = baseUrl_ + "/v1/embeddings";
        net.timeoutMs = config_.timeoutMs;
        net.headers   = {{"content-type", "application/json"}};
        if (!key.empty()) {
            net.headers.push_back({"authorization", "Bearer " + key});
        }
        net.body = body.dump();

        Error transportError;
        TransportResponse resp = transport_->Request(net, &transportError);
        if (!transportError.IsOk()) {
            out.error = transportError;
            return out;
        }
        if (resp.statusCode >= 400) {
            out.error = MapOpenAIError(resp.statusCode, resp.body);
            return out;
        }
        return ParseResponse(resp.body, request.input.size());
    }

    std::future<EmbeddingResponse> EmbedAsync(
        const EmbeddingRequest& request) override {
        return std::async(std::launch::async,
                          [this, request] { return Embed(request); });
    }

private:
    static EmbeddingResponse ParseResponse(const std::string& body,
                                           size_t inputCount) {
        EmbeddingResponse out;
        json j = ParseJsonLenient(body);
        if (!j.is_object() || !j.contains("data") || !j["data"].is_array()) {
            out.error.code    = ErrorCode::ProviderError;
            out.error.message = "OpenAI embeddings response has no data array";
            return out;
        }
        out.model = j.value("model", "");
        // One vector per input, in input order; the API tags each entry with
        // its index, so place by index rather than trusting array order.
        out.embeddings.resize(inputCount);
        for (const auto& entry : j["data"]) {
            const size_t index = entry.value("index", 0);
            if (index >= out.embeddings.size() ||
                !entry.contains("embedding") ||
                !entry["embedding"].is_array()) {
                continue;
            }
            EmbeddingVector v;
            v.values.reserve(entry["embedding"].size());
            for (const auto& x : entry["embedding"]) {
                if (x.is_number()) v.values.push_back(x.get<float>());
            }
            out.embeddings[index] = std::move(v);
        }
        for (const auto& v : out.embeddings) {
            if (v.values.empty()) {
                out.error.code    = ErrorCode::ProviderError;
                out.error.message = "OpenAI embeddings response is missing "
                                    "vectors for some inputs";
                out.embeddings.clear();
                return out;
            }
        }
        if (j.contains("usage")) ParseUsage(j["usage"], out.usage);
        return out;
    }

    bool ResolveKey(std::string& outKey, Error* outError) {
        std::lock_guard<std::mutex> lock(keyMutex_);
        if (!keyResolved_) {
            if (!ResolveOptionalKey(config_, baseUrl_, resolvedKey_, outError)) {
                return false;
            }
            keyResolved_ = true;
        }
        outKey = resolvedKey_;
        return true;
    }

    EmbeddingsConfig config_;
    std::shared_ptr<ITransport> transport_;
    std::string baseUrl_;
    std::mutex keyMutex_;
    std::string resolvedKey_;
    bool keyResolved_ = false;
};

} // namespace

std::unique_ptr<IEmbeddings> CreateOpenAIEmbeddings(
    const EmbeddingsConfig& config,
    Error* outError,
    std::shared_ptr<ITransport> transport) {
    if (!transport) {
#ifdef ULTRAAI_HAS_ULTRANET
        transport = std::make_shared<UltraNetTransport>();
#else
        if (outError) {
            outError->code    = ErrorCode::NetworkError;
            outError->message = "OpenAI adapter needs a transport: build "
                                "with ULTRAAI_USE_ULTRANET=ON or inject one";
        }
        return nullptr;
#endif
    }
    return std::make_unique<OpenAIEmbeddings>(config, std::move(transport));
}

} // namespace UltraAI
