// OS/MSWindows/UltraCanvasWindowsIODevicePrinterGdi.cpp
// Native printing on Windows: the GDI renderer and the device context it
// draws onto.
//
// This is the half of Windows printing the spooler backend cannot do. The
// spooler takes device-ready bytes - which is how GutenPrint reaches a
// printer here - but it will not take a PDF or a PNG and work out what to do
// with it the way CUPS's filter chain does. The platform's answer is to draw:
// open a printer DC, StartDoc, then for each page StartPage, issue GDI calls,
// EndPage, and the driver turns those calls into the device's own commands.
//
// So the "renderer" produces pages rather than bytes (IPrintPageSource) and
// the transport hands them here. The page *layout* - fitting, wrapping,
// pagination - deliberately lives in core/ against the abstract
// IPrintPageTarget, so it is tested on any platform; what is left in this
// file is only what genuinely needs Win32.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#ifdef _WIN32

#include "UltraCanvasWindowsIODevicePrinterGdi.h"

#include "../../include/IODeviceManager/UltraCanvasIODevicePrinterPage.h"
#include "../../include/UltraCanvasRasterDocument.h"
#include "../../include/UltraCanvasRasterLayer.h"
#include "../../include/UltraCanvasUtils.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace Internal {
namespace {

// ============================================================================
// SMALL HELPERS
// ============================================================================

std::string GdiErrorText(DWORD error) {
    LPWSTR buffer = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);

    std::string message;
    if (length && buffer) {
        message = WideToUtf8(std::wstring(buffer, length));
        while (!message.empty() &&
               (message.back() == '\n' || message.back() == '\r')) {
            message.pop_back();
        }
    }
    if (buffer) {
        LocalFree(buffer);
    }
    if (message.empty()) {
        message = "Windows error " + std::to_string(error);
    }
    return message;
}

std::string LowerExtension(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return std::string();
    }
    std::string ext = path.substr(dot + 1);
    for (char& c : ext) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return ext;
}

bool StartsWith(const std::string& text, const char* prefix) {
    const size_t length = std::char_traits<char>::length(prefix);
    return text.size() >= length && text.compare(0, length, prefix) == 0;
}

// ============================================================================
// DEVMODE
// ============================================================================

// Nominal sizes are in hundredths of a millimetre; DEVMODE wants tenths.
short ToTenthsMM(int hundredthsMM) {
    return static_cast<short>((hundredthsMM + 5) / 10);
}

short PaperSizeToDmPaper(IOPaperSize size) {
    switch (size) {
        case IOPaperSize::A3:          return DMPAPER_A3;
        case IOPaperSize::A4:          return DMPAPER_A4;
        case IOPaperSize::A5:          return DMPAPER_A5;
        case IOPaperSize::A6:          return DMPAPER_A6;
        case IOPaperSize::B4:          return DMPAPER_B4;
        case IOPaperSize::B5:          return DMPAPER_B5;
        case IOPaperSize::Letter:      return DMPAPER_LETTER;
        case IOPaperSize::Legal:       return DMPAPER_LEGAL;
        case IOPaperSize::Tabloid:     return DMPAPER_TABLOID;
        case IOPaperSize::Executive:   return DMPAPER_EXECUTIVE;
        case IOPaperSize::Envelope10:  return DMPAPER_ENV_10;
        case IOPaperSize::EnvelopeDL:  return DMPAPER_ENV_DL;
        case IOPaperSize::EnvelopeC5:  return DMPAPER_ENV_C5;
        // The photo sizes and Custom have no DMPAPER_ constant, so they go
        // through dmPaperWidth/dmPaperLength instead. Returning 0 says so.
        default:                       return 0;
    }
}

