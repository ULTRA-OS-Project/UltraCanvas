// core/IODeviceManager/UltraCanvasIODevicePrinterCUPS.cpp
// CUPS printer backend: enumeration, capabilities, status, supplies, and the
// transport that carries both driver documents and device-native raw streams.
//
// This lives in core/ rather than OS/Linux/ and OS/MacOS/ because CUPS is the
// same library with the same API on both, and the platform split that an
// earlier attempt at this module used produced two copies that drifted until
// the macOS half no longer matched what the shared code called.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#if defined(ULTRACANVAS_HAS_CUPS) && (defined(__linux__) || defined(__APPLE__))

#include "../../include/IODeviceManager/UltraCanvasIODevicePrinter.h"
#include "../../include/IODeviceManager/UltraCanvasIODeviceManager.h"

#include <cups/cups.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace {

// ============================================================================
// SMALL HELPERS
// ============================================================================

// IPP reserves status codes 0x0000-0x00FF for success. Several of them mean
// "accepted, but I substituted something" - the usual answer when a printer
// quietly swaps an option - so testing the class beats listing the members.
bool IppSucceeded(ipp_status_t status) {
    return static_cast<int>(status) < 0x0100;
}

std::string OptionValue(cups_dest_t* dest, const char* name) {
    if (!dest) {
        return std::string();
    }
    const char* value = cupsGetOption(name, dest->num_options, dest->options);
    return value ? std::string(value) : std::string();
}

// CUPS reports media in hundredths of a millimetre, the same unit
// IOPaperDimensions uses, so a size matches by number rather than by the
// name the printer happens to give it. 1 mm of slack absorbs the rounding
// between a printer's own table and the nominal ISO/ANSI figure.
IOPaperSize PaperSizeFromDimensions(int widthHundredthsMM, int lengthHundredthsMM) {
    constexpr int kToleranceHundredthsMM = 100;

    static const IOPaperSize kCandidates[] = {
        IOPaperSize::A3, IOPaperSize::A4, IOPaperSize::A5, IOPaperSize::A6,
        IOPaperSize::B4, IOPaperSize::B5,
        IOPaperSize::Letter, IOPaperSize::Legal, IOPaperSize::Tabloid,
        IOPaperSize::Executive,
        IOPaperSize::Photo4x6, IOPaperSize::Photo5x7, IOPaperSize::Photo8x10,
        IOPaperSize::Envelope10, IOPaperSize::EnvelopeDL, IOPaperSize::EnvelopeC5,
    };

    for (IOPaperSize candidate : kCandidates) {
        const IOPaperDimensions nominal = IOPaperSizeDimensions(candidate);
        if (std::abs(nominal.widthHundredthsMM - widthHundredthsMM) <= kToleranceHundredthsMM &&
            std::abs(nominal.heightHundredthsMM - lengthHundredthsMM) <= kToleranceHundredthsMM) {
            return candidate;
        }
    }
    return IOPaperSize::Unknown;
}

IOSupplyType SupplyTypeFromMarker(const std::string& markerType) {
    if (markerType.find("toner") != std::string::npos)       return IOSupplyType::Toner;
    if (markerType.find("ink") != std::string::npos)         return IOSupplyType::Ink;
    if (markerType.find("opc") != std::string::npos ||
        markerType.find("drum") != std::string::npos)        return IOSupplyType::Drum;
    if (markerType.find("fuser") != std::string::npos)       return IOSupplyType::Fuser;
    if (markerType.find("waste") != std::string::npos)       return IOSupplyType::WasteTank;
    if (markerType.find("staple") != std::string::npos)      return IOSupplyType::Staples;
    return IOSupplyType::Unknown;
}

// CUPS gives marker colours as #RRGGBB, so the colour is recovered from the
// hex rather than from the marker's name, which is localised and
// vendor-specific.
IOSupplyColor SupplyColorFromHex(const std::string& hex) {
    if (hex.size() < 7 || hex[0] != '#') {
        return IOSupplyColor::None;
    }
    const long rgb = std::strtol(hex.c_str() + 1, nullptr, 16);
    switch (rgb) {
        case 0x000000: return IOSupplyColor::Black;
        case 0x00FFFF: return IOSupplyColor::Cyan;
        case 0xFF00FF: return IOSupplyColor::Magenta;
        case 0xFFFF00: return IOSupplyColor::Yellow;
        case 0xE0FFFF: return IOSupplyColor::LightCyan;
        case 0xFFE0FF: return IOSupplyColor::LightMagenta;
        case 0x808080: return IOSupplyColor::Gray;
        case 0xFF0000: return IOSupplyColor::Red;
        case 0x0000FF: return IOSupplyColor::Blue;
        default: break;
    }
    return IOSupplyColor::None;
}

