// UltraCanvas/Plugins/Vector/UltraCanvasEMFReader.cpp
// EMF (Enhanced Metafile) reader - the import side of EMFConverter (the
// writer lives in UltraCanvasEMFConverter.cpp).
//
// Parses [MS-EMF] records into a VectorStorage document: the header frame
// sets the page size, the window/viewport records establish the logical
// unit mapping, the GDI object table (ExtCreatePen / CreatePen /
// CreateBrushIndirect / ExtCreateFontIndirectW) resolves selected styles,
// and geometry arrives through GDI paths (BeginPath .. EndPath painted by
// Fill/Stroke(AndFill)Path) plus the immediate polygon/polyline/bezier
// records common in foreign files. ExtTextOutW text becomes VectorText;
// chains of TA_UPDATECP text-out records (the writer's multi-span lines)
// merge back into one text element with spans. Unknown record types are
// counted and reported through the warning callback, never dropped
// silently.
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

struct EmfPen {
    bool null = false;
    Color color = Color(0, 0, 0, 255);
    double widthLogical = 1;
    std::vector<double> dashesLogical;
    StrokeLineCap cap = StrokeLineCap::Butt;
    StrokeLineJoin join = StrokeLineJoin::Miter;
};

struct EmfBrush {
    bool null = false;
    Color color = Color(0, 0, 0, 255);
};

struct EmfFont {
    std::string family;
    double sizeLogical = 12;
    bool bold = false, italic = false, underline = false, strike = false;
};

struct EmfObject {
    enum Kind { None, Pen, Brush, Font } kind = None;
    EmfPen pen;
    EmfBrush brush;
    EmfFont font;
};

class EmfParser {
public:
    EmfParser(const std::string& bytes, std::function<void(const std::string&)> warnFn)
            : data(reinterpret_cast<const uint8_t*>(bytes.data())),
              size(bytes.size()), warn(std::move(warnFn)) {}

    std::shared_ptr<VectorDocument> Parse() {
        if (size < 88) return nullptr;
        // EMR_HEADER: rclFrame at offset 24 (0.01 mm).
        int32_t fl = I32(24), ft = I32(28), fr = I32(32), fb = I32(36);
        pageW = (fr - fl) * 72.0 / 2540.0;
        pageH = (fb - ft) * 72.0 / 2540.0;
        if (pageW <= 0 || pageH <= 0) {
            // Fall back to the device bounds at 96 dpi.
            pageW = (I32(16) - I32(8) + 1) * 72.0 / 96.0;
            pageH = (I32(20) - I32(12) + 1) * 72.0 / 96.0;
        }
        if (pageW <= 0 || pageH <= 0) { pageW = 595; pageH = 842; }

        doc = std::make_shared<VectorDocument>();
        doc->Size = Size2Dd{pageW, pageH};
        layer = doc->AddLayer("EMF");

        size_t pos = U32(4);   // header nSize -> first record
        if (pos < 88 || pos >= size) pos = 88;
        while (pos + 8 <= size) {
            uint32_t type = U32(pos);
            uint32_t nSize = U32(pos + 4);
            if (nSize < 8 || pos + nSize > size) break;
            Record(type, pos, nSize);
            if (type == 14) break;   // EMR_EOF
            pos += nSize;
        }
        FlushChainedText();

        if (!skipped.empty()) {
            std::ostringstream msg;
            msg << "EMF import: unsupported record types skipped:";
            for (const auto& [type, count] : skipped) {
                msg << " " << type << " (x" << count << ")";
            }
            warn(msg.str());
        }
        return doc;
    }

private:
    const uint8_t* data;
    size_t size;
    std::function<void(const std::string&)> warn;
    double pageW = 0, pageH = 0;
    std::shared_ptr<VectorDocument> doc;
    std::shared_ptr<VectorLayer> layer;

    // Mapping state (defaults = identity device mapping at 96 dpi).
    double winOrgX = 0, winOrgY = 0, winExtX = 1, winExtY = 1;
    double vpOrgX = 0, vpOrgY = 0, vpExtX = 1, vpExtY = 1;

