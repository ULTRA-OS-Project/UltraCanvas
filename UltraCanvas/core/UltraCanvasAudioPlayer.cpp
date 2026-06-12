// core/UltraCanvasAudioPlayer.cpp
// Skeleton implementation. Backend wiring (output stream + fill callback) is a
// TODO; this file holds the public-API contract and state machine.
// Version: 0.1.0
// Last Modified: 2026-06-12
// Author: UltraCanvas Framework

#include "UltraCanvasAudioPlayer.h"
#include "../libspecific/Audio/IAudioBackend.h"
#include <algorithm>

namespace UltraCanvas {

struct UltraCanvasAudioPlayer::Impl {
    AudioPlaybackConfig config;
    AudioPlaybackState state = AudioPlaybackState::Idle;
    std::shared_ptr<UCAudio> audio;
    std::unique_ptr<IAudioStream> stream;

    double position = 0.0;
    size_t playCursorFrames = 0;
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

    size_t FillOutput(void* /*out*/, size_t frames) {
        // TODO: copy from audio->GetData() at playCursorFrames, advance cursor,
        //       handle looping, emit onPositionChanged at config.positionUpdateHz,
        //       emit onEnded when reaching the end with !loop.
        return frames;
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
        impl->EmitError("failed to load: " + filePath);
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
    impl->position = 0.0;
    impl->playCursorFrames = 0;
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
    impl->position = 0.0;
    impl->playCursorFrames = 0;
    impl->SetState(AudioPlaybackState::Stopped);
    return true;
}

bool UltraCanvasAudioPlayer::Seek(double seconds) {
    if (!impl->audio) return false;
    double dur = impl->audio->GetDuration();
    impl->position = std::clamp(seconds, 0.0, dur);
    impl->playCursorFrames = static_cast<size_t>(impl->position * impl->audio->GetSampleRate());
    if (onPositionChanged) onPositionChanged(impl->position);
    return true;
}

// ===== STATE =====
AudioPlaybackState UltraCanvasAudioPlayer::GetState() const { return impl->state; }
double UltraCanvasAudioPlayer::GetPosition() const { return impl->position; }
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
