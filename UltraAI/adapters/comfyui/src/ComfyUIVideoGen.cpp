// UltraAI/adapters/comfyui/src/ComfyUIVideoGen.cpp
// ComfyUI IVideoGen adapter. Same machinery as the image adapter — it only
// builds a different graph and fills a different response; submitting,
// progress and retrieval come from ComfyUIRun.cpp.
// Version: 0.1.0
// Last Modified: 2026-08-24
// Author: UltraAI Module

#include "UltraAIComfyUI.h"

#include "ComfyUIInternal.h"
#include "ComfyUIRun.h"
#include "ComfyUIWorkflows.h"
#include "UltraAICredentials.h"
#include "UltraAIStreamHandleBase.h"
#ifdef ULTRAAI_HAS_ULTRANET
#include "UltraAIUltraNetTransport.h"
#endif

#include <algorithm>
#include <cmath>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace UltraAI {

namespace {

using namespace comfyui_detail;
using nlohmann::json;

// Stable Video Diffusion's own default; also what the built-in template
// carries when the request says nothing.
constexpr int32_t kDefaultFps    = 6;
constexpr int32_t kDefaultFrames = 14;

VideoGenResponse RunVideoGraph(const RunContext& ctx, const json& graph,
                               const VideoGenRequest& request,
                               int32_t frames, int32_t fps,
                               const ComfyCancelledFn& isCancelled,
                               const ComfyEmitFn& onEvent) {
    VideoGenResponse out;
    out.model = request.model;

    std::vector<OutputRef> refs;
    if (!RunGraph(ctx, graph, isCancelled, onEvent, refs, &out.error)) {
        return out;
    }
    if (refs.empty()) {
        out.error.code    = ErrorCode::ProviderError;
        out.error.message = "ComfyUI finished the prompt but produced no "
                            "video — check that the workflow ends in a save "
                            "node (SaveAnimatedWEBP, SaveWEBM, ...)";
        return out;
    }

    std::vector<MediaBlob> blobs;
    // The built-in template saves an animated WebP; a caller's workflow may
    // save anything, so the server's content type wins when it sends one.
    if (!DownloadOutputs(ctx, refs, "image/webp", blobs, &out.error)) return out;

    for (MediaBlob& blob : blobs) {
        GeneratedVideo video;
        video.video       = std::move(blob);
        video.width       = request.width;
        video.height      = request.height;
        video.fps         = fps;
        video.durationSec = fps > 0 ? static_cast<double>(frames) / fps : 0.0;
        out.videos.push_back(std::move(video));
    }
    out.usage.units = static_cast<int32_t>(out.videos.size());
    return out;
}

class ComfyUIVideoGen : public IVideoGen {
public:
    ComfyUIVideoGen(VideoGenConfig config,
                    std::shared_ptr<ITransport> transport)
        : config_(std::move(config)), transport_(std::move(transport)),
          baseUrl_(NormalizeBaseUrl(config_.baseUrl)),
          clientId_(MakeClientId()) {}

    VideoGenProviderCapabilities GetCapabilities() const override {
        VideoGenProviderCapabilities caps;
        caps.providerId = "comfyui";
        // The built-in template writes animated WebP, which VideoFormat has
        // no name for; Auto is the honest answer, and the produced blob
        // carries the real mime type.
        caps.outputFormats = {VideoFormat::Auto};

        for (const std::string& checkpoint : Checkpoints()) {
            VideoGenModelInfo model;
            model.id             = checkpoint;
            model.displayName    = checkpoint;
            model.supportedModes = {VideoGenMode::ImageToVideo};
            model.supportedSizes = {{1024, 576}, {576, 1024}};
            model.maxDurationSec = 4.0;      // 25 frames at 6 fps
            model.maxFps         = 30;
            model.runsLocally    = true;
            caps.models.push_back(std::move(model));
        }
        return caps;
    }