    std::map<uint32_t, EmfObject> objects;
    EmfPen curPen;
    EmfBrush curBrush{true, Color(255, 255, 255, 255)};
    EmfFont curFont;
    Color textColor = Color(0, 0, 0, 255);
    uint32_t textAlign = 0;

    bool inPath = false;
    std::shared_ptr<VectorPath> path;
    Point2Dd curPos{0, 0};        // logical
    bool subpathOpen = false;

    // Chained TA_UPDATECP text (the writer's multi-span lines).
    std::shared_ptr<VectorText> chainedText;

    std::map<uint32_t, int> skipped;

    uint32_t U32(size_t off) const {
        uint32_t v;
        std::memcpy(&v, data + off, 4);
        return v;   // x86-family little-endian; matches the writer
    }
    int32_t I32(size_t off) const { return static_cast<int32_t>(U32(off)); }
    uint16_t U16(size_t off) const {
        uint16_t v;
        std::memcpy(&v, data + off, 2);
        return v;
    }

    double PX(double xLog) const {
        return ((xLog - winOrgX) * vpExtX / winExtX + vpOrgX) * 72.0 / 96.0;
    }
    double PY(double yLog) const {
        return ((yLog - winOrgY) * vpExtY / winExtY + vpOrgY) * 72.0 / 96.0;
    }
    double ScaleX() const { return vpExtX / winExtX * 72.0 / 96.0; }
    Point2Dd P(double x, double y) const { return Point2Dd(PX(x), PY(y)); }

