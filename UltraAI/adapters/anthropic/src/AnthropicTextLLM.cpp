// UltraAI/adapters/anthropic/src/AnthropicTextLLM.cpp
// Anthropic (Claude) ITextLLM adapter: Messages API serialization, response
// parsing, SSE stream-event assembly, token counting, error mapping.
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module

#include "UltraAIAnthropicTextLLM.h"

#include "UltraAICredentials.h"
#include "UltraAIHttpError.h"
#include "UltraAIStreamHandleBase.h"
#ifdef ULTRAAI_HAS_ULTRANET
#include "UltraAIUltraNetTransport.h"
#endif

#include <nlohmann/json.hpp>

#include <cstdint>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace UltraAI {

namespace {

using nlohmann::json;

constexpr const char* kDefaultBaseUrl  = "https://api.anthropic.com";
constexpr const char* kDefaultModel    = "claude-opus-5";
constexpr const char* kApiVersion      = "2023-06-01";
constexpr int32_t     kDefaultMaxTokens = 16000;

// ---------------------------------------------------------------------
// base64 for inline media (RFC 4648, no line breaks — the API rejects
// base64 containing newlines).
// ---------------------------------------------------------------------
std::string Base64Encode(const std::vector<uint8_t>& bytes) {
    static const char* kAlphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        uint32_t n = (bytes[i] << 16) | (bytes[i + 1] << 8) | bytes[i + 2];
        out += kAlphabet[(n >> 18) & 63]; out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];  out += kAlphabet[n & 63];
    }
    if (i + 1 == bytes.size()) {
        uint32_t n = bytes[i] << 16;
        out += kAlphabet[(n >> 18) & 63]; out += kAlphabet[(n >> 12) & 63];
        out += "==";
    } else if (i + 2 == bytes.size()) {
        uint32_t n = (bytes[i] << 16) | (bytes[i + 1] << 8);
        out += kAlphabet[(n >> 18) & 63]; out += kAlphabet[(n >> 12) & 63];
        out += kAlphabet[(n >> 6) & 63];  out += '=';
    }
    return out;
}

json ParseJsonLenient(const std::string& text) {
    return json::parse(text, nullptr, /*allow_exceptions=*/false);
}

// ---------------------------------------------------------------------
// Request serialization
// ---------------------------------------------------------------------

json ContentPartToJson(const ContentPart& part) {
    if (const auto* t = std::get_if<TextPart>(&part)) {
        return json{{"type", "text"}, {"text", t->text}};
    }
    if (const auto* img = std::get_if<ImagePart>(&part)) {
        json source;
        if (!img->bytes.empty()) {
            source = {{"type", "base64"},
                      {"media_type", img->mimeType},
                      {"data", Base64Encode(img->bytes)}};
        } else {
            source = {{"type", "url"}, {"url", img->url}};
        }
        return json{{"type", "image"}, {"source", std::move(source)}};
    }
    const auto& f = std::get<FilePart>(part);
    json source;
    if (!f.bytes.empty()) {
        source = {{"type", "base64"},
                  {"media_type", f.mimeType.empty() ? "application/pdf"
                                                    : f.mimeType},
                  {"data", Base64Encode(f.bytes)}};
    } else {
        source = {{"type", "url"}, {"url", f.url}};
    }
    return json{{"type", "document"}, {"source", std::move(source)}};
}

json MessageContentToJson(const Message& m) {
    json blocks = json::array();
    if (!m.text.empty()) {
        blocks.push_back(json{{"type", "text"}, {"text", m.text}});
    }
    for (const auto& part : m.parts) {
        blocks.push_back(ContentPartToJson(part));
    }
    for (const auto& call : m.toolCalls) {
        json input = ParseJsonLenient(call.argumentsJson);
        if (input.is_discarded()) input = json::object();
        blocks.push_back(json{{"type", "tool_use"},
                              {"id", call.id},
                              {"name", call.name},
                              {"input", std::move(input)}});
    }
    return blocks;
}

