// core/UltraRTC/UltraRTCAudioDevices.cpp
// UltraRTC - the microphone and speaker of a call, on the framework's audio
// backend: UltraCanvasAudioRecorder in live mode (20 ms frames as they are
// captured) and UltraCanvasAudioPlayer's push sink. Compiled into the core
// library, where those classes live.
// Version: 0.1.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#include "UltraRTCAudioDevices.h"

#include "UltraCanvasAudioPlayer.h"
#include "UltraCanvasAudioRecorder.h"
#include "UltraCanvasAudioStreaming.h"

#include <algorithm>
#include <vector>

namespace UltraRtcInternal {

namespace {

constexpr int kRate = 48000;
// More than this queued for the speaker and the oldest audio is dropped:
// a call keeps its delay short rather than playing everything.
constexpr double kMaxQueuedSeconds = 0.12;

class RecorderCapture final : public MicrophoneCapture {
public:
    bool Open(const std::string& deviceId, PcmCallback onPcm, std::string& error) {
        UltraCanvas::AudioCaptureConfig config;
        config.sampleRate = kRate;
        config.channels = 1;
        config.deviceId = deviceId;
        config.mode = UltraCanvas::AudioCaptureMode::Live;
        config.liveFrameMs = 20;
        config.echoCancel = true;          // a hint the backend may honour
        config.noiseSuppress = true;
        recorder_.onLiveFrame = [onPcm, this](const UltraCanvas::AudioLiveFrame& frame) {
            pcm_.resize(frame.frameCount);
            for (size_t i = 0; i < frame.frameCount; ++i) {
                // Mono: the first channel of each frame.
                const float v = frame.samples[i * static_cast<size_t>(std::max(1, frame.channels))];
                pcm_[i] = static_cast<int16_t>(std::clamp(v, -1.0f, 1.0f) * 32767.0f);
            }
            onPcm(pcm_.data(), pcm_.size());
        };
        if (!recorder_.Open(config) || !recorder_.Start()) {
            error = recorder_.GetLastError().empty() ? "The microphone could not be opened" : recorder_.GetLastError();
            return false;
        }
        return true;
    }

    void Stop() override {
        recorder_.Stop();
        recorder_.Close();
        recorder_.onLiveFrame = nullptr;
    }

    ~RecorderCapture() override { Stop(); }

private:
    UltraCanvas::UltraCanvasAudioRecorder recorder_;
    std::vector<int16_t> pcm_;      // touched only on the audio thread
};

class SinkPlayback final : public SpeakerPlayback {
public:
    bool Open(const std::string& deviceId, std::string& error) {
        if (!deviceId.empty()) player_.SetOutputDevice(deviceId);
        UltraCanvas::AudioSinkConfig config;
        config.sampleRate = kRate;
        config.channels = 1;
        config.bufferMs = 200;
        if (!player_.OpenSink(config)) {
            error = "The speaker could not be opened";
            return false;
        }
        return true;
    }

    void Push(const int16_t* samples, size_t count) override {
        if (player_.GetSinkQueuedSeconds() > kMaxQueuedSeconds) player_.ClearSink();
        buffer_.resize(count);
        for (size_t i = 0; i < count; ++i) buffer_[i] = static_cast<float>(samples[i]) / 32768.0f;
        player_.PushSinkFrames(buffer_.data(), count);
    }

    void Stop() override { player_.CloseSink(); }

    ~SinkPlayback() override { Stop(); }

private:
    UltraCanvas::UltraCanvasAudioPlayer player_;
    std::vector<float> buffer_;
};

} // namespace

std::unique_ptr<MicrophoneCapture> OpenMicrophone(const std::string& deviceId, PcmCallback onPcm, std::string& error) {
    auto capture = std::make_unique<RecorderCapture>();
    if (!capture->Open(deviceId, std::move(onPcm), error)) return nullptr;
    return capture;
}

std::unique_ptr<SpeakerPlayback> OpenSpeaker(const std::string& deviceId, std::string& error) {
    auto playback = std::make_unique<SinkPlayback>();
    if (!playback->Open(deviceId, error)) return nullptr;
    return playback;
}

} // namespace UltraRtcInternal
