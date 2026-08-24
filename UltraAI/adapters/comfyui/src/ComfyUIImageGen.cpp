// UltraAI/adapters/comfyui/src/ComfyUIImageGen.cpp
// ComfyUI IImageGen adapter: workflow binding, upload, submit, progress and
// output retrieval.
//
// Two ways to follow a running job. The WebSocket path (GenerateJob by
// default) opens /ws before submitting, so queue position, per-node
// progress and preview frames reach the caller live. The polling path
// (blocking Generate, and the fallback when the socket drops) asks
// /history until the prompt appears. Frames that arrive before the
// prompt_id is known are queued rather than dropped — ComfyUI starts
// talking as soon as the socket opens.
// Version: 0.1.0
// Last Modified: 2026-08-24
// Author: UltraAI Module

#include "UltraAIComfyUI.h"

#include "ComfyUIInternal.h"
#include "ComfyUIWorkflows.h"
#include "UltraAICredentials.h"
#include "UltraAIJobPoll.h"
#include "UltraAIMultipart.h"
#include "UltraAIStreamHandleBase.h"
#ifdef ULTRAAI_HAS_ULTRANET
#include "UltraAIUltraNetTransport.h"
#endif

#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace UltraAI {

namespace {

using namespace comfyui_detail;
using nlohmann::json;

using EmitFn = std::function<void(const ImageJobEvent&)>;
using CancelledFn = std::function<bool()>;

// ---------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------

// ComfyUI ties a submission to a WebSocket by client id; it only has to be
// unique among the clients of one server.
std::string MakeClientId() {
    static std::atomic<uint64_t> counter{0};
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto micros =
        std::chrono::duration_cast<std::chrono::microseconds>(now).count();
    std::ostringstream out;
    out << "ultraai-" << std::hex << micros << '-' << counter.fetch_add(1);
    return out.str();
}

// ComfyUI caches results per (graph, seed): reusing a fixed seed returns the
// previous image instead of generating a new one, so an unset seed becomes
// a fresh random one rather than zero.
uint64_t RandomSeed() {
    static std::mt19937_64 engine{std::random_device{}()};
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    // ComfyUI's seed widget is a signed 64-bit value; stay inside it.
    return engine() >> 1;
}

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

void Emit(const EmitFn& emit, ImageJobEventKind kind, double progress,
          Error error = {}) {
    if (!emit) return;
    ImageJobEvent event;
    event.kind     = kind;
    event.progress = progress;
    event.error    = std::move(error);
    emit(event);
}

// ---------------------------------------------------------------------
// HTTP steps
// ---------------------------------------------------------------------

// The name an upload is filed under. Uploads land in ComfyUI's shared
// input directory, so the caller's own file name is prefixed rather than
// used as-is: "photo.png" would otherwise collide with — and, with
// overwrite, replace — a file the user put there themselves.
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

// POST /upload/image. Returns the reference LoadImage expects: "name", or
// "subfolder/name" when the server filed it away.
//
// Masks go through this endpoint too: /upload/mask is ComfyUI's clipspace
// editor path, which needs an original_ref of an image already on the
// server. A mask uploaded here is read back by a LoadImageMask node.
bool UploadImage(const RunContext& ctx, const MediaBlob& blob,
                 const std::string& fallbackName,
                 std::string& outReference, Error* outError) {
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
    image.filename    = UploadName(blob, fallbackName);
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
// the queue, whether it produced images or failed.
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
        const std::string statusStr = status.value("status_str", "");
        if (statusStr == "error") {
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

// GET /view for each produced file.
bool DownloadOutputs(const RunContext& ctx, const std::vector<OutputRef>& refs,
                     ImageGenResponse& out, Error* outError) {
    for (const OutputRef& ref : refs) {
        GeneratedImage image;
        image.image.filename = ref.filename;
        image.image.url      = ViewUrl(ctx, ref);
        image.image.mimeType = "image/png";

        if (!ctx.returnAsUrl) {
            TransportRequest net;
            net.method    = "GET";
            net.url       = image.image.url;
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
            if (!contentType.empty()) image.image.mimeType = contentType;
            image.image.bytes.assign(resp.body.begin(), resp.body.end());
            image.image.url.clear();
        }
        out.images.push_back(std::move(image));
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
    bool finished = false;          // execution ended (successfully)
    bool failed   = false;
    Error error;
    std::vector<OutputRef> outputs; // from "executed" events
};

// Interpret one text frame. Frames for another client's prompt are ignored;
// frames from older builds that carry no prompt_id are accepted.
void HandleTextFrame(const std::string& text, const std::string& promptId,
                     const EmitFn& emit, WsProgress& progress) {
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
        Emit(emit, ImageJobEventKind::InProgress,
             max > 0.0 ? value / max : 0.0);
        return;
    }
    if (type == "executing") {
        // node == null marks the end of this prompt's execution.
        if (data.contains("node") && data["node"].is_null()) {
            progress.finished = true;
        } else {
            Emit(emit, ImageJobEventKind::InProgress, 0.0);
        }
        return;
    }
    if (type == "executed") {
        if (data.contains("output")) CollectOutputs(data["output"], progress.outputs);
        return;
    }
    if (type == "execution_success") {
        progress.finished = true;
        return;
    }
    if (type == "execution_error") {
        progress.failed = true;
        progress.error.code = ErrorCode::ProviderError;
        std::string message = data.value("exception_message", "");
        const std::string nodeType = data.value("node_type", "");
        if (!nodeType.empty()) message = nodeType + ": " + message;
        progress.error.message =
            message.empty() ? "ComfyUI execution failed" : message;
        return;
    }
    if (type == "execution_interrupted") {
        progress.failed       = true;
        progress.error.code   = ErrorCode::Cancelled;
        progress.error.message = "ComfyUI execution was interrupted";
    }
}

void HandleBinaryFrame(const TransportWsMessage& frame, const EmitFn& emit) {
    if (!emit) return;
    MediaBlob preview;
    if (!ParsePreviewFrame(frame.bytes, preview)) return;
    ImageJobEvent event;
    event.kind    = ImageJobEventKind::PreviewImage;
    event.preview = std::move(preview);
    emit(event);
}

// ---------------------------------------------------------------------
// Running a generation
// ---------------------------------------------------------------------

ImageGenResponse Finish(const RunContext& ctx,
                        const std::vector<OutputRef>& refs) {
    ImageGenResponse out;
    if (refs.empty()) {
        out.error.code    = ErrorCode::ProviderError;
        out.error.message = "ComfyUI finished the prompt but produced no "
                            "images — check that the workflow ends in a "
                            "SaveImage node";
        return out;
    }
    if (!DownloadOutputs(ctx, refs, out, &out.error)) {
        out.images.clear();
        return out;
    }
    out.usage.units = static_cast<int32_t>(out.images.size());
    return out;
}

ImageGenResponse RunWithPolling(const RunContext& ctx, const std::string& promptId,
                                const CancelledFn& isCancelled,
                                const EmitFn& emit) {
    ImageGenResponse out;
    std::vector<OutputRef> refs;

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
                Emit(emit, ImageJobEventKind::InProgress, 0.0);
                return JobPollState::Pending;
            }
            refs = std::move(found);
            return JobPollState::Completed;
        },
        &pollError);

