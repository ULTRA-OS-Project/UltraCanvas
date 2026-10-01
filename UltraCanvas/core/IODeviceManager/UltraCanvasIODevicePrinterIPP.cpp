// core/IODeviceManager/UltraCanvasIODevicePrinterIPP.cpp
// The IPP printer backend: driverless printing over the network.
//
// An IPP Everywhere, AirPrint or Mopria printer needs no driver, because it
// describes itself - what it takes, what paper, which sides - and renders the
// document itself. So this backend is one file for every platform, like the
// eSCL scanner backend it mirrors: IPP is HTTP and a binary encoding, and
// nothing in it belongs under OS/.
//
// It is what makes a network printer usable where there is no print system
// to go through - Windows has no IPP client an application can drive, and a
// Linux or macOS build may have no CUPS - and it reaches a printer CUPS does
// not know about. Where CUPS is present and already offers a printer, the
// printer is left to CUPS rather than listed twice (see
// IppCupsQueueReachesPrinter).
//
// The protocol arithmetic lives next door in ...IPPProtocol.cpp and the page
// format in ...PwgRaster.cpp, so both can be tested without a network; this
// file is the part that needs one.
//
// The Windows spooler backend borrows one piece of it: when a queue's driver
// keeps its ink levels to itself, QueryIppSupplyLevels asks the printer
// behind the queue directly.
// Version: 0.2.0
// Author: UltraCanvas Framework / ULTRA OS

#include "IODeviceManager/UltraCanvasIODevicePrinterIPP.h"

#if defined(ULTRACANVAS_HAS_NET)

#include "IODeviceManager/UltraCanvasIODeviceManager.h"
#include "IODeviceManager/UltraCanvasIODevicePrinterIPPProtocol.h"
#include "IODeviceManager/UltraCanvasIODevicePrinterJobSource.h"
#include "IODeviceManager/UltraCanvasIODevicePrinterPwgRaster.h"
#include "IODeviceManager/UltraCanvasIODevicePrinterRaster.h"
#include "IODeviceManager/UltraCanvasIODevicePrinterRasterTarget.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraNet/UltraNetHttp.h"
#include "UltraNet/UltraNetPlugins.h"

#if defined(ULTRACANVAS_HAS_CUPS) && (defined(__linux__) || defined(__APPLE__))
#include <cups/cups.h>
#define ULTRACANVAS_IPP_DEFERS_TO_CUPS 1
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

namespace UltraCanvas {

namespace {

// Describing a printer should be instant; a job can take a while to upload,
// and some printers hold the connection until they have rendered it.
constexpr int kMetadataTimeoutMs = 10000;
constexpr int kPrintTimeoutMs = 300000;

// How long a job waits for a printer that says it is busy. A printer that
// holds one job at a time answers server-error-busy until the job in hand is
// on paper, and printing two documents back to back is ordinary, so the job
// waits and asks again - which is what CUPS's own IPP backend does - rather
// than failing on the second document. Bounded, because Print() is a
// blocking call and a printer that stays busy for minutes is stuck, not busy.
constexpr int kBusyWaitMs = 180000;

uint32_t NextRequestId() {
    static std::atomic<uint32_t> next{1};
    return next.fetch_add(1);
}

// Who the job is for, as the printer's queue will show it.
std::string RequestingUserName() {
    for (const char* variable : {"USER", "USERNAME", "LOGNAME"}) {
        const char* value = std::getenv(variable);
        if (value && *value) return value;
    }
    return "ultracanvas";
}

// ============================================================================
// ONE ROUND TRIP
// ============================================================================

// Printers that answer IPP 2.0 with "version not supported" are asked again
// in 1.1, and remembered, so a 1.1-only printer costs one refused request per
// process rather than one per call - which matters for Print-Job, where the
// refused request carried the whole document.
std::mutex& VersionMutex() {
    static std::mutex mutex;
    return mutex;
}

std::map<std::string, uint8_t>& MinorVersionFor() {
    static std::map<std::string, uint8_t> versions;
    return versions;
}

const char* OperationName(uint16_t operation) {
    switch (static_cast<IppOperation>(operation)) {
        case IppOperation::PrintJob: return "Print-Job";
        case IppOperation::ValidateJob: return "Validate-Job";
        case IppOperation::CancelJob: return "Cancel-Job";
        case IppOperation::GetJobAttributes: return "Get-Job-Attributes";
        case IppOperation::GetJobs: return "Get-Jobs";
        case IppOperation::GetPrinterAttributes: return "Get-Printer-Attributes";
    }
    return "the request";
}

// POSTs `request`, with `document` after it when there is one, and reads the
// reply. Every failure comes back as a result that says which layer failed:
// the network, HTTP, the encoding, or the printer's own answer.
IODeviceResult SendIpp(const std::string& printerUri, IppMessage request,
                       const std::vector<uint8_t>* document, int timeoutMs,
                       IppMessage& response, const IODeviceId& deviceId) {
    const std::string url = IppHttpUrlFor(printerUri);
    if (url.empty()) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                     "'" + printerUri + "' is not an IPP address", deviceId);
    }

