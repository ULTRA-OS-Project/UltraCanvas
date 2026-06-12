// libspecific/Audio/IAudioBackend.h
// Abstract audio backend interface (one impl per platform / audio library)
// Version: 0.1.0
// Last Modified: 2026-06-12
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasAudio.h"
#include "UltraCanvasAudioDevices.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// ===== STREAM CONFIGURATION =====
struct AudioStreamConfig {
    int sampleRate = 44100;
    int channels = 2;
    AudioSampleType sampleType = AudioSampleType::PCM_S16;
    std::string deviceId;
    size_t bufferFrames = 0;     // 0 = backend default (~ low latency)
};

// ===== STREAM DIRECTIONS =====
enum class AudioStreamDirection { Output, Input };

// ===== STREAM INSTANCE =====
// Output streams pull frames via fillCallback; input streams push via dataCallback.
class IAudioStream {
public:
    virtual ~IAudioStream() = default;

    virtual bool Start() = 0;
    virtual bool Stop() = 0;
    virtual bool IsRunning() const = 0;

    virtual void SetVolume(float v) = 0;
    virtual float GetVolume() const = 0;

    virtual const AudioStreamConfig& GetConfig() const = 0;
    virtual AudioStreamDirection GetDirection() const = 0;

    // Output stream — backend pulls samples
    // Called on backend audio thread; must return frames written.
    std::function<size_t(void* outBuffer, size_t frames)> fillCallback;

    // Input stream — backend pushes samples
    // Called on backend audio thread.
    std::function<void(const void* inBuffer, size_t frames)> dataCallback;

    // Error signalling
    std::function<void(const std::string&)> errorCallback;
};

// ===== BACKEND =====
class IAudioBackend {
public:
    virtual ~IAudioBackend() = default;

    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    virtual std::string GetName() const = 0;

    virtual std::vector<AudioDeviceInfo> ListInputDevices() = 0;
    virtual std::vector<AudioDeviceInfo> ListOutputDevices() = 0;
    virtual AudioDeviceInfo GetDefaultInputDevice() = 0;
    virtual AudioDeviceInfo GetDefaultOutputDevice() = 0;

    virtual MicrophonePermission GetMicrophonePermission() = 0;
    virtual void RequestMicrophonePermission(std::function<void(bool)> cb) = 0;

    virtual std::unique_ptr<IAudioStream> OpenOutputStream(const AudioStreamConfig& cfg) = 0;
    virtual std::unique_ptr<IAudioStream> OpenInputStream(const AudioStreamConfig& cfg) = 0;

    // Decoding / encoding. Implementations may return null/false when format
    // support isn't compiled in. The null backend always returns null/false.
    virtual std::shared_ptr<UCAudio> DecodeFile(const std::string& path) = 0;
    virtual std::shared_ptr<UCAudio> DecodeMemory(const uint8_t* data, size_t size) = 0;
    virtual bool EncodeFile(const std::string& path, const UCAudio& audio,
                            AudioFormat format) = 0;
};

// ===== ACCESSOR =====
// Returns the singleton backend selected at build time. Returns nullptr when
// ULTRACANVAS_ENABLE_AUDIO is OFF.
IAudioBackend* GetAudioBackend();

} // namespace UltraCanvas
