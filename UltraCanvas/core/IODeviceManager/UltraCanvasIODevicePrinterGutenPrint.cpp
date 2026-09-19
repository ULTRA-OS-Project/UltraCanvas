// core/IODeviceManager/UltraCanvasIODevicePrinterGutenPrint.cpp
// The renderer itself: lay the document out, rasterise it, and pipe it
// through GutenPrint's filter. Model matching and tool discovery live next
// door in ...GutenPrintModels.cpp, which the tests link and this does not
// need to repeat.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "IODeviceManager/UltraCanvasIODevicePrinterGutenPrint.h"

#include "IODeviceManager/UltraCanvasIODevicePrinterJobSource.h"
#include "IODeviceManager/UltraCanvasIODevicePrinterRaster.h"
#include "IODeviceManager/UltraCanvasIODevicePrinterRasterTarget.h"
#include "UltraCanvasUtils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>

namespace UltraCanvas {

namespace {

// Letters and digits, folded to lower case - used here only to name the
// cached PPD file after its model.
std::string NormalizeName(const std::string& text) {
    std::string folded;
    folded.reserve(text.size());
    for (unsigned char c : text) {
        if (std::isalnum(c)) folded.push_back(static_cast<char>(std::tolower(c)));
    }
    return folded;
}

// ============================================================================
// THE RENDERER
// ============================================================================

// The resolution to rasterise at.
//
// Taken from the job when it names one and from a middling default when it
// does not. Higher is not automatically better here: the raster is a full
// page of RGB at this resolution, so 1440 dpi on A4 is about 1.5 GB, and
// GutenPrint resamples to the printer's real grid anyway.
IOResolution ChooseResolution(const IOPrintOptions& options) {
    IOResolution resolution = options.resolutionMode == IOResolutionMode::Custom
                                  ? options.customResolution
                                  : IOResolutionModeDpi(options.resolutionMode);
    if (!resolution.IsValid()) {
        resolution = IOResolution{360, 360};
    }
    // Clamped so an ambitious setting cannot ask for a page that will not fit
    // in memory; GutenPrint still prints at the device's own resolution.
    resolution.dpiX = std::min(std::max(resolution.dpiX, 72), 720);
    resolution.dpiY = std::min(std::max(resolution.dpiY, 72), 720);
    return resolution;
}

// The PPD keyword for a paper size. GutenPrint's PPDs use the PostScript
// names, which are what IOPaperSizeToString already produces for the common
// sizes.
std::string PpdPageSizeName(const IOPageSetup& page) {
    switch (page.paperSize) {
        case IOPaperSize::A3:        return "A3";
        case IOPaperSize::A4:        return "A4";
        case IOPaperSize::A5:        return "A5";
        case IOPaperSize::A6:        return "A6";
        case IOPaperSize::B5:        return "B5";
        case IOPaperSize::Letter:    return "Letter";
        case IOPaperSize::Legal:     return "Legal";
        case IOPaperSize::Tabloid:   return "Tabloid";
        case IOPaperSize::Executive: return "Executive";
        default:                     return std::string();
    }
}

class GutenPrintRenderer : public IPrintRenderer {
public:
    IOPrintRenderer GetKind() const override { return IOPrintRenderer::GutenPrint; }

    // The stream this produces is the printer's own command language, so it
    // is only usable where the transport can carry a raw job. Both of them
    // can - CUPS as application/vnd.cups-raw, the Windows spooler as datatype
    // RAW - which is what makes GutenPrint available on every platform rather
    // than only where CUPS runs.
    bool ProducesRawStream() const override { return true; }

    bool IsAvailable() const override { return Tools().IsComplete(); }

    bool SupportsPrinter(const IODeviceInfo& printer) const override {
        if (!Tools().IsComplete()) return false;
        return !ModelFor(printer).empty();
    }

    IODeviceResult Render(const IODeviceInfo& printer,
                          const IOPrintJob& job,
                          const IOPrinterCapabilities& capabilities,
                          IOPrintPayload& payload) override;

private:
    static const IOGutenPrintTools& Tools() {
        // Looked up once: it walks the file system, and it is asked every
        // time a renderer list is built for any printer.
        static const IOGutenPrintTools tools = FindGutenPrintTools();
        return tools;
    }