    void Record(uint32_t type, size_t at, uint32_t nSize) {
        size_t b = at + 8;   // body
        switch (type) {
            case 9:  winExtX = I32(b); winExtY = I32(b + 4); break;   // SETWINDOWEXTEX
            case 10: winOrgX = I32(b); winOrgY = I32(b + 4); break;   // SETWINDOWORGEX
            case 11: vpExtX = I32(b); vpExtY = I32(b + 4); break;     // SETVIEWPORTEXTEX
            case 12: vpOrgX = I32(b); vpOrgY = I32(b + 4); break;     // SETVIEWPORTORGEX
            case 14: break;   // EOF
            case 17: case 18: case 19: break;   // mapmode/bkmode/polyfill
            case 22: textAlign = U32(b); break;                       // SETTEXTALIGN
            case 24: textColor = FromColorRef(U32(b)); break;         // SETTEXTCOLOR
            case 25: break;   // SETBKCOLOR
            case 33: case 34: break;   // SAVEDC / RESTOREDC (state kept flat)
            case 27: {   // MOVETOEX
                FlushChainedText();
                curPos = Point2Dd(I32(b), I32(b + 4));
                if (inPath) {
                    EnsurePath();
                    Point2Dd p = P(curPos.x, curPos.y);
                    path->MoveTo(static_cast<float>(p.x), static_cast<float>(p.y));
                    subpathOpen = true;
                }
                break;
            }
            case 54: {   // LINETO
                Point2Dd to(I32(b), I32(b + 4));
                if (inPath) {
                    EnsurePath();
                    Point2Dd p = P(to.x, to.y);
                    path->LineTo(static_cast<float>(p.x), static_cast<float>(p.y));
                } else {
                    auto line = std::make_shared<VectorLine>();
                    line->Start = P(curPos.x, curPos.y);
                    line->End = P(to.x, to.y);
                    line->Style.Stroke = PenStroke();
                    layer->AddChild(line);
                }
                curPos = to;
                break;
            }
            case 5: case 88: {   // POLYBEZIERTO / POLYBEZIERTO16
                bool wide = (type == 5);
                size_t p = b + 16;   // skip bounds
                uint32_t count = U32(p);
                p += 4;
                EnsurePath();
                if (!subpathOpen) {
                    Point2Dd s = P(curPos.x, curPos.y);
                    path->MoveTo(static_cast<float>(s.x), static_cast<float>(s.y));
                    subpathOpen = true;
                }
                for (uint32_t i = 0; i + 2 < count; i += 3) {
                    Point2Dd c1 = ReadPt(p, wide, i);
                    Point2Dd c2 = ReadPt(p, wide, i + 1);
                    Point2Dd e = ReadPt(p, wide, i + 2);
                    Point2Dd tc1 = P(c1.x, c1.y), tc2 = P(c2.x, c2.y), te = P(e.x, e.y);
                    path->CurveTo(static_cast<float>(tc1.x), static_cast<float>(tc1.y),
                                  static_cast<float>(tc2.x), static_cast<float>(tc2.y),
                                  static_cast<float>(te.x), static_cast<float>(te.y));
                    curPos = e;
                }
                break;
            }
            case 89: {   // POLYLINETO16
                size_t p = b + 16;
                uint32_t count = U32(p);
                p += 4;
                EnsurePath();
                for (uint32_t i = 0; i < count; ++i) {
                    Point2Dd v = ReadPt(p, false, i);
                    Point2Dd t = P(v.x, v.y);
                    path->LineTo(static_cast<float>(t.x), static_cast<float>(t.y));
                    curPos = v;
                }
                break;
            }
            case 59: inPath = true; subpathOpen = false; EnsurePath(true); break;  // BEGINPATH
            case 60: inPath = false; break;                                        // ENDPATH
            case 61: if (path) { path->ClosePath(); subpathOpen = false; } break;  // CLOSEFIGURE
            case 62: PaintPath(true, false); break;    // FILLPATH
            case 63: PaintPath(true, true); break;     // STROKEANDFILLPATH
            case 64: PaintPath(false, true); break;    // STROKEPATH
            case 37: SelectObject(U32(b)); break;      // SELECTOBJECT
            case 40: objects.erase(U32(b)); break;     // DELETEOBJECT
            case 38: {   // CREATEPEN
                EmfObject o;
                o.kind = EmfObject::Pen;
                uint32_t style = U32(b + 4);
                o.pen.null = (style & 0xFF) == 5;   // PS_NULL
                o.pen.widthLogical = I32(b + 8);
                o.pen.color = FromColorRef(U32(b + 16));
                if ((style & 0xFF) == 1) o.pen.dashesLogical = {18, 6};   // PS_DASH
                objects[U32(b)] = o;
                break;
            }
            case 95: {   // EXTCREATEPEN
                EmfObject o;
                o.kind = EmfObject::Pen;
                size_t p = b + 4 + 16;   // skip ihPen + bitmap offsets
                uint32_t style = U32(p);
                o.pen.null = (style & 0xFF) == 5;
                o.pen.widthLogical = U32(p + 4);
                o.pen.color = FromColorRef(U32(p + 12));
                if (style & 0x100) o.pen.cap = StrokeLineCap::Square;
                else if (style & 0x200) o.pen.cap = StrokeLineCap::Butt;
                else o.pen.cap = StrokeLineCap::Round;
                if (style & 0x1000) o.pen.join = StrokeLineJoin::Bevel;
                else if (style & 0x2000) o.pen.join = StrokeLineJoin::Miter;
                else o.pen.join = StrokeLineJoin::Round;
                uint32_t numDash = U32(p + 20);
                for (uint32_t i = 0; i < numDash && p + 24 + 4 * i + 4 <= at + nSize;
                     ++i) {
                    o.pen.dashesLogical.push_back(U32(p + 24 + 4 * i));
                }
                objects[U32(b)] = o;
                break;
            }
            case 39: {   // CREATEBRUSHINDIRECT
                EmfObject o;
                o.kind = EmfObject::Brush;
                uint32_t style = U32(b + 4);
                o.brush.null = (style == 1);   // BS_NULL
                o.brush.color = FromColorRef(U32(b + 8));
                objects[U32(b)] = o;
                break;
            }
            case 82: {   // EXTCREATEFONTINDIRECTW
                EmfObject o;
                o.kind = EmfObject::Font;
                size_t p = b + 4;   // LOGFONTW
                int32_t height = I32(p);
                o.font.sizeLogical = std::fabs(static_cast<double>(height));
                o.font.bold = I32(p + 16) >= 600;
                o.font.italic = data[p + 20] != 0;
                o.font.underline = data[p + 21] != 0;
                o.font.strike = data[p + 22] != 0;
                for (int i = 0; i < 32; ++i) {
                    uint16_t ch = U16(p + 28 + 2 * i);
                    if (!ch) break;
                    AppendUtf8(o.font.family, ch);
                }
                objects[U32(b)] = o;
                break;
            }
            case 84: TextOut(at, nSize, true); break;   // EXTTEXTOUTW
            case 83: TextOut(at, nSize, false); break;  // EXTTEXTOUTA
            case 3: case 86: Polygon(b, type == 3, true); break;    // POLYGON(16)
            case 4: case 87: Polygon(b, type == 4, false); break;   // POLYLINE(16)
            case 43: {   // RECTANGLE
                auto rect = std::make_shared<VectorRect>();
                Point2Dd tl = P(I32(b), I32(b + 4));
                Point2Dd br = P(I32(b + 8), I32(b + 12));
                rect->Bounds = Rect2Dd{tl.x, tl.y, br.x - tl.x, br.y - tl.y};
                ApplyFillStroke(rect->Style);
                layer->AddChild(rect);
                break;
            }
            case 42: {   // ELLIPSE
                auto el = std::make_shared<VectorEllipse>();
                Point2Dd tl = P(I32(b), I32(b + 4));
                Point2Dd br = P(I32(b + 8), I32(b + 12));
                el->Center = Point2Dd((tl.x + br.x) / 2, (tl.y + br.y) / 2);
                el->RadiusX = static_cast<float>(std::fabs(br.x - tl.x) / 2);
                el->RadiusY = static_cast<float>(std::fabs(br.y - tl.y) / 2);
                ApplyFillStroke(el->Style);
                layer->AddChild(el);
                break;
            }
            default:
                ++skipped[type];
                break;
        }
        // Any drawing/state record other than a text-out breaks a span chain.
        if (type != 84 && type != 83 && type != 37 && type != 40 && type != 82 &&
            type != 22 && type != 24) {
            if (type != 27) FlushChainedText();
        }
    }

