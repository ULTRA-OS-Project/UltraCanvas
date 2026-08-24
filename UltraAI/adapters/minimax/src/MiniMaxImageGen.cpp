// UltraAI/adapters/minimax/src/MiniMaxImageGen.cpp
// MiniMax IImageGen adapter (POST {baseUrl}/v1/image_generation). Unlike
// video generation this endpoint answers synchronously, so GenerateJob is
// the blocking call wrapped in the Queued/Completed event pair.
// Version: 0.1.0
// Last Modified: 2026-08-24
// Author: UltraAI Module

#include "UltraAIMiniMax.h"

#include "MiniMaxInternal.h"
#include "UltraAIStreamHandleBase.h"
#ifdef ULTRAAI_HAS_ULTRANET
#include "UltraAIUltraNetTransport.h"
#endif

#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace UltraAI {

namespace {

using nlohmann::json;
using namespace minimax_detail;

// MiniMax takes sizes either as an aspect ratio or as explicit pixel
// dimensions; the documented pixel range is 512..2048 on each side.
constexpr int32_t kMinSide = 512;
constexpr int32_t kMaxSide = 2048;

const char* MimeForFormat(ImageFormat format) {
    switch (format) {
        case ImageFormat::Jpeg: return "image/jpeg";
        case ImageFormat::Webp: return "image/webp";
        case ImageFormat::Png:
        case ImageFormat::Auto:
        default:                return "image/png";
    }
}

class MiniMaxImageGen : public IImageGen {
public:
    MiniMaxImageGen(ImageGenConfig config,
                    std::shared_ptr<ITransport> transport)
        : config_(std::move(config)), transport_(std::move(transport)),
          baseUrl_(NormalizeBaseUrl(config_.baseUrl)) {}

    ImageGenProviderCapabilities GetCapabilities() const override {
        ImageGenProviderCapabilities caps;
        caps.providerId    = "minimax";
        caps.outputFormats = {ImageFormat::Png};

        ImageGenModelInfo model;
        model.id             = kDefaultImageModel;
        model.displayName    = "MiniMax Image-01";
        model.supportedModes = {ImageGenMode::TextToImage,
                                ImageGenMode::Variation};
        model.supportedSizes = {{1024, 1024}, {1280, 720}, {720, 1280}};
        model.maxBatch       = 9;
        model.runsLocally    = false;
        caps.models.push_back(std::move(model));
        return caps;
    }

    ImageGenResponse Generate(const ImageGenRequest& request) override {
        ImageGenResponse out;

        TransportRequest net;
        if (!BuildRequest(request, net, &out.error)) return out;

        Error transportError;
        TransportResponse resp = transport_->Request(net, &transportError);
        if (!transportError.IsOk()) {
            out.error = transportError;
            return out;
        }
        if (resp.statusCode >= 400) {
            out.error = MapMiniMaxHttpError(resp.statusCode, resp.body);
            return out;
        }

        json body = ParseJsonLenient(resp.body);
        if (body.is_discarded() || !body.is_object()) {
            out.error.code    = ErrorCode::ProviderError;
            out.error.message = "MiniMax response is not a JSON object";
            return out;
        }
        if (!CheckBaseResp(body, &out.error)) return out;
        return ParseImages(body, request);
    }

    std::future<ImageGenResponse> GenerateAsync(
        const ImageGenRequest& request) override {
        return std::async(std::launch::async,
                          [this, request] { return Generate(request); });
    }