// Starts from the driver's own current defaults rather than a zeroed struct,
// so every setting this code does not touch keeps whatever the user
// configured in the printer's properties. The second DocumentPropertiesW call
// lets the driver validate and normalise what was changed - a duplex request
// to a printer without a duplexer is dropped by the driver rather than
// silently producing single-sided output the caller thinks is duplex.
std::vector<uint8_t> BuildDevMode(const std::wstring& printerName,
                                  HANDLE printerHandle,
                                  const IOPrintOptions& options) {
    std::vector<uint8_t> buffer;

    const LONG needed = DocumentPropertiesW(
        nullptr, printerHandle, const_cast<LPWSTR>(printerName.c_str()),
        nullptr, nullptr, 0);
    if (needed <= 0) {
        return buffer;   // caller falls back to the driver defaults
    }

    buffer.resize(static_cast<size_t>(needed));
    DEVMODEW* devMode = reinterpret_cast<DEVMODEW*>(buffer.data());
    if (DocumentPropertiesW(nullptr, printerHandle,
                            const_cast<LPWSTR>(printerName.c_str()), devMode,
                            nullptr, DM_OUT_BUFFER) != IDOK) {
        buffer.clear();
        return buffer;
    }

    const IOPaperDimensions paper = options.page.GetDimensions();
    const short dmPaper = PaperSizeToDmPaper(options.page.paperSize);
    if (dmPaper != 0) {
        devMode->dmPaperSize = dmPaper;
        devMode->dmFields |= DM_PAPERSIZE;
    } else if (paper.IsValid()) {
        // dmPaperSize wins over the explicit measurements unless it is set
        // to DMPAPER_USER, so a photo size left at the driver's inherited
        // A4 would quietly print A4.
        devMode->dmPaperSize = DMPAPER_USER;
        devMode->dmPaperWidth = ToTenthsMM(paper.widthHundredthsMM);
        devMode->dmPaperLength = ToTenthsMM(paper.heightHundredthsMM);
        devMode->dmFields |= DM_PAPERSIZE | DM_PAPERWIDTH | DM_PAPERLENGTH;
    }

    // DEVMODE has only two orientations. The reverse forms rotate by 180
    // degrees, which GDI expresses as a page transform rather than a paper
    // setting, so they map onto their plain counterparts here and the
    // difference is lost - better than refusing the job over it.
    devMode->dmOrientation =
        (options.page.orientation == IOPrintOrientation::Landscape ||
         options.page.orientation == IOPrintOrientation::ReverseLandscape)
            ? DMORIENT_LANDSCAPE
            : DMORIENT_PORTRAIT;
    devMode->dmFields |= DM_ORIENTATION;

    if (options.copies > 0) {
        devMode->dmCopies = static_cast<short>(std::min(options.copies, 9999));
        devMode->dmFields |= DM_COPIES;
        devMode->dmCollate = options.collate ? DMCOLLATE_TRUE : DMCOLLATE_FALSE;
        devMode->dmFields |= DM_COLLATE;
    }

    switch (options.colorMode) {
        case IOPrinterColorMode::Grayscale:
        case IOPrinterColorMode::Monochrome:
            devMode->dmColor = DMCOLOR_MONOCHROME;
            devMode->dmFields |= DM_COLOR;
            break;
        case IOPrinterColorMode::Color:
            devMode->dmColor = DMCOLOR_COLOR;
            devMode->dmFields |= DM_COLOR;
            break;
        case IOPrinterColorMode::Auto:
            break;   // leave the driver's own default alone
    }

    switch (options.duplex) {
        case IODuplexMode::None:
            devMode->dmDuplex = DMDUP_SIMPLEX;
            devMode->dmFields |= DM_DUPLEX;
            break;
        case IODuplexMode::LongEdge:
            devMode->dmDuplex = DMDUP_VERTICAL;
            devMode->dmFields |= DM_DUPLEX;
            break;
        case IODuplexMode::ShortEdge:
            devMode->dmDuplex = DMDUP_HORIZONTAL;
            devMode->dmFields |= DM_DUPLEX;
            break;
    }

    // dmPrintQuality doubles as a DPI field: positive values are an explicit
    // horizontal resolution, negative ones are the DMRES_ quality bands.
    switch (options.quality) {
        case IOPrintQuality::Draft:  devMode->dmPrintQuality = DMRES_DRAFT;  break;
        case IOPrintQuality::Normal: devMode->dmPrintQuality = DMRES_MEDIUM; break;
        case IOPrintQuality::High:   devMode->dmPrintQuality = DMRES_HIGH;   break;
        case IOPrintQuality::Photo:  devMode->dmPrintQuality = DMRES_HIGH;   break;
    }
    devMode->dmFields |= DM_PRINTQUALITY;

    // Hand it back to the driver to validate what was just changed.
    if (DocumentPropertiesW(nullptr, printerHandle,
                            const_cast<LPWSTR>(printerName.c_str()), devMode,
                            devMode, DM_IN_BUFFER | DM_OUT_BUFFER) != IDOK) {
        buffer.clear();
    }
    return buffer;
}

