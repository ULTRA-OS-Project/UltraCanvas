// VideoFX/include/VideoFX/VideoFX.h
// Public API of the VideoFX module - video probing, frame extraction, and a
// segment timeline that is trimmed, filtered, joined and encoded to a file.
// Version: 0.2.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework
#pragma once

/**
 * @file VideoFX.h
 * @brief VideoFX - video editing and conversion engine (stage 1)
 *
 * Standalone module (no UltraCanvas UI dependency). FFmpeg does the decoding,
 * filtering and encoding, entirely behind this API: no FFmpeg type or header
 * reaches a caller.
 *
 * @code
 * #include <VideoFX/VideoFX.h>
 * using namespace VideoFX;
 *
 * VideoFXMediaInfo info;
 * if (VideoFX_Probe("holiday.mp4", info) != VideoFXResult::Ok) {
 *     std::cerr << VideoFX_GetLastError() << "\n";
 * }
 *
 * // Seconds 10..25, warmer, faded in and out, as a 720p MP4
 * VideoFXSegment clip = VideoFXSegment::FromFile("holiday.mp4", 10.0, 25.0);
 * clip.effects = { VideoFXEffect::Temperature(0.3), VideoFXEffect::FadeIn(1.0),
 *                  VideoFXEffect::FadeOut(1.0) };
 * VideoFX_Export({ clip }, "cut.mp4", VideoFXExportSettings::WebMP4(720),
 *                [](double f) { std::cout << int(f * 100) << "%\r"; return true; });
 * @endcode
 *
 * All calls are blocking and thread-safe for independent jobs; run them on a
 * worker thread, or use VideoFXExportJob, which does that for you.
 */

#include "VideoFXTypes.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace VideoFX {

// ============================================================================
// MODULE
// ============================================================================

// VideoFX's own version ("0.1.0")
std::string VideoFX_GetVersion();

// The engine behind it, e.g. "FFmpeg 6.1.1 (libavformat 60.16.100)";
// "unavailable" when the module was built without it.
std::string VideoFX_GetBackendVersion();

// true when the module was built with its backend (otherwise every operation
// returns VideoFXResult::NotAvailable)
bool VideoFX_IsAvailable();

// Reason for the last failure on the calling thread ("" after a success)
std::string VideoFX_GetLastError();

// Readable name of a result code ("Ok", "Encoder not available", ...)
const char* VideoFX_ResultToString(VideoFXResult result);

// Whether this build can encode the codec (e.g. H.265 needs libx265)
bool VideoFX_IsVideoEncoderAvailable(VideoFXVideoCodec codec);
bool VideoFX_IsAudioEncoderAvailable(VideoFXAudioCodec codec);

// Whether text overlays can be drawn (FFmpeg built with libfreetype)
bool VideoFX_IsTextOverlayAvailable();

// Backend diagnostics to stderr: false (default) = errors only
void VideoFX_SetVerboseLogging(bool verbose);

// ============================================================================
// INSPECTION
// ============================================================================

// Container, duration and every stream of a media file
VideoFXResult VideoFX_Probe(const std::string& path, VideoFXMediaInfo& info);

// The frame shown at `seconds`, as RGBA, rotated as the file says it should be
// displayed. maxWidth / maxHeight (0 = no limit) scale it down keeping the
// aspect ratio.
VideoFXResult VideoFX_ExtractFrame(const std::string& path, double seconds, VideoFXFrame& frame,
                                   int maxWidth = 0, int maxHeight = 0);

// `count` frames evenly spaced over the duration (a filmstrip / scrub bar)
VideoFXResult VideoFX_ExtractThumbnails(const std::string& path, int count,
                                        std::vector<VideoFXFrame>& frames,
                                        int maxWidth = 160, int maxHeight = 90);

// Write a frame as PNG or JPEG, chosen by the extension (.png / .jpg / .jpeg)
VideoFXResult VideoFX_SaveFrameImage(const VideoFXFrame& frame, const std::string& path);

