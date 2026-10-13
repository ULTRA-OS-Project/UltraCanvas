// VideoFX/core/VideoFXUnavailable.cpp
// Built instead of the FFmpeg sources when FFmpeg is not found: the API links,
// VideoFX_IsAvailable() says false, and every operation returns NotAvailable.
// Version: 0.13.0 - VideoFX_GetFileFormats: none
// Version: 0.12.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework

#include "VideoFX/VideoFX.h"
#include "VideoFXFaces.h"
#include "VideoFXProject.h"
#include "VideoFXProxy.h"

#include "../../UltraCanvas/include/UltraCanvasPathUtf8.h"   // PathFromUtf8

#include <filesystem>

namespace VideoFX {

// The version has one home, project(VideoFX VERSION ...) in VideoFX/CMakeLists.txt,
// which passes it in; no copy of the number lives in the code.
#ifndef VIDEOFX_VERSION_STRING
#error "VIDEOFX_VERSION_STRING is set by VideoFX/CMakeLists.txt - build VideoFX through CMake"
#endif

namespace {
    const char* kReason = "VideoFX was built without FFmpeg";
    thread_local std::string projectError;          // project files work here too, and say why they failed
    VideoFXResult ProjectResult(VideoFXResult r, const std::string& error) {
        projectError = r == VideoFXResult::Ok ? std::string() : error;
        return r;
    }
    VideoFXResult Unavailable() { return VideoFXResult::NotAvailable; }
}

std::string VideoFX_GetVersion() { return VIDEOFX_VERSION_STRING; }
std::string VideoFX_GetBackendVersion() { return "unavailable"; }
bool VideoFX_IsAvailable() { return false; }
std::string VideoFX_GetLastError() { return projectError.empty() ? kReason : projectError; }
bool VideoFX_IsVideoEncoderAvailable(VideoFXVideoCodec, VideoFXAcceleration) { return false; }
std::vector<VideoFXEncoderInfo> VideoFX_ListVideoEncoders(VideoFXVideoCodec) { return {}; }
std::string VideoFX_ChooseVideoEncoder(VideoFXVideoCodec, VideoFXAcceleration) { return ""; }
bool VideoFX_IsAudioEncoderAvailable(VideoFXAudioCodec) { return false; }
bool VideoFX_IsTextOverlayAvailable() { return false; }
std::vector<VideoFXFileFormat> VideoFX_GetFileFormats() { return {}; }
bool VideoFX_SetDefaultFontPath(const std::string& path) { return path.empty(); }
std::string VideoFX_GetDefaultFontPath() { return ""; }
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
VideoFXResult VideoFX_CreateSlideshow(const std::vector<std::string>&, const std::string&,
                                      const VideoFXSlideshowOptions&, const VideoFXExportSettings&,
                                      const VideoFXProgressCallback&) {
    return Unavailable();
}
VideoFXResult VideoFX_DetectBeats(const std::string&, VideoFXBeatInfo&) { return Unavailable(); }
VideoFXResult VideoFX_ExtractWaveform(const std::string&, VideoFXWaveform&, double, double, double) {
    return Unavailable();
}

struct VideoFXPreview::Impl {};
VideoFXPreview::VideoFXPreview() = default;
VideoFXPreview::~VideoFXPreview() = default;
VideoFXResult VideoFXPreview::Open(const std::vector<VideoFXSegment>&, const VideoFXExportSettings&) { return Unavailable(); }
void VideoFXPreview::Close() {}
bool VideoFXPreview::IsOpen() const { return false; }
double VideoFXPreview::GetDuration() const { return 0.0; }
int VideoFXPreview::GetWidth() const { return 0; }
int VideoFXPreview::GetHeight() const { return 0; }
double VideoFXPreview::GetFrameRate() const { return 0.0; }
int VideoFXPreview::SegmentAt(double, double*) const { return -1; }
VideoFXResult VideoFXPreview::RenderFrame(double, VideoFXFrame& frame, int, int) {
    frame = VideoFXFrame{};
    return Unavailable();
}
VideoFXResult VideoFX_RenderFrame(const std::vector<VideoFXSegment>&, double, VideoFXFrame& frame,
                                  const VideoFXExportSettings&, int, int) {
    frame = VideoFXFrame{};
    return Unavailable();
}
VideoFXResult VideoFX_PlanSlideshow(const std::vector<std::string>&, const VideoFXSlideshowOptions&,
                                    const VideoFXExportSettings&, std::vector<VideoFXSegment>& segments,
                                    VideoFXExportSettings&) {
    segments.clear();
    return Unavailable();
}

// Project files: pure C++, they load and save without FFmpeg; rendering does not
VideoFXResult VideoFX_SaveProject(const VideoFXProject& project, const std::string& path) {
    std::string error;
    return ProjectResult(Internal::SaveProjectFile(project, path, error), error);
}
VideoFXResult VideoFX_LoadProject(const std::string& path, VideoFXProject& project,
                                  std::vector<std::string>* missingMedia) {
    std::string error;
    return ProjectResult(Internal::LoadProjectFile(path, project, missingMedia, error), error);
}
VideoFXResult VideoFX_ProjectToJson(const VideoFXProject& project, std::string& json, const std::string& base) {
    std::string error;
    return ProjectResult(Internal::ProjectToJsonText(project, base, json, error), error);
}
VideoFXResult VideoFX_ProjectFromJson(const std::string& json, VideoFXProject& project, const std::string& base) {
    std::string error;
    return ProjectResult(Internal::ProjectFromJsonText(json, base, project, error), error);
}
VideoFXResult VideoFX_RenderProject(const VideoFXProject&, const std::string&, const VideoFXProgressCallback&) {
    projectError.clear();
    return Unavailable();
}

// Proxies: where they are, switching to them and clearing them work here
// too; making one needs the engine (its probe answers NotAvailable)
std::string VideoFX_GetDefaultProxyDirectory() { return Internal::DefaultProxyDirectory(); }
std::string VideoFX_ProxyPath(const std::string& clip, const VideoFXProxySettings& settings) {
    return Internal::ProxyPathFor(clip, settings);
}
bool VideoFX_HasProxy(const std::string& clip, const VideoFXProxySettings& settings) {
    const std::string proxy = Internal::ProxyPathFor(clip, settings);
    std::error_code ec;
    return !proxy.empty() && std::filesystem::is_regular_file(UltraCanvas::PathFromUtf8(proxy), ec);
}
VideoFXResult VideoFX_CreateProxy(const std::string& clip, const VideoFXProxySettings& settings,
                                  const VideoFXProgressCallback& progress, std::string* editPath) {
    projectError.clear();
    std::string error;
    return ProjectResult(Internal::CreateProxy(clip, settings, progress, editPath, error), error);
}
VideoFXResult VideoFX_CreateProxies(const std::vector<VideoFXSegment>& segments,
                                    const VideoFXProxySettings& settings, const VideoFXProgressCallback& progress) {
    projectError.clear();
    std::string error;
    return ProjectResult(Internal::CreateProxies(segments, settings, progress, error), error);
}
std::vector<VideoFXSegment> VideoFX_UseProxies(const std::vector<VideoFXSegment>& segments,
                                               const VideoFXProxySettings& settings, int* replaced) {
    return Internal::UseProxies(segments, settings, replaced);
}
VideoFXResult VideoFX_ClearProxies(const VideoFXProxySettings& settings, int* filesRemoved, uint64_t* bytesFreed) {
    std::string error;
    return ProjectResult(Internal::ClearProxies(settings, filesRemoved, bytesFreed, error), error);
}

// Pure C++: works without FFmpeg as well
VideoFXResult VideoFX_DetectFaces(const VideoFXFrame& image, std::vector<VideoFXRect>& faces) {
    faces.clear();
    if (!image.IsValid()) return VideoFXResult::InvalidArgument;
    faces = Internal::DetectFaces(image);
    return VideoFXResult::Ok;
}
VideoFXResult VideoFX_GenerateTestClip(const std::string&, double, int, int, double, bool) { return Unavailable(); }

} // namespace VideoFX
