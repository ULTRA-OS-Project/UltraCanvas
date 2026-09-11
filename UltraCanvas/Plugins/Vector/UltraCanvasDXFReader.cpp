// UltraCanvas/Plugins/Vector/UltraCanvasDXFReader.cpp
// DXF (AutoCAD Drawing Exchange Format) reader - the import side of
// DXFConverter (the writer lives in UltraCanvasDXFConverter.cpp). The
// native DWG decoder renders drawings as DXF, so this reader is the import
// path of both CAD formats.
//
// Parses tagged ASCII DXF per Autodesk's public reference: the HEADER
// extents, the LAYER/LTYPE/STYLE tables, the BLOCKS section and the
// ENTITIES section with LINE, CIRCLE, ARC, ELLIPSE, LWPOLYLINE (bulges
// included), legacy POLYLINE/VERTEX (2D, 3D, polyface and polygon meshes
// projected onto the XY plane), SPLINE, HATCH, SOLID,
// 3DFACE, LEADER, TEXT, ATTRIB, MTEXT, INSERT (nested, scaled, rotated,
// mirrored through the OCS extrusion, MINSERT arrays, "0"-layer and
// ByBlock inheritance) and DIMENSION (through its rendered block). Curved
// geometry comes back as real cubics: arcs and bulges convert exactly (to
// within the standard bezier arc approximation), and splines whose knot
// vector is the piecewise-bezier form (the writer's own output, clamped
// with interior multiplicity = degree) reproduce their cubics exactly;
// general NURBS are sampled with de Boor evaluation and reported through
// the warning callback. Entities with an object coordinate system other
// than the world's (the 210 extrusion, typically a mirrored block) are
// wrapped in a transformed group. DXF's Y-up world maps to the document's
// Y-down page using the drawing's real extents - computed from the built
// geometry, block content included, and reconciled with $EXTMIN/$EXTMAX
// when a file declares them; a page derived from the extents is scaled to
// a sensible point size, since drawing units are arbitrary (metres or
// millimetres). Entities keep their real layers; layers that
// are off or frozen, invisible entities and paper-space entities (when the
// model space has content) are not imported; unsupported entity types are
// counted and reported, never dropped silently.
// Version: 1.1.0
// Last Modified: 2026-09-08
// Author: UltraCanvas Framework

#include "UltraCanvasCADConverters.h"
#include "UltraCanvasVectorStorage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace UltraCanvas {
namespace VectorConverter {

using namespace VectorStorage;

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
    bool visible = true;   // off (negative colour) or frozen layers hide
    bool frozen = false;
    bool locked = false;
    bool plottable = true;
};

// $INSUNITS codes (DXF reference, HEADER section).
LengthUnit UnitFromInsunits(int code) {
    switch (code) {
        case 1:  return LengthUnit::Inch;
        case 2:  return LengthUnit::Foot;
        case 3:  return LengthUnit::Mile;
        case 4:  return LengthUnit::Millimeter;
        case 5:  return LengthUnit::Centimeter;
        case 6:  return LengthUnit::Meter;
        case 7:  return LengthUnit::Kilometer;
        case 9:  return LengthUnit::Mil;
        case 10: return LengthUnit::Yard;
        case 12: return LengthUnit::Nanometer;
        case 13: return LengthUnit::Micrometer;
        case 14: return LengthUnit::Decimeter;
        default: return LengthUnit::Unspecified;   // 0 unitless, exotic units
    }
}

struct TableData {
    std::map<std::string, LayerDef> layers;
    std::vector<std::string> layerOrder;
    std::map<std::string, std::vector<double>> linetypes;   // name -> dashes (on/off)
    std::map<std::string, std::string> textStyles;          // style name -> family
};

struct BlockDef {
    double baseX = 0, baseY = 0;
    size_t start = 0, end = 0;   // tag range of the block's entities
};

// Double-precision affine map, x' = a x + c y + e, y' = b x + d y + f
// (the SVG matrix layout). Matrix3x3 is float; composing insert
// transforms in double keeps large drawing coordinates exact.
struct Affine {
    double a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;
    static Affine Translate(double tx, double ty) { Affine m; m.e = tx; m.f = ty; return m; }
    static Affine Scale(double sx, double sy) { Affine m; m.a = sx; m.d = sy; return m; }
    static Affine Rotate(double rad) {
        Affine m;
        m.a = std::cos(rad); m.b = std::sin(rad);
        m.c = -m.b; m.d = m.a;
        return m;
    }
    // this * o : apply o first, then this.
    Affine operator*(const Affine& o) const {
        Affine r;
        r.a = a * o.a + c * o.b;
        r.b = b * o.a + d * o.b;
        r.c = a * o.c + c * o.d;
        r.d = b * o.c + d * o.d;
        r.e = a * o.e + c * o.f + e;
        r.f = b * o.e + d * o.f + f;
        return r;
    }
    Affine Inverse() const {
        double det = a * d - b * c;
        if (std::fabs(det) < 1e-300) return Affine();
        Affine r;
        r.a = d / det; r.b = -b / det; r.c = -c / det; r.d = a / det;
        r.e = -(r.a * e + r.c * f);
        r.f = -(r.b * e + r.d * f);
        return r;
    }
    Point2Dd Apply(double x, double y) const {
        return Point2Dd(a * x + c * y + e, b * x + d * y + f);
    }
    bool IsIdentity() const {
        return std::fabs(a - 1) < 1e-12 && std::fabs(d - 1) < 1e-12 &&
               std::fabs(b) < 1e-12 && std::fabs(c) < 1e-12 &&
               std::fabs(e) < 1e-9 && std::fabs(f) < 1e-9;
    }
    // Matrix3x3::FromValues is row-major (x' = A x + B y + e), so b and c
    // swap places.
    Matrix3x3 ToMatrix() const {
        return Matrix3x3::FromValues(static_cast<float>(a), static_cast<float>(c),
                                     static_cast<float>(b), static_cast<float>(d),
                                     static_cast<float>(e), static_cast<float>(f));
    }
    static Affine FromMatrix(const Matrix3x3& m) {
        Affine r;
        r.a = m.m[0][0]; r.c = m.m[0][1]; r.e = m.m[0][2];
        r.b = m.m[1][0]; r.d = m.m[1][1]; r.f = m.m[1][2];
        return r;
    }
};