    {
        std::lock_guard<std::mutex> lock(VersionMutex());
        auto known = MinorVersionFor().find(printerUri);
        if (known != MinorVersionFor().end() && known->second == 1) {
            request.versionMajor = 1;
            request.versionMinor = 1;
        }
    }

    for (int attempt = 0; attempt < 2; ++attempt) {
        std::vector<uint8_t> body = EncodeIppMessage(request);
        if (document) body.insert(body.end(), document->begin(), document->end());

        UltraNetHttpOptions options;
        options.headers.Set("Content-Type", "application/ipp");
        options.timeoutMs = timeoutMs;
        options.connectTimeoutMs = 5000;

        UltraNetResponse http;
        const UltraNetResult sent = UltraNet_HttpPost(url, body, http, options);

        if (!sent.success && http.statusCode == 0) {
            std::string message = "Could not reach the printer at " + url;
            if (!sent.message.empty()) message += ": " + sent.message;
            if (url.rfind("https://", 0) == 0) {
                // Nearly every printer's certificate is self-signed, and TLS
                // verification stays on; say so, because the failure reads
                // like a network fault otherwise.
                message += ". If the printer uses a self-signed certificate, "
                           "reach it through its ipp:// address instead";
            }
            return IODeviceResult::BackendError(IODeviceResultCode::ConnectionFailed, message,
                                                0, deviceId);
        }
        if (http.statusCode == 401) {
            return IODeviceResult::BackendError(
                IODeviceResultCode::AccessDenied,
                "The printer asks for a user name and password, which this backend "
                "does not send yet",
                http.statusCode, deviceId);
        }
        if (!http.IsSuccess()) {
            return IODeviceResult::BackendError(
                IODeviceResultCode::BackendError,
                "The printer answered HTTP " + std::to_string(http.statusCode) + " to " +
                    OperationName(request.code),
                http.statusCode, deviceId);
        }

        std::string error;
        if (!DecodeIppMessage(http.body.data(), http.body.size(), response, nullptr, &error)) {
            return IODeviceResult::BackendError(
                IODeviceResultCode::CommunicationError,
                "The printer's reply to " + std::string(OperationName(request.code)) +
                    " is not IPP: " + error,
                0, deviceId);
        }

        if (response.code == kIppStatusVersionNotSupported && request.versionMajor == 2 &&
            attempt == 0) {
            request.versionMajor = 1;
            request.versionMinor = 1;
            std::lock_guard<std::mutex> lock(VersionMutex());
            MinorVersionFor()[printerUri] = 1;
            continue;
        }
        break;
    }

    if (!IppStatusSucceeded(response.code)) {
        std::string message = std::string("The printer refused ") + OperationName(request.code) +
                              ": " + IppStatusToString(response.code);
        if (const IppAttribute* detail =
                response.Find(IppTag::OperationGroup, "status-message")) {
            if (!detail->String().empty()) message += " (" + detail->String() + ")";
        }
        return IODeviceResult::BackendError(IppStatusToResultCode(response.code), message,
                                            response.code, deviceId);
    }
    return IODeviceResult::Ok(deviceId);
}

// ============================================================================
// WHAT THE DEVICE LEARNS AND ITS RENDERER NEEDS
// ============================================================================

// Written by the device when it describes the printer, read by its renderer
// when it plans a job. Both happen under the device's mutex - PrinterDevice
// holds it across Connect, RefreshCapabilities and Print - so there is no
// second lock here.
//
// Each device has its own: unlike the GutenPrint renderer, which is shared
// between printers and so must not remember one, this renderer exists to
// draw for exactly one printer.
struct IppPrinterFacts {
    IppDocumentSupport documents;
    bool described = false;
};

// ============================================================================
// DRAWING PWG RASTER
// ============================================================================

