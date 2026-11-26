// Plugins/Documents/Word/UltraCanvasOfficeArt.h
// OfficeArt ([MS-ODRAW]) - the drawing layer of the binary Office formats -
// shared by the Word 97-2003 reader (UltraCanvasDocLegacyFormat.cpp) and the
// PowerPoint 97-2003 reader (core/UltraCanvasPresentationPpt.cpp): record
// headers, a shape's property table (FOPT / TertiaryFOPT) with its complex
// values, the picture store (BStore, FBSE) and its pictures (PNG, JPEG, EMF
// and WMF BLIPs, a pattern's 1-bit bitmap), a freeform's outline, and the
// shape types (MSOSPT) as DrawingML preset names. Internal to the document
// readers - not installed, not part of the public API.
// Specification: [MS-ODRAW] Office Drawing Binary File Format.
// Version: 1.0.0 - moved here from the .doc reader, for the .ppt reader; more shape types; a
//                  shape's text insets, wrap, text flow, crop, opacity, name, description and
//                  table properties
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasRichDocument.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace UltraCanvas {
namespace OfficeArt {

// Little-endian reads that answer 0 past the end, so a damaged length never
// reads outside the buffer.
inline uint16_t U16(const std::vector<uint8_t>& data, size_t at) {
    if (at > data.size() || data.size() - at < 2) return 0;
    return static_cast<uint16_t>(data[at] | (data[at + 1] << 8));
}
inline uint32_t U32(const std::vector<uint8_t>& data, size_t at) {
    if (at > data.size() || data.size() - at < 4) return 0;
    return static_cast<uint32_t>(data[at]) | (static_cast<uint32_t>(data[at + 1]) << 8) |
           (static_cast<uint32_t>(data[at + 2]) << 16) | (static_cast<uint32_t>(data[at + 3]) << 24);
}

// A record header (OfficeArtRecordHeader, and PowerPoint's RecordHeader,
// which is the same 8 bytes): version (4 bits), instance (12 bits), type,
// length; the body follows.
struct RecordHeader {
    uint16_t version = 0;
    uint16_t instance = 0;
    uint16_t type = 0;
    size_t body = 0;      // where the body starts
    size_t end = 0;       // where it ends: body + length
    bool IsContainer() const { return version == 0x0F; }
};
// The header at data[at], whose body must end by `limit`; false when it does
// not fit (a damaged length).
inline bool ReadRecordHeader(const std::vector<uint8_t>& data, size_t at, size_t limit, RecordHeader& out) {
    if (limit > data.size() || at > limit || limit - at < 8) return false;
    const uint16_t verInstance = U16(data, at);
    out.version = static_cast<uint16_t>(verInstance & 0x0F);
    out.instance = static_cast<uint16_t>(verInstance >> 4);
    out.type = U16(data, at + 2);
    const uint32_t length = U32(data, at + 4);
    out.body = at + 8;
    if (length > limit - out.body) return false;
    out.end = out.body + length;
    return true;
}

// What a shape's property tables (FOPT, TertiaryFOPT) say, with the
// defaults [MS-ODRAW] gives a property that is not there. The *Set flags
// tell an explicit value from a default where a reader needs to know.
struct ShapeProperties {
    int32_t textId = 0;                     // lTxid: a Word text box (index << 16) | sequence
    uint32_t pib = 0;                       // the picture: BStore index, 1-based; 0 none
    int posH = -1, posRelH = -1, posV = -1, posRelV = -1;   // Word's placement
    uint32_t fillColor = 0xFFFFFF;          // OfficeArtCOLORREF
    bool filled = true;
    bool filledSet = false;
    int fillType = 0;                       // 0 solid, 1 pattern, 2 texture, 3 picture, 4-7 shades, 9 background
    uint32_t fillBlip = 0;                  // a pattern's or texture's picture (BStore, 1-based)
    uint32_t fillBackColor = 0xFFFFFF;      // a shade's other colour
    double fillAngle = 0.0;                 // anticlockwise from top-to-bottom
    double fillTo[4] = {0.0, 0.0, 0.0, 0.0};   // a round shade's centre: left, top, right, bottom
    int fillFocus = 0;                      // percent: where the back colour sits along the shade
    double fillOpacity = 1.0;               // 0 transparent .. 1 opaque
    uint32_t lineColor = 0;
    bool lined = true;
    bool linedSet = false;
    int32_t lineWidth = 9525;               // EMU
    // The text's insets (EMU) and how it sits: anchorText 0 top, 1 middle,
    // 2 bottom (and their centred kinds); wrapText 0 square, 2 none;
    // textFlow 0 across, 1 and 5 down, 2 up. -1: not said.
    int32_t textLeft = 91440, textTop = 45720, textRight = 91440, textBottom = 45720;
    bool textInsetsSet = false;
    int anchorText = 0;
    bool anchorTextSet = false;
    int wrapText = -1;
    int textFlow = -1;
    double rotation = 0.0;                  // degrees, clockwise
    // Its shadow: on or off, colour, how much of it shows (16.16), and its
    // offsets (EMU; Word's default is 2 pt down and right).
    bool shadow = false;
    uint32_t shadowColor = 0x00808080;
    int32_t shadowOpacity = 0x00010000;
    int32_t shadowOffsetX = 25400, shadowOffsetY = 25400;
    int lineDashing = 0;                    // 0 solid, 2 dotted, other dashes
    bool arrowStart = false, arrowEnd = false;
    int32_t adjust = -1;                    // adjustValue: a rounded rectangle's corner, of 21600
    int32_t geoLeft = 0, geoTop = 0, geoRight = 21600, geoBottom = 21600;
    std::vector<std::pair<int32_t, int32_t>> vertices;   // a freeform's points (pVertices)
    std::vector<uint16_t> segments;         // and how they join (pSegmentInfo)
    // A picture's crop, a share of it from each side (cropFromTop ...).
    double cropTop = 0.0, cropBottom = 0.0, cropLeft = 0.0, cropRight = 0.0;
    std::string name;                       // wzName, UTF-8
    std::string description;                // wzDescription: the alternative text, UTF-8
    uint32_t tableProperties = 0;           // a group that is a table (PowerPoint): bit 0
    std::vector<int32_t> tableRowHeights;   // tableRowProperties: its rows' heights
};

// Reads the property table whose body is data[from, to) - `count` entries
// (the record's instance), then their complex values in order - into
// `shape`. Damaged values are skipped; nothing outside [from, to) is read.
void ReadProperties(const std::vector<uint8_t>& data, size_t from, size_t to, size_t count, ShapeProperties& shape);

// A picture in the store: the BLIP record's place - in the store itself, or
// at foDelay in a stream of its own (Word's WordDocument, PowerPoint's
// Pictures) - or none (stream null).
struct BlipRef {
    const std::vector<uint8_t>* stream = nullptr;
    size_t at = 0;
    size_t end = 0;
};
// The BStore's FBSE records in data[from, to), in order: each picture
// embedded after its record's header, or in `delayStream` at foDelay.
void ReadBStore(const std::vector<uint8_t>& data, size_t from, size_t to, const std::vector<uint8_t>* delayStream,
                std::vector<BlipRef>& out);

// A PNG, JPEG, EMF or WMF BLIP record at data[at] (its 8-byte header first),
// ending by `end`: the picture's bytes (a metafile inflated) and its MIME
// type. Anything else, or a damaged record, is not.
bool ReadBlipRecord(const std::vector<uint8_t>& data, size_t at, size_t end, std::vector<uint8_t>& image,
                    std::string& mimeType);
// A pattern fill's bitmap (a DIB BLIP holding an 8 x 8, 1-bit picture) as
// RichShape's "bits:" tile: the darker of its two colours is the pattern.
bool ReadPatternBlip(const std::vector<uint8_t>& data, size_t at, size_t end, std::string& tile);

// A freeform's outline: its vertices in its geometry's space (geoLeft ..
// geoRight) as points 0..1 across and down the box, joined as pSegmentInfo
// says (or by straight lines).
std::vector<RichShape::PathPoint> FreeformPath(const ShapeProperties& shape);

// An OfficeArtCOLORREF (0x00BBGGRR) as "#RRGGBB". Its flags byte is not
// looked at: the caller decides what a scheme or system colour means.
std::string ColorRefHex(uint32_t color);

// A shape type (MSOSPT: 1 rectangle, 3 ellipse, 20 line, ...) as the
// DrawingML preset that draws it (ECMA-376 ST_ShapeType), or null for one
// with no preset (0, a freeform, has its own outline; 75, a picture frame,
// is drawn by its picture; WordArt). A text box (202) is a rectangle.
const char* PresetOfShapeType(int type);

} // namespace OfficeArt
} // namespace UltraCanvas
