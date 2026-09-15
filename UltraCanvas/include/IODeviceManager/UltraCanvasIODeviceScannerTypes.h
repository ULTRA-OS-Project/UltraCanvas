// include/IODeviceManager/UltraCanvasIODeviceScannerTypes.h
// Scanner vocabulary: scan area, colour mode, paper source, capabilities and
// the image a scan produces.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasIODeviceTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvas {

// ============================================================================
// COLOUR MODE
// ============================================================================

enum class ScanColorMode {
    Unknown,
    Lineart,        // 1 bit per pixel, black or white
    Halftone,       // 1 bit, dithered
    Grayscale,
    Color
};

const char* ScanColorModeToString(ScanColorMode mode);

// Channels per pixel for a mode: 1 for everything but colour.
int ScanColorModeChannels(ScanColorMode mode);

// ============================================================================
// PAPER SOURCE
// ============================================================================

enum class ScanSource {
    Auto,
    Flatbed,
    ADF,                // automatic document feeder, one side
    ADFDuplex,          // feeder scanning both sides
    TransparencyUnit    // slide and negative adapter
};

const char* ScanSourceToString(ScanSource source);

// Whether a source feeds pages one after another, so a scan continues until
// the tray empties rather than producing exactly one image.
bool ScanSourceIsFeeder(ScanSource source);

// ============================================================================
// SCAN AREA
// ============================================================================

// Hundredths of a millimetre from the scanner's origin, the same unit
// IOPaperDimensions uses, so a scan area and a paper size are directly
// comparable without a conversion at every call site.
struct ScanArea {
    int leftHundredthsMM = 0;
    int topHundredthsMM = 0;
    int rightHundredthsMM = 0;
    int bottomHundredthsMM = 0;

    bool IsValid() const {
        return rightHundredthsMM > leftHundredthsMM &&
               bottomHundredthsMM > topHundredthsMM;
    }

    int WidthHundredthsMM() const { return rightHundredthsMM - leftHundredthsMM; }
    int HeightHundredthsMM() const { return bottomHundredthsMM - topHundredthsMM; }

    // The whole of a paper size, measured from the origin.
    static ScanArea FromPaperSize(int widthHundredthsMM, int heightHundredthsMM) {
        ScanArea area;
        area.rightHundredthsMM = widthHundredthsMM;
        area.bottomHundredthsMM = heightHundredthsMM;
        return area;
    }
};

// ============================================================================
// CONFIGURATION
// ============================================================================

struct ScanConfiguration {
    int resolutionDpi = 0;              // 0 asks for the scanner's default
    ScanColorMode colorMode = ScanColorMode::Color;
    ScanSource source = ScanSource::Auto;

    // An invalid area means the whole scannable bed.
    ScanArea area;

    // Bits per sample per channel: 8 normally, 16 on scanners that offer it,
    // 1 for Lineart. 0 asks for the scanner's default.
    int bitDepth = 0;

    // Feeder scans only: 0 means keep going until the tray is empty.
    int maxPages = 0;
};

// ============================================================================
// CAPABILITIES
// ============================================================================

struct ScanCapabilities {
    // Discrete resolutions the scanner offers. Empty when it reports a
    // continuous range instead, in which case the bounds below apply -
    // empty means "did not enumerate", never "supports none".
    std::vector<int> resolutions;
    int minResolutionDpi = 0;
    int maxResolutionDpi = 0;

    std::vector<ScanColorMode> colorModes;
    std::vector<ScanSource> sources;
    std::vector<int> bitDepths;

    // The largest area the scanner can cover, which is its bed or its feeder
    // page size.
    ScanArea maxArea;

    IOSupport supportsPreview = IOSupport::Unknown;

    bool Supports(ScanColorMode mode) const;
    bool Supports(ScanSource source) const;

    // True when the resolution is in the list, or within the range when the
    // scanner reported one instead.
    bool SupportsResolution(int dpi) const;

    // The offered resolution closest to `dpi`, preferring one at or below it
    // so a scan never silently costs more time and memory than asked for.
    int NearestResolution(int dpi) const;
};

// ============================================================================
// SCANNED IMAGE
// ============================================================================

struct ScannedImage {
    std::vector<uint8_t> data;

    int width = 0;                  // pixels
    int height = 0;                 // pixels; a backend that cannot know the
                                    // page length in advance leaves this 0 and
                                    // ScannerDevice derives it from the data
    int bytesPerLine = 0;
    int bitsPerSample = 8;

    // 0 means the backend did not say, and ScannerDevice fills it in from the
    // colour mode. Defaulting to 1 would make "not reported" and "genuinely
    // one channel" the same value, which is the mistake IOSupport exists to
    // avoid elsewhere in this module.
    int channels = 0;

    ScanColorMode colorMode = ScanColorMode::Unknown;

    // The resolution actually used, which is what makes the pixel dimensions
    // mean a physical size.
    int resolutionDpi = 0;

    // 1-based within one ScanPages() run.
    int pageNumber = 1;

    bool IsValid() const { return !data.empty() && width > 0 && height > 0; }

    // Physical size of the scanned area, derived from the pixel count and the
    // resolution.
    int WidthHundredthsMM() const;
    int HeightHundredthsMM() const;
};

} // namespace UltraCanvas
