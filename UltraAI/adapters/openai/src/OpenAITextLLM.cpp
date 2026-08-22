// UltraAI/adapters/openai/src/OpenAITextLLM.cpp
// OpenAI (Chat Completions) ITextLLM adapter: request serialization,
// response parsing, SSE chunk assembly, error mapping. The same wire format
// is served by self-hosted OpenAI-compatible servers (Ollama, vLLM,
// llama.cpp server), which the adapter supports via a custom baseUrl.
// Version: 0.1.0
// Last Modified: 2026-08-21
// Author: UltraAI Module

#include "UltraAIOpenAI.h"

#include "OpenAIInternal.h"
#include "UltraAIStreamHandleBase.h"
#ifdef ULTRAAI_HAS_ULTRANET
#include "UltraAIUltraNetTransport.h"
#endif

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
using namespace openai_detail;

// ---------------------------------------------------------------------
// base64 for inline media data URLs (RFC 4648, no line breaks).
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

std::string DataUrl(const std::string& mimeType,
                    const std::vector<uint8_t>& bytes) {
    return "data:" + mimeType + ";base64," + Base64Encode(bytes);
}

// ---------------------------------------------------------------------
// Request serialization
// ---------------------------------------------------------------------

json ContentPartToJson(const ContentPart& part) {
    if (const auto* t = std::get_if<TextPart>(&part)) {
        return json{{"type", "text"}, {"text", t->text}};
    }
    if (const auto* img = std::get_if<ImagePart>(&part)) {
        json url;
        url["url"] = !img->bytes.empty() ? DataUrl(img->mimeType, img->bytes)
                                         : img->url;
        if (!img->detail.empty()) url["detail"] = img->detail;
        return json{{"type", "image_url"}, {"image_url", std::move(url)}};
    }
    const auto& f = std::get<FilePart>(part);
    if (!f.bytes.empty()) {
        json file;
        file["filename"] = f.filename.empty() ? "document.pdf" : f.filename;
        file["file_data"] = DataUrl(
            f.mimeType.empty() ? "application/pdf" : f.mimeType, f.bytes);
        return json{{"type", "file"}, {"file", std::move(file)}};
    }
    // Chat Completions has no URL-referenced file block; degrade to text so
    // the model at least sees the reference instead of losing the part.
    return json{{"type", "text"}, {"text", f.url}};
}

json ToolCallsToJson(const std::vector<ToolCall>& calls) {
    json out = json::array();
    for (const auto& call : calls) {
        out.push_back(json{
            {"id", call.id},
            {"type", "function"},
            {"function", {{"name", call.name},
                          {"arguments", call.argumentsJson.empty()
                                            ? "{}" : call.argumentsJson}}}});
    }
    return out;
}

json MessageToJson(const Message& m) {
    switch (m.role) {
        case Role::System:
            return json{{"role", "system"}, {"content", m.text}};
        case Role::Tool: {
            json msg = {{"role", "tool"}, {"tool_call_id", m.toolCallId}};
            msg["content"] = m.text;
            return msg;
        }
        case Role::User:
        case Role::Assistant: {
            json msg;
            msg["role"] = m.role == Role::User ? "user" : "assistant";
            if (m.parts.empty()) {
                msg["content"] = m.text;
            } else {
                json content = json::array();
                if (!m.text.empty()) {
                    content.push_back(json{{"type", "text"}, {"text", m.text}});
                }
                for (const auto& part : m.parts) {
                    content.push_back(ContentPartToJson(part));
                }
                msg["content"] = std::move(content);
            }
            if (m.role == Role::Assistant && !m.toolCalls.empty()) {
                msg["tool_calls"] = ToolCallsToJson(m.toolCalls);
                if (m.text.empty()) msg.erase("content");
            }
            if (!m.name.empty()) msg["name"] = m.name;
            return msg;
        }
    }
    return json::object();
}