// ============================================================================
// PAGE TARGET
// ============================================================================

// Draws onto a printer device context. Fonts are cached per pixel height
// because a text page asks for metrics once per line and creating a font per
// call would dominate the cost of printing a page of text.
class WindowsGdiPageTarget : public IPrintPageTarget {
public:
    explicit WindowsGdiPageTarget(HDC dc) : dc(dc) {}

    ~WindowsGdiPageTarget() override {
        if (originalFont) {
            SelectObject(dc, originalFont);
        }
        for (auto& entry : fonts) {
            DeleteObject(entry.second);
        }
    }

    IOPrintPageMetrics GetMetrics() const override {
        IOPrintPageMetrics metrics;
        metrics.widthDots = GetDeviceCaps(dc, HORZRES);
        metrics.heightDots = GetDeviceCaps(dc, VERTRES);
        metrics.dpiX = GetDeviceCaps(dc, LOGPIXELSX);
        metrics.dpiY = GetDeviceCaps(dc, LOGPIXELSY);
        metrics.offsetXDots = GetDeviceCaps(dc, PHYSICALOFFSETX);
        metrics.offsetYDots = GetDeviceCaps(dc, PHYSICALOFFSETY);
        return metrics;
    }

    bool DrawImage(const uint8_t* pixels, int width, int height,
                   const IOPrintRect& dest) override {
        if (!pixels || width <= 0 || height <= 0 || dest.IsEmpty()) {
            return false;
        }

        // Composited onto white rather than alpha-blended. Paper is opaque
        // and already white, so a transparent PNG must print as if it were
        // laid on the sheet; AlphaBlend against an uninitialised page would
        // blend against whatever the driver happens to have there.
        std::vector<uint8_t> bgra(static_cast<size_t>(width) *
                                  static_cast<size_t>(height) * 4u);
        for (size_t i = 0, n = static_cast<size_t>(width) *
                               static_cast<size_t>(height);
             i < n; ++i) {
            const uint8_t r = pixels[i * 4 + 0];
            const uint8_t g = pixels[i * 4 + 1];
            const uint8_t b = pixels[i * 4 + 2];
            const unsigned a = pixels[i * 4 + 3];
            // Rounded rather than truncated: over a large flat area the
            // half-bit of consistent bias is a visible tint.
            bgra[i * 4 + 0] =
                static_cast<uint8_t>((b * a + 255 * (255 - a) + 127) / 255);
            bgra[i * 4 + 1] =
                static_cast<uint8_t>((g * a + 255 * (255 - a) + 127) / 255);
            bgra[i * 4 + 2] =
                static_cast<uint8_t>((r * a + 255 * (255 - a) + 127) / 255);
            bgra[i * 4 + 3] = 255;
        }

        BITMAPINFO info = {};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = width;
        // Negative: the buffer is top-down, which is how every decoder in
        // this repository hands pixels over. A positive height would print
        // the image upside down.
        info.bmiHeader.biHeight = -height;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        const int previousMode = SetStretchBltMode(dc, HALFTONE);
        // HALFTONE stretching is the only mode that resamples rather than
        // dropping rows, and it needs the brush origin reset after use or
        // later fills are misaligned.
        SetBrushOrgEx(dc, 0, 0, nullptr);

        const int written =
            StretchDIBits(dc, dest.x, dest.y, dest.width, dest.height, 0, 0,
                          width, height, bgra.data(), &info, DIB_RGB_COLORS,
                          SRCCOPY);

        SetStretchBltMode(dc, previousMode);
        // StretchDIBits returns the number of scan lines it copied; both 0
        // and GDI_ERROR mean nothing reached the page.
        return written > 0;
    }

