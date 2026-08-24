// UltraAI/adapters/comfyui/src/ComfyUIRun.cpp
// Implementation of the ComfyUI run layer.
//
// Two ways to follow a running job. The WebSocket path opens /ws before
// submitting, so queue position, per-node progress and preview frames reach
// the caller live. The polling path asks /history until the prompt appears —
// it is what the blocking calls use, and the fallback when the socket drops.
// Frames that arrive before the prompt_id is known are queued rather than
// dropped: ComfyUI starts talking as soon as the socket opens.
// Version: 0.1.0
// Last Modified: 2026-08-24
// Author: UltraAI Module

#include "ComfyUIRun.h"

#include "UltraAIJobPoll.h"
#include "UltraAIMultipart.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace UltraAI {
namespace comfyui_detail {

namespace {

uint32_t ReadBigEndian32(const uint8_t* bytes) {
    return (static_cast<uint32_t>(bytes[0]) << 24) |
           (static_cast<uint32_t>(bytes[1]) << 16) |
           (static_cast<uint32_t>(bytes[2]) << 8) |
            static_cast<uint32_t>(bytes[3]);
}

// Binary WebSocket frames are an 8-byte header (event type, image format)
// followed by the encoded preview image.
bool ParsePreviewFrame(const std::vector<uint8_t>& frame, MediaBlob& out) {
    constexpr uint32_t kEventPreviewImage = 1;
    constexpr uint32_t kFormatPng = 2;
    if (frame.size() <= 8) return false;
    if (ReadBigEndian32(frame.data()) != kEventPreviewImage) return false;

    out.mimeType = ReadBigEndian32(frame.data() + 4) == kFormatPng
                       ? "image/png" : "image/jpeg";
    out.bytes.assign(frame.begin() + 8, frame.end());
    return true;
}

void Emit(const ComfyEmitFn& onEvent, ComfyEventKind kind, double progress) {
    if (!onEvent) return;
    ComfyEvent event;
    event.kind     = kind;
    event.progress = progress;
    onEvent(event);
}

// The name an upload is filed under. Uploads land in ComfyUI's shared input
// directory, so the caller's own file name is prefixed rather than used
// as-is: "photo.png" would otherwise collide with — and, with overwrite,
// replace — a file the user put there themselves.
std::string UploadName(const MediaBlob& blob, const std::string& extension) {
    static std::atomic<uint64_t> counter{0};
    std::string suffix;
    for (char c : blob.filename) {
        // Keep it recognisable in the input folder without letting a
        // caller-controlled string steer the path.
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' ||
            c == '_' || c == '.') {
            suffix += c;
        }
    }
    if (suffix.empty() || suffix.find('.') == std::string::npos) {
        suffix += extension;
    }
    return "ultraai-" + std::to_string(counter.fetch_add(1)) + "-" + suffix;
}

// POST /prompt. Returns the queued prompt_id.
bool Submit(const RunContext& ctx, const json& graph, std::string& outPromptId,
            Error* outError) {
    json body;
    body["prompt"]    = graph;
    body["client_id"] = ctx.clientId;

    TransportRequest net;
    net.method    = "POST";
    net.url       = ctx.baseUrl + "/prompt";
    net.timeoutMs = ctx.timeoutMs;
    net.headers   = Headers(ctx, /*json=*/true);
    net.body      = body.dump();

    Error transportError;
    TransportResponse resp = ctx.transport->Request(net, &transportError);
    if (!transportError.IsOk()) {
        if (outError) *outError = transportError;
        return false;
    }
    if (resp.statusCode >= 400) {
        if (outError) *outError = MapPromptError(resp.statusCode, resp.body);
        return false;
    }

    json parsed = ParseJsonLenient(resp.body);
    outPromptId = parsed.is_object() ? parsed.value("prompt_id", "")
                                     : std::string();
    if (outPromptId.empty()) {
        if (outError) {
            outError->code    = ErrorCode::ProviderError;
            outError->message = "ComfyUI accepted the graph but returned no "
                                "prompt_id";
        }
        return false;
    }
    return true;
}

// GET /history/{prompt_id}. `outFinished` is set when the prompt has left
// the queue, whether it produced files or failed.
bool HistoryOutputs(const RunContext& ctx, const std::string& promptId,
                    std::vector<OutputRef>& outRefs, bool& outFinished,
                    Error* outError) {
    outFinished = false;

    TransportRequest net;
    net.method    = "GET";
    net.url       = ctx.baseUrl + "/history/" + UrlEncode(promptId);
    net.timeoutMs = ctx.timeoutMs;
    net.headers   = Headers(ctx, /*json=*/false);

    Error transportError;
    TransportResponse resp = ctx.transport->Request(net, &transportError);
    if (!transportError.IsOk()) {
        if (outError) *outError = transportError;
        return false;
    }
    if (resp.statusCode >= 400) {
        if (outError) {
            *outError = MapHttpStatus(resp.statusCode,
                                      "reading generation history");
        }
        return false;
    }

    json parsed = ParseJsonLenient(resp.body);
    if (!parsed.is_object() || !parsed.contains(promptId)) {
        return true;   // still queued; not an error
    }

    const json& entry = parsed[promptId];
    if (entry.contains("outputs")) CollectOutputs(entry["outputs"], outRefs);

    // status.completed distinguishes "finished" from "still running with
    // partial outputs"; older builds only have the outputs object.
    if (entry.contains("status") && entry["status"].is_object()) {
        const json& status = entry["status"];
        if (status.value("status_str", "") == "error") {
            if (outError) {
                outError->code    = ErrorCode::ProviderError;
                outError->message = "ComfyUI reported an execution error; see "
                                    "the server log for the failing node";
            }
            outFinished = true;
            return false;
        }
        outFinished = status.value("completed", false) || !outRefs.empty();
    } else {
        outFinished = !outRefs.empty();
    }
    return true;
}

void Interrupt(const RunContext& ctx) {
    TransportRequest net;
    net.method    = "POST";
    net.url       = ctx.baseUrl + "/interrupt";
    net.timeoutMs = ctx.timeoutMs;
    net.headers   = Headers(ctx, /*json=*/true);
    net.body      = "{}";

    Error ignored;
    ctx.transport->Request(net, &ignored);
}

// ---------------------------------------------------------------------
// Progress: WebSocket
// ---------------------------------------------------------------------

struct WsStream {
    std::mutex mutex;
    std::condition_variable signal;
    std::deque<TransportWsMessage> frames;
    bool closed = false;
    Error closeError;
};

// What the frame stream told us about our prompt.
struct WsProgress {
    bool finished = false;          // execution ended successfully
    bool failed   = false;
    Error error;
    std::vector<OutputRef> outputs; // from "executed" events
};

// Interpret one text frame. Frames for another client's prompt are ignored;
// frames from older builds that carry no prompt_id are accepted.
void HandleTextFrame(const std::string& text, const std::string& promptId,
                     const ComfyEmitFn& onEvent, WsProgress& progress) {
    json frame = ParseJsonLenient(text);
    if (!frame.is_object()) return;

    const std::string type = frame.value("type", "");
    const json& data = frame.contains("data") && frame["data"].is_object()
                           ? frame["data"] : json::object();

    if (data.is_object() && data.contains("prompt_id") &&
        data["prompt_id"].is_string() &&
        data["prompt_id"].get<std::string>() != promptId) {
        return;
    }

    if (type == "progress") {
        const double value = data.value("value", 0.0);
        const double max   = data.value("max", 0.0);
        Emit(onEvent, ComfyEventKind::InProgress, max > 0.0 ? value / max : 0.0);
        return;
    }
    if (type == "executing") {
        // node == null marks the end of this prompt's execution.
        if (data.contains("node") && data["node"].is_null()) {
            progress.finished = true;
        } else {
            Emit(onEvent, ComfyEventKind::InProgress, 0.0);
        }
        return;
    }
    if (type == "executed") {
        if (data.contains("output")) {
            CollectOutputs(data["output"], progress.outputs);
        }
        return;
    }
    if (type == "execution_success") {
        progress.finished = true;
        return;
    }
    if (type == "execution_error") {
        progress.failed     = true;
        progress.error.code = ErrorCode::ProviderError;
        std::string message = data.value("exception_message", "");
        const std::string nodeType = data.value("node_type", "");
        if (!nodeType.empty()) message = nodeType + ": " + message;
        progress.error.message =
            message.empty() ? "ComfyUI execution failed" : message;
        return;
    }
    if (type == "execution_interrupted") {
        progress.failed        = true;
        progress.error.code    = ErrorCode::Cancelled;
        progress.error.message = "ComfyUI execution was interrupted";
    }
}

void HandleBinaryFrame(const TransportWsMessage& frame,
                       const ComfyEmitFn& onEvent) {
    if (!onEvent) return;
    ComfyEvent event;
    event.kind = ComfyEventKind::Preview;
    if (!ParsePreviewFrame(frame.bytes, event.preview)) return;
    onEvent(event);
}

bool RunWithPolling(const RunContext& ctx, const std::string& promptId,
                    const ComfyCancelledFn& isCancelled,
                    const ComfyEmitFn& onEvent,
                    std::vector<OutputRef>& outRefs, Error* outError) {
    JobPollOptions options;
    options.initialDelayMs = ctx.pollIntervalMs;
    options.intervalMs     = ctx.pollIntervalMs;
    options.maxIntervalMs  = ctx.pollIntervalMs;
    options.timeoutMs      = ctx.jobTimeoutMs;

    Error pollError;
    const JobPollOutcome outcome = RunJobPoll(
        options, isCancelled,
        [&](Error* stepError) {
            std::vector<OutputRef> found;
            bool finished = false;
            if (!HistoryOutputs(ctx, promptId, found, finished, stepError)) {
                // A finished prompt that reported an error is terminal;
                // anything else is worth another poll.
                return finished ? JobPollState::Failed : JobPollState::Pending;
            }
            if (!finished) {
                Emit(onEvent, ComfyEventKind::InProgress, 0.0);
                return JobPollState::Pending;
            }
            outRefs = std::move(found);
            return JobPollState::Completed;
        },
        &pollError);

    if (outcome == JobPollOutcome::Cancelled) Interrupt(ctx);
    if (outcome != JobPollOutcome::Completed) {
        if (outError) *outError = pollError;
        return false;
    }
    return true;
}

bool RunWithWebSocket(const RunContext& ctx, const json& graph,
                      const ComfyCancelledFn& isCancelled,
                      const ComfyEmitFn& onEvent,
                      std::vector<OutputRef>& outRefs, Error* outError) {
    // Connect before submitting: ComfyUI starts reporting as soon as the
    // prompt is queued, and frames that arrive before we know the prompt_id
    // are held in `stream` until we do.
    auto stream = std::make_shared<WsStream>();
    TransportRequest wsRequest;
    wsRequest.method    = "GET";
    wsRequest.url       = ctx.baseUrl + "/ws?clientId=" + UrlEncode(ctx.clientId);
    wsRequest.timeoutMs = ctx.timeoutMs;
    wsRequest.headers   = Headers(ctx, /*json=*/false);

    CancelFn closeSocket = ctx.transport->WebSocketStream(
        wsRequest,
        [stream](const TransportWsMessage& message) {
            {
                std::lock_guard<std::mutex> lock(stream->mutex);
                stream->frames.push_back(message);
            }
            stream->signal.notify_all();
        },
        [stream](const Error& error) {
            {
                std::lock_guard<std::mutex> lock(stream->mutex);
                stream->closed     = true;
                stream->closeError = error;
            }
            stream->signal.notify_all();
        });

    std::string promptId;
    if (!Submit(ctx, graph, promptId, outError)) {
        closeSocket();
        return false;
    }
    Emit(onEvent, ComfyEventKind::Queued, 0.0);

    WsProgress progress;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(ctx.jobTimeoutMs);
    bool streamEnded = false;

    while (!progress.finished && !progress.failed) {
        if (isCancelled && isCancelled()) {
            closeSocket();
            Interrupt(ctx);
            if (outError) {
                outError->code    = ErrorCode::Cancelled;
                outError->message = "generation cancelled";
            }
            return false;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            closeSocket();
            Interrupt(ctx);
            if (outError) {
                outError->code    = ErrorCode::Timeout;
                outError->message = "ComfyUI did not finish within " +
                                    std::to_string(ctx.jobTimeoutMs) + " ms";
            }
            return false;
        }

        std::deque<TransportWsMessage> batch;
        {
            std::unique_lock<std::mutex> lock(stream->mutex);
            if (stream->frames.empty() && !stream->closed) {
                stream->signal.wait_for(lock, std::chrono::milliseconds(100));
            }
            batch.swap(stream->frames);
            streamEnded = stream->closed;
        }

        for (const TransportWsMessage& frame : batch) {
            if (frame.binary) {
                HandleBinaryFrame(frame, onEvent);
            } else {
                HandleTextFrame(frame.text, promptId, onEvent, progress);
            }
        }

        if (batch.empty() && streamEnded) break;
    }

    closeSocket();

    if (progress.failed) {
        if (outError) *outError = progress.error;
        return false;
    }
    if (!progress.finished) {
        // The socket dropped before the prompt finished (proxy, older build,
        // a transport with no WebSocket support). The job itself is
        // unaffected, so follow it through /history instead.
        return RunWithPolling(ctx, promptId, isCancelled, onEvent, outRefs,
                              outError);
    }
    if (progress.outputs.empty()) {
        // Execution ended without an "executed" payload — ask history for
        // what it produced.
        bool finished = false;
        return HistoryOutputs(ctx, promptId, outRefs, finished, outError);
    }
    outRefs = std::move(progress.outputs);
    return true;
}

} // namespace

