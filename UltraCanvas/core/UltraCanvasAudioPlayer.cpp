// core/UltraCanvasAudioPlayer.cpp
// Skeleton implementation. Backend wiring (output stream + fill callback) is a
// TODO; this file holds the public-API contract and state machine.
// Version: 0.1.2
// Last Modified: 2026-09-06
// Author: UltraCanvas Framework

#include "UltraCanvasAudioPlayer.h"
#include "UltraCanvasFileError.h"
#include "UltraCanvasMediaCodecRegistry.h"
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
    // Set when a non-looping source has played to its end. The device callback
    // cannot stop its own device, so until a transport call (Play/Seek/Stop)
    // clears this, FillOutput emits silence — without it the backend kept
    // pulling frames and the track audibly restarted from frame 0 while the
    // state said Stopped.
    std::atomic<bool> playbackDone{false};
    // Throttling: emit onPositionChanged at most positionUpdateHz
    std::atomic<size_t> framesSinceLastPosUpdate{0};

    // Streaming sink. `ring` is written by PushSinkFrames (caller thread)
    // and read by FillOutput (audio thread); everything else is atomic.
    bool sinkOpen = false;
    AudioSinkConfig sinkConfig;
    AudioFrameRing ring;
    std::atomic<uint64_t> sinkPlayedFrames{0};
    std::atomic<uint64_t> sinkUnderruns{0};
    std::atomic<bool> sinkInUnderrun{false};

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

    bool OpenSinkStream() {
        auto* backend = GetAudioBackend();
        if (!backend) { EmitError("no audio backend"); return false; }

        AudioStreamConfig sc;
        sc.sampleRate = sinkConfig.sampleRate;
        sc.channels = sinkConfig.channels;
        sc.sampleType = AudioSampleType::PCM_F32;
        sc.deviceId = config.deviceId;
        stream = backend->OpenOutputStream(sc);
        if (!stream) { EmitError("failed to open output stream"); return false; }

        stream->SetVolume(config.mute ? 0.0f : config.volume);
        stream->fillCallback = [this](void* out, size_t frames) -> size_t {
            return FillFromSink(out, frames);
        };
        stream->errorCallback = [this](const std::string& e) { EmitError(e); };
        return true;
    }

    size_t FillFromSink(void* out, size_t frames) {
        // Audio thread. Pop what the ring has; the rest is silence, counted
        // as one underrun per episode so a caller can see pacing trouble
        // without being flooded.
        float* dst = static_cast<float*>(out);
        const size_t channels = static_cast<size_t>(sinkConfig.channels);
        size_t got = ring.Pop(dst, frames);
        if (got < frames) {
            std::memset(dst + got * channels, 0, (frames - got) * channels * sizeof(float));
            if (!sinkInUnderrun.exchange(true, std::memory_order_acq_rel)) {
                sinkUnderruns.fetch_add(1, std::memory_order_relaxed);
                if (owner && owner->onSinkUnderrun) owner->onSinkUnderrun();
            }
        } else {
            sinkInUnderrun.store(false, std::memory_order_release);
        }
        uint64_t played = sinkPlayedFrames.fetch_add(got, std::memory_order_relaxed) + got;
        position.store(sinkConfig.sampleRate > 0
                           ? static_cast<double>(played) / sinkConfig.sampleRate : 0.0,
                       std::memory_order_relaxed);
        return frames;
    }

    size_t FillOutput(void* out, size_t frames) {
        // Called on the backend audio thread. Must be lock-free and brief.
        if (!audio || !audio->IsValid() || frames == 0) {
            if (audio && frames)
                std::memset(out, 0, frames * audio->GetInfo().BytesPerFrame());
            return frames;
        }
        if (playbackDone.load(std::memory_order_acquire)) {
            // Played to the end and no transport call restarted us yet: keep
            // the still-running device fed with silence instead of replaying.
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
            playbackDone.store(true, std::memory_order_release);
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
        // failing that, let the backend name the codec it found and the library
        // that would decode it; only then fall back to the generic wording.
        std::string reason = DescribeFileReadError(filePath);
        if (reason.empty()) {
            if (auto* backend = GetAudioBackend()) reason = backend->DescribeDecodeFailure(filePath);
        }
        if (reason.empty()) {
            // The registry recognises plenty of formats this build cannot
            // decode — that is what lets the player appear at all — so it can
            // name the format and what is missing.
            if (auto codec = FindMediaCodecForFile(MediaCodecKind::Audio, filePath);
                codec && !codec->canDecode) {
                reason = codec->description + " is not supported by this build";
                reason += codec->notes.empty() ? "." : (": " + codec->notes + ".");
            }
        }
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
    impl->sinkOpen = false;
    impl->ring.Clear();
    impl->position.store(0.0);
    impl->playCursorFrames.store(0);
    impl->playbackDone.store(false);
    impl->state = AudioPlaybackState::Idle;
}

// ===== STREAMING SINK =====
bool UltraCanvasAudioPlayer::OpenSink(const AudioSinkConfig& cfg) {
    Unload();
    if (cfg.sampleRate <= 0 || cfg.channels <= 0) {
        impl->EmitError("invalid sink configuration");
        return false;
    }
    impl->sinkConfig = cfg;
    impl->ring.Reset(AudioFramesForMilliseconds(cfg.bufferMs > 0 ? cfg.bufferMs : 200,
                                                cfg.sampleRate),
                     cfg.channels);
    impl->sinkPlayedFrames.store(0);
    impl->sinkUnderruns.store(0);
    impl->sinkInUnderrun.store(false);
    if (!impl->OpenSinkStream()) return false;
    impl->sinkOpen = true;
    if (!impl->stream->Start()) { impl->EmitError("stream start failed"); return false; }
    impl->SetState(AudioPlaybackState::Playing);
    return true;
}

bool UltraCanvasAudioPlayer::IsSinkOpen() const { return impl->sinkOpen; }

size_t UltraCanvasAudioPlayer::PushSinkFrames(const float* interleaved, size_t frames) {
    if (!impl->sinkOpen) return 0;
    return impl->ring.Push(interleaved, frames);
}

size_t UltraCanvasAudioPlayer::GetSinkQueuedFrames() const {
    return impl->sinkOpen ? impl->ring.GetAvailableFrames() : 0;
}

double UltraCanvasAudioPlayer::GetSinkQueuedSeconds() const {
    if (!impl->sinkOpen || impl->sinkConfig.sampleRate <= 0) return 0.0;
    return static_cast<double>(impl->ring.GetAvailableFrames()) / impl->sinkConfig.sampleRate;
}

void UltraCanvasAudioPlayer::ClearSink() { impl->ring.Clear(); }

void UltraCanvasAudioPlayer::CloseSink() {
    if (impl->sinkOpen) Unload();
}

uint64_t UltraCanvasAudioPlayer::GetSinkUnderrunCount() const { return impl->sinkUnderruns.load(); }
uint64_t UltraCanvasAudioPlayer::GetSinkDroppedFrames() const { return impl->ring.GetDroppedFrames(); }

// ===== TRANSPORT =====
bool UltraCanvasAudioPlayer::Play() {
    if (impl->sinkOpen) {
        if (!impl->stream && !impl->OpenSinkStream()) return false;
        if (!impl->stream->Start()) { impl->EmitError("stream start failed"); return false; }
        impl->SetState(AudioPlaybackState::Playing);
        return true;
    }
    if (!impl->audio) return false;
    if (!impl->stream && !impl->OpenStream()) return false;
    impl->playbackDone.store(false, std::memory_order_release);
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
    if (impl->sinkOpen) { impl->ring.Clear(); impl->sinkPlayedFrames.store(0); }
    impl->position.store(0.0);
    impl->playCursorFrames.store(0);
    impl->playbackDone.store(false, std::memory_order_release);
    impl->SetState(AudioPlaybackState::Stopped);
    return true;
}

bool UltraCanvasAudioPlayer::Seek(double seconds) {
    if (!impl->audio || impl->sinkOpen) return false;
    double dur = impl->audio->GetDuration();
    double clamped = std::clamp(seconds, 0.0, dur);
    impl->position.store(clamped);
    impl->playCursorFrames.store(
        static_cast<size_t>(clamped * impl->audio->GetSampleRate()));
    impl->playbackDone.store(false, std::memory_order_release);
    if (onPositionChanged) onPositionChanged(clamped);
    return true;
}

// ===== STATE =====
AudioPlaybackState UltraCanvasAudioPlayer::GetState() const { return impl->state; }
double UltraCanvasAudioPlayer::GetPosition() const { return impl->position.load(); }
double UltraCanvasAudioPlayer::GetDuration() const {
    // A sink has no end; GetPosition() reports the seconds played so far.
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
    // Device hot-swap requires reopening the stream. The sink keeps its ring,
    // so queued frames carry over to the new device.
    bool wasPlaying = impl->state == AudioPlaybackState::Playing;
    if (impl->stream) { impl->stream->Stop(); impl->stream.reset(); }
    if (wasPlaying) Play();
}

} // namespace UltraCanvas