json BuildRequestBody(const ChatRequest& request, const TextLLMConfig& config,
                      Error* outError) {
    json body;
    const std::string model =
        !request.model.empty() ? request.model : config.defaultModel;
    if (model.empty()) {
        if (outError) {
            outError->code    = ErrorCode::InvalidRequest;
            outError->message = "no model specified: set ChatRequest::model "
                                "or ProviderConfig::defaultModel";
        }
        return json();
    }
    body["model"] = model;

    json messages = json::array();
    for (const auto& m : request.messages) {
        messages.push_back(MessageToJson(m));
    }
    body["messages"] = std::move(messages);

    const auto& s = request.sampling;
    if (s.temperature)      body["temperature"] = *s.temperature;
    if (s.topP)             body["top_p"]       = *s.topP;
    // The current parameter name; OpenAI-compatible servers (vLLM,
    // llama.cpp server) accept it as an alias of the legacy max_tokens.
    if (s.maxOutputTokens)  body["max_completion_tokens"] = *s.maxOutputTokens;
    if (s.seed)             body["seed"] = *s.seed;
    if (s.presencePenalty)  body["presence_penalty"]  = *s.presencePenalty;
    if (s.frequencyPenalty) body["frequency_penalty"] = *s.frequencyPenalty;
    if (!s.stopSequences.empty()) body["stop"] = s.stopSequences;

    if (!request.tools.empty()) {
        json tools = json::array();
        for (const auto& t : request.tools) {
            json schema = ParseJsonLenient(t.parametersJsonSchema);
            if (schema.is_discarded()) schema = json{{"type", "object"}};
            tools.push_back(json{
                {"type", "function"},
                {"function", {{"name", t.name},
                              {"description", t.description},
                              {"parameters", std::move(schema)}}}});
        }
        body["tools"] = std::move(tools);
        switch (request.toolChoice) {
            case ToolChoice::Auto:     break;   // API default
            case ToolChoice::None:     body["tool_choice"] = "none"; break;
            case ToolChoice::Required: body["tool_choice"] = "required"; break;
            case ToolChoice::Specific:
                body["tool_choice"] = {
                    {"type", "function"},
                    {"function", {{"name", request.forcedToolName}}}};
                break;
        }
    }

    if (request.responseFormat == ResponseFormat::JsonObject) {
        body["response_format"] = {{"type", "json_object"}};
    } else if (request.responseFormat == ResponseFormat::JsonSchema) {
        json schema = ParseJsonLenient(request.jsonSchema);
        if (schema.is_discarded()) schema = json{{"type", "object"}};
        body["response_format"] = {
            {"type", "json_schema"},
            {"json_schema", {{"name", "response"},
                             {"schema", std::move(schema)}}}};
    }

    ApplyOptions(body, config.providerOptions);
    ApplyOptions(body, request.options);
    return body;
}

// ---------------------------------------------------------------------
// Response parsing
// ---------------------------------------------------------------------

FinishReason MapFinishReason(const std::string& reason) {
    if (reason == "stop")           return FinishReason::Stop;
    if (reason == "length")         return FinishReason::Length;
    if (reason == "tool_calls" || reason == "function_call")
        return FinishReason::ToolCalls;
    if (reason == "content_filter") return FinishReason::ContentFiltered;
    return FinishReason::Unknown;
}