// ============================================================================
// EDITING AND EXPORT
// ============================================================================

// The general call: play `segments` one after another, each with its own trim,
// speed and effects, and encode the result to `outputPath`.
VideoFXResult VideoFX_Export(const std::vector<VideoFXSegment>& segments,
                             const std::string& outputPath,
                             const VideoFXExportSettings& settings = {},
                             const VideoFXProgressCallback& progress = {});

// Re-encode a whole file with other settings (format, size, codec, quality)
VideoFXResult VideoFX_Transcode(const std::string& inputPath, const std::string& outputPath,
                                const VideoFXExportSettings& settings = {},
                                const VideoFXProgressCallback& progress = {});

// Keep seconds start..end (end 0 = to the end), re-encoded frame-accurately
VideoFXResult VideoFX_Trim(const std::string& inputPath, const std::string& outputPath,
                           double start, double end,
                           const VideoFXExportSettings& settings = {},
                           const VideoFXProgressCallback& progress = {});

// Apply effects to a whole file
VideoFXResult VideoFX_ApplyEffects(const std::string& inputPath, const std::string& outputPath,
                                   const std::vector<VideoFXEffect>& effects,
                                   const VideoFXExportSettings& settings = {},
                                   const VideoFXProgressCallback& progress = {});

// Join files end to end; they may differ in size, rate and codec
VideoFXResult VideoFX_Concatenate(const std::vector<std::string>& inputPaths,
                                  const std::string& outputPath,
                                  const VideoFXExportSettings& settings = {},
                                  const VideoFXProgressCallback& progress = {});

// Audio track only (container / codec from the output extension or settings)
VideoFXResult VideoFX_ExtractAudio(const std::string& inputPath, const std::string& outputPath,
                                   const VideoFXExportSettings& settings = {},
                                   const VideoFXProgressCallback& progress = {});

// Cut without re-encoding: fast and lossless, but the cut starts at the
// keyframe at or before `start`. Streams are copied into the output container.
VideoFXResult VideoFX_TrimLossless(const std::string& inputPath, const std::string& outputPath,
                                   double start, double end,
                                   const VideoFXProgressCallback& progress = {});

// A generated test clip (moving pattern + tone) - for tests and demos
VideoFXResult VideoFX_GenerateTestClip(const std::string& outputPath, double seconds,
                                       int width = 640, int height = 360,
                                       double frameRate = 25.0, bool withAudio = true);

// ============================================================================
// BACKGROUND EXPORT
// ============================================================================
// Runs VideoFX_Export on its own thread; poll it from a UI timer.
//
//     auto job = std::make_shared<VideoFXExportJob>();
//     job->Start(segments, "out.mp4", settings);
//     ... progressGauge->SetValue(job->GetProgress()); ...
//     if (!job->IsRunning()) { auto r = job->GetResult(); }
class VideoFXExportJob {
public:
    VideoFXExportJob() = default;
    ~VideoFXExportJob();                 // cancels and joins a running export
    VideoFXExportJob(const VideoFXExportJob&) = delete;
    VideoFXExportJob& operator=(const VideoFXExportJob&) = delete;

    // false if a job is still running
    bool Start(std::vector<VideoFXSegment> segments, std::string outputPath,
               VideoFXExportSettings settings = {});
    void Cancel();                       // asynchronous; IsRunning() turns false soon after
    VideoFXResult Wait();                // blocks until finished

    bool IsRunning() const { return running.load(); }
    double GetProgress() const { return progress.load(); }   // 0..1
    VideoFXResult GetResult() const { return result.load(); }
    std::string GetError() const;

private:
    std::thread worker;
    std::atomic<bool> running{false};
    std::atomic<bool> cancelRequested{false};
    std::atomic<double> progress{0.0};
    std::atomic<VideoFXResult> result{VideoFXResult::Ok};
    mutable std::mutex errorMutex;
    std::string error;
};

} // namespace VideoFX
