// Apps/UltraSnip/ui/UltraSnipRecorder.cpp
// See the header.
// Version: 0.2.0 - the texts go through Tr (UltraLocalisation)
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "UltraSnipRecorder.h"

#include "UltraCanvasDesktopShell.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <VideoFX/VideoFX.h>

#include <algorithm>
#include <filesystem>
#include <system_error>

using namespace UltraCanvas;
using namespace VideoFX;

namespace UltraSnip {

UltraSnipRecorder::UltraSnipRecorder() = default;

UltraSnipRecorder::~UltraSnipRecorder() {
    onFailed = nullptr;
    Stop();
}

std::string UltraSnipRecorder::UnavailableReason() {
    if (!VideoFX_IsAvailable()) return Tr("This build has no video engine (VideoFX was built without FFmpeg).");
    if (!VideoFX_IsVideoEncoderAvailable(VideoFXVideoCodec::H264) &&
        !VideoFX_IsVideoEncoderAvailable(VideoFXVideoCodec::MPEG4))
        return Tr("This build's FFmpeg has no encoder a recording could use.");
    if (!UltraCanvasDesktopShell::IsAvailable()) return Tr("The screen cannot be captured on this platform yet.");
    return "";
}

bool UltraSnipRecorder::Start(const Rect2Di& area, const std::string& path, int frameRate, std::string* error) {
    if (running_.load()) {
        if (error) *error = Tr("A recording is already running.");
        return false;
    }
    const std::string reason = UnavailableReason();
    if (!reason.empty()) {
        if (error) *error = reason;
        return false;
    }
    if (area.width <= 0 || area.height <= 0) {
        if (error) *error = Tr("The area to record is empty.");
        return false;
    }
    namespace fs = std::filesystem;
    const fs::path target = PathFromUtf8(path);
    std::error_code ec;
    if (target.has_parent_path()) fs::create_directories(target.parent_path(), ec);
    if (ec) {
        if (error) *error = TrF("The folder {0} could not be created: {1}", PathToUtf8(target.parent_path()), ec.message());
        return false;
    }

    recorder_ = std::make_unique<VideoFXFrameRecorder>();
    VideoFXRecorderSettings settings;
    settings.width = area.width;
    settings.height = area.height;
    settings.frameRate = std::clamp(frameRate, 5, 60);
    if (recorder_->Open(path, settings) != VideoFXResult::Ok) {
        if (error) *error = TrF("The recording could not be started: {0}", VideoFX_GetLastError());
        recorder_.reset();
        return false;
    }
    area_ = area;
    path_ = path;
    frameRate_ = std::clamp(frameRate, 5, 60);
    frames_.store(0);
    failure_.clear();
    stopRequested_.store(false);
    paused_.store(false);
    {
        std::lock_guard<std::mutex> lock(clockMutex_);
        recordedBeforePause_ = 0.0;
        activeSince_ = std::chrono::steady_clock::now();
        clockRunning_ = true;
    }
    running_.store(true);
    thread_ = std::thread([this]() { Run(); });
    return true;
}

double UltraSnipRecorder::ElapsedSeconds() const {
    std::lock_guard<std::mutex> lock(clockMutex_);
    double seconds = recordedBeforePause_;
    if (clockRunning_)
        seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - activeSince_).count();
    return seconds;
}

void UltraSnipRecorder::Pause() {
    if (!running_.load() || paused_.load()) return;
    std::lock_guard<std::mutex> lock(clockMutex_);
    if (clockRunning_) {
        recordedBeforePause_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - activeSince_).count();
        clockRunning_ = false;
    }
    paused_.store(true);
}

void UltraSnipRecorder::Resume() {
    if (!running_.load() || !paused_.load()) return;
    std::lock_guard<std::mutex> lock(clockMutex_);
    activeSince_ = std::chrono::steady_clock::now();
    clockRunning_ = true;
    paused_.store(false);
}

bool UltraSnipRecorder::Stop(std::string* error) {
    if (!recorder_) return true;
    stopRequested_.store(true);
    if (thread_.joinable()) thread_.join();
    {
        std::lock_guard<std::mutex> lock(clockMutex_);
        if (clockRunning_) {
            recordedBeforePause_ +=
                std::chrono::duration<double>(std::chrono::steady_clock::now() - activeSince_).count();
            clockRunning_ = false;
        }
    }
    running_.store(false);
    paused_.store(false);
    const VideoFXResult closed = recorder_->Close();
    recorder_.reset();
    if (!failure_.empty()) {
        if (error) *error = failure_;
        return false;
    }
    if (closed != VideoFXResult::Ok) {
        if (error) *error = TrF("The recording could not be completed: {0}", VideoFX_GetLastError());
        return false;
    }
    return true;
}

void UltraSnipRecorder::Run() {
    using clock = std::chrono::steady_clock;
    const auto interval = std::chrono::duration_cast<clock::duration>(std::chrono::duration<double>(1.0 / frameRate_));
    auto next = clock::now();
    DesktopScreenImage frame;
    while (!stopRequested_.load()) {
        if (paused_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            next = clock::now();
            continue;
        }
        const double at = ElapsedSeconds();
        std::string error;
        if (!UltraCanvasDesktopShell::CaptureScreenArea(area_.x, area_.y, area_.width, area_.height, frame, &error)) {
            failure_ = TrF("The screen could not be captured: {0}", error);
            break;
        }
        if (recorder_->PushFrame(frame.pixels.data(), frame.width, frame.height, frame.stride, at,
                                 VideoFXPixelLayout::BGRA) != VideoFXResult::Ok) {
            failure_ = TrF("The recording failed: {0}", VideoFX_GetLastError());
            break;
        }
        frames_.store(recorder_->GetFrameCount());
        next += interval;
        const auto now = clock::now();
        if (next > now) std::this_thread::sleep_until(next);
        else next = now;        // the capture is slower than the rate: no catching up, the clock places the frames
    }
    if (!failure_.empty()) {
        running_.store(false);
        if (onFailed) onFailed(failure_);
    }
}

} // namespace UltraSnip