ChatResponse ParseChatResponse(const std::string& body) {
    ChatResponse out;
    json j = ParseJsonLenient(body);
    if (!j.is_object()) {
        out.error.code    = ErrorCode::ProviderError;
        out.error.message = "OpenAI response is not a JSON object";
        return out;
    }
    out.id    = j.value("id", "");
    out.model = j.value("model", "");
    if (j.contains("choices") && j["choices"].is_array() &&
        !j["choices"].empty()) {
        const json& choice = j["choices"][0];
        const json& msg = choice.value("message", json::object());
        if (msg.contains("content") && msg["content"].is_string()) {
            out.text = msg["content"];
        }
        if (msg.contains("refusal") && msg["refusal"].is_string() &&
            out.text.empty()) {
            out.text = msg["refusal"];
            out.finishReason = FinishReason::ContentFiltered;
        }
        if (msg.contains("tool_calls") && msg["tool_calls"].is_array()) {
            for (const auto& tc : msg["tool_calls"]) {
                ToolCall call;
                call.id = tc.value("id", "");
                const json& fn = tc.value("function", json::object());
                call.name          = fn.value("name", "");
                call.argumentsJson = fn.value("arguments", "");
                if (call.argumentsJson.empty()) call.argumentsJson = "{}";
                out.toolCalls.push_back(std::move(call));
            }
        }
        if (out.finishReason == FinishReason::Unknown) {
            out.finishReason = MapFinishReason(choice.value("finish_reason", ""));
        }
    } else {
        out.error.code    = ErrorCode::ProviderError;
        out.error.message = "OpenAI response has no choices";
        return out;
    }
    if (j.contains("usage")) ParseUsage(j["usage"], out.usage);
    return out;
}

// ---------------------------------------------------------------------
// The adapter
// ---------------------------------------------------------------------

class OpenAITextLLM : public ITextLLM {
public:
    OpenAITextLLM(TextLLMConfig config, std::shared_ptr<ITransport> transport)
        : config_(std::move(config)), transport_(std::move(transport)),
          baseUrl_(NormalizeBaseUrl(config_.baseUrl)) {}

