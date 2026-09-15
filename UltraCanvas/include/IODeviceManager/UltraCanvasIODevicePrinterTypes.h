// include/IODeviceManager/UltraCanvasIODevicePrinterTypes.h
// Printer vocabulary: page setup, print options, the GutenPrint parameter
// model, job description and printer status.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraCanvasIODeviceTypes.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace UltraCanvas {

// ============================================================================
// RENDERER
// ============================================================================
//
// Who turns a page into the bytes a printer understands. This is the choice
// an application makes; the transport that carries those bytes is a property
// of the platform and is picked for it (see IPrintTransport).
//
// GutenPrint is a renderer, not a platform backend. libgutenprint is portable
// C that emits the printer's native command stream and needs no CUPS — only
// its *CUPS driver* is Unix-only. Treating the two as one thing is what
// confined earlier attempts at this module to Linux and macOS.
//
enum class IOPrintRenderer {
    Auto,           // GutenPrint when it knows this model, else Native
    Native,         // the OS driver: CUPS filter chain, or the Windows driver
    GutenPrint,     // libgutenprint → the printer's own command stream → raw job
    IPP             // driverless: the printer renders PWG Raster / PDF itself
};

const char* IOPrintRendererToString(IOPrintRenderer renderer);

// ============================================================================
// PAGE SETUP
// ============================================================================

enum class IOPaperSize {
    Unknown,
    A3, A4, A5, A6,
    B4, B5,
    Letter, Legal, Tabloid, Executive,
    Photo4x6, Photo5x7, Photo8x10,
    Envelope10, EnvelopeDL, EnvelopeC5,
    Custom          // dimensions come from IOPageSetup::customSize
};

// Hundredths of a millimetre, so integer arithmetic stays exact for the
// sizes printers actually quote and no rounding creeps into margins.
struct IOPaperDimensions {
    int widthHundredthsMM = 0;
    int heightHundredthsMM = 0;

    bool IsValid() const {
        return widthHundredthsMM > 0 && heightHundredthsMM > 0;
    }
};

// Nominal dimensions of a standard size, portrait. Zero for Unknown/Custom.
IOPaperDimensions IOPaperSizeDimensions(IOPaperSize size);
const char* IOPaperSizeToString(IOPaperSize size);

// The PWG/IPP self-describing media name for a size ("iso_a4_210x297mm",
// "na_letter_8.5x11in"), which is what IPP and modern CUPS speak. Empty for
// Unknown and Custom.
std::string IOPaperSizeToPwgName(IOPaperSize size);

// The standard size whose nominal portrait dimensions match, or Unknown.
//
// A size is recognised by its measurements rather than by the name it comes
// with, because the name is whatever the printer, the driver or the desktop
// chose to call it - "A4", "iso_a4", "A4 210x297mm" and a localised string
// are all the same sheet. The tolerance absorbs the rounding between a
// printer's own table and the nominal ISO/ANSI figure; a millimetre is wide
// enough for that and far narrower than the gap to the next size.
//
// Portrait only: every caller here learns the orientation separately, from a
// field that states it, so guessing it from a swapped pair would override
// something already known.
IOPaperSize IOPaperSizeFromDimensions(int widthHundredthsMM,
                                      int heightHundredthsMM,
                                      int toleranceHundredthsMM = 100);

enum class IOPrintOrientation {
    Portrait,
    Landscape,
    ReversePortrait,
    ReverseLandscape
};

struct IOPageMargins {
    int leftHundredthsMM = 0;
    int topHundredthsMM = 0;
    int rightHundredthsMM = 0;
    int bottomHundredthsMM = 0;
};

struct IOPageSetup {
    IOPaperSize paperSize = IOPaperSize::A4;
    IOPaperDimensions customSize;       // used when paperSize == Custom
    IOPrintOrientation orientation = IOPrintOrientation::Portrait;
    IOPageMargins margins;
    bool borderless = false;

    // Resolved dimensions, honouring Custom and orientation.
    IOPaperDimensions GetDimensions() const;
};

