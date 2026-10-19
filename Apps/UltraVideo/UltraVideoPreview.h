// Apps/UltraVideo/UltraVideoPreview.h
// The engine's worker thread: frames of the timeline for the viewer while
// scrubbing (VideoFXPreview, Docs/Research/UltraVideoDesignProposal.md §5.5
// level 2) and any other engine work the window needs off the UI thread -
// thumbnails and peaks for the timeline, probes on import. A frame request
// coalesces: only the latest position is rendered, and a frame that arrives
// after a newer request is dropped. Results come back on the UI thread from
// Pump(), which the window calls from its timer.
// Version: 0.1.0
// Last Modified: 2026-10-11
// Author: UltraCanvas Framework
#pragma once

#include "VideoFX/VideoFX.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace UltraVideo {

class Preview {
public:
    Preview();
    ~Preview();
    Preview(const Preview&) = delete;
    Preview& operator=(const Preview&) = delete;

    // Plan the timeline again (after every applied edit). Music is ignored.
    void Open(std::vector<VideoFX::VideoFXSegment> segments, VideoFX::VideoFXExportSettings settings);
    // The frame at `seconds`, no larger than the viewer; the latest request wins
    void RequestFrame(double seconds, int maxWidth, int maxHeight);
    // Other engine work: `work` runs on the worker, then `done` on the UI thread
    void Run(std::function<void()> work, std::function<void()> done);
    // UI thread: deliver what finished since the last call
    void Pump();

    std::function<void(double seconds, const VideoFX::VideoFXFrame& frame)> onFrame;
    std::function<void(const std::string& error)> onError;
    std::function<void()> onOpened;                 // the plan is ready: GetDuration is current

    double GetDuration() const { return duration.load(); }
    bool IsOpen() const { return opened.load(); }
    bool IsBusy() const { return busy.load() > 0; }

private:
    void Loop();

    struct OpenRequest {
        std::vector<VideoFX::VideoFXSegment> segments;
        VideoFX::VideoFXExportSettings settings;
    };
    struct FrameRequest {
        double seconds = 0.0;
        int width = 0;
        int height = 0;
    };
    struct Job {
        std::function<void()> work;
        std::function<void()> done;
    };

    std::thread thread;
    std::mutex mutex;
    std::condition_variable wake;
    bool quit = false;
    std::optional<OpenRequest> pendingOpen;
    std::optional<FrameRequest> pendingFrame;
    std::deque<Job> jobs;
    // finished, waiting for Pump
    std::optional<std::pair<double, VideoFX::VideoFXFrame>> readyFrame;
    std::vector<std::function<void()>> readyDone;
    std::vector<std::string> errors;
    bool justOpened = false;

    VideoFX::VideoFXPreview engine;                 // touched by the worker only
    std::atomic<double> duration{0.0};
    std::atomic<bool> opened{false};
    std::atomic<int> busy{0};
};

} // namespace UltraVideo
