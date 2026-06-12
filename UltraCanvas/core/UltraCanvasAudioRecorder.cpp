// core/UltraCanvasAudioRecorder.cpp
// Skeleton implementation. Backend wiring (input stream + data callback) is a
// TODO; this file holds the public-API contract and state machine.
// Version: 0.1.0
// Last Modified: 2026-06-12
// Author: UltraCanvas Framework

#include "UltraCanvasAudioRecorder.h"
#include "../libspecific/Audio/IAudioBackend.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <fstream>

namespace UltraCanvas {

struct UltraCanvasAudioRecorder::Impl {
    AudioCaptureConfig config;
    AudioRecordingState state = AudioRecordingState::Idle;
    std::unique_ptr<IAudioStream> stream;
    std::vector<uint8_t> buffer;          // Captured PCM (when !streamToFile)
    size_t frameCount = 0;

    std::chrono::steady_clock::time_point startedAt{};
    double accumulatedSeconds = 0.0;

    std::atomic<float> currentPeak{0.0f};
    std::atomic<float> currentRMS{0.0f};
    std::atomic<bool>  muted{false};
    // Throttling: emit onLevelChanged at most levelUpdateHz
    size_t framesSinceLastLevelEmit = 0;
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

    static float SampleToFloat(const uint8_t* p, AudioSampleType t) {
        switch (t) {
            case AudioSampleType::PCM_S16: {
                int16_t s; std::memcpy(&s, p, 2);
                return static_cast<float>(s) / 32768.0f;
            }
            case AudioSampleType::PCM_S24: {
                int32_t s = (static_cast<int32_t>(static_cast<int8_t>(p[2])) << 16) |
                            (static_cast<int32_t>(p[1]) << 8) |
                            (static_cast<int32_t>(p[0]));
                return static_cast<float>(s) / 8388608.0f;
            }
            case AudioSampleType::PCM_S32: {
                int32_t s; std::memcpy(&s, p, 4);
                return static_cast<float>(s) / 2147483648.0f;
            }
            case AudioSampleType::PCM_F32: {
                float f; std::memcpy(&f, p, 4);
                return f;
            }
        }
        return 0.0f;
    }

    void ConsumeInput(const void* in, size_t frames) {
        // Audio thread. Lock-free append + atomic level state.
        if (!in || frames == 0) return;
        const size_t bytesPerSample = (config.sampleType == AudioSampleType::PCM_S16) ? 2 :
                                      (config.sampleType == AudioSampleType::PCM_S24) ? 3 :
                                      (config.sampleType == AudioSampleType::PCM_F32) ? 4 : 4;
        const size_t bpf = bytesPerSample * config.channels;
        const size_t bytes = frames * bpf;
        const uint8_t* srcBytes = static_cast<const uint8_t*>(in);

        // Apply gain into a working chunk so the on-disk / in-memory bytes
        // reflect post-gain levels. For non-unity gain on S16/S32 we'd need to
        // clamp/saturate per sample; for now copy verbatim when gain==1.
        const float gain = config.inputGain;
        const bool isMuted = muted.load(std::memory_order_relaxed);

        // Append (or stream-to-file) raw bytes
        if (config.streamToFile && !config.streamFilePath.empty()) {
            std::ofstream f(config.streamFilePath,
                            std::ios::binary | std::ios::app);
            if (f) {
                if (isMuted) {
                    static thread_local std::vector<uint8_t> silence;
                    silence.assign(bytes, 0);
                    f.write(reinterpret_cast<const char*>(silence.data()), bytes);
                } else {
                    f.write(reinterpret_cast<const char*>(srcBytes), bytes);
                }
            }
        } else {
            size_t oldSize = buffer.size();
            buffer.resize(oldSize + bytes);
            if (isMuted) {
                std::memset(buffer.data() + oldSize, 0, bytes);
            } else {
                std::memcpy(buffer.data() + oldSize, srcBytes, bytes);
            }
        }

        frameCount += frames;

        // Compute peak/RMS across this chunk (post-gain, pre-mute)
        float peak = 0.0f;
        double sumSquares = 0.0;
        size_t sampleCount = frames * config.channels;
        for (size_t i = 0; i < sampleCount; ++i) {
            float v = SampleToFloat(srcBytes + i * bytesPerSample, config.sampleType) * gain;
            if (v < 0) v = -v;
            if (v > peak) peak = v;
            sumSquares += static_cast<double>(v) * v;
        }
        float rms = sampleCount ? static_cast<float>(std::sqrt(sumSquares / sampleCount)) : 0.0f;
        currentPeak.store(peak, std::memory_order_relaxed);
        currentRMS.store(rms, std::memory_order_relaxed);

        // Rate-limited callbacks
        int hz = config.levelUpdateHz > 0 ? config.levelUpdateHz : 30;
        size_t framesPerTick = config.sampleRate / hz;
        framesSinceLastLevelEmit += frames;
        if (framesSinceLastLevelEmit >= framesPerTick) {
            framesSinceLastLevelEmit = 0;
            if (owner && owner->onLevelChanged) owner->onLevelChanged(peak, rms);
            if (peak >= config.clippingThreshold && owner && owner->onClipping) {
                owner->onClipping();
            }
            if (rms < config.silenceThreshold && owner && owner->onSilenceDetected) {
                owner->onSilenceDetected();
            }
        }

        // Per-chunk raw buffer event (only useful for live visualization /
        // off-the-fly encoding). Convert to f32 lazily on demand by the user.
        if (owner && owner->onBufferAvailable) {
            // We pass the raw bytes reinterpreted as float only when format is f32.
            // For other formats the user must reinterpret based on config.
            if (config.sampleType == AudioSampleType::PCM_F32) {
                owner->onBufferAvailable(
                    reinterpret_cast<const float*>(srcBytes), frames, config.channels);
            }
        }

        // Max duration check
        if (config.maxDurationMs > 0) {
            double elapsedSec = static_cast<double>(frameCount) / config.sampleRate;
            if (elapsedSec * 1000.0 >= config.maxDurationMs) {
                if (owner && owner->onMaxDurationReached) owner->onMaxDurationReached();
            }
        }
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
float UltraCanvasAudioRecorder::GetCurrentPeakLevel() const { return impl->currentPeak.load(); }
float UltraCanvasAudioRecorder::GetCurrentRMSLevel() const { return impl->currentRMS.load(); }
const std::string& UltraCanvasAudioRecorder::GetLastError() const { return impl->lastError; }
const AudioCaptureConfig& UltraCanvasAudioRecorder::GetConfig() const { return impl->config; }

// ===== PROPERTIES =====
void UltraCanvasAudioRecorder::SetInputGain(float gain) {
    impl->config.inputGain = std::max(0.0f, gain);
}
float UltraCanvasAudioRecorder::GetInputGain() const { return impl->config.inputGain; }

void UltraCanvasAudioRecorder::SetMuted(bool muted) { impl->muted.store(muted); }
bool UltraCanvasAudioRecorder::IsMuted() const { return impl->muted.load(); }

void UltraCanvasAudioRecorder::SetInputDevice(const std::string& deviceId) {
    impl->config.deviceId = deviceId;
    bool wasOpen = IsOpen();
    Close();
    if (wasOpen) Open();
}

} // namespace UltraCanvas
