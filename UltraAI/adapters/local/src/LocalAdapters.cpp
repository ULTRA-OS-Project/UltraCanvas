// UltraAI/adapters/local/src/LocalAdapters.cpp
// The `local` provider: the runtime supplies an engine endpoint for the
// model, the OpenAI adapter speaks to it. Resolution is lazy and per
// model: a request naming another installed model gets its own engine.
// Version: 0.1.0
// Last Modified: 2026-10-20
// Author: UltraAI Module

#include "UltraAILocal.h"

#include "UltraAILocalRuntime.h"
#include "UltraAIOpenAI.h"
#include "UltraAIStreamHandleBase.h"

#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <variant>

namespace UltraAI {

namespace {

bool OptionBool(const OptionsMap& options, const char* key) {
    auto it = options.find(key);
    if (it == options.end()) return false;
    if (const bool* b = std::get_if<bool>(&it->second)) return *b;
    if (const int64_t* i = std::get_if<int64_t>(&it->second)) return *i != 0;
    if (const std::string* s = std::get_if<std::string>(&it->second)) {
        return *s == "1" || *s == "true" || *s == "yes" || *s == "on";
    }
    return false;
}

// Shared by both wrappers: which runtime, which model, and one delegate
// per model that has been asked for.
template <typename Inner, typename Config>
class LocalBase {
public:
    LocalBase(Config config, LocalRuntime* runtime, std::shared_ptr<ITransport> transport,
              std::string capability)
        : config_(std::move(config)),
          runtime_(runtime ? runtime : &LocalRuntime::Instance()),
          transport_(std::move(transport)),
          capability_(std::move(capability)) {
        allowDownload_ = OptionBool(config_.providerOptions, "allow_download");
        config_.providerOptions.erase("allow_download");
    }
    virtual ~LocalBase() = default;

protected:
    // The model a request means: its own, the configured default, or the
    // runtime's recommendation for this computer.
    std::string ResolveModelId(const std::string& requested, Error* outError) const {
        if (!requested.empty()) return requested;
        if (!config_.defaultModel.empty()) return config_.defaultModel;
        const LocalModelInfo recommended = runtime_->Recommend(capability_);
        if (recommended.id.empty() && outError) {
            outError->code    = ErrorCode::ModelNotFound;
            outError->message = "no local model is installed or offered for " + capability_ +
                                " on this computer";
            outError->providerCode = "needs-install";
        }
        return recommended.id;
    }

    // The delegate for a model, built on the engine the runtime provides.
    Inner* DelegateFor(const std::string& modelId, Error* outError) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = delegates_.find(modelId);
        if (it != delegates_.end()) {
            runtime_->Touch(it->second.endpoint);
            return it->second.inner.get();
        }
        EngineEndpoint endpoint;
        Error error = runtime_->Ensure(modelId, endpoint, allowDownload_);
        if (!error.IsOk()) {
            if (outError) *outError = error;
            return nullptr;
        }
        Config innerConfig = config_;
        innerConfig.baseUrl        = endpoint.baseUrl;
        innerConfig.apiKey         = endpoint.apiKey;
        innerConfig.apiKeyVaultRef.clear();
        innerConfig.defaultModel   = endpoint.modelAlias;
        Error createError;
        std::unique_ptr<Inner> inner = Create(innerConfig, &createError);
        if (!inner) {
            if (outError) {
                *outError = createError;
                if (outError->IsOk()) {
                    outError->code    = ErrorCode::ProviderError;
                    outError->message = "could not create the OpenAI-compatible delegate for the local engine";
                }
            }
            return nullptr;
        }
        Entry entry;
        entry.endpoint = endpoint;
        entry.inner    = std::move(inner);
        Inner* raw = entry.inner.get();
        delegates_[modelId] = std::move(entry);
        return raw;
    }

    virtual std::unique_ptr<Inner> Create(const Config& config, Error* outError) const = 0;

    Config config_;
    LocalRuntime* runtime_;
    std::shared_ptr<ITransport> transport_;
    std::string capability_;
    bool allowDownload_ = false;

    struct Entry {
        EngineEndpoint endpoint;
        std::unique_ptr<Inner> inner;
    };
    mutable std::mutex mutex_;
    mutable std::map<std::string, Entry> delegates_;
};

// ---------------------------------------------------------------------
// ITextLLM
// ---------------------------------------------------------------------

class LocalTextLLM : public ITextLLM, public LocalBase<ITextLLM, TextLLMConfig> {
public:
    LocalTextLLM(TextLLMConfig config, LocalRuntime* runtime, std::shared_ptr<ITransport> transport)
        : LocalBase(std::move(config), runtime, std::move(transport), "textllm") {}

    ProviderCapabilities GetCapabilities() const override {
        ProviderCapabilities caps;
        caps.providerId         = "local";
        caps.supportsStreaming  = true;
        caps.supportsTools      = true;
        caps.supportsVision     = true;
        caps.supportsEmbeddings = true;
        // Installed models first, then what the catalogue offers for this
        // computer, so a picker can show both without a second API.
        for (int pass = 0; pass < 2; ++pass) {
            for (const LocalModelInfo& m : runtime_->ListModels("textllm")) {
                if ((pass == 0) != m.installed) continue;
                if (m.fit == ModelFit::TooLarge) continue;
                ModelInfo info;
                info.id                 = m.id;
                info.displayName        = m.installed ? m.displayName
                                                      : m.displayName + " (not installed)";
                info.contextWindow      = m.contextWindow;
                info.supportsVision     = m.supportsVision;
                info.supportsTools      = m.supportsTools;
                info.supportsStreaming  = true;
                info.supportsJsonSchema = true;
                info.runsLocally        = true;
                caps.models.push_back(std::move(info));
            }
        }
        return caps;
    }

