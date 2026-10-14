// Apps/UltraVideo/UltraVideoPreview.cpp
// The engine's worker thread (UltraVideoPreview.h).
// Version: 0.1.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#include "UltraVideoPreview.h"

#include <utility>

using namespace VideoFX;

namespace UltraVideo {

Preview::Preview() {
    thread = std::thread([this]() { Loop(); });
}

Preview::~Preview() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        quit = true;
    }
    wake.notify_all();
    if (thread.joinable()) thread.join();
}

void Preview::Open(std::vector<VideoFXSegment> segments, VideoFXExportSettings settings) {
    settings.music = VideoFXMusic{};
    {
        std::lock_guard<std::mutex> lock(mutex);
        pendingOpen = OpenRequest{std::move(segments), std::move(settings)};
        ++busy;
    }
    wake.notify_all();
}

void Preview::RequestFrame(double seconds, int maxWidth, int maxHeight) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!pendingFrame) ++busy;
        pendingFrame = FrameRequest{seconds, maxWidth, maxHeight};
    }
    wake.notify_all();
}

void Preview::Run(std::function<void()> work, std::function<void()> done) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        jobs.push_back(Job{std::move(work), std::move(done)});
        ++busy;
    }
    wake.notify_all();
}

void Preview::Pump() {
    std::optional<std::pair<double, VideoFXFrame>> frame;
    std::vector<std::function<void()>> done;
    std::vector<std::string> failed;
    bool openedNow = false;
    {
        std::lock_guard<std::mutex> lock(mutex);
        frame.swap(readyFrame);
        done.swap(readyDone);
        failed.swap(errors);
        openedNow = justOpened;
        justOpened = false;
    }
    if (openedNow && onOpened) onOpened();
    for (const std::string& e : failed) if (onError) onError(e);
    for (auto& d : done) if (d) d();
    if (frame && onFrame) onFrame(frame->first, frame->second);
}

void Preview::Loop() {
    while (true) {
        std::optional<OpenRequest> open;
        std::optional<FrameRequest> frame;
        std::optional<Job> job;
        {
            std::unique_lock<std::mutex> lock(mutex);
            wake.wait(lock, [this]() { return quit || pendingOpen || pendingFrame || !jobs.empty(); });
            if (quit) return;
            if (pendingOpen) {
                open.swap(pendingOpen);
            } else if (pendingFrame) {
                frame.swap(pendingFrame);
            } else {
                job = std::move(jobs.front());
                jobs.pop_front();
            }
        }
        if (open) {
            const VideoFXResult r = engine.Open(open->segments, open->settings);
            std::lock_guard<std::mutex> lock(mutex);
            if (r == VideoFXResult::Ok) {
                duration.store(engine.GetDuration());
                opened.store(true);
                justOpened = true;
            } else {
                duration.store(0.0);
                opened.store(false);
                if (!open->segments.empty()) errors.push_back(VideoFX_GetLastError());
            }
            --busy;
            continue;
        }
        if (frame) {
            VideoFXFrame out;
            VideoFXResult r = VideoFXResult::NotAvailable;
            if (engine.IsOpen()) {
                const double limit = engine.GetDuration();
                double at = frame->seconds;
                if (limit > 0.0 && at >= limit) at = std::max(0.0, limit - 0.5 / std::max(1.0, engine.GetFrameRate()));
                r = engine.RenderFrame(at, out, frame->width, frame->height);
            }
            std::lock_guard<std::mutex> lock(mutex);
            if (r == VideoFXResult::Ok) readyFrame = std::make_pair(frame->seconds, std::move(out));
            --busy;
            continue;
        }
        if (job) {
            if (job->work) job->work();
            std::lock_guard<std::mutex> lock(mutex);
            if (job->done) readyDone.push_back(std::move(job->done));
            --busy;
        }
    }
}

} // namespace UltraVideo