    Point2Dd ReadPt(size_t base, bool wide, uint32_t index) const {
        if (wide) {
            return Point2Dd(I32(base + 8 * index), I32(base + 8 * index + 4));
        }
        return Point2Dd(static_cast<int16_t>(U16(base + 4 * index)),
                        static_cast<int16_t>(U16(base + 4 * index + 2)));
    }

    void EnsurePath(bool fresh = false) {
        if (fresh || !path) path = std::make_shared<VectorPath>();
    }

    StrokeData PenStroke() const {
        StrokeData s;
        s.Fill = curPen.color;
        s.Width = static_cast<float>(
                std::max(0.1, curPen.widthLogical * ScaleX()));
        s.LineCap = curPen.cap;
        s.LineJoin = curPen.join;
        for (double d : curPen.dashesLogical) {
            s.DashArray.push_back(d * ScaleX());
        }
        return s;
    }

    void ApplyFillStroke(VectorStyle& style) {
        if (!curBrush.null) style.Fill = curBrush.color;
        if (!curPen.null) style.Stroke = PenStroke();
    }

    void SelectObject(uint32_t ih) {
        if (ih & 0x80000000) {   // stock object
            switch (ih & 0xFF) {
                case 5: curBrush = {true, Color(255, 255, 255, 255)}; break;  // NULL_BRUSH
                case 8: curPen.null = true; break;                            // NULL_PEN
                case 0: curBrush = {false, Color(255, 255, 255, 255)}; break; // WHITE_BRUSH
                case 4: curBrush = {false, Color(0, 0, 0, 255)}; break;       // BLACK_BRUSH
                case 6: curPen = {}; curPen.color = Color(255, 255, 255, 255); break;
                case 7: curPen = {}; break;                                   // BLACK_PEN
                default: break;
            }
            return;
        }
        auto it = objects.find(ih);
        if (it == objects.end()) return;
        switch (it->second.kind) {
            case EmfObject::Pen: curPen = it->second.pen; break;
            case EmfObject::Brush: curBrush = it->second.brush; break;
            case EmfObject::Font: curFont = it->second.font; break;
            default: break;
        }
    }

