// UltraAI/include/UltraAIVideoGen.h
// Provider-agnostic video generation / editing interface.
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
// Output format / mode
// =====================================================================

enum class VideoFormat {
    Auto,
    Mp4,
    Webm,
    Mov,
    Gif,
    Hevc
};

enum class VideoGenMode {
    TextToVideo,
    ImageToVideo,        // requires sourceImage
    VideoToVideo,        // restyle / re-edit; requires sourceVideo
    FrameInterpolation,  // requires sourceVideo
    Upscale              // requires sourceVideo
};

// =====================================================================
// Request / Response
// =====================================================================

struct VideoGenRequest {
    std::string model;                    // empty -> adapter default
    VideoGenMode mode = VideoGenMode::TextToVideo;

    std::string prompt;
    std::string negativePrompt;

    int32_t width  = 0;                   // 0 -> model default
    int32_t height = 0;
    double  durationSec = 0.0;            // 0 -> model default
    int32_t fps    = 0;
    int32_t count  = 1;

    // Sampling
    std::optional<int32_t>  steps;
    std::optional<double>   guidanceScale;
    std::optional<uint64_t> seed;

    // Mode-specific inputs
    std::optional<MediaBlob> sourceImage;
    std::optional<MediaBlob> sourceVideo;
    std::optional<MediaBlob> audioTrack;  // attach narration / music
    std::optional<int32_t>   upscaleFactor;
    std::optional<int32_t>   targetFps;   // for FrameInterpolation

    VideoFormat outputFormat = VideoFormat::Mp4;
    bool requestSafetyCheck = true;

    OptionsMap options;
};

struct GeneratedVideo {
    MediaBlob video;
    MediaBlob thumbnail;            // optional first-frame preview
    double durationSec = 0.0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t fps = 0;
    uint64_t seed = 0;
    bool flaggedUnsafe = false;
};

struct VideoGenResponse {
    std::string model;
    std::vector<GeneratedVideo> videos;
    TokenUsage usage;               // videos counted in usage.units
    Error error;
};

// =====================================================================
// Job-style progress (video gen typically takes minutes)
// =====================================================================

enum class VideoJobEventKind {
    Queued,
    InProgress,
    PreviewFrame,                   // optional in-progress preview
    Completed,
    Error
};

struct VideoJobEvent {
    VideoJobEventKind kind = VideoJobEventKind::InProgress;
    double progress = 0.0;
    std::optional<MediaBlob> previewFrame;
    std::optional<VideoGenResponse> result;
    Error error;
};

using VideoJobCallback = std::function<void(const VideoJobEvent&)>;

// =====================================================================
// Capability discovery
// =====================================================================

struct VideoGenModelInfo {
    std::string id;
    std::string displayName;
    std::vector<VideoGenMode> supportedModes;
    std::vector<std::pair<int32_t, int32_t>> supportedSizes;
    double maxDurationSec = 0.0;
    int32_t maxFps = 0;
    bool supportsAudioInput = false;
    bool runsLocally = false;
};

struct VideoGenProviderCapabilities {
    std::string providerId;
    std::vector<VideoGenModelInfo> models;
    std::vector<VideoFormat> outputFormats;
};

// =====================================================================
// IVideoGen
// =====================================================================

class IVideoGen {
public:
    virtual ~IVideoGen() = default;

    virtual VideoGenProviderCapabilities GetCapabilities() const = 0;

    virtual VideoGenResponse Generate(const VideoGenRequest& request) = 0;

    virtual std::future<VideoGenResponse> GenerateAsync(
        const VideoGenRequest& request) = 0;

    virtual StreamHandle GenerateJob(const VideoGenRequest& request,
                                     VideoJobCallback onEvent) = 0;

    virtual void* RawProvider() { return nullptr; }
};

// =====================================================================
// Factory
// =====================================================================

using VideoGenConfig = ProviderConfig;

std::unique_ptr<IVideoGen> CreateVideoGen(const VideoGenConfig& config,
                                          Error* outError = nullptr);

std::vector<std::string> ListVideoGenProviders();

bool RegisterVideoGenProvider(
    const std::string& providerId,
    std::function<std::unique_ptr<IVideoGen>(const VideoGenConfig&, Error*)> factory);

} // namespace UltraAI
