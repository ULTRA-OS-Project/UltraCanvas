// UltraAI/adapters/mock/src/MockTextToSpeech.cpp
// Version: 0.1.0

#include "UltraAIMockTextToSpeech.h"

#include <atomic>
#include <deque>
#include <thread>

namespace UltraAI {

namespace {

class MockStreamHandle : public IStreamHandle {
public:
    void Cancel() override { cancelled.store(true); }
    bool IsDone() const override { return done.load(); }
    void MarkDone() { done.store(true); }
    bool IsCancelled() const { return cancelled.load(); }

    void AttachThread(std::thread t) {
        std::lock_guard<std::mutex> lock(mu);
        worker = std::move(t);
    }
    ~MockStreamHandle() override {
        std::lock_guard<std::mutex> lock(mu);
        if (worker.joinable()) worker.join();
    }
private:
    std::atomic<bool> cancelled{false};
    std::atomic<bool> done{false};
    std::mutex mu;
    std::thread worker;
};

const char* MimeForFormat(TtsAudioFormat f) {
    switch (f) {
        case TtsAudioFormat::Mp3:      return "audio/mpeg";
        case TtsAudioFormat::Wav:      return "audio/wav";
        case TtsAudioFormat::Flac:     return "audio/flac";
        case TtsAudioFormat::Opus:     return "audio/opus";
        case TtsAudioFormat::Ogg:      return "audio/ogg";
        case TtsAudioFormat::PcmS16Le: return "audio/L16";
        case TtsAudioFormat::PcmF32Le: return "audio/L32";
    }
    return "application/octet-stream";
}

std::vector<uint8_t> SynthesizeBytes(const SpeakRequest& req) {
    // Each character becomes one byte (low-fidelity placeholder).
    std::vector<uint8_t> out;
    out.reserve(req.text.size());
    for (char c : req.text) out.push_back(static_cast<uint8_t>(c));
    return out;
}

} // namespace

struct MockTextToSpeech::Impl {
    std::mutex mu;
    std::deque<std::pair<std::vector<uint8_t>, std::string>> scripted; // bytes, mime
    std::deque<Error> errors;
    size_t streamChunkSize = 16;
    std::chrono::milliseconds streamInterval{1};
    int callCount = 0;
};

MockTextToSpeech::MockTextToSpeech() : impl_(std::make_unique<Impl>()) {}
MockTextToSpeech::~MockTextToSpeech() = default;

void MockTextToSpeech::EnqueueAudio(std::vector<uint8_t> bytes,
                                    std::string mimeType) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->scripted.emplace_back(std::move(bytes), std::move(mimeType));
}
void MockTextToSpeech::EnqueueError(Error error) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->errors.push_back(std::move(error));
}
void MockTextToSpeech::SetStreamChunkSize(size_t bytesPerChunk) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->streamChunkSize = bytesPerChunk == 0 ? 1 : bytesPerChunk;
}
void MockTextToSpeech::SetStreamChunkInterval(std::chrono::milliseconds interval) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->streamInterval = interval;
}
int MockTextToSpeech::CallCount() const {
    std::lock_guard<std::mutex> lock(impl_->mu);
    return impl_->callCount;
}

TTSProviderCapabilities MockTextToSpeech::GetCapabilities() const {
    TTSProviderCapabilities caps;
    caps.providerId = "mock";
    TTSModelInfo m;
    m.id = "mock-tts-1";
    m.displayName = "UltraAI Mock TTS";
    m.supportedLanguages = { "en", "de" };
    m.supportedFormats = { TtsAudioFormat::Mp3, TtsAudioFormat::Wav,
                           TtsAudioFormat::PcmS16Le };
    m.supportsSsml = true;
    m.supportsStreaming = true;
    m.supportsVoiceCloning = true;
    m.runsLocally = true;
    caps.models.push_back(std::move(m));
    return caps;
}

