// UltraAI/adapters/anthropic/src/AnthropicVisionAnalyzer.cpp
// Anthropic (Claude) IVisionAnalyzer: OCR through the Messages API. See
// include/UltraAIAnthropicVisionAnalyzer.h.
// Version: 0.1.0
// Last Modified: 2026-11-06
// Author: UltraAI Module

#include "UltraAIAnthropicVisionAnalyzer.h"

#include "UltraAIAnthropicTextLLM.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <future>
#include <memory>
#include <string>
#include <utility>

namespace UltraAI {

namespace {

using nlohmann::json;

constexpr const char* kDefaultModel = "claude-opus-5-5";
constexpr const char* kFallbackBeta = "server-side-fallback-2026-07-01";
constexpr const char* kVaultRef = "ai.anthropic.api_key";   // UltraAICommon.h's canonical name

// What Claude is asked. The structured output carries the words alone, so
// the instruction is about fidelity, not about leaving commentary out.
constexpr const char* kOcrInstruction =
    "Transcribe all of the text in this image exactly as written, in reading order. "
    "Keep the original language, spelling, numbers and punctuation; do not translate, "
    "correct or summarise. Separate lines with a line break and paragraphs or separate "
    "blocks with a blank line; write a table row by row with its cells separated by tabs. "
    "Leave out anything that is not text. If the image contains no text, return an empty string.";

// {"text": "..."}: the transcription and nothing else.
json OcrOutputConfig() {
    return json{{"format",
                 {{"type", "json_schema"},
                  {"schema",
                   {{"type", "object"},
                    {"properties", {{"text", {{"type", "string"}}}}},
                    {"required", json::array({"text"})},
                    {"additionalProperties", false}}}}},
                {"effort", "low"}};
}

bool HasOption(const OptionsMap& options, const std::string& key) {
    return options.find(key) != options.end();
}

std::string OptionString(const OptionsMap& options, const std::string& key) {
    auto it = options.find(key);
    if (it == options.end()) return {};
    const auto* s = std::get_if<std::string>(&it->second);
    return s ? *s : std::string{};
}

class AnthropicVisionAnalyzer : public IVisionAnalyzer {
public:
    AnthropicVisionAnalyzer(VisionAnalyzerConfig config, std::unique_ptr<ITextLLM> llm)
        : config_(std::move(config)), llm_(std::move(llm)) {}

    VisionProviderCapabilities GetCapabilities() const override {
        VisionProviderCapabilities caps;
        caps.providerId = "anthropic";
        auto add = [&caps](const char* id, const char* name) {
            VisionModelInfo m;
            m.id = id;
            m.displayName = name;
            m.supportedTasks = {VisionTask::Ocr};
            m.runsLocally = false;
            caps.models.push_back(std::move(m));
        };
        // The configured default first: the bridge reads with the first model
        // that declares OCR.
        const std::string preferred = config_.defaultModel.empty() ? kDefaultModel : config_.defaultModel;
        add(preferred.c_str(), preferred == "claude-opus-5-5" ? "Claude Opus 5.5" : preferred.c_str());
        if (preferred != "claude-opus-5-5") add("claude-opus-5-5", "Claude Opus 5.5");
        if (preferred != "claude-sonnet-5-5") add("claude-sonnet-5-5", "Claude Sonnet 5.5");
        if (preferred != "claude-haiku-5-5") add("claude-haiku-5-5", "Claude Haiku 5.5");
        return caps;
    }

