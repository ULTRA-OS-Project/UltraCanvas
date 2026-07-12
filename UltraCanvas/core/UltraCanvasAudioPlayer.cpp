// core/UltraCanvasAudioPlayer.cpp
// Skeleton implementation. Backend wiring (output stream + fill callback) is a
// TODO; this file holds the public-API contract and state machine.
// Version: 0.1.0
// Last Modified: 2026-06-12
// Author: UltraCanvas Framework

#include "UltraCanvasAudioPlayer.h"
#include "UltraCanvasFileError.h"
#include "../libspecific/Audio/IAudioBackend.h"
#include <algorithm>
#include <atomic>
#include <cstring>

namespace UltraCanvas {

struct UltraCanvasAudioPlayer::Impl {
    AudioPlaybackConfig config;
    AudioPlaybackState state = AudioPlaybackState::Idle;
    std::shared_ptr<UCAudio> audio;
    std::unique_ptr<IAudioStream> stream;

    // Audio-thread-touched state. Use atomics so the UI thread can read safely.
    std::atomic<double> position{0.0};
    std::atomic<size_t> playCursorFrames{0};
    std::atomic<bool> reachedEnd{false};
    // Throttling: emit onPositionChanged at most positionUpdateHz
    std::atomic<size_t> framesSinceLastPosUpdate{0};

    std::string lastError;

    UltraCanvasAudioPlayer* owner = nullptr;

    void SetState(AudioPlaybackState s) {
        if (state == s) return;
        state = s;
        if (owner && owner->onPlaybackStateChanged) owner->onPlaybackStateChanged(s);
    }

    void EmitError(const std::string& msg) {
        lastError = msg;
        SetState(AudioPlaybackState::Error);
        if (owner && owner->onError) owner->onError(msg);
    }

    bool OpenStream() {
        auto* backend = GetAudioBackend();
        if (!backend) { EmitError("no audio backend"); return false; }
        if (!audio || !audio->IsValid()) { EmitError("no source loaded"); return false; }

        AudioStreamConfig sc;
        sc.sampleRate = audio->GetSampleRate();
        sc.channels = audio->GetChannels();
        sc.sampleType = audio->GetInfo().sampleType;
        sc.deviceId = config.deviceId;
        stream = backend->OpenOutputStream(sc);
        if (!stream) { EmitError("failed to open output stream"); return false; }

        stream->SetVolume(config.mute ? 0.0f : config.volume);
        stream->fillCallback = [this](void* out, size_t frames) -> size_t {
            return FillOutput(out, frames);
        };
        stream->errorCallback = [this](const std::string& e) { EmitError(e); };
        return true;
    }

