// UltraCanvas/Plugins/Vector/UltraCanvasDXFReader.cpp
// DXF (AutoCAD Drawing Exchange Format) reader - the import side of
// DXFConverter (the writer lives in UltraCanvasDXFConverter.cpp).
//
// Parses tagged ASCII DXF per Autodesk's public reference: the HEADER
// extents, the LAYER/LTYPE/STYLE tables and the ENTITIES section with
// LINE, CIRCLE, ARC, ELLIPSE, LWPOLYLINE (bulges included), legacy
// POLYLINE/VERTEX, SPLINE, HATCH, SOLID, TEXT and MTEXT. Curved geometry
// comes back as real cubics: arcs and bulges convert exactly (to within
// the standard bezier arc approximation), and splines whose knot vector is
// the piecewise-bezier form (the writer's own output, clamped with interior
// multiplicity = degree) reproduce their cubics exactly; general NURBS are
// sampled with de Boor evaluation and reported through the warning
// callback. DXF's Y-up world maps to the document's Y-down page using the
// $EXTMIN/$EXTMAX extents (computed from the entities when a file declares
// none). Entities keep their real layers; unsupported entity types are
// counted and reported, never dropped silently.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "UltraCanvasCADConverters.h"
#include "UltraCanvasVectorStorage.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace VectorConverter {

using namespace VectorStorage;

// The classic AutoCAD Color Index palette: exact values for the named
// colours (1-9) and the greys (250-255), the standard 24-hue construction
// for the colour wheel entries (10-249). Declared in
// UltraCanvasCADConverters.h; the writer's nearest-ACI fallback uses it too.
Color AciPaletteColor(int aci) {
    switch (aci) {
        case 1: return Color(255, 0, 0, 255);
        case 2: return Color(255, 255, 0, 255);
        case 3: return Color(0, 255, 0, 255);
        case 4: return Color(0, 255, 255, 255);
        case 5: return Color(0, 0, 255, 255);
        case 6: return Color(255, 0, 255, 255);
        case 7: return Color(0, 0, 0, 255);      // white on screen; black on page
        case 8: return Color(128, 128, 128, 255);
        case 9: return Color(192, 192, 192, 255);
        default: break;
    }
    if (aci >= 250 && aci <= 255) {
        int v = 51 + (aci - 250) * 40;   // 51..251 grey ramp
        return Color(static_cast<uint8_t>(v), static_cast<uint8_t>(v),
                     static_cast<uint8_t>(v), 255);
    }
    if (aci < 10 || aci > 249) return Color(0, 0, 0, 255);
    int idx = aci - 10;
    double hue = (idx / 10) * 15.0;              // 24 hues, 15 degrees apart
    int variant = idx % 10;
    static const double values[] = {1.0, 0.8, 0.6, 0.5, 0.3};
    double v = values[variant / 2];
    double s = (variant % 2) ? 0.5 : 1.0;
    double c = v * s, hp = hue / 60.0, x = c * (1 - std::fabs(std::fmod(hp, 2.0) - 1));
    double r = 0, g = 0, b = 0;
    if (hp < 1) { r = c; g = x; }
    else if (hp < 2) { r = x; g = c; }
    else if (hp < 3) { g = c; b = x; }
    else if (hp < 4) { g = x; b = c; }
    else if (hp < 5) { r = x; b = c; }
    else { r = c; b = x; }
    double m = v - c;
    auto B = [&](double f) { return static_cast<uint8_t>(std::lround((f + m) * 255)); };
    return Color(B(r), B(g), B(b), 255);
}

namespace {

constexpr double kPi = 3.14159265358979323846;

struct Tag {
    int code;
    std::string value;
    double D() const { return std::atof(value.c_str()); }
    int I() const { return std::atoi(value.c_str()); }
};

struct LayerDef {
    Color color = Color(0, 0, 0, 255);
    std::string linetype = "Continuous";
    int lineweight = -3;
    bool plottable = true;
};

struct TableData {
    std::map<std::string, LayerDef> layers;
    std::vector<std::string> layerOrder;
    std::map<std::string, std::vector<double>> linetypes;   // name -> dashes (on/off)
    std::map<std::string, std::string> textStyles;          // style name -> family
};

class DxfReader {
public:
    DxfReader(const std::string& data, std::function<void(const std::string&)> warnFn)
            : warn(std::move(warnFn)) {
        Tokenize(data);
    }

    std::shared_ptr<VectorDocument> Parse() {
        if (tags.empty()) {
            warn("DXF import: no group codes found (binary DXF is not supported)");
            return nullptr;
        }
        ScanSections();
        if (!sawEntities) {
            warn("DXF import: no ENTITIES section");
            return nullptr;
        }

        if (!hasExtents) ComputeExtentsFromEntities();
        pageW = extMaxX - extMinX;
        pageH = extMaxY - extMinY;
        if (pageW <= 0 || pageH <= 0) { pageW = 595; pageH = 842; }

        doc = std::make_shared<VectorDocument>();
        doc->Size = Size2Dd{pageW, pageH};

        for (const auto& name : tables.layerOrder) GetLayer(name);
        ParseEntities();

        // Table-only layers that received no entities are noise in a
        // drawing document; keep only layers that hold content.
        doc->Layers.erase(
                std::remove_if(doc->Layers.begin(), doc->Layers.end(),
                               [](const std::shared_ptr<VectorLayer>& l) {
                                   return !l || l->Children.empty();
                               }),
                doc->Layers.end());

        if (!skipped.empty()) {
            std::ostringstream msg;
            msg << "DXF import: unsupported entity types skipped:";
            for (const auto& [type, count] : skipped) {
                msg << " " << type << " (x" << count << ")";
            }
            warn(msg.str());
        }
        return doc;
    }

private:
    std::function<void(const std::string&)> warn;
    std::vector<Tag> tags;
    size_t entStart = 0, entEnd = 0;   // ENTITIES section tag range
    bool sawEntities = false;
    TableData tables;
    double extMinX = 0, extMinY = 0, extMaxX = 0, extMaxY = 0;
    bool hasExtents = false;
    double pageW = 0, pageH = 0;
    std::shared_ptr<VectorDocument> doc;
    std::map<std::string, std::shared_ptr<VectorLayer>> layerGroups;
    std::map<std::string, int> skipped;
    bool warnedSplineApprox = false, warnedHatchPattern = false;

