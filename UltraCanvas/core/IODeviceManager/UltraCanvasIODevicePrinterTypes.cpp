// core/IODeviceManager/UltraCanvasIODevicePrinterTypes.cpp
// Printer vocabulary: paper tables, enum names, and the option resolver that
// folds a requested option set down to what a printer will actually accept.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "../../include/IODeviceManager/UltraCanvasIODevicePrinterTypes.h"

#include <algorithm>

namespace UltraCanvas {

// ============================================================================
// PAPER
// ============================================================================

namespace {

struct PaperEntry {
    IOPaperSize size;
    int widthHundredthsMM;
    int heightHundredthsMM;
    const char* name;
    const char* pwgName;
};

// Portrait dimensions. Imperial sizes are the exact inch measure converted,
// not a rounded millimetre figure, so a Letter page is 21.59 x 27.94 cm
// rather than 21.6 x 27.9 and margins still add up at 1200 dpi.
const PaperEntry kPaperTable[] = {
    {IOPaperSize::A3,          29700, 42000, "A3",          "iso_a3_297x420mm"},
    {IOPaperSize::A4,          21000, 29700, "A4",          "iso_a4_210x297mm"},
    {IOPaperSize::A5,          14800, 21000, "A5",          "iso_a5_148x210mm"},
    {IOPaperSize::A6,          10500, 14800, "A6",          "iso_a6_105x148mm"},
    {IOPaperSize::B4,          25000, 35300, "B4",          "iso_b4_250x353mm"},
    {IOPaperSize::B5,          17600, 25000, "B5",          "iso_b5_176x250mm"},
    {IOPaperSize::Letter,      21590, 27940, "Letter",      "na_letter_8.5x11in"},
    {IOPaperSize::Legal,       21590, 35560, "Legal",       "na_legal_8.5x14in"},
    {IOPaperSize::Tabloid,     27940, 43180, "Tabloid",     "na_ledger_11x17in"},
    {IOPaperSize::Executive,   18415, 26670, "Executive",   "na_executive_7.25x10.5in"},
    {IOPaperSize::Photo4x6,    10160, 15240, "4x6 Photo",   "na_index-4x6_4x6in"},
    {IOPaperSize::Photo5x7,    12700, 17780, "5x7 Photo",   "na_5x7_5x7in"},
    {IOPaperSize::Photo8x10,   20320, 25400, "8x10 Photo",  "na_govt-letter_8x10in"},
    {IOPaperSize::Envelope10,  10478, 24130, "Envelope #10","na_number-10_4.125x9.5in"},
    {IOPaperSize::EnvelopeDL,  11000, 22000, "Envelope DL", "iso_dl_110x220mm"},
    {IOPaperSize::EnvelopeC5,  16200, 22900, "Envelope C5", "iso_c5_162x229mm"},
};

const PaperEntry* FindPaper(IOPaperSize size) {
    for (const auto& entry : kPaperTable) {
        if (entry.size == size) {
            return &entry;
        }
    }
    return nullptr;
}

}  // namespace

IOPaperDimensions IOPaperSizeDimensions(IOPaperSize size) {
    IOPaperDimensions dimensions;
    if (const PaperEntry* entry = FindPaper(size)) {
        dimensions.widthHundredthsMM = entry->widthHundredthsMM;
        dimensions.heightHundredthsMM = entry->heightHundredthsMM;
    }
    return dimensions;
}

const char* IOPaperSizeToString(IOPaperSize size) {
    if (const PaperEntry* entry = FindPaper(size)) {
        return entry->name;
    }
    return size == IOPaperSize::Custom ? "Custom" : "Unknown";
}

std::string IOPaperSizeToPwgName(IOPaperSize size) {
    if (const PaperEntry* entry = FindPaper(size)) {
        return entry->pwgName;
    }
    return std::string();
}

IOPaperDimensions IOPageSetup::GetDimensions() const {
    IOPaperDimensions dimensions = (paperSize == IOPaperSize::Custom)
                                       ? customSize
                                       : IOPaperSizeDimensions(paperSize);

    const bool sideways = orientation == IOPrintOrientation::Landscape ||
                          orientation == IOPrintOrientation::ReverseLandscape;
    if (sideways) {
        std::swap(dimensions.widthHundredthsMM, dimensions.heightHundredthsMM);
    }
    return dimensions;
}

// ============================================================================
// RESOLUTION
// ============================================================================

IOResolution IOResolutionModeDpi(IOResolutionMode mode) {
    IOResolution resolution;
    switch (mode) {
        case IOResolutionMode::Draft:        resolution = {360, 360};   break;
        case IOResolutionMode::Standard:     resolution = {720, 720};   break;
        case IOResolutionMode::High:         resolution = {1440, 720};  break;
        case IOResolutionMode::Photo:        resolution = {1440, 1440}; break;
        case IOResolutionMode::PhotoHighest: resolution = {2880, 1440}; break;
        case IOResolutionMode::Auto:
        case IOResolutionMode::Custom:
            break;
    }
    return resolution;
}

namespace {

// Orders the graded modes so one can be clamped to another. Auto and Custom
// are outside the ordering and return -1.
int ResolutionRank(IOResolutionMode mode) {
    switch (mode) {
        case IOResolutionMode::Draft:        return 0;
        case IOResolutionMode::Standard:     return 1;
        case IOResolutionMode::High:         return 2;
        case IOResolutionMode::Photo:        return 3;
        case IOResolutionMode::PhotoHighest: return 4;
        case IOResolutionMode::Auto:
        case IOResolutionMode::Custom:
            break;
    }
    return -1;
}

// The ceiling a medium imposes. Plain paper cannot hold a photo-grade ink
// load — the driver will refuse it, or the sheet will cockle — so GutenPrint
// gates the top resolutions behind photo media, and this mirrors that.
IOResolutionMode MaxResolutionForMedia(IOMediaType media) {
    switch (media) {
        case IOMediaType::Transparency:
            return IOResolutionMode::Standard;

        case IOMediaType::Plain:
        case IOMediaType::PlainFast:
        case IOMediaType::Bond:
        case IOMediaType::Letterhead:
        case IOMediaType::Recycled:
        case IOMediaType::Envelope:
        case IOMediaType::CardStock:
        case IOMediaType::Label:
        case IOMediaType::CDDVD:
            return IOResolutionMode::High;

        case IOMediaType::Coated:
        case IOMediaType::Inkjet:
            return IOResolutionMode::Photo;

        case IOMediaType::PhotoMatte:
        case IOMediaType::PhotoSemiGloss:
        case IOMediaType::PhotoGlossy:
        case IOMediaType::PhotoLuster:
        case IOMediaType::PhotoPremiumGlossy:
        case IOMediaType::FineArt:
        case IOMediaType::Canvas:
            return IOResolutionMode::PhotoHighest;

        case IOMediaType::Auto:
            break;
    }
    return IOResolutionMode::PhotoHighest;
}

bool IsPhotoMedia(IOMediaType media) {
    return MaxResolutionForMedia(media) == IOResolutionMode::PhotoHighest &&
           media != IOMediaType::Auto;
}

void Note(std::vector<std::string>* changes, const std::string& text) {
    if (changes) {
        changes->push_back(text);
    }
}

template <typename T>
bool Contains(const std::vector<T>& values, T value) {
    return std::find(values.begin(), values.end(), value) != values.end();
}

}  // namespace

// ============================================================================
// ENUM NAMES
// ============================================================================

const char* IOPrintRendererToString(IOPrintRenderer renderer) {
    switch (renderer) {
        case IOPrintRenderer::Auto:       return "Auto";
        case IOPrintRenderer::Native:     return "Native";
        case IOPrintRenderer::GutenPrint: return "GutenPrint";
        case IOPrintRenderer::IPP:        return "IPP";
    }
    return "Unknown";
}

const char* IOPrintQualityToString(IOPrintQuality quality) {
    switch (quality) {
        case IOPrintQuality::Draft:  return "Draft";
        case IOPrintQuality::Normal: return "Normal";
        case IOPrintQuality::High:   return "High";
        case IOPrintQuality::Photo:  return "Photo";
    }
    return "Unknown";
}

const char* IOPrinterColorModeToString(IOPrinterColorMode mode) {
    switch (mode) {
        case IOPrinterColorMode::Auto:       return "Auto";
        case IOPrinterColorMode::Color:      return "Color";
        case IOPrinterColorMode::Grayscale:  return "Grayscale";
        case IOPrinterColorMode::Monochrome: return "Monochrome";
    }
    return "Unknown";
}

const char* IODuplexModeToString(IODuplexMode mode) {
    switch (mode) {
        case IODuplexMode::None:      return "None";
        case IODuplexMode::LongEdge:  return "LongEdge";
        case IODuplexMode::ShortEdge: return "ShortEdge";
    }
    return "Unknown";
}

const char* IOMediaTypeToString(IOMediaType media) {
    switch (media) {
        case IOMediaType::Auto:               return "Auto";
        case IOMediaType::Plain:              return "Plain";
        case IOMediaType::PlainFast:          return "Plain Fast";
        case IOMediaType::Bond:               return "Bond";
        case IOMediaType::Letterhead:         return "Letterhead";
        case IOMediaType::Recycled:           return "Recycled";
        case IOMediaType::Coated:             return "Coated";
        case IOMediaType::Inkjet:             return "Inkjet";
        case IOMediaType::PhotoMatte:         return "Photo Matte";
        case IOMediaType::PhotoSemiGloss:     return "Photo Semi-Gloss";
        case IOMediaType::PhotoGlossy:        return "Photo Glossy";
        case IOMediaType::PhotoLuster:        return "Photo Luster";
        case IOMediaType::PhotoPremiumGlossy: return "Photo Premium Glossy";
        case IOMediaType::FineArt:            return "Fine Art";
        case IOMediaType::Canvas:             return "Canvas";
        case IOMediaType::Transparency:       return "Transparency";
        case IOMediaType::Envelope:           return "Envelope";
        case IOMediaType::CardStock:          return "Card Stock";
        case IOMediaType::Label:              return "Label";
        case IOMediaType::CDDVD:              return "CD/DVD";
    }
    return "Unknown";
}

const char* IOResolutionModeToString(IOResolutionMode mode) {
    switch (mode) {
        case IOResolutionMode::Auto:         return "Auto";
        case IOResolutionMode::Draft:        return "Draft";
        case IOResolutionMode::Standard:     return "Standard";
        case IOResolutionMode::High:         return "High";
        case IOResolutionMode::Photo:        return "Photo";
        case IOResolutionMode::PhotoHighest: return "Photo Highest";
        case IOResolutionMode::Custom:       return "Custom";
    }
    return "Unknown";
}

const char* IOInksetToString(IOInkset inkset) {
    switch (inkset) {
        case IOInkset::Auto:       return "Auto";
        case IOInkset::CMYK:       return "CMYK";
        case IOInkset::CMYKcm:     return "CMYK + light cyan/magenta";
        case IOInkset::CMYKRB:     return "CMYK + red/blue";
        case IOInkset::CMYKcmk:    return "CMYK + light black";
        case IOInkset::Photo:      return "Photo";
        case IOInkset::MatteOnly:  return "Matte only";
        case IOInkset::Monochrome: return "Monochrome";
    }
    return "Unknown";
}

const char* IOCartridgeTypeToString(IOCartridgeType cartridge) {
    switch (cartridge) {
        case IOCartridgeType::Auto:         return "Auto";
        case IOCartridgeType::Standard:     return "Standard";
        case IOCartridgeType::PhotoBlack:   return "Photo Black";
        case IOCartridgeType::MatteBlack:   return "Matte Black";
        case IOCartridgeType::HighCapacity: return "High Capacity";
        case IOCartridgeType::EcoTank:      return "EcoTank";
    }
    return "Unknown";
}

const char* IOPrintJobStateToString(IOPrintJobState state) {
    switch (state) {
        case IOPrintJobState::Pending:    return "Pending";
        case IOPrintJobState::Held:       return "Held";
        case IOPrintJobState::Processing: return "Processing";
        case IOPrintJobState::Stopped:    return "Stopped";
        case IOPrintJobState::Completed:  return "Completed";
        case IOPrintJobState::Cancelled:  return "Cancelled";
        case IOPrintJobState::Aborted:    return "Aborted";
        case IOPrintJobState::Unknown:    break;
    }
    return "Unknown";
}

const char* IOSupplyTypeToString(IOSupplyType type) {
    switch (type) {
        case IOSupplyType::Ink:       return "Ink";
        case IOSupplyType::Toner:     return "Toner";
        case IOSupplyType::Drum:      return "Drum";
        case IOSupplyType::Fuser:     return "Fuser";
        case IOSupplyType::WasteTank: return "Waste Tank";
        case IOSupplyType::Staples:   return "Staples";
        case IOSupplyType::Paper:     return "Paper";
        case IOSupplyType::Unknown:   break;
    }
    return "Unknown";
}

const char* IOSupplyColorToString(IOSupplyColor color) {
    switch (color) {
        case IOSupplyColor::Black:        return "Black";
        case IOSupplyColor::Cyan:         return "Cyan";
        case IOSupplyColor::Magenta:      return "Magenta";
        case IOSupplyColor::Yellow:       return "Yellow";
        case IOSupplyColor::LightCyan:    return "Light Cyan";
        case IOSupplyColor::LightMagenta: return "Light Magenta";
        case IOSupplyColor::LightBlack:   return "Light Black";
        case IOSupplyColor::Red:          return "Red";
        case IOSupplyColor::Blue:         return "Blue";
        case IOSupplyColor::Gray:         return "Gray";
        case IOSupplyColor::PhotoBlack:   return "Photo Black";
        case IOSupplyColor::MatteBlack:   return "Matte Black";
        case IOSupplyColor::None:         break;
    }
    return "None";
}

const char* IOPrinterStateToString(IOPrinterState state) {
    switch (state) {
        case IOPrinterState::Idle:     return "Idle";
        case IOPrinterState::Printing: return "Printing";
        case IOPrinterState::Stopped:  return "Stopped";
        case IOPrinterState::Unknown:  break;
    }
    return "Unknown";
}

// ============================================================================
// OPTION FACTORIES
// ============================================================================

IOPrintOptions IOPrintOptions::PhotoQuality() {
    IOPrintOptions options;
    options.quality = IOPrintQuality::Photo;
    options.colorMode = IOPrinterColorMode::Color;
    options.mediaType = IOMediaType::PhotoGlossy;
    options.resolutionMode = IOResolutionMode::PhotoHighest;
    options.inkset = IOInkset::Photo;
    options.cartridge = IOCartridgeType::PhotoBlack;
    options.page.borderless = true;
    return options;
}

IOPrintOptions IOPrintOptions::FastDraft() {
    IOPrintOptions options;
    options.quality = IOPrintQuality::Draft;
    options.colorMode = IOPrinterColorMode::Grayscale;
    options.mediaType = IOMediaType::PlainFast;
    options.resolutionMode = IOResolutionMode::Draft;
    return options;
}

// ============================================================================
// CAPABILITIES
// ============================================================================

// An empty capability list means "the backend did not tell us", not "nothing
// is supported" — a printer whose PPD we could not read must stay usable.
bool IOPrinterCapabilities::Supports(IOPaperSize size) const {
    return paperSizes.empty() || Contains(paperSizes, size);
}

bool IOPrinterCapabilities::Supports(IOPrintQuality quality) const {
    return qualities.empty() || Contains(qualities, quality);
}

bool IOPrinterCapabilities::Supports(IOMediaType media) const {
    return mediaTypes.empty() || media == IOMediaType::Auto ||
           Contains(mediaTypes, media);
}

bool IOPrinterCapabilities::Supports(IOResolutionMode mode) const {
    return resolutionModes.empty() || mode == IOResolutionMode::Auto ||
           Contains(resolutionModes, mode);
}

bool IOPrinterCapabilities::Supports(IOInkset value) const {
    return inksets.empty() || value == IOInkset::Auto || Contains(inksets, value);
}

// ============================================================================
// OPTION RESOLUTION
// ============================================================================

IOPrintOptions ResolvePrintOptions(const IOPrintOptions& requested,
                                   const IOPrinterCapabilities& capabilities,
                                   std::vector<std::string>* changes) {
    IOPrintOptions resolved = requested;

    // --- 1. Paper size -------------------------------------------------
    if (resolved.page.paperSize == IOPaperSize::Custom) {
        if (!resolved.page.customSize.IsValid()) {
            Note(changes, "Custom paper size has no dimensions, using A4");
            resolved.page.paperSize = IOPaperSize::A4;
            resolved.page.customSize = IOPaperDimensions();
        } else if (capabilities.maxCustomSize.IsValid()) {
            IOPaperDimensions& size = resolved.page.customSize;
            if (size.widthHundredthsMM > capabilities.maxCustomSize.widthHundredthsMM ||
                size.heightHundredthsMM > capabilities.maxCustomSize.heightHundredthsMM) {
                Note(changes, "Custom paper is larger than this printer accepts, "
                              "clamped to its maximum");
                size.widthHundredthsMM = std::min(size.widthHundredthsMM,
                                                  capabilities.maxCustomSize.widthHundredthsMM);
                size.heightHundredthsMM = std::min(size.heightHundredthsMM,
                                                   capabilities.maxCustomSize.heightHundredthsMM);
            }
        }
    } else if (!capabilities.Supports(resolved.page.paperSize)) {
        const char* wanted = IOPaperSizeToString(resolved.page.paperSize);
        IOPaperSize fallback = IOPaperSize::Unknown;
        for (IOPaperSize candidate : {IOPaperSize::A4, IOPaperSize::Letter}) {
            if (capabilities.Supports(candidate)) {
                fallback = candidate;
                break;
            }
        }
        if (fallback == IOPaperSize::Unknown && !capabilities.paperSizes.empty()) {
            fallback = capabilities.paperSizes.front();
        }
        if (fallback != IOPaperSize::Unknown) {
            Note(changes, std::string(wanted) + " is not supported, using " +
                          IOPaperSizeToString(fallback));
            resolved.page.paperSize = fallback;
        }
    }

    if (resolved.page.borderless && Denies(capabilities.supportsBorderless)) {
        Note(changes, "This printer cannot print borderless, keeping margins");
        resolved.page.borderless = false;
    }

    // --- 2. Media type -------------------------------------------------
    if (!capabilities.Supports(resolved.mediaType)) {
        const char* wanted = IOMediaTypeToString(resolved.mediaType);
        IOMediaType fallback = capabilities.Supports(IOMediaType::Plain)
                                   ? IOMediaType::Plain
                                   : (capabilities.mediaTypes.empty()
                                          ? IOMediaType::Auto
                                          : capabilities.mediaTypes.front());
        Note(changes, std::string(wanted) + " paper is not supported, using " +
                      IOMediaTypeToString(fallback));
        resolved.mediaType = fallback;
    }

    // --- 3. Resolution, constrained by the media chosen above ----------
    if (resolved.resolutionMode == IOResolutionMode::Custom) {
        if (!resolved.customResolution.IsValid()) {
            Note(changes, "Custom resolution has no dpi, using the printer default");
            resolved.resolutionMode = IOResolutionMode::Auto;
        }
    } else {
        if (!capabilities.Supports(resolved.resolutionMode)) {
            const char* wanted = IOResolutionModeToString(resolved.resolutionMode);
            IOResolutionMode best = IOResolutionMode::Auto;
            int bestRank = -1;
            for (IOResolutionMode candidate : capabilities.resolutionModes) {
                const int rank = ResolutionRank(candidate);
                if (rank > bestRank && rank <= ResolutionRank(resolved.resolutionMode)) {
                    bestRank = rank;
                    best = candidate;
                }
            }
            if (best == IOResolutionMode::Auto && !capabilities.resolutionModes.empty()) {
                best = capabilities.resolutionModes.front();
            }
            Note(changes, std::string(wanted) + " resolution is not supported, using " +
                          IOResolutionModeToString(best));
            resolved.resolutionMode = best;
        }

        const IOResolutionMode ceiling = MaxResolutionForMedia(resolved.mediaType);
        if (ResolutionRank(resolved.resolutionMode) > ResolutionRank(ceiling)) {
            Note(changes, std::string(IOResolutionModeToString(resolved.resolutionMode)) +
                          " resolution needs photo paper; on " +
                          IOMediaTypeToString(resolved.mediaType) + " using " +
                          IOResolutionModeToString(ceiling));
            resolved.resolutionMode = ceiling;
        }
    }

    // --- 4. Cartridge, constrained by the media ------------------------
    if (resolved.cartridge == IOCartridgeType::PhotoBlack &&
        !IsPhotoMedia(resolved.mediaType) && resolved.mediaType != IOMediaType::Auto) {
        Note(changes, std::string("Photo Black ink is for photo paper; on ") +
                      IOMediaTypeToString(resolved.mediaType) + " using Matte Black");
        resolved.cartridge = IOCartridgeType::MatteBlack;
    }

    // --- 5. Inkset, constrained by media and colour mode ---------------
    if (!capabilities.Supports(resolved.inkset)) {
        Note(changes, std::string(IOInksetToString(resolved.inkset)) +
                      " inkset is not supported, choosing automatically");
        resolved.inkset = IOInkset::Auto;
    }

    if (Denies(capabilities.supportsColor) &&
        resolved.colorMode == IOPrinterColorMode::Color) {
        Note(changes, "This printer does not print colour, using grayscale");
        resolved.colorMode = IOPrinterColorMode::Grayscale;
    }

    if (resolved.colorMode == IOPrinterColorMode::Monochrome &&
        resolved.inkset != IOInkset::Auto && resolved.inkset != IOInkset::Monochrome) {
        Note(changes, "Monochrome output does not use a colour inkset, "
                      "switching to the monochrome inkset");
        resolved.inkset = capabilities.Supports(IOInkset::Monochrome)
                              ? IOInkset::Monochrome
                              : IOInkset::Auto;
    }

    if (resolved.inkset == IOInkset::Photo && !IsPhotoMedia(resolved.mediaType) &&
        resolved.mediaType != IOMediaType::Auto) {
        Note(changes, std::string("The photo inkset is for photo paper; on ") +
                      IOMediaTypeToString(resolved.mediaType) +
                      " choosing the inkset automatically");
        resolved.inkset = IOInkset::Auto;
    }

    // --- 6. Duplex -----------------------------------------------------
    if (resolved.duplex != IODuplexMode::None && Denies(capabilities.supportsDuplex)) {
        Note(changes, "This printer has no duplex unit, printing single-sided");
        resolved.duplex = IODuplexMode::None;
    }

    // --- Remaining independent settings --------------------------------
    if (!capabilities.Supports(resolved.quality)) {
        IOPrintQuality fallback = IOPrintQuality::Normal;
        if (!capabilities.qualities.empty() &&
            !capabilities.Supports(IOPrintQuality::Normal)) {
            fallback = capabilities.qualities.front();
        }
        Note(changes, std::string(IOPrintQualityToString(resolved.quality)) +
                      " quality is not supported, using " +
                      IOPrintQualityToString(fallback));
        resolved.quality = fallback;
    }

    if (resolved.copies < 1) {
        Note(changes, "Copies must be at least 1");
        resolved.copies = 1;
    } else if (capabilities.maxCopies > 0 && resolved.copies > capabilities.maxCopies) {
        Note(changes, "This printer accepts at most " +
                      std::to_string(capabilities.maxCopies) + " copies per job");
        resolved.copies = capabilities.maxCopies;
    }

    if (resolved.collate && Denies(capabilities.supportsCollate)) {
        Note(changes, "This printer cannot collate");
        resolved.collate = false;
    }

    return resolved;
}

}  // namespace UltraCanvas
