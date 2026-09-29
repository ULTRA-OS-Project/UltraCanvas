// VideoFX/core/VideoFXUnavailable.cpp
// Built instead of the FFmpeg sources when FFmpeg is not found: the API links,
// VideoFX_IsAvailable() says false, and every operation returns NotAvailable.
// Version: 0.1.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework

#include "VideoFX/VideoFX.h"

namespace VideoFX {

#ifndef VIDEOFX_VERSION_STRING
#define VIDEOFX_VERSION_STRING "0.1.0"
#endif

namespace {
    const char* kReason = "VideoFX was built without FFmpeg";
    VideoFXResult Unavailable() { return VideoFXResult::NotAvailable; }
}

std::string VideoFX_GetVersion() { return VIDEOFX_VERSION_STRING; }
std::string VideoFX_GetBackendVersion() { return "unavailable"; }
bool VideoFX_IsAvailable() { return false; }
std::string VideoFX_GetLastError() { return kReason; }
bool VideoFX_IsVideoEncoderAvailable(VideoFXVideoCodec) { return false; }
bool VideoFX_IsAudioEncoderAvailable(VideoFXAudioCodec) { return false; }
void VideoFX_SetVerboseLogging(bool) {}

const char* VideoFX_ResultToString(VideoFXResult result) {
    return result == VideoFXResult::Ok ? "Ok" : "VideoFX not available";
}

VideoFXResult VideoFX_Probe(const std::string&, VideoFXMediaInfo&) { return Unavailable(); }
VideoFXResult VideoFX_ExtractFrame(const std::string&, double, VideoFXFrame&, int, int) { return Unavailable(); }
VideoFXResult VideoFX_ExtractThumbnails(const std::string&, int, std::vector<VideoFXFrame>&, int, int) {
    return Unavailable();
}
VideoFXResult VideoFX_SaveFrameImage(const VideoFXFrame&, const std::string&) { return Unavailable(); }
VideoFXResult VideoFX_Export(const std::vector<VideoFXSegment>&, const std::string&, const VideoFXExportSettings&,
                             const VideoFXProgressCallback&) {
    return Unavailable();
}
VideoFXResult VideoFX_Transcode(const std::string&, const std::string&, const VideoFXExportSettings&,
                                const VideoFXProgressCallback&) {
    return Unavailable();
}
VideoFXResult VideoFX_Trim(const std::string&, const std::string&, double, double, const VideoFXExportSettings&,
                           const VideoFXProgressCallback&) {
    return Unavailable();
}
VideoFXResult VideoFX_ApplyEffects(const std::string&, const std::string&, const std::vector<VideoFXEffect>&,
                                   const VideoFXExportSettings&, const VideoFXProgressCallback&) {
    return Unavailable();
}
VideoFXResult VideoFX_Concatenate(const std::vector<std::string>&, const std::string&, const VideoFXExportSettings&,
                                  const VideoFXProgressCallback&) {
    return Unavailable();
}
VideoFXResult VideoFX_ExtractAudio(const std::string&, const std::string&, const VideoFXExportSettings&,
                                   const VideoFXProgressCallback&) {
    return Unavailable();
}
VideoFXResult VideoFX_TrimLossless(const std::string&, const std::string&, double, double,
                                   const VideoFXProgressCallback&) {
    return Unavailable();
}
VideoFXResult VideoFX_GenerateTestClip(const std::string&, double, int, int, double, bool) { return Unavailable(); }

} // namespace VideoFX
