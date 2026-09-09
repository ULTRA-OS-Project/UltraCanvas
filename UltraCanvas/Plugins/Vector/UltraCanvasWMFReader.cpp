// UltraCanvas/Plugins/Vector/UltraCanvasWMFReader.cpp
// WMF (legacy Windows Metafile) reader - the import side of WMFConverter
// (the writer lives in UltraCanvasWMFConverter.cpp).
//
// Parses [MS-WMF] 16-bit records into a VectorStorage document. The
// placeable (Aldus) header supplies the page rectangle and units-per-inch;
// files without one fall back to twips. The GDI object table (pens,
// brushes, fonts in lowest-free-slot order) resolves selected styles;
// geometry arrives through Polygon/PolyPolygon/Polyline/Rectangle/Ellipse
// and MoveTo/LineTo records, text through TextOut/ExtTextOut (chains of
// TA_UPDATECP text records - the writer's multi-span lines - merge back
// into one text element with spans). WMF has no bezier record, so curves
// arrive as the polylines the producer flattened them to. Unknown record
// types are counted and reported through the warning callback.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "UltraCanvasMetafileConverters.h"
#include "UltraCanvasVectorStorage.h"

#include <cmath>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace VectorConverter {

using namespace VectorStorage;

namespace {

Color FromColorRef(uint32_t v) {   // 0x00BBGGRR
    return Color(v & 0xFF, (v >> 8) & 0xFF, (v >> 16) & 0xFF, 255);
}

struct WmfPen {
    bool null = false;
    Color color = Color(0, 0, 0, 255);
    double widthUnits = 1;
    bool dashed = false;
};

struct WmfBrush {
    bool null = true;
    Color color = Color(255, 255, 255, 255);
};

struct WmfFont {
    std::string family;
    double sizeUnits = 240;
    bool bold = false, italic = false, underline = false, strike = false;
};

struct WmfObject {
    enum Kind { None, Pen, Brush, Font } kind = None;
    WmfPen pen;
    WmfBrush brush;
    WmfFont font;
};

class WmfParser {
public:
    WmfParser(const std::string& bytes, std::function<void(const std::string&)> warnFn)
            : data(reinterpret_cast<const uint8_t*>(bytes.data())),
              size(bytes.size()), warn(std::move(warnFn)) {}

    std::shared_ptr<VectorDocument> Parse() {
        size_t pos = 0;
        double bboxLeft = 0, bboxTop = 0, bboxRight = 0, bboxBottom = 0;
        double unitsPerInch = 1440;
        bool placeable = size >= 22 && U32(0) == 0x9AC6CDD7;
        if (placeable) {
            bboxLeft = I16(6);
            bboxTop = I16(8);
            bboxRight = I16(10);
            bboxBottom = I16(12);
            uint16_t inch = U16(14);
            if (inch > 0) unitsPerInch = inch;
            pos = 22;
        } else {
            warn("WMF import: no placeable header; assuming twips units");
        }
        if (size < pos + 18) return nullptr;
        pos += 18;   // standard header (9 words)

        scale = 72.0 / unitsPerInch;
        orgX = bboxLeft;
        orgY = bboxTop;
        pageW = (bboxRight - bboxLeft) * scale;
        pageH = (bboxBottom - bboxTop) * scale;

        doc = std::make_shared<VectorDocument>();
        layer = doc->AddLayer("WMF");

        while (pos + 6 <= size) {
            uint32_t words = U32(pos);
            uint16_t func = U16(pos + 4);
            if (words < 3 || pos + words * 2 > size) break;
            if (func == 0) break;   // META_EOF
            Record(func, pos + 6, words * 2 - 6);
            pos += words * 2;
        }
        FlushChainedText();

        if (pageW <= 0 || pageH <= 0) {
            // No placeable bounds: size from the window extent, else content.
            if (winExtX > 0 && winExtY > 0) {
                pageW = winExtX * scale;
                pageH = winExtY * scale;
            } else {
                Rect2Dd bbox = doc->GetBoundingBox();
                pageW = bbox.x + bbox.width;
                pageH = bbox.y + bbox.height;
            }
            if (pageW <= 0) pageW = 595;
            if (pageH <= 0) pageH = 842;
        }
        doc->Size = Size2Dd{pageW, pageH};

        if (!skipped.empty()) {
            std::ostringstream msg;
            msg << "WMF import: unsupported record types skipped:";
            for (const auto& [func, count] : skipped) {
                char buf[16];
                std::snprintf(buf, sizeof(buf), " 0x%04X", func);
                msg << buf << " (x" << count << ")";
            }
            warn(msg.str());
        }
        return doc;
    }

private:
    const uint8_t* data;
    size_t size;
    std::function<void(const std::string&)> warn;
    double scale = 72.0 / 1440.0;
    double orgX = 0, orgY = 0;
    double winExtX = 0, winExtY = 0;
    double pageW = 0, pageH = 0;
    std::shared_ptr<VectorDocument> doc;
    std::shared_ptr<VectorLayer> layer;

