// UltraCanvas/Plugins/Vector/UltraCanvasEMFPlus.cpp
// EMF+ playback for the EMF reader - see the header.
// Version: 1.0.0
// Last Modified: 2026-11-20
// Author: UltraCanvas Framework

#include "UltraCanvasEMFPlus.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace UltraCanvas {
namespace VectorConverter {

using namespace VectorStorage;

namespace {

constexpr double kPi = 3.14159265358979323846;

// EMF+ record types ([MS-EMFPLUS] 2.1.1.1).
enum : uint16_t {
    Header = 0x4001, EndOfFile = 0x4002, GetDC = 0x4004, ObjectRec = 0x4008, Clear = 0x4009,
    FillRects = 0x400A, DrawRects = 0x400B, FillPolygon = 0x400C, DrawLines = 0x400D,
    FillEllipse = 0x400E, DrawEllipse = 0x400F, FillPie = 0x4010, DrawPie = 0x4011, DrawArc = 0x4012,
    FillRegion = 0x4013, FillPath = 0x4014, DrawPath = 0x4015, FillClosedCurve = 0x4016,
    DrawClosedCurve = 0x4017, DrawCurve = 0x4018, DrawBeziers = 0x4019, DrawImage = 0x401A,
    DrawImagePoints = 0x401B, DrawString = 0x401C,
    Save = 0x4025, Restore = 0x4026, BeginContainer = 0x4027, BeginContainerNoParams = 0x4028,
    EndContainer = 0x4029, SetWorldTransform = 0x402A, ResetWorldTransform = 0x402B,
    MultiplyWorldTransform = 0x402C, TranslateWorldTransform = 0x402D, ScaleWorldTransform = 0x402E,
    RotateWorldTransform = 0x402F, SetPageTransform = 0x4030, DrawDriverString = 0x4036,
    StrokeFillPath = 0x4037,
};

// Little-endian reads, bounded: a short record reads as zeros and clears ok.
struct Reader {
    const uint8_t* d;
    size_t n, at = 0;
    bool ok = true;
    Reader(const uint8_t* data, size_t size) : d(data), n(size) {}
    bool Has(size_t k) const { return at + k <= n; }
    uint32_t U32() {
        if (!Has(4)) { ok = false; at = n; return 0; }
        uint32_t v;
        std::memcpy(&v, d + at, 4);
        at += 4;
        return v;
    }
    int32_t I32() { return static_cast<int32_t>(U32()); }
    uint16_t U16() {
        if (!Has(2)) { ok = false; at = n; return 0; }
        uint16_t v;
        std::memcpy(&v, d + at, 2);
        at += 2;
        return v;
    }
    uint8_t U8() {
        if (!Has(1)) { ok = false; at = n; return 0; }
        return d[at++];
    }
    float F32() {
        const uint32_t u = U32();
        float f;
        std::memcpy(&f, &u, 4);
        return std::isfinite(f) ? f : 0.0f;
    }
    void Skip(size_t k) {
        if (!Has(k)) { ok = false; at = n; return; }
        at += k;
    }
};

Color Argb(uint32_t v) {
    return Color(static_cast<uint8_t>((v >> 16) & 0xFF), static_cast<uint8_t>((v >> 8) & 0xFF),
                 static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>(v >> 24));
}

// [m11 m12 m21 m22 dx dy]: x' = m11 x + m21 y + dx, y' = m12 x + m22 y + dy.
Matrix3x3 ReadMatrix(Reader& r) {
    const double m11 = r.F32(), m12 = r.F32(), m21 = r.F32(), m22 = r.F32(), dx = r.F32(), dy = r.F32();
    return Matrix3x3::FromValues(m11, m21, m12, m22, dx, dy);
}

// A relative coordinate (EmfPlusInteger7 / EmfPlusInteger15).
int ReadRelative(Reader& r) {
    const uint8_t b = r.U8();
    if (!(b & 0x80)) return (b & 0x40) ? static_cast<int>(b & 0x7F) - 0x80 : (b & 0x7F);
    int v = ((b & 0x7F) << 8) | r.U8();
    return (v & 0x4000) ? v - 0x8000 : v;
}

// `count` points as the flags say: C (0x4000) 16-bit integers, P (0x0800)
// relative to the one before, otherwise 32-bit floats.
std::vector<Point2Dd> ReadPoints(Reader& r, uint16_t flags, uint32_t count) {
    std::vector<Point2Dd> pts;
    if (count > r.n) { r.ok = false; return pts; }   // more points than bytes
    pts.reserve(count);
    if (flags & 0x0800) {
        double x = 0, y = 0;
        for (uint32_t i = 0; i < count && r.ok; ++i) {
            x += ReadRelative(r);
            y += ReadRelative(r);
            pts.emplace_back(x, y);
        }
    } else if (flags & 0x4000) {
        for (uint32_t i = 0; i < count && r.ok; ++i) {
            const double x = static_cast<int16_t>(r.U16());
            const double y = static_cast<int16_t>(r.U16());
            pts.emplace_back(x, y);
        }
    } else {
        for (uint32_t i = 0; i < count && r.ok; ++i) {
            const double x = r.F32();
            const double y = r.F32();
            pts.emplace_back(x, y);
        }
    }
    if (!r.ok) pts.clear();
    return pts;
}

Rect2Dd ReadRect(Reader& r, bool compressed) {
    if (compressed) {
        const double x = static_cast<int16_t>(r.U16()), y = static_cast<int16_t>(r.U16());
        const double w = static_cast<int16_t>(r.U16()), h = static_cast<int16_t>(r.U16());
        return Rect2Dd(x, y, w, h);
    }
    const double x = r.F32(), y = r.F32(), w = r.F32(), h = r.F32();
    return Rect2Dd(x, y, w, h);
}

std::string Utf16(Reader& r, uint32_t chars) {
    std::string out;
    for (uint32_t i = 0; i < chars && r.ok; ++i) {
        uint32_t cp = r.U16();
        if (cp >= 0xD800 && cp < 0xDC00 && i + 1 < chars) {
            const uint32_t lo = r.U16();
            ++i;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
        }
        if (cp == 0) break;
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

// Geometry is built in world units and mapped through `m` point by point:
// an affine map keeps a Bezier a Bezier.
struct Builder {
    VectorPath& path;
    const Matrix3x3& m;
    void Move(const Point2Dd& p) {
        const Point2Dd q = m.Transform(p);
        path.MoveTo(static_cast<float>(q.x), static_cast<float>(q.y));
    }
    void Line(const Point2Dd& p) {
        const Point2Dd q = m.Transform(p);
        path.LineTo(static_cast<float>(q.x), static_cast<float>(q.y));
    }
    void Curve(const Point2Dd& a, const Point2Dd& b, const Point2Dd& c) {
        const Point2Dd p = m.Transform(a), q = m.Transform(b), s = m.Transform(c);
        path.CurveTo(static_cast<float>(p.x), static_cast<float>(p.y), static_cast<float>(q.x),
                     static_cast<float>(q.y), static_cast<float>(s.x), static_cast<float>(s.y));
    }
    void Close() { path.ClosePath(); }

    void Polygon(const std::vector<Point2Dd>& pts, bool closed) {
        if (pts.empty()) return;
        Move(pts[0]);
        for (size_t i = 1; i < pts.size(); ++i) Line(pts[i]);
        if (closed) Close();
    }
    void Rect(const Rect2Dd& r) {
        Move({r.x, r.y});
        Line({r.x + r.width, r.y});
        Line({r.x + r.width, r.y + r.height});
        Line({r.x, r.y + r.height});
        Close();
    }
    // An arc of the ellipse in `r`: GDI+ measures its angles in degrees,
    // clockwise on the page, to the point on the ellipse itself (not to the
    // parameter), and an arc sweeps no more than once round.
    void Arc(const Rect2Dd& r, double startDeg, double sweepDeg, bool move) {
        const double rx = r.width / 2, ry = r.height / 2, cx = r.x + rx, cy = r.y + ry;
        if (!(std::fabs(rx) > 0) || !(std::fabs(ry) > 0)) return;
        auto param = [&](double deg) {
            const double a = deg * kPi / 180;
            return std::atan2(std::sin(a) / ry, std::cos(a) / rx);
        };
        sweepDeg = std::max(-360.0, std::min(360.0, sweepDeg));
        const double t0 = param(startDeg);
        double sweep;
        if (std::fabs(sweepDeg) >= 360) {
            sweep = sweepDeg > 0 ? 2 * kPi : -2 * kPi;
        } else {
            sweep = param(startDeg + sweepDeg) - t0;
            if (sweepDeg > 0) while (sweep < 0) sweep += 2 * kPi;
            if (sweepDeg < 0) while (sweep > 0) sweep -= 2 * kPi;
        }
        auto at = [&](double t) { return Point2Dd(cx + rx * std::cos(t), cy + ry * std::sin(t)); };
        if (move) Move(at(t0)); else Line(at(t0));
        const int segments = std::max(1, static_cast<int>(std::ceil(std::fabs(sweep) / (kPi / 2) - 1e-9)));
        const double step = sweep / segments;
        const double k = 4.0 / 3.0 * std::tan(step / 4);
        for (int i = 0; i < segments; ++i) {
            const double a = t0 + i * step, b = a + step;
            const Point2Dd p0 = at(a), p3 = at(b);
            Curve({p0.x - k * rx * std::sin(a), p0.y + k * ry * std::cos(a)},
                  {p3.x + k * rx * std::sin(b), p3.y - k * ry * std::cos(b)}, p3);
        }
    }
    void Ellipse(const Rect2Dd& r) {
        Arc(r, 0, 360, true);
        Close();
    }
    // A cardinal spline through `pts` from segment `first`, `segments` of
    // them: GDI+'s curves, with its tension (0.5 is its default).
    void Cardinal(const std::vector<Point2Dd>& pts, double tension, bool closed, size_t first = 0,
                  size_t segments = SIZE_MAX) {
        const size_t n = pts.size();
        if (n < 2) return;
        const size_t total = closed ? n : n - 1;
        segments = std::min(segments, total > first ? total - first : 0);
        if (segments == 0) return;
        auto P = [&](long i) {
            if (closed) return pts[static_cast<size_t>(((i % static_cast<long>(n)) + n) % n)];
            return pts[static_cast<size_t>(std::max(0L, std::min(static_cast<long>(n) - 1, i)))];
        };
        const double t = tension / 3;
        Move(P(static_cast<long>(first)));
        for (size_t s = first; s < first + segments; ++s) {
            const long i = static_cast<long>(s);
            const Point2Dd a = P(i - 1), b = P(i), c = P(i + 1), d = P(i + 2);
            Curve({b.x + t * (c.x - a.x), b.y + t * (c.y - a.y)}, {c.x - t * (d.x - b.x), c.y - t * (d.y - b.y)}, c);
        }
        if (closed) Close();
    }
};

}   // namespace

// ===== OBJECTS =====

struct EmfPlusPlayer::Brush {
    enum Kind { Solid, Linear, Unsupported } kind = Solid;
    Color color = Color(0, 0, 0, 255);
    // Linear: the ramp runs across `rect` left to right, through `transform`.
    Rect2Dd rect;
    Matrix3x3 transform = Matrix3x3::Identity();
    std::vector<GradientStop> stops;
    GradientSpreadMethod spread = GradientSpreadMethod::Repeat;
};

struct EmfPlusPlayer::Pen {
    std::shared_ptr<Brush> brush;
    double width = 1;
    int unit = 0;
    StrokeLineCap cap = StrokeLineCap::Butt;
    StrokeLineJoin join = StrokeLineJoin::Miter;
    double miter = 10;
    std::vector<double> dashes;   // in pen widths
};

struct EmfPlusPlayer::Path {
    std::vector<Point2Dd> points;
    std::vector<uint8_t> types;
};

struct EmfPlusPlayer::Font {
    double emSize = 12;
    int unit = 3;
    uint32_t style = 0;   // 1 bold, 2 italic, 4 underline, 8 strikeout
    std::string family;
};

struct EmfPlusPlayer::Object {
    std::shared_ptr<Brush> brush;
    std::shared_ptr<Pen> pen;
    std::shared_ptr<Path> path;
    std::shared_ptr<Font> font;
    int alignment = -1;   // a StringFormat's: 0 near, 1 centre, 2 far
};

namespace {

std::shared_ptr<EmfPlusPlayer::Brush> ReadBrush(Reader& r) {
    auto b = std::make_shared<EmfPlusPlayer::Brush>();
    r.U32();   // version
    const uint32_t type = r.U32();
    switch (type) {
        case 0:   // solid
            b->color = Argb(r.U32());
            break;
        case 1: {   // hatch: drawn in its foreground colour
            r.U32();
            b->color = Argb(r.U32());
            break;
        }
        case 4: {   // linear gradient
            b->kind = EmfPlusPlayer::Brush::Linear;
            const uint32_t flags = r.U32();
            const uint32_t wrap = r.U32();
            b->spread = wrap == 4 ? GradientSpreadMethod::Pad
                      : wrap == 0 ? GradientSpreadMethod::Repeat : GradientSpreadMethod::Reflect;
            b->rect = ReadRect(r, false);
            const Color start = Argb(r.U32()), end = Argb(r.U32());
            r.Skip(8);
            if (flags & 0x02) b->transform = ReadMatrix(r);
            auto mix = [&](double f) {
                auto c = [&](uint8_t a, uint8_t z) { return static_cast<uint8_t>(std::lround(a + (z - a) * f)); };
                return Color(c(start.r, end.r), c(start.g, end.g), c(start.b, end.b), c(start.a, end.a));
            };
            if (flags & 0x04) {   // preset colours
                const uint32_t count = r.U32();
                if (count > r.n) { r.ok = false; break; }
                std::vector<double> pos(count);
                for (auto& p : pos) p = r.F32();
                for (uint32_t i = 0; i < count && r.ok; ++i) b->stops.emplace_back(pos[i], Argb(r.U32()));
            } else if (flags & 0x08) {   // horizontal blend factors
                const uint32_t count = r.U32();
                if (count > r.n) { r.ok = false; break; }
                std::vector<double> pos(count);
                for (auto& p : pos) p = r.F32();
                for (uint32_t i = 0; i < count && r.ok; ++i) b->stops.emplace_back(pos[i], mix(r.F32()));
            }
            if (b->stops.empty()) b->stops = {GradientStop(0, start), GradientStop(1, end)};
            b->color = start;
            break;
        }
        default:   // texture, path gradient: not drawn
            b->kind = EmfPlusPlayer::Brush::Unsupported;
            break;
    }
    return r.ok ? b : nullptr;
}

std::shared_ptr<EmfPlusPlayer::Pen> ReadPen(Reader& r) {
    auto p = std::make_shared<EmfPlusPlayer::Pen>();
    r.U32();   // version
    r.U32();   // type (0)
    const uint32_t flags = r.U32();
    p->unit = static_cast<int>(r.U32());
    p->width = r.F32();
    if (flags & 0x0001) r.Skip(24);   // pen transform
    auto cap = [](uint32_t c) {
        return c == 1 ? StrokeLineCap::Square : c == 2 ? StrokeLineCap::Round : StrokeLineCap::Butt;
    };
    if (flags & 0x0002) p->cap = cap(r.U32());
    if (flags & 0x0004) r.U32();   // end cap: the start cap stands for both
    if (flags & 0x0008) {
        const uint32_t j = r.U32();
        p->join = j == 1 ? StrokeLineJoin::Bevel : j == 2 ? StrokeLineJoin::Round : StrokeLineJoin::Miter;
    }
    if (flags & 0x0010) p->miter = r.F32();
    uint32_t lineStyle = 0;
    if (flags & 0x0020) lineStyle = r.U32();
    if (flags & 0x0040) r.U32();   // dashed line cap
    if (flags & 0x0080) r.F32();   // dash offset
    std::vector<double> custom;
    if (flags & 0x0100) {
        const uint32_t count = r.U32();
        if (count > r.n) { r.ok = false; return nullptr; }
        for (uint32_t i = 0; i < count; ++i) custom.push_back(r.F32());
    }
    if (flags & 0x0200) r.U32();   // alignment
    if (flags & 0x0400) {          // compound line
        const uint32_t count = r.U32();
        if (count > r.n) { r.ok = false; return nullptr; }
        r.Skip(4 * static_cast<size_t>(count));
    }
    if (flags & 0x0800) r.Skip(r.U32());   // custom start cap
    if (flags & 0x1000) r.Skip(r.U32());   // custom end cap
    switch (lineStyle) {
        case 1: p->dashes = {3, 1}; break;
        case 2: p->dashes = {1, 1}; break;
        case 3: p->dashes = {3, 1, 1, 1}; break;
        case 4: p->dashes = {3, 1, 1, 1, 1, 1}; break;
        case 5: p->dashes = custom; break;
        default: break;
    }
    p->brush = ReadBrush(r);
    return r.ok && p->brush ? p : nullptr;
}

std::shared_ptr<EmfPlusPlayer::Path> ReadPath(Reader& r) {
    auto p = std::make_shared<EmfPlusPlayer::Path>();
    r.U32();   // version
    const uint32_t count = r.U32();
    const uint32_t flags = r.U32();
    p->points = ReadPoints(r, static_cast<uint16_t>(flags), count);
    if (!r.ok || p->points.size() != count) return nullptr;
    if (flags & 0x1000) {   // run-length encoded types
        while (p->types.size() < count && r.ok) {
            const uint8_t run = r.U8() & 0x3F;
            const uint8_t type = r.U8();
            p->types.insert(p->types.end(), std::max<uint8_t>(run, 1), type);
        }
        p->types.resize(count);
    } else {
        for (uint32_t i = 0; i < count && r.ok; ++i) p->types.push_back(r.U8());
    }
    return r.ok ? p : nullptr;
}

std::shared_ptr<EmfPlusPlayer::Font> ReadFont(Reader& r) {
    auto f = std::make_shared<EmfPlusPlayer::Font>();
    r.U32();   // version
    f->emSize = r.F32();
    f->unit = static_cast<int>(r.U32());
    f->style = r.U32();
    r.U32();   // reserved
    const uint32_t length = r.U32();
    f->family = Utf16(r, length);
    return r.ok ? f : nullptr;
}

// A path object's subpaths: point types 0 start, 1 line, 3 Bezier (in
// threes), with 0x80 closing the subpath.
void AddPath(Builder& b, const EmfPlusPlayer::Path& path) {
    const auto& pts = path.points;
    const auto& types = path.types;
    for (size_t i = 0; i < pts.size(); ++i) {
        const uint8_t t = types[i] & 0x07;
        if (t == 0 || i == 0) {
            b.Move(pts[i]);
        } else if (t == 3 && i + 2 < pts.size()) {
            b.Curve(pts[i], pts[i + 1], pts[i + 2]);
            i += 2;
        } else {
            b.Line(pts[i]);
        }
        if (types[i] & 0x80) b.Close();
    }
}

}   // namespace

// ===== THE PLAYER =====

EmfPlusPlayer::EmfPlusPlayer(double pxToPtIn, const Rect2Dd& pageIn, Emit emitIn)
        : pxToPt(pxToPtIn), page(pageIn), emit(std::move(emitIn)),
          world(Matrix3x3::Identity()), base(Matrix3x3::Identity()) {}

double EmfPlusPlayer::UnitToPx(int unit, bool horizontal) const {
    const double dpi = horizontal ? dpiX : dpiY;
    switch (unit) {
        case 1: return videoDisplay ? 1.0 : dpi / 100;   // display
        case 3: return dpi / 72;                          // point
        case 4: return dpi;                               // inch
        case 5: return dpi / 300;                         // document
        case 6: return dpi / 25.4;                        // millimetre
        default: return 1.0;                              // world, pixel
    }
}

Matrix3x3 EmfPlusPlayer::ToPoints() const {
    return Matrix3x3::Scale(pxToPt * pageScale * UnitToPx(pageUnit, true),
                            pxToPt * pageScale * UnitToPx(pageUnit, false)) *
           base * world;
}

double EmfPlusPlayer::LengthScale() const {
    return std::sqrt(std::fabs(ToPoints().Determinant()));
}

Point2Dd EmfPlusPlayer::Pt(double x, double y) const { return ToPoints().Transform(Point2Dd(x, y)); }

std::shared_ptr<EmfPlusPlayer::Brush> EmfPlusPlayer::BrushFor(uint16_t flags, uint32_t id) const {
    if (flags & 0x8000) {   // S: the id is the colour itself
        auto b = std::make_shared<Brush>();
        b->color = Argb(id);
        return b;
    }
    auto it = objects.find(static_cast<uint8_t>(id & 0xFF));
    return it != objects.end() && it->second->brush ? it->second->brush : nullptr;
}

std::shared_ptr<EmfPlusPlayer::Pen> EmfPlusPlayer::PenFor(uint32_t id) const {
    auto it = objects.find(static_cast<uint8_t>(id & 0xFF));
    return it != objects.end() ? it->second->pen : nullptr;
}

bool EmfPlusPlayer::FillWith(const Brush& b, VectorStyle& style) const {
    if (b.kind == Brush::Unsupported) return false;
    if (b.kind == Brush::Solid) {
        style.Fill = b.color;
        return true;
    }
    // The ramp across the brush rectangle, placed on the page now: a brush
    // lives in the world space of the drawing that uses it.
    const Matrix3x3 m = ToPoints() * b.transform;
    LinearGradientData g;
    g.Units = GradientUnits::UserSpaceOnUse;
    g.Start = m.Transform(Point2Dd(b.rect.x, b.rect.y + b.rect.height / 2));
    g.End = m.Transform(Point2Dd(b.rect.x + b.rect.width, b.rect.y + b.rect.height / 2));
    g.Stops = b.stops;
    g.SpreadMethod = b.spread;
    style.Fill = GradientData(g);
    return true;
}

bool EmfPlusPlayer::StrokeWith(const Pen& p, VectorStyle& style) const {
    if (!p.brush || p.brush->kind == Brush::Unsupported) return false;
    VectorStyle fill;
    if (!FillWith(*p.brush, fill) || !fill.Fill) return false;
    StrokeData s;
    s.Fill = *fill.Fill;
    double width = p.unit == 0 ? p.width * LengthScale() : p.width * UnitToPx(p.unit) * pxToPt;
    if (!(width > 0)) width = pxToPt;   // width 0: GDI+'s one-pixel hairline
    s.Width = static_cast<float>(width);
    s.LineCap = p.cap;
    s.LineJoin = p.join;
    s.MiterLimit = static_cast<float>(std::max(1.0, p.miter));
    for (double d : p.dashes) s.DashArray.push_back(d * width);
    style.Stroke = s;
    return true;
}

void EmfPlusPlayer::Play(const uint8_t* data, size_t size) {
    seen = true;
    size_t at = 0;
    while (at + 12 <= size) {
        uint16_t type, flags;
        uint32_t recSize, dataSize;
        std::memcpy(&type, data + at, 2);
        std::memcpy(&flags, data + at + 2, 2);
        std::memcpy(&recSize, data + at + 4, 4);
        std::memcpy(&dataSize, data + at + 8, 4);
        if (recSize < 12 || at + recSize > size) break;
        Record(type, flags, data + at + 12, std::min<size_t>(dataSize, recSize - 12));
        at += recSize;
    }
}

void EmfPlusPlayer::Drawing(bool played, std::shared_ptr<VectorElement> element) {
    stretch = true;
    if (!played) stretchOk = false;
    if (played && element) pending.push_back(std::move(element));
}

void EmfPlusPlayer::Settle() {
    if (!stretch) return;
    const bool keep = stretchOk || !dual;
    if (keep) for (auto& e : pending) emit(e);
    // The GDI copy of a stretch drawn here is skipped; the copy of one that
    // was not is what draws it.
    gdiSuppressed = dual && stretchOk;
    pending.clear();
    stretch = false;
    stretchOk = true;
}

bool EmfPlusPlayer::GdiDraws() {
    Settle();
    return !gdiSuppressed;
}

void EmfPlusPlayer::Finish() { Settle(); }

std::string EmfPlusPlayer::Report() const {
    if (fellBack.empty()) return "";
    std::ostringstream msg;
    msg << "EMF+ import: " << (dual ? "drawn from the file's GDI copy instead" : "not drawn") << ":";
    for (const auto& [type, count] : fellBack) msg << " 0x" << std::hex << type << std::dec << " (x" << count << ")";
    return msg.str();
}

void EmfPlusPlayer::ObjectPart(uint16_t flags, const uint8_t* d, size_t n) {
    const uint8_t id = static_cast<uint8_t>(flags & 0xFF);
    const int type = (flags >> 8) & 0x7F;
    if (flags & 0x8000) {   // continued: TotalObjectSize, then this part
        if (n < 4) return;
        uint32_t total;
        std::memcpy(&total, d, 4);
        if (partial.empty() || partialId != id || partialFlags != (flags & 0x7FFF)) {
            partial.clear();
            partialId = id;
            partialFlags = flags & 0x7FFF;
            partialTotal = total;
        }
        partial.insert(partial.end(), d + 4, d + n);
        if (partial.size() >= partialTotal) {
            DefineObject(id, type, partial.data(), partial.size());
            partial.clear();
        }
        return;
    }
    if (!partial.empty() && partialId == id) {   // the last part of a continued object
        partial.insert(partial.end(), d, d + n);
        DefineObject(id, type, partial.data(), partial.size());
        partial.clear();
        return;
    }
    DefineObject(id, type, d, n);
}

void EmfPlusPlayer::DefineObject(uint8_t id, int type, const uint8_t* d, size_t n) {
    Reader r(d, n);
    auto o = std::make_shared<Object>();
    switch (type) {
        case 1: o->brush = ReadBrush(r); break;
        case 2: o->pen = ReadPen(r); break;
        case 3: o->path = ReadPath(r); break;
        case 6: o->font = ReadFont(r); break;
        case 7: {   // string format: version, flags, language, alignment
            r.Skip(12);
            o->alignment = static_cast<int>(r.U32());
            break;
        }
        default: break;   // regions, images, attributes: not drawn
    }
    objects[id] = o;
}

void EmfPlusPlayer::Record(uint16_t type, uint16_t flags, const uint8_t* d, size_t n) {
    Reader r(d, n);
    // A shape with its style, or nothing when the style cannot be drawn.
    auto filled = [&](uint32_t brushId) -> std::shared_ptr<VectorPath> {
        auto brush = BrushFor(flags, brushId);
        auto path = std::make_shared<VectorPath>();
        if (!brush || !FillWith(*brush, path->Style)) return nullptr;
        path->Style.ShapeFillRule = VectorStorage::FillRule::EvenOdd;   // GDI+'s FillModeAlternate
        return path;
    };
    auto stroked = [&](uint32_t penId) -> std::shared_ptr<VectorPath> {
        auto pen = PenFor(penId);
        auto path = std::make_shared<VectorPath>();
        if (!pen || !StrokeWith(*pen, path->Style)) return nullptr;
        return path;
    };
    auto done = [&](const std::shared_ptr<VectorPath>& path) {
        const bool ok = path && r.ok;
        if (!ok) ++fellBack[type];
        Drawing(ok, ok && !path->Path.commands.empty() ? path : nullptr);
    };
    const Matrix3x3 m = ToPoints();
    const bool compressed = (flags & 0x4000) != 0;

    switch (type) {
        case Header: {
            dual = (flags & 0x0001) != 0;
            r.U32();   // version
            videoDisplay = (r.U32() & 0x1) != 0;
            const double dx = r.U32(), dy = r.U32();
            if (dx > 0 && dx < 100000) dpiX = dx;
            if (dy > 0 && dy < 100000) dpiY = dy;
            break;
        }
        case EndOfFile: Settle(); break;
        case GetDC:
            Settle();
            gdiSuppressed = false;
            break;
        case ObjectRec: ObjectPart(flags, d, n); break;

        // ----- transforms -----
        case SetWorldTransform: world = ReadMatrix(r); break;
        case ResetWorldTransform: world = Matrix3x3::Identity(); break;
        case MultiplyWorldTransform: {
            const Matrix3x3 t = ReadMatrix(r);
            world = (flags & 0x2000) ? t * world : world * t;   // A: appended, applied after
            break;
        }
        case TranslateWorldTransform: {
            const double dx = r.F32(), dy = r.F32();
            const Matrix3x3 t = Matrix3x3::Translate(dx, dy);
            world = (flags & 0x2000) ? t * world : world * t;
            break;
        }
        case ScaleWorldTransform: {
            const double sx = r.F32(), sy = r.F32();
            const Matrix3x3 t = Matrix3x3::Scale(sx, sy);
            world = (flags & 0x2000) ? t * world : world * t;
            break;
        }
        case RotateWorldTransform: {
            const Matrix3x3 t = Matrix3x3::RotateDegrees(r.F32());
            world = (flags & 0x2000) ? t * world : world * t;
            break;
        }
        case SetPageTransform: {
            pageUnit = flags & 0xFF;
            const double s = r.F32();
            pageScale = s > 0 ? s : 1;
            break;
        }
        case Save: saved[r.U32()].push_back({world, base, pageUnit, pageScale}); break;
        case Restore: case EndContainer: {
            auto it = saved.find(r.U32());
            if (it != saved.end() && !it->second.empty()) {
                const State s = it->second.back();
                it->second.pop_back();
                world = s.world;
                base = s.base;
                pageUnit = s.pageUnit;
                pageScale = s.pageScale;
            }
            break;
        }
        case BeginContainer: {
            const int unit = flags & 0xFF;
            const Rect2Dd dst = ReadRect(r, false), src = ReadRect(r, false);
            const uint32_t index = r.U32();
            saved[index].push_back({world, base, pageUnit, pageScale});
            Matrix3x3 c = Matrix3x3::Identity();
            if (src.width != 0 && src.height != 0) {
                const double u = UnitToPx(unit) / UnitToPx(pageUnit) / pageScale;
                c = Matrix3x3::Translate(dst.x * u, dst.y * u) *
                    Matrix3x3::Scale(dst.width * u / src.width, dst.height * u / src.height) *
                    Matrix3x3::Translate(-src.x, -src.y);
            }
            base = base * world * c;
            world = Matrix3x3::Identity();
            break;
        }
        case BeginContainerNoParams:
            saved[r.U32()].push_back({world, base, pageUnit, pageScale});
            base = base * world;
            world = Matrix3x3::Identity();
            break;

        // ----- drawing -----
        case Clear: {
            auto path = std::make_shared<VectorPath>();
            path->Style.Fill = Argb(r.U32());
            const Matrix3x3 identity = Matrix3x3::Identity();
            Builder b{*path, identity};
            b.Rect(page);
            done(path);
            break;
        }
        case FillRects: case DrawRects: {
            auto path = type == FillRects ? filled(r.U32()) : stroked(flags & 0xFF);
            const uint32_t count = r.U32();
            if (path) {
                // Each rectangle filled on its own, so overlaps stay filled.
                if (type == FillRects) path->Style.ShapeFillRule = VectorStorage::FillRule::NonZero;
                Builder b{*path, m};
                for (uint32_t i = 0; i < count && r.ok; ++i) {
                    Rect2Dd rc = ReadRect(r, compressed);
                    if (rc.width < 0) { rc.x += rc.width; rc.width = -rc.width; }
                    if (rc.height < 0) { rc.y += rc.height; rc.height = -rc.height; }
                    b.Rect(rc);
                }
            }
            done(path);
            break;
        }
        case FillPolygon: {
            auto path = filled(r.U32());
            const uint32_t count = r.U32();
            const auto pts = ReadPoints(r, flags, count);
            if (path) {
                Builder b{*path, m};
                b.Polygon(pts, true);
            }
            done(path);
            break;
        }
        case DrawLines: {
            auto path = stroked(flags & 0xFF);
            const uint32_t count = r.U32();
            const auto pts = ReadPoints(r, flags, count);
            if (path) {
                Builder b{*path, m};
                b.Polygon(pts, (flags & 0x2000) != 0);   // L: closed
            }
            done(path);
            break;
        }
        case FillEllipse: case DrawEllipse: {
            auto path = type == FillEllipse ? filled(r.U32()) : stroked(flags & 0xFF);
            const Rect2Dd rc = ReadRect(r, compressed);
            if (path) {
                Builder b{*path, m};
                b.Ellipse(rc);
            }
            done(path);
            break;
        }
        case FillPie: case DrawPie: case DrawArc: {
            auto path = type == FillPie ? filled(r.U32()) : stroked(flags & 0xFF);
            const double start = r.F32(), sweep = r.F32();
            const Rect2Dd rc = ReadRect(r, compressed);
            if (path) {
                Builder b{*path, m};
                if (type == DrawArc) {
                    b.Arc(rc, start, sweep, true);
                } else {
                    b.Move({rc.x + rc.width / 2, rc.y + rc.height / 2});
                    b.Arc(rc, start, sweep, false);
                    b.Close();
                }
            }
            done(path);
            break;
        }
        case FillPath: case DrawPath: {
            auto it = objects.find(static_cast<uint8_t>(flags & 0xFF));
            const auto pathObject = it != objects.end() ? it->second->path : nullptr;
            const uint32_t id = r.U32();
            // FillPath's S flag says the brush id is a colour; the path's id
            // is in the low byte either way.
            auto path = type == FillPath ? filled(id) : stroked(id);
            if (path && pathObject) {
                Builder b{*path, m};
                AddPath(b, *pathObject);
            }
            done(pathObject ? path : nullptr);
            break;
        }
        case FillClosedCurve: case DrawClosedCurve: case DrawCurve: {
            auto path = type == FillClosedCurve ? filled(r.U32()) : stroked(flags & 0xFF);
            const double tension = r.F32();
            size_t first = 0, segments = SIZE_MAX;
            if (type == DrawCurve) {
                first = r.U32();
                segments = r.U32();
            }
            const uint32_t count = r.U32();
            const auto pts = ReadPoints(r, flags, count);
            if (path) {
                if (type == FillClosedCurve && (flags & 0x2000))   // W: winding
                    path->Style.ShapeFillRule = VectorStorage::FillRule::NonZero;
                Builder b{*path, m};
                b.Cardinal(pts, tension, type != DrawCurve, first, segments);
            }
            done(path);
            break;
        }
        case DrawBeziers: {
            auto path = stroked(flags & 0xFF);
            const uint32_t count = r.U32();
            const auto pts = ReadPoints(r, flags, count);
            if (path && !pts.empty()) {
                Builder b{*path, m};
                b.Move(pts[0]);
                for (size_t i = 1; i + 2 < pts.size(); i += 3) b.Curve(pts[i], pts[i + 1], pts[i + 2]);
            }
            done(path);
            break;
        }
        case DrawString: {
            auto fit = objects.find(static_cast<uint8_t>(flags & 0xFF));
            const auto font = fit != objects.end() ? fit->second->font : nullptr;
            const auto brush = BrushFor(flags, r.U32());
            const uint32_t formatId = r.U32();
            const uint32_t length = r.U32();
            const Rect2Dd layout = ReadRect(r, false);
            const std::string text = Utf16(r, length);
            auto t = std::make_shared<VectorText>();
            VectorStyle fill;
            const bool ok = font && brush && FillWith(*brush, fill) && r.ok;
            if (!ok) {
                ++fellBack[type];
                Drawing(false);
                break;
            }
            int alignment = 0;
            auto format = objects.find(static_cast<uint8_t>(formatId & 0xFF));
            if (format != objects.end() && format->second->alignment >= 0) alignment = format->second->alignment;
            // The size in world units, so the transform scales it with the drawing.
            const Matrix3x3 worldOnly = base * world;
            const double worldScale = std::sqrt(std::fabs(worldOnly.Determinant()));
            const double sizePt = font->unit == 0 ? font->emSize * LengthScale()
                                                  : font->emSize * UnitToPx(font->unit) * pxToPt * worldScale;
            const double emWorld = sizePt / std::max(1e-9, LengthScale());
            double x = layout.x;
            if (alignment == 1) x += layout.width / 2;
            else if (alignment == 2) x += layout.width;
            // The layout box's top: the first baseline sits about an ascent below.
            t->Position = m.Transform(Point2Dd(x, layout.y + 0.9 * emWorld));
            if (!font->family.empty()) t->BaseStyle.FontFamily = font->family;
            t->BaseStyle.FontSize = static_cast<float>(std::max(0.5, sizePt));
            t->BaseStyle.Weight = (font->style & 1) ? FontWeight::Bold : FontWeight::Normal;
            t->BaseStyle.Slant = (font->style & 2) ? FontSlant::Italic : FontSlant::Normal;
            t->BaseStyle.Underline = (font->style & 4) != 0;
            t->BaseStyle.StrikeThrough = (font->style & 8) != 0;
            t->BaseStyle.Anchor = alignment == 1 ? TextAnchor::Middle : alignment == 2 ? TextAnchor::End
                                                                                        : TextAnchor::Start;
            t->Style.Fill = fill.Fill;
            t->SetText(text);
            Drawing(true, text.empty() ? nullptr : t);
            break;
        }
        case FillRegion: case DrawImage: case DrawImagePoints: case DrawDriverString: case StrokeFillPath:
            ++fellBack[type];
            Drawing(false);
            break;
        default:
            // Hints, clipping, comments, multi-format sections, terminal
            // server records: nothing to draw.
            break;
    }
}

} // namespace VectorConverter
} // namespace UltraCanvas