    if (outcome == JobPollOutcome::Cancelled) Interrupt(ctx);
    if (outcome != JobPollOutcome::Completed) {
        out.error = pollError;
        return out;
    }
    return Finish(ctx, refs);
}

ImageGenResponse RunWithWebSocket(const RunContext& ctx, const json& graph,
                                  const CancelledFn& isCancelled,
                                  const EmitFn& emit) {
    ImageGenResponse out;

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
    if (!Submit(ctx, graph, promptId, &out.error)) {
        closeSocket();
        return out;
    }
    Emit(emit, ImageJobEventKind::Queued, 0.0);

    WsProgress progress;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(ctx.jobTimeoutMs);
    bool streamEnded = false;

    while (!progress.finished && !progress.failed) {
        if (isCancelled && isCancelled()) {
            closeSocket();
            Interrupt(ctx);
            out.error.code    = ErrorCode::Cancelled;
            out.error.message = "generation cancelled";
            return out;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            closeSocket();
            Interrupt(ctx);
            out.error.code    = ErrorCode::Timeout;
            out.error.message = "ComfyUI did not finish within " +
                                std::to_string(ctx.jobTimeoutMs) + " ms";
            return out;
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
                HandleBinaryFrame(frame, emit);
            } else {
                HandleTextFrame(frame.text, promptId, emit, progress);
            }
        }

        if (batch.empty() && streamEnded) break;
    }

    closeSocket();

    if (progress.failed) {
        out.error = progress.error;
        return out;
    }
    if (!progress.finished) {
        // The socket dropped before the prompt finished (proxy, older build,
        // no WebSocket support in this transport). The job itself is
        // unaffected, so follow it through /history instead.
        return RunWithPolling(ctx, promptId, isCancelled, emit);
    }
    if (progress.outputs.empty()) {
        // Execution ended without an "executed" payload — ask history for
        // what it produced.
        std::vector<OutputRef> refs;
        bool finished = false;
        Error historyError;
        if (!HistoryOutputs(ctx, promptId, refs, finished, &historyError)) {
            out.error = historyError;
            return out;
        }
        return Finish(ctx, refs);
    }
    return Finish(ctx, progress.outputs);
}