RunContext MakeRunContext(const ProviderConfig& config,
                          const OptionsMap& requestOptions,
                          std::shared_ptr<ITransport> transport,
                          const std::string& baseUrl,
                          const std::string& clientId,
                          const std::string& apiKey,
                          bool defaultUseWebSocket) {
    RunContext ctx;
    ctx.transport = std::move(transport);
    ctx.baseUrl   = baseUrl;
    ctx.clientId  = clientId;
    ctx.apiKey    = apiKey;
    ctx.timeoutMs = config.timeoutMs;
    ctx.jobTimeoutMs = static_cast<int>(
        IntOption(config.providerOptions, requestOptions, kOptJobTimeoutMs,
                  600000));
    ctx.pollIntervalMs = static_cast<int>(
        IntOption(config.providerOptions, requestOptions, kOptPollIntervalMs,
                  1000));
    ctx.useWebSocket = BoolOption(config.providerOptions, requestOptions,
                                  kOptUseWebSocket, defaultUseWebSocket);
    return ctx;
}

std::string MakeClientId() {
    static std::atomic<uint64_t> counter{0};
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto micros =
        std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    std::ostringstream out;
    out << "ultraai-" << std::hex << micros << '-' << counter.fetch_add(1);
    return out.str();
}

uint64_t RandomSeed() {
    static std::mt19937_64 engine{std::random_device{}()};
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    // ComfyUI's seed widget is a signed 64-bit value; stay inside it.
    return engine() >> 1;
}

