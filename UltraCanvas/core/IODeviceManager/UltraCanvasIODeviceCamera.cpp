// core/IODeviceManager/UltraCanvasIODeviceCamera.cpp
// CameraDevice: configuration validation, the streaming state machine and
// control clamping, all platform-neutral.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "../../include/IODeviceManager/UltraCanvasIODeviceCamera.h"

namespace UltraCanvas {

CameraDevice::CameraDevice(const IODeviceInfo& info, CameraType type)
    : IODevice(info), cameraType(type) {}

CameraType CameraDevice::GetCameraType() const { return cameraType; }

// ===== CAPABILITIES =====

const CameraCapabilities& CameraDevice::GetCapabilities() {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    if (!capabilitiesLoaded) {
        RefreshCapabilities();
    }
    return capabilities;
}

IODeviceResult CameraDevice::RefreshCapabilities() {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);

    CameraCapabilities fresh;
    IODeviceResult result = DoGetCapabilities(fresh);
    if (result.success) {
        capabilities = fresh;
        capabilitiesLoaded = true;
    }
    return result;
}

// ===== CONFIGURATION =====

IODeviceResult CameraDevice::SetConfiguration(const CameraConfiguration& requested) {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);

    IODeviceResult ready = RequireConnected();
    if (!ready.success) {
        return ready;
    }

    // Changing format mid-stream means tearing down and rebuilding the
    // buffer pool, which drops frames and races whatever is reading them.
    // The caller stops the stream first.
    if (streaming) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidState,
                                     "Stop the stream before changing the camera "
                                     "configuration",
                                     GetDeviceId());
    }

    if (!requested.IsValid()) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                     "Camera configuration has no resolution",
                                     GetDeviceId());
    }

    const CameraCapabilities& caps = GetCapabilities();

    // Refused rather than silently substituted: a caller that asked for
    // 1920x1080 MJPEG and got 640x480 YUYV would find out only by inspecting
    // the frames.
    if (!caps.formats.empty() &&
        !caps.Supports(requested.pixelFormat, requested.resolution)) {
        return IODeviceResult::Error(
            IODeviceResultCode::NotSupported,
            std::string("This camera does not offer ") +
                CameraPixelFormatToString(requested.pixelFormat) + " at " +
                requested.resolution.ToString(),
            GetDeviceId());
    }

    IODeviceResult applied = DoApplyConfiguration(requested);
    if (applied.success) {
        configuration = requested;
    } else {
        SetLastError(applied);
    }
    return applied;
}

CameraConfiguration CameraDevice::GetConfiguration() const {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    return configuration;
}

CameraConfiguration
CameraDevice::ResolveConfiguration(const CameraConfiguration& requested,
                                   std::vector<std::string>* changes) {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);

    CameraConfiguration resolved = requested;
    const CameraCapabilities& caps = GetCapabilities();

    auto note = [&](const std::string& text) {
        if (changes) {
            changes->push_back(text);
        }
    };

    if (caps.formats.empty()) {
        // Nothing reported, so nothing to resolve against; the caller's
        // choice stands and the backend will answer for it.
        return resolved;
    }

    // Format first: it constrains which resolutions exist.
    if (resolved.pixelFormat == CameraPixelFormat::Unknown ||
        !caps.Supports(resolved.pixelFormat)) {
        const CameraPixelFormat wanted = resolved.pixelFormat;

        // Prefer an uncompressed format a caller can read directly; fall back
        // to whatever the camera leads with.
        CameraPixelFormat chosen = CameraPixelFormat::Unknown;
        for (const auto& entry : caps.formats) {
            if (!CameraPixelFormatIsCompressed(entry.format)) {
                chosen = entry.format;
                break;
            }
        }
        if (chosen == CameraPixelFormat::Unknown) {
            chosen = caps.formats.front().format;
        }

        if (wanted == CameraPixelFormat::Unknown) {
            note(std::string("No pixel format asked for, using ") +
                 CameraPixelFormatToString(chosen));
        } else {
            note(std::string(CameraPixelFormatToString(wanted)) +
                 " is not supported, using " + CameraPixelFormatToString(chosen));
        }
        resolved.pixelFormat = chosen;
    }

    // Then resolution, within that format.
    if (!resolved.resolution.IsValid() ||
        !caps.Supports(resolved.pixelFormat, resolved.resolution)) {
        const CameraResolution largest = caps.GetLargestResolution(resolved.pixelFormat);
        if (largest.IsValid()) {
            if (resolved.resolution.IsValid()) {
                note(resolved.resolution.ToString() + " is not offered for " +
                     CameraPixelFormatToString(resolved.pixelFormat) + ", using " +
                     largest.ToString());
            } else {
                note("No resolution asked for, using " + largest.ToString());
            }
            resolved.resolution = largest;
        }
    }

    if (resolved.bufferCount < 2) {
        // One buffer means the driver has nowhere to put the next frame while
        // the caller holds this one, so every frame arrives late or not at all.
        note("At least two capture buffers are needed, using 2");
        resolved.bufferCount = 2;
    }

    return resolved;
}