    VisionAnalyzeResponse Analyze(const VisionAnalyzeRequest& request) override {
        VisionAnalyzeResponse out;
        out.model = !request.model.empty() ? request.model
                    : !config_.defaultModel.empty() ? config_.defaultModel
                                                     : kDefaultModel;
        for (VisionTask task : request.tasks) {
            if (task != VisionTask::Ocr) {
                out.error.code = ErrorCode::InvalidRequest;
                out.error.message = "The Anthropic vision adapter reads text only (VisionTask::Ocr)";
                return out;
            }
        }
        if (request.image.bytes.empty() && request.image.url.empty()) {
            out.error.code = ErrorCode::InvalidRequest;
            out.error.message = "No image to read";
            return out;
        }
        if (!request.image.bytes.empty() && request.image.mimeType.empty()) {
            out.error.code = ErrorCode::InvalidRequest;
            out.error.message = "An inline image needs its mimeType (image/png, image/jpeg, image/webp, image/gif)";
            return out;
        }

        ChatRequest chat;
        chat.model = out.model;
        Message user;
        user.role = Role::User;
        ImagePart image;
        image.bytes = request.image.bytes;
        image.url = request.image.url;
        image.mimeType = request.image.mimeType;
        user.parts.push_back(std::move(image));
        std::string instruction = kOcrInstruction;
        if (!request.prompt.empty()) instruction += "\n\n" + request.prompt;
        user.parts.push_back(TextPart{instruction});
        chat.messages.push_back(std::move(user));

        // Defaults the configuration may replace (see the header).
        const OptionsMap& configured = config_.providerOptions;
        if (!HasOption(configured, "output_config")) chat.options["output_config"] = OcrOutputConfig().dump();
        if (!HasOption(configured, "fallbacks")) {
            chat.options["fallbacks"] = std::string("default");
            std::string beta = OptionString(configured, "anthropic-beta");
            if (beta.find(kFallbackBeta) == std::string::npos)
                beta = beta.empty() ? std::string(kFallbackBeta) : beta + "," + kFallbackBeta;
            chat.options["anthropic-beta"] = beta;
        }
        for (const auto& [key, value] : request.options) chat.options[key] = value;

        const ChatResponse response = llm_->Chat(chat);
        out.usage = response.usage;
        if (!response.model.empty()) out.model = response.model;
        if (!response.error.IsOk()) {
            out.error = response.error;
            return out;
        }
        if (response.finishReason == FinishReason::ContentFiltered) {
            out.error.code = ErrorCode::ContentFiltered;
            out.error.message = "Claude declined to read this image";
            return out;
        }
        out.ocrText = TextOf(response.text);
        if (response.finishReason == FinishReason::Length) {
            out.error.code = ErrorCode::ProviderError;
            out.error.message = "The transcription was cut off (max_tokens)";
        }
        return out;
    }

    std::future<VisionAnalyzeResponse> AnalyzeAsync(const VisionAnalyzeRequest& request) override {
        return std::async(std::launch::async, [this, request] { return Analyze(request); });
    }

private:
    // The structured output's "text"; a reply that is not that JSON (a
    // configuration that replaced output_config) is the text itself.
    static std::string TextOf(const std::string& reply) {
        const json j = json::parse(reply, nullptr, /*allow_exceptions=*/false);
        if (j.is_object() && j.contains("text") && j["text"].is_string()) return j["text"].get<std::string>();
        return reply;
    }

    VisionAnalyzerConfig config_;
    std::unique_ptr<ITextLLM> llm_;
};

} // namespace

std::unique_ptr<IVisionAnalyzer> CreateAnthropicVisionAnalyzer(
    const VisionAnalyzerConfig& config,
    Error* outError,
    std::shared_ptr<ITransport> transport) {
    TextLLMConfig llmConfig = config;   // the same ProviderConfig
    if (llmConfig.defaultModel.empty()) llmConfig.defaultModel = kDefaultModel;
    // Built by the default route (UltraAITextRecognition.h) the config is
    // empty: the key is then the canonical UltraVault entry.
    if (llmConfig.apiKey.empty() && llmConfig.apiKeyVaultRef.empty()) llmConfig.apiKeyVaultRef = kVaultRef;
    // The adapter adds its own output_config / fallbacks per request; the
    // text adapter applies the configured options first and the request's
    // after them, so the configured ones the header promises win only when
    // the request leaves them out - which Analyze does.
    std::unique_ptr<ITextLLM> llm = CreateAnthropicTextLLM(llmConfig, outError, std::move(transport));
    if (!llm) return nullptr;
    return std::make_unique<AnthropicVisionAnalyzer>(config, std::move(llm));
}

} // namespace UltraAI