    bool DrawTextLine(const std::string& utf8, int x, int baselineY,
                      int pixelHeight) override {
        if (!UseFont(pixelHeight)) {
            return false;
        }
        const std::wstring wide = Utf8ToWide(utf8);
        SetTextAlign(dc, TA_LEFT | TA_BASELINE);
        SetBkMode(dc, TRANSPARENT);
        return TextOutW(dc, x, baselineY, wide.c_str(),
                        static_cast<int>(wide.size())) != FALSE;
    }

    int MeasureTextWidth(const std::string& utf8,
                         int pixelHeight) const override {
        if (!UseFont(pixelHeight)) {
            return 0;
        }
        const std::wstring wide = Utf8ToWide(utf8);
        SIZE size = {};
        if (!GetTextExtentPoint32W(dc, wide.c_str(),
                                   static_cast<int>(wide.size()), &size)) {
            return 0;
        }
        return size.cx;
    }

    int GetLineHeight(int pixelHeight) const override {
        TEXTMETRICW metrics = {};
        if (!Metrics(pixelHeight, metrics)) {
            return 0;
        }
        return metrics.tmHeight + metrics.tmExternalLeading;
    }

    int GetAscent(int pixelHeight) const override {
        TEXTMETRICW metrics = {};
        if (!Metrics(pixelHeight, metrics)) {
            return 0;
        }
        return metrics.tmAscent;
    }

private:
    bool Metrics(int pixelHeight, TEXTMETRICW& out) const {
        if (!UseFont(pixelHeight)) {
            return false;
        }
        return GetTextMetricsW(dc, &out) != FALSE;
    }

    // const because measuring is logically a query: what changes is the
    // cache and which font the DC has selected, neither of which is part of
    // the target's observable state.
    bool UseFont(int pixelHeight) const {
        if (pixelHeight <= 0) {
            return false;
        }
        auto found = fonts.find(pixelHeight);
        if (found == fonts.end()) {
            LOGFONTW description = {};
            // Negative height asks for that *character* height rather than
            // that cell height, which is what a caller measuring text means.
            description.lfHeight = -pixelHeight;
            description.lfWeight = FW_NORMAL;
            description.lfCharSet = DEFAULT_CHARSET;
            description.lfQuality = PROOF_QUALITY;
            wcscpy_s(description.lfFaceName, L"Segoe UI");

            HFONT font = CreateFontIndirectW(&description);
            if (!font) {
                return false;
            }
            found = fonts.emplace(pixelHeight, font).first;
        }

        if (currentHeight != pixelHeight) {
            HGDIOBJ previous = SelectObject(dc, found->second);
            if (!originalFont) {
                originalFont = previous;
            }
            currentHeight = pixelHeight;
        }
        return true;
    }

    HDC dc = nullptr;
    mutable std::map<int, HFONT> fonts;
    mutable HGDIOBJ originalFont = nullptr;
    mutable int currentHeight = 0;
};

// ============================================================================
// RENDERER
// ============================================================================

// Turns a job into pages. What it can paginate is deliberately narrow for
// now - raster images and plain text - and anything else is refused by name
// rather than half-printed. A caller finds that out from Print() returning
// NotSupported with the type in the message, which is recoverable; the
// failure mode this design exists to avoid is a job that silently vanishes.
class WindowsGdiRenderer : public IPrintRenderer {
public:
    IOPrintRenderer GetKind() const override { return IOPrintRenderer::Native; }

    // GDI is part of the operating system; there is no library to look for.
    bool IsAvailable() const override { return true; }

    // Every printer Windows will show has a driver behind it, and the driver
    // is what consumes the drawing calls - so unlike GutenPrint, which knows
    // only the models it has a definition for, this is not a per-model
    // question.
    bool SupportsPrinter(const IODeviceInfo& printer) const override {
        (void)printer;
        return true;
    }

    bool ProducesPageSource() const override { return true; }