IODeviceResult DrawPwgRaster(const IOPrintJob& job, const IppDocumentSupport& documents,
                             IOPrintPayload& payload) {
    IPrintPageSourcePtr pages;
    std::string sourceType;
    IODeviceResult built = MakePageSourceForJob(job, pages, sourceType);
    if (!built.success) return built;

    const IOPrintOptions& options = job.options;
    const std::string rasterType = ChoosePwgRasterType(documents.rasterTypes, options.colorMode);
    const IOPwgColorSpace space =
        rasterType == "sgray_8" ? IOPwgColorSpace::SGray : IOPwgColorSpace::SRGB;
    const IOCupsColorSpace conversion =
        space == IOPwgColorSpace::SGray ? IOCupsColorSpace::Gray : IOCupsColorSpace::RGB;
    const int channels = space == IOPwgColorSpace::SGray ? 1 : 3;

    const IOResolution dpi = ChoosePwgRasterResolution(
        documents.rasterResolutions, options.quality,
        options.resolutionMode == IOResolutionMode::Custom ? options.customResolution
                                                            : IOResolution());
    if (!dpi.IsValid()) {
        return IODeviceResult::Error(IODeviceResultCode::NotSupported,
                                     "The printer names no resolution for PWG raster");
    }

    // The sheet as it goes through the printer: portrait, whatever is drawn
    // on it. Orientation is applied to the pixels below.
    const bool custom = options.page.paperSize == IOPaperSize::Custom;
    const IOPaperDimensions sheet =
        custom ? options.page.customSize : IOPaperSizeDimensions(options.page.paperSize);
    if (!sheet.IsValid()) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                     "The job names no usable paper size");
    }

    // Hundredths of a millimetre to points and to pixels; 2540 of them are
    // an inch.
    const int widthPoints = (sheet.widthHundredthsMM * 72 + 1270) / 2540;
    const int heightPoints = (sheet.heightHundredthsMM * 72 + 1270) / 2540;
    const int widthPixels =
        static_cast<int>(static_cast<long long>(sheet.widthHundredthsMM) * dpi.dpiX / 2540);
    const int heightPixels =
        static_cast<int>(static_cast<long long>(sheet.heightHundredthsMM) * dpi.dpiY / 2540);

    // The page is drawn inside the margins and placed on the sheet
    // afterwards. PWG raster is the whole sheet and the printer prints it as
    // it is, so without this the first letters of every line would fall in
    // its unprintable border. (GutenPrint's filter applies the margin itself;
    // a driverless printer does not.)
    const IOPageMargins margins = IppDrawingMargins(documents, options.page);
    auto dots = [](int hundredthsMM, int perInch) {
        return static_cast<int>(static_cast<long long>(hundredthsMM) * perInch / 2540);
    };
    const int left = dots(margins.leftHundredthsMM, dpi.dpiX);
    const int top = dots(margins.topHundredthsMM, dpi.dpiY);
    const int printableWidth = widthPixels - left - dots(margins.rightHundredthsMM, dpi.dpiX);
    const int printableHeight = heightPixels - top - dots(margins.bottomHundredthsMM, dpi.dpiY);
    if (printableWidth <= 0 || printableHeight <= 0) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                     "The margins leave nothing of the page to print on");
    }
    const bool hasMargins = left > 0 || top > 0 || printableWidth < widthPixels ||
                            printableHeight < heightPixels;

    // Landscape is drawn on a landscape page and turned onto the portrait
    // sheet: a quarter turn counter-clockwise for landscape, clockwise for
    // reverse landscape - the rotations IPP's orientation-requested names.
    int quarterTurns = 0;
    bool sideways = false;
    switch (options.page.orientation) {
        case IOPrintOrientation::Portrait: break;
        case IOPrintOrientation::Landscape: quarterTurns = 1; sideways = true; break;
        case IOPrintOrientation::ReverseLandscape: quarterTurns = 3; sideways = true; break;
        case IOPrintOrientation::ReversePortrait: quarterTurns = 2; break;
    }
    const int drawWidth = sideways ? printableHeight : printableWidth;
    const int drawHeight = sideways ? printableWidth : printableHeight;

    RasterPageTarget target(drawWidth, drawHeight, sideways ? dpi.dpiY : dpi.dpiX,
                            sideways ? dpi.dpiX : dpi.dpiY);
    if (!target.IsValid()) {
        return IODeviceResult::Error(
            IODeviceResultCode::BackendError,
            "Could not create a page to draw on; this build has no rendering backend");
    }

    target.BeginPage();
    IODeviceResult prepared = pages->Prepare(target);
    if (!prepared.success) return prepared;

    const int pageCount = pages->GetPageCount();
    const std::vector<int> selected = IOSelectPages(job.pageRange, pageCount);
    if (selected.empty()) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                     pageCount <= 0 ? "The document has no pages to print"
                                                    : "The selected pages are not in this document");
    }

    IOPwgRasterPage header;
    header.widthPixels = widthPixels;
    header.heightPixels = heightPixels;
    header.dpiX = dpi.dpiX;
    header.dpiY = dpi.dpiY;
    header.pageWidthPoints = widthPoints;
    header.pageHeightPoints = heightPoints;
    header.pageSizeName = custom ? std::string() : IOPaperSizeToPwgName(options.page.paperSize);
    header.colorSpace = space;
    header.duplex = options.duplex != IODuplexMode::None;
    header.tumble = options.duplex == IODuplexMode::ShortEdge;
    header.totalPageCount = static_cast<int>(selected.size());
    switch (options.quality) {
        case IOPrintQuality::Draft: header.printQuality = 3; break;
        case IOPrintQuality::Normal: header.printQuality = 4; break;
        case IOPrintQuality::High:
        case IOPrintQuality::Photo: header.printQuality = 5; break;
    }

    const IOPwgBackSide backSide =
        IOPwgBackSideTransform(documents.rasterSheetBack, header.tumble);

    std::vector<uint8_t> stream;
    AppendPwgRasterSync(stream);

    for (size_t i = 0; i < selected.size(); ++i) {
        target.BeginPage();
        IODeviceResult drawn = pages->DrawPage(selected[i], target);
        if (!drawn.success) return drawn;

        // The page in the raster's colour space, composited onto white - the
        // same conversion the GutenPrint path makes, so a transparent PNG
        // prints its transparent parts as paper there and here alike.
        std::vector<uint8_t> pixels;
        pixels.reserve(static_cast<size_t>(drawWidth) * static_cast<size_t>(drawHeight) *
                       static_cast<size_t>(channels));
        bool rowsOk = true;
        const bool readBack = target.ForEachRow([&](const uint8_t* rgba, int width) {
            rowsOk = WriteCupsRasterRow(rgba, width, conversion, pixels);
            return rowsOk;
        });
        if (!rowsOk || !readBack) {
            return IODeviceResult::Error(IODeviceResultCode::BackendError,
                                         "Could not read the drawn page back");
        }

        // Turned onto the sheet's orientation, then placed inside the
        // margins, then - for the back of a sheet - mirrored whole. The
        // mirror comes last because it is about the physical sheet, margins
        // included, not about what is drawn on it.
        int width = drawWidth;
        int height = drawHeight;
        if (quarterTurns != 0) {
            pixels = IOPwgTransformPixels(pixels, width, height, channels, quarterTurns,
                                          false, false);
        }
        if (hasMargins) {
            pixels = IOPwgPlaceOnSheet(pixels, width, height, channels, widthPixels,
                                       heightPixels, left, top);
            width = widthPixels;
            height = heightPixels;
        }

        // Every second side of a two-sided job is a back.
        const bool back = header.duplex && (i % 2 == 1);
        const bool mirrorX = back && backSide.mirrorCrossFeed;
        const bool mirrorY = back && backSide.mirrorFeed;
        if (mirrorX || mirrorY) {
            pixels = IOPwgTransformPixels(pixels, width, height, channels, 0, mirrorX, mirrorY);
        }

        header.crossFeedMirrored = mirrorX;
        header.feedMirrored = mirrorY;
        if (!AppendPwgRasterPageHeader(header, stream) ||
            !AppendPwgRasterPageLines(header, pixels.data(), pixels.size(), stream)) {
            return IODeviceResult::Error(IODeviceResultCode::BackendError,
                                         "Could not write the page as PWG raster");
        }
    }

    payload.filePath.clear();
    payload.data = std::move(stream);
    payload.contentType = "image/pwg-raster";
    payload.pageRange.clear();      // selected above; sending it too would select twice
    payload.isRaw = false;
    return IODeviceResult::Ok();
}

