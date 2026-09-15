// include/IODeviceManager/UltraCanvasIODeviceScanner.h
// ScannerDevice: the category class for flatbed, feeder and network scanners.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasIODevice.h"
#include "UltraCanvasIODeviceScannerTypes.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

// Called for each page a feeder scan produces. Return false to stop after
// this page; the scan then ends as though the tray had emptied.
using ScanPageCallback = std::function<bool(const ScannedImage& page)>;

// Fraction of the current page, 0.0 to 1.0. A scanner that does not report
// its progress never calls this, so a caller must not wait for 1.0 to know a
// page finished.
using ScanProgressCallback = std::function<void(float fraction)>;

// ============================================================================
// SCANNERDEVICE
// ============================================================================

class ScannerDevice : public IODevice {
public:
    // ===== CAPABILITIES =====

    const ScanCapabilities& GetCapabilities();
    IODeviceResult RefreshCapabilities();

    // ===== CONFIGURATION =====

    // Refuses a colour mode or source the scanner does not offer. A
    // resolution outside its range is snapped to the nearest it does offer,
    // because scanners expose arbitrary dpi values and refusing 301 when the
    // device does 300 helps nobody.
    IODeviceResult SetConfiguration(const ScanConfiguration& configuration);
    ScanConfiguration GetConfiguration() const;

    // Fills in what the caller left unset and reports what it chose, the same
    // contract ResolvePrintOptions() and ResolveConfiguration() have.
    ScanConfiguration ResolveConfiguration(const ScanConfiguration& requested,
                                           std::vector<std::string>* changes = nullptr);

    // ===== SCANNING =====

    // One page. On a feeder this takes the next sheet; on a flatbed it scans
    // the bed. Blocks until the page is complete.
    IODeviceResult Scan(ScannedImage& image);

    // Scans until the feeder empties, the page limit is reached, the callback
    // returns false, or CancelScan() is called. On a flatbed this yields
    // exactly one page. Blocks for the whole run.
    IODeviceResult ScanPages(ScanPageCallback onPage);

    // Asks the current scan to stop. Safe from another thread, and safe when
    // no scan is running. The blocked Scan()/ScanPages() returns Cancelled.
    void CancelScan();

    bool IsScanning() const;

    // Fires on the thread that called Scan()/ScanPages().
    void SetProgressCallback(ScanProgressCallback callback);

protected:
    explicit ScannerDevice(const IODeviceInfo& info);

    // ===== BACKEND HOOKS =====

    virtual IODeviceResult DoGetCapabilities(ScanCapabilities& capabilities) = 0;
    virtual IODeviceResult DoApplyConfiguration(const ScanConfiguration& configuration) = 0;

    // Scans one page into `image`. Returns DeviceNotFound when a feeder has
    // run out of paper, which ends a multi-page run normally rather than as
    // an error.
    virtual IODeviceResult DoScanPage(ScannedImage& image) = 0;

    // Asks the backend to abort a scan in progress. Called from another
    // thread than the one inside DoScanPage(), so it must only signal.
    virtual void DoCancelScan() = 0;

    // ===== FOR BACKENDS =====

    // Reports progress of the page being scanned.
    void ReportProgress(float fraction);

    // False once CancelScan() has been called; a read loop tests this.
    bool ShouldContinueScanning() const;

    ScanConfiguration configuration;
    ScanCapabilities capabilities;
    bool capabilitiesLoaded = false;

private:
    // Runs one page and post-processes the result. Caller must hold
    // deviceMutex.
    IODeviceResult ScanOnePageLocked(ScannedImage& image, int pageNumber);

    ScanProgressCallback progressCallback;
    std::atomic<bool> scanning{false};
    std::atomic<bool> cancelRequested{false};
};

using ScannerDevicePtr = std::shared_ptr<ScannerDevice>;

} // namespace UltraCanvas