bool UploadImage(const RunContext& ctx, const MediaBlob& blob,
                 const std::string& extension, std::string& outReference,
                 Error* outError) {
    if (blob.bytes.empty()) {
        // A remote image would have to be fetched first; ComfyUI has no
        // "load from URL" core node, so say so instead of failing later
        // with an opaque validation error.
        if (outError) {
            outError->code    = ErrorCode::InvalidRequest;
            outError->message = "ComfyUI needs image bytes; MediaBlob::url "
                                "inputs must be fetched by the caller first";
        }
        return false;
    }

    MultipartPart image;
    image.name        = "image";
    image.filename    = UploadName(blob, extension);
    image.contentType = blob.mimeType.empty() ? "image/png" : blob.mimeType;
    image.value.assign(blob.bytes.begin(), blob.bytes.end());

    // No "overwrite" field on purpose: ComfyUI then renames a collision
    // instead of replacing the existing file, and the name it returns is
    // what the graph references.
    MultipartBody body = BuildMultipartBody({image});

    TransportRequest net;
    net.method    = "POST";
    net.url       = ctx.baseUrl + "/upload/image";
    net.timeoutMs = ctx.timeoutMs;
    net.headers   = Headers(ctx, /*json=*/false);
    net.headers.push_back({"content-type", body.contentType});
    net.body      = std::move(body.body);

    Error transportError;
    TransportResponse resp = ctx.transport->Request(net, &transportError);
    if (!transportError.IsOk()) {
        if (outError) *outError = transportError;
        return false;
    }
    if (resp.statusCode >= 400) {
        if (outError) {
            *outError = MapHttpStatus(resp.statusCode,
                                      "uploading an input image: " +
                                      resp.body.substr(0, 200));
        }
        return false;
    }

    json parsed = ParseJsonLenient(resp.body);
    const std::string name =
        parsed.is_object() ? parsed.value("name", "") : std::string();
    if (name.empty()) {
        if (outError) {
            outError->code    = ErrorCode::ProviderError;
            outError->message = "ComfyUI upload returned no file name";
        }
        return false;
    }
    const std::string subfolder = parsed.value("subfolder", "");
    outReference = subfolder.empty() ? name : subfolder + "/" + name;
    return true;
}