    // Document coordinates: shift to the extents origin, flip Y.
    Point2Dd P(double x, double y) const {
        return Point2Dd(x - extMinX, pageH - (y - extMinY));
    }

    void Tokenize(const std::string& data) {
        size_t pos = 0;
        auto line = [&](std::string& out) {
            if (pos >= data.size()) return false;
            size_t nl = data.find('\n', pos);
            if (nl == std::string::npos) nl = data.size();
            out.assign(data, pos, nl - pos);
            if (!out.empty() && out.back() == '\r') out.pop_back();
            pos = nl + 1;
            return true;
        };
        std::string codeLine, valueLine;
        while (line(codeLine)) {
            if (!line(valueLine)) break;
            // Group code lines are numeric (possibly space-padded).
            char* end = nullptr;
            long code = std::strtol(codeLine.c_str(), &end, 10);
            if (end == codeLine.c_str()) continue;   // not a DXF pair; skip
            tags.push_back({static_cast<int>(code), valueLine});
        }
    }

    void ScanSections() {
        size_t i = 0;
        while (i < tags.size()) {
            if (tags[i].code == 0 && tags[i].value == "SECTION" &&
                i + 1 < tags.size() && tags[i + 1].code == 2) {
                const std::string section = tags[i + 1].value;
                size_t start = i + 2;
                size_t end = start;
                while (end < tags.size() &&
                       !(tags[end].code == 0 && tags[end].value == "ENDSEC")) {
                    ++end;
                }
                if (section == "HEADER") ParseHeader(start, end);
                else if (section == "TABLES") ParseTables(start, end);
                else if (section == "ENTITIES") {
                    entStart = start;
                    entEnd = end;
                    sawEntities = true;
                }
                i = end + 1;
            } else {
                ++i;
            }
        }
    }

    void ParseHeader(size_t start, size_t end) {
        for (size_t i = start; i < end; ++i) {
            if (tags[i].code != 9) continue;
            const std::string& var = tags[i].value;
            auto grab = [&](int code, double& out) {
                for (size_t j = i + 1; j < end && tags[j].code != 9; ++j) {
                    if (tags[j].code == code) { out = tags[j].D(); return; }
                }
            };
            if (var == "$EXTMIN") {
                grab(10, extMinX);
                grab(20, extMinY);
                hasExtents = true;
            } else if (var == "$EXTMAX") {
                grab(10, extMaxX);
                grab(20, extMaxY);
                hasExtents = true;
            }
        }
        if (hasExtents && (extMaxX <= extMinX || extMaxY <= extMinY)) {
            hasExtents = false;   // declared but degenerate; recompute
        }
    }