// Splits the comma-separated lists CUPS uses for the marker-* attributes.
std::vector<std::string> SplitList(const std::string& text) {
    std::vector<std::string> parts;
    std::string current;
    for (char c : text) {
        if (c == ',') {
            parts.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    if (!current.empty() || !text.empty()) {
        parts.push_back(current);
    }
    return parts;
}

// ============================================================================
// OPTION MAPPING
// ============================================================================

class CupsOptions {
public:
    explicit CupsOptions(const IOPrintOptions& options) {
        Add(CUPS_COPIES, std::to_string(options.copies));
        Add("collate", options.collate ? "true" : "false");

        const std::string media = IOPaperSizeToPwgName(options.page.paperSize);
        if (!media.empty()) {
            Add(CUPS_MEDIA, media);
        }

        switch (options.page.orientation) {
            // IPP orientation-requested enumeration.
            case IOPrintOrientation::Portrait:        Add("orientation-requested", "3"); break;
            case IOPrintOrientation::Landscape:       Add("orientation-requested", "4"); break;
            case IOPrintOrientation::ReverseLandscape:Add("orientation-requested", "5"); break;
            case IOPrintOrientation::ReversePortrait: Add("orientation-requested", "6"); break;
        }

        switch (options.duplex) {
            case IODuplexMode::None:
                Add(CUPS_SIDES, CUPS_SIDES_ONE_SIDED);
                break;
            case IODuplexMode::LongEdge:
                Add(CUPS_SIDES, CUPS_SIDES_TWO_SIDED_PORTRAIT);
                break;
            case IODuplexMode::ShortEdge:
                Add(CUPS_SIDES, CUPS_SIDES_TWO_SIDED_LANDSCAPE);
                break;
        }

        switch (options.colorMode) {
            case IOPrinterColorMode::Color:
                Add(CUPS_PRINT_COLOR_MODE, CUPS_PRINT_COLOR_MODE_COLOR);
                break;
            case IOPrinterColorMode::Grayscale:
            case IOPrinterColorMode::Monochrome:
                Add(CUPS_PRINT_COLOR_MODE, CUPS_PRINT_COLOR_MODE_MONOCHROME);
                break;
            case IOPrinterColorMode::Auto:
                Add(CUPS_PRINT_COLOR_MODE, CUPS_PRINT_COLOR_MODE_AUTO);
                break;
        }

        // IPP print-quality enumeration: 3 draft, 4 normal, 5 high.
        switch (options.quality) {
            case IOPrintQuality::Draft:  Add(CUPS_PRINT_QUALITY, "3"); break;
            case IOPrintQuality::Normal: Add(CUPS_PRINT_QUALITY, "4"); break;
            case IOPrintQuality::High:
            case IOPrintQuality::Photo:  Add(CUPS_PRINT_QUALITY, "5"); break;
        }

        IOResolution resolution = options.resolutionMode == IOResolutionMode::Custom
                                      ? options.customResolution
                                      : IOResolutionModeDpi(options.resolutionMode);
        if (resolution.IsValid()) {
            Add("printer-resolution", std::to_string(resolution.dpiX) + "x" +
                                          std::to_string(resolution.dpiY) + "dpi");
        }

        // Caller escape hatches last, so an explicit PPD keyword wins over
        // anything derived above.
        for (const auto& option : options.backendOptions) {
            Add(option.first, option.second);
        }
    }

    ~CupsOptions() { cupsFreeOptions(count, options); }

    CupsOptions(const CupsOptions&) = delete;
    CupsOptions& operator=(const CupsOptions&) = delete;

    int Count() const { return count; }
    cups_option_t* Get() const { return options; }

private:
    void Add(const std::string& name, const std::string& value) {
        count = cupsAddOption(name.c_str(), value.c_str(), count, &options);
    }

    cups_option_t* options = nullptr;
    int count = 0;
};

// ============================================================================
// TRANSPORT
// ============================================================================

// Carries both kinds of payload: a document for the CUPS filter chain, and a
// device-native stream submitted as application/vnd.cups-raw, which is what
// lets the GutenPrint renderer reach the printer untouched.
class CupsPrintTransport : public IPrintTransport {
public:
    std::string GetName() const override { return "CUPS"; }
    bool SupportsRaw() const override { return true; }

    IODeviceResult Submit(const IODeviceInfo& printer,
                          const IOPrintPayload& payload,
                          const IOPrintOptions& options,
                          int& outJobId) override {
        cups_dest_t* dest = cupsGetNamedDest(CUPS_HTTP_DEFAULT,
                                             printer.connectionPath.c_str(), nullptr);
        if (!dest) {
            return IODeviceResult::Error(IODeviceResultCode::DeviceNotFound,
                                         "CUPS no longer knows a printer named '" +
                                             printer.connectionPath + "'",
                                         printer.deviceId);
        }

        cups_dinfo_t* info = cupsCopyDestInfo(CUPS_HTTP_DEFAULT, dest);
        if (!info) {
            cupsFreeDests(1, dest);
            return IODeviceResult::BackendError(IODeviceResultCode::CommunicationError,
                                                std::string("CUPS: ") + cupsLastErrorString(),
                                                cupsLastError(), printer.deviceId);
        }

        CupsOptions cupsOptions(options);
        // The job's own name, not the printer's: a queue is read by what
        // the documents are called.
        const std::string title =
            payload.jobName.empty() ? std::string("UltraCanvas document")
                                    : payload.jobName;

        int jobId = 0;
        ipp_status_t status = cupsCreateDestJob(CUPS_HTTP_DEFAULT, dest, info, &jobId,
                                                title.c_str(), cupsOptions.Count(),
                                                cupsOptions.Get());
        if (!IppSucceeded(status)) {
            IODeviceResult result = IODeviceResult::BackendError(
                IODeviceResultCode::BackendError,
                std::string("CUPS refused the job: ") + cupsLastErrorString(),
                static_cast<int>(status), printer.deviceId);
            cupsFreeDestInfo(info);
            cupsFreeDests(1, dest);
            return result;
        }

        // A raw payload is the printer's own command stream and must not be
        // filtered; anything else goes through CUPS's auto-detection unless
        // the renderer named a type.
        const char* format = payload.isRaw
                                 ? CUPS_FORMAT_RAW
                                 : (payload.contentType.empty() ? CUPS_FORMAT_AUTO
                                                                : payload.contentType.c_str());

        IODeviceResult result = SendDocument(dest, info, jobId, format, payload, printer);

        cupsFreeDestInfo(info);
        cupsFreeDests(1, dest);

        if (result.success) {
            outJobId = jobId;
        }
        return result;
    }

private:
    IODeviceResult SendDocument(cups_dest_t* dest, cups_dinfo_t* info, int jobId,
                                const char* format, const IOPrintPayload& payload,
                                const IODeviceInfo& printer) {
        const std::string docName =
            payload.filePath.empty() ? std::string("document") : payload.filePath;

        http_status_t started = cupsStartDestDocument(
            CUPS_HTTP_DEFAULT, dest, info, jobId, docName.c_str(), format, 0, nullptr,
            /*last_document=*/1);
        if (started != HTTP_STATUS_CONTINUE) {
            return IODeviceResult::BackendError(
                IODeviceResultCode::CommunicationError,
                std::string("CUPS would not accept the document: ") + cupsLastErrorString(),
                static_cast<int>(started), printer.deviceId);
        }

        const bool sent = payload.filePath.empty()
                              ? WriteBuffer(payload.data)
                              : WriteFile(payload.filePath);

        ipp_status_t finished = cupsFinishDestDocument(CUPS_HTTP_DEFAULT, dest, info);

        if (!sent) {
            return IODeviceResult::Error(IODeviceResultCode::IOError,
                                         "Could not read '" + payload.filePath +
                                             "' to send it to the printer",
                                         printer.deviceId);
        }
        if (!IppSucceeded(finished)) {
            return IODeviceResult::BackendError(
                IODeviceResultCode::BackendError,
                std::string("CUPS rejected the document: ") + cupsLastErrorString(),
                static_cast<int>(finished), printer.deviceId);
        }
        return IODeviceResult::Ok(printer.deviceId);
    }

    static bool WriteBuffer(const std::vector<uint8_t>& data) {
        if (data.empty()) {
            return true;
        }
        return cupsWriteRequestData(CUPS_HTTP_DEFAULT,
                                    reinterpret_cast<const char*>(data.data()),
                                    data.size()) == HTTP_STATUS_CONTINUE;
    }

    // Streamed rather than slurped: a print-ready raster of a photo page can
    // be hundreds of megabytes.
    static bool WriteFile(const std::string& path) {
        FILE* file = std::fopen(path.c_str(), "rb");
        if (!file) {
            return false;
        }
        char buffer[64 * 1024];
        size_t read = 0;
        bool ok = true;
        while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
            if (cupsWriteRequestData(CUPS_HTTP_DEFAULT, buffer, read) != HTTP_STATUS_CONTINUE) {
                ok = false;
                break;
            }
        }
        std::fclose(file);
        return ok;
    }
};

IPrintTransportPtr SharedCupsTransport() {
    static IPrintTransportPtr transport = std::make_shared<CupsPrintTransport>();
    return transport;
}

// ============================================================================
// DEVICE
// ============================================================================

class CupsPrinterDevice : public PrinterDevice {
public:
    explicit CupsPrinterDevice(const IODeviceInfo& info) : PrinterDevice(info) {
        AddRenderer(std::make_shared<NativePrintRenderer>());
    }

    ~CupsPrinterDevice() override { ReleaseCupsHandles(); }

protected:
    IODeviceResult DoConnect() override {
        const std::string queue = GetDeviceInfo().connectionPath;

        dest = cupsGetNamedDest(CUPS_HTTP_DEFAULT, queue.c_str(), nullptr);
        if (!dest) {
            return IODeviceResult::BackendError(
                IODeviceResultCode::DeviceNotFound,
                "CUPS has no printer named '" + queue + "'",
                cupsLastError());
        }

        // Not fatal: a printer that is offline still enumerates and can be
        // queued to, it just cannot answer capability questions yet.
        destInfo = cupsCopyDestInfo(CUPS_HTTP_DEFAULT, dest);
        return IODeviceResult::Ok();
    }

    void DoDisconnect() override { ReleaseCupsHandles(); }

    IODeviceResult DoGetCapabilities(IOPrinterCapabilities& capabilities) override {
        if (!dest || !destInfo) {
            return IODeviceResult::Error(IODeviceResultCode::InvalidState,
                                         "Printer capabilities need an open connection");
        }

        // Media: ask CUPS for the sizes the printer reports, and recognise
        // each by its dimensions. The name a printer gives a size is its own
        // business; 21000 x 29700 hundredths of a millimetre is A4 whatever
        // it is called.
        const int mediaCount =
            cupsGetDestMediaCount(CUPS_HTTP_DEFAULT, dest, destInfo, 0);
        for (int i = 0; i < mediaCount; ++i) {
            cups_size_t size;
            if (!cupsGetDestMediaByIndex(CUPS_HTTP_DEFAULT, dest, destInfo, i, 0, &size)) {
                continue;
            }
            const IOPaperSize paper = PaperSizeFromDimensions(size.width, size.length);
            if (paper != IOPaperSize::Unknown) {
                bool known = false;
                for (IOPaperSize existing : capabilities.paperSizes) {
                    if (existing == paper) {
                        known = true;
                        break;
                    }
                }
                if (!known) {
                    capabilities.paperSizes.push_back(paper);
                }
            }
            if (size.width > capabilities.maxCustomSize.widthHundredthsMM) {
                capabilities.maxCustomSize.widthHundredthsMM = size.width;
            }
            if (size.length > capabilities.maxCustomSize.heightHundredthsMM) {
                capabilities.maxCustomSize.heightHundredthsMM = size.length;
            }
        }

        // CUPS answers these definitively, so they are Yes or No rather
        // than Unknown - a printer we could reach has told us.
        capabilities.supportsColor = IOSupportFrom(cupsCheckDestSupported(
            CUPS_HTTP_DEFAULT, dest, destInfo, CUPS_PRINT_COLOR_MODE,
            CUPS_PRINT_COLOR_MODE_COLOR) != 0);

        capabilities.supportsDuplex = IOSupportFrom(cupsCheckDestSupported(
            CUPS_HTTP_DEFAULT, dest, destInfo, CUPS_SIDES,
            CUPS_SIDES_TWO_SIDED_PORTRAIT) != 0);

        capabilities.supportsCollate = IOSupportFrom(cupsCheckDestSupported(
            CUPS_HTTP_DEFAULT, dest, destInfo, "collate", nullptr) != 0);

        capabilities.qualities = {IOPrintQuality::Draft, IOPrintQuality::Normal,
                                  IOPrintQuality::High};

        // Deliberately left empty: CUPS does not describe GutenPrint's media
        // and inkset vocabulary, and an empty list reads as "not reported"
        // rather than "nothing supported". The GutenPrint renderer fills
        // these in from the model when it lands.
        capabilities.maxCopies = 999;

        return IODeviceResult::Ok(GetDeviceId());
    }

    IOPrinterStatus DoGetStatus() override {
        IOPrinterStatus status;
        if (!dest) {
            return status;
        }

        const std::string state = OptionValue(dest, "printer-state");
        switch (std::atoi(state.c_str())) {
            case 3:  status.state = IOPrinterState::Idle;     break;
            case 4:  status.state = IOPrinterState::Printing; break;
            case 5:  status.state = IOPrinterState::Stopped;  break;
            default: status.state = IOPrinterState::Unknown;  break;
        }

        status.stateReason = OptionValue(dest, "printer-state-reasons");
        status.acceptingJobs = OptionValue(dest, "printer-is-accepting-jobs") == "true";
        status.supplies = DoGetSupplyLevels();
        return status;
    }

    std::vector<IOSupplyLevel> DoGetSupplyLevels() override {
        std::vector<IOSupplyLevel> supplies;
        if (!dest) {
            return supplies;
        }

        const std::vector<std::string> levels = SplitList(OptionValue(dest, "marker-levels"));
        if (levels.empty()) {
            return supplies;
        }
        const std::vector<std::string> names = SplitList(OptionValue(dest, "marker-names"));
        const std::vector<std::string> types = SplitList(OptionValue(dest, "marker-types"));
        const std::vector<std::string> colors = SplitList(OptionValue(dest, "marker-colors"));

        for (size_t i = 0; i < levels.size(); ++i) {
            IOSupplyLevel supply;

            // CUPS uses -1 and -2 for "unknown" and "unavailable"; both mean
            // the printer did not say, which is not the same as empty.
            const int level = std::atoi(levels[i].c_str());
            supply.percentRemaining = (level >= 0 && level <= 100) ? level : -1;

            if (i < names.size())  supply.description = names[i];
            if (i < types.size())  supply.type = SupplyTypeFromMarker(types[i]);
            if (i < colors.size()) supply.color = SupplyColorFromHex(colors[i]);

            if (supply.description.empty()) {
                supply.description = IOSupplyColorToString(supply.color);
            }
            supplies.push_back(supply);
        }
        return supplies;
    }

    IODeviceResult DoCancelJob(int jobId) override {
        if (!dest) {
            return IODeviceResult::Error(IODeviceResultCode::InvalidState,
                                         "Cancelling a job needs an open connection",
                                         GetDeviceId());
        }
        const ipp_status_t status = cupsCancelDestJob(CUPS_HTTP_DEFAULT, dest, jobId);
        if (!IppSucceeded(status)) {
            return IODeviceResult::BackendError(
                IODeviceResultCode::BackendError,
                std::string("CUPS could not cancel job ") + std::to_string(jobId) + ": " +
                    cupsLastErrorString(),
                static_cast<int>(status), GetDeviceId());
        }
        return IODeviceResult::Ok(GetDeviceId());
    }

    IOPrintJobStatus DoGetJobStatus(int jobId) override {
        for (const IOPrintJobStatus& job : DoGetJobQueue()) {
            if (job.jobId == jobId) {
                return job;
            }
        }
        // Absent from the active queue means finished; CUPS keeps no detail
        // once the job leaves, so say completed rather than unknown.
        IOPrintJobStatus status;
        status.jobId = jobId;
        status.state = IOPrintJobState::Completed;
        return status;
    }

    std::vector<IOPrintJobStatus> DoGetJobQueue() override {
        std::vector<IOPrintJobStatus> queue;
        if (!dest) {
            return queue;
        }

        cups_job_t* jobs = nullptr;
        const int count = cupsGetJobs(&jobs, dest->name, /*myjobs=*/0, CUPS_WHICHJOBS_ACTIVE);
        for (int i = 0; i < count; ++i) {
            IOPrintJobStatus status;
            status.jobId = jobs[i].id;
            status.jobName = jobs[i].title ? jobs[i].title : "";
            status.user = jobs[i].user ? jobs[i].user : "";
            status.pagesTotal = jobs[i].size;

            switch (jobs[i].state) {
                case IPP_JSTATE_PENDING:    status.state = IOPrintJobState::Pending;    break;
                case IPP_JSTATE_HELD:       status.state = IOPrintJobState::Held;       break;
                case IPP_JSTATE_PROCESSING: status.state = IOPrintJobState::Processing; break;
                case IPP_JSTATE_STOPPED:    status.state = IOPrintJobState::Stopped;    break;
                case IPP_JSTATE_CANCELED:   status.state = IOPrintJobState::Cancelled;  break;
                case IPP_JSTATE_ABORTED:    status.state = IOPrintJobState::Aborted;    break;
                case IPP_JSTATE_COMPLETED:  status.state = IOPrintJobState::Completed;  break;
                default:                    status.state = IOPrintJobState::Unknown;    break;
            }
            queue.push_back(status);
        }
        cupsFreeJobs(count, jobs);
        return queue;
    }

    IPrintTransportPtr GetTransport() override { return SharedCupsTransport(); }

private:
    void ReleaseCupsHandles() {
        if (destInfo) {
            cupsFreeDestInfo(destInfo);
            destInfo = nullptr;
        }
        if (dest) {
            cupsFreeDests(1, dest);
            dest = nullptr;
        }
    }

    cups_dest_t* dest = nullptr;
    cups_dinfo_t* destInfo = nullptr;
};

// ============================================================================
// ENUMERATION
// ============================================================================

std::vector<IODevicePtr> EnumerateCupsPrinters() {
    std::vector<IODevicePtr> printers;

    cups_dest_t* dests = nullptr;
    const int count = cupsGetDests2(CUPS_HTTP_DEFAULT, &dests);

    for (int i = 0; i < count; ++i) {
        cups_dest_t* dest = &dests[i];
        if (!dest->name) {
            continue;
        }

        IODeviceInfo info;
        info.name = dest->name;
        info.category = IODeviceCategory::Printer;
        info.backend = "CUPS";
        info.connectionPath = dest->name;

        // Prefer the printer's own UUID as the device id so that the same
        // physical printer found again through the IPP backend collapses to
        // one registry entry instead of appearing twice. Queue names differ
        // between hosts; a UUID does not.
        const std::string uuid = OptionValue(dest, "printer-uuid");
        info.deviceId = uuid.empty() ? ("cups:" + std::string(dest->name)) : uuid;

        info.description = OptionValue(dest, "printer-info");
        info.location = OptionValue(dest, "printer-location");

        const std::string makeAndModel = OptionValue(dest, "printer-make-and-model");
        if (!makeAndModel.empty()) {
            const size_t space = makeAndModel.find(' ');
            if (space != std::string::npos) {
                info.manufacturer = makeAndModel.substr(0, space);
                info.model = makeAndModel.substr(space + 1);
            } else {
                info.model = makeAndModel;
            }
        }

        const std::string uri = OptionValue(dest, "device-uri");
        if (!uri.empty()) {
            info.attributes["device-uri"] = uri;
            if (uri.rfind("usb:", 0) == 0) {
                info.transport = IODeviceTransport::USB;
            } else if (uri.rfind("ipp:", 0) == 0 || uri.rfind("ipps:", 0) == 0 ||
                       uri.rfind("socket:", 0) == 0 || uri.rfind("lpd:", 0) == 0 ||
                       uri.rfind("dnssd:", 0) == 0 || uri.rfind("smb:", 0) == 0) {
                info.transport = IODeviceTransport::Network;
            }
        }
        if (dest->is_default) {
            info.attributes["default"] = "true";
        }

        const std::string state = OptionValue(dest, "printer-state");
        info.state = (state == "5") ? IODeviceState::Offline : IODeviceState::Disconnected;

        printers.push_back(std::make_shared<CupsPrinterDevice>(info));
    }

    cupsFreeDests(count, dests);
    return printers;
}

}  // namespace

namespace Internal {

void RegisterCupsPrinterBackend(IODeviceManager& manager) {
    manager.RegisterEnumerator(IODeviceCategory::Printer, "CUPS", EnumerateCupsPrinters);
}

}  // namespace Internal
}  // namespace UltraCanvas

#endif  // ULTRACANVAS_HAS_CUPS && (__linux__ || __APPLE__)