    IODeviceResult Render(const IOPrintJob& job,
                          const IOPrinterCapabilities& capabilities,
                          IOPrintPayload& payload) override {
        (void)capabilities;

        const std::string type = ResolveType(job);
        if (type.empty()) {
            return IODeviceResult::Error(
                IODeviceResultCode::InvalidArgument,
                "The job says nothing about its type, and its name carries no "
                "extension to infer one from");
        }

        if (type == "text") {
            return RenderText(job, payload);
        }
        if (type == "image") {
            return RenderImage(job, payload);
        }

        return IODeviceResult::Error(
            IODeviceResultCode::NotSupported,
            "The Windows GDI renderer prints images and plain text; '" + type +
                "' needs a renderer that can paginate it. Printing it as a "
                "device-native stream (GutenPrint) is unaffected.");
    }

private:
    // Uses the declared MIME type when there is one and the file extension
    // when there is not, because PrintFile() sets no MIME type and asking
    // every caller to is how a print button ends up refusing valid files.
    static std::string ResolveType(const IOPrintJob& job) {
        if (!job.mimeType.empty()) {
            if (StartsWith(job.mimeType, "image/")) {
                return "image";
            }
            if (StartsWith(job.mimeType, "text/plain")) {
                return "text";
            }
            return job.mimeType;
        }

        const std::string extension = LowerExtension(job.filePath);
        if (extension.empty()) {
            return std::string();
        }
        if (extension == "txt" || extension == "log" || extension == "md") {
            return "text";
        }
        if (extension == "png" || extension == "jpg" || extension == "jpeg" ||
            extension == "bmp" || extension == "gif" || extension == "tif" ||
            extension == "tiff" || extension == "webp" || extension == "qoi") {
            return "image";
        }
        return extension;
    }

    static IODeviceResult RenderText(const IOPrintJob& job,
                                     IOPrintPayload& payload) {
        std::string text;
        if (!job.data.empty()) {
            text.assign(job.data.begin(), job.data.end());
        } else {
            FILE* file = nullptr;
            if (_wfopen_s(&file, Utf8ToWide(job.filePath).c_str(), L"rb") != 0 ||
                !file) {
                return IODeviceResult::Error(
                    IODeviceResultCode::IOError,
                    "Could not open '" + job.filePath + "' to print it");
            }
            char buffer[8192];
            size_t read = 0;
            while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0) {
                text.append(buffer, read);
            }
            std::fclose(file);
        }

        payload.pages = std::make_shared<TextPageSource>(std::move(text));
        payload.contentType = "text/plain";
        return IODeviceResult::Ok();
    }

    static IODeviceResult RenderImage(const IOPrintJob& job,
                                      IOPrintPayload& payload) {
        if (job.filePath.empty()) {
            // Decoding from memory would mean writing the bytes out and
            // reading them back, since the document loader is path-based.
            // Saying so beats doing it silently behind the caller's back.
            return IODeviceResult::Error(
                IODeviceResultCode::NotImplemented,
                "Printing an image from memory is not wired up yet; print it "
                "from a file");
        }

        UCRasterDocument document;
        std::string error;
        if (!document.LoadFromFile(job.filePath, error)) {
            return IODeviceResult::Error(
                IODeviceResultCode::MediaError,
                "Could not decode '" + job.filePath + "': " +
                    (error.empty() ? std::string("unsupported image") : error));
        }

        std::shared_ptr<UCRasterLayer> layer = document.GetLayer(0);
        if (!layer || !layer->IsValid()) {
            return IODeviceResult::Error(
                IODeviceResultCode::MediaError,
                "'" + job.filePath + "' decoded to nothing printable");
        }

        payload.pages = std::make_shared<ImagePageSource>(
            layer->Data(), layer->GetWidth(), layer->GetHeight());
        payload.contentType = "image/x-raster";
        return IODeviceResult::Ok();
    }
};

}  // namespace

// ============================================================================
// ENTRY POINTS
// ============================================================================

IPrintRendererPtr CreateWindowsGdiRenderer() {
    return std::make_shared<WindowsGdiRenderer>();
}

