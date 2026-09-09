// UltraAI/adapters/qwen/src/QwenAdapters.cpp
// Qwen adapters for locally served OpenAI-compatible endpoints. The wire
// protocol is the OpenAI adapter's; what lives here is discovery (which
// local server is running), model selection (which Qwen model it serves),
// and capability reporting with runsLocally = true.
// Version: 0.1.0
// Last Modified: 2026-08-24
// Author: UltraAI Module

#include "UltraAIQwen.h"

#include "QwenDiscovery.h"
#include "UltraAIOpenAI.h"
#include "UltraAIStreamHandleBase.h"
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

using namespace qwen_detail;

// Probing four endpoints must not stall a caller behind a long transport
// timeout, so discovery uses its own short one.
constexpr int kProbeTimeoutMs = 2000;

// Shared state and lazy resolution for both wrappers. `Inner` is the
// capability interface being delegated to (ITextLLM / IEmbeddings), built
// once the endpoint is known.
template <typename Inner, typename Config>
class QwenBase {
public:
    QwenBase(Config config, std::shared_ptr<ITransport> transport,
             bool embeddings)
        : config_(std::move(config)), transport_(std::move(transport)),
          embeddings_(embeddings) {}

protected:
    // Resolve the endpoint, build the delegate, and report the model to use
    // when the caller named none. Const so GetCapabilities() can call it.
    bool Ensure(Error* outError) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (built_) {
            if (!buildError_.IsOk()) {
                if (outError) *outError = buildError_;
                return false;
            }
            return true;
        }
        built_ = true;

        Discovery discovery;
        Error discoveryError;
        if (!cache_.Resolve(config_.baseUrl, config_.apiKey, transport_,
                            kProbeTimeoutMs, discovery, &discoveryError)) {
            buildError_ = discoveryError;
            if (outError) *outError = buildError_;
            return false;
        }
        discovery_ = std::move(discovery);

        defaultModel_ = config_.defaultModel.empty()
                            ? PickDefaultModel(discovery_.models, embeddings_)
                            : config_.defaultModel;

        Config innerConfig   = config_;
        innerConfig.baseUrl  = discovery_.baseUrl;
        // The OpenAI adapter treats a non-default baseUrl as keyless-allowed
        // and requires an explicit model, both of which suit a local server.
        innerConfig.defaultModel = defaultModel_;

        Error createError;
        inner_ = Create(innerConfig, &createError);
        if (!inner_) {
            buildError_ = createError;
            if (buildError_.IsOk()) {
                buildError_.code    = ErrorCode::ProviderError;
                buildError_.message = "could not create the OpenAI-compatible "
                                      "delegate for the local Qwen endpoint";
            }
            if (outError) *outError = buildError_;
            return false;
        }
        return true;
    }

    // Built by the concrete wrapper; separates "which factory" from "when".
    virtual std::unique_ptr<Inner> Create(const Config& config,
                                          Error* outError) const = 0;
    virtual ~QwenBase() = default;

    Inner* inner() const { return inner_.get(); }
    const std::string& defaultModel() const { return defaultModel_; }
    const Discovery& discovery() const { return discovery_; }

    Config config_;
    std::shared_ptr<ITransport> transport_;
    bool embeddings_ = false;

    mutable std::mutex mutex_;
    mutable qwen_detail::DiscoveryCache cache_;
    mutable Discovery discovery_;
    mutable std::unique_ptr<Inner> inner_;
    mutable std::string defaultModel_;
    mutable Error buildError_;
    mutable bool built_ = false;
};

// ---------------------------------------------------------------------
// ITextLLM
// ---------------------------------------------------------------------

class QwenTextLLM : public ITextLLM, public QwenBase<ITextLLM, TextLLMConfig> {
public:
    QwenTextLLM(TextLLMConfig config, std::shared_ptr<ITransport> transport)
        : QwenBase(std::move(config), std::move(transport),
                   /*embeddings=*/false) {}

    ProviderCapabilities GetCapabilities() const override {
        ProviderCapabilities caps;
        caps.providerId        = "qwen";
        caps.supportsStreaming = true;
        caps.supportsTools     = true;
        caps.supportsVision    = true;
        caps.supportsEmbeddings = true;

        Error ignored;
        if (!Ensure(&ignored)) return caps;   // no server: no models to list

        for (const auto& served : discovery().models) {
            if (served.looksLikeEmbedding) continue;
            ModelInfo model;
            model.id                 = served.id;
            model.displayName        = served.id;
            model.supportsStreaming  = true;
            model.supportsTools      = true;
            model.supportsJsonSchema = true;
            model.runsLocally        = true;
            caps.models.push_back(std::move(model));
        }
        return caps;
    }

    ChatResponse Chat(const ChatRequest& request) override {
        ChatResponse out;
        ChatRequest resolved = request;
        if (!Prepare(resolved, &out.error)) return out;
        return inner()->Chat(resolved);
    }

    std::future<ChatResponse> ChatAsync(const ChatRequest& request) override {
        return std::async(std::launch::async,
                          [this, request] { return Chat(request); });
    }

