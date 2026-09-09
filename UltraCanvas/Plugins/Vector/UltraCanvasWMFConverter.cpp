// UltraCanvas/Plugins/Vector/UltraCanvasWMFConverter.cpp
// WMF (legacy Windows Metafile) writer - see UltraCanvasMetafileConverters.h.
//
// Record layouts follow [MS-WMF]. The file carries the placeable (Aldus)
// header so consumers know its real size, with coordinates in twips
// (1/20 point, the placeable header's 1440 units per inch). WMF's 16-bit
// record set has no bezier record, so curves flatten to polylines; filled
// shapes go through POLYGON/POLYPOLYGON (which also outline with the
// selected pen), open strokes through POLYLINE. The GDI object table is
// modelled faithfully: objects take the lowest free slot and free it on
// delete. Like EMF, WMF has no transparency - opacity flattens toward the
// white page - and dash patterns approximate as PS_DASH.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "UltraCanvasMetafileConverters.h"
#include "UltraCanvasVectorStorage.h"
#include "UltraCanvasVectorPathOps.h"

#include <cmath>
#include <fstream>
#include <functional>
#include <variant>

namespace UltraCanvas {
namespace VectorConverter {

using namespace VectorStorage;

namespace {

using namespace PathOps;

constexpr double kTwipsPerPoint = 20.0;
constexpr int kCurveSteps = 16;   // cubic flattening

// [MS-WMF] record functions used here.
enum : uint16_t {
    META_EOF_REC = 0x0000,
    META_SETBKMODE = 0x0102,
    META_SETMAPMODE = 0x0103,
    META_SETPOLYFILLMODE = 0x0106,
    META_SETTEXTCOLOR = 0x0209,
    META_SETWINDOWORG = 0x020B,
    META_SETWINDOWEXT = 0x020C,
    META_MOVETO = 0x0214,
    META_SELECTOBJECT = 0x012D,
    META_SETTEXTALIGN = 0x012E,
    META_DELETEOBJECT = 0x01F0,
    META_CREATEPENINDIRECT = 0x02FA,
    META_CREATEFONTINDIRECT = 0x02FB,
    META_CREATEBRUSHINDIRECT = 0x02FC,
    META_POLYGON = 0x0324,
    META_POLYLINE = 0x0325,
    META_TEXTOUT = 0x0521,
    META_POLYPOLYGON = 0x0538,
};

class WmfBuf {
public:
    std::vector<uint8_t> bytes;

