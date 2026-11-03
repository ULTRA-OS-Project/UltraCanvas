// core/UltraRTC/UltraRTCVideoDevices.cpp
// UltraRTC - the camera of a video call, on the framework's video capture:
// UltraCanvasVideoRecorder's live preview (GStreamer, Media Foundation,
// AVFoundation, Android), with nothing recorded. Compiled into the core
// library, where that class lives.
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "UltraRTCVideoDevices.h"

#include "UltraCanvasVideo.h"
#include "UltraCanvasVideoRecorder.h"

namespace UltraRtcInternal {

namespace {

class RecorderCamera final : public CameraCapture {
public:
    bool Open(const std::string& deviceId, int width, int height, int fps, CameraFrameCallback onFrame,
              std::string& error) {
        UltraCanvas::VideoCaptureConfig config;
        config.width = width;
        config.height = height;
        config.frameRate = fps;
        config.cameraId = deviceId;
        config.captureAudio = false;            // the call's audio track has the microphone
        recorder_.onPreviewFrame = [onFrame](UltraCanvas::UCVideoFramePtr frame) {
            if (!frame || !frame->IsValid()) return;
            const int stride = frame->GetStride() > 0 ? frame->GetStride() : frame->GetWidth() * 4;
            onFrame(frame->GetData(), frame->GetWidth(), frame->GetHeight(), stride,
                    frame->GetInfo().pixelFormat == UltraCanvas::VideoPixelFormat::BGRA32);
        };
        if (!recorder_.Open(config)) {
            error = recorder_.GetLastError().empty() ? "The camera could not be opened" : recorder_.GetLastError();
            recorder_.onPreviewFrame = nullptr;
            return false;
        }
        return true;
    }

    void Stop() override {
        recorder_.Close();
        recorder_.onPreviewFrame = nullptr;
    }

    ~RecorderCamera() override { Stop(); }

private:
    UltraCanvas::UltraCanvasVideoRecorder recorder_;
};

} // namespace

std::unique_ptr<CameraCapture> OpenCamera(const std::string& deviceId, int width, int height, int fps,
                                          CameraFrameCallback onFrame, std::string& error) {
    auto camera = std::make_unique<RecorderCamera>();
    if (!camera->Open(deviceId, width, height, fps, std::move(onFrame), error)) return nullptr;
    return camera;
}

} // namespace UltraRtcInternal