// Apply an OptionsMap as top-level request fields. String values that parse
// as JSON are embedded structurally (escape hatch for nested config like
// "thinking"); "anthropic-beta" is handled by the caller as a header.
void ApplyOptions(json& body, const OptionsMap& options) {
    for (const auto& [key, value] : options) {
        if (key == "anthropic-beta") continue;
        if (const auto* b = std::get_if<bool>(&value))          body[key] = *b;
        else if (const auto* i = std::get_if<int64_t>(&value))  body[key] = *i;
        else if (const auto* d = std::get_if<double>(&value))   body[key] = *d;
        else if (const auto* s = std::get_if<std::string>(&value)) {
            json parsed = ParseJsonLenient(*s);
            if (!parsed.is_discarded() && (parsed.is_object() || parsed.is_array())) {
                body[key] = std::move(parsed);
            } else {
                body[key] = *s;
            }
        }
    }
}

std::string FindOptionString(const OptionsMap& options, const std::string& key) {
    auto it = options.find(key);
    if (it == options.end()) return {};
    const auto* s = std::get_if<std::string>(&it->second);
    return s ? *s : std::string{};
}

json BuildRequestBody(const ChatRequest& request, const TextLLMConfig& config) {
    json body;
    body["model"] = !request.model.empty()       ? request.model
                    : !config.defaultModel.empty() ? config.defaultModel
                                                   : kDefaultModel;
    body["max_tokens"] = request.sampling.maxOutputTokens.value_or(kDefaultMaxTokens);

    // System messages lift into the top-level `system` field.
    std::string system;
    json messages = json::array();
    for (const auto& m : request.messages) {
        switch (m.role) {
            case Role::System:
                if (!system.empty()) system += "\n\n";
                system += m.text;
                break;
            case Role::Tool: {
                json result = {{"type", "tool_result"},
                               {"tool_use_id", m.toolCallId}};
                if (!m.text.empty()) result["content"] = m.text;
                messages.push_back(json{{"role", "user"},
                                        {"content", json::array({std::move(result)})}});
                break;
            }
            case Role::User:
            case Role::Assistant: {
                json content = MessageContentToJson(m);
                // Plain single-text messages serialize as a bare string.
                if (content.size() == 1 && m.parts.empty() && m.toolCalls.empty()) {
                    messages.push_back(json{
                        {"role", m.role == Role::User ? "user" : "assistant"},
                        {"content", m.text}});
                } else {
                    messages.push_back(json{
                        {"role", m.role == Role::User ? "user" : "assistant"},
                        {"content", std::move(content)}});
                }
                break;
            }
        }
    }
    if (!system.empty()) body["system"] = system;
    body["messages"] = std::move(messages);

    const auto& s = request.sampling;
    if (s.temperature)      body["temperature"] = *s.temperature;
    if (s.topP)             body["top_p"]       = *s.topP;
    if (!s.stopSequences.empty()) body["stop_sequences"] = s.stopSequences;
    // presence/frequency penalties and seed have no Messages API equivalent
    // and are intentionally dropped.

    if (!request.tools.empty()) {
        json tools = json::array();
        for (const auto& t : request.tools) {
            json schema = ParseJsonLenient(t.parametersJsonSchema);
            if (schema.is_discarded()) schema = json{{"type", "object"}};
            tools.push_back(json{{"name", t.name},
                                 {"description", t.description},
                                 {"input_schema", std::move(schema)}});
        }
        body["tools"] = std::move(tools);
        switch (request.toolChoice) {
            case ToolChoice::Auto:     break;   // API default
            case ToolChoice::None:     body["tool_choice"] = {{"type", "none"}}; break;
            case ToolChoice::Required: body["tool_choice"] = {{"type", "any"}}; break;
            case ToolChoice::Specific:
                body["tool_choice"] = {{"type", "tool"},
                                       {"name", request.forcedToolName}};
                break;
        }
    }

    if (request.responseFormat != ResponseFormat::Text) {
        json schema;
        if (request.responseFormat == ResponseFormat::JsonSchema) {
            schema = ParseJsonLenient(request.jsonSchema);
            if (schema.is_discarded()) schema = json{{"type", "object"}};
        } else {
            schema = json{{"type", "object"}};    // free-form JSON object
        }
        body["output_config"] = {{"format", {{"type", "json_schema"},
                                             {"schema", std::move(schema)}}}};
    }

    ApplyOptions(body, config.providerOptions);
    ApplyOptions(body, request.options);
    return body;
}

// ---------------------------------------------------------------------
// Response parsing
// ---------------------------------------------------------------------

FinishReason MapStopReason(const std::string& stopReason) {
    if (stopReason == "end_turn" || stopReason == "stop_sequence") return FinishReason::Stop;
    if (stopReason == "max_tokens") return FinishReason::Length;
    if (stopReason == "tool_use")   return FinishReason::ToolCalls;
    if (stopReason == "refusal")    return FinishReason::ContentFiltered;
    return FinishReason::Unknown;
}