    size_t FillOutput(void* out, size_t frames) {
        // Called on the backend audio thread. Must be lock-free and brief.
        if (!audio || !audio->IsValid() || frames == 0) {
            std::memset(out, 0, frames * audio->GetInfo().BytesPerFrame());
            return frames;
        }
        const AudioBufferInfo& info = audio->GetInfo();
        const size_t bpf = info.BytesPerFrame();
        const uint8_t* src = audio->GetData();
        const size_t totalFrames = info.frameCount;

        uint8_t* dst = static_cast<uint8_t*>(out);
        size_t cursor = playCursorFrames.load(std::memory_order_relaxed);
        size_t framesWritten = 0;

        while (framesWritten < frames) {
            if (cursor >= totalFrames) {
                if (config.loop) {
                    cursor = 0;
                } else {
                    // Pad remainder with silence and signal end-of-stream
                    size_t remaining = frames - framesWritten;
                    std::memset(dst + framesWritten * bpf, 0, remaining * bpf);
                    framesWritten = frames;
                    reachedEnd.store(true, std::memory_order_release);
                    break;
                }
            }
            size_t available = totalFrames - cursor;
            size_t want = frames - framesWritten;
            size_t copy = available < want ? available : want;
            std::memcpy(dst + framesWritten * bpf, src + cursor * bpf, copy * bpf);
            cursor += copy;
            framesWritten += copy;
        }

        playCursorFrames.store(cursor, std::memory_order_relaxed);
        double newPos = info.sampleRate > 0
            ? static_cast<double>(cursor) / info.sampleRate : 0.0;
        position.store(newPos, std::memory_order_relaxed);

        // Rate-limit onPositionChanged emission
        size_t since = framesSinceLastPosUpdate.fetch_add(frames, std::memory_order_relaxed) + frames;
        int hz = config.positionUpdateHz > 0 ? config.positionUpdateHz : 10;
        size_t framesPerTick = info.sampleRate / hz;
        if (since >= framesPerTick) {
            framesSinceLastPosUpdate.store(0, std::memory_order_relaxed);
            // Note: invoking user callbacks from the audio thread. Callers
            // should marshal to the UI thread if their callback touches UI.
            if (owner && owner->onPositionChanged) owner->onPositionChanged(newPos);
        }

        if (reachedEnd.load(std::memory_order_acquire)) {
            reachedEnd.store(false, std::memory_order_relaxed);
            playCursorFrames.store(0, std::memory_order_relaxed);
            position.store(0.0, std::memory_order_relaxed);
            SetState(AudioPlaybackState::Stopped);
            if (owner && owner->onEnded) owner->onEnded();
        }

        return framesWritten;
    }
};

// ===== CTOR / DTOR =====
UltraCanvasAudioPlayer::UltraCanvasAudioPlayer() : impl(std::make_unique<Impl>()) {
    impl->owner = this;
}

UltraCanvasAudioPlayer::UltraCanvasAudioPlayer(const AudioPlaybackConfig& cfg)
    : impl(std::make_unique<Impl>()) {
    impl->owner = this;
    impl->config = cfg;
}

UltraCanvasAudioPlayer::~UltraCanvasAudioPlayer() {
    Unload();
}

// ===== SOURCE =====
bool UltraCanvasAudioPlayer::LoadFromFile(const std::string& filePath) {
    impl->SetState(AudioPlaybackState::Loading);
    auto a = UCAudio::LoadFromFile(filePath);
    if (!a || !a->IsValid()) {
        // Prefer a clear file-access reason (missing / locked / no permission);
        // otherwise the file opened but the audio format is unsupported/damaged.
        std::string reason = DescribeFileReadError(filePath);
        if (reason.empty())
            reason = "The audio format is not supported or the file is damaged: " + filePath;
        impl->EmitError(reason);
        return false;
    }
    return LoadFromAudio(a);
}

bool UltraCanvasAudioPlayer::LoadFromAudio(std::shared_ptr<UCAudio> audio) {
    Unload();
    impl->audio = std::move(audio);
    if (!impl->audio || !impl->audio->IsValid()) {
        impl->EmitError("invalid audio buffer");
        return false;
    }
    impl->SetState(AudioPlaybackState::Stopped);
    if (onLoaded) onLoaded();
    return true;
}

void UltraCanvasAudioPlayer::Unload() {
    if (impl->stream) { impl->stream->Stop(); impl->stream.reset(); }
    impl->audio.reset();
    impl->position.store(0.0);
    impl->playCursorFrames.store(0);
    impl->state = AudioPlaybackState::Idle;
}

// ===== TRANSPORT =====
bool UltraCanvasAudioPlayer::Play() {
    if (!impl->audio) return false;
    if (!impl->stream && !impl->OpenStream()) return false;
    if (!impl->stream->Start()) { impl->EmitError("stream start failed"); return false; }
    impl->SetState(AudioPlaybackState::Playing);
    return true;
}

bool UltraCanvasAudioPlayer::Pause() {
    if (!impl->stream) return false;
    impl->stream->Stop();
    impl->SetState(AudioPlaybackState::Paused);
    return true;
}

bool UltraCanvasAudioPlayer::Stop() {
    if (impl->stream) impl->stream->Stop();
    impl->position.store(0.0);
    impl->playCursorFrames.store(0);
    impl->SetState(AudioPlaybackState::Stopped);
    return true;
}

bool UltraCanvasAudioPlayer::Seek(double seconds) {
    if (!impl->audio) return false;
    double dur = impl->audio->GetDuration();
    double clamped = std::clamp(seconds, 0.0, dur);
    impl->position.store(clamped);
    impl->playCursorFrames.store(
        static_cast<size_t>(clamped * impl->audio->GetSampleRate()));
    if (onPositionChanged) onPositionChanged(clamped);
    return true;
}

// ===== STATE =====
AudioPlaybackState UltraCanvasAudioPlayer::GetState() const { return impl->state; }
double UltraCanvasAudioPlayer::GetPosition() const { return impl->position.load(); }
double UltraCanvasAudioPlayer::GetDuration() const {
    return impl->audio ? impl->audio->GetDuration() : 0.0;
}
const std::string& UltraCanvasAudioPlayer::GetLastError() const { return impl->lastError; }

// ===== PROPERTIES =====
void UltraCanvasAudioPlayer::SetVolume(float v) {
    impl->config.volume = std::clamp(v, 0.0f, 1.0f);
    if (impl->stream) impl->stream->SetVolume(impl->config.mute ? 0.0f : impl->config.volume);
}
float UltraCanvasAudioPlayer::GetVolume() const { return impl->config.volume; }

void UltraCanvasAudioPlayer::SetMute(bool mute) {
    impl->config.mute = mute;
    if (impl->stream) impl->stream->SetVolume(mute ? 0.0f : impl->config.volume);
}
bool UltraCanvasAudioPlayer::IsMuted() const { return impl->config.mute; }

void UltraCanvasAudioPlayer::SetLoop(bool loop) { impl->config.loop = loop; }
bool UltraCanvasAudioPlayer::IsLoop() const { return impl->config.loop; }

void UltraCanvasAudioPlayer::SetPlaybackRate(float rate) {
    impl->config.playbackRate = std::clamp(rate, 0.25f, 4.0f);
}
float UltraCanvasAudioPlayer::GetPlaybackRate() const { return impl->config.playbackRate; }

void UltraCanvasAudioPlayer::SetOutputDevice(const std::string& deviceId) {
    impl->config.deviceId = deviceId;
    // Device hot-swap requires reopening the stream
    bool wasPlaying = impl->state == AudioPlaybackState::Playing;
    if (impl->stream) { impl->stream->Stop(); impl->stream.reset(); }
    if (wasPlaying) Play();
}

} // namespace UltraCanvas
