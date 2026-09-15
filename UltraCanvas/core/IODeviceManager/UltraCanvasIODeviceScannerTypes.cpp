// core/IODeviceManager/UltraCanvasIODeviceScannerTypes.cpp
// Scanner vocabulary: mode properties, capability queries and image geometry.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "../../include/IODeviceManager/UltraCanvasIODeviceScannerTypes.h"

#include <algorithm>
#include <cstdlib>

namespace UltraCanvas {

const char* ScanColorModeToString(ScanColorMode mode) {
    switch (mode) {
        case ScanColorMode::Lineart:   return "Lineart";
        case ScanColorMode::Halftone:  return "Halftone";
        case ScanColorMode::Grayscale: return "Grayscale";
        case ScanColorMode::Color:     return "Color";
        case ScanColorMode::Unknown:   break;
    }
    return "Unknown";
}

int ScanColorModeChannels(ScanColorMode mode) {
    return mode == ScanColorMode::Color ? 3 : 1;
}

const char* ScanSourceToString(ScanSource source) {
    switch (source) {
        case ScanSource::Auto:             return "Auto";
        case ScanSource::Flatbed:          return "Flatbed";
        case ScanSource::ADF:              return "Document Feeder";
        case ScanSource::ADFDuplex:        return "Document Feeder (duplex)";
        case ScanSource::TransparencyUnit: return "Transparency Unit";
    }
    return "Unknown";
}

bool ScanSourceIsFeeder(ScanSource source) {
    return source == ScanSource::ADF || source == ScanSource::ADFDuplex;
}

// ============================================================================
// CAPABILITIES
// ============================================================================

bool ScanCapabilities::Supports(ScanColorMode mode) const {
    // Empty means the backend did not enumerate, not that nothing is
    // supported - the rule every capability list in this module follows.
    if (colorModes.empty()) {
        return true;
    }
    return std::find(colorModes.begin(), colorModes.end(), mode) != colorModes.end();
}

bool ScanCapabilities::Supports(ScanSource source) const {
    if (sources.empty() || source == ScanSource::Auto) {
        return true;
    }
    return std::find(sources.begin(), sources.end(), source) != sources.end();
}

bool ScanCapabilities::SupportsResolution(int dpi) const {
    if (dpi <= 0) {
        return false;
    }
    if (!resolutions.empty()) {
        return std::find(resolutions.begin(), resolutions.end(), dpi) != resolutions.end();
    }
    if (minResolutionDpi > 0 && maxResolutionDpi > 0) {
        return dpi >= minResolutionDpi && dpi <= maxResolutionDpi;
    }
    return true;    // nothing reported
}

int ScanCapabilities::NearestResolution(int dpi) const {
    if (dpi <= 0) {
        return dpi;
    }

    if (!resolutions.empty()) {
        // Prefer the closest at or below the request: scanning at a higher
        // dpi than asked for costs time and memory quadratically, which is
        // not a substitution to make silently.
        int best = 0;
        for (int candidate : resolutions) {
            if (candidate <= dpi && candidate > best) {
                best = candidate;
            }
        }
        if (best > 0) {
            return best;
        }
        // Everything on offer is higher, so take the lowest of those.
        return *std::min_element(resolutions.begin(), resolutions.end());
    }

    if (minResolutionDpi > 0 && dpi < minResolutionDpi) {
        return minResolutionDpi;
    }
    if (maxResolutionDpi > 0 && dpi > maxResolutionDpi) {
        return maxResolutionDpi;
    }
    return dpi;
}

// ============================================================================
// SCANNED IMAGE
// ============================================================================

namespace {
// 1 inch is 2540 hundredths of a millimetre.
constexpr int kHundredthsMMPerInch = 2540;
}

int ScannedImage::WidthHundredthsMM() const {
    if (resolutionDpi <= 0) {
        return 0;
    }
    return width * kHundredthsMMPerInch / resolutionDpi;
}

int ScannedImage::HeightHundredthsMM() const {
    if (resolutionDpi <= 0) {
        return 0;
    }
    return height * kHundredthsMMPerInch / resolutionDpi;
}

}  // namespace UltraCanvas