    void U8(uint8_t v) { bytes.push_back(v); }
    void U16(uint16_t v) {
        bytes.push_back(static_cast<uint8_t>(v));
        bytes.push_back(static_cast<uint8_t>(v >> 8));
    }
    void I16(int16_t v) { U16(static_cast<uint16_t>(v)); }
    void U32(uint32_t v) { U16(static_cast<uint16_t>(v)); U16(static_cast<uint16_t>(v >> 16)); }
};

uint32_t ColorRef(const Color& c) {   // 0x00BBGGRR
    return (static_cast<uint32_t>(c.b) << 16) |
           (static_cast<uint32_t>(c.g) << 8) | c.r;
}

class WmfEmitter {
public:
    WmfEmitter(const VectorDocument& document,
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
        // 16-bit coordinates: clamp the twips scale for very large pages.
        scale = kTwipsPerPoint;
        double maxDim = std::max(pageW, pageH);
        if (maxDim * scale > 32000.0) scale = 32000.0 / maxDim;

        Rec(META_SETMAPMODE, [&](WmfBuf& b) { b.U16(8); });   // MM_ANISOTROPIC
        Rec(META_SETWINDOWORG, [&](WmfBuf& b) { b.I16(0); b.I16(0); });   // y, x
        Rec(META_SETWINDOWEXT, [&](WmfBuf& b) {
            b.I16(T(pageH)); b.I16(T(pageW));                             // cy, cx
        });
        Rec(META_SETPOLYFILLMODE, [&](WmfBuf& b) { b.U16(2); });   // WINDING
        Rec(META_SETBKMODE, [&](WmfBuf& b) { b.U16(1); });         // TRANSPARENT

        for (const auto& layer : doc.Layers) {
            if (!layer || !layer->Visible) continue;
            for (const auto& child : layer->Children) {
                if (child) EmitElement(*child, layer->Style, Matrix3x3::Identity());
            }
        }

        Rec(META_EOF_REC, [](WmfBuf&) {});

        // Assemble: placeable header, standard header, records.
        WmfBuf out;
        out.U32(0x9AC6CDD7);   // placeable key
        out.U16(0);            // hmf
        out.I16(0); out.I16(0);                       // bbox left, top (twips)
        int16_t right = static_cast<int16_t>(std::lround(pageW * kTwipsPerPoint *
                                                         (scale / kTwipsPerPoint)));
        int16_t bottom = static_cast<int16_t>(std::lround(pageH * kTwipsPerPoint *
                                                          (scale / kTwipsPerPoint)));
        out.I16(right); out.I16(bottom);
        out.U16(static_cast<uint16_t>(std::lround(1440.0 * (scale / kTwipsPerPoint))));
        out.U32(0);            // reserved
        uint16_t checksum = 0;
        for (size_t i = 0; i + 1 < out.bytes.size(); i += 2) {
            checksum ^= static_cast<uint16_t>(out.bytes[i] | (out.bytes[i + 1] << 8));
        }
        out.U16(checksum);

        uint32_t fileWords = (18 + records.bytes.size()) / 2 + 22 / 2 - 11;
        // Standard header (9 words): counted without the placeable part.
        uint32_t headerAndRecordsWords = 9 + static_cast<uint32_t>(records.bytes.size() / 2);
        out.U16(1);            // MEMORYMETAFILE
        out.U16(9);            // header size in words
        out.U16(0x0300);       // version
        out.U32(headerAndRecordsWords);
        out.U16(maxObjects);
        out.U32(maxRecordWords);
        out.U16(0);            // noParameters
        (void)fileWords;

        out.bytes.insert(out.bytes.end(), records.bytes.begin(), records.bytes.end());
        return std::string(out.bytes.begin(), out.bytes.end());
    }

private:
    const VectorDocument& doc;
    std::function<void(const std::string&)> warn;
    WmfBuf records;
    double pageW = 0, pageH = 0;
    double scale = kTwipsPerPoint;
    std::vector<bool> slots;   // GDI object table
    uint16_t maxObjects = 0;
    uint32_t maxRecordWords = 0;
    bool warnedAlpha = false, warnedGradient = false, warnedDash = false,
         warnedAnchor = false, warnedAnsi = false;

    int16_t T(double pt) const {
        double v = pt * scale;
        return static_cast<int16_t>(std::lround(std::max(-32000.0, std::min(32000.0, v))));
    }

    template <typename Fn>
    void Rec(uint16_t function, Fn&& fill) {
        WmfBuf body;
        fill(body);
        uint32_t words = 3 + static_cast<uint32_t>(body.bytes.size() / 2);
        records.U32(words);
        records.U16(function);
        records.bytes.insert(records.bytes.end(), body.bytes.begin(), body.bytes.end());
        maxRecordWords = std::max(maxRecordWords, words);
    }

    // ===== OBJECT TABLE =====

    uint16_t Alloc() {
        for (size_t i = 0; i < slots.size(); ++i) {
            if (!slots[i]) { slots[i] = true; return static_cast<uint16_t>(i); }
        }
        slots.push_back(true);
        maxObjects = std::max<uint16_t>(maxObjects, static_cast<uint16_t>(slots.size()));
        return static_cast<uint16_t>(slots.size() - 1);
    }
    void Select(uint16_t idx) {
        Rec(META_SELECTOBJECT, [&](WmfBuf& b) { b.U16(idx); });
    }
    void Delete(uint16_t idx) {
        Rec(META_DELETEOBJECT, [&](WmfBuf& b) { b.U16(idx); });
        if (idx < slots.size()) slots[idx] = false;
    }

