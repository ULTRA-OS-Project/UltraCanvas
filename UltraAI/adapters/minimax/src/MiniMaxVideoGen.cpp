// UltraAI/adapters/minimax/src/MiniMaxVideoGen.cpp
// MiniMax (Hailuo) IVideoGen adapter. The provider runs generation as an
// asynchronous job, so one request becomes three exchanges:
//
//   POST /v1/video_generation          -> task_id
//   GET  /v1/query/video_generation    -> status, then file_id on Success
//   GET  /v1/files/retrieve            -> download_url
//
// The poll loop is the shared RunJobPoll; the worker context holds copies of
// everything it needs so a job outliving its adapter is safe.
// Version: 0.1.0
// Last Modified: 2026-08-24
// Author: UltraAI Module

#include "UltraAIMiniMax.h"

#include "MiniMaxInternal.h"
#include "UltraAIJobPoll.h"
#include "UltraAIStreamHandleBase.h"
#ifdef ULTRAAI_HAS_ULTRANET
#include "UltraAIUltraNetTransport.h"
#endif

#include <algorithm>
#include <cmath>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace UltraAI {

namespace {

using nlohmann::json;
using namespace minimax_detail;

constexpr const char* kOptJobTimeoutMs   = "job_timeout_ms";
constexpr const char* kOptPollIntervalMs = "poll_interval_ms";
constexpr const char* kOptReturnUrlOnly  = "return_url_only";

// Everything a running job needs, by value: the worker thread must not
// reach back into the adapter, which the caller may destroy first.
struct VideoJobContext {
    std::shared_ptr<ITransport> transport;
    std::string baseUrl;
    std::string apiKey;
    std::string model;
    int timeoutMs = 60000;
    JobPollOptions poll;
    bool returnUrlOnly = false;
    double requestedDurationSec = 0.0;
};

// MiniMax names resolutions rather than taking pixel sizes. Map from the
// requested frame's short side; an unset size lets the model decide.
std::string ResolutionFor(int32_t width, int32_t height) {
    const int32_t shortSide = (width > 0 && height > 0)
                                  ? std::min(width, height)
                                  : std::max(width, height);
    if (shortSide <= 0)   return {};
    if (shortSide >= 1024) return "1080P";
    if (shortSide >= 700)  return "768P";
    return "512P";
}

std::vector<std::pair<std::string, std::string>> AuthHeaders(
    const std::string& apiKey, bool json) {
    std::vector<std::pair<std::string, std::string>> headers;
    if (json) headers.push_back({"content-type", "application/json"});
    headers.push_back({"authorization", "Bearer " + apiKey});
    return headers;
}

// One JSON exchange: transport error, HTTP status and base_resp are all
// folded into *outError so callers have a single failure path.
bool JsonExchange(const VideoJobContext& ctx, const TransportRequest& net,
                  json& outBody, Error* outError) {
    Error transportError;
    TransportResponse resp = ctx.transport->Request(net, &transportError);
    if (!transportError.IsOk()) {
        if (outError) *outError = transportError;
        return false;
    }
    if (resp.statusCode >= 400) {
        if (outError) *outError = MapMiniMaxHttpError(resp.statusCode, resp.body);
        return false;
    }
    outBody = ParseJsonLenient(resp.body);
    if (outBody.is_discarded() || !outBody.is_object()) {
        if (outError) {
            outError->code    = ErrorCode::ProviderError;
            outError->message = "MiniMax response is not a JSON object";
        }
        return false;
    }
    return CheckBaseResp(outBody, outError);
}

bool BuildSubmitRequest(const VideoGenRequest& request,
                        const VideoGenConfig& config,
                        const VideoJobContext& ctx,
                        TransportRequest& outNet, Error* outError) {
    auto reject = [outError](ErrorCode code, const std::string& message) {
        if (outError) { outError->code = code; outError->message = message; }
        return false;
    };

    if (request.prompt.empty()) {
        return reject(ErrorCode::InvalidRequest,
                      "VideoGenRequest::prompt is empty");
    }

    json body;
    body["model"]  = ctx.model;
    body["prompt"] = request.prompt;

    switch (request.mode) {
        case VideoGenMode::TextToVideo:
            break;
        case VideoGenMode::ImageToVideo: {
            if (!request.sourceImage) {
                return reject(ErrorCode::InvalidRequest,
                              "ImageToVideo needs VideoGenRequest::sourceImage");
            }
            const std::string reference = MediaReference(*request.sourceImage);
            if (reference.empty()) {
                return reject(ErrorCode::InvalidRequest,
                              "VideoGenRequest::sourceImage has neither bytes "
                              "nor a url");
            }
            body["first_frame_image"] = reference;
            break;
        }
        case VideoGenMode::VideoToVideo:
        case VideoGenMode::FrameInterpolation:
        case VideoGenMode::Upscale:
            return reject(ErrorCode::UnsupportedFormat,
                          "MiniMax supports TextToVideo and ImageToVideo only");
    }

    if (request.durationSec > 0.0) {
        body["duration"] = static_cast<int>(std::lround(request.durationSec));
    }
    const std::string resolution = ResolutionFor(request.width, request.height);
    if (!resolution.empty()) body["resolution"] = resolution;

    // negativePrompt, steps, guidanceScale, seed, fps and count have no
    // MiniMax equivalent; documented as ignored in UltraAIMiniMax.h.
    ApplyOptions(body, config.providerOptions,
                 {kOptJobTimeoutMs, kOptPollIntervalMs, kOptReturnUrlOnly});
    ApplyOptions(body, request.options,
                 {kOptJobTimeoutMs, kOptPollIntervalMs, kOptReturnUrlOnly});

    outNet.method    = "POST";
    outNet.url       = ctx.baseUrl + "/v1/video_generation";
    outNet.timeoutMs = ctx.timeoutMs;
    outNet.headers   = AuthHeaders(ctx.apiKey, /*json=*/true);
    outNet.body      = body.dump();
    return true;
}

bool Submit(const VideoJobContext& ctx, const TransportRequest& net,
            std::string& outTaskId, Error* outError) {
    json body;
    if (!JsonExchange(ctx, net, body, outError)) return false;

    outTaskId = body.value("task_id", "");
    if (outTaskId.empty()) {
        if (outError) {
            outError->code    = ErrorCode::ProviderError;
            outError->message = "MiniMax accepted the request but returned no "
                                "task_id";
        }
        return false;
    }
    return true;
}

// One status poll. Sets outFileId when the job succeeded.
JobPollState PollOnce(const VideoJobContext& ctx, const std::string& taskId,
                      std::string& outFileId, Error* outError) {
    TransportRequest net;
    net.method    = "GET";
    net.url       = ctx.baseUrl + "/v1/query/video_generation?task_id=" + taskId;
    net.timeoutMs = ctx.timeoutMs;
    net.headers   = AuthHeaders(ctx.apiKey, /*json=*/false);

    json body;
    Error exchangeError;
    if (!JsonExchange(ctx, net, body, &exchangeError)) {
        // Auth, quota and malformed-request failures will not fix
        // themselves; network hiccups and rate limits might.
        const bool transient = exchangeError.code == ErrorCode::NetworkError ||
                               exchangeError.code == ErrorCode::Timeout ||
                               exchangeError.code == ErrorCode::RateLimited;
        if (outError) *outError = exchangeError;
        return transient ? JobPollState::Pending : JobPollState::Failed;
    }

    const std::string status = body.value("status", "");
    if (status == "Success") {
        outFileId = body.value("file_id", "");
        if (outFileId.empty()) {
            if (outError) {
                outError->code    = ErrorCode::ProviderError;
                outError->message = "MiniMax reported Success with no file_id";
            }
            return JobPollState::Failed;
        }
        return JobPollState::Completed;
    }
    if (status == "Fail") {
        if (outError) {
            outError->code    = ErrorCode::ProviderError;
            outError->message = "MiniMax video job failed";
            outError->providerCode = body.value("status", "Fail");
        }
        return JobPollState::Failed;
    }
    // Preparing / Queueing / Processing, or a status this adapter predates.
    return JobPollState::Pending;
}

bool Retrieve(const VideoJobContext& ctx, const std::string& fileId,
              GeneratedVideo& outVideo, Error* outError) {
    TransportRequest net;
    net.method    = "GET";
    net.url       = ctx.baseUrl + "/v1/files/retrieve?file_id=" + fileId;
    net.timeoutMs = ctx.timeoutMs;
    net.headers   = AuthHeaders(ctx.apiKey, /*json=*/false);

    json body;
    if (!JsonExchange(ctx, net, body, outError)) return false;

    std::string downloadUrl;
    std::string filename;
    if (body.contains("file") && body["file"].is_object()) {
        downloadUrl = body["file"].value("download_url", "");
        filename    = body["file"].value("filename", "");
    }
    if (downloadUrl.empty()) {
        if (outError) {
            outError->code    = ErrorCode::ProviderError;
            outError->message = "MiniMax file record has no download_url";
        }
        return false;
    }

    outVideo.video.url      = downloadUrl;
    outVideo.video.filename = filename;
    outVideo.video.mimeType = "video/mp4";
    outVideo.durationSec    = ctx.requestedDurationSec;

    if (ctx.returnUrlOnly) return true;

    TransportRequest download;
    download.method    = "GET";
    download.url       = downloadUrl;
    download.timeoutMs = ctx.timeoutMs;
    // Deliberately unauthenticated: download_url points at object storage,
    // and the API key has no business travelling to a different host.

    Error transportError;
    TransportResponse resp = ctx.transport->Request(download, &transportError);
    if (!transportError.IsOk()) {
        if (outError) *outError = transportError;
        return false;
    }
    if (resp.statusCode >= 400) {
        if (outError) {
            *outError = MapHttpStatus(resp.statusCode,
                                      "downloading the finished video");
        }
        return false;
    }
    outVideo.video.bytes.assign(resp.body.begin(), resp.body.end());
    return true;
}

// The whole job: submit, poll, retrieve. `onEvent` may be null (blocking
// Generate); `isCancelled` may be null (never cancelled).
VideoGenResponse RunJob(const VideoJobContext& ctx,
                        const TransportRequest& submitRequest,
                        const std::function<bool()>& isCancelled,
                        const std::function<void(const VideoJobEvent&)>& onEvent) {
    VideoGenResponse out;
    out.model = ctx.model;

    auto emit = [&onEvent](VideoJobEventKind kind, const Error& error) {
        if (!onEvent) return;
        VideoJobEvent event;
        event.kind  = kind;
        event.error = error;
        onEvent(event);
    };

    std::string taskId;
    if (!Submit(ctx, submitRequest, taskId, &out.error)) return out;
    emit(VideoJobEventKind::Queued, Error{});

    std::string fileId;
    Error pollError;
    const JobPollOutcome outcome = RunJobPoll(
        ctx.poll, isCancelled,
        [&](Error* stepError) {
            const JobPollState state = PollOnce(ctx, taskId, fileId, stepError);
            if (state == JobPollState::Pending) {
                emit(VideoJobEventKind::InProgress, Error{});
            }
            return state;
        },
        &pollError);

    if (outcome != JobPollOutcome::Completed) {
        out.error = pollError;
        return out;
    }

    GeneratedVideo video;
    if (!Retrieve(ctx, fileId, video, &out.error)) return out;

    out.videos.push_back(std::move(video));
    out.usage.units = 1;
    return out;
}

class MiniMaxVideoGen : public IVideoGen {
public:
    MiniMaxVideoGen(VideoGenConfig config,
                    std::shared_ptr<ITransport> transport)
        : config_(std::move(config)), transport_(std::move(transport)),
          baseUrl_(NormalizeBaseUrl(config_.baseUrl)) {}

