// Tests/IODeviceScannerTest.cpp
// ScannerDevice: configuration resolution and the page loop.
//
// The page loop is what needs guarding. A feeder scan ends when the tray runs
// out, and the backend signals that the same way it signals a failure - by
// not returning a page. Treating the empty tray as an error throws away every
// page already scanned, so the two are distinguished here explicitly, along
// with the other four ways a run ends: the caller's callback saying stop, the
// page limit, cancellation, and a flatbed having exactly one page by
// definition.
//
// Driven by a fake scanner, so it runs with no scanner attached.
// Version: 1.0.0
// Last Modified: 2026-09-14
// Author: UltraCanvas Framework

#include "IODeviceManager/UltraCanvasIODeviceScanner.h"

#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

int g_failures = 0;

void Check(bool condition, const std::string& what) {
    std::cout << (condition ? "  [ OK ] " : "  [FAIL] ") << what << "\n";
    if (!condition) ++g_failures;
}

bool Mentions(const std::vector<std::string>& changes, const std::string& needle) {
    for (const auto& change : changes) {
        if (change.find(needle) != std::string::npos) return true;
    }
    return false;
}

// ===== FAKE SCANNER =====

class FakeScanner : public ScannerDevice {
public:
    explicit FakeScanner(const IODeviceInfo& info) : ScannerDevice(info) {}

    ScanCapabilities fakeCapabilities;

    // How many sheets are in the feeder. Each scanned page consumes one.
    int sheetsLoaded = 0;
    int pagesScanned = 0;
    bool failNextPage = false;
    ScanConfiguration lastApplied;

protected:
    IODeviceResult DoConnect() override { return IODeviceResult::Ok(); }
    void DoDisconnect() override {}

    IODeviceResult DoGetCapabilities(ScanCapabilities& caps) override {
        caps = fakeCapabilities;
        return IODeviceResult::Ok();
    }

    IODeviceResult DoApplyConfiguration(const ScanConfiguration& config) override {
        lastApplied = config;
        return IODeviceResult::Ok();
    }

    IODeviceResult DoScanPage(ScannedImage& image) override {
        if (failNextPage) {
            failNextPage = false;
            return IODeviceResult::Error(IODeviceResultCode::IOError,
                                         "the fake scanner jammed");
        }

        // An empty feeder is how a multi-page run ends, and the backend says
        // so with DeviceNotFound rather than a failure.
        if (sheetsLoaded <= 0) {
            return IODeviceResult::Error(IODeviceResultCode::DeviceNotFound,
                                         "no more sheets in the feeder");
        }
        --sheetsLoaded;
        ++pagesScanned;

        image.width = 100;
        image.bytesPerLine = 300;
        image.bitsPerSample = 8;
        image.data.assign(300 * 50, 0xC0);   // 50 lines
        return IODeviceResult::Ok();
    }

    void DoCancelScan() override { sheetsLoaded = 0; }
};

IODeviceInfo MakeScannerInfo() {
    IODeviceInfo info;
    info.deviceId = "fake:scanner";
    info.name = "Fake Scanner";
    info.category = IODeviceCategory::Scanner;
    info.backend = "Fake";
    return info;
}

std::shared_ptr<FakeScanner> MakeScanner() {
    auto scanner = std::make_shared<FakeScanner>(MakeScannerInfo());
    scanner->fakeCapabilities.resolutions = {75, 150, 300, 600, 1200};
    scanner->fakeCapabilities.colorModes = {ScanColorMode::Lineart,
                                            ScanColorMode::Grayscale,
                                            ScanColorMode::Color};
    scanner->fakeCapabilities.sources = {ScanSource::Flatbed, ScanSource::ADF};
    scanner->fakeCapabilities.maxArea = ScanArea::FromPaperSize(21000, 29700);  // A4
    return scanner;
}

// ===== TESTS =====

void TestResolutionSelection() {
    std::cout << "\nResolution selection\n";

    ScanCapabilities caps;
    caps.resolutions = {75, 150, 300, 600, 1200};

    Check(caps.SupportsResolution(300), "an offered resolution is supported");
    Check(!caps.SupportsResolution(400), "one not offered is not");

    // Scanning higher than asked for costs time and memory quadratically, so
    // the nearest at-or-below is preferred over the numerically closest.
    Check(caps.NearestResolution(400) == 300, "400 resolves down to 300, not up to 600");
    Check(caps.NearestResolution(599) == 300, "599 resolves down to 300");
    Check(caps.NearestResolution(600) == 600, "an exact match is kept");
    Check(caps.NearestResolution(50) == 75,
          "below everything on offer takes the lowest available");
    Check(caps.NearestResolution(5000) == 1200, "above everything takes the highest");

    // A scanner reporting a continuous range rather than a list.
    ScanCapabilities ranged;
    ranged.minResolutionDpi = 50;
    ranged.maxResolutionDpi = 2400;
    Check(ranged.SupportsResolution(137), "any value inside a reported range is supported");
    Check(!ranged.SupportsResolution(4800), "one outside it is not");
    Check(ranged.NearestResolution(4800) == 2400, "and clamps to the range");

    ScanCapabilities silent;
    Check(silent.SupportsResolution(1234),
          "a scanner that reported nothing does not refuse a resolution");
}

