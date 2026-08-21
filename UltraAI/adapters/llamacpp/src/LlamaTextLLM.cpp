// UltraAI/adapters/llamacpp/src/LlamaTextLLM.cpp
// llama.cpp ITextLLM adapter: chat-template prompting, sampler-chain
// generation, streaming with cancellation, exact token counting, and
// GBNF-constrained JSON output. One llama_context per adapter instance;
// calls are serialized on an internal mutex.
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module

#include "UltraAILlamaTextLLM.h"
#include "UltraAIStreamHandleBase.h"

#include <llama.h>

#include <atomic>
#include <cstring>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace UltraAI {

namespace {

// Universal JSON grammar (llama.cpp grammars/json.gbnf). Constrains output
// to syntactically valid JSON; per-schema constraints are a follow-up.
constexpr const char* kJsonGrammar = R"GBNF(
root   ::= object
value  ::= object | array | string | number | ("true" | "false" | "null") ws
object ::=
  "{" ws (
            string ":" ws value
    ("," ws string ":" ws value)*
  )? "}" ws
array  ::=
  "[" ws (
            value
    ("," ws value)*
  )? "]" ws
string ::=
  "\"" (
    [^"\\\x7F\x00-\x1F] |
    "\\" (["\\bfnrt] | "u" [0-9a-fA-F]{4})
  )* "\"" ws
number ::= ("-"? ([0-9] | [1-9] [0-9]{0,15})) ("." [0-9]+)? ([eE] [-+]? [0-9] [1-9]{0,15})? ws
ws ::= | " " | "\n" [ \t]{0,20}
)GBNF";

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

const char* RoleName(Role role) {
    switch (role) {
        case Role::System:    return "system";
        case Role::User:      return "user";
        case Role::Assistant: return "assistant";
        case Role::Tool:      return "tool";
    }
    return "user";
}

class LlamaTextLLM : public ITextLLM {
public:
    LlamaTextLLM(TextLLMConfig config)
        : config_(std::move(config)) {}

    ~LlamaTextLLM() override {
        if (ctx_)   llama_free(ctx_);
        if (model_) llama_model_free(model_);
    }

    bool Load(Error* outError) {
        modelPath_ = OptionString(config_.providerOptions, "model_path");
        if (modelPath_.empty()) modelPath_ = config_.defaultModel;
        if (modelPath_.empty()) {
            if (outError) {
                outError->code    = ErrorCode::InvalidRequest;
                outError->message = "llama-cpp adapter needs a GGUF path: set "
                                    "ProviderConfig::defaultModel or "
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

        llama_context_params cparams = llama_context_default_params();
        cparams.n_ctx = static_cast<uint32_t>(
            OptionInt(config_.providerOptions, "n_ctx", 0));   // 0 = model default
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

    ProviderCapabilities GetCapabilities() const override {
        ProviderCapabilities caps;
        caps.providerId        = "llama-cpp";
        caps.supportsStreaming = true;
        ModelInfo m;
        m.id                 = modelPath_;
        m.displayName        = modelPath_;
        m.contextWindow      = model_ ? llama_model_n_ctx_train(model_) : 0;
        m.supportsStreaming  = true;
        m.supportsJsonSchema = true;    // valid-JSON grammar; see header
        m.runsLocally        = true;
        caps.models.push_back(std::move(m));
        return caps;
    }

    ChatResponse Chat(const ChatRequest& request) override {
        ChatResponse out;
        std::lock_guard<std::mutex> lock(mutex_);
        std::atomic<bool> neverCancelled{false};
        Generate(request, neverCancelled,
                 [&out](const std::string& piece) { out.text += piece; },
                 out.finishReason, out.usage, out.error);
        out.model = modelPath_;
        return out;
    }

    std::future<ChatResponse> ChatAsync(const ChatRequest& request) override {
        return std::async(std::launch::async,
                          [this, request] { return Chat(request); });
    }

    StreamHandle ChatStream(const ChatRequest& request,
                            StreamCallback onEvent) override {
        auto handle    = std::make_shared<StreamHandleBase>();
        auto cancelled = std::make_shared<std::atomic<bool>>(false);
        handle->SetCancelHook([cancelled] { cancelled->store(true); });

        std::thread([this, request, onEvent, handle, cancelled] {
            FinishReason finish = FinishReason::Unknown;
            TokenUsage usage;
            Error error;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                Generate(request, *cancelled,
                         [&onEvent](const std::string& piece) {
                             StreamEvent ev;
                             ev.kind      = StreamEventKind::TextDelta;
                             ev.textDelta = piece;
                             onEvent(ev);
                         },
                         finish, usage, error);
            }
            StreamEvent last;
            if (cancelled->load()) {
                last.kind          = StreamEventKind::Error;
                last.error.code    = ErrorCode::Cancelled;
                last.error.message = "generation cancelled";
            } else if (!error.IsOk()) {
                last.kind  = StreamEventKind::Error;
                last.error = error;
            } else {
                last.kind         = StreamEventKind::Done;
                last.finishReason = finish;
                last.usage        = usage;
            }
            onEvent(last);
            handle->MarkDone();
        }).detach();

        return handle;
    }

    int32_t CountTokens(const std::string& /*model*/,
                        const std::vector<Message>& messages) override {
        std::lock_guard<std::mutex> lock(mutex_);
        ChatRequest req;
        req.messages = messages;
        Error error;
        std::string prompt = BuildPrompt(req, &error);
        if (!error.IsOk()) return 0;
        return static_cast<int32_t>(Tokenize(prompt).size());
    }

private:
    // Render the conversation through the model's chat template (or a
    // plain role-prefixed fallback when the model ships none).
    std::string BuildPrompt(const ChatRequest& request, Error* outError) {
        for (const auto& m : request.messages) {
            if (m.role == Role::Tool || !m.toolCalls.empty()) {
                if (outError) {
                    outError->code    = ErrorCode::InvalidRequest;
                    outError->message = "llama-cpp adapter v0.1 does not "
                                        "support tool calls";
                }
                return {};
            }
        }
        if (!request.tools.empty()) {
            if (outError) {
                outError->code    = ErrorCode::InvalidRequest;
                outError->message = "llama-cpp adapter v0.1 does not support "
                                    "tool definitions";
            }
            return {};
        }

        std::vector<llama_chat_message> chat;
        chat.reserve(request.messages.size());
        for (const auto& m : request.messages) {
            chat.push_back({RoleName(m.role), m.text.c_str()});
        }

        const char* tmpl = llama_model_chat_template(model_, nullptr);
        if (tmpl) {
            size_t cap = 256;
            for (const auto& m : request.messages) cap += 2 * m.text.size();
            std::vector<char> buf(cap);
            int32_t n = llama_chat_apply_template(tmpl, chat.data(), chat.size(),
                                                  /*add_ass=*/true,
                                                  buf.data(),
                                                  static_cast<int32_t>(buf.size()));
            if (n > static_cast<int32_t>(buf.size())) {
                buf.resize(static_cast<size_t>(n));
                n = llama_chat_apply_template(tmpl, chat.data(), chat.size(),
                                              true, buf.data(),
                                              static_cast<int32_t>(buf.size()));
            }
            if (n >= 0) return std::string(buf.data(), static_cast<size_t>(n));
            // fall through to the plain fallback on template failure
        }

        std::string prompt;
        for (const auto& m : request.messages) {
            prompt += RoleName(m.role);
            prompt += ": ";
            prompt += m.text;
            prompt += "\n";
        }
        prompt += "assistant:";
        return prompt;
    }

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

    std::string PieceOf(llama_token token) {
        char buf[256];
        int32_t n = llama_token_to_piece(vocab_, token, buf,
                                         static_cast<int32_t>(sizeof(buf)),
                                         /*lstrip=*/0, /*special=*/false);
        return n > 0 ? std::string(buf, static_cast<size_t>(n)) : std::string();
    }

    // Shared generation core for Chat and ChatStream. Emits text through
    // `emit`, reports the finish reason, token usage, and any error.
    // Caller holds mutex_.
    void Generate(const ChatRequest& request,
                  std::atomic<bool>& cancelled,
                  const std::function<void(const std::string&)>& emit,
                  FinishReason& finish, TokenUsage& usage, Error& error) {
        std::string prompt = BuildPrompt(request, &error);
        if (!error.IsOk()) { finish = FinishReason::Error; return; }

        std::vector<llama_token> tokens = Tokenize(prompt);
        if (tokens.empty()) {
            error.code    = ErrorCode::InvalidRequest;
            error.message = "prompt tokenized to zero tokens";
            finish = FinishReason::Error;
            return;
        }

        const int32_t nCtx = static_cast<int32_t>(llama_n_ctx(ctx_));
        const int32_t maxOut = request.sampling.maxOutputTokens.value_or(512);
        if (static_cast<int32_t>(tokens.size()) + maxOut > nCtx) {
            error.code    = ErrorCode::ContextLengthExceeded;
            error.message = "prompt (" + std::to_string(tokens.size()) +
                            " tokens) + max output (" + std::to_string(maxOut) +
                            ") exceeds context size " + std::to_string(nCtx);
            finish = FinishReason::Error;
            return;
        }
        usage.inputTokens = static_cast<int32_t>(tokens.size());

        // Stateless calls: reset the context memory between requests.
        llama_memory_clear(llama_get_memory(ctx_), /*data=*/true);

        // Sampler chain: grammar (optional) -> top_p -> temp -> dist.
        llama_sampler* chain =
            llama_sampler_chain_init(llama_sampler_chain_default_params());
        if (request.responseFormat != ResponseFormat::Text) {
            llama_sampler* grammar =
                llama_sampler_init_grammar(vocab_, kJsonGrammar, "root");
            if (grammar) llama_sampler_chain_add(chain, grammar);
        }
        const double temperature = request.sampling.temperature.value_or(0.8);
        if (temperature <= 0.0) {
            llama_sampler_chain_add(chain, llama_sampler_init_greedy());
        } else {
            if (request.sampling.topP) {
                llama_sampler_chain_add(
                    chain, llama_sampler_init_top_p(
                               static_cast<float>(*request.sampling.topP), 1));
            }
            llama_sampler_chain_add(
                chain, llama_sampler_init_temp(static_cast<float>(temperature)));
            const uint32_t seed = request.sampling.seed
                ? static_cast<uint32_t>(*request.sampling.seed)
                : LLAMA_DEFAULT_SEED;
            llama_sampler_chain_add(chain, llama_sampler_init_dist(seed));
        }

        llama_batch batch =
            llama_batch_get_one(tokens.data(), static_cast<int32_t>(tokens.size()));

        std::string generated;
        finish = FinishReason::Length;
        for (int32_t produced = 0; produced < maxOut; ++produced) {
            if (cancelled.load()) { finish = FinishReason::Error; break; }
            if (llama_decode(ctx_, batch) != 0) {
                error.code    = ErrorCode::ProviderError;
                error.message = "llama_decode failed";
                finish = FinishReason::Error;
                break;
            }
            llama_token tok = llama_sampler_sample(chain, ctx_, -1);
            if (llama_vocab_is_eog(vocab_, tok)) {
                finish = FinishReason::Stop;
                break;
            }
            std::string piece = PieceOf(tok);
            generated += piece;
            usage.outputTokens = produced + 1;
            if (!piece.empty()) emit(piece);

            bool hitStop = false;
            for (const auto& stop : request.sampling.stopSequences) {
                if (!stop.empty() && generated.size() >= stop.size() &&
                    generated.compare(generated.size() - stop.size(),
                                      stop.size(), stop) == 0) {
                    hitStop = true;
                    break;
                }
            }
            if (hitStop) { finish = FinishReason::Stop; break; }

            nextToken_ = tok;
            batch = llama_batch_get_one(&nextToken_, 1);
        }

        llama_sampler_free(chain);
    }

    TextLLMConfig config_;
    std::string modelPath_;
    llama_model*   model_ = nullptr;
    llama_context* ctx_   = nullptr;
    const llama_vocab* vocab_ = nullptr;
    llama_token nextToken_ = 0;
    std::mutex mutex_;
};

} // namespace

std::unique_ptr<ITextLLM> CreateLlamaTextLLM(const TextLLMConfig& config,
                                             Error* outError) {
    auto llm = std::make_unique<LlamaTextLLM>(config);
    if (!llm->Load(outError)) return nullptr;
    return llm;
}

} // namespace UltraAI
