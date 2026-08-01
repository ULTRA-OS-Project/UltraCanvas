// include/Plugins/Diagrams/UltraCanvasPacketLayout.h
// Packet structure layout engine: turns (bitOffset, bitLength) into cell
// rectangles. Pure geometry - no render context, no UI element, no text
// measurement - so it is fully unit-testable against known headers.
//
// The hard part, and the reason this is a separate unit: a field that crosses
// a row boundary must be emitted as SEVERAL cells that still read as ONE
// field. A 64-bit Sequence Number over a 32-bit grid is two cells, labelled
// once, with the cut edges marked so the renderer can dash them and suppress
// the shared border.
//
// Version: 1.0.0
// Last Modified: 2026-07-31
// Author: UltraCanvas Framework
#pragma once

#include "Plugins/Diagrams/UltraCanvasPacketModel.h"
#include <vector>
#include <string>
#include <cstdint>

namespace UltraCanvas {

// =============================================================================
// ENUMERATIONS
// =============================================================================

    enum class PacketLayoutMode {
        WordGrid,           // RFC style: wraps every bitsPerRow bits
        ProportionalLinear, // one continuous strip, width proportional to size
        Encapsulation,      // nested PDU bands
        Lanes               // one row per signal (SDIO/SPI/CAN bitstreams)
    };

// Image 3 places a 1-byte field beside a 96-byte field; a strictly
// proportional 1:96 ratio makes the small cell unreadable.
    enum class PacketLengthScale {
        Linear,     // true proportion
        Sqrt,       // compressed
        Log,        // heavily compressed
        Clamped     // linear, but never narrower than minCellWidth
    };

    enum class PacketSizeUnit { Bits, Bytes, Auto };

// =============================================================================
// LAYOUT INPUT
// =============================================================================

    struct PacketLayoutOptions {
        PacketLayoutMode  mode = PacketLayoutMode::WordGrid;
        PacketLengthScale lengthScale = PacketLengthScale::Clamped;

        uint32_t bitsPerRow = 32;

        double rowHeight = 34.0;
        double rowGap = 0.0;
        double minCellWidth = 18.0;      // used by Clamped and as a floor everywhere

        // Gutters and rulers reserve space before the cell grid starts.
        double rulerHeight = 0.0;        // top bit ruler / byte columns
        double offsetGutterWidth = 0.0;  // left byte/bit offset column
        double wordGutterWidth = 0.0;    // left word index column (image 6)
        double leftBraceWidth = 0.0;
        double rightBraceWidth = 0.0;

        // Lanes mode.
        uint32_t laneCount = 1;
        double   laneGutterWidth = 0.0;

        // A payload of unknown length still needs a drawn height.
        double payloadRowCount = 1.5;
    };

// =============================================================================
// LAYOUT OUTPUT
// =============================================================================

// One drawn rectangle. A field spanning a row boundary produces several of
// these, all sharing fieldIndex/layerIndex, distinguished by cellIndexInField.
    struct PacketCell {
        size_t layerIndex = 0;
        size_t fieldIndex = 0;          // index within the layer's field list
        size_t cellIndexInField = 0;    // 0 for the first fragment
        size_t cellCountInField = 1;

        int    depth = 0;               // 0 = top-level field, 1+ = sub-field
        size_t parentCell = SIZE_MAX;   // index into cells for depth > 0

        uint64_t bitOffset = 0;         // absolute within the PDU
        uint64_t bitLength = 0;         // of THIS fragment, not the whole field

        Rect2Dd  bounds;
        uint32_t row = 0;

        // Cut edges: true when the field continues into the neighbouring row.
        bool continuesLeft = false;
        bool continuesRight = false;

        // Convenience mirrors of the source field, so renderers and hit-testers
        // do not have to walk back into the model.
        std::string     name;
        PacketFieldKind kind = PacketFieldKind::Fixed;
        bool  hasColorOverride = false;
        Color color;

        // Only the first fragment of a field carries its label.
        bool IsLabelCell() const { return cellIndexInField == 0; }
    };

    struct PacketRulerTick {
        uint32_t bit = 0;          // bit index within a row
        double   x = 0.0;
        std::string label;
        bool major = false;
    };

    struct PacketRowIndexEntry {
        uint32_t row = 0;
        double   y = 0.0;
        double   height = 0.0;
        std::string offsetLabel;   // "0", "32", "0x04" ...
        std::string wordLabel;     // "1", "2", ...
    };

    struct PacketBracketSpan {
        size_t layerIndex = 0;
        std::string label;
        double y = 0.0;
        double height = 0.0;
    };

    struct PacketLayoutResult {
        std::vector<PacketCell> cells;
        std::vector<PacketRulerTick> rulerTicks;
        std::vector<PacketRowIndexEntry> rowIndex;
        std::vector<PacketBracketSpan> layerSpans;

        Rect2Dd gridArea;          // the cell grid, excluding rulers and gutters
        Rect2Dd totalArea;         // everything
        uint32_t rowCount = 0;
        double  bitWidth = 0.0;    // pixels per bit (word grid / proportional)
        bool    valid = false;

        const PacketCell* FindCellAt(const Point2Dd& p) const;
        // Every fragment of one logical field, in order.
        std::vector<size_t> CellsOfField(size_t layerIndex, size_t fieldIndex) const;
    };

// =============================================================================
// THE ENGINE
// =============================================================================

// Lay a model out inside `area`. Pure function: same inputs, same output.
    PacketLayoutResult ComputePacketLayout(const PacketModel& model,
                                           const PacketLayoutOptions& options,
                                           const Rect2Dd& area);

// Scale helper, exposed for tests and for the "broken scale" marker: returns
// the relative width weight of a field of `bits` under the given scale.
    double PacketLengthWeight(uint64_t bits, PacketLengthScale scale);

// Format a bit offset the way the offset gutter wants it.
    std::string FormatPacketOffset(uint64_t bitOffset, PacketSizeUnit unit, bool hex);

} // namespace UltraCanvas