// ============================================================================
// RENDERER
// ============================================================================

// Sends a document the printer renders itself as it is, and draws anything
// else as PWG raster. Either way the printer does the rest: that is what
// driverless means.
class IppPrintRenderer : public IPrintRenderer {
public:
    explicit IppPrintRenderer(std::shared_ptr<IppPrinterFacts> printerFacts)
        : facts(std::move(printerFacts)) {}

    IOPrintRenderer GetKind() const override { return IOPrintRenderer::IPP; }
    bool IsAvailable() const override { return true; }
    bool SupportsPrinter(const IODeviceInfo& printer) const override {
        (void)printer;
        return true;
    }

    IODeviceResult Render(const IODeviceInfo& printer, const IOPrintJob& job,
                          const IOPrinterCapabilities& capabilities,
                          IOPrintPayload& payload) override {
        (void)printer;
        (void)capabilities;

        if (!facts || !facts->described) {
            return IODeviceResult::Error(
                IODeviceResultCode::InvalidState,
                "The printer has not described itself yet; connect to it first");
        }

        IppDocumentPlan plan;
        IODeviceResult planned =
            PlanIppDocument(IppJobDocumentType(job), job.pageRange, facts->documents, plan);
        if (!planned.success) return planned;

        if (!plan.passThrough) return DrawPwgRaster(job, facts->documents, payload);

        payload.filePath = job.filePath;
        payload.data = job.filePath.empty() ? job.data : std::vector<uint8_t>();
        payload.contentType = plan.documentFormat;
        if (!plan.sendPageRange) payload.pageRange.clear();
        payload.isRaw = false;
        return IODeviceResult::Ok();
    }

private:
    std::shared_ptr<IppPrinterFacts> facts;
};

