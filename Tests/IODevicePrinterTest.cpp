// Tests/IODevicePrinterTest.cpp
// PrinterDevice: renderer selection and the option resolver.
//
// Two things are worth guarding here.
//
// The first is the renderer/transport seam, which is what lets an application
// pick GutenPrint or the platform driver on any platform. A renderer that
// emits the printer's own command stream — GutenPrint does — is only usable
// where the transport can carry a raw job, so the test drives a raw-producing
// renderer against a transport that allows raw and one that does not, and
// checks it is offered in the first case and withheld in the second. That is
// the property the design turns on, and it is asserted without libgutenprint
// present.
//
// The second is ResolvePrintOptions(), which folds a requested option set
// down to what a printer will accept, in GutenPrint's priority order. The
// version this replaces was a FIXME that returned its input unchanged, so a
// caller asking for 2880 dpi on plain paper got a silent substitution in the
// output tray instead of an answer.
// Version: 1.0.0
// Last Modified: 2026-09-14
// Author: UltraCanvas Framework

#include "IODeviceManager/UltraCanvasIODevicePrinter.h"

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

// ===== FAKES =====

class FakeTransport : public IPrintTransport {
public:
    explicit FakeTransport(bool raw) : rawSupported(raw) {}

    std::string GetName() const override { return "Fake"; }
    bool SupportsRaw() const override { return rawSupported; }

    IODeviceResult Submit(const IODeviceInfo& printer, const IOPrintPayload& payload,
                          const IOPrintOptions& options, int& outJobId) override {
        (void)printer;
        lastPayloadWasRaw = payload.isRaw;
        lastContentType = payload.contentType;
        lastOptions = options;
        ++submissions;
        outJobId = 4242;
        return IODeviceResult::Ok();
    }

    bool rawSupported = true;
    bool lastPayloadWasRaw = false;
    std::string lastContentType;
    IOPrintOptions lastOptions;
    int submissions = 0;
};

// Stands in for the GutenPrint renderer: emits the printer's own command
// stream, so it needs a transport that can carry a raw job.
class FakeRawRenderer : public IPrintRenderer {
public:
    IOPrintRenderer GetKind() const override { return IOPrintRenderer::GutenPrint; }
    bool IsAvailable() const override { return available; }
    bool ProducesRawStream() const override { return true; }

    bool SupportsPrinter(const IODeviceInfo& printer) const override {
        return knowsModel && printer.model != "Unknown Model";
    }

    IODeviceResult Render(const IOPrintJob& job, const IOPrinterCapabilities& capabilities,
                          IOPrintPayload& payload) override {
        (void)job;
        (void)capabilities;
        payload.data = {0x1B, 0x40};   // ESC @ — an ESC/P2 reset, near enough
        payload.isRaw = true;
        ++renders;
        return IODeviceResult::Ok();
    }

    bool available = true;
    bool knowsModel = true;
    int renders = 0;
};

class FakePrinter : public PrinterDevice {
public:
    FakePrinter(const IODeviceInfo& info, const std::shared_ptr<FakeTransport>& t)
        : PrinterDevice(info), transport(t) {
        AddRenderer(std::make_shared<NativePrintRenderer>());
    }

    IOPrinterCapabilities capabilities;

protected:
    IODeviceResult DoConnect() override { return IODeviceResult::Ok(); }
    void DoDisconnect() override {}

    IODeviceResult DoGetCapabilities(IOPrinterCapabilities& out) override {
        out = capabilities;
        return IODeviceResult::Ok();
    }
    IOPrinterStatus DoGetStatus() override {
        IOPrinterStatus status;
        status.state = IOPrinterState::Idle;
        status.acceptingJobs = true;
        return status;
    }
    std::vector<IOSupplyLevel> DoGetSupplyLevels() override { return {}; }
    IODeviceResult DoCancelJob(int) override { return IODeviceResult::Ok(); }
    IOPrintJobStatus DoGetJobStatus(int) override { return IOPrintJobStatus(); }
    std::vector<IOPrintJobStatus> DoGetJobQueue() override { return {}; }
    IPrintTransportPtr GetTransport() override { return transport; }

private:
    std::shared_ptr<FakeTransport> transport;
};

IODeviceInfo MakePrinterInfo(const std::string& model = "WF-7720") {
    IODeviceInfo info;
    info.deviceId = "fake:printer";
    info.name = "Fake Printer";
    info.model = model;
    info.category = IODeviceCategory::Printer;
    info.backend = "Fake";
    return info;
}

// ===== TESTS =====