    VideoGenResponse Generate(const VideoGenRequest& request) override {
        VideoGenResponse out;
        RunContext ctx;
        json graph;
        int32_t frames = kDefaultFrames;
        int32_t fps    = kDefaultFps;
        if (!Prepare(request, /*defaultUseWebSocket=*/false, ctx, graph, frames,
                     fps, &out.error)) {
            return out;
        }
        return RunVideoGraph(ctx, graph, request, frames, fps, nullptr, nullptr);
    }

    std::future<VideoGenResponse> GenerateAsync(
        const VideoGenRequest& request) override {
        return std::async(std::launch::async,
                          [this, request] { return Generate(request); });
    }

    StreamHandle GenerateJob(const VideoGenRequest& request,
                             VideoJobCallback onEvent) override {
        auto handle = std::make_shared<StreamHandleBase>();

        RunContext ctx;
        json graph;
        int32_t frames = kDefaultFrames;
        int32_t fps    = kDefaultFps;
        Error prepareError;
        if (!Prepare(request, /*defaultUseWebSocket=*/true, ctx, graph, frames,
                     fps, &prepareError)) {
            if (onEvent) {
                VideoJobEvent event;
                event.kind  = VideoJobEventKind::Error;
                event.error = prepareError;
                onEvent(event);
            }
            handle->MarkDone();
            return handle;
        }

        std::thread worker([handle, ctx, graph, request, frames, fps, onEvent] {
            VideoGenResponse response = RunVideoGraph(
                ctx, graph, request, frames, fps,
                [handle] { return handle->IsCancelled(); },
                [onEvent](const ComfyEvent& event) {
                    if (!onEvent) return;
                    VideoJobEvent out;
                    switch (event.kind) {
                        case ComfyEventKind::Queued:
                            out.kind = VideoJobEventKind::Queued;
                            break;
                        case ComfyEventKind::Preview:
                            out.kind         = VideoJobEventKind::PreviewFrame;
                            out.previewFrame = event.preview;
                            break;
                        case ComfyEventKind::InProgress:
                            out.kind = VideoJobEventKind::InProgress;
                            break;
                    }
                    out.progress = event.progress;
                    onEvent(out);
                });

            if (onEvent) {
                VideoJobEvent event;
                if (response.error.IsOk()) {
                    event.kind     = VideoJobEventKind::Completed;
                    event.progress = 1.0;
                    event.result   = std::move(response);
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
    bool Prepare(const VideoGenRequest& request, bool defaultUseWebSocket,
                 RunContext& ctx, json& graph, int32_t& outFrames,
                 int32_t& outFps, Error* outError) const {
        ctx = MakeRunContext(config_, request.options, transport_, baseUrl_,
                             clientId_, OptionalKey(), defaultUseWebSocket);
        // VideoGenRequest has no returnAsUrl field, and a finished video is
        // large enough that fetching it is worth opting out of.
        ctx.returnAsUrl = BoolOption(config_.providerOptions, request.options,
                                     kOptReturnUrlOnly, false);
        return BuildGraph(ctx, request, graph, outFrames, outFps, outError);
    }

    bool BuildGraph(const RunContext& ctx, const VideoGenRequest& request,
                    json& graph, int32_t& outFrames, int32_t& outFps,
                    Error* outError) const {
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
            graph = BuiltInVideoWorkflow(request.mode);
            if (!graph.is_object() || graph.empty()) {
                return reject(ErrorCode::UnsupportedFormat,
                              "this ComfyUI adapter's only built-in video "
                              "workflow is image-to-video (Stable Video "
                              "Diffusion); supply a workflow through the "
                              "\"workflow\" option for anything else");
            }
        }

        if (request.mode == VideoGenMode::ImageToVideo && !request.sourceImage) {
            return reject(ErrorCode::InvalidRequest,
                          "ImageToVideo needs VideoGenRequest::sourceImage");
        }

        outFps = request.fps > 0 ? request.fps : kDefaultFps;
        outFrames = kDefaultFrames;
        if (request.durationSec > 0.0) {
            outFrames = std::max(1, static_cast<int>(
                std::lround(request.durationSec * outFps)));
        }

        // Every video binding below goes through SetExistingInput: video
        // models disagree on what these fields are called (video_frames vs
        // length, ckpt_name vs unet_name), and a field a node does not
        // declare would make ComfyUI reject the graph. The built-in SVD
        // template and a caller's Hunyuan or Wan workflow both bind through
        // the same titles.

        // --- model
        const std::string checkpoint =
            !request.model.empty() ? request.model : config_.defaultModel;
        if (!checkpoint.empty()) {
            SetExistingInput(graph, kTitleCheckpoint,
                             "ImageOnlyCheckpointLoader",
                             {"ckpt_name", "unet_name", "model_name"},
                             checkpoint);
        }

        // --- video shape. The titled video node owns the frame count and
        // the conditioning fps; the save node owns the playback fps.
        if (request.width > 0) {
            SetExistingInput(graph, kTitleVideo, "SVD_img2vid_Conditioning",
                             {"width"}, request.width);
        }
        if (request.height > 0) {
            SetExistingInput(graph, kTitleVideo, "SVD_img2vid_Conditioning",
                             {"height"}, request.height);
        }
        SetExistingInput(graph, kTitleVideo, "SVD_img2vid_Conditioning",
                         {"video_frames", "length", "num_frames"}, outFrames);
        SetExistingInput(graph, kTitleVideo, "SVD_img2vid_Conditioning",
                         {"fps"}, outFps);
        SetExistingInput(graph, "UltraAI Output", "SaveAnimatedWEBP",
                         {"fps"}, outFps);

        // --- prompts, where the workflow has somewhere to put them. The
        // built-in SVD template has no text encoder, so these are no-ops
        // there; a caller's text-conditioned workflow picks them up.
        if (!request.prompt.empty()) {
            SetByTitle(graph, kTitlePositive, "CLIPTextEncode", "text",
                       request.prompt);
        }
        if (!request.negativePrompt.empty()) {
            SetByTitle(graph, kTitleNegative, "", "text",
                       request.negativePrompt);
        }

        // --- sampling. A workflow driven by SamplerCustomAdvanced has no
        // seed/steps/cfg on its sampler node; those simply do not bind.
        SetExistingInput(graph, kTitleSampler, "KSampler", {"seed", "noise_seed"},
                         static_cast<int64_t>(request.seed ? *request.seed
                                                           : RandomSeed()));
        if (request.steps) {
            SetExistingInput(graph, kTitleSampler, "KSampler", {"steps"},
                             *request.steps);
        }
        if (request.guidanceScale) {
            SetExistingInput(graph, kTitleSampler, "KSampler", {"cfg"},
                             *request.guidanceScale);
        }

        // --- the conditioning image
        if (request.sourceImage) {
            std::string reference;
            if (!UploadImage(ctx, *request.sourceImage, ".png", reference,
                             outError)) {
                return false;
            }
            SetByTitle(graph, kTitleSource, "LoadImage", "image", reference);
        }

        return ApplyNodeOverrides(graph, config_.providerOptions,
                                  request.options, outError);
    }

    std::string OptionalKey() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (!keyResolved_) {
            keyResolved_ = true;
            Error ignored;
            resolvedKey_ = ResolveApiKey(config_, &ignored);
        }
        return resolvedKey_;
    }

    // The installed image-to-video checkpoints, read once per instance.
    std::vector<std::string> Checkpoints() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (!checkpointsFetched_) {
            checkpointsFetched_ = true;
            RunContext ctx = MakeRunContext(config_, OptionsMap{}, transport_,
                                            baseUrl_, clientId_, resolvedKey_,
                                            false);
            checkpoints_ = ListNodeChoices(ctx, "ImageOnlyCheckpointLoader",
                                           "ckpt_name");
        }
        return checkpoints_;
    }

    VideoGenConfig config_;
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

std::unique_ptr<IVideoGen> CreateComfyUIVideoGen(
    const VideoGenConfig& config,
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
    return std::make_unique<ComfyUIVideoGen>(config, std::move(transport));
}

} // namespace UltraAI
