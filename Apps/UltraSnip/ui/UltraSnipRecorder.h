// Apps/UltraSnip/ui/UltraSnipRecorder.h
// The screen recording: a thread that captures one rectangle of the screen
// at the chosen rate (UltraCanvasDesktopShell::CaptureScreenArea) and hands
// each frame, with the moment it was taken, to VideoFX's frame recorder,
// which encodes it into the file as it comes. Pause stops the clock as well
// as the capture, so the recording holds no still stretch; Stop finishes the
// file. No sound in this version.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasCommonTypes.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace VideoFX { class VideoFXFrameRecorder; }

namespace UltraSnip {

class UltraSnipRecorder {
public:
    UltraSnipRecorder();
    ~UltraSnipRecorder();                    // stops a running recording

    // Why a recording cannot start on this build: "" when it can
    static std::string UnavailableReason();

    // Starts recording `area` (screen pixels) into `path` at `frameRate`
    // frames a second; the file's folder is created. False with the reason.
    bool Start(const UltraCanvas::Rect2Di& area, const std::string& path, int frameRate, std::string* error = nullptr);
    void Pause();
    void Resume();
    // Ends the capture, finishes the file; false with the reason when the
    // file could not be completed (it may still be partly playable).
    bool Stop(std::string* error = nullptr);

    bool IsRecording() const { return running_.load(); }
    bool IsPaused() const { return paused_.load(); }
    // Seconds of recording made so far (pauses left out)
    double ElapsedSeconds() const;
    int64_t FrameCount() const { return frames_.load(); }
    const std::string& Path() const { return path_; }
    const UltraCanvas::Rect2Di& Area() const { return area_; }

    // The capture or the encoder failed mid-way: called on the capture
    // thread with the reason; the recording has stopped itself (Stop still
    // finishes the file).
    std::function<void(const std::string&)> onFailed;

private:
    void Run();

    std::unique_ptr<VideoFX::VideoFXFrameRecorder> recorder_;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> paused_{false};
    std::atomic<int64_t> frames_{0};
    UltraCanvas::Rect2Di area_;
    std::string path_;
    int frameRate_ = 20;

    // The recording clock: active time only
    mutable std::mutex clockMutex_;
    double recordedBeforePause_ = 0.0;
    std::chrono::steady_clock::time_point activeSince_;
    bool clockRunning_ = false;
    std::string failure_;
};

} // namespace UltraSnip