void TestRendererAvailability() {
    std::cout << "\nRenderer availability\n";

    auto transport = std::make_shared<FakeTransport>(/*raw=*/true);
    auto printer = std::make_shared<FakePrinter>(MakePrinterInfo(), transport);
    auto guten = std::make_shared<FakeRawRenderer>();
    printer->AddRenderer(guten);

    auto available = printer->GetAvailableRenderers();
    Check(available.size() == 2, "both renderers are offered");
    Check(printer->IsRendererAvailable(IOPrintRenderer::Native), "Native is available");
    Check(printer->IsRendererAvailable(IOPrintRenderer::GutenPrint),
          "GutenPrint is available when the transport carries raw jobs");

    // The property the whole design rests on: a raw-emitting renderer is
    // useless where the platform cannot carry a raw job, and must not be
    // offered there.
    auto noRaw = std::make_shared<FakeTransport>(/*raw=*/false);
    auto limited = std::make_shared<FakePrinter>(MakePrinterInfo(), noRaw);
    limited->AddRenderer(std::make_shared<FakeRawRenderer>());
    Check(!limited->IsRendererAvailable(IOPrintRenderer::GutenPrint),
          "GutenPrint is withheld when the transport cannot carry raw jobs");
    Check(limited->IsRendererAvailable(IOPrintRenderer::Native),
          "Native still works there");

    // Availability is per printer, not per platform: GutenPrint drives the
    // models it knows and no others.
    auto unknown = std::make_shared<FakePrinter>(MakePrinterInfo("Unknown Model"), transport);
    unknown->AddRenderer(std::make_shared<FakeRawRenderer>());
    Check(!unknown->IsRendererAvailable(IOPrintRenderer::GutenPrint),
          "GutenPrint is withheld for a model it does not recognise");

    // Not installed at all.
    auto absent = std::make_shared<FakeRawRenderer>();
    absent->available = false;
    auto without = std::make_shared<FakePrinter>(MakePrinterInfo(), transport);
    without->AddRenderer(absent);
    Check(!without->IsRendererAvailable(IOPrintRenderer::GutenPrint),
          "GutenPrint is withheld when it is not installed");
}

void TestRendererSelection() {
    std::cout << "\nRenderer selection\n";

    auto transport = std::make_shared<FakeTransport>(true);
    auto printer = std::make_shared<FakePrinter>(MakePrinterInfo(), transport);
    auto guten = std::make_shared<FakeRawRenderer>();
    printer->AddRenderer(guten);

    Check(printer->GetRenderer() == IOPrintRenderer::GutenPrint,
          "Auto prefers GutenPrint when it can drive the printer");

    Check(static_cast<bool>(printer->SetRenderer(IOPrintRenderer::Native)),
          "the native driver can be selected");
    Check(printer->GetRenderer() == IOPrintRenderer::Native,
          "and GetRenderer() reports it");

    Check(static_cast<bool>(printer->SetRenderer(IOPrintRenderer::GutenPrint)),
          "and switched back to GutenPrint");
    Check(printer->GetRenderer() == IOPrintRenderer::GutenPrint, "which sticks");

    // Refused, not silently substituted: a caller that asked for IPP needs to
    // know it is not getting it.
    IODeviceResult refused = printer->SetRenderer(IOPrintRenderer::IPP);
    Check(!static_cast<bool>(refused), "selecting an unavailable renderer fails");
    Check(refused.code == IODeviceResultCode::NotSupported, "with NotSupported");
    Check(printer->GetRenderer() == IOPrintRenderer::GutenPrint,
          "and the previous choice is untouched");

    // Auto falls through to Native when GutenPrint cannot help.
    auto plain = std::make_shared<FakePrinter>(MakePrinterInfo("Unknown Model"), transport);
    plain->AddRenderer(std::make_shared<FakeRawRenderer>());
    Check(plain->GetRenderer() == IOPrintRenderer::Native,
          "Auto falls back to Native for a model GutenPrint does not know");
}

