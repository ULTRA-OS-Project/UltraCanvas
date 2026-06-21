// include/Plugins/Barcode/UltraCanvasBarcodeEncoders.h
// 1D barcode symbology encoders. Each encoder turns input data into a
// stream of "modules" (1 = bar, 0 = space) plus optional human-readable
// text. The widget consumes this output and renders bars as filled rects.
//
// Supported symbologies (v1, all 1D, encoder-only):
//   - Code 39 (LOGMARS), Code 93, Code 128 + GS1-128 variant
//   - EAN-13, EAN-8, UPC-A, UPC-E, ISBN-13 (EAN-13 prefix 978/979)
//   - ITF (Interleaved 2 of 5), ITF-14 (with bearer bars)
//   - Standard 2 of 5 (Industrial)
//   - Codabar (NW-7 / Monarch / Rationalized)
//   - MSI Plessey (mod 10 / mod 11 / mod 10+10 / mod 11+10 check options)
//   - Pharmacode (Laetus, binary 3..131070)
//
// Version: 1.0.0
// Last Modified: 2026-05-19
// Author: UltraCanvas Framework

#pragma once

#include <memory>
#include <string>
#include <vector>

namespace UltraCanvas {

    // ===== SUPPORTED SYMBOLOGIES =====
    enum class BarcodeSymbology {
        Code39,
        Code39Extended,   // Full ASCII Code 39 (uses shift chars)
        Code93,
        Code128,          // Auto-switching subsets A/B/C
        Code128A,         // Force subset A
        Code128B,         // Force subset B
        Code128C,         // Force subset C (numeric pairs)
        GS1_128,          // Code 128 with FNC1 prefix + AI parens
        EAN13,
        EAN8,
        UPCA,
        UPCE,
        ISBN,             // EAN-13 with 978/979 prefix + ISBN HRI on top
        ITF,              // Interleaved 2 of 5
        ITF14,            // 14-digit ITF with bearer bars (GTIN-14)
        Standard25,       // Standard 2 of 5 (Industrial)
        Codabar,
        MSIPlessey,
        Pharmacode,
    };

    // ===== MSI CHECK DIGIT VARIANTS =====
    // NoCheck instead of None because X11/Xlib.h defines None as a macro.
    enum class MSICheckDigit {
        NoCheck,
        Mod10,
        Mod11,
        Mod10Mod10,
        Mod11Mod10,
    };

    // ===== EXTENDED MODULE METADATA =====
    // EAN/UPC have guard bars that extend below the main bar zone (HRI text
    // sits in the notch). We flag those positions so the renderer can draw
    // them slightly taller.
    enum class BarcodeModuleKind : unsigned char {
        Space = 0,
        Bar = 1,
        GuardBar = 2,     // Extends past the main bar zone (EAN/UPC)
    };

    // ===== ENCODED PATTERN =====
    // Output of an encoder. The widget renders modules left-to-right.
    struct BarcodePattern {
        // One entry per module. Modules are unit-width; the widget multiplies
        // by the chosen X-dimension at draw time.
        std::vector<BarcodeModuleKind> modules;

        // Text shown above and/or below the bars. For EAN/UPC this gets
        // split into HRI segments below; humanReadable holds the canonical
        // (full) text used for the title/tooltip.
        std::string humanReadable;

        // ===== OPTIONAL HRI LAYOUT =====
        // For EAN/UPC, the human-readable text is broken into segments
        // positioned beneath specific module ranges (so "5 901234 123457"
        // appears split by the guard bars). If empty, the renderer
        // centers humanReadable beneath the bars.
        struct HRISegment {
            int moduleStart = 0;     // First module covered by this segment
            int moduleWidth = 0;     // Width in modules
            std::string text;
            bool aboveBars = false;  // false = below (default), true = above
        };
        std::vector<HRISegment> hriSegments;

        // ===== BEARER BARS =====
        // ITF-14 draws a horizontal bearer bar above + below (and sometimes
        // a vertical bar on either side). The encoder requests them; the
        // widget figures out the geometry.
        bool drawBearerBars = false;

        // ===== STATUS =====
        bool valid = true;
        std::string errorMessage;

        // Convenience: total module count
        int ModuleCount() const { return static_cast<int>(modules.size()); }
    };

    // ===== ENCODER INTERFACE =====
    class IBarcodeEncoder {
    public:
        virtual ~IBarcodeEncoder() = default;
        virtual BarcodePattern Encode(const std::string& data) const = 0;
        virtual BarcodeSymbology GetSymbology() const = 0;
        virtual const char* GetName() const = 0;
    };

    // ===== ENCODER OPTIONS =====
    // Per-call knobs that some encoders honor (e.g. Code 39 checksum,
    // MSI check digit type, ITF check digit). Defaults are sane.
    struct BarcodeEncoderOptions {
        bool includeChecksum = true;           // Code 39, ITF, Std25, Codabar
        MSICheckDigit msiCheckDigit = MSICheckDigit::Mod10;
        bool emitHumanReadable = true;         // If false, encoder leaves text blank
        bool codabarUppercaseStartStop = true; // Codabar A/B/C/D vs a/b/c/d
    };

    // ===== ENCODER FACTORY =====
    // Returns nullptr if symbology is unknown.
    std::unique_ptr<IBarcodeEncoder> CreateBarcodeEncoder(
        BarcodeSymbology symbology,
        const BarcodeEncoderOptions& options = BarcodeEncoderOptions{});

    // Convenience one-shot encoder.
    BarcodePattern EncodeBarcode(
        BarcodeSymbology symbology,
        const std::string& data,
        const BarcodeEncoderOptions& options = BarcodeEncoderOptions{});

    // Human-readable symbology name (e.g. "Code 128", "EAN-13").
    const char* GetBarcodeSymbologyName(BarcodeSymbology symbology);

} // namespace UltraCanvas
