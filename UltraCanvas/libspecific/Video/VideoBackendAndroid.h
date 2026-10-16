// libspecific/Video/VideoBackendAndroid.h
// Internal to the Android video backend: what its playback half
// (VideoBackendAndroid.cpp) and capture half (VideoBackendAndroidCapture.cpp)
// share.
//
// Version: 1.0.0
// Last Modified: 2026-10-14
// Author: UltraCanvas Framework
#pragma once

#include "IVideoBackend.h"
#include "VideoBackendAndroidImage.h"

#include <memory>
#include <string>
#include <vector>

struct AMediaExtractor;
struct AMediaFormat;

namespace UltraCanvas {
namespace AndroidVideo {

// MediaCodec MIME type -> VideoCodec ("video/avc" -> H264, ...).
VideoCodec CodecFromMime(const std::string& mime);

// Point an extractor at a local path, a file:// URL or a network URL.
bool OpenExtractor(AMediaExtractor* extractor, const std::string& source);

// The YUV matrix and range a decoder's output format declares.
YuvColorInfo ColorInfoFromFormat(AMediaFormat* format, int height);

// Camera2 devices, the first back-facing one marked default.
std::vector<VideoDeviceInfo> ListAndroidCameras();

std::unique_ptr<IVideoCaptureSession> CreateAndroidCaptureSession(const VideoCaptureParams& params);

} // namespace AndroidVideo
} // namespace UltraCanvas
