// core/IODeviceManager/UltraCanvasIODeviceScannerESCL.cpp
// The eSCL scanner backend: driverless network scanning over HTTP.
//
// This is in core/ rather than under OS/ because there is nothing
// platform-specific in it. eSCL is HTTP and XML, so the same file serves
// Linux, macOS and Windows - which is the point of choosing it over WIA,
// TWAIN and ICA, each of which would be one platform's worth of work for one
// platform's worth of scanners.
//
// The protocol arithmetic lives next door in ...ESCLProtocol.cpp so it can be
// tested without a network; this file is the part that needs one.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "IODeviceManager/UltraCanvasIODeviceScannerESCL.h"

#if defined(ULTRACANVAS_HAS_NET)

#include "IODeviceManager/UltraCanvasIODeviceManager.h"
#include "IODeviceManager/UltraCanvasIODeviceScannerESCLProtocol.h"
#include "UltraCanvasImage.h"
#include "UltraNet/UltraNetHttp.h"
#include "UltraNet/UltraNetPlugins.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdlib>
#include <mutex>
#include <sstream>

namespace UltraCanvas {

namespace {

// How long to wait for each call. A scan is slow - a page at 600 dpi can take
// a minute - so the page fetch gets far longer than the metadata calls, which
// should answer at once or be treated as unreachable.
constexpr int kMetadataTimeoutMs = 10000;
constexpr int kScanTimeoutMs = 180000;

UltraNetHttpOptions MetadataOptions() {
    UltraNetHttpOptions options;
    options.timeoutMs = kMetadataTimeoutMs;
    options.connectTimeoutMs = 5000;
    return options;
}

// ============================================================================
// DECODING A PAGE
// ============================================================================

// eSCL hands back an encoded image - JPEG almost always - while ScannedImage
// holds raw pixels. Decoding goes through the same image stack the rest of
// UltraCanvas uses rather than a second one written for scanning.
IODeviceResult DecodePage(const std::vector<uint8_t>& encoded,
                          ScanColorMode wanted, ScannedImage& out) {
    if (encoded.empty()) {
        return IODeviceResult::Error(IODeviceResultCode::MediaError,
                                     "The scanner returned an empty page");
    }

    std::shared_ptr<UCImageRaster> image = UCImageRaster::LoadFromMemory(encoded);

    // Not IsValid(): that also requires a file name, and an image decoded
    // from memory has none. Width and the error slot are what actually say
    // whether the decode worked.
    if (!image || image->GetWidth() <= 0 || !image->errorMessage.empty()) {
        return IODeviceResult::Error(
            IODeviceResultCode::MediaError,
            "Could not decode the page the scanner returned" +
                (image && !image->errorMessage.empty()
                     ? ": " + image->errorMessage
                     : std::string()));
    }

    std::shared_ptr<UCPixmapCairo> pixmap = image->GetPixmap();
    const uint32_t* pixels = pixmap ? pixmap->GetPixelData() : nullptr;
    if (!pixels) {
        return IODeviceResult::Error(IODeviceResultCode::MediaError,
                                     "The decoded page had no pixels");
    }

    const int width = pixmap->GetRawWidth();
    const int height = pixmap->GetRawHeight();
    if (width <= 0 || height <= 0) {
        return IODeviceResult::Error(IODeviceResultCode::MediaError,
                                     "The decoded page had no size");
    }

    const bool grey = wanted == ScanColorMode::Grayscale ||
                      wanted == ScanColorMode::Lineart ||
                      wanted == ScanColorMode::Halftone;
    const int channels = grey ? 1 : 3;

    out.width = width;
    out.height = height;
    out.bitsPerSample = 8;
    out.channels = channels;
    out.bytesPerLine = width * channels;
    out.colorMode = grey ? ScanColorMode::Grayscale : ScanColorMode::Color;
    out.data.clear();
    out.data.reserve(static_cast<size_t>(out.bytesPerLine) *
                     static_cast<size_t>(height));

    for (int y = 0; y < height; ++y) {
        const uint32_t* row = pixels + static_cast<size_t>(y) *
                                       static_cast<size_t>(width);
        for (int x = 0; x < width; ++x) {
            // 32-bit native-endian ARGB, alpha-premultiplied. A scanned page
            // is opaque, so the premultiplication is the identity and the
            // channels come out as they went in.
            const uint32_t pixel = row[x];
            const uint8_t r = static_cast<uint8_t>((pixel >> 16) & 0xFFu);
            const uint8_t g = static_cast<uint8_t>((pixel >> 8) & 0xFFu);
            const uint8_t b = static_cast<uint8_t>(pixel & 0xFFu);

            if (grey) {
                // Rec. 601 luma, as everywhere else in this codebase.
                out.data.push_back(static_cast<uint8_t>(
                    (r * 299u + g * 587u + b * 114u) / 1000u));
            } else {
                out.data.push_back(r);
                out.data.push_back(g);
                out.data.push_back(b);
            }
        }
    }
    return IODeviceResult::Ok();
}

// ============================================================================
// THE DEVICE
// ============================================================================

class EsclScannerDevice : public ScannerDevice {
public:
    EsclScannerDevice(const IODeviceInfo& info, std::string baseUrl)
        : ScannerDevice(info), base(std::move(baseUrl)) {}

