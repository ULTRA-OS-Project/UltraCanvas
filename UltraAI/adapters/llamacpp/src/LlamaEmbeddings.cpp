// UltraAI/adapters/llamacpp/src/LlamaEmbeddings.cpp
// llama.cpp IEmbeddings adapter: mean-pooled, L2-normalized sequence
// embeddings from a local GGUF model. Encoder-only models (BERT family)
// go through llama_encode, decoder models through llama_decode.
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module

#include "UltraAILlamaEmbeddings.h"

#include <llama.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace UltraAI {

namespace {

int64_t OptionInt(const OptionsMap& options, const std::string& key,
                  int64_t fallback) {
    auto it = options.find(key);
    if (it == options.end()) return fallback;
    if (const auto* i = std::get_if<int64_t>(&it->second)) return *i;
    if (const auto* d = std::get_if<double>(&it->second))
        return static_cast<int64_t>(*d);
    return fallback;
}

std::string OptionString(const OptionsMap& options, const std::string& key) {
    auto it = options.find(key);
    if (it == options.end()) return {};
    const auto* s = std::get_if<std::string>(&it->second);
    return s ? *s : std::string{};
}

class LlamaEmbeddings : public IEmbeddings {
public:
    explicit LlamaEmbeddings(EmbeddingsConfig config)
        : config_(std::move(config)) {}

    ~LlamaEmbeddings() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (ctx_)   llama_free(ctx_);
        if (model_) llama_model_free(model_);
    }

    bool Load(Error* outError) {
        modelPath_ = OptionString(config_.providerOptions, "model_path");
        if (modelPath_.empty()) modelPath_ = config_.defaultModel;
        if (modelPath_.empty()) {
            if (outError) {
                outError->code    = ErrorCode::InvalidRequest;
                outError->message = "llama-cpp embeddings adapter needs a GGUF "
                                    "path: set ProviderConfig::defaultModel or "
                                    "providerOptions[\"model_path\"]";
            }
            return false;
        }

        llama_backend_init();

        llama_model_params mparams = llama_model_default_params();
        mparams.n_gpu_layers = static_cast<int32_t>(
            OptionInt(config_.providerOptions, "n_gpu_layers", 0));
        model_ = llama_model_load_from_file(modelPath_.c_str(), mparams);
        if (!model_) {
            if (outError) {
                outError->code    = ErrorCode::ModelNotFound;
                outError->message = "failed to load GGUF model: " + modelPath_;
            }
            return false;
        }

        // Non-causal (pooled) evaluation needs every token of an input in
        // one physical batch, so context and batch sizes are kept equal.
        int64_t nCtxOpt = OptionInt(config_.providerOptions, "n_ctx", 0);
        uint32_t nCtx = nCtxOpt > 0
            ? static_cast<uint32_t>(nCtxOpt)
            : static_cast<uint32_t>(std::min<int32_t>(
                  std::max(llama_model_n_ctx_train(model_), 1), 4096));

        llama_context_params cparams = llama_context_default_params();
        cparams.n_ctx        = nCtx;
        cparams.n_batch      = nCtx;
        cparams.n_ubatch     = nCtx;
        cparams.embeddings   = true;
        cparams.pooling_type = LLAMA_POOLING_TYPE_MEAN;
        const int64_t threads =
            OptionInt(config_.providerOptions, "n_threads", 0);
        if (threads > 0) {
            cparams.n_threads       = static_cast<int32_t>(threads);
            cparams.n_threads_batch = static_cast<int32_t>(threads);
        }
        ctx_ = llama_init_from_model(model_, cparams);
        if (!ctx_) {
            if (outError) {
                outError->code    = ErrorCode::ProviderError;
                outError->message = "llama_init_from_model failed for " +
                                    modelPath_;
            }
            llama_model_free(model_);
            model_ = nullptr;
            return false;
        }
        vocab_ = llama_model_get_vocab(model_);
        return true;
    }

    EmbeddingProviderCapabilities GetCapabilities() const override {
        EmbeddingProviderCapabilities caps;
        caps.providerId = "llama-cpp";
        EmbeddingModelInfo m;
        m.id               = modelPath_;
        m.displayName      = modelPath_;
        m.maxInputTokens   = ctx_ ? static_cast<int32_t>(llama_n_ctx(ctx_)) : 0;
        m.outputDimensions = model_ ? llama_model_n_embd_out(model_) : 0;
        m.supportsVariableDimensions = true;    // truncate + re-normalize
        m.runsLocally      = true;
        caps.models.push_back(std::move(m));
        return caps;
    }