void ParseUsage(const json& u, TokenUsage& out) {
    if (!u.is_object()) return;
    out.inputTokens       = u.value("input_tokens", out.inputTokens);
    out.outputTokens      = u.value("output_tokens", out.outputTokens);
    out.cachedInputTokens = u.value("cache_read_input_tokens", out.cachedInputTokens);
}

// Refine a generic HTTP error with the Anthropic error body:
// {"type":"error","error":{"type":"rate_limit_error","message":"..."}}
Error MapAnthropicError(int statusCode, const std::string& body) {
    json j = ParseJsonLenient(body);
    std::string type, message;
    if (j.is_object() && j.contains("error") && j["error"].is_object()) {
        type    = j["error"].value("type", "");
        message = j["error"].value("message", "");
    }
    Error e = MapHttpStatus(statusCode, message.empty() ? body.substr(0, 200)
                                                        : message);
    if (type == "authentication_error" || type == "permission_error")
        e.code = ErrorCode::AuthenticationFailed;
    else if (type == "rate_limit_error")
        e.code = ErrorCode::RateLimited;
    else if (type == "not_found_error")
        e.code = ErrorCode::ModelNotFound;
    else if (type == "invalid_request_error")
        e.code = ErrorCode::InvalidRequest;
    else if (type == "overloaded_error")
        e.code = ErrorCode::RateLimited;
    if (!type.empty()) e.providerCode = type;
    return e;
}

ChatResponse ParseChatResponse(const std::string& body) {
    ChatResponse out;
    json j = ParseJsonLenient(body);
    if (!j.is_object()) {
        out.error.code    = ErrorCode::ProviderError;
        out.error.message = "Anthropic response is not a JSON object";
        return out;
    }
    out.id    = j.value("id", "");
    out.model = j.value("model", "");
    if (j.contains("content") && j["content"].is_array()) {
        for (const auto& block : j["content"]) {
            const std::string type = block.value("type", "");
            if (type == "text") {
                out.text += block.value("text", "");
            } else if (type == "tool_use") {
                ToolCall call;
                call.id            = block.value("id", "");
                call.name          = block.value("name", "");
                call.argumentsJson = block.value("input", json::object()).dump();
                out.toolCalls.push_back(std::move(call));
            }
            // thinking / fallback / server-tool blocks are skipped.
        }
    }
    out.finishReason = MapStopReason(j.value("stop_reason", ""));
    if (j.contains("usage")) ParseUsage(j["usage"], out.usage);
    return out;
}

// ---------------------------------------------------------------------
// The adapter
// ---------------------------------------------------------------------

class AnthropicTextLLM : public ITextLLM {
public:
    AnthropicTextLLM(TextLLMConfig config, std::shared_ptr<ITransport> transport)
        : config_(std::move(config)), transport_(std::move(transport)) {
        baseUrl_ = config_.baseUrl.empty() ? kDefaultBaseUrl : config_.baseUrl;
        while (!baseUrl_.empty() && baseUrl_.back() == '/') baseUrl_.pop_back();
    }

    ProviderCapabilities GetCapabilities() const override {
        ProviderCapabilities caps;
        caps.providerId        = "anthropic";
        caps.supportsStreaming = true;
        caps.supportsTools     = true;
        caps.supportsVision    = true;
        auto add = [&caps](const char* id, const char* name, int32_t ctx,
                           int32_t maxOut) {
            ModelInfo m;
            m.id = id; m.displayName = name;
            m.contextWindow = ctx; m.maxOutputTokens = maxOut;
            m.supportsVision = m.supportsTools = m.supportsStreaming =
                m.supportsJsonSchema = true;
            caps.models.push_back(std::move(m));
        };
        add("claude-opus-5",    "Claude Opus 5",    1000000, 128000);
        add("claude-sonnet-5",  "Claude Sonnet 5",  1000000, 128000);
        add("claude-haiku-4-5", "Claude Haiku 4.5",  200000,  64000);
        return caps;
    }

    ChatResponse Chat(const ChatRequest& request) override {
        ChatResponse out;
        TransportRequest net;
        if (!PrepareRequest(request, /*stream=*/false, net, &out.error)) {
            return out;
        }
        Error transportError;
        TransportResponse resp = transport_->Request(net, &transportError);
        if (!transportError.IsOk()) {
            out.error = transportError;
            return out;
        }
        if (resp.statusCode >= 400) {
            out.error = MapAnthropicError(resp.statusCode, resp.body);
            return out;
        }
        return ParseChatResponse(resp.body);
    }

