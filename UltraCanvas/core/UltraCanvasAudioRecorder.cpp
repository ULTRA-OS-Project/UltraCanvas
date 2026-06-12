// core/UltraCanvasAudioRecorder.cpp
// Skeleton implementation. Backend wiring (input stream + data callback) is a
// TODO; this file holds the public-API contract and state machine.
// Version: 0.1.0
// Last Modified: 2026-06-12
// Author: UltraCanvas Framework

#include "UltraCanvasAudioRecorder.h"
#include "../libspecific/Audio/IAudioBackend.h"
#include <algorithm>
#include <chrono>

namespace UltraCanvas {

struct UltraCanvasAudioRecorder::Impl {
    AudioCaptureConfig config;
    AudioRecordingState state = AudioRecordingState::Idle;
    std::unique_ptr<IAudioStream> stream;
    std::vector<uint8_t> buffer;          // Captured PCM (when !streamToFile)
    size_t frameCount = 0;

    std::chrono::steady_clock::time_point startedAt{};
    double accumulatedSeconds = 0.0;

    float currentPeak = 0.0f;
    float currentRMS = 0.0f;
    bool  muted = false;
    std::string lastError;

    UltraCanvasAudioRecorder* owner = nullptr;

    void SetState(AudioRecordingState s) {
        if (state == s) return;
        state = s;
        if (owner && owner->onRecordingStateChanged) owner->onRecordingStateChanged(s);
    }

    void EmitError(const std::string& msg) {
        lastError = msg;
        SetState(AudioRecordingState::Error);
        if (owner && owner->onError) owner->onError(msg);
    }

    bool OpenStream() {
        auto* backend = GetAudioBackend();
        if (!backend) { EmitError("no audio backend"); return false; }

        AudioStreamConfig sc;
        sc.sampleRate = config.sampleRate;
        sc.channels = config.channels;
        sc.sampleType = config.sampleType;
        sc.deviceId = config.deviceId;
        stream = backend->OpenInputStream(sc);
        if (!stream) { EmitError("failed to open input stream"); return false; }

        stream->dataCallback = [this](const void* in, size_t frames) {
            ConsumeInput(in, frames);
        };
        stream->errorCallback = [this](const std::string& e) { EmitError(e); };
        return true;
    }

