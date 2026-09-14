// core/IODeviceManager/UltraCanvasIODeviceScanner.cpp
// ScannerDevice: configuration resolution, the single- and multi-page scan
// loops and cancellation. Platform-neutral.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "../../include/IODeviceManager/UltraCanvasIODeviceScanner.h"

#include <algorithm>

namespace UltraCanvas {

ScannerDevice::ScannerDevice(const IODeviceInfo& info) : IODevice(info) {}

// ===== CAPABILITIES =====

const ScanCapabilities& ScannerDevice::GetCapabilities() {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    if (!capabilitiesLoaded) {
        RefreshCapabilities();
    }
    return capabilities;
}

IODeviceResult ScannerDevice::RefreshCapabilities() {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);

    ScanCapabilities fresh;
    IODeviceResult result = DoGetCapabilities(fresh);
    if (result.success) {
        capabilities = fresh;
        capabilitiesLoaded = true;
    }
    return result;
}

// ===== CONFIGURATION =====

IODeviceResult ScannerDevice::SetConfiguration(const ScanConfiguration& requested) {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);

    IODeviceResult ready = RequireConnected();
    if (!ready.success) {
        return ready;
    }
    if (scanning) {
        return IODeviceResult::Error(IODeviceResultCode::DeviceBusy,
                                     "This scanner is mid-scan", GetDeviceId());
    }

    const ScanCapabilities& caps = GetCapabilities();

    // Colour mode and source are enumerations: a scanner either has them or
    // does not, so an unsupported one is an error rather than something to
    // substitute.
    if (!caps.Supports(requested.colorMode)) {
        return IODeviceResult::Error(
            IODeviceResultCode::NotSupported,
            std::string("This scanner does not offer ") +
                ScanColorModeToString(requested.colorMode),
            GetDeviceId());
    }
    if (!caps.Supports(requested.source)) {
        return IODeviceResult::Error(
            IODeviceResultCode::NotSupported,
            std::string("This scanner has no ") + ScanSourceToString(requested.source),
            GetDeviceId());
    }

    // Resolution is a number on a scale, and scanners expose arbitrary
    // values, so it is snapped rather than refused: turning down 301 dpi when
    // the device does 300 helps nobody.
    ScanConfiguration applied = requested;
    if (applied.resolutionDpi > 0 && !caps.SupportsResolution(applied.resolutionDpi)) {
        applied.resolutionDpi = caps.NearestResolution(applied.resolutionDpi);
    }

    IODeviceResult result = DoApplyConfiguration(applied);
    if (result.success) {
        configuration = applied;
    } else {
        SetLastError(result);
    }
    return result;
}

ScanConfiguration ScannerDevice::GetConfiguration() const {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    return configuration;
}

ScanConfiguration
ScannerDevice::ResolveConfiguration(const ScanConfiguration& requested,
                                    std::vector<std::string>* changes) {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);

    ScanConfiguration resolved = requested;
    const ScanCapabilities& caps = GetCapabilities();

    auto note = [&](const std::string& text) {
        if (changes) {
            changes->push_back(text);
        }
    };

    if (!caps.Supports(resolved.colorMode)) {
        const ScanColorMode wanted = resolved.colorMode;
        const ScanColorMode fallback = caps.Supports(ScanColorMode::Color)
                                           ? ScanColorMode::Color
                                           : (caps.colorModes.empty()
                                                  ? ScanColorMode::Grayscale
                                                  : caps.colorModes.front());
        note(std::string(ScanColorModeToString(wanted)) +
             " is not supported, using " + ScanColorModeToString(fallback));
        resolved.colorMode = fallback;
    }

    if (!caps.Supports(resolved.source)) {
        const ScanSource wanted = resolved.source;
        const ScanSource fallback =
            caps.sources.empty() ? ScanSource::Auto : caps.sources.front();
        note(std::string("This scanner has no ") + ScanSourceToString(wanted) +
             ", using " + ScanSourceToString(fallback));
        resolved.source = fallback;
    }

    if (resolved.resolutionDpi <= 0) {
        // 300 dpi is the document-scanning default: enough for OCR and for
        // print, without the fourfold size of 600.
        const int preferred = caps.SupportsResolution(300)
                                  ? 300
                                  : caps.NearestResolution(300);
        if (preferred > 0) {
            note("No resolution asked for, using " + std::to_string(preferred) + " dpi");
            resolved.resolutionDpi = preferred;
        }
    } else if (!caps.SupportsResolution(resolved.resolutionDpi)) {
        const int nearest = caps.NearestResolution(resolved.resolutionDpi);
        note(std::to_string(resolved.resolutionDpi) + " dpi is not offered, using " +
             std::to_string(nearest) + " dpi");
        resolved.resolutionDpi = nearest;
    }

    if (!resolved.area.IsValid() && caps.maxArea.IsValid()) {
        note("No scan area asked for, using the whole bed");
        resolved.area = caps.maxArea;
    } else if (resolved.area.IsValid() && caps.maxArea.IsValid()) {
        // An area reaching past the bed scans nothing useful beyond its edge,
        // so it is trimmed to what the scanner can actually reach.
        if (resolved.area.rightHundredthsMM > caps.maxArea.rightHundredthsMM ||
            resolved.area.bottomHundredthsMM > caps.maxArea.bottomHundredthsMM) {
            note("The scan area reaches past the scannable bed, trimming it");
            resolved.area.rightHundredthsMM =
                std::min(resolved.area.rightHundredthsMM, caps.maxArea.rightHundredthsMM);
            resolved.area.bottomHundredthsMM =
                std::min(resolved.area.bottomHundredthsMM, caps.maxArea.bottomHundredthsMM);
        }
    }

    return resolved;
}

