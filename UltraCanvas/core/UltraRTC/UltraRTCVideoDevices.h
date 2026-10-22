// core/UltraRTC/UltraRTCVideoDevices.h
// UltraRTC - the camera of a video call (internal). The engine reaches the
// camera only through this, so the device code - the framework's own video
// capture - stays in one file.
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace UltraRtcInternal {

class CameraCapture {
public:
    virtual ~CameraCapture() = default;
    virtual void Stop() = 0;
};

// A captured picture, 8 bits per channel, on the capture backend's thread;
// valid only during the call. `bgra`: byte order B, G, R, A (else R, G, B, A).
using CameraFrameCallback = std::function<void(const uint8_t* pixels, int width, int height, int stride, bool bgra)>;

// nullptr and `error` when the camera cannot be opened (or the build has no
// video capture backend). deviceId "" = the default camera.
std::unique_ptr<CameraCapture> OpenCamera(const std::string& deviceId, int width, int height, int fps,
                                          CameraFrameCallback onFrame, std::string& error);

} // namespace UltraRtcInternal