std::vector<VoiceInfo> MockTextToSpeech::ListVoices(const std::string& language) {
    std::vector<VoiceInfo> all;
    auto add = [&](const char* id, const char* name, const char* lang,
                   VoiceGender g) {
        VoiceInfo v;
        v.id = id; v.displayName = name; v.language = lang; v.gender = g;
        v.runsLocally = true;
        all.push_back(std::move(v));
    };
    add("mock-aria",  "Aria",  "en", VoiceGender::Female);
    add("mock-leo",   "Leo",   "en", VoiceGender::Male);
    add("mock-greta", "Greta", "de", VoiceGender::Female);
    if (language.empty()) return all;
    std::vector<VoiceInfo> filtered;
    for (auto& v : all) if (v.language == language) filtered.push_back(std::move(v));
    return filtered;
}

SpeakResponse MockTextToSpeech::Speak(const SpeakRequest& request) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    ++impl_->callCount;

    SpeakResponse resp;
    if (!impl_->errors.empty()) {
        resp.error = impl_->errors.front();
        impl_->errors.pop_front();
        return resp;
    }

    if (!impl_->scripted.empty()) {
        auto pair = std::move(impl_->scripted.front());
        impl_->scripted.pop_front();
        resp.audio.bytes = std::move(pair.first);
        resp.audio.mimeType = std::move(pair.second);
    } else {
        resp.audio.bytes = SynthesizeBytes(request);
        resp.audio.mimeType = MimeForFormat(request.format);
    }
    resp.durationSec =
        static_cast<double>(resp.audio.bytes.size()) / 16000.0;  // arbitrary
    resp.usage.units = static_cast<int32_t>(request.text.size());
    return resp;
}

std::future<SpeakResponse> MockTextToSpeech::SpeakAsync(const SpeakRequest& request) {
    return std::async(std::launch::async,
                      [this, request]() { return Speak(request); });
}

StreamHandle MockTextToSpeech::SpeakStream(const SpeakRequest& request,
                                           TtsStreamCallback onEvent) {
    auto handle = std::make_shared<MockStreamHandle>();
    auto resp = Speak(request);
    auto chunkSize = impl_->streamChunkSize;
    auto interval  = impl_->streamInterval;

    std::thread worker([handle, resp, chunkSize, interval, onEvent]() {
        if (resp.error.code != ErrorCode::None) {
            TtsStreamEvent ev;
            ev.kind = TtsStreamEventKind::Error;
            ev.error = resp.error;
            if (onEvent) onEvent(ev);
            handle->MarkDone();
            return;
        }
        const auto& bytes = resp.audio.bytes;
        for (size_t i = 0; i < bytes.size(); i += chunkSize) {
            if (handle->IsCancelled()) break;
            TtsStreamEvent ev;
            ev.kind = TtsStreamEventKind::AudioChunk;
            size_t end = std::min(i + chunkSize, bytes.size());
            ev.audioChunk.assign(bytes.begin() + i, bytes.begin() + end);
            ev.timestampSec = static_cast<double>(i) / 16000.0;
            if (onEvent) onEvent(ev);
            std::this_thread::sleep_for(interval);
        }
        TtsStreamEvent done;
        done.kind = TtsStreamEventKind::Done;
        if (onEvent) onEvent(done);
        handle->MarkDone();
    });
    handle->AttachThread(std::move(worker));
    return handle;
}

CloneVoiceResponse MockTextToSpeech::CloneVoice(const CloneVoiceRequest& request) {
    CloneVoiceResponse r;
    r.voice.id = "mock-cloned-" + std::to_string(impl_->callCount + 1);
    r.voice.displayName = request.displayName.empty() ? "Cloned Voice"
                                                       : request.displayName;
    r.voice.isCloned = true;
    r.voice.runsLocally = true;
    return r;
}

std::unique_ptr<ITextToSpeech> CreateMockTextToSpeech() {
    return std::make_unique<MockTextToSpeech>();
}

} // namespace UltraAI