    std::vector<WmfObject> slots;   // GDI object table, lowest-free-slot
    WmfPen curPen;
    WmfBrush curBrush;
    WmfFont curFont;
    Color textColor = Color(0, 0, 0, 255);
    uint16_t textAlign = 0;
    Point2Dd curPos{0, 0};   // units
    std::shared_ptr<VectorText> chainedText;
    std::map<uint16_t, int> skipped;

    uint16_t U16(size_t off) const {
        uint16_t v;
        std::memcpy(&v, data + off, 2);
        return v;
    }
    int16_t I16(size_t off) const { return static_cast<int16_t>(U16(off)); }
    uint32_t U32(size_t off) const {
        uint32_t v;
        std::memcpy(&v, data + off, 4);
        return v;
    }

    double PX(double x) const { return (x - orgX) * scale; }
    double PY(double y) const { return (y - orgY) * scale; }
    Point2Dd P(double x, double y) const { return Point2Dd(PX(x), PY(y)); }

    StrokeData PenStroke() const {
        StrokeData s;
        s.Fill = curPen.color;
        s.Width = static_cast<float>(std::max(0.1, curPen.widthUnits * scale));
        if (curPen.dashed) s.DashArray = {6.0, 3.0};   // PS_DASH approximation
        return s;
    }

    void ApplyFillStroke(VectorStyle& style) const {
        if (!curBrush.null) style.Fill = curBrush.color;
        if (!curPen.null) style.Stroke = PenStroke();
    }

    void Alloc(WmfObject obj) {
        for (auto& slot : slots) {
            if (slot.kind == WmfObject::None) {
                slot = obj;
                return;
            }
        }
        slots.push_back(obj);
    }