    uint16_t CreateBrush(const Color& c, bool hollow) {
        uint16_t idx = Alloc();
        Rec(META_CREATEBRUSHINDIRECT, [&](WmfBuf& b) {
            b.U16(hollow ? 1 : 0);   // BS_NULL / BS_SOLID
            b.U32(ColorRef(c));
            b.U16(0);                // hatch
        });
        return idx;
    }

    uint16_t CreatePen(const StrokeData* st, const Color& c, double ctmScale) {
        uint16_t idx = Alloc();
        uint16_t style = 5;   // PS_NULL
        int16_t width = 0;
        if (st) {
            style = 0;        // PS_SOLID
            if (!st->DashArray.empty()) {
                style = 1;    // PS_DASH
                if (!warnedDash) {
                    warnedDash = true;
                    warn("WMF export: dash patterns approximate as the fixed "
                         "PS_DASH style");
                }
            }
            width = T(st->Width * ctmScale);
            if (width < 1) width = 1;
        }
        Rec(META_CREATEPENINDIRECT, [&](WmfBuf& b) {
            b.U16(style);
            b.I16(width); b.I16(width);   // POINTS16 width
            b.U32(ColorRef(c));
        });
        return idx;
    }

    // ===== COLOUR FLATTENING (no alpha in GDI) =====