    ~EsclScannerDevice() override {
        // The job belongs to the scanner until somebody cancels it, and a
        // scanner that thinks a job is still running will refuse the next
        // one. Releasing it here costs one request and saves the next caller
        // a DeviceBusy they did nothing to deserve.
        AbandonJob();
    }

protected:
    IODeviceResult DoConnect() override {
        // eSCL has no session to open, so connecting means confirming that
        // something answering eSCL is really there - which also gets the
        // capabilities, the only document worth having up front.
        ScanCapabilities fresh;
        return DoGetCapabilities(fresh);
    }

    void DoDisconnect() override { AbandonJob(); }

    IODeviceResult DoGetCapabilities(ScanCapabilities& outCapabilities) override {
        UltraNetResponse response;
        const UltraNetResult result = UltraNet_HttpGet(
            base + "/ScannerCapabilities", response, MetadataOptions());

        if (!result.success) {
            return IODeviceResult::BackendError(
                IODeviceResultCode::ConnectionFailed,
                "Could not reach the scanner at " + base +
                    (result.message.empty() ? std::string() : ": " + result.message),
                response.statusCode, GetDeviceId());
        }
        if (!response.IsSuccess()) {
            return IODeviceResult::BackendError(
                IODeviceResultCode::BackendError,
                "The scanner refused to describe itself (HTTP " +
                    std::to_string(response.statusCode) + ")",
                response.statusCode, GetDeviceId());
        }

        EsclScannerDescription described;
        IODeviceResult parsed =
            ParseEsclCapabilities(response.GetBodyAsString(), described);
        if (!parsed.success) {
            parsed.deviceId = GetDeviceId();
            return parsed;
        }

        outCapabilities = described.capabilities;
        documentFormat = ChooseFormat(described.documentFormats);
        return IODeviceResult::Ok(GetDeviceId());
    }

    IODeviceResult DoApplyConfiguration(const ScanConfiguration& applied) override {
        // eSCL has no endpoint that holds settings: they travel with each
        // job. So there is nothing to send, and the only thing that matters
        // is that a run already in progress does not keep using the old ones.
        (void)applied;
        AbandonJob();
        return IODeviceResult::Ok(GetDeviceId());
    }

    IODeviceResult DoScanPage(ScannedImage& image) override;

    void DoCancelScan() override {
        // **Only signals.** This runs on a different thread from the one
        // inside DoScanPage, and jobUrl belongs to that thread - releasing
        // the job here would race a request that is using it, and issuing an
        // HTTP DELETE from a function documented to only signal would block
        // the caller of CancelScan() as well.
        //
        // The scanning thread sees the flag when its request returns or
        // times out, and abandons the job there. A cancel arriving when no
        // scan is running has nothing to release: a flatbed job is closed
        // the moment its page arrives, and a feeder job is closed by the
        // thread that was reading it.
        cancelled.store(true);
    }

private:
    // eSCL scanners must offer JPEG, and most offer more. JPEG is chosen
    // where it exists because it is the one every scanner can produce and the
    // one the image stack always decodes; PDF would need the PDF plugin, and
    // a page of it would have to be rasterised back down anyway.
    static std::string ChooseFormat(const std::vector<std::string>& offered) {
        for (const char* preferred : {"image/jpeg", "image/png"}) {
            if (std::find(offered.begin(), offered.end(), preferred) != offered.end()) {
                return preferred;
            }
        }
        // Nothing recognised: say nothing and let the scanner pick its own
        // default, which is likelier to work than naming a type it rejected.
        return std::string();
    }

