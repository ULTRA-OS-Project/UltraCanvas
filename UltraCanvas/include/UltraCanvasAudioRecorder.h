// include/UltraCanvasAudioRecorder.h
// Non-visual cross-platform audio capture (microphone recording) engine
// Version: 0.1.0
// Last Modified: 2026-06-12
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASAUDIORECORDER_H
#define ULTRACANVASAUDIORECORDER_H

#include "UltraCanvasAudio.h"
#include "UltraCanvasAudioDevices.h"
#include <string>
#include <memory>
#include <functional>
#include <cstdint>

namespace UltraCanvas {

// ===== RECORDING STATE =====
enum class AudioRecordingState {
    Idle,           // No backend open
    Preparing,      // Permissions / device init in flight
    Ready,          // Backend open, not yet capturing
    Recording,
    Paused,
    Stopped,        // Finalized buffer available
    Error
};

// ===== CAPTURE CONFIG =====
struct AudioCaptureConfig {
    int sampleRate = 44100;                 // 8000 / 16000 / 22050 / 44100 / 48000
    int channels = 1;                       // 1 = mono, 2 = stereo
    AudioSampleType sampleType = AudioSampleType::PCM_S16;
    AudioFormat encoding = AudioFormat::WAV; // Output format if SaveToFile() is used
    std::string deviceId;                   // empty = system default input
    float inputGain = 1.0f;                 // Software gain multiplier, 0..N
    bool  echoCancel = false;               // Backend hint (not all backends honor)
    bool  noiseSuppress = false;            // Backend hint
    size_t maxDurationMs = 0;               // 0 = unlimited
    bool  streamToFile = false;             // If true, write incrementally; don't keep RAM buffer
    std::string streamFilePath;             // Required when streamToFile == true

    // Monitoring
    int   levelUpdateHz = 30;               // onLevelChanged frequency
    float silenceThreshold = 0.01f;         // RMS below this fires onSilenceDetected
    float clippingThreshold = 0.98f;        // Peak above this fires onClipping
};

// ===== AUDIO RECORDER (NON-VISUAL) =====
class UltraCanvasAudioRecorder {
public:
    UltraCanvasAudioRecorder();
    explicit UltraCanvasAudioRecorder(const AudioCaptureConfig& cfg);
    ~UltraCanvasAudioRecorder();

    UltraCanvasAudioRecorder(const UltraCanvasAudioRecorder&) = delete;
    UltraCanvasAudioRecorder& operator=(const UltraCanvasAudioRecorder&) = delete;

    // ===== SESSION =====
    bool Open();                              // Acquire device + check permission
    bool Open(const AudioCaptureConfig& cfg); // Reconfigure + open
    void Close();
    bool IsOpen() const;

    // ===== TRANSPORT =====
    bool Start();
    bool Pause();
    bool Resume();
    bool Stop();

    // ===== OUTPUT =====
    std::shared_ptr<UCAudio> TakeBuffer();           // Move captured PCM out as UCAudio
    bool SaveToFile(const std::string& filePath,
                    AudioFormat format = AudioFormat::WAV) const;
    void Discard();                                   // Drop captured samples without saving

    // ===== STATE =====
    AudioRecordingState GetState() const;
    double GetElapsed() const;                        // seconds since Start()
    size_t GetSampleCount() const;                    // frames captured
    float  GetCurrentPeakLevel() const;               // 0..1
    float  GetCurrentRMSLevel() const;                // 0..1
    const std::string& GetLastError() const;
    const AudioCaptureConfig& GetConfig() const;

    // ===== PROPERTIES =====
    void SetInputGain(float gain);
    float GetInputGain() const;
    void SetMuted(bool muted);                        // Mid-recording mute (writes silence)
    bool IsMuted() const;
    void SetInputDevice(const std::string& deviceId); // Requires reopen

    // ===== EVENTS =====
    std::function<void(AudioRecordingState)> onRecordingStateChanged;
    std::function<void(float peak, float rms)> onLevelChanged;
    std::function<void(const float* samples, size_t frames, int channels)> onBufferAvailable;
    std::function<void()> onSilenceDetected;
    std::function<void()> onClipping;
    std::function<void()> onMaxDurationReached;
    std::function<void(const std::string& message)> onError;
    std::function<void(MicrophonePermission)> onPermissionChanged;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// ===== FACTORY =====
inline std::shared_ptr<UltraCanvasAudioRecorder> CreateAudioRecorder() {
    return std::make_shared<UltraCanvasAudioRecorder>();
}

inline std::shared_ptr<UltraCanvasAudioRecorder> CreateAudioRecorderWithConfig(
        const AudioCaptureConfig& cfg) {
    return std::make_shared<UltraCanvasAudioRecorder>(cfg);
}

} // namespace UltraCanvas

#endif // ULTRACANVASAUDIORECORDER_H