// ============================================================================
// TRANSPORT
// ============================================================================

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& out) {
    std::FILE* file = OpenFileUtf8(path, "rb");
    if (!file) return false;
    char buffer[64 * 1024];
    size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
        out.insert(out.end(), buffer, buffer + read);
    }
    const bool ok = !std::ferror(file);
    std::fclose(file);
    return ok;
}

// A Print-Job request with the document after it, POSTed to the printer.
class IppPrintTransport : public IPrintTransport {
public:
    std::string GetName() const override { return "IPP"; }

    // A driverless printer takes documents, in the formats it names. It has
    // no raw channel for a device-native stream, and no drawing session.
    bool SupportsRaw() const override { return false; }
    bool SupportsDocument() const override { return true; }
    bool SupportsPageSource() const override { return false; }

    IODeviceResult Submit(const IODeviceInfo& printer, const IOPrintPayload& payload,
                          const IOPrintOptions& options, int& outJobId) override {
        const std::string& uri = printer.connectionPath;

        std::vector<uint8_t> document;
        if (!payload.filePath.empty()) {
            // Read whole: the request and the document travel as one HTTP
            // body. A document printed this way is one the printer renders
            // itself - a PDF, a JPEG - rather than a raster of every page, so
            // it is the size of the file on disk.
            if (!ReadWholeFile(payload.filePath, document)) {
                return IODeviceResult::Error(
                    IODeviceResultCode::IOError,
                    "Could not read '" + payload.filePath + "' to send it to the printer",
                    printer.deviceId);
            }
        } else {
            document = payload.data;
        }

        IppMessage request =
            MakeIppRequest(IppOperation::PrintJob, NextRequestId(), uri, RequestingUserName());
        IppGroup& operation = request.groups.front();
        operation.Set("job-name", IppValue::Name(payload.jobName.empty()
                                                     ? std::string("UltraCanvas document")
                                                     : payload.jobName));
        operation.Set("document-format",
                      IppValue::MimeType(payload.contentType.empty() ? "application/octet-stream"
                                                                     : payload.contentType));

        IppGroup& job = request.AddGroup(IppTag::JobGroup);
        AddIppJobTemplate(job, options, payload.pageRange,
                          payload.contentType == "image/pwg-raster");

        IppMessage response;
        IODeviceResult sent;
        const auto started = std::chrono::steady_clock::now();
        int pauseMs = 1000;
        for (;;) {
            request.requestId = NextRequestId();
            sent = SendIpp(uri, request, &document, kPrintTimeoutMs, response, printer.deviceId);
            if (sent.success || !IppStatusIsRetryable(static_cast<uint16_t>(sent.backendCode))) {
                break;
            }
            const auto waited = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - started)
                                    .count();
            if (waited + pauseMs > kBusyWaitMs) {
                sent.message += " - still refusing after " + std::to_string(waited / 1000) +
                                " seconds of asking";
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(pauseMs));
            pauseMs = std::min(pauseMs * 2, 10000);
        }
        if (!sent.success) return sent;

        outJobId = 0;
        if (const IppAttribute* id = response.Find(IppTag::JobGroup, "job-id")) {
            outJobId = id->IntegerOr(0);
        }

        // Accepted with substitutions is still accepted - IPP's fidelity
        // default has the printer do its best - but a caller should hear what
        // the printer set aside, so it is named rather than swallowed.
        IODeviceResult result = IODeviceResult::Ok(printer.deviceId);
        if (const IppGroup* ignored = response.FindGroup(IppTag::UnsupportedGroup)) {
            std::string names;
            for (const IppAttribute& attribute : ignored->attributes) {
                if (!names.empty()) names += ", ";
                names += attribute.name;
            }
            if (!names.empty()) result.message = "The printer ignored: " + names;
        }
        return result;
    }
};

IPrintTransportPtr SharedIppTransport() {
    static IPrintTransportPtr transport = std::make_shared<IppPrintTransport>();
    return transport;
}

// ============================================================================
// THE DEVICE
// ============================================================================

class IppPrinterDevice : public PrinterDevice {
public:
    explicit IppPrinterDevice(const IODeviceInfo& info)
        : PrinterDevice(info), facts(std::make_shared<IppPrinterFacts>()) {
        AddRenderer(std::make_shared<IppPrintRenderer>(facts));
    }

protected:
    IODeviceResult DoConnect() override {
        // IPP has no session: connecting is asking the printer to describe
        // itself, which both proves it is there and gets the one document the
        // renderer cannot work without.
        IOPrinterCapabilities unused;
        return DoGetCapabilities(unused);
    }

    void DoDisconnect() override {}

