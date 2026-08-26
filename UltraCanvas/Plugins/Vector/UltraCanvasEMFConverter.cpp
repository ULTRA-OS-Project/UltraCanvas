// UltraCanvas/Plugins/Vector/UltraCanvasEMFConverter.cpp
// EMF (Enhanced Metafile) writer - see UltraCanvasMetafileConverters.h.
//
// Record layouts follow [MS-EMF] (the public Microsoft specification). The
// drawing maps through MM_ANISOTROPIC with 20 logical units per point for
// sub-point precision; geometry goes through GDI paths (BeginPath /
// MoveToEx / LineTo / PolyBezierTo / CloseFigure, painted with FillPath /
// StrokePath / StrokeAndFillPath), pens are ExtCreatePen geometric pens
// with caps, joins and user-style dash entries, and text is ExtTextOutW
// with SetTextAlign handling the anchoring. The document tree walks like
// the other writers': styles resolve by inheritance and transforms bake
// into coordinates through the shared PathOps normalisation.
//
// GDI metafiles have no alpha channel (that arrived with GDI+ "EMF+"
// records), so opacity flattens toward the white page with a warning, and
// gradients fall back to the blend of their end stops.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "UltraCanvasMetafileConverters.h"
#include "UltraCanvasVectorStorage.h"
#include "UltraCanvasVectorPathOps.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <variant>

namespace UltraCanvas {
namespace VectorConverter {

using namespace VectorStorage;

namespace {

using namespace PathOps;

// [MS-EMF] record types used here.
enum : uint32_t {
    EMR_POLYBEZIERTO = 5,
    EMR_SETWINDOWEXTEX = 9,
    EMR_SETWINDOWORGEX = 10,
    EMR_SETVIEWPORTEXTEX = 11,
    EMR_SETVIEWPORTORGEX = 12,
    EMR_EOF = 14,
    EMR_SETMAPMODE = 17,
    EMR_SETBKMODE = 18,
    EMR_SETPOLYFILLMODE = 19,
    EMR_SETTEXTALIGN = 22,
    EMR_SETTEXTCOLOR = 24,
    EMR_MOVETOEX = 27,
    EMR_SELECTOBJECT = 37,
    EMR_CREATEBRUSHINDIRECT = 39,
    EMR_DELETEOBJECT = 40,
    EMR_LINETO = 54,
    EMR_BEGINPATH = 59,
    EMR_ENDPATH = 60,
    EMR_CLOSEFIGURE = 61,
    EMR_FILLPATH = 62,
    EMR_STROKEANDFILLPATH = 63,
    EMR_STROKEPATH = 64,
    EMR_EXTCREATEFONTINDIRECTW = 82,
    EMR_EXTTEXTOUTW = 84,
    EMR_EXTCREATEPEN = 95,
};

constexpr uint32_t kStockNullPen = 0x80000008;    // NULL_PEN
constexpr uint32_t kStockNullBrush = 0x80000005;  // NULL_BRUSH (hollow)

constexpr double kUnitsPerPoint = 20.0;   // logical units (window space)
constexpr double kDevPerPoint = 96.0 / 72.0;   // device units at 96 dpi

class EmfBuf {
public:
    std::vector<uint8_t> bytes;

    void U8(uint8_t v) { bytes.push_back(v); }
    void U16(uint16_t v) {
        bytes.push_back(static_cast<uint8_t>(v));
        bytes.push_back(static_cast<uint8_t>(v >> 8));
    }
    void U32(uint32_t v) {
        bytes.push_back(static_cast<uint8_t>(v));
        bytes.push_back(static_cast<uint8_t>(v >> 8));
        bytes.push_back(static_cast<uint8_t>(v >> 16));
        bytes.push_back(static_cast<uint8_t>(v >> 24));
    }
    void I32(int32_t v) { U32(static_cast<uint32_t>(v)); }
    void F32(float v) {
        uint32_t bits;
        std::memcpy(&bits, &v, sizeof(bits));
        U32(bits);
    }
    void PatchU32(size_t offset, uint32_t v) {
        bytes[offset] = static_cast<uint8_t>(v);
        bytes[offset + 1] = static_cast<uint8_t>(v >> 8);
        bytes[offset + 2] = static_cast<uint8_t>(v >> 16);
        bytes[offset + 3] = static_cast<uint8_t>(v >> 24);
    }
};

uint32_t ColorRef(const Color& c) {   // 0x00BBGGRR
    return (static_cast<uint32_t>(c.b) << 16) |
           (static_cast<uint32_t>(c.g) << 8) | c.r;
}

class EmfEmitter {
public:
    EmfEmitter(const VectorDocument& document,
               std::function<void(const std::string&)> warnFn)
            : doc(document), warn(std::move(warnFn)) {}