    // The endpoint is synchronous; the job API is offered for interface
    // symmetry so callers can use one code path across providers.
    StreamHandle GenerateJob(const ImageGenRequest& request,
                             ImageJobCallback onEvent) override {
        auto handle = std::make_shared<StreamHandleBase>();
        if (onEvent) {
            ImageJobEvent queued;
            queued.kind = ImageJobEventKind::Queued;
            onEvent(queued);
        }

        ImageGenResponse response = Generate(request);
        if (onEvent && !handle->IsCancelled()) {
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
        return handle;
    }

private:
    bool BuildRequest(const ImageGenRequest& request, TransportRequest& outNet,
                      Error* outError) {
        auto reject = [outError](ErrorCode code, const std::string& message) {
            if (outError) { outError->code = code; outError->message = message; }
            return false;
        };

        if (request.prompt.empty()) {
            return reject(ErrorCode::InvalidRequest,
                          "ImageGenRequest::prompt is empty");
        }

        json body;
        body["model"] = !request.model.empty()       ? request.model
                      : !config_.defaultModel.empty() ? config_.defaultModel
                                                      : kDefaultImageModel;
        body["prompt"] = request.prompt;
        body["response_format"] = request.returnAsUrl ? "url" : "base64";
        if (request.count > 0) body["n"] = request.count;

        switch (request.mode) {
            case ImageGenMode::TextToImage:
                break;
            case ImageGenMode::Variation: {
                if (!request.sourceImage) {
                    return reject(ErrorCode::InvalidRequest,
                                  "Variation needs ImageGenRequest::sourceImage");
                }
                const std::string reference = MediaReference(*request.sourceImage);
                if (reference.empty()) {
                    return reject(ErrorCode::InvalidRequest,
                                  "ImageGenRequest::sourceImage has neither "
                                  "bytes nor a url");
                }
                body["subject_reference"] = json::array({
                    json{{"type", "character"}, {"image_file", reference}}});
                break;
            }
            default:
                return reject(ErrorCode::UnsupportedFormat,
                              "MiniMax image generation supports TextToImage "
                              "and Variation (subject reference) only");
        }

        if (request.width > 0 && request.height > 0) {
            if (request.width  < kMinSide || request.width  > kMaxSide ||
                request.height < kMinSide || request.height > kMaxSide) {
                return reject(ErrorCode::InvalidRequest,
                              "MiniMax accepts 512..2048 px per side; got " +
                              std::to_string(request.width) + "x" +
                              std::to_string(request.height));
            }
            body["width"]  = request.width;
            body["height"] = request.height;
        }

        // negativePrompt, steps, guidanceScale, seed and scheduler have no
        // MiniMax equivalent; documented as ignored in UltraAIMiniMax.h.
        ApplyOptions(body, config_.providerOptions, {});
        ApplyOptions(body, request.options, {});

        std::string key;
        if (!ResolveKeyOnce(key, outError)) return false;

        outNet.method    = "POST";
        outNet.url       = baseUrl_ + "/v1/image_generation";
        outNet.timeoutMs = config_.timeoutMs;
        outNet.headers   = {{"content-type", "application/json"},
                            {"authorization", "Bearer " + key}};
        outNet.body      = body.dump();
        return true;
    }

    static ImageGenResponse ParseImages(const json& body,
                                        const ImageGenRequest& request) {
        ImageGenResponse out;
        out.model = request.model;

        const json* data = nullptr;
        if (body.contains("data") && body["data"].is_object()) {
            data = &body["data"];
        }
        if (!data) {
            out.error.code    = ErrorCode::ProviderError;
            out.error.message = "MiniMax image response has no data object";
            return out;
        }

        const char* mime = MimeForFormat(request.outputFormat);
        if (data->contains("image_urls") && (*data)["image_urls"].is_array()) {
            for (const auto& entry : (*data)["image_urls"]) {
                if (!entry.is_string()) continue;
                GeneratedImage image;
                image.image.url      = entry.get<std::string>();
                image.image.mimeType = mime;
                out.images.push_back(std::move(image));
            }
        } else if (data->contains("image_base64") &&
                   (*data)["image_base64"].is_array()) {
            for (const auto& entry : (*data)["image_base64"]) {
                if (!entry.is_string()) continue;
                bool ok = false;
                std::vector<uint8_t> bytes =
                    Base64Decode(entry.get<std::string>(), &ok);
                if (!ok) {
                    out.images.clear();
                    out.error.code    = ErrorCode::ProviderError;
                    out.error.message = "MiniMax returned an image that is "
                                        "not valid base64";
                    return out;
                }
                GeneratedImage image;
                image.image.bytes    = std::move(bytes);
                image.image.mimeType = mime;
                out.images.push_back(std::move(image));
            }
        }

        if (out.images.empty()) {
            out.error.code    = ErrorCode::ProviderError;
            out.error.message = "MiniMax image response carried no images";
            return out;
        }
        out.usage.units = static_cast<int32_t>(out.images.size());
        return out;
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

    ImageGenConfig config_;
    std::shared_ptr<ITransport> transport_;
    std::string baseUrl_;
    std::mutex keyMutex_;
    std::string resolvedKey_;
    bool keyResolved_ = false;
};

} // namespace

std::unique_ptr<IImageGen> CreateMiniMaxImageGen(
    const ImageGenConfig& config,
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
    return std::make_unique<MiniMaxImageGen>(config, std::move(transport));
}

} // namespace UltraAI
