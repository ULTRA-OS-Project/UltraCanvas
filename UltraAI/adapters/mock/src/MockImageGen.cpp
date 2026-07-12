// UltraAI/adapters/mock/src/MockImageGen.cpp
// Version: 0.1.0

#include "UltraAIMockImageGen.h"

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

// 8-byte PNG header so the bytes look like a real image to crude inspectors.
static const std::vector<uint8_t> kPngHeader = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A
};

GeneratedImage SynthesizeImage(const ImageGenRequest& req, int index) {
    GeneratedImage img;
    img.image.bytes = kPngHeader;
    // Tag the bytes with width/height/index so tests can verify metadata.
    auto append = [&](uint32_t v) {
        img.image.bytes.push_back(static_cast<uint8_t>(v >> 24));
        img.image.bytes.push_back(static_cast<uint8_t>(v >> 16));
        img.image.bytes.push_back(static_cast<uint8_t>(v >> 8));
        img.image.bytes.push_back(static_cast<uint8_t>(v));
    };
    append(static_cast<uint32_t>(req.width  > 0 ? req.width  : 512));
    append(static_cast<uint32_t>(req.height > 0 ? req.height : 512));
    append(static_cast<uint32_t>(index));
    img.image.mimeType = "image/png";
    img.seed = req.seed.value_or(0) + static_cast<uint64_t>(index);
    img.flaggedUnsafe = false;
    return img;
}

} // namespace

struct MockImageGen::Impl {
    std::mutex mu;
    std::deque<std::pair<std::vector<uint8_t>, std::string>> scriptedImages;
    std::deque<ImageGenResponse> scriptedResponses;
    std::deque<Error> errors;
    std::chrono::milliseconds jobInterval{2};
    int jobSteps = 4;
    int callCount = 0;
};

MockImageGen::MockImageGen() : impl_(std::make_unique<Impl>()) {}
MockImageGen::~MockImageGen() = default;

void MockImageGen::EnqueueImage(std::vector<uint8_t> bytes, std::string mimeType) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->scriptedImages.emplace_back(std::move(bytes), std::move(mimeType));
}
void MockImageGen::EnqueueResponse(ImageGenResponse response) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->scriptedResponses.push_back(std::move(response));
}
void MockImageGen::EnqueueError(Error error) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->errors.push_back(std::move(error));
}
void MockImageGen::SetJobStepInterval(std::chrono::milliseconds interval) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->jobInterval = interval;
}
void MockImageGen::SetJobSteps(int steps) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->jobSteps = steps < 1 ? 1 : steps;
}
int MockImageGen::CallCount() const {
    std::lock_guard<std::mutex> lock(impl_->mu);
    return impl_->callCount;
}

ImageGenProviderCapabilities MockImageGen::GetCapabilities() const {
    ImageGenProviderCapabilities caps;
    caps.providerId = "mock";
    caps.outputFormats = { ImageFormat::Png, ImageFormat::Jpeg, ImageFormat::Webp };
    ImageGenModelInfo m;
    m.id = "mock-image-1";
    m.displayName = "UltraAI Mock Image";
    m.supportedModes = { ImageGenMode::TextToImage, ImageGenMode::ImageToImage,
                         ImageGenMode::Inpaint, ImageGenMode::Upscale,
                         ImageGenMode::Variation, ImageGenMode::BackgroundRemoval };
    m.supportedControls = { ControlType::None, ControlType::CannyEdge,
                            ControlType::Depth, ControlType::Pose };
    m.supportedSizes = { {512, 512}, {1024, 1024}, {1024, 1792} };
    m.maxBatch = 8;
    m.runsLocally = true;
    caps.models.push_back(std::move(m));
    return caps;
}

ImageGenResponse MockImageGen::Generate(const ImageGenRequest& request) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    ++impl_->callCount;

    ImageGenResponse resp;
    resp.model = request.model.empty() ? "mock-image-1" : request.model;

    if (!impl_->errors.empty()) {
        resp.error = impl_->errors.front();
        impl_->errors.pop_front();
        return resp;
    }
    if (!impl_->scriptedResponses.empty()) {
        ImageGenResponse out = std::move(impl_->scriptedResponses.front());
        impl_->scriptedResponses.pop_front();
        if (out.model.empty()) out.model = resp.model;
        return out;
    }

    int count = request.count > 0 ? request.count : 1;
    resp.images.reserve(count);
    for (int i = 0; i < count; ++i) {
        if (!impl_->scriptedImages.empty()) {
            auto pair = std::move(impl_->scriptedImages.front());
            impl_->scriptedImages.pop_front();
            GeneratedImage img;
            img.image.bytes = std::move(pair.first);
            img.image.mimeType = std::move(pair.second);
            resp.images.push_back(std::move(img));
        } else {
            resp.images.push_back(SynthesizeImage(request, i));
        }
    }
    resp.usage.units = count;
    return resp;
}

std::future<ImageGenResponse> MockImageGen::GenerateAsync(
    const ImageGenRequest& request) {
    return std::async(std::launch::async,
                      [this, request]() { return Generate(request); });
}

StreamHandle MockImageGen::GenerateJob(const ImageGenRequest& request,
                                       ImageJobCallback onEvent) {
    auto handle = std::make_shared<JobHandle>();
    auto interval = impl_->jobInterval;
    auto steps    = impl_->jobSteps;

    // Compute the final response now so we can stream progress against it.
    auto response = Generate(request);

    std::thread worker([handle, response, steps, interval, onEvent]() {
        if (onEvent) {
            ImageJobEvent ev;
            ev.kind = ImageJobEventKind::Queued;
            onEvent(ev);
        }
        for (int i = 1; i <= steps; ++i) {
            if (handle->IsCancelled()) break;
            std::this_thread::sleep_for(interval);
            if (onEvent) {
                ImageJobEvent ev;
                ev.kind = ImageJobEventKind::InProgress;
                ev.progress = static_cast<double>(i) / static_cast<double>(steps);
                onEvent(ev);
            }
        }
        if (onEvent && !handle->IsCancelled()) {
            ImageJobEvent done;
            if (response.error.code != ErrorCode::None) {
                done.kind  = ImageJobEventKind::Error;
                done.error = response.error;
            } else {
                done.kind   = ImageJobEventKind::Completed;
                done.result = response;
            }
            onEvent(done);
        }
        handle->MarkDone();
    });
    handle->AttachThread(std::move(worker));
    return handle;
}

std::unique_ptr<IImageGen> CreateMockImageGen() {
    return std::make_unique<MockImageGen>();
}

} // namespace UltraAI
