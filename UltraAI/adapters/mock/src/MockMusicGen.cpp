// UltraAI/adapters/mock/src/MockMusicGen.cpp
// Version: 0.1.0

#include "UltraAIMockMusicGen.h"

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

const char* MimeForFormat(MusicAudioFormat f) {
    switch (f) {
        case MusicAudioFormat::Mp3:  return "audio/mpeg";
        case MusicAudioFormat::Wav:  return "audio/wav";
        case MusicAudioFormat::Flac: return "audio/flac";
        case MusicAudioFormat::Ogg:  return "audio/ogg";
        case MusicAudioFormat::Opus: return "audio/opus";
        case MusicAudioFormat::Midi: return "audio/midi";
    }
    return "application/octet-stream";
}

GeneratedTrack SynthesizeTrack(const MusicGenRequest& req, int idx) {
    GeneratedTrack t;
    // Placeholder bytes scale with requested duration so tests can verify size.
    double dur = req.durationSec > 0.0 ? req.durationSec : 8.0;
    size_t bytes = static_cast<size_t>(dur * 32.0);   // arbitrary
    t.audio.bytes.assign(bytes, static_cast<uint8_t>(0xA5 ^ idx));
    t.audio.mimeType = MimeForFormat(req.outputFormat);
    t.durationSec = dur;
    t.bpm  = req.bpm > 0 ? req.bpm : 120;
    t.key  = req.key.empty() ? "C major" : req.key;
    t.seed = req.seed.value_or(0) + static_cast<uint64_t>(idx);
    if (req.mode == MusicGenMode::Song) {
        t.lyrics = req.lyrics.empty() && req.autoGenerateLyrics
            ? "(auto-generated lyrics for: " + req.prompt + ")"
            : req.lyrics;
    }
    if (req.returnStems) {
        for (const char* name : {"vocals", "drums", "bass", "other"}) {
            MediaBlob s;
            s.bytes = std::vector<uint8_t>(bytes / 4, static_cast<uint8_t>(name[0]));
            s.mimeType = t.audio.mimeType;
            s.filename = std::string(name) + ".track";
            t.stems.push_back(std::move(s));
        }
    }
    return t;
}

} // namespace

struct MockMusicGen::Impl {
    std::mutex mu;
    std::deque<MusicGenResponse> scripted;
    std::deque<Error> errors;
    int jobSteps = 4;
    std::chrono::milliseconds jobInterval{2};
    int callCount = 0;
};

MockMusicGen::MockMusicGen() : impl_(std::make_unique<Impl>()) {}
MockMusicGen::~MockMusicGen() = default;

void MockMusicGen::EnqueueResponse(MusicGenResponse response) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->scripted.push_back(std::move(response));
}
void MockMusicGen::EnqueueError(Error error) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->errors.push_back(std::move(error));
}
void MockMusicGen::SetJobSteps(int steps) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->jobSteps = steps < 1 ? 1 : steps;
}
void MockMusicGen::SetJobStepInterval(std::chrono::milliseconds interval) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    impl_->jobInterval = interval;
}
int MockMusicGen::CallCount() const {
    std::lock_guard<std::mutex> lock(impl_->mu);
    return impl_->callCount;
}

MusicGenProviderCapabilities MockMusicGen::GetCapabilities() const {
    MusicGenProviderCapabilities caps;
    caps.providerId = "mock";
    MusicGenModelInfo m;
    m.id = "mock-music-1";
    m.displayName = "UltraAI Mock Music";
    m.supportedModes = { MusicGenMode::Instrumental, MusicGenMode::Song,
                         MusicGenMode::StyleTransfer, MusicGenMode::Continuation,
                         MusicGenMode::SoundEffect };
    m.supportedGenres = { "pop", "rock", "jazz", "classical", "electronic", "ambient" };
    m.supportedLanguages = { "en", "de", "fr", "es", "ja" };
    m.supportedFormats = { MusicAudioFormat::Mp3, MusicAudioFormat::Wav,
                           MusicAudioFormat::Flac, MusicAudioFormat::Midi };
    m.maxDurationSec = 240.0;
    m.supportsLyrics = true;
    m.supportsVocals = true;
    m.supportsStems = true;
    m.runsLocally = true;
    caps.models.push_back(std::move(m));
    return caps;
}

MusicGenResponse MockMusicGen::Generate(const MusicGenRequest& request) {
    std::lock_guard<std::mutex> lock(impl_->mu);
    ++impl_->callCount;

    MusicGenResponse resp;
    resp.model = request.model.empty() ? "mock-music-1" : request.model;

    if (!impl_->errors.empty()) {
        resp.error = impl_->errors.front();
        impl_->errors.pop_front();
        return resp;
    }
    if (!impl_->scripted.empty()) {
        MusicGenResponse out = std::move(impl_->scripted.front());
        impl_->scripted.pop_front();
        if (out.model.empty()) out.model = resp.model;
        return out;
    }

    int count = request.count > 0 ? request.count : 1;
    resp.tracks.reserve(count);
    for (int i = 0; i < count; ++i) resp.tracks.push_back(SynthesizeTrack(request, i));
    resp.usage.units = count;
    return resp;
}

std::future<MusicGenResponse> MockMusicGen::GenerateAsync(
    const MusicGenRequest& request) {
    return std::async(std::launch::async,
                      [this, request]() { return Generate(request); });
}

StreamHandle MockMusicGen::GenerateJob(const MusicGenRequest& request,
                                       MusicJobCallback onEvent) {
    auto handle = std::make_shared<JobHandle>();
    auto interval = impl_->jobInterval;
    auto steps    = impl_->jobSteps;
    auto response = Generate(request);

    std::thread worker([handle, response, steps, interval, onEvent]() {
        if (onEvent) {
            MusicJobEvent ev; ev.kind = MusicJobEventKind::Queued;
            onEvent(ev);
        }
        for (int i = 1; i <= steps; ++i) {
            if (handle->IsCancelled()) break;
            std::this_thread::sleep_for(interval);
            if (onEvent) {
                MusicJobEvent ev;
                ev.kind = MusicJobEventKind::InProgress;
                ev.progress = static_cast<double>(i) / static_cast<double>(steps);
                onEvent(ev);
            }
        }
        if (onEvent && !handle->IsCancelled()) {
            MusicJobEvent done;
            if (response.error.code != ErrorCode::None) {
                done.kind = MusicJobEventKind::Error;
                done.error = response.error;
            } else {
                done.kind = MusicJobEventKind::Completed;
                done.result = response;
            }
            onEvent(done);
        }
        handle->MarkDone();
    });
    handle->AttachThread(std::move(worker));
    return handle;
}

std::unique_ptr<IMusicGen> CreateMockMusicGen() {
    return std::make_unique<MockMusicGen>();
}

} // namespace UltraAI
