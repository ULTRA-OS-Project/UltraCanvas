// libspecific/Audio/AudioBackendNull.cpp
// No-op audio backend. Built when ULTRACANVAS_ENABLE_AUDIO is OFF or when no
// platform backend is available. All operations succeed-but-do-nothing so app
// code can compile and run unchanged; IsAvailable() returns false.
// Version: 0.1.0
// Last Modified: 2026-06-12
// Author: UltraCanvas Framework

#include "IAudioBackend.h"

namespace UltraCanvas {

namespace {

class NullStream : public IAudioStream {
public:
    NullStream(const AudioStreamConfig& c, AudioStreamDirection d)
        : config(c), direction(d) {}

    bool Start() override { running = true; return true; }
    bool Stop() override { running = false; return true; }
    bool IsRunning() const override { return running; }

    void SetVolume(float v) override { volume = v; }
    float GetVolume() const override { return volume; }

    const AudioStreamConfig& GetConfig() const override { return config; }
    AudioStreamDirection GetDirection() const override { return direction; }

private:
    AudioStreamConfig config;
    AudioStreamDirection direction;
    bool running = false;
    float volume = 1.0f;
};

class NullBackend : public IAudioBackend {
public:
    bool Initialize() override { return true; }
    void Shutdown() override {}
    std::string GetName() const override { return "null"; }

    std::vector<AudioDeviceInfo> ListInputDevices() override { return {}; }
    std::vector<AudioDeviceInfo> ListOutputDevices() override { return {}; }
    AudioDeviceInfo GetDefaultInputDevice() override { return {}; }
    AudioDeviceInfo GetDefaultOutputDevice() override { return {}; }

    MicrophonePermission GetMicrophonePermission() override {
        return MicrophonePermission::Denied;
    }
    void RequestMicrophonePermission(std::function<void(bool)> cb) override {
        if (cb) cb(false);
    }

    std::unique_ptr<IAudioStream> OpenOutputStream(const AudioStreamConfig& cfg) override {
        return std::make_unique<NullStream>(cfg, AudioStreamDirection::Output);
    }
    std::unique_ptr<IAudioStream> OpenInputStream(const AudioStreamConfig& cfg) override {
        return std::make_unique<NullStream>(cfg, AudioStreamDirection::Input);
    }

    std::shared_ptr<UCAudio> DecodeFile(const std::string&) override { return nullptr; }
    std::shared_ptr<UCAudio> DecodeMemory(const uint8_t*, size_t) override { return nullptr; }
    bool EncodeFile(const std::string&, const UCAudio&, AudioFormat) override { return false; }
};

} // namespace

#ifndef ULTRACANVAS_ENABLE_AUDIO
IAudioBackend* GetAudioBackend() {
    static NullBackend backend;
    static bool inited = (backend.Initialize(), true);
    (void)inited;
    return &backend;
}
#else
// When a real backend is compiled in, its translation unit defines
// GetAudioBackend(). The null backend is still available as a fallback for
// platforms where the real backend fails to initialize.
IAudioBackend* GetNullAudioBackend() {
    static NullBackend backend;
    static bool inited = (backend.Initialize(), true);
    (void)inited;
    return &backend;
}
#endif

} // namespace UltraCanvas