    ChatResponse Chat(const ChatRequest& request) override {
        ChatResponse out;
        ChatRequest resolved = request;
        ITextLLM* inner = Prepare(resolved, &out.error);
        if (!inner) return out;
        return inner->Chat(resolved);
    }

    std::future<ChatResponse> ChatAsync(const ChatRequest& request) override {
        return std::async(std::launch::async, [this, request] { return Chat(request); });
    }

    StreamHandle ChatStream(const ChatRequest& request, StreamCallback onEvent) override {
        ChatRequest resolved = request;
        Error error;
        ITextLLM* inner = Prepare(resolved, &error);
        if (!inner) {
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
        return inner->ChatStream(resolved, std::move(onEvent));
    }

    int32_t CountTokens(const std::string& model, const std::vector<Message>& messages) override {
        Error ignored;
        const std::string modelId = ResolveModelId(model, &ignored);
        ITextLLM* inner = modelId.empty() ? nullptr : DelegateFor(modelId, &ignored);
        return inner ? inner->CountTokens(modelId, messages) : 0;
    }

    // The OpenAI-adapter delegate of the default model; null until the
    // first successful call.
    void* RawProvider() override {
        Error ignored;
        const std::string modelId = ResolveModelId("", &ignored);
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = delegates_.find(modelId);
        return it == delegates_.end() ? nullptr : it->second.inner.get();
    }

protected:
    std::unique_ptr<ITextLLM> Create(const TextLLMConfig& config, Error* outError) const override {
        return CreateOpenAITextLLM(config, outError, transport_);
    }

private:
    ITextLLM* Prepare(ChatRequest& request, Error* outError) const {
        const std::string modelId = ResolveModelId(request.model, outError);
        if (modelId.empty()) return nullptr;
        ITextLLM* inner = DelegateFor(modelId, outError);
        if (inner) request.model = modelId;
        return inner;
    }
};

// ---------------------------------------------------------------------
// IEmbeddings
// ---------------------------------------------------------------------

class LocalEmbeddings : public IEmbeddings, public LocalBase<IEmbeddings, EmbeddingsConfig> {
public:
    LocalEmbeddings(EmbeddingsConfig config, LocalRuntime* runtime, std::shared_ptr<ITransport> transport)
        : LocalBase(std::move(config), runtime, std::move(transport), "embeddings") {}

    EmbeddingProviderCapabilities GetCapabilities() const override {
        EmbeddingProviderCapabilities caps;
        caps.providerId = "local";
        for (int pass = 0; pass < 2; ++pass) {
            for (const LocalModelInfo& m : runtime_->ListModels("embeddings")) {
                if ((pass == 0) != m.installed) continue;
                if (m.fit == ModelFit::TooLarge) continue;
                EmbeddingModelInfo info;
                info.id               = m.id;
                info.displayName      = m.installed ? m.displayName : m.displayName + " (not installed)";
                info.maxInputTokens   = m.contextWindow;
                info.outputDimensions = m.embeddingDimensions;
                info.runsLocally      = true;
                caps.models.push_back(std::move(info));
            }
        }
        return caps;
    }

    EmbeddingResponse Embed(const EmbeddingRequest& request) override {
        EmbeddingResponse out;
        EmbeddingRequest resolved = request;
        const std::string modelId = ResolveModelId(request.model, &out.error);
        if (modelId.empty()) return out;
        IEmbeddings* inner = DelegateFor(modelId, &out.error);
        if (!inner) return out;
        resolved.model = modelId;
        return inner->Embed(resolved);
    }

    std::future<EmbeddingResponse> EmbedAsync(const EmbeddingRequest& request) override {
        return std::async(std::launch::async, [this, request] { return Embed(request); });
    }

    void* RawProvider() override {
        Error ignored;
        const std::string modelId = ResolveModelId("", &ignored);
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = delegates_.find(modelId);
        return it == delegates_.end() ? nullptr : it->second.inner.get();
    }

protected:
    std::unique_ptr<IEmbeddings> Create(const EmbeddingsConfig& config, Error* outError) const override {
        return CreateOpenAIEmbeddings(config, outError, transport_);
    }
};

} // namespace

std::unique_ptr<ITextLLM> CreateLocalTextLLM(const TextLLMConfig& config, Error* outError,
                                             LocalRuntime* runtime,
                                             std::shared_ptr<ITransport> transport) {
    if (outError) *outError = Error{};
    return std::make_unique<LocalTextLLM>(config, runtime, std::move(transport));
}

std::unique_ptr<IEmbeddings> CreateLocalEmbeddings(const EmbeddingsConfig& config, Error* outError,
                                                   LocalRuntime* runtime,
                                                   std::shared_ptr<ITransport> transport) {
    if (outError) *outError = Error{};
    return std::make_unique<LocalEmbeddings>(config, runtime, std::move(transport));
}

} // namespace UltraAI
