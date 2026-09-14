// include/IODeviceManager/UltraCanvasIODeviceCamera.h
// CameraDevice: the category class for webcams, DSLRs and network cameras.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasIODevice.h"
#include "UltraCanvasIODeviceCameraTypes.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// Delivered on the backend's capture thread, not the UI thread: do not block
// in it, and do not touch UI elements from it. `frame` is owned by the
// callback for the duration of the call only.
using CameraFrameCallback = std::function<void(const CameraFrame& frame)>;

// ============================================================================
// CAMERADEVICE
// ============================================================================
//
// Streaming follows the same non-virtual-public / virtual-protected shape as
// IODevice's lifecycle: callers use StartStream()/StopStream(), backends
// implement DoStartStream()/DoStopStream(), and this class owns the streaming
// flag and the callback.
//
// As with IODevice, the destructor here calls no virtuals. A backend running
// a capture thread must stop and join it in its own destructor: by the time
// ~CameraDevice() runs the derived object is gone, so a thread still calling
// into it is reading freed memory.
//
class CameraDevice : public IODevice {
public:
    CameraType GetCameraType() const;

    // ===== CAPABILITIES =====

    // Cached after the first query; Refresh() re-reads from the backend.
    const CameraCapabilities& GetCapabilities();
    IODeviceResult RefreshCapabilities();

    // ===== CONFIGURATION =====

    // Fails with NotSupported when the camera does not offer that format at
    // that resolution, rather than accepting it and quietly capturing
    // something else. Cannot be changed while streaming.
    IODeviceResult SetConfiguration(const CameraConfiguration& configuration);
    CameraConfiguration GetConfiguration() const;

    // Fills in whatever the caller left unset, and reports what it changed:
    // the largest resolution the camera offers, its preferred format, its
    // default rate. Useful for "just open the camera" without a capability
    // walk first.
    CameraConfiguration ResolveConfiguration(const CameraConfiguration& requested,
                                             std::vector<std::string>* changes = nullptr);

    // ===== STILL CAPTURE =====

    // One frame, synchronously. Works whether or not a stream is running: a
    // streaming camera returns the next frame off the stream rather than
    // restarting the pipeline.
    IODeviceResult CaptureFrame(CameraFrame& frame);

    // ===== STREAMING =====

    // Starts continuous capture, delivering frames to `callback` on the
    // backend's capture thread. Idempotent.
    IODeviceResult StartStream(CameraFrameCallback callback);

    // Stops capture and joins the backend's thread, so no callback runs after
    // this returns. Idempotent, and safe on a camera that never started.
    void StopStream();

    bool IsStreaming() const;

    // Frames delivered since the stream started.
    uint64_t GetFrameCount() const;

    // ===== CONTROLS =====

    // A control this camera does not have reports `supported` false.
    CameraControlRange GetControlRange(CameraControl control);

    IODeviceResult GetControl(CameraControl control, int& value);

    // The value is clamped to the control's range and snapped to its step,
    // so a caller can pass a raw slider position.
    IODeviceResult SetControl(CameraControl control, int value);

    // Switches a control between automatic and manual. Fails with
    // NotSupported on a control with no automatic mode.
    IODeviceResult SetControlAuto(CameraControl control, bool automatic);

protected:
    CameraDevice(const IODeviceInfo& info, CameraType type);

    // ===== BACKEND HOOKS =====

    virtual IODeviceResult DoGetCapabilities(CameraCapabilities& capabilities) = 0;
    virtual IODeviceResult DoApplyConfiguration(const CameraConfiguration& configuration) = 0;
    virtual IODeviceResult DoCaptureFrame(CameraFrame& frame) = 0;

    // DoStartStream() should return once capture is running; frames are
    // delivered by calling DeliverFrame(). DoStopStream() must stop and join
    // whatever DoStartStream() started, and must tolerate being called when
    // no stream is running.
    virtual IODeviceResult DoStartStream() = 0;
    virtual void DoStopStream() = 0;

    virtual IODeviceResult DoGetControl(CameraControl control, int& value) = 0;
    virtual IODeviceResult DoSetControl(CameraControl control, int value) = 0;
    virtual IODeviceResult DoSetControlAuto(CameraControl control, bool automatic) = 0;

    // ===== FOR BACKENDS =====

    // Call from the capture thread for each frame. Stamps the frame number
    // and invokes the caller's callback.
    void DeliverFrame(CameraFrame& frame);

    // True while the stream should keep running; a capture loop tests this.
    bool ShouldKeepStreaming() const;

    CameraConfiguration configuration;
    CameraCapabilities capabilities;
    bool capabilitiesLoaded = false;

private:
    CameraType cameraType = CameraType::Unknown;
    CameraFrameCallback frameCallback;
    std::atomic<bool> streaming{false};
    std::atomic<uint64_t> frameCount{0};
};

using CameraDevicePtr = std::shared_ptr<CameraDevice>;

} // namespace UltraCanvas