    IODeviceResult DoGetCapabilities(IOPrinterCapabilities& capabilities) override {
        IppMessage response;
        IODeviceResult asked = GetPrinterAttributes({"all"}, response);
        if (!asked.success) return asked;

        const IppGroup* printer = response.FindGroup(IppTag::PrinterGroup);
        if (!printer) {
            return IODeviceResult::Error(IODeviceResultCode::CommunicationError,
                                         "The printer's description has no printer attributes",
                                         GetDeviceId());
        }

        capabilities = IppCapabilitiesFromAttributes(*printer);
        facts->documents = IppDocumentSupportFromAttributes(*printer);
        facts->described = true;
        FillInIdentity(*printer);
        return IODeviceResult::Ok(GetDeviceId());
    }

    IOPrinterStatus DoGetStatus() override {
        IppMessage response;
        if (!GetPrinterAttributes({"printer-state", "printer-state-reasons",
                                   "printer-is-accepting-jobs", "queued-job-count",
                                   "marker-levels", "marker-names", "marker-types",
                                   "marker-colors", "printer-supply",
                                   "printer-supply-description"},
                                  response)
                 .success) {
            return IOPrinterStatus();
        }
        const IppGroup* printer = response.FindGroup(IppTag::PrinterGroup);
        return printer ? IppStatusFromAttributes(*printer) : IOPrinterStatus();
    }

    std::vector<IOSupplyLevel> DoGetSupplyLevels() override {
        std::vector<IOSupplyLevel> supplies;
        Internal::QueryIppSupplyLevels(Uri(), supplies);
        return supplies;
    }

    IODeviceResult DoCancelJob(int jobId) override {
        IppMessage request = MakeIppRequest(IppOperation::CancelJob, NextRequestId(), Uri(),
                                            RequestingUserName());
        request.groups.front().Set("job-id", IppValue::Integer(jobId));
        IppMessage response;
        return SendIpp(Uri(), request, nullptr, kMetadataTimeoutMs, response, GetDeviceId());
    }

    IOPrintJobStatus DoGetJobStatus(int jobId) override {
        IppMessage request = MakeIppRequest(IppOperation::GetJobAttributes, NextRequestId(),
                                            Uri(), RequestingUserName());
        request.groups.front().Set("job-id", IppValue::Integer(jobId));

        IppMessage response;
        IODeviceResult asked =
            SendIpp(Uri(), request, nullptr, kMetadataTimeoutMs, response, GetDeviceId());

        IOPrintJobStatus status;
        status.jobId = jobId;
        if (asked.success) {
            if (const IppGroup* job = response.FindGroup(IppTag::JobGroup)) {
                status = IppJobStatusFromAttributes(*job);
                if (status.jobId == 0) status.jobId = jobId;
            }
            return status;
        }
        if (asked.backendCode == kIppStatusNotFound) {
            // Gone from the printer's history, which it keeps only briefly.
            // The CUPS backend reads the same absence as completion; so does
            // this one, and says why, so a caller can tell the two apart.
            status.state = IOPrintJobState::Completed;
            status.stateReason = "not-in-printer-history";
        }
        return status;
    }

    std::vector<IOPrintJobStatus> DoGetJobQueue() override {
        IppMessage request =
            MakeIppRequest(IppOperation::GetJobs, NextRequestId(), Uri(), RequestingUserName());
        IppGroup& operation = request.groups.front();
        operation.Set("which-jobs", IppValue::Keyword("not-completed"));
        std::vector<IppValue> wanted;
        for (const char* name : {"job-id", "job-state", "job-state-reasons", "job-name",
                                 "job-originating-user-name", "job-impressions",
                                 "job-impressions-completed"}) {
            wanted.push_back(IppValue::Keyword(name));
        }
        operation.Set("requested-attributes", std::move(wanted));

        IppMessage response;
        std::vector<IOPrintJobStatus> queue;
        if (!SendIpp(Uri(), request, nullptr, kMetadataTimeoutMs, response, GetDeviceId())
                 .success) {
            return queue;
        }
        for (const IppGroup* job : response.FindGroups(IppTag::JobGroup)) {
            queue.push_back(IppJobStatusFromAttributes(*job));
        }
        return queue;
    }

    IPrintTransportPtr GetTransport() override { return SharedIppTransport(); }

private:
    std::string Uri() const { return GetDeviceInfo().connectionPath; }

    IODeviceResult GetPrinterAttributes(const std::vector<std::string>& attributes,
                                        IppMessage& response) {
        IppMessage request = MakeIppRequest(IppOperation::GetPrinterAttributes, NextRequestId(),
                                            Uri(), RequestingUserName());
        std::vector<IppValue> wanted;
        for (const std::string& name : attributes) wanted.push_back(IppValue::Keyword(name));
        request.groups.front().Set("requested-attributes", std::move(wanted));
        return SendIpp(Uri(), request, nullptr, kMetadataTimeoutMs, response, GetDeviceId());
    }

