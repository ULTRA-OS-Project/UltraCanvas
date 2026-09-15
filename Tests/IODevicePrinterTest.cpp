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
#include "IODeviceManager/UltraCanvasIODevicePrintDialog.h"

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

// A page target with a synthetic font: every code point is `advance` dots
// wide and the line box is a fixed amount taller than the text, so wrapping
// and pagination become arithmetic a test can predict exactly. The real
// targets measure through GDI or Pango; the layout code under test cannot
// tell the difference, which is the point of the interface.
class FakePageTarget : public IPrintPageTarget {
public:
    struct DrawnImage {
        int width = 0;
        int height = 0;
        IOPrintRect dest;
    };

    FakePageTarget(int width, int height, int dpi = 600, int advance = 10)
        : metrics{width, height, dpi, dpi, 0, 0}, advance(advance) {}

    IOPrintPageMetrics GetMetrics() const override { return metrics; }

    bool DrawImage(const uint8_t* pixels, int width, int height,
                   const IOPrintRect& dest) override {
        (void)pixels;
        images.push_back(DrawnImage{width, height, dest});
        return true;
    }

    bool DrawTextLine(const std::string& utf8, int x, int baselineY,
                      int pixelHeight) override {
        (void)x;
        (void)pixelHeight;
        drawn.push_back(utf8);
        baselines.push_back(baselineY);
        return true;
    }

    int MeasureTextWidth(const std::string& utf8, int pixelHeight) const override {
        (void)pixelHeight;
        return CodePoints(utf8) * advance;
    }

    int GetLineHeight(int pixelHeight) const override { return pixelHeight + 4; }
    int GetAscent(int pixelHeight) const override { return (pixelHeight * 4) / 5; }

    // Counting code points rather than bytes keeps the synthetic font honest
    // about UTF-8: a wrapper that split a multi-byte sequence would measure
    // the halves as wider than the whole and the test would notice.
    static int CodePoints(const std::string& text) {
        int count = 0;
        for (unsigned char c : text) {
            if ((c & 0xC0) != 0x80) ++count;
        }
        return count;
    }

    IOPrintPageMetrics metrics;
    int advance = 10;
    std::vector<DrawnImage> images;
    std::vector<std::string> drawn;
    std::vector<int> baselines;
};

class FakeTransport : public IPrintTransport {
public:
    explicit FakeTransport(bool raw, bool document = true, bool pageSource = false)
        : rawSupported(raw), documentSupported(document),
          pageSourceSupported(pageSource) {}

    std::string GetName() const override { return "Fake"; }
    bool SupportsRaw() const override { return rawSupported; }
    bool SupportsDocument() const override { return documentSupported; }
    bool SupportsPageSource() const override { return pageSourceSupported; }

    IODeviceResult Submit(const IODeviceInfo& printer, const IOPrintPayload& payload,
                          const IOPrintOptions& options, int& outJobId) override {
        (void)printer;
        lastPayloadWasRaw = payload.isRaw;
        lastContentType = payload.contentType;
        lastJobName = payload.jobName;
        lastPageRange = payload.pageRange;
        lastOptions = options;
        ++submissions;
        outJobId = 4242;

        // Stands in for what the Windows transport does with a page source:
        // prepare it against the device, then draw every page. Doing it here
        // rather than just counting the payload means the test exercises the
        // same order of calls the real one makes.
        if (payload.pages) {
            IODeviceResult prepared = payload.pages->Prepare(page);
            if (!prepared.success) return prepared;
            pagesPrinted = payload.pages->GetPageCount();
            for (int i = 0; i < pagesPrinted; ++i) {
                IODeviceResult drawn = payload.pages->DrawPage(i, page);
                if (!drawn.success) return drawn;
            }
        }
        return IODeviceResult::Ok();
    }

    FakePageTarget page{4960, 7016};   // A4 at 600 dpi, near enough

    bool rawSupported = true;
    bool documentSupported = true;
    bool pageSourceSupported = false;
    int pagesPrinted = 0;
    bool lastPayloadWasRaw = false;
    std::string lastContentType;
    std::string lastJobName;
    std::vector<int> lastPageRange;
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

// Stands in for the Windows GDI renderer: Native by kind, but it produces
// pages to be drawn rather than bytes to be sent, so it needs a transport
// that can drive a drawing session.
class FakePageRenderer : public IPrintRenderer {
public:
    IOPrintRenderer GetKind() const override { return IOPrintRenderer::Native; }
    bool IsAvailable() const override { return true; }
    bool ProducesPageSource() const override { return true; }
    bool SupportsPrinter(const IODeviceInfo& printer) const override {
        (void)printer;
        return true;
    }