    void PaintPath(bool fill, bool stroke) {
        if (!path) return;
        if (fill && !curBrush.null) path->Style.Fill = curBrush.color;
        if (stroke && !curPen.null) path->Style.Stroke = PenStroke();
        bool any = path->Style.Fill.has_value() || path->Style.Stroke.has_value();
        if (any && !path->Path.commands.empty()) layer->AddChild(path);
        path.reset();
        subpathOpen = false;
    }

    static void AppendUtf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    void ApplyFont(VectorTextStyle& style) const {
        if (!curFont.family.empty()) style.FontFamily = curFont.family;
        style.FontSize = static_cast<float>(
                std::max(1.0, curFont.sizeLogical * ScaleX()));
        style.Weight = curFont.bold ? FontWeight::Bold : FontWeight::Normal;
        style.Slant = curFont.italic ? FontSlant::Italic : FontSlant::Normal;
        style.Underline = curFont.underline;
        style.StrikeThrough = curFont.strike;
    }

    void TextOut(size_t at, uint32_t nSize, bool wide) {
        size_t b = at + 8;
        // EMRTEXT after bounds(16) + graphics mode(4) + ex/ey scale(8).
        size_t t = b + 28;
        if (t + 40 > at + nSize) return;
        Point2Dd ref(I32(t), I32(t + 4));
        uint32_t nChars = U32(t + 8);
        uint32_t offString = U32(t + 12);
        size_t str = at + offString;
        if (str + (wide ? nChars * 2 : nChars) > at + nSize) return;

        std::string utf8;
        for (uint32_t i = 0; i < nChars; ++i) {
            uint32_t cp = wide ? U16(str + 2 * i) : data[str + i];
            if (cp >= 0xD800 && cp < 0xDC00 && wide && i + 1 < nChars) {
                uint32_t lo = U16(str + 2 * (i + 1));
                if (lo >= 0xDC00 && lo < 0xE000) {
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    ++i;
                    // encode 4-byte
                    utf8.push_back(static_cast<char>(0xF0 | (cp >> 18)));
                    utf8.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
                    utf8.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
                    utf8.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
                    continue;
                }
            }
            AppendUtf8(utf8, cp);
        }
        if (utf8.empty()) return;

        bool updateCp = (textAlign & 1) != 0;   // TA_UPDATECP: span chain
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
        // The writer chains from the pen position set by MOVETOEX; a
        // standalone record anchors at its reference point.
        Point2Dd anchor = updateCp ? P(curPos.x, curPos.y) : P(ref.x, ref.y);
        if (!(textAlign & 24)) {
            // TA_TOP (default): the reference is the cell top, not the
            // baseline; approximate the baseline one em down.
            anchor.y += curFont.sizeLogical * ScaleX();
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

    void FlushChainedText() { chainedText.reset(); }

    void Polygon(size_t b, bool wide, bool closed) {
        size_t p = b + 16;
        uint32_t count = U32(p);
        p += 4;
        if (count < 2) return;
        if (closed) {
            auto poly = std::make_shared<VectorPolygon>();
            for (uint32_t i = 0; i < count; ++i) {
                Point2Dd v = ReadPt(p, wide, i);
                poly->Points.push_back(P(v.x, v.y));
            }
            ApplyFillStroke(poly->Style);
            layer->AddChild(poly);
        } else {
            auto poly = std::make_shared<VectorPolyline>();
            for (uint32_t i = 0; i < count; ++i) {
                Point2Dd v = ReadPt(p, wide, i);
                poly->Points.push_back(P(v.x, v.y));
            }
            if (!curPen.null) poly->Style.Stroke = PenStroke();
            layer->AddChild(poly);
        }
    }
};

}   // anonymous namespace

std::shared_ptr<VectorStorage::VectorDocument> EMFConverter::ImportFromString(
        const std::string& data, const ConversionOptions& options) {
    if (!ValidateData(data)) {
        if (options.WarningCallback) {
            options.WarningCallback("Not an EMF file (missing ' EMF' signature)");
        }
        return nullptr;
    }
    EmfParser parser(data, [&options](const std::string& msg) {
        if (options.WarningCallback) options.WarningCallback(msg);
    });
    auto doc = parser.Parse();
    if (options.ProgressCallback) options.ProgressCallback(1.0f);
    return doc;
}

} // namespace VectorConverter
} // namespace UltraCanvas