    // Discovery knows a printer by what its advertisement said; its own
    // description is better, and fills what the advertisement left out. The
    // device id is left alone: the registry is keyed on it.
    void FillInIdentity(const IppGroup& printer) {
        IODeviceInfo info = GetDeviceInfo();
        bool changed = false;
        auto fill = [&](std::string& field, const char* attribute) {
            if (!field.empty()) return;
            if (const IppAttribute* value = printer.Find(attribute)) {
                if (!value->String().empty()) {
                    field = value->String();
                    changed = true;
                }
            }
        };
        fill(info.model, "printer-make-and-model");
        fill(info.location, "printer-location");
        fill(info.description, "printer-info");
        if (info.name.empty() || info.name == info.connectionPath) {
            if (const IppAttribute* name = printer.Find("printer-name")) {
                if (!name->String().empty()) {
                    info.name = name->String();
                    changed = true;
                }
            }
        }
        if (changed) UpdateDeviceInfo(info);
    }

    std::shared_ptr<IppPrinterFacts> facts;
};

// ============================================================================
// FINDING PRINTERS
// ============================================================================

// Printers named outright, as a comma-separated list of addresses.
//
// Not a fallback so much as the way a printer on another subnet is reached at
// all: DNS-SD does not cross routers.
std::vector<std::string> ConfiguredPrinterUris() {
    std::vector<std::string> uris;
    const char* setting = std::getenv("ULTRACANVAS_IPP_PRINTERS");
    if (!setting) return uris;

    std::istringstream list(setting);
    std::string entry;
    while (std::getline(list, entry, ',')) {
        const std::string uri = IppNormalizePrinterUri(entry);
        if (!uri.empty()) uris.push_back(uri);
    }
    return uris;
}

// The device-uri of every queue CUPS offers, so a printer it already reaches
// is not listed a second time. Empty where CUPS is not compiled in, and where
// no scheduler is running - in which case every printer found is listed.
std::vector<std::string> CupsDeviceUris() {
    std::vector<std::string> uris;
#if defined(ULTRACANVAS_IPP_DEFERS_TO_CUPS)
    cups_dest_t* dests = nullptr;
    const int count = cupsGetDests2(CUPS_HTTP_DEFAULT, &dests);
    for (int i = 0; i < count; ++i) {
        const char* uri = cupsGetOption("device-uri", dests[i].num_options, dests[i].options);
        if (uri && *uri) uris.push_back(uri);
    }
    cupsFreeDests(count, dests);
#endif
    return uris;
}