void TestPrintingRoutesThroughTheChosenRenderer() {
    std::cout << "\nPrinting routes through the chosen renderer\n";

    auto transport = std::make_shared<FakeTransport>(true);
    auto printer = std::make_shared<FakePrinter>(MakePrinterInfo(), transport);
    auto guten = std::make_shared<FakeRawRenderer>();
    printer->AddRenderer(guten);
    printer->Connect();

    printer->SetRenderer(IOPrintRenderer::GutenPrint);
    IODeviceResult result = printer->PrintFile("/tmp/page.pdf", "Test page");
    Check(static_cast<bool>(result), "the job is submitted");
    Check(result.backendCode == 4242, "the job id comes back in backendCode");
    Check(guten->renders == 1, "GutenPrint rendered the page");
    Check(transport->lastPayloadWasRaw,
          "and the transport received a raw device stream");

    printer->SetRenderer(IOPrintRenderer::Native);
    printer->PrintFile("/tmp/page.pdf", "Test page");
    Check(guten->renders == 1, "switching to Native stops GutenPrint rendering");
    Check(!transport->lastPayloadWasRaw,
          "and the transport receives a document for the OS driver instead");
    Check(transport->submissions == 2, "both jobs reached the transport");

    // Printing a job that names no source is a caller error, not a blank page.
    IOPrintJob empty;
    Check(!static_cast<bool>(printer->Print(empty)), "an empty job is rejected");

    // And a disconnected printer cannot be printed to.
    printer->Disconnect();
    Check(!static_cast<bool>(printer->PrintFile("/tmp/page.pdf")),
          "printing to a disconnected printer fails");
}

void TestOptionResolution() {
    std::cout << "\nOption resolution\n";

    IOPrinterCapabilities caps;
    caps.paperSizes = {IOPaperSize::A4, IOPaperSize::Letter};
    caps.qualities = {IOPrintQuality::Draft, IOPrintQuality::Normal};
    caps.mediaTypes = {IOMediaType::Plain, IOMediaType::PhotoGlossy};
    caps.resolutionModes = {IOResolutionMode::Draft, IOResolutionMode::Standard,
                            IOResolutionMode::High};
    caps.supportsColor = IOSupport::No;
    caps.supportsDuplex = IOSupport::No;
    caps.supportsCollate = IOSupport::No;
    caps.supportsBorderless = IOSupport::No;
    caps.maxCopies = 10;

    IOPrintOptions requested;
    requested.page.paperSize = IOPaperSize::A3;
    requested.page.borderless = true;
    requested.quality = IOPrintQuality::Photo;
    requested.colorMode = IOPrinterColorMode::Color;
    requested.duplex = IODuplexMode::LongEdge;
    requested.copies = 50;
    requested.collate = true;
    requested.mediaType = IOMediaType::Canvas;
    requested.resolutionMode = IOResolutionMode::PhotoHighest;

    std::vector<std::string> changes;
    IOPrintOptions resolved = ResolvePrintOptions(requested, caps, &changes);

    Check(resolved.page.paperSize == IOPaperSize::A4, "unsupported A3 falls back to A4");
    Check(Mentions(changes, "A3"), "and the change says so by name");
    Check(!resolved.page.borderless, "borderless is dropped when unsupported");
    Check(resolved.colorMode == IOPrinterColorMode::Grayscale,
          "colour falls back to grayscale on a mono printer");
    Check(resolved.duplex == IODuplexMode::None, "duplex is dropped when there is no unit");
    Check(resolved.copies == 10, "copies are clamped to the printer's maximum");
    Check(!resolved.collate, "collate is dropped when unsupported");
    Check(resolved.quality == IOPrintQuality::Normal, "Photo quality falls back to Normal");
    Check(resolved.mediaType == IOMediaType::Plain, "unsupported Canvas media becomes Plain");
    Check(resolved.resolutionMode == IOResolutionMode::High,
          "resolution is clamped to the best the printer offers");
    Check(changes.size() >= 8, "every substitution is reported, not applied silently");
}

