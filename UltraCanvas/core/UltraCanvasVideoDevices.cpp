// core/UltraCanvasVideoDevices.cpp
// Camera device enumeration + permission, delegating to the active backend
// Version: 0.1.0
// Last Modified: 2026-06-15
// Author: UltraCanvas Framework

#include "UltraCanvasVideoDevices.h"
#include "../libspecific/Video/IVideoBackend.h"

namespace UltraCanvas {

std::vector<VideoDeviceInfo> UltraCanvasVideoDevices::ListCameras() {
    if (auto* b = GetVideoBackend()) return b->ListCameras();
    return {};
}

VideoDeviceInfo UltraCanvasVideoDevices::GetDefaultCamera() {
    if (auto* b = GetVideoBackend()) return b->GetDefaultCamera();
    return {};
}

CameraPermission UltraCanvasVideoDevices::GetCameraPermission() {
    if (auto* b = GetVideoBackend()) return b->GetCameraPermission();
    return CameraPermission::Denied;
}

void UltraCanvasVideoDevices::RequestCameraPermission(std::function<void(bool)> callback) {
    if (auto* b = GetVideoBackend()) { b->RequestCameraPermission(std::move(callback)); return; }
    if (callback) callback(false);
}

std::string UltraCanvasVideoDevices::GetBackendName() {
    if (auto* b = GetVideoBackend()) return b->GetName();
    return "null";
}

bool UltraCanvasVideoDevices::IsAvailable() {
    auto* b = GetVideoBackend();
    return b && b->GetName() != "null";
}

} // namespace UltraCanvas