ImageGenResponse RunGeneration(const RunContext& ctx, const json& graph,
                               const CancelledFn& isCancelled,
                               const EmitFn& emit) {
    if (ctx.useWebSocket) return RunWithWebSocket(ctx, graph, isCancelled, emit);

    ImageGenResponse out;
    std::string promptId;
    if (!Submit(ctx, graph, promptId, &out.error)) return out;
    Emit(emit, ImageJobEventKind::Queued, 0.0);
    return RunWithPolling(ctx, promptId, isCancelled, emit);
}

// ---------------------------------------------------------------------
// The adapter
// ---------------------------------------------------------------------

class ComfyUIImageGen : public IImageGen {
public:
    ComfyUIImageGen(ImageGenConfig config,
                    std::shared_ptr<ITransport> transport)
        : config_(std::move(config)), transport_(std::move(transport)),
          baseUrl_(NormalizeBaseUrl(config_.baseUrl)),
          clientId_(MakeClientId()) {}

    ImageGenProviderCapabilities GetCapabilities() const override {
        ImageGenProviderCapabilities caps;
        caps.providerId    = "comfyui";
        caps.outputFormats = {ImageFormat::Png};

        for (const std::string& checkpoint : InstalledCheckpoints()) {
            ImageGenModelInfo model;
            model.id             = checkpoint;
            model.displayName    = checkpoint;
            model.supportedModes = {ImageGenMode::TextToImage,
                                    ImageGenMode::ImageToImage,
                                    ImageGenMode::Inpaint,
                                    ImageGenMode::Upscale};
            model.maxBatch       = 8;
            model.runsLocally    = true;
            caps.models.push_back(std::move(model));
        }
        return caps;
    }

    ImageGenResponse Generate(const ImageGenRequest& request) override {
        ImageGenResponse out;
        RunContext ctx;
        json graph;
        // The blocking call has nowhere to put progress events, so it takes
        // the cheaper polling path unless asked for the socket.
        if (!Prepare(request, /*defaultUseWebSocket=*/false, ctx, graph,
                     &out.error)) {
            return out;
        }
        return RunGeneration(ctx, graph, nullptr, nullptr);
    }

    std::future<ImageGenResponse> GenerateAsync(
        const ImageGenRequest& request) override {
        return std::async(std::launch::async,
                          [this, request] { return Generate(request); });
    }

    StreamHandle GenerateJob(const ImageGenRequest& request,
                             ImageJobCallback onEvent) override {
        auto handle = std::make_shared<StreamHandleBase>();

        RunContext ctx;
        json graph;
        Error prepareError;
        if (!Prepare(request, /*defaultUseWebSocket=*/true, ctx, graph,
                     &prepareError)) {
            if (onEvent) {
                ImageJobEvent event;
                event.kind  = ImageJobEventKind::Error;
                event.error = prepareError;
                onEvent(event);
            }
            handle->MarkDone();
            return handle;
        }

        std::thread worker([handle, ctx, graph, onEvent] {
            ImageGenResponse response = RunGeneration(
                ctx, graph,
                [handle] { return handle->IsCancelled(); },
                [onEvent](const ImageJobEvent& event) {
                    if (onEvent) onEvent(event);
                });

            if (onEvent) {
                ImageJobEvent event;
                if (response.error.IsOk()) {
                    event.kind     = ImageJobEventKind::Completed;
                    event.progress = 1.0;
                    event.result   = std::move(response);
                } else {
                    event.kind  = ImageJobEventKind::Error;
                    event.error = response.error;
                }
                onEvent(event);
            }
            handle->MarkDone();
        });
        worker.detach();
        return handle;
    }

private:
    // Build the run context and the graph to submit.
    bool Prepare(const ImageGenRequest& request, bool defaultUseWebSocket,
                 RunContext& ctx, json& graph, Error* outError) const {
        ctx.transport = transport_;
        ctx.baseUrl   = baseUrl_;
        ctx.clientId  = clientId_;
        ctx.timeoutMs = config_.timeoutMs;
        ctx.apiKey    = OptionalKey();
        ctx.returnAsUrl = request.returnAsUrl;
        ctx.jobTimeoutMs = static_cast<int>(
            IntOption(config_.providerOptions, request.options,
                      kOptJobTimeoutMs, 600000));
        ctx.pollIntervalMs = static_cast<int>(
            IntOption(config_.providerOptions, request.options,
                      kOptPollIntervalMs, 1000));
        ctx.useWebSocket = BoolOption(config_.providerOptions, request.options,
                                      kOptUseWebSocket, defaultUseWebSocket);

        return BuildGraph(ctx, request, graph, outError);
    }