void TestConfigurationValidation() {
    std::cout << "\nConfiguration validation\n";

    auto scanner = MakeScanner();
    scanner->Connect();

    ScanConfiguration good;
    good.resolutionDpi = 300;
    good.colorMode = ScanColorMode::Color;
    good.source = ScanSource::Flatbed;
    Check(static_cast<bool>(scanner->SetConfiguration(good)), "a supported setup is accepted");

    // A colour mode is an enumeration: the scanner either has it or does not.
    ScanConfiguration badMode;
    badMode.colorMode = ScanColorMode::Halftone;
    IODeviceResult refused = scanner->SetConfiguration(badMode);
    Check(!static_cast<bool>(refused), "an unsupported colour mode is refused");
    Check(refused.code == IODeviceResultCode::NotSupported, "with NotSupported");

    ScanConfiguration badSource;
    badSource.source = ScanSource::TransparencyUnit;
    Check(!static_cast<bool>(scanner->SetConfiguration(badSource)),
          "a source the scanner lacks is refused");

    // A resolution is a number on a scale, so it is snapped rather than
    // refused - turning down 400 dpi when the device does 300 helps nobody.
    ScanConfiguration oddDpi;
    oddDpi.resolutionDpi = 400;
    oddDpi.colorMode = ScanColorMode::Color;
    Check(static_cast<bool>(scanner->SetConfiguration(oddDpi)),
          "an unlisted resolution is accepted, not refused");
    Check(scanner->GetConfiguration().resolutionDpi == 300,
          "and snapped to the nearest at or below it");
}

void TestResolveConfiguration() {
    std::cout << "\nResolveConfiguration fills in what was left out\n";

    auto scanner = MakeScanner();
    scanner->Connect();

    std::vector<std::string> changes;
    ScanConfiguration resolved = scanner->ResolveConfiguration(ScanConfiguration(), &changes);

    Check(resolved.resolutionDpi == 300, "an unset resolution defaults to 300 dpi");
    Check(resolved.area.IsValid(), "an unset area becomes the whole bed");
    Check(resolved.area.rightHundredthsMM == 21000, "which is A4 wide here");
    Check(Mentions(changes, "300 dpi"), "and the caller is told");

    // An area past the edge of the bed scans nothing useful beyond it.
    ScanConfiguration oversized;
    oversized.area = ScanArea::FromPaperSize(30000, 42000);   // A3 on an A4 bed
    changes.clear();
    resolved = scanner->ResolveConfiguration(oversized, &changes);
    Check(resolved.area.rightHundredthsMM == 21000, "an oversized area is trimmed to the bed");
    Check(Mentions(changes, "past the scannable bed"), "and the reason is given");

    ScanConfiguration unsupported;
    unsupported.colorMode = ScanColorMode::Halftone;
    changes.clear();
    resolved = scanner->ResolveConfiguration(unsupported, &changes);
    Check(resolved.colorMode != ScanColorMode::Halftone, "an unsupported mode is replaced");
    Check(Mentions(changes, "Halftone"), "and named");
}

void TestSinglePageScan() {
    std::cout << "\nSingle-page scan\n";

    auto scanner = MakeScanner();
    scanner->Connect();
    scanner->sheetsLoaded = 1;

    ScanConfiguration config;
    config.resolutionDpi = 300;
    config.colorMode = ScanColorMode::Color;
    config.source = ScanSource::Flatbed;
    scanner->SetConfiguration(config);

    ScannedImage image;
    Check(static_cast<bool>(scanner->Scan(image)), "the page scans");
    Check(image.IsValid(), "and the image is valid");
    Check(image.height == 50, "the height is derived from the data and the line length");
    Check(image.resolutionDpi == 300, "the image records the resolution used");
    Check(image.colorMode == ScanColorMode::Color, "and the colour mode");
    Check(image.channels == 3, "colour means three channels");

    // Pixel dimensions only mean a physical size once the resolution is known.
    Check(image.WidthHundredthsMM() == 100 * 2540 / 300,
          "the physical width follows from the pixels and the dpi");

    Check(!scanner->IsScanning(), "the scanner is idle afterwards");
}

void TestFeederEndsRunNormally() {
    std::cout << "\nAn empty feeder ends a run, it does not fail it\n";

    auto scanner = MakeScanner();
    scanner->Connect();
    scanner->sheetsLoaded = 3;

    ScanConfiguration config;
    config.source = ScanSource::ADF;
    config.resolutionDpi = 300;
    scanner->SetConfiguration(config);

    std::vector<int> pages;
    IODeviceResult result = scanner->ScanPages([&](const ScannedImage& page) {
        pages.push_back(page.pageNumber);
        return true;
    });

    // The pages already scanned are the result, not collateral of an error.
    Check(static_cast<bool>(result), "the run succeeds when the tray empties");
    Check(pages.size() == 3, "every loaded sheet was scanned");
    Check(result.backendCode == 3, "and the page count comes back in backendCode");
    Check(pages[0] == 1 && pages[2] == 3, "pages are numbered from one");

    // A genuine failure is still a failure.
    scanner->sheetsLoaded = 2;
    scanner->failNextPage = true;
    IODeviceResult failed = scanner->ScanPages([](const ScannedImage&) { return true; });
    Check(!static_cast<bool>(failed), "a real error fails the run");
    Check(failed.code == IODeviceResultCode::IOError, "keeping the backend's code");
}