    std::string Build() {
        pageW = doc.Size.width;
        pageH = doc.Size.height;
        if (pageW <= 0 || pageH <= 0) {
            Rect2Dd bbox = doc.GetBoundingBox();
            pageW = bbox.x + bbox.width;
            pageH = bbox.y + bbox.height;
            if (pageW <= 0) pageW = 595;
            if (pageH <= 0) pageH = 842;
        }
        devW = static_cast<int32_t>(std::lround(pageW * kDevPerPoint));
        devH = static_cast<int32_t>(std::lround(pageH * kDevPerPoint));

        // EMR_HEADER: 88 bytes; nBytes/nRecords/nHandles patched at the end.
        out.U32(1);          // EMR_HEADER
        out.U32(88);
        out.I32(0); out.I32(0); out.I32(devW - 1); out.I32(devH - 1);   // rclBounds (device)
        int32_t frameW = static_cast<int32_t>(std::lround(pageW / 72.0 * 25.4 * 100));
        int32_t frameH = static_cast<int32_t>(std::lround(pageH / 72.0 * 25.4 * 100));
        out.I32(0); out.I32(0); out.I32(frameW); out.I32(frameH);       // rclFrame (0.01mm)
        out.U32(0x464D4520); // " EMF"
        out.U32(0x00010000); // version
        nBytesOffset = out.bytes.size();
        out.U32(0);          // nBytes (patched)
        nRecordsOffset = out.bytes.size();
        out.U32(0);          // nRecords (patched)
        nHandlesOffset = out.bytes.size();
        out.U16(0);          // nHandles (patched)
        out.U16(0);          // reserved
        out.U32(0);          // nDescription
        out.U32(0);          // offDescription
        out.U32(0);          // nPalEntries
        out.U32(1920); out.U32(1080);   // szlDevice (px)  -> ~96 dpi with
        out.U32(508); out.U32(286);     // szlMillimeters      the pair below
        records = 1;

        // Map logical (points * 20, Y down, origin top-left) onto the page.
        Rec(EMR_SETMAPMODE, [&](EmfBuf& b) { b.U32(8); });   // MM_ANISOTROPIC
        Rec(EMR_SETWINDOWORGEX, [&](EmfBuf& b) { b.I32(0); b.I32(0); });
        Rec(EMR_SETWINDOWEXTEX, [&](EmfBuf& b) {
            b.I32(static_cast<int32_t>(std::lround(pageW * kUnitsPerPoint)));
            b.I32(static_cast<int32_t>(std::lround(pageH * kUnitsPerPoint)));
        });
        Rec(EMR_SETVIEWPORTORGEX, [&](EmfBuf& b) { b.I32(0); b.I32(0); });
        Rec(EMR_SETVIEWPORTEXTEX, [&](EmfBuf& b) { b.I32(devW); b.I32(devH); });
        Rec(EMR_SETPOLYFILLMODE, [&](EmfBuf& b) { b.U32(2); });   // WINDING
        Rec(EMR_SETBKMODE, [&](EmfBuf& b) { b.U32(1); });         // TRANSPARENT

        for (const auto& layer : doc.Layers) {
            if (!layer || !layer->Visible) continue;
            for (const auto& child : layer->Children) {
                if (child) EmitElement(*child, layer->Style, Matrix3x3::Identity());
            }
        }

        Rec(EMR_EOF, [&](EmfBuf& b) {
            b.U32(0);    // nPalEntries
            b.U32(16);   // offPalEntries
            b.U32(20);   // nSizeLast (this record's size)
        });

        out.PatchU32(nBytesOffset, static_cast<uint32_t>(out.bytes.size()));
        out.PatchU32(nRecordsOffset, records);
        out.bytes[nHandlesOffset] = static_cast<uint8_t>(nextHandle);
        out.bytes[nHandlesOffset + 1] = static_cast<uint8_t>(nextHandle >> 8);
        return std::string(out.bytes.begin(), out.bytes.end());
    }

private:
    const VectorDocument& doc;
    std::function<void(const std::string&)> warn;
    EmfBuf out;
    double pageW = 0, pageH = 0;
    int32_t devW = 0, devH = 0;
    size_t nBytesOffset = 0, nRecordsOffset = 0, nHandlesOffset = 0;
    uint32_t records = 0;
    uint32_t nextHandle = 1;
    bool warnedAlpha = false, warnedGradient = false, warnedAnchor = false;

