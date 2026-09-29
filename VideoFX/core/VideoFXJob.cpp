// VideoFX/core/VideoFXJob.cpp
// VideoFXExportJob - VideoFX_Export on a worker thread, polled by the UI.
// Backend-independent: also built when FFmpeg is absent.
// Version: 0.1.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework

#include "VideoFX/VideoFX.h"

namespace VideoFX {

VideoFXExportJob::~VideoFXExportJob() {
    Cancel();
    if (worker.joinable()) worker.join();
}

bool VideoFXExportJob::Start(std::vector<VideoFXSegment> segments, std::string outputPath,
                             VideoFXExportSettings settings) {
    if (running.load()) return false;
    if (worker.joinable()) worker.join();
    cancelRequested = false;
    progress = 0.0;
    result = VideoFXResult::Ok;
    {
        std::lock_guard<std::mutex> lock(errorMutex);
        error.clear();
    }
    running = true;
    worker = std::thread([this, segments = std::move(segments), outputPath = std::move(outputPath),
                          settings = std::move(settings)]() {
        const VideoFXResult r = VideoFX_Export(segments, outputPath, settings, [this](double fraction) {
            progress = fraction;
            return !cancelRequested.load();
        });
        {
            std::lock_guard<std::mutex> lock(errorMutex);
            error = r == VideoFXResult::Ok ? std::string() : VideoFX_GetLastError();
        }
        result = r;
        running = false;
    });
    return true;
}

void VideoFXExportJob::Cancel() { cancelRequested = true; }

VideoFXResult VideoFXExportJob::Wait() {
    if (worker.joinable()) worker.join();
    return result.load();
}

std::string VideoFXExportJob::GetError() const {
    std::lock_guard<std::mutex> lock(errorMutex);
    return error;
}

} // namespace VideoFX