    void ParseTables(size_t start, size_t end) {
        for (size_t i = start; i < end; ++i) {
            if (tags[i].code != 0) continue;
            const std::string& rec = tags[i].value;
            size_t recEnd = i + 1;
            while (recEnd < end && tags[recEnd].code != 0) ++recEnd;

            auto field = [&](int code) -> const std::string* {
                for (size_t j = i + 1; j < recEnd; ++j) {
                    if (tags[j].code == code) return &tags[j].value;
                }
                return nullptr;
            };

            if (rec == "LAYER") {
                const std::string* name = field(2);
                if (!name) continue;
                LayerDef def;
                if (const std::string* c = field(62)) {
                    int aci = std::atoi(c->c_str());
                    def.plottable = aci >= 0;   // negative = layer off
                    def.color = AciPaletteColor(std::abs(aci));
                }
                if (const std::string* tc = field(420)) {
                    long rgb = std::atol(tc->c_str());
                    def.color = Color((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF,
                                      rgb & 0xFF, 255);
                }
                if (const std::string* lt = field(6)) def.linetype = *lt;
                if (const std::string* lw = field(370)) {
                    def.lineweight = std::atoi(lw->c_str());
                }
                if (!tables.layers.count(*name)) tables.layerOrder.push_back(*name);
                tables.layers[*name] = def;
            } else if (rec == "LTYPE") {
                const std::string* name = field(2);
                if (!name) continue;
                std::vector<double> dashes;
                for (size_t j = i + 1; j < recEnd; ++j) {
                    if (tags[j].code == 49) dashes.push_back(tags[j].D());
                }
                tables.linetypes[*name] = dashes;
            } else if (rec == "STYLE") {
                const std::string* name = field(2);
                if (!name) continue;
                // The style name carries the family (the writer sanitizes it
                // but keeps spaces); the font file is the fallback.
                std::string family = *name;
                if (family.empty() || family == "Standard") {
                    if (const std::string* font = field(3)) {
                        family = *font;
                        size_t dot = family.find_last_of('.');
                        if (dot != std::string::npos) family.resize(dot);
                    }
                }
                tables.textStyles[*name] = family;
            }
            i = recEnd - 1;
        }
    }

    void ComputeExtentsFromEntities() {
        bool first = true;
        for (size_t i = entStart; i < entEnd; ++i) {
            int c = tags[i].code;
            bool isX = (c == 10 || c == 11);
            bool isY = (c == 20 || c == 21);
            if (!isX && !isY) continue;
            double v = tags[i].D();
            if (first) {
                extMinX = extMaxX = isX ? v : 0;
                extMinY = extMaxY = isY ? v : 0;
                first = false;
            }
            if (isX) { extMinX = std::min(extMinX, v); extMaxX = std::max(extMaxX, v); }
            else     { extMinY = std::min(extMinY, v); extMaxY = std::max(extMaxY, v); }
        }
        if (!first) {
            // A margin keeps zero-extent files usable.
            if (extMaxX - extMinX < 1) extMaxX = extMinX + 1;
            if (extMaxY - extMinY < 1) extMaxY = extMinY + 1;
        }
    }

    std::shared_ptr<VectorLayer> GetLayer(const std::string& name) {
        auto it = layerGroups.find(name);
        if (it != layerGroups.end()) return it->second;
        auto layer = doc->AddLayer(name.empty() ? "0" : name);
        layerGroups[name] = layer;
        return layer;
    }

    // ===== STYLE RESOLUTION =====

    Color EntityColor(const std::vector<Tag>& e, const LayerDef& layer) {
        for (const Tag& t : e) {
            if (t.code == 420) {
                long rgb = std::atol(t.value.c_str());
                return Color((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255);
            }
        }
        for (const Tag& t : e) {
            if (t.code == 62) {
                int aci = t.I();
                if (aci == 0 || aci == 256) break;   // ByBlock/ByLayer
                return AciPaletteColor(std::abs(aci));
            }
        }
        return layer.color;
    }

    std::vector<double> EntityDashes(const std::vector<Tag>& e, const LayerDef& layer) {
        std::string lt = layer.linetype;
        for (const Tag& t : e) {
            if (t.code == 6) { lt = t.value; break; }
        }
        if (lt.empty() || lt == "Continuous" || lt == "CONTINUOUS" ||
            lt == "ByBlock" || lt == "BYBLOCK") {
            if (lt == "ByBlock" || lt == "BYBLOCK") lt = layer.linetype;
            else return {};
        }
        if (lt == "ByLayer" || lt == "BYLAYER") lt = layer.linetype;
        auto it = tables.linetypes.find(lt);
        if (it == tables.linetypes.end() || it->second.empty()) return {};
        std::vector<double> dashes;
        for (double d : it->second) {
            dashes.push_back(d == 0 ? 0.5 : std::fabs(d));   // dots become short dashes
        }
        return dashes;
    }

    float EntityWidth(const std::vector<Tag>& e, const LayerDef& layer) {
        int lw = -1;
        for (const Tag& t : e) {
            if (t.code == 370) { lw = t.I(); break; }
        }
        if (lw < 0) lw = layer.lineweight;
        if (lw < 0) return 1.0f;                       // default/ByBlock
        if (lw == 0) return 0.25f;                     // "0" = hairline
        return static_cast<float>(lw / 100.0 * 72.0 / 25.4);   // 1/100 mm -> pt
    }

    StrokeData MakeStroke(const std::vector<Tag>& e, const LayerDef& layer) {
        StrokeData stroke;
        stroke.Fill = EntityColor(e, layer);
        stroke.Width = EntityWidth(e, layer);
        auto dashes = EntityDashes(e, layer);
        if (!dashes.empty()) stroke.DashArray = dashes;
        return stroke;
    }

    // ===== GEOMETRY HELPERS =====

    // One elliptical-arc span (< pi/2 each) as a cubic: the standard
    // tangent-scaled construction. C = centre, U/V = axis vectors.
    void ArcSpanToCubic(VectorPath& path, const Point2Dd& c, const Point2Dd& u,
                        const Point2Dd& v, double t1, double t2) {
        auto point = [&](double t) {
            return Point2Dd(c.x + u.x * std::cos(t) + v.x * std::sin(t),
                            c.y + u.y * std::cos(t) + v.y * std::sin(t));
        };
        auto deriv = [&](double t) {
            return Point2Dd(-u.x * std::sin(t) + v.x * std::cos(t),
                            -u.y * std::sin(t) + v.y * std::cos(t));
        };
        double k = 4.0 / 3.0 * std::tan((t2 - t1) / 4.0);
        Point2Dd p1 = point(t1), p2 = point(t2);
        Point2Dd d1 = deriv(t1), d2 = deriv(t2);
        path.CurveTo(static_cast<float>(p1.x + k * d1.x),
                     static_cast<float>(p1.y + k * d1.y),
                     static_cast<float>(p2.x - k * d2.x),
                     static_cast<float>(p2.y - k * d2.y),
                     static_cast<float>(p2.x), static_cast<float>(p2.y));
    }

    // Append an arc (already in document coordinates) running t1 -> t2.
    void AppendArc(VectorPath& path, const Point2Dd& c, const Point2Dd& u,
                   const Point2Dd& v, double t1, double t2, bool moveFirst) {
        auto start = Point2Dd(c.x + u.x * std::cos(t1) + v.x * std::sin(t1),
                              c.y + u.y * std::cos(t1) + v.y * std::sin(t1));
        if (moveFirst) {
            path.MoveTo(static_cast<float>(start.x), static_cast<float>(start.y));
        }
        int spans = std::max(1, static_cast<int>(
                std::ceil(std::fabs(t2 - t1) / (kPi / 2))));
        for (int s = 0; s < spans; ++s) {
            double a = t1 + (t2 - t1) * s / spans;
            double b = t1 + (t2 - t1) * (s + 1) / spans;
            ArcSpanToCubic(path, c, u, v, a, b);
        }
    }

    // A polyline bulge segment: bulge = tan(theta/4) of the included angle.
    void AppendBulge(VectorPath& path, const Point2Dd& from, const Point2Dd& to,
                     double bulge) {
        if (std::fabs(bulge) < 1e-12) {
            path.LineTo(static_cast<float>(to.x), static_cast<float>(to.y));
            return;
        }
        double theta = 4.0 * std::atan(bulge);
        double dx = to.x - from.x, dy = to.y - from.y;
        double chord = std::hypot(dx, dy);
        if (chord < 1e-12) return;
        double r = chord / (2.0 * std::sin(std::fabs(theta) / 2.0));
        // Perpendicular from the chord midpoint to the centre. Y is already
        // flipped into document space, so the winding sign flips with it.
        double sign = bulge > 0 ? -1.0 : 1.0;
        double h = std::sqrt(std::max(0.0, r * r - chord * chord / 4.0));
        if (std::fabs(theta) > kPi) h = -h;
        Point2Dd mid((from.x + to.x) / 2, (from.y + to.y) / 2);
        Point2Dd c(mid.x + sign * h * (-dy / chord), mid.y + sign * h * (dx / chord));
        double a1 = std::atan2(from.y - c.y, from.x - c.x);
        double a2 = std::atan2(to.y - c.y, to.x - c.x);
        // Sweep direction in document space is the reverse of DXF's.
        if (bulge > 0) { while (a2 > a1) a2 -= 2 * kPi; }
        else           { while (a2 < a1) a2 += 2 * kPi; }
        AppendArc(path, c, Point2Dd(r, 0), Point2Dd(0, r), a1, a2, false);
    }

    // de Boor evaluation for a clamped B-spline (any degree).
    static Point2Dd DeBoor(int degree, const std::vector<double>& knots,
                           const std::vector<Point2Dd>& ctrl, double t) {
        int n = static_cast<int>(ctrl.size()) - 1;
        int k = degree;
        // Find the knot span.
        int s = k;
        while (s < n && !(t < knots[s + 1])) ++s;
        std::vector<Point2Dd> d(ctrl.begin() + (s - k), ctrl.begin() + s + 1);
        for (int r = 1; r <= k; ++r) {
            for (int j = k; j >= r; --j) {
                double den = knots[j + 1 + s - r] - knots[j + s - k];
                double alpha = den > 1e-12 ? (t - knots[j + s - k]) / den : 0.0;
                d[j].x = (1 - alpha) * d[j - 1].x + alpha * d[j].x;
                d[j].y = (1 - alpha) * d[j - 1].y + alpha * d[j].y;
            }
        }
        return d[k];
    }

    // True when the knot vector is the clamped piecewise-bezier form the
    // writer emits: [a x4, b x3, c x3, ..., z x4] for degree 3.
    static bool IsPiecewiseBezier(int degree, const std::vector<double>& knots,
                                  size_t ctrlCount) {
        if (degree != 3) return false;
        if (ctrlCount < 4 || (ctrlCount - 1) % 3 != 0) return false;
        size_t spans = (ctrlCount - 1) / 3;
        if (knots.size() != 3 * spans + 5) return false;
        auto near = [](double a, double b) { return std::fabs(a - b) < 1e-9; };
        for (int k = 0; k < 4; ++k) {
            if (!near(knots[k], knots[0])) return false;
            if (!near(knots[knots.size() - 1 - k], knots.back())) return false;
        }
        for (size_t s = 1; s < spans; ++s) {
            for (int k = 0; k < 3; ++k) {
                if (!near(knots[4 + (s - 1) * 3 + k], knots[4 + (s - 1) * 3])) {
                    return false;
                }
            }
        }
        return true;
    }

    // Spline (already transformed control points) appended to a path.
    void AppendSpline(VectorPath& path, int degree, std::vector<double> knots,
                      const std::vector<Point2Dd>& ctrl, bool moveFirst) {
        if (ctrl.size() < 2) return;
        if (IsPiecewiseBezier(degree, knots, ctrl.size())) {
            if (moveFirst) {
                path.MoveTo(static_cast<float>(ctrl[0].x),
                            static_cast<float>(ctrl[0].y));
            }
            for (size_t i = 0; i + 3 < ctrl.size(); i += 3) {
                path.CurveTo(static_cast<float>(ctrl[i + 1].x),
                             static_cast<float>(ctrl[i + 1].y),
                             static_cast<float>(ctrl[i + 2].x),
                             static_cast<float>(ctrl[i + 2].y),
                             static_cast<float>(ctrl[i + 3].x),
                             static_cast<float>(ctrl[i + 3].y));
            }
            return;
        }
        // General NURBS: sample with de Boor.
        if (!warnedSplineApprox) {
            warnedSplineApprox = true;
            warn("DXF import: general NURBS splines are sampled "
                 "(only piecewise-bezier knot vectors convert exactly)");
        }
        if (knots.size() < ctrl.size() + degree + 1) {
            // Malformed: fall back to the control polygon.
            if (moveFirst) {
                path.MoveTo(static_cast<float>(ctrl[0].x),
                            static_cast<float>(ctrl[0].y));
            }
            for (size_t i = 1; i < ctrl.size(); ++i) {
                path.LineTo(static_cast<float>(ctrl[i].x),
                            static_cast<float>(ctrl[i].y));
            }
            return;
        }
        double t0 = knots[degree], t1 = knots[ctrl.size()];
        int samples = std::max<int>(32, static_cast<int>(ctrl.size()) * 8);
        Point2Dd first = DeBoor(degree, knots, ctrl, t0);
        if (moveFirst) {
            path.MoveTo(static_cast<float>(first.x), static_cast<float>(first.y));
        }
        for (int i = 1; i <= samples; ++i) {
            double t = t0 + (t1 - t0) * i / samples;
            if (i == samples) t = t1 - 1e-9;   // stay inside the last span
            Point2Dd p = DeBoor(degree, knots, ctrl, t);
            path.LineTo(static_cast<float>(p.x), static_cast<float>(p.y));
        }
    }

    // ===== ENTITIES =====

    void ParseEntities() {
        size_t i = entStart;
        while (i < entEnd) {
            if (tags[i].code != 0) { ++i; continue; }
            std::string type = tags[i].value;
            size_t recEnd = i + 1;
            while (recEnd < entEnd && tags[recEnd].code != 0) ++recEnd;

            // Legacy POLYLINE owns its VERTEX/SEQEND records.
            if (type == "POLYLINE") {
                size_t seqEnd = recEnd;
                while (seqEnd < entEnd &&
                       !(tags[seqEnd].code == 0 && tags[seqEnd].value == "SEQEND")) {
                    ++seqEnd;
                }
                size_t seqRecEnd = seqEnd;
                while (seqRecEnd < entEnd &&
                       (seqRecEnd == seqEnd || tags[seqRecEnd].code != 0)) {
                    ++seqRecEnd;
                }
                ParsePolylineChain(i + 1, seqEnd);
                i = seqRecEnd;
                continue;
            }

            std::vector<Tag> e(tags.begin() + i + 1, tags.begin() + recEnd);
            if (type == "LINE") ParseLine(e);
            else if (type == "CIRCLE") ParseCircle(e);
            else if (type == "ARC") ParseArc(e);
            else if (type == "ELLIPSE") ParseEllipse(e);
            else if (type == "LWPOLYLINE") ParseLwPolyline(e);
            else if (type == "SPLINE") ParseSpline(e);
            else if (type == "HATCH") ParseHatch(e);
            else if (type == "SOLID") ParseSolid(e);
            else if (type == "TEXT") ParseText(e);
            else if (type == "MTEXT") ParseMText(e);
            else if (type != "SEQEND" && type != "VERTEX") ++skipped[type];
            i = recEnd;
        }
    }

    const LayerDef& LayerOf(const std::vector<Tag>& e, std::string& nameOut) {
        static const LayerDef fallback;
        nameOut = "0";
        for (const Tag& t : e) {
            if (t.code == 8) { nameOut = t.value; break; }
        }
        auto it = tables.layers.find(nameOut);
        return it != tables.layers.end() ? it->second : fallback;
    }

    void Add(const std::vector<Tag>& e, std::shared_ptr<VectorElement> el) {
        std::string layerName;
        LayerOf(e, layerName);
        GetLayer(layerName)->AddChild(std::move(el));
    }

    static double F(const std::vector<Tag>& e, int code, double def = 0) {
        for (const Tag& t : e) {
            if (t.code == code) return t.D();
        }
        return def;
    }
    static int FI(const std::vector<Tag>& e, int code, int def = 0) {
        for (const Tag& t : e) {
            if (t.code == code) return t.I();
        }
        return def;
    }
    static const std::string* FS(const std::vector<Tag>& e, int code) {
        for (const Tag& t : e) {
            if (t.code == code) return &t.value;
        }
        return nullptr;
    }

    void ParseLine(const std::vector<Tag>& e) {
        std::string layerName;
        const LayerDef& layer = LayerOf(e, layerName);
        auto line = std::make_shared<VectorLine>();
        line->Start = P(F(e, 10), F(e, 20));
        line->End = P(F(e, 11), F(e, 21));
        line->Style.Stroke = MakeStroke(e, layer);
        Add(e, line);
    }

    void ParseCircle(const std::vector<Tag>& e) {
        std::string layerName;
        const LayerDef& layer = LayerOf(e, layerName);
        auto circle = std::make_shared<VectorCircle>();
        circle->Center = P(F(e, 10), F(e, 20));
        circle->Radius = static_cast<float>(F(e, 40));
        circle->Style.Stroke = MakeStroke(e, layer);
        Add(e, circle);
    }

    void ParseArc(const std::vector<Tag>& e) {
        std::string layerName;
        const LayerDef& layer = LayerOf(e, layerName);
        Point2Dd c = P(F(e, 10), F(e, 20));
        double r = F(e, 40);
        double a1 = F(e, 50) * kPi / 180.0;
        double a2 = F(e, 51) * kPi / 180.0;
        while (a2 <= a1) a2 += 2 * kPi;   // DXF arcs run CCW from 50 to 51
        auto path = std::make_shared<VectorPath>();
        // Y-flip mirrors the sweep: use V = (0,-r) so angles keep meaning.
        AppendArc(*path, c, Point2Dd(r, 0), Point2Dd(0, -r), a1, a2, true);
        path->Style.Stroke = MakeStroke(e, layer);
        Add(e, path);
    }

    void ParseEllipse(const std::vector<Tag>& e) {
        std::string layerName;
        const LayerDef& layer = LayerOf(e, layerName);
        Point2Dd c = P(F(e, 10), F(e, 20));
        // Major-axis endpoint is relative to the centre; flip its Y.
        Point2Dd u(F(e, 11), -F(e, 21));
        double ratio = F(e, 40, 1.0);
        double t1 = F(e, 41, 0.0);
        double t2 = F(e, 42, 2 * kPi);
        // The minor axis is ratio * perp(major); the Y-flip mirrors it so
        // the parameter range keeps its meaning in document space.
        Point2Dd v(u.y * ratio, -u.x * ratio);
        auto path = std::make_shared<VectorPath>();
        AppendArc(*path, c, u, v, t1, t2, true);
        bool full = std::fabs((t2 - t1) - 2 * kPi) < 1e-9;
        if (full) path->ClosePath();
        path->Style.Stroke = MakeStroke(e, layer);
        Add(e, path);
    }

    struct PolyVertex { double x, y, bulge; };

    void EmitPolyline(const std::vector<Tag>& e, std::vector<PolyVertex> verts,
                      bool closed) {
        // A closed polyline whose last vertex repeats the first carries a
        // redundant point; the closed flag already draws that segment.
        if (closed && verts.size() > 2 &&
            std::fabs(verts.front().x - verts.back().x) < 1e-9 &&
            std::fabs(verts.front().y - verts.back().y) < 1e-9 &&
            std::fabs(verts.back().bulge) < 1e-12) {
            verts.pop_back();
        }
        if (verts.size() < 2) return;
        std::string layerName;
        const LayerDef& layer = LayerOf(e, layerName);
        bool hasBulge = false;
        for (const auto& v : verts) hasBulge |= std::fabs(v.bulge) > 1e-12;

        if (!hasBulge) {
            if (closed) {
                auto poly = std::make_shared<VectorPolygon>();
                for (const auto& v : verts) poly->Points.push_back(P(v.x, v.y));
                poly->Style.Stroke = MakeStroke(e, layer);
                Add(e, poly);
            } else {
                auto poly = std::make_shared<VectorPolyline>();
                for (const auto& v : verts) poly->Points.push_back(P(v.x, v.y));
                poly->Style.Stroke = MakeStroke(e, layer);
                Add(e, poly);
            }
            return;
        }
        auto path = std::make_shared<VectorPath>();
        Point2Dd first = P(verts[0].x, verts[0].y);
        path->MoveTo(static_cast<float>(first.x), static_cast<float>(first.y));
        for (size_t i = 0; i + 1 < verts.size(); ++i) {
            AppendBulge(*path, P(verts[i].x, verts[i].y),
                        P(verts[i + 1].x, verts[i + 1].y), verts[i].bulge);
        }
        if (closed) {
            AppendBulge(*path, P(verts.back().x, verts.back().y), first,
                        verts.back().bulge);
            path->ClosePath();
        }
        path->Style.Stroke = MakeStroke(e, layer);
        Add(e, path);
    }

    void ParseLwPolyline(const std::vector<Tag>& e) {
        std::vector<PolyVertex> verts;
        bool haveX = false;
        double x = 0;
        for (const Tag& t : e) {
            if (t.code == 10) {
                x = t.D();
                haveX = true;
            } else if (t.code == 20 && haveX) {
                verts.push_back({x, t.D(), 0.0});
                haveX = false;
            } else if (t.code == 42 && !verts.empty()) {
                verts.back().bulge = t.D();
            }
        }
        EmitPolyline(e, verts, (FI(e, 70) & 1) != 0);
    }

    void ParsePolylineChain(size_t start, size_t seqEnd) {
        std::vector<Tag> header;
        size_t i = start;
        while (i < seqEnd && tags[i].code != 0) {
            header.push_back(tags[i]);
            ++i;
        }
        int flags = FI(header, 70);
        if (flags & 0x58) {   // 3D mesh variants
            ++skipped["POLYLINE(3D/mesh)"];
            return;
        }
        std::vector<PolyVertex> verts;
        while (i < seqEnd) {
            if (tags[i].code == 0 && tags[i].value == "VERTEX") {
                size_t recEnd = i + 1;
                while (recEnd < seqEnd && tags[recEnd].code != 0) ++recEnd;
                std::vector<Tag> v(tags.begin() + i + 1, tags.begin() + recEnd);
                verts.push_back({F(v, 10), F(v, 20), F(v, 42)});
                i = recEnd;
            } else {
                ++i;
            }
        }
        EmitPolyline(header, verts, (flags & 1) != 0);
    }

    void ParseSpline(const std::vector<Tag>& e) {
        std::string layerName;
        const LayerDef& layer = LayerOf(e, layerName);
        int flags = FI(e, 70);
        int degree = FI(e, 71, 3);
        std::vector<double> knots;
        std::vector<Point2Dd> ctrl;
        std::vector<Point2Dd> fit;
        bool rational = false;
        bool haveX = false;
        double x = 0;
        for (const Tag& t : e) {
            if (t.code == 40) knots.push_back(t.D());
            else if (t.code == 41 && std::fabs(t.D() - 1.0) > 1e-9) rational = true;
            else if (t.code == 10) { x = t.D(); haveX = true; }
            else if (t.code == 20 && haveX) { ctrl.push_back(P(x, t.D())); haveX = false; }
            else if (t.code == 11) { x = t.D(); haveX = true; }
            else if (t.code == 21 && haveX) { fit.push_back(P(x, t.D())); haveX = false; }
        }
        if (rational) {
            warn("DXF import: rational spline weights are ignored");
        }
        auto path = std::make_shared<VectorPath>();
        if (!ctrl.empty()) {
            AppendSpline(*path, degree, knots, ctrl, true);
        } else if (fit.size() >= 2) {
            warn("DXF import: spline with fit points only, connecting linearly");
            path->MoveTo(static_cast<float>(fit[0].x), static_cast<float>(fit[0].y));
            for (size_t i = 1; i < fit.size(); ++i) {
                path->LineTo(static_cast<float>(fit[i].x),
                             static_cast<float>(fit[i].y));
            }
        } else {
            return;
        }
        if (flags & 1) path->ClosePath();
        path->Style.Stroke = MakeStroke(e, layer);
        Add(e, path);
    }

    void ParseSolid(const std::vector<Tag>& e) {
        std::string layerName;
        const LayerDef& layer = LayerOf(e, layerName);
        // SOLID corner order is 1,2,4,3.
        Point2Dd p1 = P(F(e, 10), F(e, 20)), p2 = P(F(e, 11), F(e, 21));
        Point2Dd p3 = P(F(e, 12), F(e, 22)), p4 = P(F(e, 13), F(e, 23));
        auto poly = std::make_shared<VectorPolygon>();
        poly->Points = {p1, p2, p4, p3};
        poly->Style.Fill = EntityColor(e, layer);
        Add(e, poly);
    }

    void ParseHatch(const std::vector<Tag>& e) {
        std::string layerName;
        const LayerDef& layer = LayerOf(e, layerName);
        if (FI(e, 70) == 0 && !warnedHatchPattern) {
            warnedHatchPattern = true;
            warn("DXF import: pattern hatches fill solid with the entity colour");
        }
        auto path = std::make_shared<VectorPath>();
        size_t i = 0;
        auto next = [&](int code) -> const Tag* {
            while (i < e.size() && e[i].code != code) ++i;
            return i < e.size() ? &e[i++] : nullptr;
        };
        const Tag* npaths = next(91);
        int pathCount = npaths ? npaths->I() : 0;
        bool emitted = false;
        for (int p = 0; p < pathCount; ++p) {
            const Tag* flagsTag = next(92);
            if (!flagsTag) break;
            int pflags = flagsTag->I();
            if (pflags & 2) {
                // Polyline boundary: 72 has-bulge, 73 closed, 93 count.
                // The flags sit between this 92 and the 93 vertex count;
                // bound the scan so a missing flag never reads past it.
                int hasBulge = 0, closed = 1, count = 0;
                for (size_t j = i; j < e.size() && e[j].code != 93; ++j) {
                    if (e[j].code == 72) hasBulge = e[j].I();
                    else if (e[j].code == 73) closed = e[j].I();
                }
                if (const Tag* t = next(93)) count = t->I();
                std::vector<PolyVertex> verts;
                for (int vtx = 0; vtx < count; ++vtx) {
                    double vx = 0, vy = 0, b = 0;
                    if (const Tag* t = next(10)) vx = t->D();
                    if (i < e.size() && e[i].code == 20) vy = e[i++].D();
                    if (hasBulge && i < e.size() && e[i].code == 42) b = e[i++].D();
                    verts.push_back({vx, vy, b});
                }
                if (verts.size() >= 2) {
                    Point2Dd first = P(verts[0].x, verts[0].y);
                    path->MoveTo(static_cast<float>(first.x),
                                 static_cast<float>(first.y));
                    for (size_t vtx = 0; vtx + 1 < verts.size(); ++vtx) {
                        AppendBulge(*path, P(verts[vtx].x, verts[vtx].y),
                                    P(verts[vtx + 1].x, verts[vtx + 1].y),
                                    verts[vtx].bulge);
                    }
                    if (closed) {
                        AppendBulge(*path, P(verts.back().x, verts.back().y),
                                    first, verts.back().bulge);
                        path->ClosePath();
                    }
                    emitted = true;
                }
                continue;
            }
            const Tag* nedges = next(93);
            int edgeCount = nedges ? nedges->I() : 0;
            bool started = false;
            for (int edge = 0; edge < edgeCount; ++edge) {
                const Tag* typeTag = next(72);
                if (!typeTag) break;
                int etype = typeTag->I();
                if (etype == 1) {   // line
                    double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
                    if (const Tag* t = next(10)) x1 = t->D();
                    if (i < e.size() && e[i].code == 20) y1 = e[i++].D();
                    if (const Tag* t = next(11)) x2 = t->D();
                    if (i < e.size() && e[i].code == 21) y2 = e[i++].D();
                    Point2Dd a = P(x1, y1), b = P(x2, y2);
                    if (!started) {
                        path->MoveTo(static_cast<float>(a.x), static_cast<float>(a.y));
                        started = true;
                    }
                    path->LineTo(static_cast<float>(b.x), static_cast<float>(b.y));
                } else if (etype == 2) {   // circular arc
                    double cx = 0, cy = 0, r = 0, a1 = 0, a2 = 360;
                    int ccw = 1;
                    if (const Tag* t = next(10)) cx = t->D();
                    if (i < e.size() && e[i].code == 20) cy = e[i++].D();
                    if (const Tag* t = next(40)) r = t->D();
                    if (const Tag* t = next(50)) a1 = t->D();
                    if (const Tag* t = next(51)) a2 = t->D();
                    if (const Tag* t = next(73)) ccw = t->I();
                    double t1 = a1 * kPi / 180.0, t2 = a2 * kPi / 180.0;
                    if (ccw) { while (t2 <= t1) t2 += 2 * kPi; }
                    else     { while (t2 >= t1) t2 -= 2 * kPi; }
                    AppendArc(*path, P(cx, cy), Point2Dd(r, 0), Point2Dd(0, -r),
                              t1, t2, !started);
                    started = true;
                } else if (etype == 3) {   // elliptical arc
                    double cx = 0, cy = 0, mx = 0, my = 0, ratio = 1, a1 = 0,
                           a2 = 2 * kPi;
                    int ccw = 1;
                    if (const Tag* t = next(10)) cx = t->D();
                    if (i < e.size() && e[i].code == 20) cy = e[i++].D();
                    if (const Tag* t = next(11)) mx = t->D();
                    if (i < e.size() && e[i].code == 21) my = e[i++].D();
                    if (const Tag* t = next(40)) ratio = t->D();
                    if (const Tag* t = next(50)) a1 = t->D() * kPi / 180.0;
                    if (const Tag* t = next(51)) a2 = t->D() * kPi / 180.0;
                    if (const Tag* t = next(73)) ccw = t->I();
                    if (ccw) { while (a2 <= a1) a2 += 2 * kPi; }
                    else     { while (a2 >= a1) a2 -= 2 * kPi; }
                    Point2Dd u(mx, -my);
                    Point2Dd v(u.y * ratio, -u.x * ratio);
                    AppendArc(*path, P(cx, cy), u, v, a1, a2, !started);
                    started = true;
                } else if (etype == 4) {   // spline edge
                    int degree = 3, nk = 0, nc = 0;
                    if (const Tag* t = next(94)) degree = t->I();
                    if (const Tag* t = next(95)) nk = t->I();
                    if (const Tag* t = next(96)) nc = t->I();
                    std::vector<double> knots;
                    std::vector<Point2Dd> ctrl;
                    for (int k = 0; k < nk; ++k) {
                        if (const Tag* t = next(40)) knots.push_back(t->D());
                    }
                    for (int cpt = 0; cpt < nc; ++cpt) {
                        double vx = 0, vy = 0;
                        if (const Tag* t = next(10)) vx = t->D();
                        if (i < e.size() && e[i].code == 20) vy = e[i++].D();
                        if (i < e.size() && e[i].code == 42) ++i;   // weight
                        ctrl.push_back(P(vx, vy));
                    }
                    if (!ctrl.empty()) {
                        AppendSpline(*path, degree, knots, ctrl, !started);
                        started = true;
                    }
                } else {
                    ++skipped["HATCH edge type " + std::to_string(etype)];
                }
            }
            if (started) {
                path->ClosePath();
                emitted = true;
            }
        }
        if (!emitted) return;
        path->Style.Fill = EntityColor(e, layer);
        Add(e, path);
    }

    void ParseText(const std::vector<Tag>& e) {
        std::string layerName;
        const LayerDef& layer = LayerOf(e, layerName);
        const std::string* value = FS(e, 1);
        if (!value || value->empty()) return;
        auto text = std::make_shared<VectorText>();
        int halign = FI(e, 72);
        // With a non-default alignment the second alignment point anchors.
        double ax = (halign != 0) ? F(e, 11, F(e, 10)) : F(e, 10);
        double ay = (halign != 0) ? F(e, 21, F(e, 20)) : F(e, 20);
        text->Position = P(ax, ay);
        text->BaseStyle.FontSize = static_cast<float>(F(e, 40, 12.0));
        if (halign == 1 || halign == 4) text->BaseStyle.Anchor = TextAnchor::Middle;
        else if (halign == 2) text->BaseStyle.Anchor = TextAnchor::End;
        if (const std::string* styleName = FS(e, 7)) {
            auto it = tables.textStyles.find(*styleName);
            if (it != tables.textStyles.end()) {
                text->BaseStyle.FontFamily = it->second;
            }
        }
        double rotation = F(e, 50);
        if (std::fabs(rotation) > 1e-9) {
            // DXF rotates CCW in Y-up; document space rotates the other way.
            text->Transform =
                    Matrix3x3::Translate(text->Position.x, text->Position.y) *
                    Matrix3x3::RotateDegrees(-rotation) *
                    Matrix3x3::Translate(-text->Position.x, -text->Position.y);
        }
        text->SetText(*value);
        text->Style.Fill = EntityColor(e, layer);
        Add(e, text);
    }

    void ParseMText(const std::vector<Tag>& e) {
        std::string layerName;
        const LayerDef& layer = LayerOf(e, layerName);
        std::string raw;
        for (const Tag& t : e) {
            if (t.code == 3) raw += t.value;   // continuation chunks first
        }
        if (const std::string* s = FS(e, 1)) raw += *s;
        if (raw.empty()) return;

        // Strip inline formatting: \P newline, \~ space, {} groups,
        // \X...; commands, ^I tab.
        std::string plain;
        for (size_t i = 0; i < raw.size(); ++i) {
            char ch = raw[i];
            if (ch == '{' || ch == '}') continue;
            if (ch == '\\' && i + 1 < raw.size()) {
                char cmd = raw[++i];
                if (cmd == 'P') plain.push_back('\n');
                else if (cmd == '~') plain.push_back(' ');
                else if (cmd == '\\' || cmd == '{' || cmd == '}') plain.push_back(cmd);
                else if (cmd == 'f' || cmd == 'F' || cmd == 'H' || cmd == 'C' ||
                         cmd == 'T' || cmd == 'Q' || cmd == 'W' || cmd == 'A' ||
                         cmd == 'p') {
                    while (i + 1 < raw.size() && raw[i] != ';') ++i;
                }
                // \L,\l,\O,\o,\K,\k and unknown single-char codes drop.
                continue;
            }
            plain.push_back(ch);
        }
        if (plain.empty()) return;

        double size = F(e, 40, 12.0);
        auto text = std::make_shared<VectorText>();
        int attach = FI(e, 71, 1);   // 1..9, TL TC TR ML MC MR BL BC BM
        int col = (attach - 1) % 3;
        if (col == 1) text->BaseStyle.Anchor = TextAnchor::Middle;
        else if (col == 2) text->BaseStyle.Anchor = TextAnchor::End;
        // The insertion point is the box corner; the first baseline sits
        // roughly one line below the top rows.
        Point2Dd pos = P(F(e, 10), F(e, 20));
        int row = (attach - 1) / 3;
        if (row == 0) pos.y += size;
        text->Position = pos;
        text->BaseStyle.FontSize = static_cast<float>(size);
        if (const std::string* styleName = FS(e, 7)) {
            auto it = tables.textStyles.find(*styleName);
            if (it != tables.textStyles.end()) {
                text->BaseStyle.FontFamily = it->second;
            }
        }
        text->SetText(plain);
        text->Style.Fill = EntityColor(e, layer);
        Add(e, text);
    }
};

}   // anonymous namespace

std::shared_ptr<VectorStorage::VectorDocument> DXFConverter::ImportFromString(
        const std::string& data, const ConversionOptions& options) {
    if (!ValidateData(data)) {
        if (options.WarningCallback) {
            options.WarningCallback("Not a DXF file (no SECTION structure)");
        }
        return nullptr;
    }
    DxfReader reader(data, [&options](const std::string& msg) {
        if (options.WarningCallback) options.WarningCallback(msg);
    });
    auto doc = reader.Parse();
    if (options.ProgressCallback) options.ProgressCallback(1.0f);
    return doc;
}

} // namespace VectorConverter
} // namespace UltraCanvas
