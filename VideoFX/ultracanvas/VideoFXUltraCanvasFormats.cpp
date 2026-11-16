// VideoFX/ultracanvas/VideoFXUltraCanvasFormats.cpp
// VideoFX's converter in the framework's media codec registry
// (VideoFXUltraCanvasFormats.h).
// Version: 0.1.0
// Last Modified: 2026-11-15
// Author: UltraCanvas Framework
#include "VideoFXUltraCanvasFormats.h"

#include "UltraCanvasMediaCodecRegistry.h"
#include "VideoFX/VideoFX.h"

#include <mutex>

namespace VideoFX {

namespace {

constexpr const char* kProvider = "VideoFX (FFmpeg)";

} // namespace

bool VideoFX_ConvertVideoFile(const std::string& sourcePath, const std::string& targetPath,
                              const std::function<bool(double fraction)>& progress, std::string& error) {
    error.clear();
    VideoFXProgressCallback report;
    if (progress) report = progress;
    // Default settings: the container from the extension, the source's size
    // and rate, the container's usual codecs at their usual quality.
    const VideoFXResult result = VideoFX_Transcode(sourcePath, targetPath, VideoFXExportSettings{}, report);
    if (result == VideoFXResult::Ok) return true;
    if (result == VideoFXResult::Cancelled) {
        error = "Stopped";
        return false;
    }
    error = VideoFX_GetLastError();
    if (error.empty()) error = VideoFX_ResultToString(result);
    return false;
}

int VideoFX_RegisterWithFileLoader() {
    static std::once_flag once;
    static int registered = 0;
    std::call_once(once, [] {
        using namespace UltraCanvas;
        if (!VideoFX_IsAvailable()) return;
        for (const VideoFXFileFormat& format : VideoFX_GetFileFormats()) {
            if (format.kind != VideoFXFileKind::Video || !format.canWrite) continue;
            MediaCodecRegistration codec;
            codec.extension = format.extension;
            codec.aliases = format.aliases;
            codec.description = format.description;
            codec.kind = MediaCodecKind::Video;
            codec.canEncode = true;
            // canDecode stays as the platform backend registered it: a film
            // is still played by the backend, not by VideoFX.
            const auto known = FindMediaCodecByExtension(MediaCodecKind::Video, format.extension);
            const std::string provider = known ? known->provider : std::string();
            if (provider.find("VideoFX") == std::string::npos)
                codec.provider = provider.empty() ? std::string(kProvider) : provider + " + " + kProvider;
            if (!known) codec.notes = "written by VideoFX; played only where the platform video backend reads it";
            codec.convertVideo = [](const std::string& source, const std::string& target,
                                    const MediaProgressFn& progress, std::string& error) {
                return VideoFX_ConvertVideoFile(source, target, progress, error);
            };
            RegisterMediaCodec(codec);
            ++registered;
        }
    });
    return registered;
}

} // namespace VideoFX
