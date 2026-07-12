// UltraAI/adapters/mock/src/MockVideoGen.cpp
// Version: 0.1.0

#include "UltraAIMockVideoGen.h"

#include <atomic>
#include <deque>
#include <thread>

namespace UltraAI {

namespace {

class JobHandle : public IStreamHandle {
public:
    void Cancel() override { cancelled.store(true); }
    bool IsDone() const override { return done.load(); }
    void MarkDone() { done.store(true); }
    bool IsCancelled() const { return cancelled.load(); }
    void AttachThread(std::thread t) {
        std::lock_guard<std::mutex> lock(mu);
        worker = std::move(t);
    }
    ~JobHandle() override {
        std::lock_guard<std::mutex> lock(mu);
        if (worker.joinable()) worker.join();
    }
private:
    std::atomic<bool> cancelled{false};
    std::atomic<bool> done{false};
    std::mutex mu;
    std::thread worker;
};

const char* MimeForFormat(VideoFormat f) {
    switch (f) {
        case VideoFormat::Auto:  return "video/mp4";
        case VideoFormat::Mp4:   return "video/mp4";
        case VideoFormat::Webm:  return "video/webm";
        case VideoFormat::Mov:   return "video/quicktime";
        case VideoFormat::Gif:   return "image/gif";
        case VideoFormat::Hevc:  return "video/H265";
    }
    return "application/octet-stream";
}

GeneratedVideo SynthesizeVideo(const VideoGenRequest& req, int /*idx*/) {
    GeneratedVideo v;
    // 4-byte ftyp box prefix so the bytes look like an MP4 to crude inspectors.
    v.video.bytes = { 'f','t','y','p','i','s','o','m' };
    v.video.mimeType = MimeForFormat(req.outputFormat);
    v.thumbnail.bytes = { 0x89, 0x50, 0x4E, 0x47 };  // PNG magic
    v.thumbnail.mimeType = "image/png";
    v.width      = req.width      > 0 ? req.width      : 1024;
    v.height     = req.height     > 0 ? req.height     : 576;
    v.fps        = req.fps        > 0 ? req.fps        : 24;
    v.durationSec = req.durationSec > 0 ? req.durationSec : 4.0;
    v.seed       = req.seed.value_or(0);
    return v;
}

} // namespace

struct MockVideoGen::Impl {
    std::mutex mu;
    std::deque<VideoGenResponse> scripted;
    std::deque<Error> errors;
    int jobSteps = 4;
    std::chrono::milliseconds jobInterval{2};
    int callCount = 0;
};

MockVideoGen::MockVideoGen() : impl_(std::make_unique<Impl>()) {}
MockVideoGen::~MockVideoGen() = default;

void MockVideoGen::EnqueueResponse(VideoGenResponse response) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->scripted.push_back(std::move(response));
}
void MockVideoGen::EnqueueError(Error error) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->errors.push_back(std::move(error));
}
void MockVideoGen::SetJobSteps(int steps) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->jobSteps = steps < 1 ? 1 : steps;
}
void MockVideoGen::SetJobStepInterval(std::chrono::milliseconds interval) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->jobInterval = interval;
}
int MockVideoGen::CallCount() const {
    std::lock_guard<std::mutex> lock(impl_->mu);
    return impl_->callCount;
}

VideoGenProviderCapabilities MockVideoGen::GetCapabilities() const {
    VideoGenProviderCapabilities caps;
    caps.providerId = "mock";
    caps.outputFormats = { VideoFormat::Mp4, VideoFormat::Webm, VideoFormat::Gif };
    VideoGenModelInfo m;
    m.id = "mock-video-1";
    m.displayName = "UltraAI Mock Video";
    m.supportedModes = { VideoGenMode::TextToVideo, VideoGenMode::ImageToVideo,
                         VideoGenMode::VideoToVideo, VideoGenMode::FrameInterpolation,
                         VideoGenMode::Upscale };
    m.supportedSizes = { {512, 512}, {1024, 576}, {1920, 1080} };
    m.maxDurationSec = 60.0;
    m.maxFps = 60;
    m.supportsAudioInput = true;
    m.runsLocally = true;
    caps.models.push_back(std::move(m));
    return caps;
}

VideoGenResponse MockVideoGen::Generate(const VideoGenRequest& request) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    ++impl_->callCount;

    VideoGenResponse resp;
    resp.model = request.model.empty() ? "mock-video-1" : request.model;

    if (!impl_->errors.empty()) {
        resp.error = impl_->errors.front();
        impl_->errors.pop_front();
        return resp;
    }
    if (!impl_->scripted.empty()) {
        VideoGenResponse out = std::move(impl_->scripted.front());
        impl_->scripted.pop_front();
        if (out.model.empty()) out.model = resp.model;
        return out;
    }

    int count = request.count > 0 ? request.count : 1;
    resp.videos.reserve(count);
    for (int i = 0; i < count; ++i) resp.videos.push_back(SynthesizeVideo(request, i));
    resp.usage.units = count;
    return resp;
}

std::future<VideoGenResponse> MockVideoGen::GenerateAsync(
    const VideoGenRequest& request) {
    return std::async(std::launch::async,
                      [this, request]() { return Generate(request); });
}

StreamHandle MockVideoGen::GenerateJob(const VideoGenRequest& request,
                                       VideoJobCallback onEvent) {
    auto handle = std::make_shared<JobHandle>();
    auto interval = impl_->jobInterval;
    auto steps    = impl_->jobSteps;
    auto response = Generate(request);

    std::thread worker([handle, response, steps, interval, onEvent]() {
        if (onEvent) {
            VideoJobEvent ev; ev.kind = VideoJobEventKind::Queued;
            onEvent(ev);
        }
        for (int i = 1; i <= steps; ++i) {
            if (handle->IsCancelled()) break;
            std::this_thread::sleep_for(interval);
            if (onEvent) {
                VideoJobEvent ev;
                ev.kind = VideoJobEventKind::InProgress;
                ev.progress = static_cast<double>(i) / static_cast<double>(steps);
                onEvent(ev);
            }
        }
        if (onEvent && !handle->IsCancelled()) {
            VideoJobEvent done;
            if (response.error.code != ErrorCode::None) {
                done.kind = VideoJobEventKind::Error;
                done.error = response.error;
            } else {
                done.kind = VideoJobEventKind::Completed;
                done.result = response;
            }
            onEvent(done);
        }
        handle->MarkDone();
    });
    handle->AttachThread(std::move(worker));
    return handle;
}

std::unique_ptr<IVideoGen> CreateMockVideoGen() {
    return std::make_unique<MockVideoGen>();
}

} // namespace UltraAI