    void Record(uint16_t func, size_t b, size_t len) {
        switch (func) {
            case 0x0103: break;   // SETMAPMODE
            case 0x0102: case 0x0106: break;   // SETBKMODE / SETPOLYFILLMODE
            case 0x020B: orgY += I16(b); orgX += I16(b + 2); break;  // SETWINDOWORG (y,x)
            case 0x020C: winExtY = I16(b); winExtX = I16(b + 2); break;  // SETWINDOWEXT
            case 0x0209: textColor = FromColorRef(U32(b)); break;    // SETTEXTCOLOR
            case 0x012E: textAlign = U16(b); break;                  // SETTEXTALIGN
            case 0x0214: {   // MOVETO (y, x)
                FlushChainedText();
                curPos = Point2Dd(I16(b + 2), I16(b));
                break;
            }
            case 0x0213: {   // LINETO (y, x)
                Point2Dd to(I16(b + 2), I16(b));
                auto line = std::make_shared<VectorLine>();
                line->Start = P(curPos.x, curPos.y);
                line->End = P(to.x, to.y);
                if (!curPen.null) line->Style.Stroke = PenStroke();
                layer->AddChild(line);
                curPos = to;
                break;
            }
            case 0x0324: {   // POLYGON
                uint16_t n = U16(b);
                auto poly = std::make_shared<VectorPolygon>();
                for (uint16_t i = 0; i < n; ++i) {
                    poly->Points.push_back(
                            P(I16(b + 2 + 4 * i), I16(b + 4 + 4 * i)));
                }
                // GDI closes polygons implicitly; drop a repeated last point.
                if (poly->Points.size() > 2 &&
                    std::fabs(poly->Points.front().x - poly->Points.back().x) < 1e-9 &&
                    std::fabs(poly->Points.front().y - poly->Points.back().y) < 1e-9) {
                    poly->Points.pop_back();
                }
                ApplyFillStroke(poly->Style);
                layer->AddChild(poly);
                break;
            }
            case 0x0325: {   // POLYLINE
                uint16_t n = U16(b);
                auto poly = std::make_shared<VectorPolyline>();
                for (uint16_t i = 0; i < n; ++i) {
                    poly->Points.push_back(
                            P(I16(b + 2 + 4 * i), I16(b + 4 + 4 * i)));
                }
                if (!curPen.null) poly->Style.Stroke = PenStroke();
                layer->AddChild(poly);
                break;
            }
            case 0x0538: {   // POLYPOLYGON
                uint16_t nPolys = U16(b);
                size_t counts = b + 2;
                size_t pts = counts + 2 * nPolys;
                auto path = std::make_shared<VectorPath>();
                size_t at = pts;
                for (uint16_t pi = 0; pi < nPolys; ++pi) {
                    uint16_t n = U16(counts + 2 * pi);
                    for (uint16_t i = 0; i < n; ++i) {
                        Point2Dd v = P(I16(at), I16(at + 2));
                        at += 4;
                        if (i == 0) {
                            path->MoveTo(static_cast<float>(v.x),
                                         static_cast<float>(v.y));
                        } else {
                            path->LineTo(static_cast<float>(v.x),
                                         static_cast<float>(v.y));
                        }
                    }
                    path->ClosePath();
                }
                ApplyFillStroke(path->Style);
                layer->AddChild(path);
                break;
            }
            case 0x041B: {   // RECTANGLE (bottom, right, top, left)
                Point2Dd br = P(I16(b + 2), I16(b));
                Point2Dd tl = P(I16(b + 6), I16(b + 4));
                auto rect = std::make_shared<VectorRect>();
                rect->Bounds = Rect2Dd{tl.x, tl.y, br.x - tl.x, br.y - tl.y};
                ApplyFillStroke(rect->Style);
                layer->AddChild(rect);
                break;
            }
            case 0x0418: {   // ELLIPSE (bottom, right, top, left)
                Point2Dd br = P(I16(b + 2), I16(b));
                Point2Dd tl = P(I16(b + 6), I16(b + 4));
                auto el = std::make_shared<VectorEllipse>();
                el->Center = Point2Dd((tl.x + br.x) / 2, (tl.y + br.y) / 2);
                el->RadiusX = static_cast<float>(std::fabs(br.x - tl.x) / 2);
                el->RadiusY = static_cast<float>(std::fabs(br.y - tl.y) / 2);
                ApplyFillStroke(el->Style);
                layer->AddChild(el);
                break;
            }
            case 0x0521: {   // TEXTOUT: count, chars (padded), YStart, XStart
                uint16_t n = U16(b);
                size_t strAt = b + 2;
                size_t padded = n + (n % 2);
                if (strAt + padded + 4 > b + len) break;
                double y = I16(strAt + padded);
                double x = I16(strAt + padded + 2);
                EmitText(std::string(reinterpret_cast<const char*>(data + strAt), n),
                         Point2Dd(x, y));
                break;
            }
            case 0x0A32: {   // EXTTEXTOUT: y, x, count, options, [rect], chars
                double y = I16(b), x = I16(b + 2);
                uint16_t n = U16(b + 4);
                uint16_t opts = U16(b + 6);
                size_t strAt = b + 8 + ((opts & 0x6) ? 8 : 0);   // clip/opaque rect
                if (strAt + n > b + len) break;
                EmitText(std::string(reinterpret_cast<const char*>(data + strAt), n),
                         Point2Dd(x, y));
                break;
            }
            case 0x02FA: {   // CREATEPENINDIRECT
                WmfObject o;
                o.kind = WmfObject::Pen;
                uint16_t style = U16(b);
                o.pen.null = (style & 0xFF) == 5;
                o.pen.dashed = (style & 0xFF) >= 1 && (style & 0xFF) <= 4;
                o.pen.widthUnits = I16(b + 2);
                o.pen.color = FromColorRef(U32(b + 6));
                Alloc(o);
                break;
            }
            case 0x02FC: {   // CREATEBRUSHINDIRECT
                WmfObject o;
                o.kind = WmfObject::Brush;
                o.brush.null = U16(b) == 1;   // BS_NULL
                o.brush.color = FromColorRef(U32(b + 2));
                Alloc(o);
                break;
            }
            case 0x02FB: {   // CREATEFONTINDIRECT (LOGFONT16)
                WmfObject o;
                o.kind = WmfObject::Font;
                o.font.sizeUnits = std::fabs(static_cast<double>(I16(b)));
                o.font.bold = I16(b + 8) >= 600;
                o.font.italic = data[b + 10] != 0;
                o.font.underline = data[b + 11] != 0;
                o.font.strike = data[b + 12] != 0;
                for (size_t i = b + 18; i < b + len && data[i]; ++i) {
                    o.font.family.push_back(static_cast<char>(data[i]));
                }
                Alloc(o);
                break;
            }
            case 0x012D: {   // SELECTOBJECT
                uint16_t idx = U16(b);
                if (idx < slots.size()) {
                    const WmfObject& o = slots[idx];
                    if (o.kind == WmfObject::Pen) curPen = o.pen;
                    else if (o.kind == WmfObject::Brush) curBrush = o.brush;
                    else if (o.kind == WmfObject::Font) curFont = o.font;
                }
                break;
            }
            case 0x01F0: {   // DELETEOBJECT
                uint16_t idx = U16(b);
                if (idx < slots.size()) slots[idx] = WmfObject{};
                break;
            }
            default:
                ++skipped[func];
                break;
        }
        // Anything except text records and object management breaks a chain.
        if (func != 0x0521 && func != 0x0A32 && func != 0x012D &&
            func != 0x01F0 && func != 0x02FB && func != 0x012E &&
            func != 0x0209 && func != 0x0214) {
            FlushChainedText();
        }
    }

