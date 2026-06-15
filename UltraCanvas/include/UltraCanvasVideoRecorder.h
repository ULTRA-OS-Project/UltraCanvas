// include/UltraCanvasVideoRecorder.h
// Non-visual cross-platform video capture (camera recording) engine
// Version: 0.1.0
// Last Modified: 2026-06-15
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASVIDEORECORDER_H
#define ULTRACANVASVIDEORECORDER_H

#include "UltraCanvasVideo.h"
#include "UltraCanvasVideoDevices.h"
#include <string>
#include <memory>
#include <functional>

namespace UltraCanvas {

// ===== RECORDING STATE =====
enum class VideoRecordingState {
    Idle,           // No camera open
    Preparing,      // Permissions / device init in flight
    Ready,          // Preview running, not writing to disk
    Recording,
    Paused,
    Stopped,        // File finalized
    Error
};

// ===== CAPTURE CONFIG =====
struct VideoCaptureConfig {
    int    width  = 1280;
    int    height = 720;
    double frameRate = 30.0;
    VideoCodec codec = VideoCodec::H264;
    VideoContainer container = VideoContainer::MP4;
    std::string cameraId;            // empty = default camera
    bool   captureAudio = true;
    std::string audioDeviceId;       // empty = default mic
    int    videoBitRate = 0;         // 0 = backend default
    std::string outputPath;          // file written between Start() and Stop()
    size_t maxDurationMs = 0;        // 0 = unlimited
};

// ===== VIDEO RECORDER (NON-VISUAL) =====
class UltraCanvasVideoRecorder {
public:
    UltraCanvasVideoRecorder();
    explicit UltraCanvasVideoRecorder(const VideoCaptureConfig& cfg);
    ~UltraCanvasVideoRecorder();

    UltraCanvasVideoRecorder(const UltraCanvasVideoRecorder&) = delete;
    UltraCanvasVideoRecorder& operator=(const UltraCanvasVideoRecorder&) = delete;

    // ===== SESSION =====
    bool Open();                              // Acquire camera + start preview
    bool Open(const VideoCaptureConfig& cfg); // Reconfigure + open
    void Close();
    bool IsOpen() const;

    // ===== TRANSPORT =====
    bool Start();                             // begin writing to outputPath
    bool Pause();
    bool Resume();
    bool Stop();                              // finalize file

    // ===== PREVIEW (UI thread) =====
    UCVideoFramePtr GetPreviewFrame() const;  // most recent live camera frame

    // ===== STATE =====
    VideoRecordingState GetState() const;
    double GetElapsed() const;                // seconds since Start()
    const std::string& GetLastError() const;
    const VideoCaptureConfig& GetConfig() const;

    // ===== CONFIG =====
    void SetConfig(const VideoCaptureConfig& cfg);   // requires reopen to take effect
    void SetCamera(const std::string& deviceId);
    void SetOutputPath(const std::string& path);

    // ===== EVENTS =====
    std::function<void(VideoRecordingState)> onRecordingStateChanged;
    std::function<void(UCVideoFramePtr)> onPreviewFrame;
    std::function<void(const std::string& filePath)> onSaved;
    std::function<void()> onMaxDurationReached;
    std::function<void(const std::string& message)> onError;
    std::function<void(CameraPermission)> onPermissionChanged;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// ===== FACTORY =====
inline std::shared_ptr<UltraCanvasVideoRecorder> CreateVideoRecorderEngine() {
    return std::make_shared<UltraCanvasVideoRecorder>();
}

} // namespace UltraCanvas

#endif // ULTRACANVASVIDEORECORDER_H