    void ConsumeInput(const void* /*in*/, size_t frames) {
        // TODO: apply inputGain, optional mute (write silence), append to buffer
        //       or write to streaming file, update currentPeak/currentRMS,
        //       emit onBufferAvailable, onLevelChanged (rate-limited),
        //       onSilenceDetected, onClipping, onMaxDurationReached.
        frameCount += frames;
    }
};

// ===== CTOR / DTOR =====
UltraCanvasAudioRecorder::UltraCanvasAudioRecorder() : impl(std::make_unique<Impl>()) {
    impl->owner = this;
}

UltraCanvasAudioRecorder::UltraCanvasAudioRecorder(const AudioCaptureConfig& cfg)
    : impl(std::make_unique<Impl>()) {
    impl->owner = this;
    impl->config = cfg;
}

UltraCanvasAudioRecorder::~UltraCanvasAudioRecorder() {
    Close();
}

// ===== SESSION =====
bool UltraCanvasAudioRecorder::Open() {
    impl->SetState(AudioRecordingState::Preparing);

    auto perm = UltraCanvasAudioDevices::GetMicrophonePermission();
    if (perm == MicrophonePermission::Denied || perm == MicrophonePermission::Restricted) {
        impl->EmitError("microphone permission denied");
        if (onPermissionChanged) onPermissionChanged(perm);
        return false;
    }

    if (!impl->OpenStream()) return false;
    impl->SetState(AudioRecordingState::Ready);
    return true;
}

bool UltraCanvasAudioRecorder::Open(const AudioCaptureConfig& cfg) {
    Close();
    impl->config = cfg;
    return Open();
}

void UltraCanvasAudioRecorder::Close() {
    if (impl->stream) { impl->stream->Stop(); impl->stream.reset(); }
    impl->SetState(AudioRecordingState::Idle);
}

bool UltraCanvasAudioRecorder::IsOpen() const {
    return impl->stream != nullptr;
}

// ===== TRANSPORT =====
bool UltraCanvasAudioRecorder::Start() {
    if (!IsOpen() && !Open()) return false;
    if (!impl->stream->Start()) { impl->EmitError("stream start failed"); return false; }
    impl->startedAt = std::chrono::steady_clock::now();
    impl->accumulatedSeconds = 0.0;
    impl->frameCount = 0;
    impl->buffer.clear();
    impl->SetState(AudioRecordingState::Recording);
    return true;
}

bool UltraCanvasAudioRecorder::Pause() {
    if (impl->state != AudioRecordingState::Recording) return false;
    if (impl->stream) impl->stream->Stop();
    auto now = std::chrono::steady_clock::now();
    impl->accumulatedSeconds +=
        std::chrono::duration<double>(now - impl->startedAt).count();
    impl->SetState(AudioRecordingState::Paused);
    return true;
}

bool UltraCanvasAudioRecorder::Resume() {
    if (impl->state != AudioRecordingState::Paused) return false;
    if (!impl->stream || !impl->stream->Start()) return false;
    impl->startedAt = std::chrono::steady_clock::now();
    impl->SetState(AudioRecordingState::Recording);
    return true;
}

bool UltraCanvasAudioRecorder::Stop() {
    if (impl->stream) impl->stream->Stop();
    if (impl->state == AudioRecordingState::Recording) {
        auto now = std::chrono::steady_clock::now();
        impl->accumulatedSeconds +=
            std::chrono::duration<double>(now - impl->startedAt).count();
    }
    impl->SetState(AudioRecordingState::Stopped);
    return true;
}

// ===== OUTPUT =====
std::shared_ptr<UCAudio> UltraCanvasAudioRecorder::TakeBuffer() {
    AudioBufferInfo info;
    info.sampleRate = impl->config.sampleRate;
    info.channels = impl->config.channels;
    info.sampleType = impl->config.sampleType;
    info.frameCount = impl->frameCount;
    info.durationSeconds = impl->config.sampleRate > 0
        ? static_cast<double>(impl->frameCount) / impl->config.sampleRate
        : 0.0;

    auto audio = std::make_shared<UCAudio>();
    audio->SetInfo(info);
    audio->SetSourceFormat(AudioFormat::PCM);
    audio->MutableData() = std::move(impl->buffer);
    impl->buffer.clear();
    impl->frameCount = 0;
    return audio;
}

bool UltraCanvasAudioRecorder::SaveToFile(const std::string& filePath, AudioFormat format) const {
    AudioBufferInfo info;
    info.sampleRate = impl->config.sampleRate;
    info.channels = impl->config.channels;
    info.sampleType = impl->config.sampleType;
    info.frameCount = impl->frameCount;
    auto audio = UCAudio::FromRawPCM(impl->buffer.data(), impl->buffer.size(), info);
    return audio->SaveToFile(filePath, format);
}

void UltraCanvasAudioRecorder::Discard() {
    impl->buffer.clear();
    impl->frameCount = 0;
    impl->accumulatedSeconds = 0.0;
}

// ===== STATE =====
AudioRecordingState UltraCanvasAudioRecorder::GetState() const { return impl->state; }

double UltraCanvasAudioRecorder::GetElapsed() const {
    if (impl->state == AudioRecordingState::Recording) {
        auto now = std::chrono::steady_clock::now();
        return impl->accumulatedSeconds +
               std::chrono::duration<double>(now - impl->startedAt).count();
    }
    return impl->accumulatedSeconds;
}

size_t UltraCanvasAudioRecorder::GetSampleCount() const { return impl->frameCount; }
float UltraCanvasAudioRecorder::GetCurrentPeakLevel() const { return impl->currentPeak; }
float UltraCanvasAudioRecorder::GetCurrentRMSLevel() const { return impl->currentRMS; }
const std::string& UltraCanvasAudioRecorder::GetLastError() const { return impl->lastError; }
const AudioCaptureConfig& UltraCanvasAudioRecorder::GetConfig() const { return impl->config; }

// ===== PROPERTIES =====
void UltraCanvasAudioRecorder::SetInputGain(float gain) {
    impl->config.inputGain = std::max(0.0f, gain);
}
float UltraCanvasAudioRecorder::GetInputGain() const { return impl->config.inputGain; }

void UltraCanvasAudioRecorder::SetMuted(bool muted) { impl->muted = muted; }
bool UltraCanvasAudioRecorder::IsMuted() const { return impl->muted; }

void UltraCanvasAudioRecorder::SetInputDevice(const std::string& deviceId) {
    impl->config.deviceId = deviceId;
    bool wasOpen = IsOpen();
    Close();
    if (wasOpen) Open();
}

} // namespace UltraCanvas
