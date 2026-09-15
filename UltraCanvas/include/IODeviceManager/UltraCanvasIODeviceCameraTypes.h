// include/IODeviceManager/UltraCanvasIODeviceCameraTypes.h
// Camera vocabulary: resolutions, pixel formats, frames, controls and
// capabilities, shared by webcams, DSLRs and network cameras.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasIODeviceTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

// ============================================================================
// CAMERA KIND
// ============================================================================

enum class CameraType {
    Unknown,
    Webcam,         // UVC or built-in, streams continuously
    DSLR,           // tethered stills camera, PTP/MTP
    NetworkCamera,  // RTSP/ONVIF over the network
    Virtual         // software source, loopback, test double
};

const char* CameraTypeToString(CameraType type);

// ============================================================================
// RESOLUTION
// ============================================================================

struct CameraResolution {
    int width = 0;
    int height = 0;

    CameraResolution() = default;
    CameraResolution(int w, int h) : width(w), height(h) {}

    bool IsValid() const { return width > 0 && height > 0; }
    int PixelCount() const { return width * height; }

    bool operator==(const CameraResolution& other) const {
        return width == other.width && height == other.height;
    }
    bool operator!=(const CameraResolution& other) const { return !(*this == other); }

    std::string ToString() const;
};

// ============================================================================
// PIXEL FORMAT
// ============================================================================

enum class CameraPixelFormat {
    Unknown,
    // Uncompressed
    RGB24,
    BGR24,
    RGBA32,
    Gray8,
    // Packed YUV
    YUYV,
    UYVY,
    // Planar YUV
    NV12,
    YU12,           // also called I420
    // Compressed
    MJPEG,
    JPEG,
    H264,
    H265,
    // Camera raw (DSLRs)
    Raw
};

const char* CameraPixelFormatToString(CameraPixelFormat format);

// True for formats that arrive as a compressed bitstream, so a frame's byte
// count bears no fixed relation to its pixel count and it has to be decoded
// before anything can read pixels out of it.
bool CameraPixelFormatIsCompressed(CameraPixelFormat format);

// Bytes one frame occupies, or 0 for a compressed format where the size
// varies per frame.
size_t CameraFrameSize(CameraPixelFormat format, const CameraResolution& resolution);

// ============================================================================
// FRAME
// ============================================================================

struct CameraFrame {
    std::vector<uint8_t> data;
    CameraResolution resolution;
    CameraPixelFormat format = CameraPixelFormat::Unknown;

    // Microseconds since an unspecified monotonic origin - good for frame
    // intervals and A/V sync, not for wall-clock time. Backends pass through
    // the driver's own timestamp where there is one, because a clock read in
    // the callback has already drifted from when the sensor was exposed.
    uint64_t timestampMicros = 0;

    // Counts frames the device delivered, so a gap tells a caller frames were
    // dropped rather than merely delayed.
    uint64_t frameNumber = 0;

    bool IsValid() const { return !data.empty() && resolution.IsValid(); }
};

// ============================================================================
// CONTROLS
// ============================================================================

// One enumeration rather than a bool per control: which controls exist is a
// property of the device, and a struct with a field per control has to guess
// the union of every camera in advance - and then cannot say whether a given
// camera has one.
enum class CameraControl {
    Brightness,
    Contrast,
    Saturation,
    Hue,
    Sharpness,
    Gamma,
    Gain,
    Exposure,
    Focus,
    WhiteBalanceTemperature,
    Zoom,
    Pan,
    Tilt,
    BacklightCompensation
};

const char* CameraControlToString(CameraControl control);

// What a device reports for one control. `supported` false means this camera
// does not have it at all.
struct CameraControlRange {
    bool supported = false;
    int minimum = 0;
    int maximum = 0;
    int step = 1;
    int defaultValue = 0;

    // Whether the control also has an automatic mode, as exposure, focus and
    // white balance usually do.
    bool autoCapable = false;

    // Clamps to the range and snaps to the step, so a caller passing a slider
    // position gets a value the device will take.
    int Clamp(int value) const;
};

// ============================================================================
// CONFIGURATION
// ============================================================================

struct CameraConfiguration {
    CameraResolution resolution;
    CameraPixelFormat pixelFormat = CameraPixelFormat::Unknown;

    // Frames per second. 0 asks the device for its default.
    int frameRate = 0;

    // How many buffers the capture path keeps in flight. More absorbs a slow
    // consumer at the cost of latency and memory.
    int bufferCount = 4;

    bool IsValid() const { return resolution.IsValid(); }
};

// ============================================================================
// CAPABILITIES
// ============================================================================

struct CameraFormatCapability {
    CameraPixelFormat format = CameraPixelFormat::Unknown;
    std::vector<CameraResolution> resolutions;

    // Frame rates the device reports for this format. Empty means it did not
    // say, not that it supports none.
    std::vector<int> frameRates;
};

struct CameraCapabilities {
    std::vector<CameraFormatCapability> formats;

    IOSupport supportsStreaming = IOSupport::Unknown;
    IOSupport supportsStillCapture = IOSupport::Unknown;

    // Present only for the controls this camera actually has, so iterating it
    // enumerates the controls rather than testing fourteen booleans.
    std::vector<std::pair<CameraControl, CameraControlRange>> controls;

    bool Supports(CameraPixelFormat format) const;
    bool Supports(CameraPixelFormat format, const CameraResolution& resolution) const;

    // An unsupported control returns a range with `supported` false.
    CameraControlRange GetControlRange(CameraControl control) const;

    std::vector<CameraPixelFormat> GetFormats() const;
    std::vector<CameraResolution> GetResolutions(CameraPixelFormat format) const;

    // Largest resolution offered for `format`, or an invalid resolution when
    // the format is not offered.
    CameraResolution GetLargestResolution(CameraPixelFormat format) const;
};

} // namespace UltraCanvas
