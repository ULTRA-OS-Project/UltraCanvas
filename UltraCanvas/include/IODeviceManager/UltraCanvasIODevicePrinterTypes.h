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

// Three-valued on purpose. A plain bool cannot tell "this printer has no
// duplex unit" from "we could not read this printer's capabilities", and
// conflating them silently strips options from a printer that would have
// accepted them. Only `No` constrains anything; `Unknown` leaves the caller's
// choice alone, which is the same rule the lists below follow by being empty.
enum class IOSupport {
    Unknown,
    No,
    Yes
};

// True only when the printer actually said no.
inline bool Denies(IOSupport support) { return support == IOSupport::No; }

inline IOSupport IOSupportFrom(bool value) {
    return value ? IOSupport::Yes : IOSupport::No;
}

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
