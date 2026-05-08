// UltraAI/adapters/mock/src/MockSpeechToText.cpp
// Version: 0.1.0

#include "UltraAIMockSpeechToText.h"

#include <atomic>
#include <deque>
#include <thread>

namespace UltraAI {

namespace {

class MockLiveTranscriber : public ILiveTranscriber {
public:
    MockLiveTranscriber(TranscriptCallback cb,
                        std::chrono::milliseconds interval,
                        std::string scriptedText)
        : cb_(std::move(cb)), interval_(interval),
          scripted_(std::move(scriptedText)) {}

    ~MockLiveTranscriber() override {
        std::lock_guard<std::mutex> lock(threadMu_);
        if (worker_.joinable()) worker_.join();
    }

    void Cancel() override { cancelled_.store(true); }
    bool IsDone() const override { return done_.load(); }

    void PushAudio(const uint8_t* /*data*/, size_t bytes) override {
        bytesPushed_ += bytes;
    }
    void Finish() override {
        std::thread t([this]() {
            std::string text = !scripted_.empty()
                ? scripted_
                : "transcribed " + std::to_string(bytesPushed_.load()) + " bytes";

            // Emit a partial first.
            if (cb_ && !cancelled_.load()) {
                StreamingTranscriptEvent ev;
                ev.kind = StreamingTranscriptKind::PartialText;
                ev.segment.text = text.substr(0, text.size() / 2);
                cb_(ev);
                std::this_thread::sleep_for(interval_);
            }
            if (cb_ && !cancelled_.load()) {
                StreamingTranscriptEvent ev;
                ev.kind = StreamingTranscriptKind::FinalText;
                ev.segment.text = text;
                ev.segment.startSec = 0.0;
                ev.segment.endSec = 1.0;
                cb_(ev);
            }
            if (cb_) {
                StreamingTranscriptEvent done;
                done.kind = StreamingTranscriptKind::Done;
                cb_(done);
            }
            done_.store(true);
        });
        std::lock_guard<std::mutex> lock(threadMu_);
        worker_ = std::move(t);
    }

private:
    TranscriptCallback cb_;
    std::chrono::milliseconds interval_;
    std::string scripted_;
    std::atomic<size_t> bytesPushed_{0};
    std::atomic<bool> cancelled_{false};
    std::atomic<bool> done_{false};
    std::mutex threadMu_;
    std::thread worker_;
};

} // namespace

struct MockSpeechToText::Impl {
    std::mutex mu;
    std::deque<TranscribeResponse> scripted;
    std::deque<Error> errors;
    std::chrono::milliseconds liveInterval{5};
    int callCount = 0;
};

MockSpeechToText::MockSpeechToText() : impl_(std::make_unique<Impl>()) {}
MockSpeechToText::~MockSpeechToText() = default;

void MockSpeechToText::EnqueueTranscript(std::string text) {
    TranscribeResponse r;
    r.text = std::move(text);
    EnqueueResponse(std::move(r));
}
void MockSpeechToText::EnqueueResponse(TranscribeResponse response) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->scripted.push_back(std::move(response));
}
void MockSpeechToText::EnqueueError(Error error) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->errors.push_back(std::move(error));
}
void MockSpeechToText::SetLiveChunkInterval(std::chrono::milliseconds interval) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->liveInterval = interval;
}
int MockSpeechToText::CallCount() const {
    std::lock_guard<std::mutex> lock(impl_->mu);
    return impl_->callCount;
}

STTProviderCapabilities MockSpeechToText::GetCapabilities() const {
    STTProviderCapabilities caps;
    caps.providerId = "mock";
    caps.supportedEncodings = { AudioEncoding::Auto, AudioEncoding::Wav,
                                AudioEncoding::PcmS16Le, AudioEncoding::Mp3 };
    STTModelInfo m;
    m.id = "mock-stt-1";
    m.displayName = "UltraAI Mock STT";
    m.supportedLanguages = { "en", "de", "fr" };
    m.supportsTranslation = true;
    m.supportsDiarization = true;
    m.supportsWordTimestamps = true;
    m.supportsStreaming = true;
    m.runsLocally = true;
    caps.models.push_back(std::move(m));
    return caps;
}

TranscribeResponse MockSpeechToText::Transcribe(const TranscribeRequest& request) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    ++impl_->callCount;

    if (!impl_->errors.empty()) {
        TranscribeResponse r;
        r.error = impl_->errors.front();
        impl_->errors.pop_front();
        return r;
    }
    if (!impl_->scripted.empty()) {
        TranscribeResponse r = std::move(impl_->scripted.front());
        impl_->scripted.pop_front();
        return r;
    }

    TranscribeResponse r;
    r.detectedLanguage = request.language.empty() ? "en" : request.language;
    r.durationSec = 1.0;
    r.text = "Mock transcript (" +
             std::to_string(request.audio.bytes.size()) + " bytes)";
    TranscriptSegment seg;
    seg.text = r.text;
    seg.startSec = 0.0;
    seg.endSec = r.durationSec;
    r.segments.push_back(std::move(seg));
    r.usage.audioSeconds = r.durationSec;
    return r;
}

std::future<TranscribeResponse> MockSpeechToText::TranscribeAsync(
    const TranscribeRequest& request) {
    return std::async(std::launch::async,
                      [this, request]() { return Transcribe(request); });
}

LiveTranscriber MockSpeechToText::StartLiveTranscribe(
    const TranscribeRequest& request,
    TranscriptCallback onEvent) {
    std::string scripted;
    {
        std::lock_guard<std::mutex> lock(impl_->mu);
        if (!impl_->scripted.empty()) {
            scripted = impl_->scripted.front().text;
            impl_->scripted.pop_front();
        }
    }
    auto interval = impl_->liveInterval;
    return std::make_shared<MockLiveTranscriber>(std::move(onEvent),
                                                 interval,
                                                 std::move(scripted));
}

std::unique_ptr<ISpeechToText> CreateMockSpeechToText() {
    return std::make_unique<MockSpeechToText>();
}

} // namespace UltraAI