    ProviderCapabilities GetCapabilities() const override {
        ProviderCapabilities caps;
        caps.providerId         = "openai";
        caps.supportsStreaming  = true;
        caps.supportsTools      = true;
        caps.supportsVision     = true;
        caps.supportsEmbeddings = true;   // served by CreateOpenAIEmbeddings
        auto add = [&caps](const char* id, const char* name, int32_t ctx,
                           int32_t maxOut) {
            ModelInfo m;
            m.id = id; m.displayName = name;
            m.contextWindow = ctx; m.maxOutputTokens = maxOut;
            m.supportsVision = m.supportsTools = m.supportsStreaming =
                m.supportsJsonSchema = true;
            caps.models.push_back(std::move(m));
        };
        add("gpt-5",      "GPT-5",      400000, 128000);
        add("gpt-5-mini", "GPT-5 mini", 400000, 128000);
        add("gpt-4.1",    "GPT-4.1",   1000000,  32768);
        add("gpt-4o",     "GPT-4o",     128000,  16384);
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
            out.error = MapOpenAIError(resp.statusCode, resp.body);
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
                DispatchSseChunk(sse, *state, onEvent);
            },
            [handle, state, onEvent](const Error& error, int statusCode) {
                StreamEvent ev;
                if (handle->IsCancelled()) {
                    ev.kind          = StreamEventKind::Error;
                    ev.error.code    = ErrorCode::Cancelled;
                    ev.error.message = "stream cancelled";
                } else if (!error.IsOk()) {
                    ev.kind  = StreamEventKind::Error;
                    ev.error = error;
                } else if (!state->streamError.IsOk()) {
                    ev.kind  = StreamEventKind::Error;
                    ev.error = state->streamError;
                } else {
                    // Chat Completions has no per-call end marker: finalize
                    // any assembled tool calls before the terminal event.
                    for (auto& [index, call] : state->toolCalls) {
                        if (call.argumentsJson.empty()) call.argumentsJson = "{}";
                        StreamEvent end;
                        end.kind          = StreamEventKind::ToolCallEnd;
                        end.toolCallDelta = call;
                        onEvent(end);
                    }
                    state->toolCalls.clear();
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
        // No token-counting endpoint; rough estimate at ~4 bytes/token.
        (void)model;
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
        std::map<int, ToolCall> toolCalls;   // choice-delta index -> call
        FinishReason finish = FinishReason::Unknown;
        TokenUsage usage;
        Error streamError;
    };

    // One Chat Completions stream chunk: {"choices":[{"delta":{...},
    // "finish_reason":...}],"usage":...}; the final sentinel is the literal
    // data payload "[DONE]".
    static void DispatchSseChunk(const TransportSseEvent& sse, SseState& state,
                                 const StreamCallback& onEvent) {
        if (sse.data == "[DONE]") return;
        json j = ParseJsonLenient(sse.data);
        if (!j.is_object()) return;

        if (j.contains("error") && j["error"].is_object()) {
            state.streamError = MapOpenAIError(0, sse.data);
            if (state.streamError.code == ErrorCode::None ||
                state.streamError.code == ErrorCode::Unknown) {
                state.streamError.code = ErrorCode::ProviderError;
            }
            return;
        }

        if (j.contains("usage") && j["usage"].is_object()) {
            ParseUsage(j["usage"], state.usage);
            StreamEvent ev;
            ev.kind  = StreamEventKind::UsageUpdate;
            ev.usage = state.usage;
            onEvent(ev);
        }

        if (!j.contains("choices") || !j["choices"].is_array() ||
            j["choices"].empty()) {
            return;
        }
        const json& choice = j["choices"][0];
        const json& delta  = choice.value("delta", json::object());

        if (delta.contains("content") && delta["content"].is_string()) {
            const std::string text = delta["content"];
            if (!text.empty()) {
                StreamEvent ev;
                ev.kind      = StreamEventKind::TextDelta;
                ev.textDelta = text;
                onEvent(ev);
            }
        }

        if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
            for (const auto& tc : delta["tool_calls"]) {
                const int index = tc.value("index", 0);
                ToolCall& call = state.toolCalls[index];
                if (tc.contains("id") && tc["id"].is_string()) {
                    call.id = tc["id"];
                }
                const json& fn = tc.value("function", json::object());
                if (fn.contains("name") && fn["name"].is_string()) {
                    call.name = fn["name"];
                }
                std::string partial;
                if (fn.contains("arguments") && fn["arguments"].is_string()) {
                    partial = fn["arguments"];
                    call.argumentsJson += partial;
                }
                StreamEvent ev;
                ev.kind                        = StreamEventKind::ToolCallDelta;
                ev.toolCallDelta.id            = call.id;
                ev.toolCallDelta.name          = call.name;
                ev.toolCallDelta.argumentsJson = partial;
                onEvent(ev);
            }
        }

        if (choice.contains("finish_reason") &&
            choice["finish_reason"].is_string()) {
            state.finish = MapFinishReason(choice["finish_reason"]);
        }
    }

    bool PrepareRequest(const ChatRequest& request, bool stream,
                        TransportRequest& outNet, Error* outError) {
        json body = BuildRequestBody(request, config_, outError);
        if (body.is_null()) return false;
        if (stream) {
            body["stream"] = true;
            body["stream_options"] = {{"include_usage", true}};
        }
        std::string key;
        if (!ResolveKey(key, outError)) return false;

        outNet.method    = "POST";
        outNet.url       = baseUrl_ + "/v1/chat/completions";
        outNet.timeoutMs = config_.timeoutMs;
        outNet.headers   = {{"content-type", "application/json"}};
        if (!key.empty()) {
            outNet.headers.push_back({"authorization", "Bearer " + key});
        }
        outNet.body = body.dump();
        return true;
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

    TextLLMConfig config_;
    std::shared_ptr<ITransport> transport_;
    std::string baseUrl_;
    std::mutex keyMutex_;
    std::string resolvedKey_;
    bool keyResolved_ = false;
};

} // namespace

std::unique_ptr<ITextLLM> CreateOpenAITextLLM(
    const TextLLMConfig& config,
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
    return std::make_unique<OpenAITextLLM>(config, std::move(transport));
}

} // namespace UltraAI