    Color Flatten(const Color& c, float opacity) {
        float a = (c.a / 255.0f) * opacity;
        if (a < 0.999f && !warnedAlpha) {
            warnedAlpha = true;
            warn("WMF export: GDI metafiles have no transparency; "
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
                warn("WMF export: gradients are not representable; "
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
        warn("WMF export: pattern/reference fills are not supported, "
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
    static double AvgScale(const Matrix3x3& m) {
        double det = std::fabs(static_cast<double>(m.m[0][0]) * m.m[1][1] -
                               static_cast<double>(m.m[0][1]) * m.m[1][0]);
        return det > 0 ? std::sqrt(det) : 1.0;
    }
    static bool AxisAligned(const Matrix3x3& m) {
        return std::fabs(m.m[0][1]) < 1e-6 && std::fabs(m.m[1][0]) < 1e-6;
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
                EmitShape(segs, eff, ctm, true);
                break;
            }
            case VectorElementType::Circle: {
                const auto& c = static_cast<const VectorCircle&>(e);
                EmitShape(EllipseSegs(c.Center, c.Radius, c.Radius), eff, ctm, true);
                break;
            }
            case VectorElementType::Ellipse: {
                const auto& el = static_cast<const VectorEllipse&>(e);
                EmitShape(EllipseSegs(el.Center, el.RadiusX, el.RadiusY), eff, ctm, true);
                break;
            }
            case VectorElementType::Line: {
                const auto& ln = static_cast<const VectorLine&>(e);
                std::vector<FlatSeg> segs;
                segs.push_back({FlatSeg::Move, {ln.Start}, false});
                segs.push_back({FlatSeg::Line, {ln.End}, false});
                EmitShape(segs, eff, ctm, false);
                break;
            }
            case VectorElementType::Polyline: {
                const auto& pl = static_cast<const VectorPolyline&>(e);
                std::vector<FlatSeg> segs;
                if (pl.Points.size() >= 2) {
                    segs.push_back({FlatSeg::Move, {pl.Points[0]}, false});
                    for (size_t i = 1; i < pl.Points.size(); ++i)
                        segs.push_back({FlatSeg::Line, {pl.Points[i]}, false});
                    EmitShape(segs, eff, ctm, true);
                }
                break;
            }
            case VectorElementType::Polygon: {
                const auto& pg = static_cast<const VectorPolygon&>(e);
                std::vector<FlatSeg> segs;
                if (pg.Points.size() >= 2) {
                    segs.push_back({FlatSeg::Move, {pg.Points[0]}, false});
                    for (size_t i = 1; i < pg.Points.size(); ++i)
                        segs.push_back({FlatSeg::Line, {pg.Points[i]}, false});
                    segs.back().closeAfter = true;
                    EmitShape(segs, eff, ctm, true);
                }
                break;
            }
            case VectorElementType::Path: {
                const auto& p = static_cast<const VectorPath&>(e);
                EmitShape(NormalizePath(p.Path), eff, ctm, true);
                break;
            }
            case VectorElementType::Text:
                EmitText(static_cast<const VectorText&>(e), eff, ctm);
                break;
            default:
                warn("WMF export: element type not supported, skipped (type " +
                     std::to_string(static_cast<int>(e.Type)) + ")");
                break;
        }
    }

    // ===== GEOMETRY =====

    // Flatten the segment list into per-subpath 16-bit point chains.
    struct SubPath { std::vector<std::pair<int16_t, int16_t>> pts; bool closed = false; };

    std::vector<SubPath> FlattenSegs(const std::vector<FlatSeg>& segs,
                                     const Matrix3x3& ctm) {
        std::vector<SubPath> subs;
        Point2Dd cur(0, 0);
        auto add = [&](const Point2Dd& local) {
            Point2Dd p = ctm.Transform(local);
            if (subs.empty()) subs.emplace_back();
            auto pt = std::make_pair(T(p.x), T(p.y));
            if (subs.back().pts.empty() || subs.back().pts.back() != pt) {
                subs.back().pts.push_back(pt);
            }
            cur = local;
        };
        for (const auto& s : segs) {
            switch (s.kind) {
                case FlatSeg::Move:
                    subs.emplace_back();
                    add(s.p[0]);
                    break;
                case FlatSeg::Line:
                    add(s.p[0]);
                    break;
                case FlatSeg::Cubic: {
                    Point2Dd p0 = cur;
                    for (int i = 1; i <= kCurveSteps; ++i) {
                        double t = static_cast<double>(i) / kCurveSteps;
                        double u = 1.0 - t;
                        Point2Dd p(u * u * u * p0.x + 3 * u * u * t * s.p[0].x +
                                           3 * u * t * t * s.p[1].x + t * t * t * s.p[2].x,
                                   u * u * u * p0.y + 3 * u * u * t * s.p[0].y +
                                           3 * u * t * t * s.p[1].y + t * t * t * s.p[2].y);
                        add(p);
                    }
                    cur = s.p[2];
                    break;
                }
            }
            if (s.closeAfter && !subs.empty()) subs.back().closed = true;
        }
        std::vector<SubPath> result;
        for (auto& sp : subs) {
            if (sp.pts.size() >= 2) result.push_back(std::move(sp));
        }
        return result;
    }

    void EmitShape(const std::vector<FlatSeg>& segs, const VectorStyle& style,
                   const Matrix3x3& ctm, bool fillable) {
        bool filled = fillable && HasVisibleFill(style);
        bool stroked = HasVisibleStroke(style);
        if (!filled && !stroked) return;
        auto subs = FlattenSegs(segs, ctm);
        if (subs.empty()) return;

        Color penColor(0, 0, 0, 255);
        const StrokeData* st = nullptr;
        if (stroked) {
            st = &*style.Stroke;
            if (const Color* c = std::get_if<Color>(&st->Fill)) penColor = *c;
            else warn("WMF export: non-solid stroke paint replaced with black");
            penColor = Flatten(penColor, style.Opacity * style.StrokeOpacity * st->Opacity);
        }
        uint16_t pen = CreatePen(st, penColor, AvgScale(ctm));
        uint16_t brush = filled
                ? CreateBrush(FlattenFill(*style.Fill,
                                          style.Opacity * style.FillOpacity), false)
                : CreateBrush(Color(0, 0, 0, 255), true);
        Select(pen);
        Select(brush);

        if (filled) {
            // POLYGON/POLYPOLYGON fill the shape and outline it with the pen.
            if (subs.size() == 1) {
                Rec(META_POLYGON, [&](WmfBuf& b) {
                    b.U16(static_cast<uint16_t>(subs[0].pts.size()));
                    for (auto& p : subs[0].pts) { b.I16(p.first); b.I16(p.second); }
                });
            } else {
                Rec(META_POLYPOLYGON, [&](WmfBuf& b) {
                    b.U16(static_cast<uint16_t>(subs.size()));
                    for (auto& sp : subs) b.U16(static_cast<uint16_t>(sp.pts.size()));
                    for (auto& sp : subs) {
                        for (auto& p : sp.pts) { b.I16(p.first); b.I16(p.second); }
                    }
                });
            }
        } else {
            for (auto& sp : subs) {
                Rec(META_POLYLINE, [&](WmfBuf& b) {
                    uint16_t n = static_cast<uint16_t>(sp.pts.size() + (sp.closed ? 1 : 0));
                    b.U16(n);
                    for (auto& p : sp.pts) { b.I16(p.first); b.I16(p.second); }
                    if (sp.closed) { b.I16(sp.pts[0].first); b.I16(sp.pts[0].second); }
                });
            }
        }

        Delete(brush);
        Delete(pen);
    }

    // ===== TEXT =====

    uint16_t CreateFont(const VectorTextStyle& s, const VectorTextStyle& base,
                        double ctmScale) {
        uint16_t idx = Alloc();
        std::string family = s.FontFamily.empty() ? base.FontFamily : s.FontFamily;
        float size = (s.FontSize > 0 ? s.FontSize
                      : base.FontSize > 0 ? base.FontSize : 12.0f) *
                     static_cast<float>(ctmScale);
        bool bold = s.Weight == FontWeight::Bold || s.Weight == FontWeight::ExtraBold;
        bool italic = s.Slant != FontSlant::Normal;
        Rec(META_CREATEFONTINDIRECT, [&](WmfBuf& b) {
            b.I16(static_cast<int16_t>(-T(size)));   // em height, negative
            b.I16(0);                        // width
            b.I16(0); b.I16(0);              // escapement, orientation
            b.I16(bold ? 700 : 400);
            b.U8(italic ? 1 : 0);
            b.U8(s.Underline ? 1 : 0);
            b.U8(s.StrikeThrough ? 1 : 0);
            b.U8(1);                         // DEFAULT_CHARSET
            b.U8(0); b.U8(0); b.U8(0); b.U8(0);
            for (char ch : family) {
                unsigned char u = static_cast<unsigned char>(ch);
                if (u < 0x80) b.U8(u);
            }
            b.U8(0);
            if (b.bytes.size() % 2) b.U8(0);   // face name padded to a word
        });
        return idx;
    }

    void EmitText(const VectorText& text, const VectorStyle& style,
                  const Matrix3x3& ctm) {
        if (!AxisAligned(ctm)) {
            warn("WMF export: rotated/skewed text is exported without its rotation");
        }
        Color tc(0, 0, 0, 255);
        if (style.Fill.has_value() &&
            !std::holds_alternative<std::monostate>(*style.Fill)) {
            tc = FlattenFill(*style.Fill, style.Opacity * style.FillOpacity);
        }
        double ctmScale = AvgScale(ctm);
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

        Rec(META_SETTEXTCOLOR, [&](WmfBuf& b) { b.U32(ColorRef(tc)); });

        for (size_t li = 0; li < lines.size(); ++li) {
            const auto& line = lines[li];
            if (line.empty()) continue;
            Point2Dd anchor = ctm.Transform(
                    Point2Dd(text.Position.x, text.Position.y + li * leading));

            bool chain = line.size() > 1;
            uint16_t align = 24;   // TA_BASELINE
            if (!chain) {
                if (text.BaseStyle.Anchor == TextAnchor::Middle) align |= 6;
                else if (text.BaseStyle.Anchor == TextAnchor::End) align |= 2;
            } else {
                align |= 1;        // TA_UPDATECP
                if (text.BaseStyle.Anchor != TextAnchor::Start && !warnedAnchor) {
                    warnedAnchor = true;
                    warn("WMF export: centre/right anchoring of multi-style lines "
                         "is not supported; using left anchoring");
                }
                Rec(META_MOVETO, [&](WmfBuf& b) {
                    b.I16(T(anchor.y)); b.I16(T(anchor.x));   // y, x
                });
            }
            Rec(META_SETTEXTALIGN, [&](WmfBuf& b) { b.U16(align); });

            for (const auto& chunk : line) {
                uint16_t font = CreateFont(*chunk.style, text.BaseStyle, ctmScale);
                Select(font);

                // UTF-8 -> Latin-1 (TEXTOUT strings are byte-encoded).
                std::string ansi;
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
                    if (cp > 0xFF) {
                        if (!warnedAnsi) {
                            warnedAnsi = true;
                            warn("WMF export: characters outside Latin-1 are "
                                 "replaced with '?'");
                        }
                        cp = '?';
                    }
                    ansi.push_back(static_cast<char>(cp));
                }

                Rec(META_TEXTOUT, [&](WmfBuf& b) {
                    b.U16(static_cast<uint16_t>(ansi.size()));
                    for (char ch : ansi) b.U8(static_cast<uint8_t>(ch));
                    if (ansi.size() % 2) b.U8(0);
                    b.I16(T(anchor.y));   // YStart
                    b.I16(T(anchor.x));   // XStart
                });
                Delete(font);
            }
        }
    }
};

}   // anonymous namespace

// ===== PUBLIC INTERFACE =====

FormatCapabilities WMFConverter::GetCapabilities() const {
    FormatCapabilities caps;
    caps.SupportsRectangle = true;
    caps.SupportsCircle = true;
    caps.SupportsEllipse = true;
    caps.SupportsLine = true;
    caps.SupportsPolyline = true;
    caps.SupportsPolygon = true;
    caps.SupportsPath = true;      // curves flattened to polylines
    caps.SupportsCompoundPaths = true;
    caps.SupportsText = true;
    caps.SupportsSolidFill = true;
    caps.SupportsDashing = true;   // approximated as PS_DASH
    caps.SupportsGroups = true;    // flattened
    caps.SupportsLayers = true;    // flattened
    return caps;
}

std::string WMFConverter::ExportToString(
        const VectorStorage::VectorDocument& document,
        const ConversionOptions& options) {
    WmfEmitter emitter(document, [&options](const std::string& msg) {
        if (options.WarningCallback) options.WarningCallback(msg);
    });
    std::string data = emitter.Build();
    if (options.ProgressCallback) options.ProgressCallback(1.0f);
    return data;
}

bool WMFConverter::ValidateFile(const std::string& filename) const {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;
    std::string head(8, '\0');
    file.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(file.gcount()));
    return ValidateData(head);
}

bool WMFConverter::ValidateData(const std::string& data) const {
    // Placeable (Aldus) header key, or a bare standard header.
    if (data.size() >= 4 &&
        static_cast<uint8_t>(data[0]) == 0xD7 && static_cast<uint8_t>(data[1]) == 0xCD &&
        static_cast<uint8_t>(data[2]) == 0xC6 && static_cast<uint8_t>(data[3]) == 0x9A) {
        return true;
    }
    return data.size() >= 4 && (data[0] == 0x01 || data[0] == 0x02) &&
           data[1] == 0x00 && data[2] == 0x09 && data[3] == 0x00;
}

} // namespace VectorConverter
} // namespace UltraCanvas
