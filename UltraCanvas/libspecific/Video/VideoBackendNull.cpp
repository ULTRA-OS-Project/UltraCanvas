// libspecific/Video/VideoBackendNull.cpp
// No-op video backend. Built when ULTRACANVAS_ENABLE_VIDEO is OFF or when no
// platform backend is available. Sessions open-but-do-nothing so app code can
// compile and run unchanged; UltraCanvasVideoDevices::IsAvailable() returns false.
// Version: 0.1.0
// Last Modified: 2026-06-15
// Author: UltraCanvas Framework

#include "IVideoBackend.h"

namespace UltraCanvas {

namespace {

class NullDecodeSession : public IVideoDecodeSession {
public:
    bool Play() override { return false; }
    bool Pause() override { return false; }
    bool Stop() override { return false; }
    bool Seek(double) override { return false; }
    double GetPosition() const override { return 0.0; }
    double GetDuration() const override { return 0.0; }
    const VideoStreamInfo& GetStreamInfo() const override { return info; }
    void SetVolume(float) override {}
    void SetMute(bool) override {}
    void SetLoop(bool) override {}
    void SetPlaybackRate(float) override {}
private:
    VideoStreamInfo info;
};

class NullCaptureSession : public IVideoCaptureSession {
public:
    explicit NullCaptureSession(const VideoCaptureParams& p) : params(p) {}
    bool Open() override { return false; }
    void Close() override {}
    bool IsOpen() const override { return false; }
    bool Start() override { return false; }
    bool Pause() override { return false; }
    bool Resume() override { return false; }
    bool Stop() override { return false; }
    double GetElapsed() const override { return 0.0; }
    const VideoCaptureParams& GetParams() const override { return params; }
private:
    VideoCaptureParams params;
};

class NullBackend : public IVideoBackend {
public:
    bool Initialize() override { return true; }
    void Shutdown() override {}
    std::string GetName() const override { return "null"; }

    std::vector<VideoDeviceInfo> ListCameras() override { return {}; }
    VideoDeviceInfo GetDefaultCamera() override { return {}; }

    CameraPermission GetCameraPermission() override { return CameraPermission::Denied; }
    void RequestCameraPermission(std::function<void(bool)> cb) override { if (cb) cb(false); }

    std::unique_ptr<IVideoDecodeSession> OpenDecoder(const std::string&) override {
        return nullptr;
    }
    std::unique_ptr<IVideoCaptureSession> OpenCapture(const VideoCaptureParams& p) override {
        return std::make_unique<NullCaptureSession>(p);
    }
};

} // namespace

#ifndef ULTRACANVAS_ENABLE_VIDEO
IVideoBackend* GetVideoBackend() {
    static NullBackend backend;
    static bool inited = (backend.Initialize(), true);
    (void)inited;
    return &backend;
}
#else
// When a real backend is compiled in, its translation unit defines
// GetVideoBackend(). The null backend remains available as a fallback for
// platforms where the real backend fails to initialize.
IVideoBackend* GetNullVideoBackend() {
    static NullBackend backend;
    static bool inited = (backend.Initialize(), true);
    (void)inited;
    return &backend;
}
#endif

} // namespace UltraCanvas
