// UltraAI/adapters/mock/src/MockVisionAnalyzer.cpp
// Version: 0.1.0

#include "UltraAIMockVisionAnalyzer.h"

#include <algorithm>
#include <deque>

namespace UltraAI {

namespace {

bool HasTask(const std::vector<VisionTask>& tasks, VisionTask t) {
    return std::find(tasks.begin(), tasks.end(), t) != tasks.end();
}

} // namespace

struct MockVisionAnalyzer::Impl {
    std::mutex mu;
    std::deque<VisionAnalyzeResponse> scripted;
    std::deque<Error> errors;
    int callCount = 0;
};

MockVisionAnalyzer::MockVisionAnalyzer() : impl_(std::make_unique<Impl>()) {}
MockVisionAnalyzer::~MockVisionAnalyzer() = default;

void MockVisionAnalyzer::EnqueueResponse(VisionAnalyzeResponse response) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->scripted.push_back(std::move(response));
}
void MockVisionAnalyzer::EnqueueError(Error error) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->errors.push_back(std::move(error));
}
int MockVisionAnalyzer::CallCount() const {
    std::lock_guard<std::mutex> lock(impl_->mu);
    return impl_->callCount;
}

VisionProviderCapabilities MockVisionAnalyzer::GetCapabilities() const {
    VisionProviderCapabilities caps;
    caps.providerId = "mock";
    VisionModelInfo m;
    m.id = "mock-vision-1";
    m.displayName = "UltraAI Mock Vision";
    m.supportedTasks = {
        VisionTask::Caption, VisionTask::DetailedDescription, VisionTask::Tags,
        VisionTask::ObjectDetection, VisionTask::Ocr, VisionTask::DocumentLayout,
        VisionTask::FaceDetection, VisionTask::ContentSafety,
        VisionTask::VisualQuestion
    };
    m.supportedLanguages = { "en", "de", "fr" };
    m.maxImagePixels = 4096 * 4096;
    m.runsLocally = true;
    caps.models.push_back(std::move(m));
    return caps;
}

VisionAnalyzeResponse MockVisionAnalyzer::Analyze(
    const VisionAnalyzeRequest& request) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    ++impl_->callCount;

    VisionAnalyzeResponse resp;
    resp.model = request.model.empty() ? "mock-vision-1" : request.model;

    if (!impl_->errors.empty()) {
        resp.error = impl_->errors.front();
        impl_->errors.pop_front();
        return resp;
    }
    if (!impl_->scripted.empty()) {
        VisionAnalyzeResponse out = std::move(impl_->scripted.front());
        impl_->scripted.pop_front();
        if (out.model.empty()) out.model = resp.model;
        return out;
    }

    // Synthesize per-task placeholders.
    if (HasTask(request.tasks, VisionTask::Caption))
        resp.caption = "A mock image.";
    if (HasTask(request.tasks, VisionTask::DetailedDescription))
        resp.detailedDescription =
            "A detailed mock description of the supplied image.";
    if (HasTask(request.tasks, VisionTask::Tags)) {
        resp.tags = { {"mock", 0.99}, {"test", 0.95}, {"placeholder", 0.9} };
    }
    if (HasTask(request.tasks, VisionTask::ObjectDetection)) {
        DetectedObject o;
        o.label = "object"; o.confidence = 0.92;
        o.box = { 0.1, 0.1, 0.4, 0.4 };
        resp.objects.push_back(o);
    }
    if (HasTask(request.tasks, VisionTask::Ocr)) {
        resp.ocrText = "MOCK TEXT";
        OcrSpan s;
        s.text = "MOCK TEXT"; s.confidence = 0.97;
        s.polygon.points = { {0.0, 0.0}, {1.0, 0.0}, {1.0, 0.1}, {0.0, 0.1} };
        s.language = "en";
        resp.ocrSpans.push_back(s);
    }
    if (HasTask(request.tasks, VisionTask::DocumentLayout)) {
        DocumentRegion r;
        r.kind = DocumentRegion::Kind::Paragraph;
        r.text = "Mock paragraph";
        r.box = { 0.0, 0.0, 1.0, 0.2 };
        r.readingOrder = 0;
        resp.documentRegions.push_back(r);
    }
    if (HasTask(request.tasks, VisionTask::FaceDetection)) {
        FaceDetection f;
        f.box = { 0.3, 0.2, 0.2, 0.3 };
        f.confidence = 0.88;
        resp.faces.push_back(f);
    }
    if (HasTask(request.tasks, VisionTask::ContentSafety)) {
        resp.safety = {
            {"nsfw", 0.01, false}, {"violence", 0.02, false}
        };
    }
    if (HasTask(request.tasks, VisionTask::VisualQuestion))
        resp.vqaAnswer = "Mock answer to: " + request.prompt;

    resp.usage.units = 1;
    return resp;
}

std::future<VisionAnalyzeResponse> MockVisionAnalyzer::AnalyzeAsync(
    const VisionAnalyzeRequest& request) {
    return std::async(std::launch::async,
                      [this, request]() { return Analyze(request); });
}

std::unique_ptr<IVisionAnalyzer> CreateMockVisionAnalyzer() {
    return std::make_unique<MockVisionAnalyzer>();
}

} // namespace UltraAI