    void EmitText(const std::string& latin1, const Point2Dd& refUnits) {
        if (latin1.empty()) return;
        // TEXTOUT strings are byte-encoded (Latin-1); expand to UTF-8.
        std::string utf8;
        for (unsigned char ch : latin1) {
            if (ch < 0x80) {
                utf8.push_back(static_cast<char>(ch));
            } else {
                utf8.push_back(static_cast<char>(0xC0 | (ch >> 6)));
                utf8.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
            }
        }

        bool updateCp = (textAlign & 1) != 0;
        if (updateCp && chainedText) {
            TextSpanData span;
            span.Text = utf8;
            span.Style = chainedText->BaseStyle;
            ApplyFont(span.Style);
            chainedText->Spans.push_back(span);
            return;
        }
        FlushChainedText();

        auto text = std::make_shared<VectorText>();
        Point2Dd anchor = updateCp ? P(curPos.x, curPos.y)
                                   : P(refUnits.x, refUnits.y);
        if (!(textAlign & 24)) {
            anchor.y += curFont.sizeUnits * scale;   // TA_TOP -> baseline approx
        }
        text->Position = anchor;
        ApplyFont(text->BaseStyle);
        if ((textAlign & 6) == 6) text->BaseStyle.Anchor = TextAnchor::Middle;
        else if (textAlign & 2) text->BaseStyle.Anchor = TextAnchor::End;
        text->Style.Fill = textColor;
        text->SetText(utf8);
        layer->AddChild(text);
        if (updateCp) chainedText = text;
    }

    void ApplyFont(VectorTextStyle& style) const {
        if (!curFont.family.empty()) style.FontFamily = curFont.family;
        style.FontSize = static_cast<float>(
                std::max(1.0, curFont.sizeUnits * scale));
        style.Weight = curFont.bold ? FontWeight::Bold : FontWeight::Normal;
        style.Slant = curFont.italic ? FontSlant::Italic : FontSlant::Normal;
        style.Underline = curFont.underline;
        style.StrikeThrough = curFont.strike;
    }

    void FlushChainedText() { chainedText.reset(); }
};

}   // anonymous namespace

std::shared_ptr<VectorStorage::VectorDocument> WMFConverter::ImportFromString(
        const std::string& data, const ConversionOptions& options) {
    if (!ValidateData(data)) {
        if (options.WarningCallback) {
            options.WarningCallback("Not a WMF file (no placeable or standard header)");
        }
        return nullptr;
    }
    WmfParser parser(data, [&options](const std::string& msg) {
        if (options.WarningCallback) options.WarningCallback(msg);
    });
    auto doc = parser.Parse();
    if (options.ProgressCallback) options.ProgressCallback(1.0f);
    return doc;
}

} // namespace VectorConverter
} // namespace UltraCanvas