    std::future<ChatResponse> ChatAsync(const ChatRequest& request) override {
        return std::async(std::launch::async,
                          [this, request] { return Chat(request); });
    }

    StreamHandle ChatStream(const ChatRequest& request,
                            StreamCallback onEvent) override {
        auto handle = std::make_shared<StreamHandleBase>();

        TransportRequest net;
        Error prepError;
        if (!PrepareRequest(request, /*stream=*/true, net, &prepError)) {
            StreamEvent ev;
            ev.kind  = StreamEventKind::Error;
            ev.error = prepError;
            onEvent(ev);
            handle->MarkDone();
            return handle;
        }

        auto state = std::make_shared<SseState>();
        CancelFn cancel = transport_->SseStream(
            net,
            [handle, state, onEvent](const TransportSseEvent& sse) {
                if (handle->IsCancelled()) return;
                DispatchSseEvent(sse, *state, onEvent);
            },
            [handle, state, onEvent](const Error& error, int statusCode) {
                StreamEvent ev;
                if (handle->IsCancelled()) {
                    ev.kind       = StreamEventKind::Error;
                    ev.error.code = ErrorCode::Cancelled;
                    ev.error.message = "stream cancelled";
                } else if (!error.IsOk()) {
                    ev.kind  = StreamEventKind::Error;
                    ev.error = error;
                } else if (!state->streamError.IsOk()) {
                    ev.kind  = StreamEventKind::Error;
                    ev.error = state->streamError;
                } else {
                    ev.kind         = StreamEventKind::Done;
                    ev.finishReason = state->finish;
                    ev.usage        = state->usage;
                }
                (void)statusCode;
                onEvent(ev);
                handle->MarkDone();
            });
        handle->SetCancelHook(std::move(cancel));
        return handle;
    }

    int32_t CountTokens(const std::string& model,
                        const std::vector<Message>& messages) override {
        ChatRequest req;
        req.model    = model;
        req.messages = messages;
        json body = BuildRequestBody(req, config_);
        body.erase("max_tokens");

        TransportRequest net;
        Error err;
        if (BuildTransport(net, body, "/v1/messages/count_tokens", &err)) {
            Error transportError;
            TransportResponse resp = transport_->Request(net, &transportError);
            if (transportError.IsOk() && resp.statusCode < 400) {
                json j = ParseJsonLenient(resp.body);
                if (j.is_object() && j.contains("input_tokens")) {
                    return j.value("input_tokens", 0);
                }
            }
        }
        // Fallback: rough estimate at ~4 bytes/token.
        size_t bytes = 0;
        for (const auto& m : messages) {
            bytes += m.text.size();
            for (const auto& p : m.parts) {
                if (const auto* t = std::get_if<TextPart>(&p)) bytes += t->text.size();
            }
        }
        return static_cast<int32_t>(bytes / 4);
    }

private:
    struct SseState {
        std::map<int, ToolCall> toolBlocks;   // content index -> in-progress call
        FinishReason finish = FinishReason::Unknown;
        TokenUsage usage;
        Error streamError;
    };

