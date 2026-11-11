// UltraAI/adapters/openai/src/OpenAITextLLM.cpp
// OpenAI (Chat Completions) ITextLLM adapter: request serialization,
// response parsing, SSE chunk assembly, error mapping. The same wire format
// is served by self-hosted OpenAI-compatible servers (Ollama, vLLM,
// llama.cpp server), which the adapter supports via a custom baseUrl.
// Version: 0.2.0 - the Batch API (ITextBatch)
// Version: 0.1.0
// Last Modified: 2026-11-09
// Author: UltraAI Module

#include "UltraAIOpenAI.h"

#include "OpenAIInternal.h"
#include "UltraAIBase64.h"
#include "UltraAIMultipart.h"
#include "UltraAIStreamHandleBase.h"
#ifdef ULTRAAI_HAS_ULTRANET
#include "UltraAIUltraNetTransport.h"
#endif

#include <algorithm>
#include <ctime>
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
// Request serialization
// ---------------------------------------------------------------------

json ContentPartToJson(const ContentPart& part) {
    if (const auto* t = std::get_if<TextPart>(&part)) {
        return json{{"type", "text"}, {"text", t->text}};
    }
    if (const auto* img = std::get_if<ImagePart>(&part)) {
        json url;
        url["url"] = !img->bytes.empty() ? Base64DataUrl(img->mimeType, img->bytes)
                                         : img->url;
        if (!img->detail.empty()) url["detail"] = img->detail;
        return json{{"type", "image_url"}, {"image_url", std::move(url)}};
    }
    const auto& f = std::get<FilePart>(part);
    if (!f.bytes.empty()) {
        json file;
        file["filename"] = f.filename.empty() ? "document.pdf" : f.filename;
        file["file_data"] = Base64DataUrl(
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
            case ToolChoice::NoTool:     body["tool_choice"] = "none"; break;
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
            if (state.streamError.code == ErrorCode::Ok ||
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

// ---------------------------------------------------------------------
// Batch API: the requests go up as a JSONL file (POST /v1/files, purpose
// "batch"), a batch is made from it (POST /v1/batches), and the answers
// come back as JSONL files (output_file_id, error_file_id). Each line's
// body is the same body Chat sends; each answer is the same reply Chat
// parses.
// ---------------------------------------------------------------------

std::string UnixToRfc3339(const json& j, const char* key) {
    if (!j.contains(key) || !j[key].is_number()) return {};
    const std::time_t t = static_cast<std::time_t>(j[key].get<int64_t>());
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &t);
#else
    gmtime_r(&t, &utc);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
}

class OpenAITextBatch : public ITextBatch {
public:
    OpenAITextBatch(TextBatchConfig config, std::shared_ptr<ITransport> transport)
        : config_(std::move(config)), transport_(std::move(transport)),
          baseUrl_(NormalizeBaseUrl(config_.baseUrl)) {}

    std::string ProviderId() const override { return "openai"; }
    bool IsNative() const override { return true; }

    BatchJob Submit(const std::vector<BatchRequestItem>& items) override {
        BatchJob out;
        if (!ValidateBatchItems(items, &out.error)) return out;

        std::string jsonl;
        for (const auto& item : items) {
            json body = BuildRequestBody(item.request, config_, &out.error);
            if (body.is_null()) {
                out.error.message = "request '" + item.customId + "': " + out.error.message;
                return out;
            }
            jsonl += json{{"custom_id", item.customId}, {"method", "POST"},
                          {"url", "/v1/chat/completions"}, {"body", std::move(body)}}.dump();
            jsonl += '\n';
        }

        // 1. The file.
        MultipartBody upload = BuildMultipartBody({
            MultipartPart{"purpose", "batch", "", ""},
            MultipartPart{"file", jsonl, "ultraai-batch.jsonl", "application/jsonl"}});
        TransportResponse fileResp;
        if (!Send("POST", "/v1/files", upload.body, upload.contentType, fileResp, &out.error)) return out;
        json file = ParseJsonLenient(fileResp.body);
        const std::string fileId = file.is_object() ? file.value("id", "") : std::string{};
        if (fileId.empty()) {
            out.error.code = ErrorCode::ProviderError;
            out.error.message = "the file upload returned no file id";
            return out;
        }

        // 2. The batch.
        json create = {{"input_file_id", fileId}, {"endpoint", "/v1/chat/completions"},
                       {"completion_window", "24h"}};
        return JobFrom("POST", "/v1/batches", create.dump());
    }

    BatchJob Get(const std::string& batchId) override {
        return JobFrom("GET", "/v1/batches/" + batchId, "");
    }

    BatchJob Cancel(const std::string& batchId) override {
        return JobFrom("POST", "/v1/batches/" + batchId + "/cancel", "");
    }

    std::vector<BatchItemResult> Results(const std::string& batchId, Error* outError) override {
        Error tmp;
        Error* err = outError ? outError : &tmp;
        json raw;
        BatchJob job = Fetch("GET", "/v1/batches/" + batchId, "", &raw);
        if (!job.error.IsOk() && !IsFinished(job.state)) { *err = job.error; return {}; }
        if (!IsFinished(job.state)) {
            err->code = ErrorCode::InvalidRequest;
            err->message = "the batch has not finished";
            return {};
        }
        std::vector<BatchItemResult> results;
        for (const char* key : {"output_file_id", "error_file_id"}) {
            if (!raw.contains(key) || !raw[key].is_string()) continue;
            const std::string fileId = raw[key];
            if (fileId.empty()) continue;
            TransportResponse resp;
            if (!Send("GET", "/v1/files/" + fileId + "/content", "", "", resp, err)) return {};
            auto part = ParseResults(resp.body);
            results.insert(results.end(), std::make_move_iterator(part.begin()),
                           std::make_move_iterator(part.end()));
        }
        return results;
    }

    static std::vector<BatchItemResult> ParseResults(const std::string& jsonl) {
        std::vector<BatchItemResult> out;
        size_t start = 0;
        while (start < jsonl.size()) {
            size_t end = jsonl.find('\n', start);
            if (end == std::string::npos) end = jsonl.size();
            json j = ParseJsonLenient(jsonl.substr(start, end - start));
            start = end + 1;
            if (!j.is_object()) continue;
            BatchItemResult r;
            r.customId = j.value("custom_id", "");
            const json response = j.value("response", json::object());
            const int status = response.is_object() ? response.value("status_code", 0) : 0;
            const json body = response.is_object() ? response.value("body", json::object()) : json::object();
            if (status >= 200 && status < 300) {
                r.status = BatchItemStatus::Succeeded;
                r.response = ParseChatResponse(body.dump());
                if (!r.response.error.IsOk()) r.status = BatchItemStatus::Failed;
            } else {
                std::string code, message;
                if (j.contains("error") && j["error"].is_object()) {
                    code = j["error"].value("code", "");
                    message = j["error"].value("message", "");
                }
                if (code == "batch_expired") {
                    r.status = BatchItemStatus::Expired;
                    r.response.error.code = ErrorCode::Timeout;
                    r.response.error.message = "the batch expired before this request ran";
                } else if (code == "batch_cancelled") {
                    r.status = BatchItemStatus::Cancelled;
                    r.response.error.code = ErrorCode::Cancelled;
                    r.response.error.message = "the batch was cancelled before this request ran";
                } else {
                    r.status = BatchItemStatus::Failed;
                    r.response.error = status > 0 ? MapOpenAIError(status, body.dump())
                                                  : MapHttpStatus(500, message);
                    // The provider's own message, without "HTTP 400: ".
                    if (body.contains("error") && body["error"].is_object()) {
                        const std::string bodyMessage = body["error"].value("message", "");
                        if (!bodyMessage.empty()) message = bodyMessage;
                    }
                    r.response.error.message = message.empty() ? "the request failed" : message;
                    if (!code.empty() && r.response.error.providerCode.empty()) {
                        r.response.error.providerCode = code;
                    }
                }
                r.response.finishReason = FinishReason::Error;
            }
            out.push_back(std::move(r));
        }
        return out;
    }

    static BatchJob ParseJob(const json& j) {
        BatchJob job;
        job.id = j.value("id", "");
        job.createdAt = UnixToRfc3339(j, "created_at");
        job.expiresAt = UnixToRfc3339(j, "expires_at");
        for (const char* key : {"completed_at", "failed_at", "expired_at", "cancelled_at"}) {
            std::string t = UnixToRfc3339(j, key);
            if (!t.empty()) { job.endedAt = t; break; }
        }
        const json c = j.value("request_counts", json::object());
        if (c.is_object()) {
            job.counts.total     = c.value("total", 0);
            job.counts.succeeded = c.value("completed", 0);
            job.counts.failed    = c.value("failed", 0);
        }
        job.counts.processing = std::max(0, job.counts.total - job.counts.succeeded - job.counts.failed);
        const std::string status = j.value("status", "");
        if (status == "validating")       job.state = BatchState::Validating;
        else if (status == "in_progress") job.state = BatchState::InProgress;
        else if (status == "finalizing")  job.state = BatchState::Finalizing;
        else if (status == "cancelling")  job.state = BatchState::Cancelling;
        else if (status == "completed")   job.state = BatchState::Completed;
        else if (status == "cancelled")   job.state = BatchState::Cancelled;
        else if (status == "expired")     job.state = BatchState::Expired;
        else {
            job.state = BatchState::Failed;
            job.error.code = status == "failed" ? ErrorCode::InvalidRequest : ErrorCode::ProviderError;
            job.error.message = status == "failed" ? "the provider refused the batch"
                                                   : "unknown batch status '" + status + "'";
            // {"errors":{"data":[{"code","message","line"}]}}
            if (j.contains("errors") && j["errors"].is_object()) {
                const json data = j["errors"].value("data", json::array());
                if (data.is_array() && !data.empty() && data[0].is_object()) {
                    job.error.message = data[0].value("message", job.error.message);
                    job.error.providerCode = data[0].value("code", "");
                }
            }
        }
        if (IsFinished(job.state) && job.state != BatchState::Failed) {
            // Unanswered requests of a cancelled or expired batch.
            if (job.state == BatchState::Cancelled) job.counts.cancelled = job.counts.processing;
            if (job.state == BatchState::Expired)   job.counts.expired = job.counts.processing;
            job.counts.processing = 0;
        }
        return job;
    }

private:
    BatchJob JobFrom(const std::string& method, const std::string& path, const std::string& body) {
        return Fetch(method, path, body, nullptr);
    }

    BatchJob Fetch(const std::string& method, const std::string& path, const std::string& body,
                   json* outRaw) {
        BatchJob out;
        TransportResponse resp;
        if (!Send(method, path, body, body.empty() ? "" : "application/json", resp, &out.error)) {
            return out;
        }
        json j = ParseJsonLenient(resp.body);
        if (!j.is_object()) {
            out.error.code = ErrorCode::ProviderError;
            out.error.message = "OpenAI batch response is not a JSON object";
            return out;
        }
        if (outRaw) *outRaw = j;
        return ParseJob(j);
    }

    bool Send(const std::string& method, const std::string& path, const std::string& body,
              const std::string& contentType, TransportResponse& out, Error* outError) {
        std::string key;
        {
            std::lock_guard<std::mutex> lock(keyMutex_);
            if (!keyResolved_) {
                if (!ResolveOptionalKey(config_, baseUrl_, resolvedKey_, outError)) return false;
                keyResolved_ = true;
            }
            key = resolvedKey_;
        }
        TransportRequest net;
        net.method = method;
        net.url = baseUrl_ + path;
        net.timeoutMs = config_.timeoutMs;
        if (!contentType.empty()) net.headers.push_back({"content-type", contentType});
        if (!key.empty()) net.headers.push_back({"authorization", "Bearer " + key});
        net.body = body;
        Error transportError;
        out = transport_->Request(net, &transportError);
        if (!transportError.IsOk()) { if (outError) *outError = transportError; return false; }
        if (out.statusCode >= 400) {
            if (outError) *outError = MapOpenAIError(out.statusCode, out.body);
            return false;
        }
        return true;
    }

    TextBatchConfig config_;
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

std::unique_ptr<ITextBatch> CreateOpenAITextBatch(
    const TextBatchConfig& config,
    Error* outError,
    std::shared_ptr<ITransport> transport) {
    // Only the hosted API has the Batch API; an OpenAI-compatible server
    // (Ollama, vLLM, llama.cpp server) does not. CreateTextBatch then falls
    // back to sending its requests one at a time.
    if (NormalizeBaseUrl(config.baseUrl) != kDefaultBaseUrl) {
        if (outError) {
            outError->code    = ErrorCode::UnsupportedFormat;
            outError->message = "the Batch API is only at api.openai.com, not at " + config.baseUrl;
        }
        return nullptr;
    }
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
    return std::make_unique<OpenAITextBatch>(config, std::move(transport));
}

} // namespace UltraAI