// ===== SCANNING =====

IODeviceResult ScannerDevice::ScanOnePageLocked(ScannedImage& image, int pageNumber) {
    image = ScannedImage();
    image.pageNumber = pageNumber;
    image.resolutionDpi = configuration.resolutionDpi;
    image.colorMode = configuration.colorMode;

    IODeviceResult result = DoScanPage(image);

    // The backend may not know these; fill in what the configuration implies
    // so a caller never has to guess how to read the buffer.
    if (result.success) {
        if (image.resolutionDpi <= 0) {
            image.resolutionDpi = configuration.resolutionDpi;
        }
        if (image.colorMode == ScanColorMode::Unknown) {
            image.colorMode = configuration.colorMode;
        }
        if (image.channels <= 0) {
            image.channels = ScanColorModeChannels(image.colorMode);
        }

        // Derived here rather than in each backend: a scanner often cannot
        // say how long a page is until it has fed the whole sheet, so the
        // height falls out of how much data arrived. Doing it once here means
        // no backend has to remember to, and none can get it subtly different.
        if (image.height <= 0 && image.bytesPerLine > 0) {
            image.height = static_cast<int>(image.data.size() /
                                            static_cast<size_t>(image.bytesPerLine));
        }
    }
    return result;
}

IODeviceResult ScannerDevice::Scan(ScannedImage& image) {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);

    IODeviceResult ready = RequireConnected();
    if (!ready.success) {
        return ready;
    }
    if (scanning) {
        return IODeviceResult::Error(IODeviceResultCode::DeviceBusy,
                                     "This scanner is already scanning", GetDeviceId());
    }

    scanning = true;
    cancelRequested = false;
    SetState(IODeviceState::Busy);

    IODeviceResult result = ScanOnePageLocked(image, 1);

    scanning = false;
    SetState(result.success ? IODeviceState::Ready : IODeviceState::Error);

    if (!result.success) {
        if (cancelRequested) {
            result = IODeviceResult::Error(IODeviceResultCode::Cancelled,
                                           "The scan was cancelled", GetDeviceId());
        } else {
            SetLastError(result);
        }
    }
    return result;
}

IODeviceResult ScannerDevice::ScanPages(ScanPageCallback onPage) {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);

    IODeviceResult ready = RequireConnected();
    if (!ready.success) {
        return ready;
    }
    if (!onPage) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                     "Scanning pages needs a page callback",
                                     GetDeviceId());
    }
    if (scanning) {
        return IODeviceResult::Error(IODeviceResultCode::DeviceBusy,
                                     "This scanner is already scanning", GetDeviceId());
    }

    scanning = true;
    cancelRequested = false;
    SetState(IODeviceState::Busy);

    const bool feeder = ScanSourceIsFeeder(configuration.source);
    const int limit = configuration.maxPages;

    int pages = 0;
    IODeviceResult result = IODeviceResult::Ok(GetDeviceId());

    while (!cancelRequested) {
        ScannedImage page;
        IODeviceResult scanned = ScanOnePageLocked(page, pages + 1);

        if (!scanned.success) {
            // An empty feeder ends the run normally: the pages already
            // scanned are the result, not a failure.
            if (scanned.code == IODeviceResultCode::DeviceNotFound && pages > 0) {
                break;
            }
            result = scanned;
            break;
        }

        ++pages;
        if (!onPage(page)) {
            break;
        }

        // A flatbed has one page by definition; only a feeder continues.
        if (!feeder) {
            break;
        }
        if (limit > 0 && pages >= limit) {
            break;
        }
    }

    scanning = false;

    if (cancelRequested && result.success) {
        result = IODeviceResult::Error(IODeviceResultCode::Cancelled,
                                       "The scan was cancelled", GetDeviceId());
    }

    SetState(result.success ? IODeviceState::Ready : IODeviceState::Error);
    if (!result.success && !cancelRequested) {
        SetLastError(result);
    }

    // The page count rides back in backendCode so a caller knows how many
    // pages a cancelled or failed run produced before it stopped.
    result.backendCode = pages;
    return result;
}

void ScannerDevice::CancelScan() {
    // Deliberately takes no lock: the scanning thread holds deviceMutex for
    // the whole run, so a cancel that waited for it could never arrive in
    // time to cancel anything.
    if (!scanning) {
        return;
    }
    cancelRequested = true;
    DoCancelScan();
}

bool ScannerDevice::IsScanning() const { return scanning; }

bool ScannerDevice::ShouldContinueScanning() const { return !cancelRequested; }

void ScannerDevice::SetProgressCallback(ScanProgressCallback callback) {
    std::lock_guard<std::recursive_mutex> lock(deviceMutex);
    progressCallback = std::move(callback);
}

void ScannerDevice::ReportProgress(float fraction) {
    if (progressCallback) {
        progressCallback(fraction);
    }
}

}  // namespace UltraCanvas
