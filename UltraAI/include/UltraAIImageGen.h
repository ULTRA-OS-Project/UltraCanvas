// UltraAI/include/UltraAIImageGen.h
// Provider-agnostic image generation / editing interface.
// Version: 0.1.0
// Last Modified: 2026-05-08
// Author: UltraAI Module
#pragma once

#include "UltraAICommon.h"

#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace UltraAI {

// =====================================================================
// Output format
// =====================================================================

enum class ImageFormat {
    Auto,
    Png,
    Jpeg,
    Webp
};

// =====================================================================
// Modes
// =====================================================================

enum class ImageGenMode {
    TextToImage,
    ImageToImage,         // requires `sourceImage`
    Inpaint,              // requires `sourceImage` + `maskImage`
    Outpaint,             // requires `sourceImage` (mask optional)
    Upscale,              // requires `sourceImage`
    BackgroundRemoval,    // requires `sourceImage`
    Variation             // requires `sourceImage`
};

// =====================================================================
// Reference / control inputs (ControlNet-style guidance)
// =====================================================================

enum class ControlType {
    None,
    CannyEdge,
    Depth,
    Pose,
    NormalMap,
    Segmentation,
    Scribble,
    Reference            // free-form style reference
};

struct ControlImage {
    MediaBlob image;
    ControlType type = ControlType::Reference;
    double weight = 1.0;
};

// =====================================================================
// Request / Response
// =====================================================================

struct ImageGenRequest {
    std::string model;             // empty -> adapter default
    ImageGenMode mode = ImageGenMode::TextToImage;

    std::string prompt;
    std::string negativePrompt;

    // Geometry / batch
    int32_t width  = 0;            // 0 -> model default
    int32_t height = 0;
    int32_t count  = 1;

    // Sampling
    std::optional<int32_t>  steps;
    std::optional<double>   guidanceScale;   // CFG
    std::optional<uint64_t> seed;
    std::string scheduler;                   // optional, model-dependent

    // Img2img / inpaint inputs
    std::optional<MediaBlob> sourceImage;
    std::optional<MediaBlob> maskImage;
    std::optional<double>    strength;       // 0..1 denoising strength
    std::optional<int32_t>   upscaleFactor;  // for Upscale mode

    // ControlNet-style inputs
    std::vector<ControlImage> controlImages;

    // Output
    ImageFormat outputFormat = ImageFormat::Png;
    bool returnAsUrl = false;                // adapter may ignore

    // Safety / metadata
    bool requestSafetyCheck = true;

    OptionsMap options;
};

struct GeneratedImage {
    MediaBlob image;
    uint64_t seed = 0;
    bool flaggedUnsafe = false;
    std::string revisedPrompt;        // some providers rewrite the prompt
};

struct ImageGenResponse {
    std::string model;
    std::vector<GeneratedImage> images;
    TokenUsage usage;                 // images counted in usage.units
    Error error;
};

// =====================================================================
// Job-style progress (image gen can take many seconds)
// =====================================================================

enum class ImageJobEventKind {
    Queued,
    InProgress,         // includes optional progress fraction / preview
    PreviewImage,
    Completed,
    Error
};

struct ImageJobEvent {
    ImageJobEventKind kind = ImageJobEventKind::InProgress;
    double progress = 0.0;            // 0..1 when known
    std::optional<MediaBlob> preview; // low-res in-progress frame
    std::optional<ImageGenResponse> result;  // populated when Completed
    Error error;
};

using ImageJobCallback = std::function<void(const ImageJobEvent&)>;

// =====================================================================
// Capability discovery
// =====================================================================

struct ImageGenModelInfo {
    std::string id;
    std::string displayName;
    std::vector<ImageGenMode> supportedModes;
    std::vector<ControlType> supportedControls;
    std::vector<std::pair<int32_t, int32_t>> supportedSizes;
    int32_t maxBatch = 0;
    bool runsLocally = false;
};

struct ImageGenProviderCapabilities {
    std::string providerId;
    std::vector<ImageGenModelInfo> models;
    std::vector<ImageFormat> outputFormats;
};

// =====================================================================
// IImageGen — the capability interface
// =====================================================================

class IImageGen {
public:
    virtual ~IImageGen() = default;

    virtual ImageGenProviderCapabilities GetCapabilities() const = 0;

    // Blocking generation. May take a long time for large requests.
    virtual ImageGenResponse Generate(const ImageGenRequest& request) = 0;

    virtual std::future<ImageGenResponse> GenerateAsync(
        const ImageGenRequest& request) = 0;

    // Job-style API with progress events (Queued/InProgress/Completed).
    virtual StreamHandle GenerateJob(const ImageGenRequest& request,
                                     ImageJobCallback onEvent) = 0;

    virtual void* RawProvider() { return nullptr; }
};

// =====================================================================
// Factory
// =====================================================================

using ImageGenConfig = ProviderConfig;

std::unique_ptr<IImageGen> CreateImageGen(const ImageGenConfig& config,
                                          Error* outError = nullptr);

std::vector<std::string> ListImageGenProviders();

bool RegisterImageGenProvider(
    const std::string& providerId,
    std::function<std::unique_ptr<IImageGen>(const ImageGenConfig&, Error*)> factory);

} // namespace UltraAI