// ============================================================================
// BASIC PRINT OPTIONS
// ============================================================================

enum class IOPrintQuality {
    Draft,
    Normal,
    High,
    Photo
};

enum class IOPrinterColorMode {
    Auto,
    Color,
    Grayscale,
    Monochrome      // 1-bit, no halftone greys
};

enum class IODuplexMode {
    None,
    LongEdge,       // book binding
    ShortEdge       // notepad binding
};

const char* IOPrintQualityToString(IOPrintQuality quality);
const char* IOPrinterColorModeToString(IOPrinterColorMode mode);
const char* IODuplexModeToString(IODuplexMode mode);

// ============================================================================
// GUTENPRINT PARAMETER MODEL
// ============================================================================
//
// GutenPrint's parameters are not independent: choosing a media type
// constrains which resolutions are legal, which in turn constrains the
// inkset, and so on. The library resolves that in a fixed priority order,
// and IOPrintOptions::ResolveConflicts() applies the same order so a caller
// gets a set the driver will accept rather than a silent substitution.
//
//   1. media type   2. resolution   3. cartridge   4. inkset   5. duplex
//
enum class IOMediaType {
    Auto,
    Plain,
    PlainFast,
    Bond,
    Letterhead,
    Recycled,
    Coated,
    Inkjet,
    PhotoMatte,
    PhotoSemiGloss,
    PhotoGlossy,
    PhotoLuster,
    PhotoPremiumGlossy,
    FineArt,
    Canvas,
    Transparency,
    Envelope,
    CardStock,
    Label,
    CDDVD
};

enum class IOResolutionMode {
    Auto,
    Draft,          // ~180-360 dpi
    Standard,       // ~360-720 dpi
    High,           // ~720-1440 dpi
    Photo,          // ~1440 dpi
    PhotoHighest,   // ~2880 dpi and up
    Custom          // dimensions come from IOPrintOptions::customResolution
};

struct IOResolution {
    int dpiX = 0;
    int dpiY = 0;

    bool IsValid() const { return dpiX > 0 && dpiY > 0; }
};

// Nominal dpi for a mode; zero for Auto and Custom.
IOResolution IOResolutionModeDpi(IOResolutionMode mode);

enum class IOCartridgeType {
    Auto,
    Standard,
    PhotoBlack,
    MatteBlack,
    HighCapacity,
    EcoTank
};

enum class IOInkset {
    Auto,
    CMYK,
    CMYKcm,         // + light cyan, light magenta
    CMYKRB,         // + red, blue
    CMYKcmk,        // + light black
    Photo,
    MatteOnly,
    Monochrome
};

const char* IOMediaTypeToString(IOMediaType media);
const char* IOResolutionModeToString(IOResolutionMode mode);
const char* IOInksetToString(IOInkset inkset);
const char* IOCartridgeTypeToString(IOCartridgeType cartridge);

// ============================================================================
// PRINT OPTIONS
// ============================================================================

struct IOPrintOptions {
    IOPrintRenderer renderer = IOPrintRenderer::Auto;

    IOPageSetup page;
    IOPrintQuality quality = IOPrintQuality::Normal;
    IOPrinterColorMode colorMode = IOPrinterColorMode::Auto;
    IODuplexMode duplex = IODuplexMode::None;
    int copies = 1;
    bool collate = true;

    // GutenPrint parameters, in the priority order documented above. They
    // apply when the renderer is GutenPrint; under Native the subset with a
    // PPD or DEVMODE equivalent is mapped across and the rest is reported
    // NotSupported rather than silently dropped.
    IOMediaType mediaType = IOMediaType::Auto;
    IOResolutionMode resolutionMode = IOResolutionMode::Auto;
    IOResolution customResolution;
    IOCartridgeType cartridge = IOCartridgeType::Auto;
    IOInkset inkset = IOInkset::Auto;

