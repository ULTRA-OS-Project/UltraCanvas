// VideoFX/include/VideoFX/VideoFX.h
// Public API of the VideoFX module - video probing, frame extraction, and a
// segment timeline that is trimmed, filtered, joined and encoded to a file.
// Version: 0.7.0
// Last Modified: 2026-10-08
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

// VideoFX's own version, as set by project() in VideoFX/CMakeLists.txt
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

// The font of text overlays that set no fontPath. Unless set, VideoFX takes
// the framework's bundled Ubuntu font from next to the application
// (share/media/fonts, Resources/media/fonts - where UltraCanvas apps ship
// media/), then a system sans font, then fontconfig's "Sans". An export whose
// text has no usable font fails up front with NotAvailable.
// Returns false (and changes nothing) when the file does not exist;
// "" returns to the automatic choice.
bool VideoFX_SetDefaultFontPath(const std::string& path);

// The font file that will be used; "" = none found (fontconfig, if any)
std::string VideoFX_GetDefaultFontPath();

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

// A video of still images, each moving slowly ("Ken Burns"), joined by
// transitions, with optional captions. Without a size in `settings` the
// output is 1920x1080 at 30 fps.
VideoFXResult VideoFX_CreateSlideshow(const std::vector<std::string>& imagePaths,
                                      const std::string& outputPath,
                                      const VideoFXSlideshowOptions& options = {},
                                      const VideoFXExportSettings& settings = {},
                                      const VideoFXProgressCallback& progress = {});

// The tempo and beat times of a file's sound (music, or a video's
// soundtrack). Finds no beat (bpm 0, Ok) in speech, noise or silence.
VideoFXResult VideoFX_DetectBeats(const std::string& path, VideoFXBeatInfo& info);

// Frontal faces in an image, as fractions of it, largest first - VideoFX's
// built-in detector (OpenCV's trained frontal-face cascade, evaluated by
// VideoFX's own code; no OpenCV is linked). Works without FFmpeg too.
// Finds faces looking roughly at the camera and at least about 1/40 of the
// image's longer side; profiles and heavily tilted heads are missed.
VideoFXResult VideoFX_DetectFaces(const VideoFXFrame& image, std::vector<VideoFXRect>& faces);

// ============================================================================
// PROJECT FILES
// ============================================================================
// Format version this build writes, and the newest it reads
constexpr int kVideoFXProjectFormatVersion = 1;

// Write `project` to `path` (a .vfxproj, UTF-8 JSON). The file is written
// beside the old one and moved over it only when complete, so a crash or a
// full disk never leaves half a project behind. Refused (InvalidArgument):
// media given only as pixels in memory, which a file cannot point to.
VideoFXResult VideoFX_SaveProject(const VideoFXProject& project, const std::string& path);

// Read a project. Paths come back absolute. `missingMedia`, when given, lists
// the media files that no longer exist - the project still loads, so an app
// can offer to find them. A project from a newer VideoFX is refused
// (InvalidArgument) rather than half-read; fields it does not know are ignored.
VideoFXResult VideoFX_LoadProject(const std::string& path, VideoFXProject& project,
                                  std::vector<std::string>* missingMedia = nullptr);

// The same as text, for an app keeping projects elsewhere (a database, an
// undo history). Relative paths are taken against / written relative to
// `baseDirectory` ("" = keep them as they are).
VideoFXResult VideoFX_ProjectToJson(const VideoFXProject& project, std::string& json,
                                    const std::string& baseDirectory = "");
VideoFXResult VideoFX_ProjectFromJson(const std::string& json, VideoFXProject& project,
                                      const std::string& baseDirectory = "");

// Render a project: to `outputPath`, or to its own outputPath when "" is given
VideoFXResult VideoFX_RenderProject(const VideoFXProject& project, const std::string& outputPath = "",
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