bool RunGraph(const RunContext& ctx, const json& graph,
              const ComfyCancelledFn& isCancelled, const ComfyEmitFn& onEvent,
              std::vector<OutputRef>& outRefs, Error* outError) {
    if (ctx.useWebSocket) {
        return RunWithWebSocket(ctx, graph, isCancelled, onEvent, outRefs,
                                outError);
    }
    std::string promptId;
    if (!Submit(ctx, graph, promptId, outError)) return false;
    Emit(onEvent, ComfyEventKind::Queued, 0.0);
    return RunWithPolling(ctx, promptId, isCancelled, onEvent, outRefs,
                          outError);
}

bool DownloadOutputs(const RunContext& ctx, const std::vector<OutputRef>& refs,
                     const char* fallbackMime, std::vector<MediaBlob>& outBlobs,
                     Error* outError) {
    for (const OutputRef& ref : refs) {
        MediaBlob blob;
        blob.filename = ref.filename;
        blob.url      = ViewUrl(ctx, ref);
        blob.mimeType = fallbackMime;

        if (!ctx.returnAsUrl) {
            TransportRequest net;
            net.method    = "GET";
            net.url       = blob.url;
            net.timeoutMs = ctx.timeoutMs;
            net.headers   = Headers(ctx, /*json=*/false);

            Error transportError;
            TransportResponse resp = ctx.transport->Request(net, &transportError);
            if (!transportError.IsOk()) {
                if (outError) *outError = transportError;
                return false;
            }
            if (resp.statusCode >= 400) {
                if (outError) {
                    *outError = MapHttpStatus(resp.statusCode,
                                              "downloading " + ref.filename);
                }
                return false;
            }
            const std::string contentType = resp.GetHeader("content-type");
            if (!contentType.empty()) blob.mimeType = contentType;
            blob.bytes.assign(resp.body.begin(), resp.body.end());
            blob.url.clear();
        }
        outBlobs.push_back(std::move(blob));
    }
    return true;
}