    // Escape hatches for options with no typed equivalent. Passed through to
    // the backend untouched: PPD keywords for CUPS, IPP attribute names for
    // IPP. Prefer the typed fields; these exist so an unusual printer is not
    // a blocker.
    std::map<std::string, std::string> backendOptions;

    static IOPrintOptions Default() { return IOPrintOptions(); }
    static IOPrintOptions PhotoQuality();
    static IOPrintOptions FastDraft();
};

// ============================================================================
// PRINTER CAPABILITIES
// ============================================================================

struct IOPrinterCapabilities {
    std::vector<IOPaperSize> paperSizes;
    std::vector<IOPrintQuality> qualities;
    std::vector<IOMediaType> mediaTypes;
    std::vector<IOResolutionMode> resolutionModes;
    std::vector<IOInkset> inksets;

    IOSupport supportsColor = IOSupport::Unknown;
    IOSupport supportsDuplex = IOSupport::Unknown;
    IOSupport supportsCollate = IOSupport::Unknown;
    IOSupport supportsBorderless = IOSupport::Unknown;

    // Zero means the printer did not say, so nothing is clamped against it.
    int maxCopies = 0;

    IOPaperDimensions minCustomSize;
    IOPaperDimensions maxCustomSize;

    bool Supports(IOPaperSize size) const;
    bool Supports(IOPrintQuality quality) const;
    bool Supports(IOMediaType media) const;
    bool Supports(IOResolutionMode mode) const;
    bool Supports(IOInkset inkset) const;
};

// ============================================================================
// OPTION RESOLUTION
// ============================================================================

// Folds `requested` down to something `capabilities` actually admits, in
// GutenPrint's priority order: media type first, then resolution, cartridge,
// inkset and duplex, each constrained by the choices above it. A caller gets
// back the set the driver will accept instead of discovering the
// substitution in the output tray.
//
// Every change is appended to `changes` (when non-null) in words meant for a
// user - "A3 is not supported, using A4" - so a print dialog can say what it
// had to alter rather than silently altering it.
IOPrintOptions ResolvePrintOptions(const IOPrintOptions& requested,
                                   const IOPrinterCapabilities& capabilities,
                                   std::vector<std::string>* changes = nullptr);

// ============================================================================
// JOBS
// ============================================================================

// "1-3,5,8-9" from a list of 1-based page numbers: consecutive runs become
// ranges, the list is sorted and duplicates collapse. This is the form IPP's
// page-ranges attribute takes and the form every print dialog shows, so it is
// also what a caller would have to build by hand otherwise. Empty list gives
// an empty string, which every consumer reads as "all pages".
//
// Pages below 1 are dropped rather than clamped: a 0 or a negative in a page
// list is a bug in the caller, and clamping it to page 1 would print a page
// nobody asked for.
std::string IOFormatPageRanges(const std::vector<int>& pages);

// The 0-based page indices of a `pageCount`-page document that a 1-based
// `pageRange` selects, in order and without duplicates. An empty range
// selects every page, which is what every layer here means by "no range".
//
// Pages past the end of the document are dropped rather than refused: a
// dialog showing "1-9999" for a range the user typed before the document was
// paginated is ordinary, and failing the job over it would be worse than
// printing the pages that do exist. A range that selects *nothing* comes back
// empty, which callers do treat as an error - it means the user asked for
// pages this document does not have at all.
std::vector<int> IOSelectPages(const std::vector<int>& pageRange, int pageCount);

struct IOPrintJob {
    std::string jobName;

    // Exactly one source: a file on disk, or bytes in memory. When both are
    // set, filePath wins.
    std::string filePath;
    std::vector<uint8_t> data;
    std::string mimeType;           // "application/pdf", "image/png", ...

    IOPrintOptions options;
    std::vector<int> pageRange;     // empty = every page

    bool IsValid() const { return !filePath.empty() || !data.empty(); }
};

// ============================================================================
// WHAT A PRINT DIALOG CHOSE
// ============================================================================

