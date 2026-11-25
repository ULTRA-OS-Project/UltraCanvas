// Plugins/Documents/Word/UltraCanvasOfficeArt.cpp
// OfficeArt records shared by the .doc and .ppt readers (UltraCanvasOfficeArt.h).
// Version: 1.0.0 - moved here from the .doc reader, for the .ppt reader
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework

#include "UltraCanvasOfficeArt.h"
#include "miniz.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace UltraCanvas {
namespace OfficeArt {

namespace {

// UTF-16LE in data[from, to), up to its first NUL, as UTF-8.
std::string Utf16String(const std::vector<uint8_t>& data, size_t from, size_t to) {
    std::string out;
    for (size_t at = from; at + 2 <= to; at += 2) {
        uint32_t c = U16(data, at);
        if (c == 0) break;
        if (c >= 0xD800 && c < 0xDC00 && at + 4 <= to) {
            const uint32_t low = U16(data, at + 2);
            if (low >= 0xDC00 && low < 0xE000) {
                c = 0x10000 + ((c - 0xD800) << 10) + (low - 0xDC00);
                at += 2;
            }
        }
        if (c < 0x80) {
            out.push_back(static_cast<char>(c));
        } else if (c < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (c >> 6)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (c >> 12)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (c >> 18)));
            out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

// An IMsoArray: element count, allocated count, element size, elements.
// Vertices are two 16-bit numbers (size 0xFFF0 or 4) or two 32-bit ones.
void ReadVertices(const std::vector<uint8_t>& data, size_t from, size_t to, ShapeProperties& shape) {
    if (from + 6 > to) return;
    const size_t count = U16(data, from);
    const uint16_t size = U16(data, from + 4);
    const size_t step = (size == 0xFFF0 || size == 4) ? 4 : 8;
    for (size_t i = 0; i < count && from + 6 + (i + 1) * step <= to; ++i) {
        const size_t at = from + 6 + i * step;
        if (step == 4) {
            shape.vertices.emplace_back(static_cast<int16_t>(U16(data, at)), static_cast<int16_t>(U16(data, at + 2)));
        } else {
            shape.vertices.emplace_back(static_cast<int32_t>(U32(data, at)), static_cast<int32_t>(U32(data, at + 4)));
        }
    }
}

void ReadSegments(const std::vector<uint8_t>& data, size_t from, size_t to, ShapeProperties& shape) {
    if (from + 6 > to) return;
    const size_t count = U16(data, from);
    for (size_t i = 0; i < count && from + 6 + (i + 1) * 2 <= to; ++i) shape.segments.push_back(U16(data, from + 6 + i * 2));
}

// tableRowProperties: an IMsoArray of 32-bit row heights.
void ReadRowHeights(const std::vector<uint8_t>& data, size_t from, size_t to, ShapeProperties& shape) {
    if (from + 6 > to) return;
    const size_t count = U16(data, from);
    const uint16_t size = U16(data, from + 4);
    if (size != 4) return;
    for (size_t i = 0; i < count && from + 6 + (i + 1) * 4 <= to; ++i)
        shape.tableRowHeights.push_back(static_cast<int32_t>(U32(data, from + 6 + i * 4)));
}

double Fixed16(uint32_t value) { return static_cast<double>(static_cast<int32_t>(value)) / 65536.0; }

} // namespace

void ReadProperties(const std::vector<uint8_t>& data, size_t from, size_t to, size_t count, ShapeProperties& shape) {
    if (to > data.size() || from > to) return;
    // Each entry is six bytes; the complex values follow the table, in order.
    if (count > (to - from) / 6) count = (to - from) / 6;
    size_t complexAt = from + count * 6;
    for (size_t i = 0; i < count; ++i) {
        const uint16_t rawId = U16(data, from + i * 6);
        const uint16_t id = rawId & 0x3FFF;
        const uint32_t value = U32(data, from + i * 6 + 2);
        if (rawId & 0x8000) {
            const size_t at = complexAt;
            size_t size = value;
            // A geometry array (IMsoArray) whose stated size leaves out its
            // 6-byte header - as some writers store it.
            if (id >= 0x0140 && id < 0x0180 && at + 6 <= to) {
                const size_t elements = U16(data, at);
                const uint16_t cb = U16(data, at + 4);
                const size_t element = cb == 0xFFF0 ? 4 : cb;
                if (elements * element == value) size = value + 6;
            }
            if (size > to - at) break;
            complexAt += size;
            if (id == 0x0145) ReadVertices(data, at, at + size, shape);
            else if (id == 0x0146) ReadSegments(data, at, at + size, shape);
            else if (id == 0x0380) shape.name = Utf16String(data, at, at + size);
            else if (id == 0x0381) shape.description = Utf16String(data, at, at + size);
            else if (id == 0x03A0) ReadRowHeights(data, at, at + size, shape);
            continue;
        }
        switch (id) {
            case 0x0004: shape.rotation = Fixed16(value); break;                               // rotation
            case 0x0080: shape.textId = static_cast<int32_t>(value); break;                    // lTxid
            case 0x0081: shape.textLeft = static_cast<int32_t>(value); shape.textInsetsSet = true; break;
            case 0x0082: shape.textTop = static_cast<int32_t>(value); shape.textInsetsSet = true; break;
            case 0x0083: shape.textRight = static_cast<int32_t>(value); shape.textInsetsSet = true; break;
            case 0x0084: shape.textBottom = static_cast<int32_t>(value); shape.textInsetsSet = true; break;
            case 0x0085: shape.wrapText = static_cast<int>(value); break;                      // WrapText
            case 0x0087: shape.anchorText = static_cast<int>(value); shape.anchorTextSet = true; break;
            case 0x0088: shape.textFlow = static_cast<int>(value); break;                      // txflTextFlow
            case 0x0100: shape.cropTop = Fixed16(value); break;                                // cropFromTop
            case 0x0101: shape.cropBottom = Fixed16(value); break;
            case 0x0102: shape.cropLeft = Fixed16(value); break;
            case 0x0103: shape.cropRight = Fixed16(value); break;
            case 0x0104: shape.pib = value; break;                                              // pib
            case 0x0140: shape.geoLeft = static_cast<int32_t>(value); break;
            case 0x0141: shape.geoTop = static_cast<int32_t>(value); break;
            case 0x0142: shape.geoRight = static_cast<int32_t>(value); break;
            case 0x0143: shape.geoBottom = static_cast<int32_t>(value); break;
            case 0x0147: shape.adjust = static_cast<int32_t>(value); break;                    // adjustValue
            case 0x0180: shape.fillType = static_cast<int>(value); break;                      // fillType
            case 0x0181: shape.fillColor = value; break;                                        // fillColor
            case 0x0182: shape.fillOpacity = std::clamp(Fixed16(value), 0.0, 1.0); break;       // fillOpacity
            case 0x0183: shape.fillBackColor = value; break;                                    // fillBackColor
            case 0x0186: shape.fillBlip = value; break;                                         // fillBlip
            case 0x018B: shape.fillAngle = Fixed16(value); break;                               // fillAngle
            case 0x018C: shape.fillFocus = static_cast<int32_t>(value); break;                 // fillFocus
            case 0x018D: case 0x018E: case 0x018F: case 0x0190:                                 // fillToLeft .. fillToBottom
                shape.fillTo[id - 0x018D] = Fixed16(value);
                break;
            case 0x01BF:                                                                        // fFilled (with fUsefFilled)
                if (value & 0x100000u) {
                    shape.filled = (value & 0x10u) != 0;
                    shape.filledSet = true;
                }
                break;
            case 0x01C0: shape.lineColor = value; break;                                        // lineColor
            case 0x01CB: shape.lineWidth = static_cast<int32_t>(value); break;                 // lineWidth
            case 0x01CE: shape.lineDashing = static_cast<int>(value); break;                   // lineDashing
            case 0x01D0: shape.arrowStart = value != 0; break;                                  // lineStartArrowhead
            case 0x01D1: shape.arrowEnd = value != 0; break;                                    // lineEndArrowhead
            case 0x01FF:                                                                        // fLine (with fUsefLine)
                if (value & 0x80000u) {
                    shape.lined = (value & 0x08u) != 0;
                    shape.linedSet = true;
                }
                break;
            case 0x0201: shape.shadowColor = value; break;                                      // shadowColor
            case 0x0204: shape.shadowOpacity = static_cast<int32_t>(value); break;             // shadowOpacity
            case 0x0205: shape.shadowOffsetX = static_cast<int32_t>(value); break;             // shadowOffsetX
            case 0x0206: shape.shadowOffsetY = static_cast<int32_t>(value); break;             // shadowOffsetY
            case 0x023F:                                                                        // fShadow (with fUsefShadow)
                if (value & 0x00020000u) shape.shadow = (value & 0x2u) != 0;
                break;
            case 0x038F: shape.posH = static_cast<int>(value); break;                          // posh
            case 0x0390: shape.posRelH = static_cast<int>(value); break;                       // posrelh
            case 0x0391: shape.posV = static_cast<int>(value); break;                          // posv
            case 0x0392: shape.posRelV = static_cast<int>(value); break;                       // posrelv
            case 0x039F: shape.tableProperties = value; break;                                  // tableProperties
            default: break;
        }
    }
}

void ReadBStore(const std::vector<uint8_t>& data, size_t from, size_t to, const std::vector<uint8_t>* delayStream,
                std::vector<BlipRef>& out) {
    RecordHeader record;
    for (size_t pos = from; ReadRecordHeader(data, pos, to, record); pos = record.end) {
        BlipRef blip;
        const size_t length = record.end - record.body;
        if (record.type == 0xF007 && length >= 36) {
            const size_t cbName = data[record.body + 33];
            const size_t embedded = record.body + 36 + cbName;
            if (embedded + 8 <= record.end) {
                blip = BlipRef{&data, embedded, record.end};
            } else if (delayStream) {
                const size_t foDelay = U32(data, record.body + 28);
                const size_t size = U32(data, record.body + 20);
                if (foDelay < delayStream->size() && delayStream->size() - foDelay >= 8) {
                    const size_t want = std::max<size_t>(size, 8);
                    const size_t end = want > delayStream->size() - foDelay ? delayStream->size() : foDelay + want;
                    blip = BlipRef{delayStream, foDelay, end};
                }
            }
        }
        out.push_back(blip);
        if (out.size() > 65536) break;
    }
}

bool ReadBlipRecord(const std::vector<uint8_t>& data, size_t at, size_t end, std::vector<uint8_t>& image,
                    std::string& mimeType) {
    RecordHeader record;
    if (!ReadRecordHeader(data, at, end, record) || record.version != 0) return false;
    const uint16_t instance = record.instance;
    const uint16_t type = record.type;
    const size_t length = record.end - record.body;
    size_t header = 0;
    if (type == 0xF01E) {                                  // PNG
        header = (instance == 0x6E1) ? 33 : 17;
        mimeType = "image/png";
    } else if (type == 0xF01D || type == 0xF02A) {         // JPEG
        header = (instance == 0x46B || instance == 0x6E3) ? 33 : 17;
        mimeType = "image/jpeg";
    } else if (type == 0xF01A || type == 0xF01B) {         // EMF, WMF
        // One or two UIDs, then a metafile header (its uncompressed size,
        // bounds, size, saved size, compression), then the metafile -
        // DEFLATE-compressed (0x00) or as it is (0xFE).
        const bool emf = type == 0xF01A;
        const size_t uids = (instance == (emf ? 0x3D5 : 0x217)) ? 32 : 16;
        const size_t meta = record.body + uids;
        if (meta + 34 > record.end) return false;
        const size_t rawSize = U32(data, meta);
        const size_t savedSize = U32(data, meta + 28);
        const uint8_t compression = data[meta + 32];
        const size_t start = meta + 34;
        if (savedSize == 0 || savedSize > record.end - start || rawSize == 0 || rawSize > 64u * 1024u * 1024u) {
            return false;
        }
        if (compression == 0x00) {
            image.resize(rawSize);
            mz_ulong out = static_cast<mz_ulong>(rawSize);
            if (mz_uncompress(image.data(), &out, data.data() + start, static_cast<mz_ulong>(savedSize)) != MZ_OK) {
                image.clear();
                return false;
            }
            image.resize(out);
        } else if (compression == 0xFE) {
            image.assign(data.begin() + static_cast<std::ptrdiff_t>(start),
                         data.begin() + static_cast<std::ptrdiff_t>(start + savedSize));
        } else {
            return false;
        }
        mimeType = emf ? "image/x-emf" : "image/x-wmf";
        // An EMF says " EMF" at byte 40; a WMF starts with its header (type
        // 1 or 2, nine words long).
        const bool looksRight = emf ? (image.size() > 44 && image[40] == ' ' && image[41] == 'E')
                                    : (image.size() > 18 && image[2] == 9 && image[3] == 0);
        if (looksRight) return true;
        image.clear();
        return false;
    } else {
        return false;
    }
    if (header >= length) return false;
    image.assign(data.begin() + static_cast<std::ptrdiff_t>(record.body + header),
                 data.begin() + static_cast<std::ptrdiff_t>(record.end));
    const bool png = image.size() > 4 && image[0] == 0x89 && image[1] == 'P';
    const bool jpeg = image.size() > 3 && image[0] == 0xFF && image[1] == 0xD8;
    if ((mimeType == "image/png" && png) || (mimeType == "image/jpeg" && jpeg)) return true;
    image.clear();
    return false;
}

bool ReadPatternBlip(const std::vector<uint8_t>& data, size_t at, size_t end, std::string& tile) {
    RecordHeader record;
    if (!ReadRecordHeader(data, at, end, record) || record.type != 0xF01F) return false;
    const size_t header = record.instance == 0x7A9 ? 33 : 17;
    const size_t dib = record.body + header;
    if (dib + 40 + 8 > record.end) return false;
    const size_t infoSize = U32(data, dib);
    const int32_t width = static_cast<int32_t>(U32(data, dib + 4));
    const int32_t height = static_cast<int32_t>(U32(data, dib + 8));
    const uint16_t bits = U16(data, dib + 14);
    if (bits != 1 || width < 8 || width > 4096 || height == 0 || std::abs(height) < 8 || std::abs(height) > 4096 ||
        infoSize < 40 || infoSize > record.end - dib - 8) {
        return false;
    }
    const size_t palette = dib + infoSize;
    auto brightness = [&](size_t entry) { return data[entry] + data[entry + 1] + data[entry + 2]; };   // B, G, R
    const bool oneIsDark = brightness(palette + 4) < brightness(palette);
    const size_t stride = ((static_cast<size_t>(width) + 31) / 32) * 4;
    const size_t pixels = palette + 8;
    if (pixels + stride * static_cast<size_t>(std::abs(height)) > record.end) return false;
    tile = "bits:";
    for (int y = 0; y < 8; ++y) {
        // Rows are stored bottom-up unless the height is negative.
        const int row = height > 0 ? height - 1 - y : y;
        const uint8_t value = data[pixels + static_cast<size_t>(row) * stride];
        char hex[3];
        std::snprintf(hex, sizeof hex, "%02X", static_cast<unsigned>(oneIsDark ? value : static_cast<uint8_t>(~value)));
        tile += hex;
    }
    return true;
}

std::vector<RichShape::PathPoint> FreeformPath(const ShapeProperties& source) {
    using Op = RichShape::PathPoint::Op;
    std::vector<RichShape::PathPoint> path;
    const double spanX = std::max<double>(1.0, static_cast<double>(source.geoRight) - source.geoLeft);
    const double spanY = std::max<double>(1.0, static_cast<double>(source.geoBottom) - source.geoTop);
    auto at = [&](size_t i, float& x, float& y) {
        x = static_cast<float>((source.vertices[i].first - static_cast<double>(source.geoLeft)) / spanX);
        y = static_cast<float>((source.vertices[i].second - static_cast<double>(source.geoTop)) / spanY);
    };
    auto add = [&](Op op, size_t i) {
        RichShape::PathPoint point;
        point.op = op;
        at(i, point.x, point.y);
        path.push_back(point);
    };
    const size_t count = source.vertices.size();
    if (source.segments.empty()) {
        for (size_t i = 0; i < count; ++i) add(i == 0 ? Op::Move : Op::Line, i);
        return path;
    }
    size_t v = 0;
    for (uint16_t segment : source.segments) {
        const int type = segment >> 13;
        const size_t n = std::max<size_t>(1, segment & 0x1FFF);
        if (type == 0) {                                   // lines
            for (size_t i = 0; i < n && v < count; ++i) add(Op::Line, v++);
        } else if (type == 1) {                            // curves, three vertices each
            for (size_t i = 0; i < n && v + 2 < count; ++i) {
                RichShape::PathPoint point;
                point.op = Op::Curve;
                at(v, point.c1x, point.c1y);
                at(v + 1, point.c2x, point.c2y);
                at(v + 2, point.x, point.y);
                path.push_back(point);
                v += 3;
            }
        } else if (type == 2) {                            // move
            if (v < count) add(Op::Move, v++);
        } else if (type == 3) {                            // close
            RichShape::PathPoint point;
            point.op = Op::Close;
            path.push_back(point);
        } else if (type == 4) {                            // end
            break;
        } else {                                           // an escape: skips its vertices
            v += segment & 0xFF;
        }
    }
    if (!path.empty() && path.front().op != Op::Move) path.front().op = Op::Move;
    return path;
}

std::string ColorRefHex(uint32_t color) {
    char hex[8];
    std::snprintf(hex, sizeof hex, "#%02X%02X%02X", color & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF);
    return hex;
}

const char* PresetOfShapeType(int type) {
    static const std::pair<int, const char*> kTypes[] = {
        {1, "rect"}, {2, "roundRect"}, {3, "ellipse"}, {4, "diamond"}, {5, "triangle"}, {6, "rtTriangle"},
        {7, "parallelogram"}, {8, "trapezoid"}, {9, "hexagon"}, {10, "octagon"}, {11, "plus"}, {12, "star5"},
        {13, "rightArrow"}, {14, "rightArrow"}, {15, "homePlate"}, {16, "cube"}, {17, "wedgeRoundRectCallout"},
        {18, "star16"}, {19, "arc"}, {20, "line"}, {21, "plaque"}, {22, "can"}, {23, "donut"},
        {32, "straightConnector1"}, {33, "bentConnector2"}, {34, "bentConnector3"}, {35, "bentConnector4"},
        {36, "bentConnector5"}, {37, "curvedConnector2"}, {38, "curvedConnector3"}, {39, "curvedConnector4"},
        {40, "curvedConnector5"}, {41, "callout1"}, {42, "callout2"}, {43, "callout3"}, {44, "accentCallout1"},
        {45, "accentCallout2"}, {46, "accentCallout3"}, {47, "borderCallout1"}, {48, "borderCallout2"},
        {49, "borderCallout3"}, {50, "accentBorderCallout1"}, {51, "accentBorderCallout2"},
        {52, "accentBorderCallout3"}, {53, "ribbon"}, {54, "ribbon2"}, {55, "chevron"}, {56, "pentagon"},
        {57, "noSmoking"}, {58, "star8"}, {59, "star16"}, {60, "star32"}, {61, "wedgeRectCallout"},
        {62, "wedgeRoundRectCallout"}, {63, "wedgeEllipseCallout"}, {64, "wave"}, {65, "foldedCorner"},
        {66, "leftArrow"}, {67, "downArrow"}, {68, "upArrow"}, {69, "leftRightArrow"}, {70, "upDownArrow"},
        {71, "irregularSeal1"}, {72, "irregularSeal2"}, {73, "lightningBolt"}, {74, "heart"},
        {76, "quadArrow"}, {77, "leftArrowCallout"}, {78, "rightArrowCallout"}, {79, "upArrowCallout"},
        {80, "downArrowCallout"}, {81, "leftRightArrowCallout"}, {82, "upDownArrowCallout"},
        {83, "quadArrowCallout"}, {84, "bevel"}, {85, "leftBracket"}, {86, "rightBracket"}, {87, "leftBrace"},
        {88, "rightBrace"}, {89, "leftUpArrow"}, {90, "bentUpArrow"}, {91, "bentArrow"}, {92, "star24"},
        {93, "stripedRightArrow"}, {94, "notchedRightArrow"}, {95, "blockArc"}, {96, "smileyFace"},
        {97, "verticalScroll"}, {98, "horizontalScroll"}, {99, "circularArrow"}, {101, "uturnArrow"},
        {102, "curvedRightArrow"}, {103, "curvedLeftArrow"}, {104, "curvedUpArrow"}, {105, "curvedDownArrow"},
        {106, "cloudCallout"}, {107, "ellipseRibbon"}, {108, "ellipseRibbon2"}, {109, "flowChartProcess"},
        {110, "flowChartDecision"}, {111, "flowChartInputOutput"}, {112, "flowChartPredefinedProcess"},
        {113, "flowChartInternalStorage"}, {114, "flowChartDocument"}, {115, "flowChartMultidocument"},
        {116, "flowChartTerminator"}, {117, "flowChartPreparation"}, {118, "flowChartManualInput"},
        {119, "flowChartManualOperation"}, {120, "flowChartConnector"}, {121, "flowChartPunchedCard"},
        {122, "flowChartPunchedTape"}, {123, "flowChartSummingJunction"}, {124, "flowChartOr"},
        {125, "flowChartCollate"}, {126, "flowChartSort"}, {127, "flowChartExtract"}, {128, "flowChartMerge"},
        {129, "flowChartOfflineStorage"}, {130, "flowChartOnlineStorage"}, {131, "flowChartMagneticTape"},
        {132, "flowChartMagneticDisk"}, {133, "flowChartMagneticDrum"}, {134, "flowChartDisplay"},
        {135, "flowChartDelay"}, {176, "flowChartAlternateProcess"}, {177, "flowChartOffpageConnector"},
        {178, "callout1"}, {179, "accentCallout1"}, {180, "borderCallout1"}, {181, "accentBorderCallout1"},
        {182, "leftRightUpArrow"}, {183, "sun"}, {184, "moon"}, {185, "bracketPair"}, {186, "bracePair"},
        {187, "star4"}, {188, "doubleWave"}, {189, "actionButtonBlank"}, {190, "actionButtonHome"},
        {191, "actionButtonHelp"}, {192, "actionButtonInformation"}, {193, "actionButtonForwardNext"},
        {194, "actionButtonBackPrevious"}, {195, "actionButtonEnd"}, {196, "actionButtonBeginning"},
        {197, "actionButtonReturn"}, {198, "actionButtonDocument"}, {199, "actionButtonSound"},
        {200, "actionButtonMovie"}, {202, "rect"}};
    for (const auto& [id, name] : kTypes) {
        if (id == type) return name;
    }
    return nullptr;
}

} // namespace OfficeArt
} // namespace UltraCanvas