std::vector<std::string> ListNodeChoices(const RunContext& ctx,
                                         const std::string& nodeClass,
                                         const std::string& inputName) {
    std::vector<std::string> choices;

    TransportRequest net;
    net.method    = "GET";
    net.url       = ctx.baseUrl + "/object_info/" + UrlEncode(nodeClass);
    net.timeoutMs = ctx.timeoutMs;
    net.headers   = Headers(ctx, /*json=*/false);

    Error transportError;
    TransportResponse resp = ctx.transport->Request(net, &transportError);
    if (!transportError.IsOk() || resp.statusCode >= 400) return choices;

    json parsed = ParseJsonLenient(resp.body);
    if (!parsed.is_object() || !parsed.contains(nodeClass)) return choices;

    const json& node = parsed[nodeClass];
    if (!node.is_object() || !node.contains("input") ||
        !node["input"].is_object()) {
        return choices;
    }
    const json& required = node["input"]["required"];
    // Each input is [<type or choice list>, <options object>]; a choice list
    // is what makes the first element an array.
    if (!required.is_object() || !required.contains(inputName) ||
        !required[inputName].is_array() || required[inputName].empty() ||
        !required[inputName][0].is_array()) {
        return choices;
    }
    for (const auto& entry : required[inputName][0]) {
        if (entry.is_string()) choices.push_back(entry);
    }
    return choices;
}

} // namespace comfyui_detail
} // namespace UltraAI