    IODeviceResult StartJob() {
        const std::string settings =
            BuildEsclScanSettings(configuration, documentFormat);

        UltraNetHttpOptions options = MetadataOptions();
        options.headers.Set("Content-Type", "text/xml");

        UltraNetResponse response;
        const std::vector<uint8_t> body(settings.begin(), settings.end());
        const UltraNetResult result =
            UltraNet_HttpPost(base + "/ScanJobs", body, response, options);

        if (!result.success) {
            return IODeviceResult::BackendError(
                IODeviceResultCode::CommunicationError,
                "Could not ask the scanner to start: " + result.message,
                response.statusCode, GetDeviceId());
        }
        if (response.statusCode == 503) {
            // Somebody else is using it, or it is still finishing the last
            // job. That is a different thing from a broken scanner.
            return IODeviceResult::BackendError(
                IODeviceResultCode::DeviceBusy,
                "The scanner is busy with another job", response.statusCode,
                GetDeviceId());
        }
        if (response.statusCode != 201) {
            return IODeviceResult::BackendError(
                IODeviceResultCode::BackendError,
                "The scanner would not start the job (HTTP " +
                    std::to_string(response.statusCode) + ")",
                response.statusCode, GetDeviceId());
        }

        jobUrl = ResolveEsclJobUrl(base, response.headers.Get("Location"));
        if (jobUrl.empty()) {
            return IODeviceResult::BackendError(
                IODeviceResultCode::BackendError,
                "The scanner started a job but did not say where it is",
                response.statusCode, GetDeviceId());
        }
        pagesInJob = 0;
        return IODeviceResult::Ok(GetDeviceId());
    }

    void AbandonJob() {
        if (jobUrl.empty()) return;
        const std::string url = jobUrl;
        jobUrl.clear();
        pagesInJob = 0;

        UltraNetResponse response;
        // Best effort: the run is over either way, and a scanner that has
        // already finished the job answers 404 here, which is not a problem.
        UltraNet_HttpDelete(url, response, MetadataOptions());
    }