// The settings a user picked in the OS print dialog.
//
// It lives here, in the printer vocabulary, rather than beside the other
// dialog result types, for a plain reason: a print dialog exists to produce a
// print job, so its answer is already IOPrintOptions plus the queue to send
// it to, and any other shape would be a second vocabulary to translate. The
// dialog header names this NativePrintResult, alongside NativeInputResult and
// the rest, and that name is an alias for this type.
//
// Keeping the definition on this side is also what lets the printing stack
// use it at all: the dialog header reaches the whole widget and window stack
// through UltraCanvasModalDialog.h, and IODeviceManager depends on no UI.
struct IOPrintDialogChoice {
    // The user confirmed the dialog. A plain bool rather than the dialog
    // system's DialogResult, which carries Yes/No/Retry answers a print
    // dialog never gives - and whose header pulls in every widget.
    bool accepted = false;

    // The queue as the platform names it - a CUPS destination name, a Windows
    // printer name. This is IODeviceInfo::connectionPath for both the CUPS and
    // Windows spooler backends, which is how a chosen name finds its device
    // again. Empty when the dialog was cancelled.
    std::string printerName;

    IOPrintOptions options;

    // 1-based pages the user asked for; empty means the whole document.
    std::vector<int> pageRange;

    // The user chose the desktop's "Print to File" destination rather than a
    // queue. Reported instead of quietly ignored: a caller acting on
    // printerName alone would otherwise spool to a device nobody picked.
    bool printToFile = false;
    std::string outputFilePath;

    bool IsOK() const { return accepted; }
    bool IsCancelled() const { return !accepted; }
    explicit operator bool() const { return accepted; }
};

enum class IOPrintJobState {
    Unknown,
    Pending,
    Held,
    Processing,
    Stopped,
    Completed,
    Cancelled,
    Aborted
};

const char* IOPrintJobStateToString(IOPrintJobState state);

struct IOPrintJobStatus {
    int jobId = 0;
    IOPrintJobState state = IOPrintJobState::Unknown;
    std::string stateReason;        // "media-jam", "toner-low", "none"
    std::string jobName;
    std::string user;
    int pagesPrinted = 0;
    int pagesTotal = 0;

    bool IsFinished() const {
        return state == IOPrintJobState::Completed ||
               state == IOPrintJobState::Cancelled ||
               state == IOPrintJobState::Aborted;
    }
};

// ============================================================================
// SUPPLIES AND STATUS
// ============================================================================

enum class IOSupplyType {
    Unknown,
    Ink,
    Toner,
    Drum,
    Fuser,
    WasteTank,
    Staples,
    Paper
};

enum class IOSupplyColor {
    None,
    Black,
    Cyan,
    Magenta,
    Yellow,
    LightCyan,
    LightMagenta,
    LightBlack,
    Red,
    Blue,
    Gray,
    PhotoBlack,
    MatteBlack
};

struct IOSupplyLevel {
    IOSupplyType type = IOSupplyType::Unknown;
    IOSupplyColor color = IOSupplyColor::None;
    std::string description;

    // 0-100, or -1 when the printer does not report a level. A printer that
    // says nothing must not be shown as empty, so this stays -1 rather than
    // defaulting to zero.
    int percentRemaining = -1;

    bool IsLow() const { return percentRemaining >= 0 && percentRemaining <= 10; }
    bool IsKnown() const { return percentRemaining >= 0; }
};

const char* IOSupplyTypeToString(IOSupplyType type);
const char* IOSupplyColorToString(IOSupplyColor color);

enum class IOPrinterState {
    Unknown,
    Idle,
    Printing,
    Stopped
};

struct IOPrinterStatus {
    IOPrinterState state = IOPrinterState::Unknown;
    std::string stateReason;        // "none", "media-empty", "door-open"
    bool acceptingJobs = false;
    int jobsQueued = 0;
    std::vector<IOSupplyLevel> supplies;

    bool IsReady() const {
        return state == IOPrinterState::Idle && acceptingJobs;
    }
};

const char* IOPrinterStateToString(IOPrinterState state);

} // namespace UltraCanvas