    const std::vector<IOGutenPrintModel>& Models() const {
        std::lock_guard<std::mutex> lock(mutex);
        if (!modelsLoaded) {
            const ProcessOutput listing =
                RunProcessCaptured({Tools().driver, "list"}, {});
            if (listing.Succeeded()) {
                models = ParseGutenPrintModels(
                    std::string(listing.standardOutput.begin(),
                                listing.standardOutput.end()));
            }
            modelsLoaded = true;
        }
        return models;
    }

    std::string ModelFor(const IODeviceInfo& printer) const {
        return MatchGutenPrintModel(Models(), printer.manufacturer, printer.model);
    }

    // The PPD for a model, written to a file because the filter takes a path
    // in its environment rather than the text itself. Cached: generating one
    // is a fifth of a second and a megabyte of process, per page otherwise.
    std::string PpdPathFor(const std::string& modelUri) const;

    mutable std::mutex mutex;
    mutable bool modelsLoaded = false;
    mutable std::vector<IOGutenPrintModel> models;
    mutable std::map<std::string, std::string> ppdPaths;
};

std::string GutenPrintRenderer::PpdPathFor(const std::string& modelUri) const {
    std::lock_guard<std::mutex> lock(mutex);

    auto cached = ppdPaths.find(modelUri);
    if (cached != ppdPaths.end()) return cached->second;

    const ProcessOutput ppd =
        RunProcessCaptured({Tools().driver, "cat", modelUri}, {});
    if (!ppd.Succeeded() || ppd.standardOutput.empty()) {
        return std::string();
    }

    std::error_code code;
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path(code) / "ultracanvas-gutenprint";
    std::filesystem::create_directories(directory, code);

    // Named after the model rather than randomly, so a second job for the
    // same printer reuses the file instead of filling the temp directory.
    std::string safeName = NormalizeName(modelUri);
    if (safeName.empty()) safeName = "model";
    const std::filesystem::path path = directory / (safeName + ".ppd");

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return std::string();
    file.write(reinterpret_cast<const char*>(ppd.standardOutput.data()),
               static_cast<std::streamsize>(ppd.standardOutput.size()));
    file.close();
    if (!file) return std::string();

    const std::string result = PathToUtf8(path);
    ppdPaths[modelUri] = result;
    return result;
}

IODeviceResult GutenPrintRenderer::Render(const IODeviceInfo& printer,
                                          const IOPrintJob& job,
                                          const IOPrinterCapabilities& capabilities,
                                          IOPrintPayload& payload) {
    (void)capabilities;

    if (!Tools().IsComplete()) {
        return IODeviceResult::Error(
            IODeviceResultCode::BackendUnavailable,
            "GutenPrint's tools are not installed on this machine");
    }

    const std::string modelUri = ModelFor(printer);
    if (modelUri.empty()) {
        return IODeviceResult::Error(
            IODeviceResultCode::NotSupported,
            "GutenPrint does not recognise this printer model");
    }

    const std::string ppdPath = PpdPathFor(modelUri);
    if (ppdPath.empty()) {
        return IODeviceResult::Error(
            IODeviceResultCode::BackendError,
            "GutenPrint would not describe '" + modelUri + "'");
    }

    // ----- lay the document out -----

    IPrintPageSourcePtr pages;
    std::string contentType;
    IODeviceResult built = MakePageSourceForJob(job, pages, contentType);
    if (!built.success) return built;

    const IOResolution resolution = ChooseResolution(job.options);
    const IOPaperDimensions sheet = job.options.page.GetDimensions();
    if (!sheet.IsValid()) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                     "The job names no usable paper size");
    }

    // Hundredths of a millimetre to points, and to pixels at the chosen
    // resolution. 2540 hundredths of a millimetre is an inch.
    const int widthPoints  = (sheet.widthHundredthsMM * 72 + 1270) / 2540;
    const int heightPoints = (sheet.heightHundredthsMM * 72 + 1270) / 2540;
    const int widthPixels  = widthPoints * resolution.dpiX / 72;
    const int heightPixels = heightPoints * resolution.dpiY / 72;

    RasterPageTarget target(widthPixels, heightPixels,
                            resolution.dpiX, resolution.dpiY);
    if (!target.IsValid()) {
        return IODeviceResult::Error(
            IODeviceResultCode::BackendError,
            "Could not create a page to draw on; this build has no rendering "
            "backend");
    }

    target.BeginPage();
    IODeviceResult prepared = pages->Prepare(target);
    if (!prepared.success) return prepared;

