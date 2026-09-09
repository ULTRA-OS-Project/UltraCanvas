// UltraAI/adapters/comfyui/src/ComfyUIImageGen.cpp
// ComfyUI IImageGen adapter: it builds the graph for an ImageGenRequest and
// translates the run layer's events and outputs into ImageJobEvent /
// ImageGenResponse. Submitting, progress and retrieval live in
// ComfyUIRun.cpp, which the video adapter shares.
// Version: 0.2.0
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

// Run a prepared graph and turn what it produced into an ImageGenResponse.
ImageGenResponse RunImageGraph(const RunContext& ctx, const json& graph,
                               const ComfyCancelledFn& isCancelled,
                               const ComfyEmitFn& onEvent) {
    ImageGenResponse out;

    std::vector<OutputRef> refs;
    if (!RunGraph(ctx, graph, isCancelled, onEvent, refs, &out.error)) {
        return out;
    }
    if (refs.empty()) {
        out.error.code    = ErrorCode::ProviderError;
        out.error.message = "ComfyUI finished the prompt but produced no "
                            "images — check that the workflow ends in a "
                            "SaveImage node";
        return out;
    }

    std::vector<MediaBlob> blobs;
    if (!DownloadOutputs(ctx, refs, "image/png", blobs, &out.error)) return out;

    for (MediaBlob& blob : blobs) {
        GeneratedImage image;
        image.image = std::move(blob);
        out.images.push_back(std::move(image));
    }
    out.usage.units = static_cast<int32_t>(out.images.size());
    return out;
}

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

        for (const std::string& checkpoint : Checkpoints()) {
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
        return RunImageGraph(ctx, graph, nullptr, nullptr);
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
            ImageGenResponse response = RunImageGraph(
                ctx, graph,
                [handle] { return handle->IsCancelled(); },
                [onEvent](const ComfyEvent& event) {
                    if (!onEvent) return;
                    ImageJobEvent out;
                    switch (event.kind) {
                        case ComfyEventKind::Queued:
                            out.kind = ImageJobEventKind::Queued;
                            break;
                        case ComfyEventKind::Preview:
                            out.kind    = ImageJobEventKind::PreviewImage;
                            out.preview = event.preview;
                            break;
                        case ComfyEventKind::InProgress:
                            out.kind = ImageJobEventKind::InProgress;
                            break;
                    }
                    out.progress = event.progress;
                    onEvent(out);
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
        ctx = MakeRunContext(config_, request.options, transport_, baseUrl_,
                             clientId_, OptionalKey(), defaultUseWebSocket);
        ctx.returnAsUrl = request.returnAsUrl;
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
        return ApplyNodeOverrides(graph, config_.providerOptions,
                                  request.options, outError);
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

    // The installed checkpoints, read once per adapter instance.
    std::vector<std::string> Checkpoints() const {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (!checkpointsFetched_) {
            checkpointsFetched_ = true;
            RunContext ctx = MakeRunContext(config_, OptionsMap{}, transport_,
                                            baseUrl_, clientId_, resolvedKey_,
                                            false);
            checkpoints_ = ListNodeChoices(ctx, "CheckpointLoaderSimple",
                                           "ckpt_name");
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