    VideoGenProviderCapabilities GetCapabilities() const override {
        VideoGenProviderCapabilities caps;
        caps.providerId    = "minimax";
        caps.outputFormats = {VideoFormat::Mp4};

        auto add = [&caps](const char* id, const char* name,
                           double maxDuration,
                           std::vector<std::pair<int32_t, int32_t>> sizes) {
            VideoGenModelInfo model;
            model.id             = id;
            model.displayName    = name;
            model.supportedModes = {VideoGenMode::TextToVideo,
                                    VideoGenMode::ImageToVideo};
            model.supportedSizes = std::move(sizes);
            model.maxDurationSec = maxDuration;
            model.maxFps         = 25;
            model.runsLocally    = false;
            caps.models.push_back(std::move(model));
        };
        // The API accepts any model string; these are the ones this adapter
        // has been exercised against. Newer model ids work without a code
        // change — pass them as VideoGenRequest::model.
        add("MiniMax-Hailuo-02", "MiniMax Hailuo 02", 10.0,
            {{1280, 720}, {1920, 1080}});
        add("T2V-01-Director", "MiniMax T2V-01 Director", 6.0,
            {{1280, 720}});
        add("I2V-01-Director", "MiniMax I2V-01 Director", 6.0,
            {{1280, 720}});
        return caps;
    }