    const int pageCount = pages->GetPageCount();
    const std::vector<int> selected = IOSelectPages(job.pageRange, pageCount);
    if (selected.empty()) {
        return IODeviceResult::Error(
            IODeviceResultCode::InvalidArgument,
            pageCount <= 0 ? "The document has no pages to print"
                           : "The selected pages are not in this document");
    }

    // ----- rasterise -----

    IOCupsRasterPage raster;
    raster.widthPixels = widthPixels;
    raster.heightPixels = heightPixels;
    raster.dpiX = resolution.dpiX;
    raster.dpiY = resolution.dpiY;
    raster.pageWidthPoints = widthPoints;
    raster.pageHeightPoints = heightPoints;
    raster.pageSizeName = PpdPageSizeName(job.options.page);
    raster.colorSpace = job.options.colorMode == IOPrinterColorMode::Grayscale ||
                        job.options.colorMode == IOPrinterColorMode::Monochrome
                            ? IOCupsColorSpace::Gray
                            : IOCupsColorSpace::RGB;
    // Deliberately 1, not the job's copy count. The transport already asks
    // for copies - CUPS_COPIES on a raw job, the spooler's own count on
    // Windows - and putting the number here as well would print the job
    // squared: three copies requested, nine on the paper. Duplex does stay in
    // the header, because a raw stream is opaque to the spooler and only
    // GutenPrint can emit the commands for it.
    raster.copies = 1;
    raster.duplex = job.options.duplex != IODuplexMode::None;
    raster.tumble = job.options.duplex == IODuplexMode::ShortEdge;

    // The whole document is built before the filter runs, because the filter
    // is handed its input as one block. That bounds what can be printed this
    // way: a page of RGB at 360 dpi is about 36 MB, so a long document is
    // gigabytes. Fine for the letters and photographs this path is for, and
    // the limit to lift first if it is ever pointed at a book - by teaching
    // RunProcessCaptured to pull its input a block at a time, after which
    // only one page need exist at once.
    std::vector<uint8_t> stream;
    for (int page : selected) {
        target.BeginPage();
        IODeviceResult drawn = pages->DrawPage(page, target);
        if (!drawn.success) return drawn;

        if (!WriteCupsRasterPageHeader(raster, stream)) {
            return IODeviceResult::Error(IODeviceResultCode::BackendError,
                                         "Could not describe the page to GutenPrint");
        }
        bool rowsOk = true;
        const bool readBack = target.ForEachRow(
            [&](const uint8_t* rgba, int width) {
                rowsOk = WriteCupsRasterRow(rgba, width, raster.colorSpace, stream);
                return rowsOk;
            });
        if (!rowsOk || !readBack) {
            return IODeviceResult::Error(IODeviceResultCode::BackendError,
                                         "Could not write the page's pixels");
        }
    }

    // ----- hand it to GutenPrint -----

    // The filter's arguments are CUPS's filter convention: job id, user,
    // title, copies, options. Copies is 1 because the raster header already
    // carries the count; passing it twice prints the job squared.
    const ProcessOutput filtered = RunProcessCaptured(
        {Tools().filter, "1", "ultracanvas",
         payload.jobName.empty() ? std::string("UltraCanvas document")
                                 : payload.jobName,
         "1", ""},
        stream,
        {{"PPD", ppdPath}});

    if (!filtered.started) {
        return IODeviceResult::Error(IODeviceResultCode::BackendUnavailable,
                                     filtered.error.empty()
                                         ? "GutenPrint's filter could not be run"
                                         : filtered.error);
    }
    if (filtered.exitCode != 0 || filtered.standardOutput.empty()) {
        // The filter's own diagnostics are the only account of what it did not
        // like, so the tail of them goes into the message rather than nowhere.
        std::string detail = filtered.standardError;
        if (detail.size() > 400) detail = detail.substr(detail.size() - 400);
        return IODeviceResult::Error(
            IODeviceResultCode::BackendError,
            "GutenPrint could not render the page" +
                (detail.empty() ? std::string() : ": " + detail));
    }

    payload.data = filtered.standardOutput;
    payload.isRaw = true;       // the printer's own language: send it untouched
    payload.contentType.clear();
    return IODeviceResult::Ok();
}

}  // namespace

IPrintRendererPtr CreateGutenPrintRenderer() {
    return std::make_shared<GutenPrintRenderer>();
}

}  // namespace UltraCanvas
