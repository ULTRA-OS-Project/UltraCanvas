// core/IODeviceManager/UltraCanvasIODeviceCameraTypes.cpp
// Camera vocabulary: format properties, frame sizing and capability queries.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "../../include/IODeviceManager/UltraCanvasIODeviceCameraTypes.h"

#include <algorithm>

namespace UltraCanvas {

const char* CameraTypeToString(CameraType type) {
    switch (type) {
        case CameraType::Webcam:        return "Webcam";
        case CameraType::DSLR:          return "DSLR";
        case CameraType::NetworkCamera: return "Network Camera";
        case CameraType::Virtual:       return "Virtual";
        case CameraType::Unknown:       break;
    }
    return "Unknown";
}

std::string CameraResolution::ToString() const {
    return std::to_string(width) + "x" + std::to_string(height);
}

const char* CameraPixelFormatToString(CameraPixelFormat format) {
    switch (format) {
        case CameraPixelFormat::RGB24:  return "RGB24";
        case CameraPixelFormat::BGR24:  return "BGR24";
        case CameraPixelFormat::RGBA32: return "RGBA32";
        case CameraPixelFormat::Gray8:  return "Gray8";
        case CameraPixelFormat::YUYV:   return "YUYV";
        case CameraPixelFormat::UYVY:   return "UYVY";
        case CameraPixelFormat::NV12:   return "NV12";
        case CameraPixelFormat::YU12:   return "YU12";
        case CameraPixelFormat::MJPEG:  return "MJPEG";
        case CameraPixelFormat::JPEG:   return "JPEG";
        case CameraPixelFormat::H264:   return "H.264";
        case CameraPixelFormat::H265:   return "H.265";
        case CameraPixelFormat::Raw:    return "Raw";
        case CameraPixelFormat::Unknown: break;
    }
    return "Unknown";
}

bool CameraPixelFormatIsCompressed(CameraPixelFormat format) {
    switch (format) {
        case CameraPixelFormat::MJPEG:
        case CameraPixelFormat::JPEG:
        case CameraPixelFormat::H264:
        case CameraPixelFormat::H265:
        case CameraPixelFormat::Raw:
            return true;
        default:
            return false;
    }
}

size_t CameraFrameSize(CameraPixelFormat format, const CameraResolution& resolution) {
    if (!resolution.IsValid() || CameraPixelFormatIsCompressed(format)) {
        // A compressed frame's size varies per frame, so there is no answer
        // to give here; 0 says so rather than guessing a worst case.
        return 0;
    }

    const size_t pixels = static_cast<size_t>(resolution.width) *
                          static_cast<size_t>(resolution.height);
    switch (format) {
        case CameraPixelFormat::Gray8:  return pixels;
        case CameraPixelFormat::NV12:
        case CameraPixelFormat::YU12:   return pixels * 3 / 2;   // 4:2:0
        case CameraPixelFormat::YUYV:
        case CameraPixelFormat::UYVY:   return pixels * 2;       // 4:2:2
        case CameraPixelFormat::RGB24:
        case CameraPixelFormat::BGR24:  return pixels * 3;
        case CameraPixelFormat::RGBA32: return pixels * 4;
        default: break;
    }
    return 0;
}

int CameraControlRange::Clamp(int value) const {
    if (!supported) {
        return value;
    }
    int clamped = std::max(minimum, std::min(maximum, value));

    // Snap to the step relative to the minimum, the way the device counts
    // them - a control with minimum 1 and step 2 takes 1, 3, 5, not 0, 2, 4.
    if (step > 1) {
        const int offset = clamped - minimum;
        clamped = minimum + (offset / step) * step;
        if (clamped > maximum) {
            clamped -= step;
        }
    }
    return clamped;
}

const char* CameraControlToString(CameraControl control) {
    switch (control) {
        case CameraControl::Brightness:              return "Brightness";
        case CameraControl::Contrast:                return "Contrast";
        case CameraControl::Saturation:              return "Saturation";
        case CameraControl::Hue:                     return "Hue";
        case CameraControl::Sharpness:               return "Sharpness";
        case CameraControl::Gamma:                   return "Gamma";
        case CameraControl::Gain:                    return "Gain";
        case CameraControl::Exposure:                return "Exposure";
        case CameraControl::Focus:                   return "Focus";
        case CameraControl::WhiteBalanceTemperature: return "White Balance";
        case CameraControl::Zoom:                    return "Zoom";
        case CameraControl::Pan:                     return "Pan";
        case CameraControl::Tilt:                    return "Tilt";
        case CameraControl::BacklightCompensation:   return "Backlight Compensation";
    }
    return "Unknown";
}

// ============================================================================
// CAPABILITIES
// ============================================================================

bool CameraCapabilities::Supports(CameraPixelFormat format) const {
    for (const auto& entry : formats) {
        if (entry.format == format) {
            return true;
        }
    }
    return false;
}

bool CameraCapabilities::Supports(CameraPixelFormat format,
                                  const CameraResolution& resolution) const {
    for (const auto& entry : formats) {
        if (entry.format != format) {
            continue;
        }
        // An empty resolution list means the device did not enumerate them -
        // some drivers only report a continuous range - so the format is
        // taken to accept the resolution rather than refusing everything.
        if (entry.resolutions.empty()) {
            return true;
        }
        for (const auto& candidate : entry.resolutions) {
            if (candidate == resolution) {
                return true;
            }
        }
        return false;
    }
    return false;
}

CameraControlRange CameraCapabilities::GetControlRange(CameraControl control) const {
    for (const auto& entry : controls) {
        if (entry.first == control) {
            return entry.second;
        }
    }
    return CameraControlRange();
}

std::vector<CameraPixelFormat> CameraCapabilities::GetFormats() const {
    std::vector<CameraPixelFormat> result;
    result.reserve(formats.size());
    for (const auto& entry : formats) {
        result.push_back(entry.format);
    }
    return result;
}

std::vector<CameraResolution>
CameraCapabilities::GetResolutions(CameraPixelFormat format) const {
    for (const auto& entry : formats) {
        if (entry.format == format) {
            return entry.resolutions;
        }
    }
    return {};
}

CameraResolution
CameraCapabilities::GetLargestResolution(CameraPixelFormat format) const {
    CameraResolution largest;
    for (const auto& entry : formats) {
        if (entry.format != format) {
            continue;
        }
        for (const auto& resolution : entry.resolutions) {
            if (resolution.PixelCount() > largest.PixelCount()) {
                largest = resolution;
            }
        }
    }
    return largest;
}

}  // namespace UltraCanvas