void TestMediaConstrainsResolution() {
    std::cout << "\nMedia constrains resolution (GutenPrint priority order)\n";

    IOPrinterCapabilities caps;
    caps.resolutionModes = {IOResolutionMode::Draft, IOResolutionMode::Standard,
                            IOResolutionMode::High, IOResolutionMode::Photo,
                            IOResolutionMode::PhotoHighest};
    caps.mediaTypes = {IOMediaType::Plain, IOMediaType::PhotoGlossy,
                       IOMediaType::Transparency};
    caps.supportsColor = IOSupport::Yes;

    // Media outranks resolution, so plain paper pulls 2880 dpi down even
    // though the printer itself can reach it.
    IOPrintOptions plain;
    plain.mediaType = IOMediaType::Plain;
    plain.resolutionMode = IOResolutionMode::PhotoHighest;
    std::vector<std::string> changes;
    IOPrintOptions resolved = ResolvePrintOptions(plain, caps, &changes);
    Check(resolved.resolutionMode == IOResolutionMode::High,
          "photo resolution on plain paper is clamped to High");
    Check(Mentions(changes, "photo paper"), "and the reason names photo paper");

    // On glossy the same request stands.
    IOPrintOptions glossy;
    glossy.mediaType = IOMediaType::PhotoGlossy;
    glossy.resolutionMode = IOResolutionMode::PhotoHighest;
    changes.clear();
    resolved = ResolvePrintOptions(glossy, caps, &changes);
    Check(resolved.resolutionMode == IOResolutionMode::PhotoHighest,
          "the same request is honoured on glossy paper");
    Check(changes.empty(), "and nothing needed changing");

    // Transparency is the tightest ceiling.
    IOPrintOptions film;
    film.mediaType = IOMediaType::Transparency;
    film.resolutionMode = IOResolutionMode::Photo;
    resolved = ResolvePrintOptions(film, caps, nullptr);
    Check(resolved.resolutionMode == IOResolutionMode::Standard,
          "transparency film is clamped to Standard");

    // Cartridge and inkset follow the media too.
    IOPrintOptions photoInk;
    photoInk.mediaType = IOMediaType::Plain;
    photoInk.cartridge = IOCartridgeType::PhotoBlack;
    photoInk.inkset = IOInkset::Photo;
    changes.clear();
    resolved = ResolvePrintOptions(photoInk, caps, &changes);
    Check(resolved.cartridge == IOCartridgeType::MatteBlack,
          "photo black ink on plain paper becomes matte black");
    Check(resolved.inkset == IOInkset::Auto,
          "and the photo inkset is chosen automatically instead");

    // Monochrome output has no use for a colour inkset.
    IOPrintOptions mono;
    mono.colorMode = IOPrinterColorMode::Monochrome;
    mono.inkset = IOInkset::CMYKcm;
    resolved = ResolvePrintOptions(mono, caps, nullptr);
    Check(resolved.inkset != IOInkset::CMYKcm,
          "a colour inkset is dropped for monochrome output");
}

void TestUnreportedCapabilitiesAreNotRefusals() {
    std::cout << "\nAn unreported capability is not a refusal\n";

    // A printer whose capabilities could not be read must stay usable:
    // an empty list means "did not say", not "supports nothing".
    IOPrinterCapabilities silent;   // every field left at Unknown

    IOPrintOptions requested;
    requested.page.paperSize = IOPaperSize::A3;
    requested.mediaType = IOMediaType::FineArt;
    requested.quality = IOPrintQuality::Photo;
    requested.copies = 5;

    std::vector<std::string> changes;
    IOPrintOptions resolved = ResolvePrintOptions(requested, silent, &changes);

    Check(resolved.page.paperSize == IOPaperSize::A3, "the requested paper size survives");
    Check(resolved.mediaType == IOMediaType::FineArt, "the requested media survives");
    Check(resolved.quality == IOPrintQuality::Photo, "the requested quality survives");
    Check(resolved.copies == 5, "copies are not clamped against an unknown maximum");
    Check(changes.empty(), "and nothing is reported as changed");
}

void TestPaperTable() {
    std::cout << "\nPaper dimensions\n";

    IOPaperDimensions a4 = IOPaperSizeDimensions(IOPaperSize::A4);
    Check(a4.widthHundredthsMM == 21000 && a4.heightHundredthsMM == 29700,
          "A4 is 210 x 297 mm");

    // The exact inch measure, not a rounded millimetre figure, so margins
    // still add up at 1200 dpi.
    IOPaperDimensions letter = IOPaperSizeDimensions(IOPaperSize::Letter);
    Check(letter.widthHundredthsMM == 21590 && letter.heightHundredthsMM == 27940,
          "Letter is 8.5 x 11 in exactly");

    Check(IOPaperSizeToPwgName(IOPaperSize::A4) == "iso_a4_210x297mm",
          "A4 has its PWG self-describing name");
    Check(IOPaperSizeToPwgName(IOPaperSize::Letter) == "na_letter_8.5x11in",
          "Letter has its PWG name");

    IOPageSetup landscape;
    landscape.paperSize = IOPaperSize::A4;
    landscape.orientation = IOPrintOrientation::Landscape;
    IOPaperDimensions rotated = landscape.GetDimensions();
    Check(rotated.widthHundredthsMM == 29700 && rotated.heightHundredthsMM == 21000,
          "landscape swaps the page dimensions");
}

}  // namespace

int main() {
    std::cout << "IODeviceManager printer tests\n";
    std::cout << "=============================\n";

    TestRendererAvailability();
    TestRendererSelection();
    TestPrintingRoutesThroughTheChosenRenderer();
    TestOptionResolution();
    TestMediaConstrainsResolution();
    TestUnreportedCapabilitiesAreNotRefusals();
    TestPaperTable();

    std::cout << "\n";
    if (g_failures == 0) {
        std::cout << "All printer tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " printer test(s) FAILED.\n";
    return 1;
}
