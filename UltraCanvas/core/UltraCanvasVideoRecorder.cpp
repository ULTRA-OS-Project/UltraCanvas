// core/UltraCanvasVideoRecorder.cpp
// Non-visual video capture engine; wraps a backend capture session
// Version: 0.1.0
// Last Modified: 2026-06-15
// Author: UltraCanvas Framework

#include "UltraCanvasVideoRecorder.h"
#include "../libspecific/Video/IVideoBackend.h"
#include <mutex>

namespace UltraCanvas {

namespace {
VideoCaptureParams ToParams(const VideoCaptureConfig& c) {
    VideoCaptureParams p;
    p.cameraId     = c.cameraId;
    p.width        = c.width;
    p.height       = c.height;
    p.frameRate    = c.frameRate;
    p.codec        = c.codec;
    p.container    = c.container;
    p.captureAudio = c.captureAudio;
    p.audioDeviceId= c.audioDeviceId;
    p.videoBitRate = c.videoBitRate;
    p.outputPath   = c.outputPath;
    return p;
}
} // namespace

struct UltraCanvasVideoRecorder::Impl {
    VideoCaptureConfig config;
    std::unique_ptr<IVideoCaptureSession> session;
    VideoRecordingState state = VideoRecordingState::Idle;
    std::string lastError;

    mutable std::mutex frameMutex;
    UCVideoFramePtr previewFrame;

    UltraCanvasVideoRecorder* owner = nullptr;

    void SetState(VideoRecordingState s) {
        if (state == s) return;
        state = s;
        if (owner->onRecordingStateChanged) owner->onRecordingStateChanged(s);
    }

    void Fail(const std::string& msg) {
        lastError = msg;
        SetState(VideoRecordingState::Error);
        if (owner->onError) owner->onError(msg);
    }

    bool Open() {
        Close();
        auto* backend = GetVideoBackend();
        if (!backend) { Fail("No video backend available"); return false; }

        SetState(VideoRecordingState::Preparing);

        CameraPermission perm = backend->GetCameraPermission();
        if (owner->onPermissionChanged) owner->onPermissionChanged(perm);
        if (perm == CameraPermission::Denied || perm == CameraPermission::Restricted) {
            Fail("Camera permission denied");
            return false;
        }

        session = backend->OpenCapture(ToParams(config));
        if (!session) { Fail("Failed to open camera"); return false; }

        IVideoCaptureSession* s = session.get();
        s->onPreviewFrame = [this](UCVideoFramePtr f) {
            {
                std::lock_guard<std::mutex> lk(frameMutex);
                previewFrame = f;
            }
            if (owner->onPreviewFrame) owner->onPreviewFrame(f);
        };
        s->onStarted = [this]() { SetState(VideoRecordingState::Recording); };
        s->onStopped = [this]() {
            SetState(VideoRecordingState::Stopped);
            if (owner->onSaved) owner->onSaved(config.outputPath);
        };
        s->onError = [this](const std::string& msg) { Fail(msg); };

        if (!session->Open()) { Fail(lastError.empty() ? "Camera open failed" : lastError); return false; }
        SetState(VideoRecordingState::Ready);
        return true;
    }

    void Close() {
        if (session) { session->Close(); session.reset(); }
        {
            std::lock_guard<std::mutex> lk(frameMutex);
            previewFrame.reset();
        }
        state = VideoRecordingState::Idle;
    }
};

UltraCanvasVideoRecorder::UltraCanvasVideoRecorder() : impl(std::make_unique<Impl>()) {
    impl->owner = this;
}
UltraCanvasVideoRecorder::UltraCanvasVideoRecorder(const VideoCaptureConfig& cfg)
    : impl(std::make_unique<Impl>()) {
    impl->owner = this;
    impl->config = cfg;
}
UltraCanvasVideoRecorder::~UltraCanvasVideoRecorder() = default;

bool UltraCanvasVideoRecorder::Open() { return impl->Open(); }
bool UltraCanvasVideoRecorder::Open(const VideoCaptureConfig& cfg) {
    impl->config = cfg;
    return impl->Open();
}
void UltraCanvasVideoRecorder::Close() { impl->Close(); }
bool UltraCanvasVideoRecorder::IsOpen() const { return impl->session && impl->session->IsOpen(); }

bool UltraCanvasVideoRecorder::Start() {
    if (!IsOpen() && !impl->Open()) return false;
    return impl->session->Start();
}
bool UltraCanvasVideoRecorder::Pause() {
    if (!impl->session) return false;
    bool ok = impl->session->Pause();
    if (ok) impl->SetState(VideoRecordingState::Paused);
    return ok;
}
bool UltraCanvasVideoRecorder::Resume() {
    if (!impl->session) return false;
    bool ok = impl->session->Resume();
    if (ok) impl->SetState(VideoRecordingState::Recording);
    return ok;
}
bool UltraCanvasVideoRecorder::Stop() {
    if (!impl->session) return false;
    return impl->session->Stop();
}

UCVideoFramePtr UltraCanvasVideoRecorder::GetPreviewFrame() const {
    std::lock_guard<std::mutex> lk(impl->frameMutex);
    return impl->previewFrame;
}

VideoRecordingState UltraCanvasVideoRecorder::GetState() const { return impl->state; }
double UltraCanvasVideoRecorder::GetElapsed() const {
    return impl->session ? impl->session->GetElapsed() : 0.0;
}
const std::string& UltraCanvasVideoRecorder::GetLastError() const { return impl->lastError; }
const VideoCaptureConfig& UltraCanvasVideoRecorder::GetConfig() const { return impl->config; }

void UltraCanvasVideoRecorder::SetConfig(const VideoCaptureConfig& cfg) { impl->config = cfg; }
void UltraCanvasVideoRecorder::SetCamera(const std::string& deviceId) { impl->config.cameraId = deviceId; }
void UltraCanvasVideoRecorder::SetOutputPath(const std::string& path) { impl->config.outputPath = path; }

} // namespace UltraCanvas