    bool BuildGraph(const RunContext& ctx, const ImageGenRequest& request,
                    json& graph, Error* outError) const {
        auto reject = [outError](ErrorCode code, const std::string& message) {
            if (outError) { outError->code = code; outError->message = message; }
            return false;
        };

        const std::string custom = StringOption(config_.providerOptions,
                                                request.options, kOptWorkflow,
                                                "");
        if (!custom.empty()) {
            graph = ParseJsonLenient(custom);
            if (!graph.is_object() || graph.empty()) {
                return reject(ErrorCode::InvalidRequest,
                              "the \"workflow\" option is not an API-format "
                              "workflow object");
            }
        } else {
            graph = BuiltInWorkflow(request.mode);
            if (!graph.is_object() || graph.empty()) {
                return reject(ErrorCode::UnsupportedFormat,
                              "this ComfyUI adapter has no built-in workflow "
                              "for the requested mode; supply one through the "
                              "\"workflow\" option");
            }
        }

        if (!request.controlImages.empty() && custom.empty()) {
            return reject(ErrorCode::UnsupportedFormat,
                          "ControlNet guidance needs a workflow with "
                          "ControlNet nodes; supply one through the "
                          "\"workflow\" option");
        }

        const bool needsSource = request.mode == ImageGenMode::ImageToImage ||
                                 request.mode == ImageGenMode::Inpaint ||
                                 request.mode == ImageGenMode::Upscale;
        if (needsSource && !request.sourceImage) {
            return reject(ErrorCode::InvalidRequest,
                          "this mode needs ImageGenRequest::sourceImage");
        }
        if (request.mode == ImageGenMode::Inpaint && !request.maskImage) {
            return reject(ErrorCode::InvalidRequest,
                          "Inpaint needs ImageGenRequest::maskImage");
        }
        if (request.prompt.empty() && request.mode != ImageGenMode::Upscale) {
            return reject(ErrorCode::InvalidRequest,
                          "ImageGenRequest::prompt is empty");
        }

        // --- prompts
        SetByTitle(graph, kTitlePositive, "CLIPTextEncode", "text",
                   request.prompt);
        // The negative encoder shares the class with the positive one, so it
        // is bound by title only — a workflow without the title keeps its
        // own negative prompt.
        SetByTitle(graph, kTitleNegative, "", "text", request.negativePrompt);

        // --- checkpoint
        const std::string checkpoint =
            !request.model.empty() ? request.model : config_.defaultModel;
        if (!checkpoint.empty()) {
            SetByTitle(graph, kTitleCheckpoint, "CheckpointLoaderSimple",
                       "ckpt_name", checkpoint);
        }

        // --- geometry
        if (request.width > 0) {
            SetByTitle(graph, kTitleLatent, "EmptyLatentImage", "width",
                       request.width);
        }
        if (request.height > 0) {
            SetByTitle(graph, kTitleLatent, "EmptyLatentImage", "height",
                       request.height);
        }
        if (request.count > 1) {
            // Only the empty-latent templates batch; img2img and inpaint
            // take their size from the source image.
            SetByTitle(graph, kTitleLatent, "EmptyLatentImage", "batch_size",
                       request.count);
        }

        // --- sampling
        SetByTitle(graph, kTitleSampler, "KSampler", "seed",
                   static_cast<int64_t>(request.seed ? *request.seed
                                                     : RandomSeed()));
        if (request.steps) {
            SetByTitle(graph, kTitleSampler, "KSampler", "steps", *request.steps);
        }
        if (request.guidanceScale) {
            SetByTitle(graph, kTitleSampler, "KSampler", "cfg",
                       *request.guidanceScale);
        }
        if (!request.scheduler.empty()) {
            SetByTitle(graph, kTitleSampler, "KSampler", "scheduler",
                       request.scheduler);
        }
        const std::string sampler = StringOption(config_.providerOptions,
                                                 request.options,
                                                 kOptSamplerName, "");
        if (!sampler.empty()) {
            SetByTitle(graph, kTitleSampler, "KSampler", "sampler_name", sampler);
        }
        if (request.strength) {
            SetByTitle(graph, kTitleSampler, "KSampler", "denoise",
                       *request.strength);
        }
        if (request.upscaleFactor && *request.upscaleFactor > 0) {
            SetByTitle(graph, kTitleUpscale, "ImageScaleBy", "scale_by",
                       static_cast<double>(*request.upscaleFactor));
        }

        // --- inputs that have to be uploaded first
        if (request.sourceImage) {
            std::string reference;
            if (!UploadImage(ctx, *request.sourceImage, ".png", reference,
                             outError)) {
                return false;
            }
            SetByTitle(graph, kTitleSource, "LoadImage", "image", reference);
        }
        if (request.maskImage) {
            std::string reference;
            if (!UploadImage(ctx, *request.maskImage, ".png", reference,
                             outError)) {
                return false;
            }
            SetByTitle(graph, kTitleMask, "LoadImageMask", "image", reference);
        }

        // --- caller's own last word
        const std::string overrides = StringOption(config_.providerOptions,
                                                   request.options,
                                                   kOptNodeOverrides, "");
        if (!overrides.empty()) {
            json parsed = ParseJsonLenient(overrides);
            if (!parsed.is_object()) {
                return reject(ErrorCode::InvalidRequest,
                              "the \"node_overrides\" option is not a JSON "
                              "object of {node id: {input: value}}");
            }
            for (const auto& [nodeId, inputs] : parsed.items()) {
                if (!inputs.is_object()) continue;
                for (const auto& [field, value] : inputs.items()) {
                    SetNodeInput(graph, nodeId, field, value);
                }
            }
        }
        return true;
    }