    std::string base;
    std::string documentFormat = "image/jpeg";
    std::string jobUrl;
    int pagesInJob = 0;
    std::atomic<bool> cancelled{false};
};

IODeviceResult EsclScannerDevice::DoScanPage(ScannedImage& image) {
    // A job covers a whole run, not a page: the feeder keeps handing sheets
    // to the same job until it empties. So one is started only when none is
    // open, and the page fetch below is what repeats.
    //
    // The cancel flag is cleared here and nowhere else, because opening a job
    // is what begins a run. Clearing it per page would let a cancel that
    // arrived between two pages be forgotten by the next one.
    if (jobUrl.empty()) {
        cancelled.store(false);
        IODeviceResult started = StartJob();
        if (!started.success) return started;
    }

    UltraNetHttpOptions options;
    options.timeoutMs = kScanTimeoutMs;
    options.connectTimeoutMs = 5000;

    UltraNetResponse response;
    const UltraNetResult result =
        UltraNet_HttpGet(jobUrl + "/NextDocument", response, options);

    if (cancelled.load() || !ShouldContinueScanning()) {
        AbandonJob();
        return IODeviceResult::Error(IODeviceResultCode::Cancelled,
                                     "The scan was cancelled", GetDeviceId());
    }

    if (!result.success) {
        AbandonJob();
        return IODeviceResult::BackendError(
            IODeviceResultCode::CommunicationError,
            "Lost the scanner while fetching a page: " + result.message,
            response.statusCode, GetDeviceId());
    }

    // **404 (or 410) is how eSCL says the feeder is empty.** It ends a run;
    // it does not fail one. Reported as DeviceNotFound, which ScanPages()
    // treats as the end of the run once at least one page has arrived - and
    // as a genuine failure before that, which is right: a job that yields
    // nothing at all was a bad job, not an empty tray.
    if (response.statusCode == 404 || response.statusCode == 410) {
        const int produced = pagesInJob;
        AbandonJob();
        return IODeviceResult::Error(
            IODeviceResultCode::DeviceNotFound,
            produced > 0 ? "The feeder is empty; the run is complete"
                         : "The scanner produced no pages for this job",
            GetDeviceId());
    }

    if (!response.IsSuccess()) {
        AbandonJob();
        return IODeviceResult::BackendError(
            IODeviceResultCode::BackendError,
            "The scanner would not return the page (HTTP " +
                std::to_string(response.statusCode) + ")",
            response.statusCode, GetDeviceId());
    }

    IODeviceResult decoded = DecodePage(response.body, configuration.colorMode, image);
    if (!decoded.success) {
        AbandonJob();
        decoded.deviceId = GetDeviceId();
        return decoded;
    }

    ++pagesInJob;
    image.resolutionDpi = configuration.resolutionDpi;
    ReportProgress(1.0f);   // eSCL reports nothing until the page is whole

    // A flatbed produces one page and then has nothing more to give, so the
    // job is closed immediately. Left open, the next Scan() would fetch from
    // a spent job and read its 404 as an empty feeder on a device that has
    // no feeder.
    if (!ScanSourceIsFeeder(configuration.source)) {
        AbandonJob();
    }
    return IODeviceResult::Ok(GetDeviceId());
}

// ============================================================================
// FINDING SCANNERS
// ============================================================================

// Scanners named outright, as a comma-separated list of base URLs.
//
// This is not a fallback for a broken discovery path so much as the way a
// scanner on another subnet is reached at all: mDNS does not cross routers.
// It is also the only way to reach one on Windows at the moment, where the
// mDNS plugin's browse is a stub.
std::vector<std::string> ExplicitScannerUrls() {
    std::vector<std::string> urls;
    const char* setting = std::getenv("ULTRACANVAS_ESCL_SCANNERS");
    if (!setting) return urls;

    std::istringstream list(setting);
    std::string entry;
    while (std::getline(list, entry, ',')) {
        while (!entry.empty() && std::isspace(static_cast<unsigned char>(entry.front()))) {
            entry.erase(0, 1);
        }
        while (!entry.empty() && std::isspace(static_cast<unsigned char>(entry.back()))) {
            entry.pop_back();
        }
        while (!entry.empty() && entry.back() == '/') entry.pop_back();
        if (!entry.empty()) urls.push_back(entry);
    }
    return urls;
}

// Browses for _uscan._tcp through the mDNS plugin.
//
// The plugin is a module loaded at run time, and nothing else in the
// application loads it, so this asks for it itself. Refreshing is idempotent.
std::vector<IODeviceInfo> DiscoverOverMdns() {
    std::vector<IODeviceInfo> found;

    UltraNet_RefreshPlugins();
    std::shared_ptr<IUltraNetPlugin> plugin = UltraNet_GetPlugin("mdns");
    auto* directory = dynamic_cast<IDirectoryProtocolPlugin*>(plugin.get());
    if (!directory) {
        // No plugin here. Not an error: a build without it still reaches
        // every scanner named explicitly.
        return found;
    }

    // Both the plain and the TLS service types, since a scanner may offer
    // either or both.
    struct ServiceType { const char* name; bool tls; };
    for (const ServiceType& service : {ServiceType{"_uscan._tcp", false},
                                       ServiceType{"_uscans._tcp", true}}) {
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

            const std::string url = EsclBaseUrlFromMdns(host->second[0], portNumber,
                                                        txt, service.tls);
            if (url.empty()) continue;

            IODeviceInfo info;
            info.deviceId = "escl:" + url;
            info.name = EsclTxtValue(txt, "ty");
            if (info.name.empty()) info.name = entry.dn;
            info.model = EsclTxtValue(txt, "ty");
            info.serialNumber = EsclTxtValue(txt, "uuid");
            info.category = IODeviceCategory::Scanner;
            info.transport = IODeviceTransport::Network;
            info.backend = "eSCL";
            info.connectionPath = url;
            info.location = host->second[0];
            info.attributes["discovery"] = "mdns";
            found.push_back(std::move(info));
        }
    }
    return found;
}

std::vector<IODevicePtr> EnumerateEsclScanners() {
    std::vector<IODeviceInfo> infos = DiscoverOverMdns();

    for (const std::string& url : ExplicitScannerUrls()) {
        const std::string deviceId = "escl:" + url;
        // A scanner named explicitly that mDNS also found is one scanner.
        const bool already = std::any_of(
            infos.begin(), infos.end(),
            [&](const IODeviceInfo& info) { return info.deviceId == deviceId; });
        if (already) continue;

        IODeviceInfo info;
        info.deviceId = deviceId;
        info.name = url;
        info.category = IODeviceCategory::Scanner;
        info.transport = IODeviceTransport::Network;
        info.backend = "eSCL";
        info.connectionPath = url;
        info.attributes["discovery"] = "configured";
        infos.push_back(std::move(info));
    }

    std::vector<IODevicePtr> devices;
    devices.reserve(infos.size());
    for (const IODeviceInfo& info : infos) {
        devices.push_back(std::make_shared<EsclScannerDevice>(info, info.connectionPath));
    }
    return devices;
}

}  // namespace

namespace Internal {

void RegisterEsclScannerBackend(IODeviceManager& manager) {
    manager.RegisterEnumerator(IODeviceCategory::Scanner, "eSCL",
                               EnumerateEsclScanners);
}

}  // namespace Internal
}  // namespace UltraCanvas

#endif  // ULTRACANVAS_HAS_NET
