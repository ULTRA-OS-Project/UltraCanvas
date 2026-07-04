// include/UltraCanvasVideoDevices.h
// Camera device enumeration and camera permission helpers
// Version: 0.1.0
// Last Modified: 2026-06-15
// Author: UltraCanvas Framework
#pragma once
#ifndef ULTRACANVASVIDEODEVICES_H
#define ULTRACANVASVIDEODEVICES_H

#include <string>
#include <vector>
#include <functional>

namespace UltraCanvas {

// ===== SUPPORTED CAPTURE MODE =====
struct CameraMode {
    int width  = 0;
    int height = 0;
    double frameRate = 0.0;
};

// ===== CAMERA DESCRIPTOR =====
struct VideoDeviceInfo {
    std::string id;                       // Opaque backend ID (e.g. "/dev/video0")
    std::string name;                     // Human-readable label
    bool isDefault = false;
    std::vector<CameraMode> supportedModes;
};

// ===== CAMERA PERMISSION =====
enum class CameraPermission {
    Undetermined,    // Not yet requested (macOS / Windows initial state)
    Granted,
    Denied,
    Restricted       // OS policy forbids
};

// ===== DEVICE & PERMISSION API =====
class UltraCanvasVideoDevices {
public:
    // Enumeration
    static std::vector<VideoDeviceInfo> ListCameras();
    static VideoDeviceInfo GetDefaultCamera();

    // Camera permission (no-op/granted on Linux, real on macOS / Windows / WASM)
    static CameraPermission GetCameraPermission();
    static void RequestCameraPermission(std::function<void(bool granted)> callback);

    // Backend reflection
    static std::string GetBackendName();   // "gstreamer", "mediafoundation", "avfoundation", "null"
    static bool IsAvailable();             // false if compiled without video support
};

} // namespace UltraCanvas

#endif // ULTRACANVASVIDEODEVICES_H