// ===== STILL CAPTURE =====

IODeviceResult CameraDevice::CaptureFrame(CameraFrame& frame) {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);

    IODeviceResult ready = RequireConnected();
    if (!ready.success) {
        return ready;
    }
    return DoCaptureFrame(frame);
}

// ===== STREAMING =====

IODeviceResult CameraDevice::StartStream(CameraFrameCallback callback) {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);

    IODeviceResult ready = RequireConnected();
    if (!ready.success) {
        return ready;
    }

    if (streaming) {
        return IODeviceResult::Ok(GetDeviceId());
    }

    if (!callback) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                     "Streaming needs a frame callback",
                                     GetDeviceId());
    }

    frameCallback = std::move(callback);
    frameCount = 0;

    // Set before starting: the backend's thread may deliver its first frame
    // before DoStartStream() has returned, and ShouldKeepStreaming() has to
    // be true by then or that frame is dropped.
    streaming = true;

    IODeviceResult started = DoStartStream();
    if (!started.success) {
        streaming = false;
        frameCallback = nullptr;
        SetLastError(started);
        return started;
    }

    SetState(IODeviceState::Busy);
    return IODeviceResult::Ok(GetDeviceId());
}

void CameraDevice::StopStream() {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);

    // Cleared first so a capture loop testing ShouldKeepStreaming() winds
    // down, then DoStopStream() joins it.
    streaming = false;

    DoStopStream();

    frameCallback = nullptr;
    if (GetState() == IODeviceState::Busy) {
        SetState(IODeviceState::Ready);
    }
}

bool CameraDevice::IsStreaming() const { return streaming; }

uint64_t CameraDevice::GetFrameCount() const { return frameCount; }

bool CameraDevice::ShouldKeepStreaming() const { return streaming; }

void CameraDevice::DeliverFrame(CameraFrame& frame) {
    // Runs on the backend's capture thread. deviceMutex is deliberately not
    // taken: StopStream() holds it while joining this thread, so locking here
    // would deadlock. The two fields touched are atomic, and the callback is
    // only cleared after the join.
    if (!streaming) {
        return;
    }

    frame.frameNumber = ++frameCount;

    if (frameCallback) {
        frameCallback(frame);
    }
}

// ===== CONTROLS =====

CameraControlRange CameraDevice::GetControlRange(CameraControl control) {
    return GetCapabilities().GetControlRange(control);
}

IODeviceResult CameraDevice::GetControl(CameraControl control, int& value) {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);

    IODeviceResult ready = RequireConnected();
    if (!ready.success) {
        return ready;
    }

    if (!GetControlRange(control).supported) {
        return IODeviceResult::Error(
            IODeviceResultCode::NotSupported,
            std::string("This camera has no ") + CameraControlToString(control) +
                " control",
            GetDeviceId());
    }
    return DoGetControl(control, value);
}

IODeviceResult CameraDevice::SetControl(CameraControl control, int value) {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);

    IODeviceResult ready = RequireConnected();
    if (!ready.success) {
        return ready;
    }

    const CameraControlRange range = GetControlRange(control);
    if (!range.supported) {
        return IODeviceResult::Error(
            IODeviceResultCode::NotSupported,
            std::string("This camera has no ") + CameraControlToString(control) +
                " control",
            GetDeviceId());
    }

    // Clamped rather than refused: a caller passing a slider position should
    // get the nearest value the device takes, not an error.
    return DoSetControl(control, range.Clamp(value));
}

IODeviceResult CameraDevice::SetControlAuto(CameraControl control, bool automatic) {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);

    IODeviceResult ready = RequireConnected();
    if (!ready.success) {
        return ready;
    }

    const CameraControlRange range = GetControlRange(control);
    if (!range.supported) {
        return IODeviceResult::Error(
            IODeviceResultCode::NotSupported,
            std::string("This camera has no ") + CameraControlToString(control) +
                " control",
            GetDeviceId());
    }
    if (!range.autoCapable) {
        return IODeviceResult::Error(
            IODeviceResultCode::NotSupported,
            std::string(CameraControlToString(control)) +
                " has no automatic mode on this camera",
            GetDeviceId());
    }
    return DoSetControlAuto(control, automatic);
}

}  // namespace UltraCanvas