    StreamHandle ChatStream(const ChatRequest& request,
                            StreamCallback onEvent) override {
        ChatRequest resolved = request;
        Error error;
        if (!Prepare(resolved, &error)) {
            auto handle = std::make_shared<StreamHandleBase>();
            if (onEvent) {
                StreamEvent event;
                event.kind  = StreamEventKind::Error;
                event.error = error;
                onEvent(event);
            }
            handle->MarkDone();
            return handle;
        }
        return inner()->ChatStream(resolved, std::move(onEvent));
    }

    int32_t CountTokens(const std::string& model,
                        const std::vector<Message>& messages) override {
        Error ignored;
        if (!Ensure(&ignored)) return 0;
        return inner()->CountTokens(model.empty() ? defaultModel() : model,
                                    messages);
    }

    // The OpenAI-adapter delegate, so callers can reach its own escape
    // hatch; null until the first successful call.
    void* RawProvider() override { return inner(); }

protected:
    std::unique_ptr<ITextLLM> Create(const TextLLMConfig& config,
                                     Error* outError) const override {
        return CreateOpenAITextLLM(config, outError, transport_);
    }

private:
    // Fill in the model the caller left empty; fail early when neither the
    // request, the config nor the server offers one.
    bool Prepare(ChatRequest& request, Error* outError) const {
        if (!Ensure(outError)) return false;
        if (request.model.empty()) request.model = defaultModel();
        if (request.model.empty()) {
            if (outError) {
                outError->code    = ErrorCode::ModelNotFound;
                outError->message = "the local server at " + discovery().baseUrl +
                                    " reported no models; set "
                                    "ChatRequest::model or "
                                    "ProviderConfig::defaultModel";
            }
            return false;
        }
        return true;
    }
};

// ---------------------------------------------------------------------
// IEmbeddings
// ---------------------------------------------------------------------

class QwenEmbeddings : public IEmbeddings,
                       public QwenBase<IEmbeddings, EmbeddingsConfig> {
public:
    QwenEmbeddings(EmbeddingsConfig config,
                   std::shared_ptr<ITransport> transport)
        : QwenBase(std::move(config), std::move(transport),
                   /*embeddings=*/true) {}

    EmbeddingProviderCapabilities GetCapabilities() const override {
        EmbeddingProviderCapabilities caps;
        caps.providerId   = "qwen";
        caps.maxBatchSize = 256;

        Error ignored;
        if (!Ensure(&ignored)) return caps;

        for (const auto& served : discovery().models) {
            if (!served.looksLikeEmbedding) continue;
            EmbeddingModelInfo model;
            model.id          = served.id;
            model.displayName = served.id;
            model.runsLocally = true;
            caps.models.push_back(std::move(model));
        }
        return caps;
    }

    EmbeddingResponse Embed(const EmbeddingRequest& request) override {
        EmbeddingResponse out;
        if (!Ensure(&out.error)) return out;

        EmbeddingRequest resolved = request;
        if (resolved.model.empty()) resolved.model = defaultModel();
        if (resolved.model.empty()) {
            out.error.code    = ErrorCode::ModelNotFound;
            out.error.message = "the local server at " + discovery().baseUrl +
                                " reported no embedding model; set "
                                "EmbeddingRequest::model or "
                                "ProviderConfig::defaultModel";
            return out;
        }
        return inner()->Embed(resolved);
    }

    std::future<EmbeddingResponse> EmbedAsync(
        const EmbeddingRequest& request) override {
        return std::async(std::launch::async,
                          [this, request] { return Embed(request); });
    }

    void* RawProvider() override { return inner(); }

protected:
    std::unique_ptr<IEmbeddings> Create(const EmbeddingsConfig& config,
                                        Error* outError) const override {
        return CreateOpenAIEmbeddings(config, outError, transport_);
    }
};

template <typename T>
bool EnsureTransport(std::shared_ptr<ITransport>& transport, Error* outError) {
    if (transport) return true;
#ifdef ULTRAAI_HAS_ULTRANET
    transport = std::make_shared<UltraNetTransport>();
    return true;
#else
    if (outError) {
        outError->code    = ErrorCode::NetworkError;
        outError->message = "Qwen adapter needs a transport: build with "
                            "ULTRAAI_USE_ULTRANET=ON or inject one";
    }
    return false;
#endif
}

} // namespace

std::unique_ptr<ITextLLM> CreateQwenTextLLM(
    const TextLLMConfig& config,
    Error* outError,
    std::shared_ptr<ITransport> transport) {
    if (!EnsureTransport<ITextLLM>(transport, outError)) return nullptr;
    return std::make_unique<QwenTextLLM>(config, std::move(transport));
}

std::unique_ptr<IEmbeddings> CreateQwenEmbeddings(
    const EmbeddingsConfig& config,
    Error* outError,
    std::shared_ptr<ITransport> transport) {
    if (!EnsureTransport<IEmbeddings>(transport, outError)) return nullptr;
    return std::make_unique<QwenEmbeddings>(config, std::move(transport));
}

} // namespace UltraAI