    template <typename Fn>
    void Rec(uint32_t type, Fn&& fill) {
        EmfBuf body;
        fill(body);
        while (body.bytes.size() % 4) body.U8(0);
        out.U32(type);
        out.U32(static_cast<uint32_t>(8 + body.bytes.size()));
        out.bytes.insert(out.bytes.end(), body.bytes.begin(), body.bytes.end());
        ++records;
    }

    int32_t LX(double xPt) const {
        return static_cast<int32_t>(std::lround(xPt * kUnitsPerPoint));
    }
    int32_t LY(double yPt) const {
        return static_cast<int32_t>(std::lround(yPt * kUnitsPerPoint));
    }
    static double AvgScale(const Matrix3x3& m) {
        double det = std::fabs(static_cast<double>(m.m[0][0]) * m.m[1][1] -
                               static_cast<double>(m.m[0][1]) * m.m[1][0]);
        return det > 0 ? std::sqrt(det) : 1.0;
    }
    static bool AxisAligned(const Matrix3x3& m) {
        return std::fabs(m.m[0][1]) < 1e-6 && std::fabs(m.m[1][0]) < 1e-6;
    }

    Color Flatten(const Color& c, float opacity) {
        float a = (c.a / 255.0f) * opacity;
        if (a < 0.999f && !warnedAlpha) {
            warnedAlpha = true;
            warn("EMF export: GDI metafiles have no transparency; "
                 "opacity is flattened toward white");
        }
        a = std::max(0.0f, std::min(1.0f, a));
        auto ch = [a](uint8_t v) {
            return static_cast<uint8_t>(std::lround(v * a + 255.0 * (1.0 - a)));
        };
        return Color(ch(c.r), ch(c.g), ch(c.b), 255);
    }

    Color FlattenFill(const FillData& fill, float opacity) {
        if (const Color* c = std::get_if<Color>(&fill)) return Flatten(*c, opacity);
        if (const GradientData* g = std::get_if<GradientData>(&fill)) {
            if (!warnedGradient) {
                warnedGradient = true;
                warn("EMF export: gradients are not written yet; "
                     "filling with the blend of the end stops");
            }
            const std::vector<GradientStop>* stops = nullptr;
            if (const auto* lg = std::get_if<LinearGradientData>(g)) stops = &lg->Stops;
            else if (const auto* rg = std::get_if<RadialGradientData>(g)) stops = &rg->Stops;
            else if (const auto* cg = std::get_if<ConicalGradientData>(g)) stops = &cg->Stops;
            if (stops && !stops->empty()) {
                const Color& c0 = stops->front().color;
                const Color& c1 = stops->back().color;
                return Flatten(Color(static_cast<uint8_t>((c0.r + c1.r) / 2),
                                     static_cast<uint8_t>((c0.g + c1.g) / 2),
                                     static_cast<uint8_t>((c0.b + c1.b) / 2),
                                     static_cast<uint8_t>((c0.a + c1.a) / 2)),
                               opacity);
            }
            return Color(128, 128, 128, 255);
        }
        warn("EMF export: pattern/reference fills are not supported, "
             "filling flat black");
        return Color(0, 0, 0, 255);
    }

    static bool HasVisibleFill(const VectorStyle& s) {
        return s.Fill.has_value() && !std::holds_alternative<std::monostate>(*s.Fill);
    }
    static bool HasVisibleStroke(const VectorStyle& s) {
        return s.Stroke.has_value() && s.Stroke->Width > 0 &&
               !std::holds_alternative<std::monostate>(s.Stroke->Fill);
    }

    // ===== OBJECTS =====

    uint32_t CreateBrush(const Color& c) {
        uint32_t ih = nextHandle++;
        Rec(EMR_CREATEBRUSHINDIRECT, [&](EmfBuf& b) {
            b.U32(ih);
            b.U32(0);            // BS_SOLID
            b.U32(ColorRef(c));
            b.U32(0);            // hatch
        });
        return ih;
    }