void TestPageLoopStopConditions() {
    std::cout << "\nThe other ways a run ends\n";

    // A flatbed has one page by definition, however many sheets are notionally
    // available.
    auto flatbed = MakeScanner();
    flatbed->Connect();
    flatbed->sheetsLoaded = 10;
    ScanConfiguration bed;
    bed.source = ScanSource::Flatbed;
    flatbed->SetConfiguration(bed);

    int count = 0;
    flatbed->ScanPages([&](const ScannedImage&) { ++count; return true; });
    Check(count == 1, "a flatbed scan produces exactly one page");

    // The caller's callback can stop the run.
    auto feeder = MakeScanner();
    feeder->Connect();
    feeder->sheetsLoaded = 10;
    ScanConfiguration adf;
    adf.source = ScanSource::ADF;
    feeder->SetConfiguration(adf);

    count = 0;
    feeder->ScanPages([&](const ScannedImage&) { return ++count < 2; });
    Check(count == 2, "returning false from the callback stops the run");

    // And so can the page limit.
    auto limited = MakeScanner();
    limited->Connect();
    limited->sheetsLoaded = 10;
    ScanConfiguration capped;
    capped.source = ScanSource::ADF;
    capped.maxPages = 4;
    limited->SetConfiguration(capped);

    count = 0;
    IODeviceResult result = limited->ScanPages([&](const ScannedImage&) { ++count; return true; });
    Check(count == 4, "the page limit stops the run");
    Check(static_cast<bool>(result), "and that is a success, not a failure");
}

void TestCancellation() {
    std::cout << "\nCancellation\n";

    auto scanner = MakeScanner();
    scanner->Connect();
    scanner->sheetsLoaded = 10;

    ScanConfiguration adf;
    adf.source = ScanSource::ADF;
    scanner->SetConfiguration(adf);

    // Cancelling from inside the callback is the realistic case: that is
    // where a UI's stop button lands.
    int count = 0;
    IODeviceResult result = scanner->ScanPages([&](const ScannedImage&) {
        ++count;
        if (count == 2) {
            scanner->CancelScan();
        }
        return true;
    });

    Check(!static_cast<bool>(result), "a cancelled run reports failure");
    Check(result.code == IODeviceResultCode::Cancelled, "with Cancelled");
    Check(result.backendCode == 2, "and still reports the pages it managed");
    Check(!scanner->IsScanning(), "the scanner is idle afterwards");

    // Cancelling when nothing is running must be harmless.
    scanner->CancelScan();
    Check(true, "cancelling an idle scanner is harmless");
}

void TestScanRequiresConnection() {
    std::cout << "\nScanning needs an open device\n";

    auto scanner = MakeScanner();
    ScannedImage image;
    Check(!static_cast<bool>(scanner->Scan(image)), "a disconnected scanner cannot scan");
    Check(!static_cast<bool>(scanner->ScanPages([](const ScannedImage&) { return true; })),
          "nor scan pages");

    scanner->Connect();
    Check(!static_cast<bool>(scanner->ScanPages(nullptr)),
          "scanning pages without a callback is refused");
}

void TestCapabilityDefaults() {
    std::cout << "\nAn unreported capability is not a refusal\n";

    ScanCapabilities silent;
    Check(silent.Supports(ScanColorMode::Color), "an unreported colour mode is allowed");
    Check(silent.Supports(ScanSource::ADF), "an unreported source is allowed");

    ScanArea area = ScanArea::FromPaperSize(21000, 29700);
    Check(area.IsValid(), "a paper-sized area is valid");
    Check(area.WidthHundredthsMM() == 21000, "and reports its width");
    Check(!ScanArea().IsValid(), "a zero area is not valid");

    Check(ScanSourceIsFeeder(ScanSource::ADF), "an ADF is a feeder");
    Check(ScanSourceIsFeeder(ScanSource::ADFDuplex), "so is a duplex ADF");
    Check(!ScanSourceIsFeeder(ScanSource::Flatbed), "a flatbed is not");
    Check(ScanColorModeChannels(ScanColorMode::Color) == 3, "colour has three channels");
    Check(ScanColorModeChannels(ScanColorMode::Grayscale) == 1, "grey has one");
}

}  // namespace

int main() {
    std::cout << "IODeviceManager scanner tests\n";
    std::cout << "=============================\n";

    TestResolutionSelection();
    TestConfigurationValidation();
    TestResolveConfiguration();
    TestSinglePageScan();
    TestFeederEndsRunNormally();
    TestPageLoopStopConditions();
    TestCancellation();
    TestScanRequiresConnection();
    TestCapabilityDefaults();

    std::cout << "\n";
    if (g_failures == 0) {
        std::cout << "All scanner tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " scanner test(s) FAILED.\n";
    return 1;
}