    EmbeddingResponse Embed(const EmbeddingRequest& request) override {
        EmbeddingResponse out;
        out.model = modelPath_;
        if (request.input.empty()) {
            out.error.code    = ErrorCode::InvalidRequest;
            out.error.message = "EmbeddingRequest::input is empty";
            return out;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        const int32_t nEmbd = llama_model_n_embd_out(model_);
        int32_t dims = nEmbd;
        if (request.dimensions) {
            if (*request.dimensions <= 0 || *request.dimensions > nEmbd) {
                out.error.code    = ErrorCode::InvalidRequest;
                out.error.message = "requested dimensions (" +
                    std::to_string(*request.dimensions) +
                    ") outside the model's native " + std::to_string(nEmbd);
                return out;
            }
            dims = *request.dimensions;
        }

        for (const std::string& text : request.input) {
            EmbeddingVector v;
            if (!EmbedOne(text, dims, v, out.usage, out.error)) {
                out.embeddings.clear();
                return out;
            }
            out.embeddings.push_back(std::move(v));
        }
        return out;
    }

    std::future<EmbeddingResponse> EmbedAsync(
        const EmbeddingRequest& request) override {
        return std::async(std::launch::async,
                          [this, request] { return Embed(request); });
    }

private:
    std::vector<llama_token> Tokenize(const std::string& text) {
        int32_t cap = static_cast<int32_t>(text.size()) + 8;
        std::vector<llama_token> tokens(static_cast<size_t>(cap));
        int32_t n = llama_tokenize(vocab_, text.c_str(),
                                   static_cast<int32_t>(text.size()),
                                   tokens.data(), cap,
                                   /*add_special=*/true,
                                   /*parse_special=*/true);
        if (n < 0) {
            tokens.resize(static_cast<size_t>(-n));
            n = llama_tokenize(vocab_, text.c_str(),
                               static_cast<int32_t>(text.size()),
                               tokens.data(), static_cast<int32_t>(tokens.size()),
                               true, true);
        }
        tokens.resize(n > 0 ? static_cast<size_t>(n) : 0);
        return tokens;
    }

    // Caller holds mutex_. Appends token usage; truncates the pooled vector
    // to `dims` and L2-normalizes.
    bool EmbedOne(const std::string& text, int32_t dims, EmbeddingVector& outV,
                  TokenUsage& usage, Error& error) {
        std::vector<llama_token> tokens = Tokenize(text);
        if (tokens.empty()) {
            error.code    = ErrorCode::InvalidRequest;
            error.message = "input tokenized to zero tokens";
            return false;
        }
        const int32_t nCtx = static_cast<int32_t>(llama_n_ctx(ctx_));
        if (static_cast<int32_t>(tokens.size()) > nCtx) {
            error.code    = ErrorCode::ContextLengthExceeded;
            error.message = "input (" + std::to_string(tokens.size()) +
                            " tokens) exceeds context size " +
                            std::to_string(nCtx);
            return false;
        }

        llama_memory_clear(llama_get_memory(ctx_), /*data=*/true);

        // Pooled embeddings need an output for every token, which
        // llama_batch_get_one does not request — build the batch manually.
        llama_batch batch =
            llama_batch_init(static_cast<int32_t>(tokens.size()), 0, 1);
        for (size_t i = 0; i < tokens.size(); ++i) {
            batch.token[i]     = tokens[i];
            batch.pos[i]       = static_cast<llama_pos>(i);
            batch.n_seq_id[i]  = 1;
            batch.seq_id[i][0] = 0;
            batch.logits[i]    = true;
        }
        batch.n_tokens = static_cast<int32_t>(tokens.size());

        const bool encoderOnly = llama_model_has_encoder(model_) &&
                                 !llama_model_has_decoder(model_);
        const int32_t rc = encoderOnly ? llama_encode(ctx_, batch)
                                       : llama_decode(ctx_, batch);
        llama_batch_free(batch);
        if (rc != 0) {
            error.code    = ErrorCode::ProviderError;
            error.message = std::string(encoderOnly ? "llama_encode"
                                                    : "llama_decode") +
                            " failed";
            return false;
        }

        const float* embd = llama_get_embeddings_seq(ctx_, 0);
        if (!embd) {
            error.code    = ErrorCode::ProviderError;
            error.message = "no pooled embedding produced (model may not "
                            "support sequence embeddings)";
            return false;
        }

        outV.values.assign(embd, embd + dims);
        double norm = 0.0;
        for (float x : outV.values) norm += static_cast<double>(x) * x;
        if (norm > 0.0) {
            const float inv = static_cast<float>(1.0 / std::sqrt(norm));
            for (float& x : outV.values) x *= inv;
        }
        usage.inputTokens += static_cast<int32_t>(tokens.size());
        return true;
    }

    EmbeddingsConfig config_;
    std::string modelPath_;
    llama_model*   model_ = nullptr;
    llama_context* ctx_   = nullptr;
    const llama_vocab* vocab_ = nullptr;
    std::mutex mutex_;
};

} // namespace

std::unique_ptr<IEmbeddings> CreateLlamaEmbeddings(
    const EmbeddingsConfig& config, Error* outError) {
    auto embeds = std::make_unique<LlamaEmbeddings>(config);
    if (!embeds->Load(outError)) return nullptr;
    return embeds;
}

} // namespace UltraAI