    uint32_t CreatePen(const StrokeData& st, const Color& c, double scale) {
        uint32_t ih = nextHandle++;
        uint32_t style = 0x00010000;   // PS_GEOMETRIC
        std::vector<uint32_t> dashes;
        if (!st.DashArray.empty()) {
            style |= 7;   // PS_USERSTYLE
            for (double d : st.DashArray) {
                dashes.push_back(static_cast<uint32_t>(
                        std::max(1.0, std::round(d * scale * kUnitsPerPoint))));
            }
        }
        if (st.LineCap == StrokeLineCap::Square) style |= 0x00000100;      // ENDCAP_SQUARE
        else if (st.LineCap == StrokeLineCap::Butt) style |= 0x00000200;   // ENDCAP_FLAT
        if (st.LineJoin == StrokeLineJoin::Bevel) style |= 0x00001000;     // JOIN_BEVEL
        else if (st.LineJoin == StrokeLineJoin::Miter) style |= 0x00002000; // JOIN_MITER

        Rec(EMR_EXTCREATEPEN, [&](EmfBuf& b) {
            b.U32(ih);
            b.U32(0); b.U32(0); b.U32(0); b.U32(0);   // no brush bitmap
            b.U32(style);
            b.U32(static_cast<uint32_t>(
                    std::max(1.0, std::round(st.Width * scale * kUnitsPerPoint))));
            b.U32(0);            // BS_SOLID
            b.U32(ColorRef(c));
            b.U32(0);            // hatch
            b.U32(static_cast<uint32_t>(dashes.size()));
            for (uint32_t d : dashes) b.U32(d);
        });
        return ih;
    }

    void Select(uint32_t ih) {
        Rec(EMR_SELECTOBJECT, [&](EmfBuf& b) { b.U32(ih); });
    }
    void Delete(uint32_t ih) {
        Rec(EMR_DELETEOBJECT, [&](EmfBuf& b) { b.U32(ih); });
    }

    // ===== TREE =====

    void EmitElement(const VectorElement& e, const VectorStyle& inherited,
                     const Matrix3x3& parentCtm) {
        if (!e.Style.Visible || !e.Style.Display) return;

        VectorStyle eff = e.Style;
        eff.Inherit(inherited);
        Matrix3x3 ctm = e.Transform ? parentCtm * (*e.Transform) : parentCtm;

        switch (e.Type) {
            case VectorElementType::Group:
            case VectorElementType::Symbol:
            case VectorElementType::Layer: {
                const auto& g = static_cast<const VectorGroup&>(e);
                for (const auto& child : g.Children) {
                    if (child) EmitElement(*child, eff, ctm);
                }
                break;
            }
            case VectorElementType::Rectangle:
            case VectorElementType::RoundedRectangle: {
                const auto& r = static_cast<const VectorRect&>(e);
                double rx = std::min<double>(r.RadiusX, r.Bounds.width / 2);
                double ry = std::min<double>(r.RadiusY, r.Bounds.height / 2);
                if (rx <= 0 && ry > 0) rx = ry;
                if (ry <= 0 && rx > 0) ry = rx;
                auto segs = (rx > 0) ? RoundedRectSegs(r.Bounds, rx, ry)
                                     : RectSegs(r.Bounds);
                EmitPath(segs, eff, ctm, true);
                break;
            }
            case VectorElementType::Circle: {
                const auto& c = static_cast<const VectorCircle&>(e);
                EmitPath(EllipseSegs(c.Center, c.Radius, c.Radius), eff, ctm, true);
                break;
            }
            case VectorElementType::Ellipse: {
                const auto& el = static_cast<const VectorEllipse&>(e);
                EmitPath(EllipseSegs(el.Center, el.RadiusX, el.RadiusY), eff, ctm, true);
                break;
            }
            case VectorElementType::Line: {
                const auto& ln = static_cast<const VectorLine&>(e);
                std::vector<FlatSeg> segs;
                segs.push_back({FlatSeg::Move, {ln.Start}, false});
                segs.push_back({FlatSeg::Line, {ln.End}, false});
                EmitPath(segs, eff, ctm, false);
                break;
            }
            case VectorElementType::Polyline: {
                const auto& pl = static_cast<const VectorPolyline&>(e);
                EmitPoly(pl.Points, false, eff, ctm);
                break;
            }
            case VectorElementType::Polygon: {
                const auto& pg = static_cast<const VectorPolygon&>(e);
                EmitPoly(pg.Points, true, eff, ctm);
                break;
            }
            case VectorElementType::Path: {
                const auto& p = static_cast<const VectorPath&>(e);
                EmitPath(NormalizePath(p.Path), eff, ctm, true);
                break;
            }
            case VectorElementType::Text:
                EmitText(static_cast<const VectorText&>(e), eff, ctm);
                break;
            default:
                warn("EMF export: element type not supported, skipped (type " +
                     std::to_string(static_cast<int>(e.Type)) + ")");
                break;
        }
    }