// Browses for _ipp._tcp and _ipps._tcp through the mDNS plugin.
std::vector<IODeviceInfo> DiscoverOverMdns() {
    std::vector<IODeviceInfo> found;

    UltraNet_RefreshPlugins();
    std::shared_ptr<IUltraNetPlugin> plugin = UltraNet_GetPlugin("mdns");
    auto* directory = dynamic_cast<IDirectoryProtocolPlugin*>(plugin.get());
    if (!directory) {
        // No plugin in this build. Not an error: every printer named in
        // ULTRACANVAS_IPP_PRINTERS is still reached.
        return found;
    }

    // Asked once, and only when there is a printer to check: with a scheduler
    // running, libcups browses DNS-SD itself before it answers, which takes
    // seconds that a network with no printers should not spend.
    std::vector<std::string> cupsQueues;
    bool cupsAsked = false;

    // Plain IPP first. A printer offering both is used over plain IPP: its
    // certificate is self-signed in all but a few cases, and TLS verification
    // stays on, so the ipps:// route would fail where the ipp:// one works.
    // The TLS address is kept in the attributes for a caller that wants it.
    struct ServiceType { const char* name; bool tls; };
    for (const ServiceType& service : {ServiceType{"_ipp._tcp", false},
                                       ServiceType{"_ipps._tcp", true}}) {
        UltraNetDirectoryQuery query;
        query.baseDn = service.name;
        query.timeLimitSeconds = 3;

        std::vector<UltraNetDirectoryEntry> entries;
        if (!directory->Search("mdns://", query, entries).success) continue;

        for (const UltraNetDirectoryEntry& entry : entries) {
            auto host = entry.attributes.find("host");
            auto port = entry.attributes.find("port");
            if (host == entry.attributes.end() || host->second.empty()) continue;
            if (port == entry.attributes.end() || port->second.empty()) continue;

            int portNumber = 0;
            try {
                portNumber = std::stoi(port->second[0]);
            } catch (...) {
                continue;
            }

            std::vector<std::string> txt;
            auto records = entry.attributes.find("txt");
            if (records != entry.attributes.end()) txt = records->second;

            const std::string uri = IppUriFromMdns(host->second[0], portNumber, txt, service.tls);
            if (uri.empty()) continue;

            const std::string uuid = IppTxtValue(txt, "UUID");
            const std::string deviceId = IppDeviceIdFor(uuid, uri);

            auto existing = std::find_if(found.begin(), found.end(), [&](const IODeviceInfo& info) {
                return info.deviceId == deviceId;
            });
            if (existing != found.end()) {
                // The same printer again: over TLS, or on another interface.
                if (service.tls) existing->attributes["ipps-uri"] = uri;
                continue;
            }

            if (!cupsAsked) {
                cupsQueues = CupsDeviceUris();
                cupsAsked = true;
            }
            const bool cupsHasIt = std::any_of(
                cupsQueues.begin(), cupsQueues.end(), [&](const std::string& queue) {
                    return IppCupsQueueReachesPrinter(queue, entry.dn, uuid, uri);
                });
            if (cupsHasIt) continue;

            IODeviceInfo info;
            info.deviceId = deviceId;
            // The instance name, which is unique on the network and is what
            // every print dialog shows - two printers of one model share a
            // "ty", and would be indistinguishable by it.
            const std::string instance = IppInstanceFromServiceName(entry.dn);
            info.name = instance.empty() ? IppTxtValue(txt, "ty") : instance;
            if (info.name.empty()) info.name = uri;
            info.model = IppTxtValue(txt, "ty");
            info.manufacturer = IppTxtValue(txt, "usb_MFG");
            info.location = IppTxtValue(txt, "note");
            info.serialNumber = uuid;
            info.category = IODeviceCategory::Printer;
            info.transport = IODeviceTransport::Network;
            info.backend = "IPP";
            info.connectionPath = uri;
            info.attributes["discovery"] = "mdns";
            info.attributes["host"] = host->second[0];
            info.attributes["mdns-name"] = entry.dn;
            if (!uuid.empty()) info.attributes["uuid"] = uuid;
            const std::string formats = IppTxtValue(txt, "pdl");
            if (!formats.empty()) info.attributes["pdl"] = formats;
            if (service.tls) info.attributes["ipps-uri"] = uri;
            found.push_back(std::move(info));
        }
    }
    return found;
}

std::vector<IODevicePtr> EnumerateIppPrinters() {
    std::vector<IODeviceInfo> infos = DiscoverOverMdns();

    for (const std::string& uri : ConfiguredPrinterUris()) {
        // Named explicitly, so listed even when CUPS also has it: a printer
        // written into the setting is a printer someone wants reached this way.
        // One also found by DNS-SD at the same address is still one printer.
        const bool already = std::any_of(infos.begin(), infos.end(), [&](const IODeviceInfo& info) {
            return info.connectionPath == uri || info.deviceId == "ipp:" + uri;
        });
        if (already) continue;

        IODeviceInfo info;
        info.deviceId = IppDeviceIdFor(std::string(), uri);
        info.name = uri;
        info.category = IODeviceCategory::Printer;
        info.transport = IODeviceTransport::Network;
        info.backend = "IPP";
        info.connectionPath = uri;
        info.attributes["discovery"] = "configured";
        infos.push_back(std::move(info));
    }

    std::vector<IODevicePtr> devices;
    devices.reserve(infos.size());
    for (const IODeviceInfo& info : infos) {
        devices.push_back(std::make_shared<IppPrinterDevice>(info));
    }
    return devices;
}

}  // namespace

namespace Internal {

void RegisterIppPrinterBackend(IODeviceManager& manager) {
    manager.RegisterEnumerator(IODeviceCategory::Printer, "IPP", EnumerateIppPrinters);
}

IODeviceResult QueryIppSupplyLevels(const std::string& printerUri,
                                    std::vector<IOSupplyLevel>& outSupplies) {
    outSupplies.clear();
    IppMessage request = MakeIppRequest(IppOperation::GetPrinterAttributes, NextRequestId(),
                                        printerUri, RequestingUserName());
    std::vector<IppValue> wanted;
    for (const char* name : {"marker-levels", "marker-names", "marker-types", "marker-colors",
                             "printer-supply", "printer-supply-description"}) {
        wanted.push_back(IppValue::Keyword(name));
    }
    request.groups.front().Set("requested-attributes", std::move(wanted));

    IppMessage response;
    IODeviceResult asked =
        SendIpp(printerUri, request, nullptr, kMetadataTimeoutMs, response, IODeviceId());
    if (asked.success) {
        if (const IppGroup* printer = response.FindGroup(IppTag::PrinterGroup)) {
            outSupplies = IppSuppliesFromAttributes(*printer);
        }
    }
    return asked;
}

}  // namespace Internal
}  // namespace UltraCanvas

#endif  // ULTRACANVAS_HAS_NET
