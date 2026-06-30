// core/UltraCanvasAudioDevices.cpp
// Thin wrappers that delegate to the active IAudioBackend.
// Version: 0.1.0
// Last Modified: 2026-06-12
// Author: UltraCanvas Framework

#include "UltraCanvasAudioDevices.h"
#include "../libspecific/Audio/IAudioBackend.h"

namespace UltraCanvas {

std::vector<AudioDeviceInfo> UltraCanvasAudioDevices::ListInputDevices() {
    auto* backend = GetAudioBackend();
    return backend ? backend->ListInputDevices() : std::vector<AudioDeviceInfo>{};
}

std::vector<AudioDeviceInfo> UltraCanvasAudioDevices::ListOutputDevices() {
    auto* backend = GetAudioBackend();
    return backend ? backend->ListOutputDevices() : std::vector<AudioDeviceInfo>{};
}

AudioDeviceInfo UltraCanvasAudioDevices::GetDefaultInputDevice() {
    auto* backend = GetAudioBackend();
    return backend ? backend->GetDefaultInputDevice() : AudioDeviceInfo{};
}

AudioDeviceInfo UltraCanvasAudioDevices::GetDefaultOutputDevice() {
    auto* backend = GetAudioBackend();
    return backend ? backend->GetDefaultOutputDevice() : AudioDeviceInfo{};
}

MicrophonePermission UltraCanvasAudioDevices::GetMicrophonePermission() {
    auto* backend = GetAudioBackend();
    return backend ? backend->GetMicrophonePermission() : MicrophonePermission::Denied;
}

void UltraCanvasAudioDevices::RequestMicrophonePermission(std::function<void(bool)> cb) {
    auto* backend = GetAudioBackend();
    if (backend) {
        backend->RequestMicrophonePermission(std::move(cb));
    } else if (cb) {
        cb(false);
    }
}

std::string UltraCanvasAudioDevices::GetBackendName() {
    auto* backend = GetAudioBackend();
    return backend ? backend->GetName() : "none";
}

bool UltraCanvasAudioDevices::IsAvailable() {
#ifdef ULTRACANVAS_ENABLE_AUDIO
    auto* backend = GetAudioBackend();
    return backend && backend->GetName() != "null";
#else
    return false;
#endif
}

} // namespace UltraCanvas