    void EmitPoly(const std::vector<Point2Dd>& pts, bool closed,
                  const VectorStyle& style, const Matrix3x3& ctm) {
        if (pts.size() < 2) return;
        std::vector<FlatSeg> segs;
        segs.push_back({FlatSeg::Move, {pts[0]}, false});
        for (size_t i = 1; i < pts.size(); ++i) {
            segs.push_back({FlatSeg::Line, {pts[i]}, false});
        }
        if (closed) segs.back().closeAfter = true;
        EmitPath(segs, style, ctm, true);
    }

    // ===== PATH + PAINT =====

    void EmitPath(const std::vector<FlatSeg>& segs, const VectorStyle& style,
                  const Matrix3x3& ctm, bool fillable) {
        if (segs.empty()) return;
        bool filled = fillable && HasVisibleFill(style);
        bool stroked = HasVisibleStroke(style);
        if (!filled && !stroked) return;

        uint32_t brush = 0, pen = 0;
        if (filled) {
            brush = CreateBrush(FlattenFill(*style.Fill,
                                            style.Opacity * style.FillOpacity));
            Select(brush);
        } else {
            Select(kStockNullBrush);
        }
        if (stroked) {
            const StrokeData& st = *style.Stroke;
            Color sc(0, 0, 0, 255);
            if (const Color* c = std::get_if<Color>(&st.Fill)) sc = *c;
            else warn("EMF export: non-solid stroke paint replaced with black");
            pen = CreatePen(st, Flatten(sc, style.Opacity * style.StrokeOpacity *
                                                st.Opacity),
                            AvgScale(ctm));
            Select(pen);
        } else {
            Select(kStockNullPen);
        }

        Rec(EMR_BEGINPATH, [](EmfBuf&) {});
        for (const auto& s : segs) {
            switch (s.kind) {
                case FlatSeg::Move: {
                    Point2Dd p = ctm.Transform(s.p[0]);
                    Rec(EMR_MOVETOEX, [&](EmfBuf& b) {
                        b.I32(LX(p.x)); b.I32(LY(p.y));
                    });
                    break;
                }
                case FlatSeg::Line: {
                    Point2Dd p = ctm.Transform(s.p[0]);
                    Rec(EMR_LINETO, [&](EmfBuf& b) {
                        b.I32(LX(p.x)); b.I32(LY(p.y));
                    });
                    break;
                }
                case FlatSeg::Cubic: {
                    Point2Dd c1 = ctm.Transform(s.p[0]);
                    Point2Dd c2 = ctm.Transform(s.p[1]);
                    Point2Dd e = ctm.Transform(s.p[2]);
                    Rec(EMR_POLYBEZIERTO, [&](EmfBuf& b) {
                        b.I32(0); b.I32(0); b.I32(-1); b.I32(-1);   // bounds: unbounded
                        b.U32(3);
                        b.I32(LX(c1.x)); b.I32(LY(c1.y));
                        b.I32(LX(c2.x)); b.I32(LY(c2.y));
                        b.I32(LX(e.x)); b.I32(LY(e.y));
                    });
                    break;
                }
            }
            if (s.closeAfter) Rec(EMR_CLOSEFIGURE, [](EmfBuf&) {});
        }
        Rec(EMR_ENDPATH, [](EmfBuf&) {});

        auto pageBounds = [&](EmfBuf& b) {
            b.I32(0); b.I32(0); b.I32(devW - 1); b.I32(devH - 1);
        };
        if (filled && stroked) Rec(EMR_STROKEANDFILLPATH, pageBounds);
        else if (filled) Rec(EMR_FILLPATH, pageBounds);
        else Rec(EMR_STROKEPATH, pageBounds);

        if (pen) Delete(pen);
        if (brush) Delete(brush);
    }