    // ComfyUI runs keyless locally; a key is only sent when one is
    // configured, so a missing credential is not an error here.
    std::string OptionalKey() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (!keyResolved_) {
            keyResolved_ = true;
            Error ignored;
            resolvedKey_ = ResolveApiKey(config_, &ignored);
        }
        return resolvedKey_;
    }

    // GET /object_info/CheckpointLoaderSimple, once per adapter instance.
    // Its ckpt_name input carries the list of installed checkpoints.
    std::vector<std::string> InstalledCheckpoints() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (checkpointsFetched_) return checkpoints_;
        checkpointsFetched_ = true;

        TransportRequest net;
        net.method    = "GET";
        net.url       = baseUrl_ + "/object_info/CheckpointLoaderSimple";
        net.timeoutMs = config_.timeoutMs;

        Error transportError;
        TransportResponse resp = transport_->Request(net, &transportError);
        if (!transportError.IsOk() || resp.statusCode >= 400) return checkpoints_;

        json parsed = ParseJsonLenient(resp.body);
        if (!parsed.is_object() ||
            !parsed.contains("CheckpointLoaderSimple")) {
            return checkpoints_;
        }
        const json& node = parsed["CheckpointLoaderSimple"];
        if (!node.is_object() || !node.contains("input") ||
            !node["input"].is_object()) {
            return checkpoints_;
        }
        const json& required = node["input"]["required"];
        if (!required.is_object() || !required.contains("ckpt_name") ||
            !required["ckpt_name"].is_array() ||
            required["ckpt_name"].empty() ||
            !required["ckpt_name"][0].is_array()) {
            return checkpoints_;
        }
        for (const auto& entry : required["ckpt_name"][0]) {
            if (entry.is_string()) checkpoints_.push_back(entry);
        }
        return checkpoints_;
    }

    ImageGenConfig config_;
    std::shared_ptr<ITransport> transport_;
    std::string baseUrl_;
    std::string clientId_;

    mutable std::mutex stateMutex_;
    mutable bool keyResolved_ = false;
    mutable std::string resolvedKey_;
    mutable bool checkpointsFetched_ = false;
    mutable std::vector<std::string> checkpoints_;
};

} // namespace

std::unique_ptr<IImageGen> CreateComfyUIImageGen(
    const ImageGenConfig& config,
    Error* outError,
    std::shared_ptr<ITransport> transport) {
    if (!transport) {
#ifdef ULTRAAI_HAS_ULTRANET
        transport = std::make_shared<UltraNetTransport>();
#else
        if (outError) {
            outError->code    = ErrorCode::NetworkError;
            outError->message = "ComfyUI adapter needs a transport: build "
                                "with ULTRAAI_USE_ULTRANET=ON or inject one";
        }
        return nullptr;
#endif
    }
    return std::make_unique<ComfyUIImageGen>(config, std::move(transport));
}

} // namespace UltraAI
