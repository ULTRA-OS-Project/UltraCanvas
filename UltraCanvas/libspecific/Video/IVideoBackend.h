// libspecific/Video/IVideoBackend.h
// Abstract video backend interface (one impl per platform media framework)
// Version: 0.1.0
// Last Modified: 2026-06-15
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasVideo.h"
#include "UltraCanvasVideoDevices.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== DECODE SESSION =====
// Wraps one open media source. Demuxes + decodes video to RGBA frames and (when
// present) plays the audio track through the platform's own audio sink so A/V
// stays in sync against the backend clock. Frames arrive via onFrame on a
// backend thread; the engine buffers the latest one for the UI thread.
class IVideoDecodeSession {
public:
    virtual ~IVideoDecodeSession() = default;

    virtual bool Play() = 0;
    virtual bool Pause() = 0;
    virtual bool Stop() = 0;
    virtual bool Seek(double seconds) = 0;

    virtual double GetPosition() const = 0;     // seconds
    virtual double GetDuration() const = 0;     // seconds
    virtual const VideoStreamInfo& GetStreamInfo() const = 0;

    virtual void SetVolume(float v) = 0;        // 0..1
    virtual void SetMute(bool mute) = 0;
    virtual void SetLoop(bool loop) = 0;
    virtual void SetPlaybackRate(float rate) = 0;

    // Backend → engine signalling (invoked on a backend thread).
    std::function<void()> onLoaded;                          // stream info available
    std::function<void(UCVideoFramePtr)> onFrame;            // a frame is ready to show
    std::function<void()> onEnded;
    std::function<void(const std::string&)> onError;
};

// ===== CAPTURE SESSION =====
// Wraps an open camera (+ optional mic) feeding a live preview to the UI and,
// while recording, muxing to a file. Preview frames arrive via onPreviewFrame.
struct VideoCaptureParams {
    std::string cameraId;          // empty = default camera
    int    width  = 1280;
    int    height = 720;
    double frameRate = 30.0;
    VideoCodec codec = VideoCodec::H264;
    VideoContainer container = VideoContainer::MP4;
    bool   captureAudio = true;
    std::string audioDeviceId;     // empty = default mic
    int    videoBitRate = 0;       // 0 = backend default
    std::string outputPath;        // file written between Start() and Stop()
};

class IVideoCaptureSession {
public:
    virtual ~IVideoCaptureSession() = default;

    // Opens the camera and begins the preview (no file written yet).
    virtual bool Open() = 0;
    virtual void Close() = 0;
    virtual bool IsOpen() const = 0;

    // File recording control. Start() begins muxing to params.outputPath.
    virtual bool Start() = 0;
    virtual bool Pause() = 0;
    virtual bool Resume() = 0;
    virtual bool Stop() = 0;                     // finalize the file

    virtual double GetElapsed() const = 0;       // seconds since Start()
    virtual const VideoCaptureParams& GetParams() const = 0;

    std::function<void(UCVideoFramePtr)> onPreviewFrame;   // live camera frame
    std::function<void()> onStarted;
    std::function<void()> onStopped;
    std::function<void(const std::string&)> onError;
};

// ===== BACKEND =====
class IVideoBackend {
public:
    virtual ~IVideoBackend() = default;

    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    virtual std::string GetName() const = 0;

    virtual std::vector<VideoDeviceInfo> ListCameras() = 0;
    virtual VideoDeviceInfo GetDefaultCamera() = 0;

    virtual CameraPermission GetCameraPermission() = 0;
    virtual void RequestCameraPermission(std::function<void(bool)> cb) = 0;

    // Returns null when the source can't be opened / format isn't supported.
    virtual std::unique_ptr<IVideoDecodeSession> OpenDecoder(const std::string& source) = 0;
    virtual std::unique_ptr<IVideoCaptureSession> OpenCapture(const VideoCaptureParams& params) = 0;
};

// ===== ACCESSOR =====
// Returns the backend selected at build time. The null backend (which reports
// IsAvailable() == false through UltraCanvasVideoDevices) is returned when
// ULTRACANVAS_ENABLE_VIDEO is OFF or no platform backend initializes.
IVideoBackend* GetVideoBackend();

} // namespace UltraCanvas