    // ===== TEXT =====

    uint32_t CreateFont(const VectorTextStyle& s, const VectorTextStyle& base,
                        double scale) {
        uint32_t ih = nextHandle++;
        std::string family = s.FontFamily.empty() ? base.FontFamily : s.FontFamily;
        float size = (s.FontSize > 0 ? s.FontSize
                      : base.FontSize > 0 ? base.FontSize : 12.0f) *
                     static_cast<float>(scale);
        bool bold = s.Weight == FontWeight::Bold || s.Weight == FontWeight::ExtraBold;
        bool italic = s.Slant != FontSlant::Normal;

        Rec(EMR_EXTCREATEFONTINDIRECTW, [&](EmfBuf& b) {
            b.U32(ih);
            // LOGFONTW (92 bytes)
            b.I32(-static_cast<int32_t>(std::lround(size * kUnitsPerPoint)));
            b.I32(0);                       // width
            b.I32(0); b.I32(0);             // escapement, orientation
            b.I32(bold ? 700 : 400);        // weight
            b.U8(italic ? 1 : 0);
            b.U8(s.Underline ? 1 : 0);
            b.U8(s.StrikeThrough ? 1 : 0);
            b.U8(1);                        // DEFAULT_CHARSET
            b.U8(0); b.U8(0); b.U8(0); b.U8(0);   // precision/quality/pitch
            // faceName: WCHAR[32], null-terminated
            size_t written = 0;
            for (size_t i = 0; i < family.size() && written < 31; ++i) {
                unsigned char ch = static_cast<unsigned char>(family[i]);
                if (ch < 0x80) { b.U16(ch); ++written; }
            }
            for (; written < 32; ++written) b.U16(0);
        });
        return ih;
    }

