// include/UltraCanvasAudioDevices.h
// Audio device enumeration and microphone permission helpers
// Version: 0.1.0
// Last Modified: 2026-06-12
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASAUDIODEVICES_H
#define ULTRACANVASAUDIODEVICES_H

#include <string>
#include <vector>
#include <functional>

namespace UltraCanvas {

// ===== DEVICE DESCRIPTOR =====
struct AudioDeviceInfo {
    std::string id;                          // Opaque backend ID
    std::string name;                        // Human-readable label
    bool isDefault = false;
    bool isInput = false;                    // true = capture, false = playback
    int maxChannels = 2;
    std::vector<int> supportedSampleRates;
};

// ===== MICROPHONE PERMISSION =====
enum class MicrophonePermission {
    Undetermined,    // Not yet requested (macOS/Windows initial state)
    Granted,
    Denied,
    Restricted       // OS policy forbids (e.g. parental controls)
};

// ===== DEVICE & PERMISSION API =====
class UltraCanvasAudioDevices {
public:
    // Enumeration
    static std::vector<AudioDeviceInfo> ListInputDevices();
    static std::vector<AudioDeviceInfo> ListOutputDevices();
    static AudioDeviceInfo GetDefaultInputDevice();
    static AudioDeviceInfo GetDefaultOutputDevice();

    // Microphone permission (no-op on Linux, real on macOS / Windows / WASM)
    static MicrophonePermission GetMicrophonePermission();
    static void RequestMicrophonePermission(std::function<void(bool granted)> callback);

    // Backend reflection
    static std::string GetBackendName();    // "miniaudio", "pulse", "wasapi", "coreaudio", ...
    static bool IsAvailable();              // false if compiled without audio support
};

} // namespace UltraCanvas

#endif // ULTRACANVASAUDIODEVICES_H