// The Arbitrary Axis Algorithm: the 2D projection of an entity's object
// coordinate system for an extrusion (normal) vector. Identity for the
// usual (0,0,1); a mirror for (0,0,-1); a general 2x2 for tilted planes.
Affine OcsProjection(double nx, double ny, double nz) {
    double len = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (len < 1e-12) return Affine();
    nx /= len; ny /= len; nz /= len;
    if (std::fabs(nx) < 1e-9 && std::fabs(ny) < 1e-9 && nz > 0) return Affine();
    double ax, ay, az;
    if (std::fabs(nx) < 1.0 / 64 && std::fabs(ny) < 1.0 / 64) {
        // Ax = Wy x N
        ax = 1 * nz - 0 * ny; ay = 0 * nx - 0 * nz; az = 0 * ny - 1 * nx;
    } else {
        // Ax = Wz x N
        ax = 0 * nz - 1 * ny; ay = 1 * nx - 0 * nz; az = 0 * ny - 0 * nx;
    }
    double al = std::sqrt(ax * ax + ay * ay + az * az);
    if (al < 1e-12) return Affine();
    ax /= al; ay /= al; az /= al;
    // Ay = N x Ax
    double bx = ny * az - nz * ay, by = nz * ax - nx * az;
    Affine m;
    m.a = ax; m.b = ay;   // column for x
    m.c = bx; m.d = by;   // column for y
    return m;
}

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
        ScanSpaces();

        // Pass 1: build with a plain Y flip to measure the drawing's real
        // extents (block content, arcs and text included).
        quiet = true;
        extMinX = extMinY = 0;
        pageH = unitsH = 0;
        unitScale = 1.0;
        auto probe = Build();
        double bx0, by0, bx1, by1;
        bool haveBounds = probe && DocumentBounds(*probe, bx0, by0, bx1, by1);
        if (haveBounds) {
            // Raw y = -docY under the flip-only mapping.
            double gx0 = bx0, gx1 = bx1, gy0 = -by1, gy1 = -by0;
            double margin = std::max(gx1 - gx0, gy1 - gy0) * 0.01;
            if (margin < 1e-9) margin = 1;
            gx0 -= margin; gy0 -= margin; gx1 += margin; gy1 += margin;
            bool declaredContains = hasExtents &&
                    extMinX <= gx0 + margin && extMinY <= gy0 + margin &&
                    extMaxX >= gx1 - margin && extMaxY >= gy1 - margin &&
                    (extMaxX - extMinX) < 4 * (gx1 - gx0) + 1 &&
                    (extMaxY - extMinY) < 4 * (gy1 - gy0) + 1;
            declaredPage = declaredContains;
            if (!declaredContains) {
                extMinX = gx0; extMinY = gy0; extMaxX = gx1; extMaxY = gy1;
                hasExtents = true;
            }
        } else if (!hasExtents) {
            ComputeExtentsFromTags();
        } else {
            declaredPage = true;
        }
        pageW = extMaxX - extMinX;
        pageH = extMaxY - extMinY;
        if (!(pageW > 0) || !(pageH > 0)) { pageW = 595; pageH = 842; }

        // A file that declares its unit ($INSUNITS) gets its physical size
        // in points, as long as that lands on a page a viewer can use (an
        // A4 plan in millimetres becomes 842 x 595 pt; a 100 km site plan
        // in millimetres would not). Otherwise drawing units are arbitrary
        // (a car in metres is 5 units wide, a house in millimetres 20000);
        // the page is in points, so a drawing whose page came from its own
        // extents is scaled to a sensible size - lineweights and default
        // strokes then read the same everywhere, and float transforms stay
        // precise. A declared page is the author's.
        unitScale = 1.0;
        double physical = PointsPerUnit(sourceUnit);
        double maxDim = std::max(pageW, pageH);
        if (physical > 0 && maxDim * physical >= 200 && maxDim * physical <= 20000) {
            unitScale = physical;
        } else if (!declaredPage) {
            if (maxDim < 1000) unitScale = 1000.0 / maxDim;
            else if (maxDim > 10000) unitScale = 10000.0 / maxDim;
        }
        unitsH = pageH;
        pageW *= unitScale;
        pageH *= unitScale;

        // Pass 2: the real document.
        quiet = false;
        skipped.clear();
        warnedSplineApprox = warnedHatchPattern = false;
        doc = Build();
        if (!doc) return nullptr;
        doc->Size = Size2Dd{pageW, pageH};
        doc->SourceUnit = sourceUnit;
        doc->PointsPerSourceUnit = unitScale;

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
    bool hasModelSpace = true;         // ENTITIES has entities without 67=1
    TableData tables;
    std::map<std::string, BlockDef> blocks;
    double extMinX = 0, extMinY = 0, extMaxX = 0, extMaxY = 0;
    bool hasExtents = false;
    double pageW = 0, pageH = 0;    // page in points (after unitScale)
    double unitsH = 0;              // page height in drawing units
    double unitScale = 1.0;         // drawing units -> points
    LengthUnit sourceUnit = LengthUnit::Unspecified;   // $INSUNITS
    bool declaredPage = false;      // the page is the file's $EXTMIN/$EXTMAX
    bool quiet = false;
    std::shared_ptr<VectorDocument> doc;
    std::map<std::string, std::shared_ptr<VectorLayer>> layerGroups;
    std::map<std::string, int> skipped;
    bool warnedSplineApprox = false, warnedHatchPattern = false;

    // Parsing context: where entities go and what they inherit.
    struct Ctx {
        std::shared_ptr<VectorGroup> parent;   // null: the entity's layer
        std::string layerOverride;             // insert's layer for "0" entities
        const Color* byBlock = nullptr;        // insert's colour for ByBlock
        int depth = 0;
        std::vector<std::string> blockStack;   // cycle guard
    };

    void Warn(const std::string& msg) { if (!quiet) warn(msg); }

    // Document coordinates: shift to the extents origin, flip Y, scale.
    Point2Dd P(double x, double y) const {
        return Point2Dd((x - extMinX) * unitScale, (unitsH - (y - extMinY)) * unitScale);
    }
    // A length in drawing units, in document units.
    double S(double v) const { return v * unitScale; }
    Affine PageMap() const {
        Affine m;
        m.a = unitScale; m.b = 0; m.c = 0; m.d = -unitScale;
        m.e = -extMinX * unitScale; m.f = (unitsH + extMinY) * unitScale;
        return m;
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
                else if (section == "BLOCKS") ParseBlocks(start, end);
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
            } else if (var == "$INSUNITS") {
                double code = 0;
                grab(70, code);
                sourceUnit = UnitFromInsunits(static_cast<int>(code));
            }
        }
        if (hasExtents && (extMaxX <= extMinX || extMaxY <= extMinY)) {
            hasExtents = false;   // declared but degenerate; recompute
        }
        // AutoCAD writes +/-1e20 when the extents are unknown.
        if (hasExtents && (std::fabs(extMinX) > 1e15 || std::fabs(extMaxX) > 1e15)) {
            hasExtents = false;
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
                    def.visible = aci >= 0;   // negative = layer off
                    def.color = AciPaletteColor(std::abs(aci));
                }
                if (const std::string* tc = field(420)) {
                    long rgb = std::atol(tc->c_str());
                    def.color = Color((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF,
                                      rgb & 0xFF, 255);
                }
                if (const std::string* fl = field(70)) {
                    int flags = std::atoi(fl->c_str());
                    if (flags & 1) { def.frozen = true; def.visible = false; }
                    if (flags & 4) def.locked = true;
                }
                if (const std::string* lt = field(6)) def.linetype = *lt;
                if (const std::string* lw = field(370)) {
                    def.lineweight = std::atoi(lw->c_str());
                }
                if (const std::string* pl = field(290)) {
                    def.plottable = std::atoi(pl->c_str()) != 0;
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

    void ParseBlocks(size_t start, size_t end) {
        size_t i = start;
        while (i < end) {
            if (!(tags[i].code == 0 && tags[i].value == "BLOCK")) { ++i; continue; }
            size_t hdrEnd = i + 1;
            while (hdrEnd < end && tags[hdrEnd].code != 0) ++hdrEnd;
            std::string name;
            BlockDef def;
            for (size_t j = i + 1; j < hdrEnd; ++j) {
                if (tags[j].code == 2 && name.empty()) name = tags[j].value;
                else if (tags[j].code == 10) def.baseX = tags[j].D();
                else if (tags[j].code == 20) def.baseY = tags[j].D();
            }
            size_t blkEnd = hdrEnd;
            while (blkEnd < end && !(tags[blkEnd].code == 0 && tags[blkEnd].value == "ENDBLK")) {
                ++blkEnd;
            }
            def.start = hdrEnd;
            def.end = blkEnd;
            if (!name.empty()) blocks[name] = def;
            i = blkEnd + 1;
        }
    }

    // Paper-space entities (67 = 1) are skipped when the model space has
    // content of its own.
    void ScanSpaces() {
        bool model = false, paper = false;
        for (size_t i = entStart; i < entEnd; ++i) {
            if (tags[i].code != 0) continue;
            size_t recEnd = i + 1;
            bool ps = false;
            while (recEnd < entEnd && tags[recEnd].code != 0) {
                if (tags[recEnd].code == 67 && tags[recEnd].I() == 1) ps = true;
                ++recEnd;
            }
            const std::string& t = tags[i].value;
            if (t != "SEQEND" && t != "VERTEX" && t != "ATTRIB" && t != "VIEWPORT") {
                if (ps) paper = true; else model = true;
            }
            i = recEnd - 1;
        }
        hasModelSpace = model || !paper;
    }

    // Fallback extents from the raw ENTITIES coordinates.
    void ComputeExtentsFromTags() {
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
            if (extMaxX - extMinX < 1) extMaxX = extMinX + 1;
            if (extMaxY - extMinY < 1) extMaxY = extMinY + 1;
        }
    }

    // ===== BUILD =====

    std::shared_ptr<VectorDocument> Build() {
        doc = std::make_shared<VectorDocument>();
        layerGroups.clear();
        for (const auto& name : tables.layerOrder) GetLayer(name);
        Ctx ctx;
        ParseEntityRange(entStart, entEnd, ctx, true);

        // Table-only layers that received no entities are noise in a
        // drawing document; keep only layers that hold content.
        doc->Layers.erase(
                std::remove_if(doc->Layers.begin(), doc->Layers.end(),
                               [](const std::shared_ptr<VectorLayer>& l) {
                                   return !l || l->Children.empty();
                               }),
                doc->Layers.end());
        return doc;
    }

    // Bounds of everything in the document, transforms applied.
    static void AccumulateBounds(const std::shared_ptr<VectorElement>& el, const Affine& acc,
                                 bool& any, double& x0, double& y0, double& x1, double& y1) {
        if (!el) return;
        if (auto group = std::dynamic_pointer_cast<VectorGroup>(el)) {
            Affine next = acc;
            if (group->Transform.has_value()) next = acc * Affine::FromMatrix(*group->Transform);
            for (const auto& child : group->Children) {
                AccumulateBounds(child, next, any, x0, y0, x1, y1);
            }
            return;
        }
        if (el->Type == VectorElementType::Text) {
            // The text box is a character-count estimate; only the anchor
            // is trusted for the extents.
            const auto* t = static_cast<const VectorText*>(el.get());
            Point2Dd p = acc.Apply(t->Position.x, t->Position.y);
            if (!std::isfinite(p.x) || !std::isfinite(p.y)) return;
            if (!any) { x0 = x1 = p.x; y0 = y1 = p.y; any = true; }
            x0 = std::min(x0, p.x); x1 = std::max(x1, p.x);
            y0 = std::min(y0, p.y); y1 = std::max(y1, p.y);
            return;
        }
        Rect2Dd box = el->GetBoundingBox();
        if (box.width <= 0 && box.height <= 0 && box.x == 0 && box.y == 0) return;
        Affine local = acc;
        if (el->Transform.has_value()) {
            local = acc * Affine::FromMatrix(*el->Transform);
        }
        const double xs[2] = {static_cast<double>(box.x), static_cast<double>(box.x + box.width)};
        const double ys[2] = {static_cast<double>(box.y), static_cast<double>(box.y + box.height)};
        for (double px : xs) {
            for (double py : ys) {
                Point2Dd p = local.Apply(px, py);
                if (!std::isfinite(p.x) || !std::isfinite(p.y)) continue;
                if (!any) { x0 = x1 = p.x; y0 = y1 = p.y; any = true; }
                x0 = std::min(x0, p.x); x1 = std::max(x1, p.x);
                y0 = std::min(y0, p.y); y1 = std::max(y1, p.y);
            }
        }
    }

    static bool DocumentBounds(const VectorDocument& d, double& x0, double& y0,
                               double& x1, double& y1) {
        bool any = false;
        for (const auto& layer : d.Layers) {
            AccumulateBounds(layer, Affine(), any, x0, y0, x1, y1);
        }
        return any && (x1 - x0 > 1e-9 || y1 - y0 > 1e-9);
    }

    std::shared_ptr<VectorLayer> GetLayer(const std::string& name) {
        auto it = layerGroups.find(name);
        if (it != layerGroups.end()) return it->second;
        auto layer = doc->AddLayer(name.empty() ? "0" : name);
        auto def = tables.layers.find(name);
        if (def != tables.layers.end()) {
            const LayerDef& d = def->second;
            layer->Visible = d.visible;
            layer->Frozen = d.frozen;
            layer->Locked = d.locked;
            layer->Plottable = d.plottable;
            layer->DefaultColor = d.color;
            layer->LineTypeName = d.linetype;
            layer->DefaultDashArray = LinetypeDashes(d.linetype);
            if (d.lineweight > 0) {
                layer->DefaultStrokeWidth =
                        static_cast<float>(d.lineweight / 100.0 * 72.0 / 25.4);
            } else if (d.lineweight == 0) {
                layer->DefaultStrokeWidth = 0.25f;
            }
        }
        layerGroups[name] = layer;
        return layer;
    }

    // A linetype's dash pattern in points; empty for Continuous/unknown.
    std::vector<double> LinetypeDashes(const std::string& lt) const {
        if (lt.empty() || lt == "Continuous" || lt == "CONTINUOUS") return {};
        auto it = tables.linetypes.find(lt);
        if (it == tables.linetypes.end() || it->second.empty()) return {};
        std::vector<double> dashes;
        for (double d : it->second) {
            dashes.push_back(d == 0 ? 0.5 : S(std::fabs(d)));   // dots become short dashes
        }
        return dashes;
    }

    // ===== STYLE RESOLUTION =====

    static const std::string* FS(const std::vector<Tag>& e, int code) {
        for (const Tag& t : e) {
            if (t.code == code) return &t.value;
        }
        return nullptr;
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

    // The layer an entity draws with: its own, or the insert's for "0"
    // entities inside a block.
    std::string EffectiveLayerName(const std::vector<Tag>& e, const Ctx& ctx) const {
        std::string name = "0";
        if (const std::string* l = FS(e, 8)) name = *l;
        if (!ctx.layerOverride.empty() && (name == "0" || name.empty())) name = ctx.layerOverride;
        return name;
    }

    const LayerDef& LayerOf(const std::vector<Tag>& e, const Ctx& ctx, std::string& nameOut) const {
        static const LayerDef fallback;
        nameOut = EffectiveLayerName(e, ctx);
        auto it = tables.layers.find(nameOut);
        return it != tables.layers.end() ? it->second : fallback;
    }

    Color EntityColor(const std::vector<Tag>& e, const LayerDef& layer, const Ctx& ctx) {
        for (const Tag& t : e) {
            if (t.code == 420) {
                long rgb = std::atol(t.value.c_str());
                return Color((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255);
            }
        }
        for (const Tag& t : e) {
            if (t.code == 62) {
                int aci = t.I();
                if (aci == 0) return ctx.byBlock ? *ctx.byBlock : layer.color;
                if (aci == 256) break;   // ByLayer
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
        return LinetypeDashes(lt);
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

    StrokeData MakeStroke(const std::vector<Tag>& e, const LayerDef& layer, const Ctx& ctx) {
        StrokeData stroke;
        stroke.Fill = EntityColor(e, layer, ctx);
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
            Warn("DXF import: general NURBS splines are sampled "
                 "(only piecewise-bezier knot vectors convert exactly)");
        }
        if (degree < 1 || knots.size() < ctrl.size() + degree + 1) {
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

    // Parses the entity records in [start, end) into the context. `top`
    // marks the ENTITIES section (paper-space filtering applies there).
    void ParseEntityRange(size_t start, size_t end, Ctx& ctx, bool top) {
        size_t i = start;
        while (i < end) {
            if (tags[i].code != 0) { ++i; continue; }
            std::string type = tags[i].value;
            size_t recEnd = i + 1;
            while (recEnd < end && tags[recEnd].code != 0) ++recEnd;

            // Legacy POLYLINE owns its VERTEX/SEQEND records.
            if (type == "POLYLINE") {
                size_t seqEnd = recEnd;
                while (seqEnd < end &&
                       !(tags[seqEnd].code == 0 && tags[seqEnd].value == "SEQEND")) {
                    ++seqEnd;
                }
                size_t seqRecEnd = seqEnd;
                while (seqRecEnd < end &&
                       (seqRecEnd == seqEnd || tags[seqRecEnd].code != 0)) {
                    ++seqRecEnd;
                }
                std::vector<Tag> header(tags.begin() + i + 1, tags.begin() + recEnd);
                if (Draws(header, ctx, top)) ParsePolylineChain(header, recEnd, seqEnd, ctx);
                i = seqRecEnd;
                continue;
            }

            std::vector<Tag> e(tags.begin() + i + 1, tags.begin() + recEnd);

            // INSERT with attributes owns the ATTRIB/SEQEND records after it.
            std::vector<std::vector<Tag>> attribs;
            size_t next = recEnd;
            if (type == "INSERT" && FI(e, 66) == 1) {
                size_t j = recEnd;
                while (j < end && !(tags[j].code == 0 && tags[j].value == "SEQEND")) {
                    if (tags[j].code == 0 && tags[j].value == "ATTRIB") {
                        size_t aEnd = j + 1;
                        while (aEnd < end && tags[aEnd].code != 0) ++aEnd;
                        attribs.emplace_back(tags.begin() + j + 1, tags.begin() + aEnd);
                        j = aEnd;
                    } else {
                        ++j;
                    }
                }
                next = j;
                while (next < end && (next == j || tags[next].code != 0)) ++next;
            }

            if (Draws(e, ctx, top)) {
                if (type == "LINE") ParseLine(e, ctx);
                else if (type == "CIRCLE") WithOcs(e, ctx, [&](Ctx& c) { ParseCircle(e, c); });
                else if (type == "ARC") WithOcs(e, ctx, [&](Ctx& c) { ParseArc(e, c); });
                else if (type == "ELLIPSE") ParseEllipse(e, ctx);
                else if (type == "LWPOLYLINE") WithOcs(e, ctx, [&](Ctx& c) { ParseLwPolyline(e, c); });
                else if (type == "SPLINE") ParseSpline(e, ctx);
                else if (type == "HATCH") WithOcs(e, ctx, [&](Ctx& c) { ParseHatch(e, c); });
                else if (type == "SOLID" || type == "TRACE") WithOcs(e, ctx, [&](Ctx& c) { ParseSolid(e, c); });
                else if (type == "3DFACE") Parse3DFace(e, ctx);
                else if (type == "TEXT" || type == "ATTRIB" || type == "ATTDEF") {
                    if (type != "ATTDEF" && !(type == "ATTRIB" && (FI(e, 70) & 1))) {
                        WithOcs(e, ctx, [&](Ctx& c) { ParseText(e, c); });
                    }
                }
                else if (type == "MTEXT") ParseMText(e, ctx);
                else if (type == "LEADER") ParseLeader(e, ctx);
                else if (type == "INSERT") {
                    ParseInsert(e, ctx);
                    for (const auto& a : attribs) {
                        if (FI(a, 70) & 1) continue;   // invisible attribute
                        if (Draws(a, ctx, false)) WithOcs(a, ctx, [&](Ctx& c) { ParseText(a, c); });
                    }
                }
                else if (type == "DIMENSION") ParseDimension(e, ctx);
                else if (type == "POINT" || type == "SEQEND" || type == "VERTEX" ||
                         type == "VIEWPORT" || type == "ATTDEF") {
                    // nothing to draw
                } else {
                    ++skipped[type];
                }
            }
            i = next;
        }
    }

    // Visibility gate: layer on, entity visible, right space.
    bool Draws(const std::vector<Tag>& e, const Ctx& ctx, bool top) const {
        if (top && hasModelSpace && FI(e, 67) == 1) return false;
        if (FI(e, 60) == 1) return false;
        std::string layerName;
        const LayerDef& layer = LayerOf(e, ctx, layerName);
        return layer.visible;
    }

    void AddTo(const std::vector<Tag>& e, const Ctx& ctx, std::shared_ptr<VectorElement> el) {
        if (ctx.parent) {
            ctx.parent->AddChild(std::move(el));
            return;
        }
        std::string layerName;
        LayerOf(e, ctx, layerName);
        GetLayer(layerName)->AddChild(std::move(el));
    }

    // Runs `fn` for an entity whose coordinates are in its own object
    // coordinate system: identity for the usual extrusion, otherwise the
    // entity lands in a group carrying the OCS projection.
    template <typename Fn>
    void WithOcs(const std::vector<Tag>& e, Ctx& ctx, Fn fn) {
        double nx = F(e, 210, 0), ny = F(e, 220, 0), nz = F(e, 230, 1);
        Affine ocs = OcsProjection(nx, ny, nz);
        if (ocs.IsIdentity()) {
            fn(ctx);
            return;
        }
        Affine pm = PageMap();
        auto group = std::make_shared<VectorGroup>();
        group->Transform = (pm * ocs * pm.Inverse()).ToMatrix();
        Ctx sub = ctx;
        sub.parent = group;
        fn(sub);
        if (!group->Children.empty()) AddTo(e, ctx, group);
    }

    void ParseLine(const std::vector<Tag>& e, const Ctx& ctx) {
        std::string layerName;
        const LayerDef& layer = LayerOf(e, ctx, layerName);
        auto line = std::make_shared<VectorLine>();
        line->Start = P(F(e, 10), F(e, 20));
        line->End = P(F(e, 11), F(e, 21));
        line->Style.Stroke = MakeStroke(e, layer, ctx);
        AddTo(e, ctx, line);
    }

    void ParseCircle(const std::vector<Tag>& e, const Ctx& ctx) {
        std::string layerName;
        const LayerDef& layer = LayerOf(e, ctx, layerName);
        auto circle = std::make_shared<VectorCircle>();
        circle->Center = P(F(e, 10), F(e, 20));
        circle->Radius = static_cast<float>(S(F(e, 40)));
        circle->Style.Stroke = MakeStroke(e, layer, ctx);
        AddTo(e, ctx, circle);
    }

    void ParseArc(const std::vector<Tag>& e, const Ctx& ctx) {
        std::string layerName;
        const LayerDef& layer = LayerOf(e, ctx, layerName);
        Point2Dd c = P(F(e, 10), F(e, 20));
        double r = S(F(e, 40));
        double a1 = F(e, 50) * kPi / 180.0;
        double a2 = F(e, 51) * kPi / 180.0;
        while (a2 <= a1) a2 += 2 * kPi;   // DXF arcs run CCW from 50 to 51
        auto path = std::make_shared<VectorPath>();
        // Y-flip mirrors the sweep: use V = (0,-r) so angles keep meaning.
        AppendArc(*path, c, Point2Dd(r, 0), Point2Dd(0, -r), a1, a2, true);
        path->Style.Stroke = MakeStroke(e, layer, ctx);
        AddTo(e, ctx, path);
    }

    void ParseEllipse(const std::vector<Tag>& e, const Ctx& ctx) {
        std::string layerName;
        const LayerDef& layer = LayerOf(e, ctx, layerName);
        Point2Dd c = P(F(e, 10), F(e, 20));
        // Major-axis endpoint is relative to the centre; flip its Y.
        Point2Dd u(S(F(e, 11)), -S(F(e, 21)));
        double ratio = F(e, 40, 1.0);
        double t1 = F(e, 41, 0.0);
        double t2 = F(e, 42, 2 * kPi);
        while (t2 <= t1) t2 += 2 * kPi;
        // The minor axis is ratio * perp(major); the Y-flip mirrors it so
        // the parameter range keeps its meaning in document space.
        Point2Dd v(u.y * ratio, -u.x * ratio);
        auto path = std::make_shared<VectorPath>();
        AppendArc(*path, c, u, v, t1, t2, true);
        bool full = std::fabs((t2 - t1) - 2 * kPi) < 1e-9;
        if (full) path->ClosePath();
        path->Style.Stroke = MakeStroke(e, layer, ctx);
        AddTo(e, ctx, path);
    }

    struct PolyVertex { double x, y, bulge; };

    void EmitPolyline(const std::vector<Tag>& e, const Ctx& ctx, std::vector<PolyVertex> verts,
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
        const LayerDef& layer = LayerOf(e, ctx, layerName);
        bool hasBulge = false;
        for (const auto& v : verts) hasBulge |= std::fabs(v.bulge) > 1e-12;

        if (!hasBulge) {
            if (closed) {
                auto poly = std::make_shared<VectorPolygon>();
                for (const auto& v : verts) poly->Points.push_back(P(v.x, v.y));
                poly->Style.Stroke = MakeStroke(e, layer, ctx);
                AddTo(e, ctx, poly);
            } else {
                auto poly = std::make_shared<VectorPolyline>();
                for (const auto& v : verts) poly->Points.push_back(P(v.x, v.y));
                poly->Style.Stroke = MakeStroke(e, layer, ctx);
                AddTo(e, ctx, poly);
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
        path->Style.Stroke = MakeStroke(e, layer, ctx);
        AddTo(e, ctx, path);
    }

    void ParseLwPolyline(const std::vector<Tag>& e, const Ctx& ctx) {
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
        EmitPolyline(e, ctx, verts, (FI(e, 70) & 1) != 0);
    }

    // Vertices of a POLYLINE chain as raw tag records.
    std::vector<std::vector<Tag>> ChainVertices(size_t start, size_t seqEnd) {
        std::vector<std::vector<Tag>> out;
        size_t i = start;
        while (i < seqEnd) {
            if (tags[i].code == 0 && tags[i].value == "VERTEX") {
                size_t recEnd = i + 1;
                while (recEnd < seqEnd && tags[recEnd].code != 0) ++recEnd;
                out.emplace_back(tags.begin() + i + 1, tags.begin() + recEnd);
                i = recEnd;
            } else {
                ++i;
            }
        }
        return out;
    }

    void EmitFace(const std::vector<Tag>& header, const Ctx& ctx, const std::vector<Point2Dd>& pts) {
        if (pts.size() < 2) return;
        std::string layerName;
        const LayerDef& layer = LayerOf(header, ctx, layerName);
        if (pts.size() == 2) {
            auto line = std::make_shared<VectorLine>();
            line->Start = pts[0];
            line->End = pts[1];
            line->Style.Stroke = MakeStroke(header, layer, ctx);
            AddTo(header, ctx, line);
            return;
        }
        auto poly = std::make_shared<VectorPolygon>();
        poly->Points = pts;
        poly->Style.Stroke = MakeStroke(header, layer, ctx);
        AddTo(header, ctx, poly);
    }

    // Polyface mesh (70 & 64): position vertices (70 & 128 clear... 192)
    // followed by face records (70 = 128) naming 1-based indices in 71-74;
    // a negative index hides that edge. Projected onto the XY plane.
    void ParsePolyfaceMesh(const std::vector<Tag>& header, size_t start, size_t seqEnd, Ctx& ctx) {
        std::vector<Point2Dd> verts;
        std::vector<std::array<int, 4>> faces;
        for (const auto& v : ChainVertices(start, seqEnd)) {
            int vflags = FI(v, 70);
            if ((vflags & 128) && !(vflags & 64)) {
                faces.push_back({FI(v, 71), FI(v, 72), FI(v, 73), FI(v, 74)});
            } else {
                verts.push_back(P(F(v, 10), F(v, 20)));
            }
        }
        if (verts.empty() || faces.empty()) return;
        if (faces.size() > 200000) { ++skipped["POLYLINE(polyface too large)"]; return; }
        for (const auto& f : faces) {
            std::vector<Point2Dd> pts;
            for (int idx : f) {
                int k = std::abs(idx);
                if (k >= 1 && static_cast<size_t>(k) <= verts.size()) pts.push_back(verts[k - 1]);
            }
            if (pts.size() >= 3 && std::fabs(pts.back().x - pts[pts.size() - 2].x) < 1e-9 &&
                std::fabs(pts.back().y - pts[pts.size() - 2].y) < 1e-9) {
                pts.pop_back();
            }
            EmitFace(header, ctx, pts);
        }
    }

    // Polygon mesh (70 & 16): an M x N grid of vertices, quads between
    // neighbours, closed in M (1) and/or N (32).
    void ParsePolygonMesh(const std::vector<Tag>& header, size_t start, size_t seqEnd, Ctx& ctx) {
        int flags = FI(header, 70);
        int m = FI(header, 71), n = FI(header, 72);
        std::vector<Point2Dd> verts;
        for (const auto& v : ChainVertices(start, seqEnd)) {
            verts.push_back(P(F(v, 10), F(v, 20)));
        }
        if (m < 2 || n < 2 || static_cast<size_t>(m) * n > verts.size()) {
            ++skipped["POLYLINE(mesh)"];
            return;
        }
        bool closedM = (flags & 1) != 0, closedN = (flags & 32) != 0;
        int mm = closedM ? m : m - 1, nn = closedN ? n : n - 1;
        for (int i = 0; i < mm; ++i) {
            for (int j = 0; j < nn; ++j) {
                int i2 = (i + 1) % m, j2 = (j + 1) % n;
                EmitFace(header, ctx, {verts[i * n + j], verts[i2 * n + j],
                                       verts[i2 * n + j2], verts[i * n + j2]});
            }
        }
    }

    void ParsePolylineChain(const std::vector<Tag>& header, size_t start, size_t seqEnd,
                            Ctx& ctx) {
        int flags = FI(header, 70);
        if (flags & 64) { ParsePolyfaceMesh(header, start, seqEnd, ctx); return; }
        if (flags & 16) { ParsePolygonMesh(header, start, seqEnd, ctx); return; }
        bool splineFit = (flags & 4) != 0;
        std::vector<PolyVertex> verts, fitVerts;
        size_t i = start;
        while (i < seqEnd) {
            if (tags[i].code == 0 && tags[i].value == "VERTEX") {
                size_t recEnd = i + 1;
                while (recEnd < seqEnd && tags[recEnd].code != 0) ++recEnd;
                std::vector<Tag> v(tags.begin() + i + 1, tags.begin() + recEnd);
                int vflags = FI(v, 70);
                PolyVertex pv{F(v, 10), F(v, 20), F(v, 42)};
                if (splineFit) {
                    // Spline-fit polylines carry the frame (16) and the
                    // fitted curve points (8); draw the curve points.
                    if (vflags & 8) fitVerts.push_back(pv);
                    else if (!(vflags & 16)) verts.push_back(pv);
                } else {
                    verts.push_back(pv);
                }
                i = recEnd;
            } else {
                ++i;
            }
        }
        if (splineFit && !fitVerts.empty()) verts = fitVerts;
        // 3D polylines (8) project onto the XY plane; the OCS only applies
        // to 2D polylines.
        if (flags & 8) EmitPolyline(header, ctx, verts, (flags & 1) != 0);
        else WithOcs(header, ctx, [&](Ctx& c) { EmitPolyline(header, c, verts, (flags & 1) != 0); });
    }

    void ParseSpline(const std::vector<Tag>& e, const Ctx& ctx) {
        std::string layerName;
        const LayerDef& layer = LayerOf(e, ctx, layerName);
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
            Warn("DXF import: rational spline weights are ignored");
        }
        auto path = std::make_shared<VectorPath>();
        if (!ctrl.empty()) {
            AppendSpline(*path, degree, knots, ctrl, true);
        } else if (fit.size() >= 2) {
            Warn("DXF import: spline with fit points only, connecting linearly");
            path->MoveTo(static_cast<float>(fit[0].x), static_cast<float>(fit[0].y));
            for (size_t i = 1; i < fit.size(); ++i) {
                path->LineTo(static_cast<float>(fit[i].x),
                             static_cast<float>(fit[i].y));
            }
        } else {
            return;
        }
        if (flags & 1) path->ClosePath();
        path->Style.Stroke = MakeStroke(e, layer, ctx);
        AddTo(e, ctx, path);
    }

    void ParseSolid(const std::vector<Tag>& e, const Ctx& ctx) {
        std::string layerName;
        const LayerDef& layer = LayerOf(e, ctx, layerName);
        // SOLID corner order is 1,2,4,3.
        Point2Dd p1 = P(F(e, 10), F(e, 20)), p2 = P(F(e, 11), F(e, 21));
        Point2Dd p3 = P(F(e, 12), F(e, 22)), p4 = P(F(e, 13), F(e, 23));
        auto poly = std::make_shared<VectorPolygon>();
        poly->Points = {p1, p2, p4, p3};
        poly->Style.Fill = EntityColor(e, layer, ctx);
        AddTo(e, ctx, poly);
    }

    void Parse3DFace(const std::vector<Tag>& e, const Ctx& ctx) {
        std::string layerName;
        const LayerDef& layer = LayerOf(e, ctx, layerName);
        std::vector<Point2Dd> pts = {P(F(e, 10), F(e, 20)), P(F(e, 11), F(e, 21)),
                                     P(F(e, 12), F(e, 22)), P(F(e, 13), F(e, 23))};
        if (std::fabs(pts[3].x - pts[2].x) < 1e-9 && std::fabs(pts[3].y - pts[2].y) < 1e-9) {
            pts.pop_back();   // triangle
        }
        auto poly = std::make_shared<VectorPolygon>();
        poly->Points = pts;
        poly->Style.Stroke = MakeStroke(e, layer, ctx);
        AddTo(e, ctx, poly);
    }

    void ParseLeader(const std::vector<Tag>& e, const Ctx& ctx) {
        std::string layerName;
        const LayerDef& layer = LayerOf(e, ctx, layerName);
        auto poly = std::make_shared<VectorPolyline>();
        bool haveX = false;
        double x = 0;
        for (const Tag& t : e) {
            if (t.code == 10) { x = t.D(); haveX = true; }
            else if (t.code == 20 && haveX) { poly->Points.push_back(P(x, t.D())); haveX = false; }
        }
        if (poly->Points.size() < 2) return;
        poly->Style.Stroke = MakeStroke(e, layer, ctx);
        AddTo(e, ctx, poly);
    }

    void ParseHatch(const std::vector<Tag>& e, const Ctx& ctx) {
        std::string layerName;
        const LayerDef& layer = LayerOf(e, ctx, layerName);
        if (FI(e, 70) == 0 && !warnedHatchPattern) {
            warnedHatchPattern = true;
            Warn("DXF import: pattern hatches fill solid with the entity colour");
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
                    if (const Tag* t = next(40)) r = S(t->D());
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
                    Point2Dd u(S(mx), -S(my));
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
        path->Style.Fill = EntityColor(e, layer, ctx);
        AddTo(e, ctx, path);
    }

    void ParseText(const std::vector<Tag>& e, const Ctx& ctx) {
        std::string layerName;
        const LayerDef& layer = LayerOf(e, ctx, layerName);
        const std::string* value = FS(e, 1);
        if (!value || value->empty()) return;
        auto text = std::make_shared<VectorText>();
        int halign = FI(e, 72);
        int valign = FI(e, 73);
        // With a non-default alignment the second alignment point anchors.
        bool useAlign = (halign != 0 && halign != 3 && halign != 5) || valign != 0;
        double ax = useAlign ? F(e, 11, F(e, 10)) : F(e, 10);
        double ay = useAlign ? F(e, 21, F(e, 20)) : F(e, 20);
        double size = S(F(e, 40, 12.0));
        text->Position = P(ax, ay);
        if (valign == 2) text->Position.y += size * 0.35;        // middle
        else if (valign == 3) text->Position.y += size * 0.8;    // top
        text->BaseStyle.FontSize = static_cast<float>(size);
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
        text->Style.Fill = EntityColor(e, layer, ctx);
        AddTo(e, ctx, text);
    }

    void ParseMText(const std::vector<Tag>& e, const Ctx& ctx) {
        std::string layerName;
        const LayerDef& layer = LayerOf(e, ctx, layerName);
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

        double size = S(F(e, 40, 12.0));
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
        else if (row == 1) pos.y += size * 0.35;
        text->Position = pos;
        text->BaseStyle.FontSize = static_cast<float>(size);
        if (const std::string* styleName = FS(e, 7)) {
            auto it = tables.textStyles.find(*styleName);
            if (it != tables.textStyles.end()) {
                text->BaseStyle.FontFamily = it->second;
            }
        }
        // Rotation: the X-axis direction vector (11/21) or an angle (50).
        double rotation = 0;
        if (FS(e, 11) || FS(e, 21)) {
            double dx = F(e, 11, 1), dy = F(e, 21, 0);
            if (std::fabs(dx) > 1e-12 || std::fabs(dy) > 1e-12) rotation = std::atan2(dy, dx) * 180.0 / kPi;
        } else {
            rotation = F(e, 50);
        }
        if (std::fabs(rotation) > 1e-9) {
            text->Transform =
                    Matrix3x3::Translate(pos.x, pos.y) *
                    Matrix3x3::RotateDegrees(-rotation) *
                    Matrix3x3::Translate(-pos.x, -pos.y);
        }
        text->SetText(plain);
        text->Style.Fill = EntityColor(e, layer, ctx);
        AddTo(e, ctx, text);
    }

    // ===== BLOCK REFERENCES =====

    // Expands a block definition under a raw-coordinate transform `T`
    // (block -> world, DXF Y-up) into a group added for entity `e`.
    void ExpandBlock(const std::vector<Tag>& e, Ctx& ctx, const std::string& name,
                     const BlockDef& def, const Affine& T, const Color& byBlock) {
        Affine pm = PageMap();
        Affine G = pm * T * pm.Inverse();
        auto group = std::make_shared<VectorGroup>();
        if (!G.IsIdentity()) group->Transform = G.ToMatrix();
        Ctx sub;
        sub.parent = group;
        sub.layerOverride = EffectiveLayerName(e, ctx);
        sub.byBlock = &byBlock;
        sub.depth = ctx.depth + 1;
        sub.blockStack = ctx.blockStack;
        sub.blockStack.push_back(name);
        ParseEntityRange(def.start, def.end, sub, false);
        if (!group->Children.empty()) AddTo(e, ctx, group);
    }

    void ParseInsert(const std::vector<Tag>& e, Ctx& ctx) {
        const std::string* name = FS(e, 2);
        if (!name) return;
        auto it = blocks.find(*name);
        if (it == blocks.end()) {
            ++skipped["INSERT(missing block)"];
            return;
        }
        if (ctx.depth > 16 ||
            std::find(ctx.blockStack.begin(), ctx.blockStack.end(), *name) != ctx.blockStack.end()) {
            ++skipped["INSERT(recursive)"];
            return;
        }
        std::string layerName;
        const LayerDef& layer = LayerOf(e, ctx, layerName);
        Color color = EntityColor(e, layer, ctx);
        const BlockDef& def = it->second;
        double ix = F(e, 10), iy = F(e, 20);
        double sx = F(e, 41, 1.0), sy = F(e, 42, 1.0);
        double rot = F(e, 50) * kPi / 180.0;
        int cols = std::max(1, FI(e, 70, 1)), rows = std::max(1, FI(e, 71, 1));
        double cs = F(e, 44), rs = F(e, 45);
        if (cols * rows > 4096) { cols = rows = 1; }
        Affine ocs = OcsProjection(F(e, 210, 0), F(e, 220, 0), F(e, 230, 1));
        Affine rotate = Affine::Rotate(rot);
        for (int r = 0; r < rows; ++r) {
            for (int c = 0; c < cols; ++c) {
                Point2Dd off = rotate.Apply(c * cs, r * rs);
                Affine T = ocs * Affine::Translate(ix + off.x, iy + off.y) * rotate *
                           Affine::Scale(sx, sy) * Affine::Translate(-def.baseX, -def.baseY);
                ExpandBlock(e, ctx, *name, def, T, color);
            }
        }
    }

    // A dimension draws through the block AutoCAD rendered for it.
    void ParseDimension(const std::vector<Tag>& e, Ctx& ctx) {
        const std::string* name = FS(e, 2);
        if (!name || name->empty()) { ++skipped["DIMENSION(no block)"]; return; }
        auto it = blocks.find(*name);
        if (it == blocks.end()) { ++skipped["DIMENSION(missing block)"]; return; }
        if (ctx.depth > 16) return;
        std::string layerName;
        const LayerDef& layer = LayerOf(e, ctx, layerName);
        Color color = EntityColor(e, layer, ctx);
        ExpandBlock(e, ctx, *name, it->second, Affine(), color);
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