    void EmitText(const VectorText& text, const VectorStyle& style,
                  const Matrix3x3& ctm) {
        if (!AxisAligned(ctm)) {
            warn("EMF export: rotated/skewed text is exported without its rotation");
        }
        Color tc(0, 0, 0, 255);
        if (style.Fill.has_value() &&
            !std::holds_alternative<std::monostate>(*style.Fill)) {
            tc = FlattenFill(*style.Fill, style.Opacity * style.FillOpacity);
        }
        double scale = AvgScale(ctm);
        float baseSize = text.BaseStyle.FontSize > 0 ? text.BaseStyle.FontSize : 12.0f;
        float leading = baseSize * (text.BaseStyle.LineHeight > 0
                                            ? text.BaseStyle.LineHeight : 1.2f);

        struct Chunk { std::string text; const VectorTextStyle* style; };
        std::vector<std::vector<Chunk>> lines(1);
        for (const auto& span : text.Spans) {
            std::string piece;
            for (char ch : span.Text) {
                if (ch == '\n') {
                    if (!piece.empty()) lines.back().push_back({piece, &span.Style});
                    piece.clear();
                    lines.emplace_back();
                } else {
                    piece.push_back(ch);
                }
            }
            if (!piece.empty()) lines.back().push_back({piece, &span.Style});
        }

        Rec(EMR_SETTEXTCOLOR, [&](EmfBuf& b) { b.U32(ColorRef(tc)); });

        for (size_t li = 0; li < lines.size(); ++li) {
            const auto& line = lines[li];
            if (line.empty()) continue;
            Point2Dd anchor = ctm.Transform(
                    Point2Dd(text.Position.x, text.Position.y + li * leading));

            // TA_BASELINE = 24. Single-chunk lines can let GDI anchor
            // (TA_CENTER = 6, TA_RIGHT = 2); span chains use TA_UPDATECP = 1
            // so successive chunks continue at the current position.
            bool chain = line.size() > 1;
            uint32_t align = 24;
            if (!chain) {
                if (text.BaseStyle.Anchor == TextAnchor::Middle) align |= 6;
                else if (text.BaseStyle.Anchor == TextAnchor::End) align |= 2;
            } else {
                align |= 1;   // TA_UPDATECP
                if (text.BaseStyle.Anchor != TextAnchor::Start && !warnedAnchor) {
                    warnedAnchor = true;
                    warn("EMF export: centre/right anchoring of multi-style lines "
                         "is not supported; using left anchoring");
                }
                Rec(EMR_MOVETOEX, [&](EmfBuf& b) {
                    b.I32(LX(anchor.x)); b.I32(LY(anchor.y));
                });
            }
            Rec(EMR_SETTEXTALIGN, [&](EmfBuf& b) { b.U32(align); });

            for (const auto& chunk : line) {
                uint32_t font = CreateFont(*chunk.style, text.BaseStyle, scale);
                Select(font);

                // UTF-8 -> UTF-16 code units.
                std::vector<uint16_t> wide;
                size_t i = 0, n = chunk.text.size();
                while (i < n) {
                    uint32_t cp = static_cast<uint8_t>(chunk.text[i]);
                    size_t extra = 0;
                    if (cp >= 0xF0) { cp &= 0x07; extra = 3; }
                    else if (cp >= 0xE0) { cp &= 0x0F; extra = 2; }
                    else if (cp >= 0xC0) { cp &= 0x1F; extra = 1; }
                    if (i + extra >= n && extra > 0) break;
                    for (size_t k = 0; k < extra; ++k) {
                        cp = (cp << 6) |
                             (static_cast<uint8_t>(chunk.text[i + 1 + k]) & 0x3F);
                    }
                    i += 1 + extra;
                    if (cp >= 0x10000) {
                        cp -= 0x10000;
                        wide.push_back(static_cast<uint16_t>(0xD800 | (cp >> 10)));
                        wide.push_back(static_cast<uint16_t>(0xDC00 | (cp & 0x3FF)));
                    } else {
                        wide.push_back(static_cast<uint16_t>(cp));
                    }
                }

                Rec(EMR_EXTTEXTOUTW, [&](EmfBuf& b) {
                    b.I32(0); b.I32(0); b.I32(-1); b.I32(-1);   // bounds
                    b.U32(1);          // GM_COMPATIBLE
                    b.F32(1.0f); b.F32(1.0f);
                    // EMRTEXT
                    b.I32(LX(anchor.x)); b.I32(LY(anchor.y));   // reference point
                    b.U32(static_cast<uint32_t>(wide.size()));
                    b.U32(76);         // offString from record start (8+68)
                    b.U32(0);          // options
                    b.I32(0); b.I32(0); b.I32(-1); b.I32(-1);   // rcl
                    b.U32(0);          // offDx (no advance array)
                    for (uint16_t w : wide) b.U16(w);
                });
                Delete(font);
            }
        }
    }
};

}   // anonymous namespace

// ===== PUBLIC INTERFACE =====

FormatCapabilities EMFConverter::GetCapabilities() const {
    FormatCapabilities caps;
    caps.SupportsRectangle = true;
    caps.SupportsCircle = true;
    caps.SupportsEllipse = true;
    caps.SupportsLine = true;
    caps.SupportsPolyline = true;
    caps.SupportsPolygon = true;
    caps.SupportsPath = true;
    caps.SupportsCubicBezier = true;
    caps.SupportsQuadraticBezier = true;   // converted to cubics
    caps.SupportsArc = true;               // converted to cubics
    caps.SupportsCompoundPaths = true;
    caps.SupportsText = true;
    caps.SupportsSolidFill = true;
    caps.SupportsDashing = true;
    caps.SupportsGroups = true;            // flattened
    caps.SupportsLayers = true;            // flattened
    return caps;
}

std::string EMFConverter::ExportToString(
        const VectorStorage::VectorDocument& document,
        const ConversionOptions& options) {
    EmfEmitter emitter(document, [&options](const std::string& msg) {
        if (options.WarningCallback) options.WarningCallback(msg);
    });
    std::string data = emitter.Build();
    if (options.ProgressCallback) options.ProgressCallback(1.0f);
    return data;
}

bool EMFConverter::ValidateFile(const std::string& filename) const {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;
    std::string head(48, '\0');
    file.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(file.gcount()));
    return ValidateData(head);
}

bool EMFConverter::ValidateData(const std::string& data) const {
    // EMR_HEADER with the " EMF" signature at offset 40.
    return data.size() >= 44 &&
           data[0] == 0x01 && data[1] == 0x00 && data[2] == 0x00 && data[3] == 0x00 &&
           data.compare(40, 4, " EMF") == 0;
}

} // namespace VectorConverter
} // namespace UltraCanvas