IODeviceResult PrintPageSourceThroughGdi(const IODeviceInfo& printer,
                                         const IPrintPageSourcePtr& pages,
                                         const IOPrintOptions& options,
                                         const std::string& jobName,
                                         int& outJobId) {
    if (!pages) {
        return IODeviceResult::Error(IODeviceResultCode::InvalidArgument,
                                     "No pages to print", printer.deviceId);
    }

    const std::wstring name = Utf8ToWide(printer.connectionPath);

    // The DEVMODE has to come from an open printer handle, but the DC is
    // created from the name: opening the handle only to read the driver's
    // defaults and closing it again keeps the handle's lifetime to the
    // shortest thing that needs it.
    std::vector<uint8_t> devModeBuffer;
    HANDLE printerHandle = nullptr;
    if (OpenPrinterW(const_cast<LPWSTR>(name.c_str()), &printerHandle, nullptr)) {
        devModeBuffer = BuildDevMode(name, printerHandle, options);
        ClosePrinter(printerHandle);
    }

    DEVMODEW* devMode =
        devModeBuffer.empty() ? nullptr
                              : reinterpret_cast<DEVMODEW*>(devModeBuffer.data());

    HDC dc = CreateDCW(nullptr, name.c_str(), nullptr, devMode);
    if (!dc) {
        const DWORD error = GetLastError();
        return IODeviceResult::BackendError(
            IODeviceResultCode::DeviceNotFound,
            "Could not open a device context for '" + printer.connectionPath +
                "': " + GdiErrorText(error),
            static_cast<int>(error), printer.deviceId);
    }

    IODeviceResult result = IODeviceResult::Ok(printer.deviceId);
    int jobId = 0;

    // Scoped: the target holds fonts selected into this DC and deselects
    // them in its destructor, so it has to be gone before DeleteDC. Left at
    // function scope it would run that destructor against a destroyed DC.
    {
        WindowsGdiPageTarget target(dc);

        // Pagination needs the device, so it happens here rather than in the
        // renderer: the same document is a different number of pages on A4 at
        // 600 dpi than on Letter at 300.
        result = pages->Prepare(target);
        if (result.success) {
            const int pageCount = pages->GetPageCount();
            if (pageCount <= 0) {
                result = IODeviceResult::Error(
                    IODeviceResultCode::InvalidArgument,
                    "The document has no pages to print", printer.deviceId);
            } else {
                const std::wstring wideJobName = Utf8ToWide(
                    jobName.empty() ? std::string("UltraCanvas document")
                                    : jobName);

                DOCINFOW docInfo = {};
                docInfo.cbSize = sizeof(docInfo);
                docInfo.lpszDocName = wideJobName.c_str();

                jobId = StartDocW(dc, &docInfo);
                if (jobId <= 0) {
                    const DWORD error = GetLastError();
                    result = IODeviceResult::BackendError(
                        IODeviceResultCode::BackendError,
                        "The spooler refused the job: " + GdiErrorText(error),
                        static_cast<int>(error), printer.deviceId);
                } else {
                    for (int page = 0; page < pageCount; ++page) {
                        if (StartPage(dc) <= 0) {
                            const DWORD error = GetLastError();
                            result = IODeviceResult::BackendError(
                                IODeviceResultCode::BackendError,
                                "The spooler would not start page " +
                                    std::to_string(page + 1) + ": " +
                                    GdiErrorText(error),
                                static_cast<int>(error), printer.deviceId);
                            break;
                        }

                        IODeviceResult drawn = pages->DrawPage(page, target);

                        // EndPage runs even for a page that failed to draw, so
                        // the context is left in a state AbortDoc can unwind.
                        EndPage(dc);

                        if (!drawn.success) {
                            drawn.deviceId = printer.deviceId;
                            result = drawn;
                            break;
                        }
                    }

                    if (result.success) {
                        EndDoc(dc);
                        outJobId = jobId;
                    } else {
                        // Abort rather than end: a partial document on paper
                        // is worse than none, because the user has to work
                        // out which pages are missing from a stack that looks
                        // complete.
                        AbortDoc(dc);
                    }
                }
            }
        } else {
            result.deviceId = printer.deviceId;
        }
    }

    DeleteDC(dc);
    return result;
}

}  // namespace Internal
}  // namespace UltraCanvas

#endif  // _WIN32