    VideoGenResponse Generate(const VideoGenRequest& request) override {
        VideoGenResponse out;
        VideoJobContext ctx;
        TransportRequest submit;
        if (!Prepare(request, ctx, submit, &out.error)) return out;
        return RunJob(ctx, submit, nullptr, nullptr);
    }

    std::future<VideoGenResponse> GenerateAsync(
        const VideoGenRequest& request) override {
        return std::async(std::launch::async,
                          [this, request] { return Generate(request); });
    }

    StreamHandle GenerateJob(const VideoGenRequest& request,
                             VideoJobCallback onEvent) override {
        auto handle = std::make_shared<StreamHandleBase>();

        VideoJobContext ctx;
        TransportRequest submit;
        Error prepareError;
        if (!Prepare(request, ctx, submit, &prepareError)) {
            if (onEvent) {
                VideoJobEvent event;
                event.kind  = VideoJobEventKind::Error;
                event.error = prepareError;
                onEvent(event);
            }
            handle->MarkDone();
            return handle;
        }

        std::thread worker([handle, ctx, submit, onEvent] {
            VideoGenResponse response = RunJob(
                ctx, submit,
                [handle] { return handle->IsCancelled(); },
                [onEvent](const VideoJobEvent& event) {
                    if (onEvent) onEvent(event);
                });

            if (onEvent) {
                VideoJobEvent event;
                if (response.error.IsOk()) {
                    event.kind   = VideoJobEventKind::Completed;
                    event.result = std::move(response);
                } else {
                    event.kind  = VideoJobEventKind::Error;
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
    bool Prepare(const VideoGenRequest& request, VideoJobContext& outCtx,
                 TransportRequest& outNet, Error* outError) {
        outCtx.transport = transport_;
        outCtx.baseUrl   = baseUrl_;
        outCtx.timeoutMs = config_.timeoutMs;
        outCtx.model     = !request.model.empty()      ? request.model
                         : !config_.defaultModel.empty() ? config_.defaultModel
                                                         : kDefaultVideoModel;
        outCtx.requestedDurationSec = request.durationSec;
        outCtx.returnUrlOnly = BoolOption(config_.providerOptions,
                                          request.options,
                                          kOptReturnUrlOnly, false);

        const int64_t jobTimeoutMs = IntOption(config_.providerOptions,
                                               request.options,
                                               kOptJobTimeoutMs, 900000);
        const int64_t intervalMs = IntOption(config_.providerOptions,
                                             request.options,
                                             kOptPollIntervalMs, 5000);
        outCtx.poll.timeoutMs     = static_cast<int>(jobTimeoutMs);
        outCtx.poll.intervalMs    = static_cast<int>(intervalMs);
        outCtx.poll.initialDelayMs = static_cast<int>(intervalMs);
        outCtx.poll.maxIntervalMs = static_cast<int>(intervalMs);

        if (!ResolveKeyOnce(outCtx.apiKey, outError)) return false;
        return BuildSubmitRequest(request, config_, outCtx, outNet, outError);
    }

    bool ResolveKeyOnce(std::string& outKey, Error* outError) {
        std::lock_guard<std::mutex> lock(keyMutex_);
        if (!keyResolved_) {
            if (!minimax_detail::ResolveKey(config_, resolvedKey_, outError)) {
                return false;
            }
            keyResolved_ = true;
        }
        outKey = resolvedKey_;
        return true;
    }

    VideoGenConfig config_;
    std::shared_ptr<ITransport> transport_;
    std::string baseUrl_;
    std::mutex keyMutex_;
    std::string resolvedKey_;
    bool keyResolved_ = false;
};

} // namespace

std::unique_ptr<IVideoGen> CreateMiniMaxVideoGen(
    const VideoGenConfig& config,
    Error* outError,
    std::shared_ptr<ITransport> transport) {
    if (!transport) {
#ifdef ULTRAAI_HAS_ULTRANET
        transport = std::make_shared<UltraNetTransport>();
#else
        if (outError) {
            outError->code    = ErrorCode::NetworkError;
            outError->message = "MiniMax adapter needs a transport: build "
                                "with ULTRAAI_USE_ULTRANET=ON or inject one";
        }
        return nullptr;
#endif
    }
    return std::make_unique<MiniMaxVideoGen>(config, std::move(transport));
}

} // namespace UltraAI