    IODeviceResult Render(const IOPrintJob& job,
                          const IOPrinterCapabilities& capabilities,
                          IOPrintPayload& payload) override {
        (void)job;
        (void)capabilities;
        payload.pages = std::make_shared<TextPageSource>(text, pixelHeight);
        payload.contentType = "text/plain";
        ++renders;
        return IODeviceResult::Ok();
    }

    std::string text = "hello world";
    int pixelHeight = 40;
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

// The Windows spooler's shape: it takes a device-native stream as a RAW job,
// but will not take a PDF and work out what to do with it the way CUPS's
// filter chain does. A document needs drawing to a printer DC first, so until
// that renderer exists the Native renderer must not be offered there - and the
// caller must learn that from GetAvailableRenderers() rather than from a job
// that disappears.
void TestTransportThatCannotTakeDocuments() {
    std::cout << "\nA transport that takes raw jobs but not documents\n";

    auto spooler = std::make_shared<FakeTransport>(/*raw=*/true, /*document=*/false);
    auto printer = std::make_shared<FakePrinter>(MakePrinterInfo(), spooler);
    auto guten = std::make_shared<FakeRawRenderer>();
    printer->AddRenderer(guten);

    Check(!printer->IsRendererAvailable(IOPrintRenderer::Native),
          "Native is withheld when the transport cannot process a document");
    Check(printer->IsRendererAvailable(IOPrintRenderer::GutenPrint),
          "GutenPrint is still offered, because it emits a raw stream");
    Check(printer->GetRenderer() == IOPrintRenderer::GutenPrint,
          "and Auto resolves to it");

    printer->Connect();
    IODeviceResult result = printer->PrintFile("/tmp/page.pdf");
    Check(static_cast<bool>(result), "printing through GutenPrint works there");
    Check(spooler->lastPayloadWasRaw, "and what reached the spooler was raw");

    // With no raw renderer at all, such a transport can print nothing, and
    // must say so rather than silently dropping the job.
    auto stranded = std::make_shared<FakePrinter>(MakePrinterInfo(), spooler);
    stranded->Connect();
    Check(stranded->GetAvailableRenderers().empty(),
          "a printer with only a document renderer has none available there");
    IODeviceResult refused = stranded->PrintFile("/tmp/page.pdf");
    Check(!static_cast<bool>(refused), "and printing fails rather than silently dropping");
    Check(refused.code == IODeviceResultCode::NotSupported, "with NotSupported");
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


// ===== PAGE DRAWING =====

void TestPageSourceRendererMatching() {
    std::cout << "\nPage-source renderer matching\n";

    // Windows: the spooler takes raw jobs and can drive a drawing session,
    // but it will not process a document. That is the exact shape.
    auto windowsLike = std::make_shared<FakeTransport>(
        /*raw=*/true, /*document=*/false, /*pageSource=*/true);
    auto printer = std::make_shared<FakePrinter>(MakePrinterInfo(), windowsLike);
    printer->AddRenderer(std::make_shared<FakePageRenderer>());

    Check(printer->IsRendererAvailable(IOPrintRenderer::Native),
          "Native is offered when the transport can drive a page source");

    // The same renderer against CUPS-like capabilities, which have no use for
    // a page source, must not be offered: CUPS hands the document to its
    // filter chain instead.
    auto cupsLike = std::make_shared<FakeTransport>(
        /*raw=*/true, /*document=*/true, /*pageSource=*/false);
    auto other = std::make_shared<FakePrinter>(MakePrinterInfo(), cupsLike);
    other->AddRenderer(std::make_shared<FakePageRenderer>());
    Check(!other->IsRendererAvailable(IOPrintRenderer::Native),
          "a page-source renderer is withheld where nothing can drive it");

    // And the pass-through Native renderer is still the right one there,
    // which is what AddRenderer replacing by kind is for.
    auto passThrough = std::make_shared<FakePrinter>(MakePrinterInfo(), cupsLike);
    Check(passThrough->IsRendererAvailable(IOPrintRenderer::Native),
          "the pass-through Native renderer still serves a document transport");
}

void TestFitPreservingAspect() {
    std::cout << "\nImage fitting\n";

    const IOPrintRect page{0, 0, 1000, 2000};

    // Wider than the page's aspect: width-limited, centred vertically.
    IOPrintRect wide = FitPreservingAspect(200, 100, page);
    Check(wide.width == 1000 && wide.height == 500, "a wide image fills the width");
    Check(wide.x == 0 && wide.y == 750, "and is centred down the page");

    // Taller: height-limited, centred horizontally.
    IOPrintRect tall = FitPreservingAspect(100, 400, page);
    Check(tall.height == 2000 && tall.width == 500, "a tall image fills the height");
    Check(tall.y == 0 && tall.x == 250, "and is centred across the page");

    // Small sources scale *up*. A screenshot must not print stamp-sized just
    // because its pixel count is small - print scales to paper, not to pixels.
    IOPrintRect small = FitPreservingAspect(10, 20, page);
    Check(small.width == 1000 && small.height == 2000,
          "a small image is scaled up to the page, not left tiny");

    Check(FitPreservingAspect(0, 100, page).IsEmpty(), "a zero-width source fits nothing");
    Check(FitPreservingAspect(100, 100, IOPrintRect{0, 0, 0, 500}).IsEmpty(),
          "a zero-width page fits nothing");
}

void TestImagePageSourcePlacement() {
    std::cout << "\nImage page source\n";

    FakePageTarget target(1000, 2000);
    std::vector<uint8_t> pixels(4 * 4 * 4, 0xFF);   // 4x4 opaque white

    ImagePageSource source(pixels.data(), 4, 4);
    Check(source.Prepare(target).success, "a square image prepares against the page");
    Check(source.GetPageCount() == 1, "an image is one page");

    const IOPrintRect placement = source.GetPlacement();
    Check(placement.width == 1000 && placement.height == 1000,
          "a square image is width-limited on a tall page");
    Check(placement.y == 500, "and centred vertically");

    Check(source.DrawPage(0, target).success, "page 0 draws");
    Check(target.images.size() == 1, "exactly one image reached the device");
    Check(target.images[0].width == 4 && target.images[0].height == 4,
          "at its own pixel dimensions, scaled by the device");
    Check(!source.DrawPage(1, target).success, "there is no page 1");

    // A source that has not met a device yet cannot know where anything goes.
    ImagePageSource unprepared(pixels.data(), 4, 4);
    IODeviceResult early = unprepared.DrawPage(0, target);
    Check(!early.success && early.code == IODeviceResultCode::InvalidState,
          "drawing before Prepare() is refused, not guessed at");
}

void TestTextPagination() {
    std::cout << "\nText pagination\n";

    // 10 dots per code point, 40-dot text in a 44-dot line box. A 200-dot
    // wide page holds 20 characters; a 440-dot tall page holds 10 lines.
    FakePageTarget target(200, 440);

    TextPageSource source("aaaa bbbb cccc dddd eeee ffff", 40);
    Check(source.Prepare(target).success, "the text prepares against the page");
    Check(source.GetLinesPerPage() == 10, "ten line boxes fit the page height");

    const std::vector<std::string>& lines = source.GetLines();
    Check(!lines.empty(), "the text wrapped into lines");
    for (const std::string& line : lines) {
        Check(FakePageTarget::CodePoints(line) * 10 <= 200,
              "every line fits the printable width: '" + line + "'");
    }

    // Blank lines survive: they are paragraph separation, not noise.
    TextPageSource paragraphs("one\n\ntwo", 40);
    Check(paragraphs.Prepare(target).success, "paragraphs prepare");
    Check(paragraphs.GetLines().size() == 3, "a blank line is kept as a line");
    Check(paragraphs.GetLines()[1].empty(), "and it is the empty one");

    // The same document is a different number of pages on a different device
    // - which is why pagination cannot happen before the printer is known.
    std::string many;
    for (int i = 0; i < 25; ++i) many += "line\n";
    TextPageSource tall(many, 40);
    Check(tall.Prepare(target).success, "a long document prepares");
    const int onTallPage = tall.GetPageCount();

    FakePageTarget shortPage(200, 132);   // three line boxes
    TextPageSource same(many, 40);
    Check(same.Prepare(shortPage).success, "and prepares against a shorter page");
    Check(same.GetPageCount() > onTallPage,
          "a shorter page needs more pages for the same text");

    // Empty in, nothing out - rather than one blank sheet.
    TextPageSource empty("", 40);
    Check(empty.Prepare(target).success, "empty text prepares");
    Check(empty.GetPageCount() == 0, "empty text is zero pages, not one blank one");
}

void TestTextDrawsEveryLineOnItsPage() {
    std::cout << "\nText drawing\n";

    FakePageTarget target(200, 132);   // three line boxes of 44 dots
    TextPageSource source("aa\nbb\ncc\ndd", 40);
    Check(source.Prepare(target).success, "four short lines prepare");
    Check(source.GetLinesPerPage() == 3, "three fit a page");
    Check(source.GetPageCount() == 2, "so four lines need two pages");

    Check(source.DrawPage(0, target).success, "page 0 draws");
    Check(target.drawn.size() == 3, "three lines land on the first page");
    Check(target.drawn[0] == "aa" && target.drawn[2] == "cc", "in order");
    Check(target.baselines[0] == 32, "the first baseline is one ascent down");
    Check(target.baselines[1] == 32 + 44, "and each next is one line box lower");

    target.drawn.clear();
    target.baselines.clear();
    Check(source.DrawPage(1, target).success, "page 1 draws");
    Check(target.drawn.size() == 1 && target.drawn[0] == "dd",
          "the remainder lands on the second page");
    Check(target.baselines[0] == 32,
          "and starts at the top of it, not where the last page left off");
}

void TestPrintingDrivesThePageSource() {
    std::cout << "\nPrinting through a page source\n";

    auto transport = std::make_shared<FakeTransport>(
        /*raw=*/true, /*document=*/false, /*pageSource=*/true);
    auto printer = std::make_shared<FakePrinter>(MakePrinterInfo(), transport);
    auto renderer = std::make_shared<FakePageRenderer>();
    renderer->text = "one\ntwo\nthree";
    printer->AddRenderer(renderer);
    Check(printer->Connect().success, "the printer connects");

    IOPrintJob job;
    job.data = {'x'};              // the fake renderer ignores it
    job.jobName = "pages";
    IODeviceResult printed = printer->Print(job);

    Check(printed.success, "the job prints");
    Check(renderer->renders == 1, "the page renderer ran once");
    Check(transport->submissions == 1, "and the transport took it once");
    Check(transport->pagesPrinted >= 1, "at least one page was drawn");
    Check(!transport->page.drawn.empty(), "text actually reached the device");
    Check(transport->page.drawn[0] == "one", "starting with the first line");
}


void TestJobNameReachesTheTransport() {
    std::cout << "\nJob naming\n";

    auto transport = std::make_shared<FakeTransport>(/*raw=*/true);
    auto printer = std::make_shared<FakePrinter>(MakePrinterInfo(), transport);
    Check(printer->Connect().success, "the printer connects");

    IOPrintJob job;
    job.filePath = "/tmp/quarterly-report.pdf";
    job.jobName = "Quarterly report";
    Check(printer->Print(job).success, "a named job prints");
    Check(transport->lastJobName == "Quarterly report",
          "the queue is told what the caller called it");

    // An unnamed job still gets something a queue can display, and it is the
    // file rather than the printer's own name - a queue of six documents
    // titled after the printer tells the user nothing.
    IOPrintJob unnamed;
    unnamed.filePath = "/tmp/invoice.pdf";
    Check(printer->Print(unnamed).success, "an unnamed job prints");
    Check(transport->lastJobName == "/tmp/invoice.pdf",
          "and falls back to the file, not to the printer");
}

// ===== WHAT A PRINT DIALOG CHOSE =====

// A print dialog is the one place a user states every one of these settings
// at once, and until this slice each platform collected them and then printed
// by itself without them: Linux read the GTK page setup and the settings,
// used neither, and shelled out to `lpr`; Windows and macOS never showed a
// print dialog at all. The tests below cover the path those answers now take.

void TestPaperSizeRecognition() {
    std::cout << "\n-- Paper recognised by its measurements --\n";

    Check(IOPaperSizeFromDimensions(21000, 29700) == IOPaperSize::A4,
          "210 x 297 mm is A4");
    Check(IOPaperSizeFromDimensions(21590, 27940) == IOPaperSize::Letter,
          "8.5 x 11 in is Letter");

    // A driver quoting its own rounded figures still has to land on the same
    // sheet: this is why the recogniser has a tolerance at all.
    Check(IOPaperSizeFromDimensions(21050, 29650) == IOPaperSize::A4,
          "half a millimetre off is still A4");

    // ISO B4 is 250 x 353; the DMPAPER_B4 that Windows drivers report is JIS
    // B4 at 257 x 364. Eleven millimetres apart, and not the same paper.
    Check(IOPaperSizeFromDimensions(25700, 36400) != IOPaperSize::B4,
          "JIS B4 is not reported as ISO B4");
    Check(IOPaperSizeFromDimensions(25000, 35300) == IOPaperSize::B4,
          "ISO B4 is");

    Check(IOPaperSizeFromDimensions(0, 0) == IOPaperSize::Unknown,
          "a size with no measurements is Unknown, not a guess");
    Check(IOPaperSizeFromDimensions(12345, 67890) == IOPaperSize::Unknown,
          "and so is a sheet nothing in the table matches");
}

void TestPageRangeFormatting() {
    std::cout << "\n-- Page ranges --\n";

    Check(IOFormatPageRanges({}).empty(),
          "no range formats as nothing, which every consumer reads as 'all'");
    Check(IOFormatPageRanges({4}) == "4", "a single page is itself");
    Check(IOFormatPageRanges({1, 2, 3}) == "1-3", "a run becomes a range");
    Check(IOFormatPageRanges({1, 3, 5}) == "1,3,5", "gaps stay separate");
    Check(IOFormatPageRanges({1, 2, 3, 7, 8}) == "1-3,7-8",
          "runs and gaps together");

    // The list comes from a dialog, so it arrives in whatever order the user
    // typed, and a page named twice is one page.
    Check(IOFormatPageRanges({3, 1, 2, 2}) == "1-3",
          "out of order and duplicated still formats as one run");
    Check(IOFormatPageRanges({0, -4, 2}) == "2",
          "a page number below 1 is dropped, not clamped onto page 1");
}

void TestPageSelection() {
    std::cout << "\n-- Selecting pages from a paginated document --\n";

    Check(IOSelectPages({}, 3) == std::vector<int>({0, 1, 2}),
          "no range selects every page");
    Check(IOSelectPages({2, 3}, 5) == std::vector<int>({1, 2}),
          "a range selects those pages, 0-based");
    Check(IOSelectPages({3, 1}, 5) == std::vector<int>({0, 2}),
          "and comes back in order whatever order it went in");

    // A dialog cannot know how long the document is - it has not been
    // paginated yet - so a range wider than the document is ordinary.
    Check(IOSelectPages({1, 2, 99}, 2) == std::vector<int>({0, 1}),
          "pages past the end are dropped rather than refused");
    Check(IOSelectPages({7, 8}, 3).empty(),
          "a range that selects nothing comes back empty");
    Check(IOSelectPages({}, 0).empty(), "an empty document selects nothing");
}

void TestMatchingADialogsPrinterName() {
    std::cout << "\n-- Matching the dialog's printer to a device --\n";

    auto transport = std::make_shared<FakeTransport>(/*raw=*/true);

    auto make = [&](const std::string& id, const std::string& name,
                    const std::string& queue) {
        IODeviceInfo info;
        info.deviceId = id;
        info.name = name;
        info.connectionPath = queue;
        info.category = IODeviceCategory::Printer;
        return std::make_shared<FakePrinter>(info, transport);
    };

    // The display name of one printer is the queue name of another. A dialog
    // returns queue names, so the queue must win - otherwise picking "Office"
    // in the dialog prints on the machine down the hall.
    auto byName = make("a", "Office", "HP_LaserJet_4000");
    auto byQueue = make("b", "HP LaserJet 4000 (Reception)", "Office");

    std::vector<IODevicePtr> devices = {byName, byQueue};

    Check(MatchPrinterByName(devices, "Office") == byQueue,
          "the queue name wins over another device's display name");
    Check(MatchPrinterByName(devices, "HP_LaserJet_4000") == byName,
          "and a queue name finds its own device");
    Check(MatchPrinterByName(devices, "HP LaserJet 4000 (Reception)") == byQueue,
          "a display name matches when no queue name does");

    // Windows printer names are not case-sensitive.
    Check(MatchPrinterByName(devices, "hp_laserjet_4000") == byName,
          "case is a last resort, but it is one");

    Check(MatchPrinterByName(devices, "Nothing Like This") == nullptr,
          "an unknown name matches nothing rather than the first printer");
    Check(MatchPrinterByName(devices, "") == nullptr,
          "and neither does an empty one");

    // The registry holds every category, so this has to be safe to hand the
    // whole list.
    IODeviceInfo cameraInfo;
    cameraInfo.deviceId = "c";
    cameraInfo.name = "Office";
    cameraInfo.category = IODeviceCategory::Camera;
    Check(MatchPrinterByName({std::make_shared<FakePrinter>(cameraInfo, transport)},
                             "Office") == nullptr,
          "a device that is not a printer is not matched");
}

void TestBuildingAJobFromADialogAnswer() {
    std::cout << "\n-- Building the job the dialog described --\n";

    IOPrintDialogChoice chosen;
    chosen.accepted = true;
    chosen.printerName = "Office";
    chosen.options.copies = 3;
    chosen.options.collate = false;
    chosen.options.duplex = IODuplexMode::LongEdge;
    chosen.options.page.paperSize = IOPaperSize::Legal;
    chosen.options.page.orientation = IOPrintOrientation::Landscape;
    chosen.pageRange = {2, 3};

    const IOPrintJob job = MakeTextPrintJob(chosen, "notes.txt", "hello");

    Check(job.jobName == "notes.txt", "the document's name titles the job");
    Check(std::string(job.data.begin(), job.data.end()) == "hello",
          "the text is the job's data");

    // Both renderers that can take this job read the MIME type first and the
    // file extension second, and an in-memory document has no extension to
    // read - so saying nothing here is how it gets refused.
    Check(job.mimeType == "text/plain",
          "the type is stated, because there is no file name to infer it from");

    Check(job.options.copies == 3 && !job.options.collate,
          "copies and collation are the user's, not defaults");
    Check(job.options.duplex == IODuplexMode::LongEdge, "so is duplex");
    Check(job.options.page.paperSize == IOPaperSize::Legal &&
              job.options.page.orientation == IOPrintOrientation::Landscape,
          "and so are the paper size and orientation");
    Check(job.pageRange == std::vector<int>({2, 3}), "and the page range");

    const IOPrintJob unnamed = MakeTextPrintJob(chosen, "", "hello");
    Check(!unnamed.jobName.empty(),
          "a document with no name still reaches the queue with one");
}

void TestPageRangeReachesTheTransport() {
    std::cout << "\n-- The page range reaches the transport --\n";

    IODeviceInfo info;
    info.deviceId = "printer:1";
    info.name = "Office";
    info.category = IODeviceCategory::Printer;

    auto transport = std::make_shared<FakeTransport>(/*raw=*/true);
    auto printer = std::make_shared<FakePrinter>(info, transport);
    Check(printer->Connect().success, "the printer connects");

    IOPrintJob job;
    job.jobName = "report.txt";
    job.data = {'h', 'i'};
    job.mimeType = "text/plain";
    job.pageRange = {2, 4};

    Check(printer->Print(job).success, "a job with a page range prints");

    // IOPrintJob has carried a pageRange since this module was written, and
    // nothing read it: the payload had nowhere to put it, and a transport only
    // ever sees the payload. So a range the user picked in a print dialog was
    // dropped between the dialog and the queue.
    Check(transport->lastPageRange == std::vector<int>({2, 4}),
          "and the range arrives with it rather than being dropped");

    IOPrintJob everything;
    everything.jobName = "report.txt";
    everything.data = {'h', 'i'};
    everything.mimeType = "text/plain";
    Check(printer->Print(everything).success, "a job with no range prints");
    Check(transport->lastPageRange.empty(),
          "and carries no range, which means every page");
}

}  // namespace

int main() {
    std::cout << "IODeviceManager printer tests\n";
    std::cout << "=============================\n";

    TestRendererAvailability();
    TestTransportThatCannotTakeDocuments();
    TestRendererSelection();
    TestPrintingRoutesThroughTheChosenRenderer();
    TestOptionResolution();
    TestMediaConstrainsResolution();
    TestUnreportedCapabilitiesAreNotRefusals();
    TestPaperTable();
    TestPageSourceRendererMatching();
    TestFitPreservingAspect();
    TestImagePageSourcePlacement();
    TestTextPagination();
    TestTextDrawsEveryLineOnItsPage();
    TestPrintingDrivesThePageSource();
    TestJobNameReachesTheTransport();
    TestPaperSizeRecognition();
    TestPageRangeFormatting();
    TestPageSelection();
    TestMatchingADialogsPrinterName();
    TestBuildingAJobFromADialogAnswer();
    TestPageRangeReachesTheTransport();

    std::cout << "\n";
    if (g_failures == 0) {
        std::cout << "All printer tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " printer test(s) FAILED.\n";
    return 1;
}