    static void DispatchSseEvent(const TransportSseEvent& sse, SseState& state,
                                 const StreamCallback& onEvent) {
        json j = ParseJsonLenient(sse.data);
        if (!j.is_object()) return;
        const std::string type = j.value("type", sse.event);

        if (type == "message_start") {
            if (j.contains("message") && j["message"].contains("usage")) {
                ParseUsage(j["message"]["usage"], state.usage);
            }
        } else if (type == "content_block_start") {
            const json& block = j.value("content_block", json::object());
            if (block.value("type", "") == "tool_use") {
                ToolCall call;
                call.id   = block.value("id", "");
                call.name = block.value("name", "");
                state.toolBlocks[j.value("index", 0)] = call;

                StreamEvent ev;
                ev.kind          = StreamEventKind::ToolCallDelta;
                ev.toolCallDelta = std::move(call);
                onEvent(ev);
            }
        } else if (type == "content_block_delta") {
            const json& delta = j.value("delta", json::object());
            const std::string deltaType = delta.value("type", "");
            if (deltaType == "text_delta") {
                StreamEvent ev;
                ev.kind      = StreamEventKind::TextDelta;
                ev.textDelta = delta.value("text", "");
                onEvent(ev);
            } else if (deltaType == "input_json_delta") {
                const int index = j.value("index", 0);
                auto it = state.toolBlocks.find(index);
                if (it != state.toolBlocks.end()) {
                    const std::string partial = delta.value("partial_json", "");
                    it->second.argumentsJson += partial;

                    StreamEvent ev;
                    ev.kind                        = StreamEventKind::ToolCallDelta;
                    ev.toolCallDelta.id            = it->second.id;
                    ev.toolCallDelta.name          = it->second.name;
                    ev.toolCallDelta.argumentsJson = partial;
                    onEvent(ev);
                }
            }
            // thinking / signature deltas are skipped.
        } else if (type == "content_block_stop") {
            const int index = j.value("index", 0);
            auto it = state.toolBlocks.find(index);
            if (it != state.toolBlocks.end()) {
                if (it->second.argumentsJson.empty()) {
                    it->second.argumentsJson = "{}";
                }
                StreamEvent ev;
                ev.kind          = StreamEventKind::ToolCallEnd;
                ev.toolCallDelta = it->second;
                onEvent(ev);
                state.toolBlocks.erase(it);
            }
        } else if (type == "message_delta") {
            const json& delta = j.value("delta", json::object());
            if (delta.contains("stop_reason") && delta["stop_reason"].is_string()) {
                state.finish = MapStopReason(delta["stop_reason"]);
            }
            if (j.contains("usage")) {
                ParseUsage(j["usage"], state.usage);
                StreamEvent ev;
                ev.kind  = StreamEventKind::UsageUpdate;
                ev.usage = state.usage;
                onEvent(ev);
            }
        } else if (type == "error") {
            state.streamError = MapAnthropicError(0, sse.data);
            if (state.streamError.code == ErrorCode::None ||
                state.streamError.code == ErrorCode::Unknown) {
                state.streamError.code = ErrorCode::ProviderError;
            }
        }
        // message_stop and ping carry nothing actionable.
    }

    bool PrepareRequest(const ChatRequest& request, bool stream,
                        TransportRequest& outNet, Error* outError) {
        json body = BuildRequestBody(request, config_);
        if (stream) body["stream"] = true;
        return BuildTransport(outNet, body, "/v1/messages", outError,
                              &request.options);
    }

    bool BuildTransport(TransportRequest& net, const json& body,
                        const std::string& path, Error* outError,
                        const OptionsMap* requestOptions = nullptr) {
        const std::string key = ResolveKey(outError);
        if (key.empty()) return false;

        net.method    = "POST";
        net.url       = baseUrl_ + path;
        net.timeoutMs = config_.timeoutMs;
        net.headers   = {{"x-api-key", key},
                         {"anthropic-version", kApiVersion},
                         {"content-type", "application/json"}};
        std::string beta = FindOptionString(config_.providerOptions, "anthropic-beta");
        if (requestOptions) {
            std::string reqBeta = FindOptionString(*requestOptions, "anthropic-beta");
            if (!reqBeta.empty()) beta = reqBeta;
        }
        if (!beta.empty()) net.headers.push_back({"anthropic-beta", beta});
        net.body = body.dump();
        return true;
    }

    std::string ResolveKey(Error* outError) {
        std::lock_guard<std::mutex> lock(keyMutex_);
        if (resolvedKey_.empty()) {
            resolvedKey_ = ResolveApiKey(config_, outError);
        }
        return resolvedKey_;
    }

    TextLLMConfig config_;
    std::shared_ptr<ITransport> transport_;
    std::string baseUrl_;
    std::mutex keyMutex_;
    std::string resolvedKey_;
};

} // namespace

std::unique_ptr<ITextLLM> CreateAnthropicTextLLM(
    const TextLLMConfig& config,
    Error* outError,
    std::shared_ptr<ITransport> transport) {
    if (!transport) {
#ifdef ULTRAAI_HAS_ULTRANET
        transport = std::make_shared<UltraNetTransport>();
#else
        if (outError) {
            outError->code    = ErrorCode::NetworkError;
            outError->message = "Anthropic adapter needs a transport: build "
                                "with ULTRAAI_USE_ULTRANET=ON or inject one";
        }
        return nullptr;
#endif
    }
    return std::make_unique<AnthropicTextLLM>(config, std::move(transport));
}

} // namespace UltraAI
