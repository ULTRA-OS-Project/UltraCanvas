// UltraCanvas/Plugins/Vector/UltraCanvasDXFConverter.cpp
// DXF (AutoCAD R2000) writer - see UltraCanvasCADConverters.h.
//
// Emits tagged ASCII per Autodesk's public DXF reference, in the shape
// validated against ezdxf's strict reader and auditor: HEADER, the full
// table set (LTYPE/LAYER/STYLE/APPID/DIMSTYLE/BLOCK_RECORD and the empty
// VPORT/VIEW/UCS), the *Model_Space/*Paper_Space blocks, ENTITIES, and the
// OBJECTS root dictionary. Every object carries a handle and an owner.
//
// Mapping choices:
//  - VectorLayers become real DXF layers; group/element structure flattens
//    with transforms baked into coordinates (shared PathOps walk) and the
//    Y axis flipped to DXF's Y-up model space (units = points).
//  - Fills become solid HATCH entities whose boundary paths carry exact
//    line and cubic-spline edges - beziers are NOT flattened.
//  - Strokes become LWPOLYLINE entities (line-only subpaths) or SPLINE
//    entities (piecewise-bezier NURBS: degree 3, clamped knots with
//    interior multiplicity 3 - exact curves), with true colour (420),
//    nearest ACI (62), snapped lineweights (370) and dash linetypes.
//  - Text becomes TEXT entities per line, one text STYLE per font family.
//  - DXF transparency predates most consumers; opacity is reported through
//    the warning callback and colours stay at full strength.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "UltraCanvasCADConverters.h"
#include "UltraCanvasVectorStorage.h"
#include "UltraCanvasVectorPathOps.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <variant>

namespace UltraCanvas {
namespace VectorConverter {

using namespace VectorStorage;

namespace {

using namespace PathOps;

std::string Num(double v) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.6f", v);
    std::string s = buf;
    size_t last = s.find_last_not_of('0');
    if (s[last] == '.') ++last;   // keep one zero: "40.0"
    return s.substr(0, last + 1);
}

// DXF only accepts these lineweight values (1/100 mm).
int SnapLineweight(double widthPt) {
    static const int valid[] = {0, 5, 9, 13, 15, 18, 20, 25, 30, 35, 40, 50, 53,
                                60, 70, 80, 90, 100, 106, 120, 140, 158, 200, 211};
    int target = static_cast<int>(std::lround(widthPt * 25.4 / 72.0 * 100.0));
    int best = valid[0];
    for (int v : valid) {
        if (std::abs(v - target) < std::abs(best - target)) best = v;
    }
    return best;
}

// Nearest ACI colour over the full 256-entry palette (a display fallback
// for consumers that ignore the 420 true colour - LibreDWG's DXF output
// carries only the ACI, so this fidelity matters for the DWG chain).
int NearestAci(const Color& c) {
    // ACI 7 renders black on white pages: keep both extremes there.
    if ((c.r > 245 && c.g > 245 && c.b > 245) ||
        (c.r < 10 && c.g < 10 && c.b < 10)) {
        return 7;
    }
    long bestDist = -1;
    int best = 7;
    for (int aci = 1; aci <= 255; ++aci) {
        if (aci == 7) continue;   // handled above (palette lists it as black)
        Color p = AciPaletteColor(aci);
        long d = static_cast<long>(p.r - c.r) * (p.r - c.r) +
                 static_cast<long>(p.g - c.g) * (p.g - c.g) +
                 static_cast<long>(p.b - c.b) * (p.b - c.b);
        if (bestDist < 0 || d < bestDist) { bestDist = d; best = aci; }
    }
    return best;
}

std::string SanitizeName(const std::string& name, const char* fallback) {
    std::string out;
    for (char ch : name) {
        static const std::string bad = "<>/\\\":;?*|=`";
        out.push_back(bad.find(ch) == std::string::npos ? ch : '_');
    }
    if (out.empty()) out = fallback;
    return out;
}

class DxfEmitter {
public:
    DxfEmitter(const VectorDocument& document,
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

        // The model/paper space block record handles are referenced by every
        // entity's owner (330), so they must exist before the entity pass.
        msbr = NH();
        psbr = NH();

        // Pre-pass: collect layers, font families and dash patterns so the
        // tables can be written before the entities that reference them.
        for (const auto& layer : doc.Layers) {
            if (!layer) continue;
            std::string name = SanitizeName(layer->Name, "Layer");
            if (std::find(layerNames.begin(), layerNames.end(), name) ==
                layerNames.end()) {
                layerNames.push_back(name);
            }
            CollectResources(*layer);
        }
        if (layerNames.empty()) layerNames.push_back("Layer");

        // Entities render into a side buffer first (they allocate handles).
        for (const auto& layer : doc.Layers) {
            if (!layer || !layer->Visible) continue;
            currentLayer = SanitizeName(layer->Name, "Layer");
            for (const auto& child : layer->Children) {
                if (child) EmitElement(*child, layer->Style, Matrix3x3::Identity());
            }
        }

        std::ostringstream out;
        WriteHeader(out);
        WriteTables(out);
        WriteBlocks(out);
        out << "  0\nSECTION\n  2\nENTITIES\n" << entities.str() << "  0\nENDSEC\n";
        WriteObjects(out);
        out << "  0\nEOF\n";
        return out.str();
    }

private:
    const VectorDocument& doc;
    std::function<void(const std::string&)> warn;
    std::ostringstream entities;
    double pageW = 0, pageH = 0;
    unsigned handle = 0x100;
    std::string msbr, psbr;   // block record handles
    std::string currentLayer = "Layer";
    std::vector<std::string> layerNames;
    std::map<std::string, std::string> fontStyles;      // family -> style name
    std::map<std::string, std::string> dashLinetypes;   // pattern key -> name
    std::map<std::string, std::vector<double>> dashPatterns;
    bool warnedOpacity = false, warnedGradient = false, warnedSpanStyles = false;

    std::string NH() {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%X", handle++);
        return buf;
    }

    static void T(std::ostringstream& o, int code, const std::string& v) {
        char c[8];
        std::snprintf(c, sizeof(c), "%3d", code);
        o << c << "\n" << v << "\n";
    }
    static void T(std::ostringstream& o, int code, double v) { T(o, code, Num(v)); }
    static void T(std::ostringstream& o, int code, int v) {
        T(o, code, std::to_string(v));
    }

    double Y(double yPt) const { return pageH - yPt; }

    // ===== RESOURCE COLLECTION =====

    void CollectResources(const VectorElement& e) {
        if (e.Style.Stroke && !e.Style.Stroke->DashArray.empty()) {
            RegisterDash(e.Style.Stroke->DashArray);
        }
        if (e.Type == VectorElementType::Text) {
            const auto& t = static_cast<const VectorText&>(e);
            RegisterFont(t.BaseStyle.FontFamily);
            for (const auto& span : t.Spans) RegisterFont(span.Style.FontFamily);
        }
        if (const auto* g = dynamic_cast<const VectorGroup*>(&e)) {
            for (const auto& child : g->Children) {
                if (child) CollectResources(*child);
            }
        }
    }

    void RegisterFont(const std::string& family) {
        if (family.empty()) return;
        if (fontStyles.count(family)) return;
        fontStyles[family] = SanitizeName(family, "Standard");
    }

    std::string RegisterDash(const std::vector<double>& dash) {
        std::ostringstream key;
        for (double d : dash) key << d << ",";
        auto it = dashLinetypes.find(key.str());
        if (it != dashLinetypes.end()) return it->second;
        std::string name = "UC_DASH" + std::to_string(dashLinetypes.size() + 1);
        dashLinetypes[key.str()] = name;
        dashPatterns[name] = dash;
        return name;
    }

    // ===== FILE SKELETON =====

    void WriteHeader(std::ostringstream& o) {
        T(o, 0, "SECTION"); T(o, 2, "HEADER");
        T(o, 9, "$ACADVER"); T(o, 1, "AC1015");
        T(o, 9, "$HANDSEED"); T(o, 5, "FFFF");
        T(o, 9, "$INSUNITS"); T(o, 70, 0);
        T(o, 9, "$EXTMIN"); T(o, 10, 0.0); T(o, 20, 0.0); T(o, 30, 0.0);
        T(o, 9, "$EXTMAX"); T(o, 10, pageW); T(o, 20, pageH); T(o, 30, 0.0);
        T(o, 0, "ENDSEC");
    }

    void WriteTables(std::ostringstream& o) {
        T(o, 0, "SECTION"); T(o, 2, "TABLES");

        auto tableHead = [&](const char* name, int count) {
            std::string th = NH();
            T(o, 0, "TABLE"); T(o, 2, name); T(o, 5, th);
            T(o, 100, "AcDbSymbolTable"); T(o, 70, count);
            return th;
        };

        tableHead("VPORT", 0);
        T(o, 0, "ENDTAB");

        // LTYPE: the three required entries plus one per dash pattern.
        {
            std::string th = tableHead("LTYPE",
                                       3 + static_cast<int>(dashPatterns.size()));
            auto ltype = [&](const std::string& name, const std::string& desc,
                             const std::vector<double>& pattern) {
                T(o, 0, "LTYPE"); T(o, 5, NH()); T(o, 330, th);
                T(o, 100, "AcDbSymbolTableRecord");
                T(o, 100, "AcDbLinetypeTableRecord");
                T(o, 2, name); T(o, 70, 0); T(o, 3, desc); T(o, 72, 65);
                T(o, 73, static_cast<int>(pattern.size()));
                double total = 0;
                for (double p : pattern) total += std::fabs(p);
                T(o, 40, total);
                for (double p : pattern) { T(o, 49, p); T(o, 74, 0); }
            };
            ltype("ByBlock", "", {});
            ltype("ByLayer", "", {});
            ltype("Continuous", "Solid line", {});
            for (const auto& [name, dash] : dashPatterns) {
                // on/off pairs: off lengths are negative in DXF.
                std::vector<double> pattern;
                for (size_t i = 0; i < dash.size(); ++i) {
                    pattern.push_back(i % 2 ? -dash[i] : dash[i]);
                }
                ltype(name, "UltraCanvas dash pattern", pattern);
            }
        }

        {
            std::string th = tableHead("LAYER",
                                       1 + static_cast<int>(layerNames.size()));
            auto layer = [&](const std::string& name) {
                T(o, 0, "LAYER"); T(o, 5, NH()); T(o, 330, th);
                T(o, 100, "AcDbSymbolTableRecord");
                T(o, 100, "AcDbLayerTableRecord");
                T(o, 2, name); T(o, 70, 0); T(o, 62, 7); T(o, 6, "Continuous");
            };
            layer("0");
            for (const auto& name : layerNames) {
                if (name != "0") layer(name);
            }
        }

        {
            std::string th = tableHead("STYLE",
                                       1 + static_cast<int>(fontStyles.size()));
            auto style = [&](const std::string& name, const std::string& font) {
                T(o, 0, "STYLE"); T(o, 5, NH()); T(o, 330, th);
                T(o, 100, "AcDbSymbolTableRecord");
                T(o, 100, "AcDbTextStyleTableRecord");
                T(o, 2, name); T(o, 70, 0); T(o, 40, 0.0); T(o, 41, 1.0);
                T(o, 50, 0.0); T(o, 71, 0); T(o, 42, 2.5);
                T(o, 3, font); T(o, 4, "");
            };
            style("Standard", "arial.ttf");
            for (const auto& [family, name] : fontStyles) {
                if (name == "Standard") continue;
                std::string font;
                for (char ch : family) {
                    if (ch != ' ') font.push_back(static_cast<char>(std::tolower(ch)));
                }
                style(name, font + ".ttf");
            }
        }

        tableHead("VIEW", 0);
        T(o, 0, "ENDTAB");
        tableHead("UCS", 0);
        T(o, 0, "ENDTAB");

        {
            std::string th = tableHead("APPID", 1);
            T(o, 0, "APPID"); T(o, 5, NH()); T(o, 330, th);
            T(o, 100, "AcDbSymbolTableRecord"); T(o, 100, "AcDbRegAppTableRecord");
            T(o, 2, "ACAD"); T(o, 70, 0);
        }

        T(o, 0, "TABLE"); T(o, 2, "DIMSTYLE"); T(o, 5, NH());
        T(o, 100, "AcDbSymbolTable"); T(o, 70, 0);
        T(o, 100, "AcDbDimStyleTable"); T(o, 71, 0);
        T(o, 0, "ENDTAB");

        {
            std::string th = NH();
            T(o, 0, "TABLE"); T(o, 2, "BLOCK_RECORD"); T(o, 5, th);
            T(o, 100, "AcDbSymbolTable"); T(o, 70, 2);
            for (std::string* h : {&msbr, &psbr}) {
                // Handles pre-allocated in Build(): entities own-link to them.
                T(o, 0, "BLOCK_RECORD"); T(o, 5, *h); T(o, 330, th);
                T(o, 100, "AcDbSymbolTableRecord");
                T(o, 100, "AcDbBlockTableRecord");
                T(o, 2, h == &msbr ? "*Model_Space" : "*Paper_Space");
            }
            T(o, 0, "ENDTAB");
        }
        T(o, 0, "ENDSEC");
    }

    void WriteBlocks(std::ostringstream& o) {
        T(o, 0, "SECTION"); T(o, 2, "BLOCKS");
        for (const auto& [name, owner] :
             {std::pair<std::string, std::string>{"*Model_Space", msbr},
              {"*Paper_Space", psbr}}) {
            T(o, 0, "BLOCK"); T(o, 5, NH()); T(o, 330, owner);
            T(o, 100, "AcDbEntity"); T(o, 8, "0"); T(o, 100, "AcDbBlockBegin");
            T(o, 2, name); T(o, 70, 0);
            T(o, 10, 0.0); T(o, 20, 0.0); T(o, 30, 0.0);
            T(o, 3, name); T(o, 1, "");
            T(o, 0, "ENDBLK"); T(o, 5, NH()); T(o, 330, owner);
            T(o, 100, "AcDbEntity"); T(o, 8, "0"); T(o, 100, "AcDbBlockEnd");
        }
        T(o, 0, "ENDSEC");
    }

    void WriteObjects(std::ostringstream& o) {
        T(o, 0, "SECTION"); T(o, 2, "OBJECTS");
        std::string root = NH();
        T(o, 0, "DICTIONARY"); T(o, 5, root); T(o, 330, "0");
        T(o, 100, "AcDbDictionary"); T(o, 281, 1);
        std::string grp = NH();
        T(o, 3, "ACAD_GROUP"); T(o, 350, grp);
        T(o, 0, "DICTIONARY"); T(o, 5, grp); T(o, 330, root);
        T(o, 100, "AcDbDictionary"); T(o, 281, 1);
        T(o, 0, "ENDSEC");
    }

    // ===== STYLES =====

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

    Color ResolveFill(const FillData& fill) {
        if (const Color* c = std::get_if<Color>(&fill)) return *c;
        if (const GradientData* g = std::get_if<GradientData>(&fill)) {
            if (!warnedGradient) {
                warnedGradient = true;
                warn("DXF export: gradients are not written; "
                     "filling with the blend of the end stops");
            }
            const std::vector<GradientStop>* stops = nullptr;
            if (const auto* lg = std::get_if<LinearGradientData>(g)) stops = &lg->Stops;
            else if (const auto* rg = std::get_if<RadialGradientData>(g)) stops = &rg->Stops;
            else if (const auto* cg = std::get_if<ConicalGradientData>(g)) stops = &cg->Stops;
            if (stops && !stops->empty()) {
                const Color& c0 = stops->front().color;
                const Color& c1 = stops->back().color;
                return Color(static_cast<uint8_t>((c0.r + c1.r) / 2),
                             static_cast<uint8_t>((c0.g + c1.g) / 2),
                             static_cast<uint8_t>((c0.b + c1.b) / 2), 255);
            }
            return Color(128, 128, 128, 255);
        }
        warn("DXF export: pattern/reference fills are not supported, "
             "filling flat black");
        return Color(0, 0, 0, 255);
    }

    void NoteOpacity(const VectorStyle& s, uint8_t alpha) {
        if (!warnedOpacity &&
            (s.Opacity < 0.999f || s.FillOpacity < 0.999f || alpha < 254)) {
            warnedOpacity = true;
            warn("DXF export: opacity is not written (most CAD consumers "
                 "ignore DXF transparency); colours keep full strength");
        }
    }

    void EntityColor(const Color& c) {
        T(entities, 62, NearestAci(c));
        T(entities, 420, static_cast<int>((static_cast<uint32_t>(c.r) << 16) |
                                          (static_cast<uint32_t>(c.g) << 8) | c.b));
    }

    void BeginEntity(const char* type, const char* marker) {
        T(entities, 0, type);
        T(entities, 5, NH());
        T(entities, 330, msbr);
        T(entities, 100, "AcDbEntity");
        T(entities, 8, currentLayer);
        (void)marker;
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
                EmitShape(EllipseSegs(el.Center, el.RadiusX, el.RadiusY),
                          eff, ctm, true);
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
            case VectorElementType::Polyline:
            case VectorElementType::Polygon: {
                const auto* pts = e.Type == VectorElementType::Polyline
                        ? &static_cast<const VectorPolyline&>(e).Points
                        : &static_cast<const VectorPolygon&>(e).Points;
                if (pts->size() < 2) break;
                std::vector<FlatSeg> segs;
                segs.push_back({FlatSeg::Move, {(*pts)[0]}, false});
                for (size_t i = 1; i < pts->size(); ++i) {
                    segs.push_back({FlatSeg::Line, {(*pts)[i]}, false});
                }
                if (e.Type == VectorElementType::Polygon) segs.back().closeAfter = true;
                EmitShape(segs, eff, ctm, true);
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
                warn("DXF export: element type not supported, skipped (type " +
                     std::to_string(static_cast<int>(e.Type)) + ")");
                break;
        }
    }

    // ===== GEOMETRY =====

    struct DxfSeg { bool curve; Point2Dd a, c1, c2, b; };   // transformed, Y-up
    struct DxfSub { std::vector<DxfSeg> segs; bool closed = false; };

    std::vector<DxfSub> SubPaths(const std::vector<FlatSeg>& segs,
                                 const Matrix3x3& ctm) {
        std::vector<DxfSub> subs;
        Point2Dd cur(0, 0);
        auto X = [&](const Point2Dd& local) {
            Point2Dd p = ctm.Transform(local);
            return Point2Dd(p.x, Y(p.y));
        };
        for (const auto& s : segs) {
            switch (s.kind) {
                case FlatSeg::Move:
                    subs.emplace_back();
                    cur = s.p[0];
                    break;
                case FlatSeg::Line:
                    if (subs.empty()) subs.emplace_back();
                    subs.back().segs.push_back({false, X(cur), {}, {}, X(s.p[0])});
                    cur = s.p[0];
                    break;
                case FlatSeg::Cubic:
                    if (subs.empty()) subs.emplace_back();
                    subs.back().segs.push_back(
                            {true, X(cur), X(s.p[0]), X(s.p[1]), X(s.p[2])});
                    cur = s.p[2];
                    break;
            }
            if (s.closeAfter && !subs.empty()) subs.back().closed = true;
        }
        std::vector<DxfSub> out;
        for (auto& sp : subs) {
            if (!sp.segs.empty()) out.push_back(std::move(sp));
        }
        return out;
    }

    void EmitShape(const std::vector<FlatSeg>& segs, const VectorStyle& style,
                   const Matrix3x3& ctm, bool fillable) {
        bool filled = fillable && HasVisibleFill(style);
        bool stroked = HasVisibleStroke(style);
        if (!filled && !stroked) return;
        auto subs = SubPaths(segs, ctm);
        if (subs.empty()) return;

        if (filled) {
            Color fc = ResolveFill(*style.Fill);
            NoteOpacity(style, fc.a);
            BeginEntity("HATCH", "AcDbHatch");
            EntityColor(fc);
            T(entities, 100, "AcDbHatch");
            T(entities, 10, 0.0); T(entities, 20, 0.0); T(entities, 30, 0.0);
            T(entities, 210, 0.0); T(entities, 220, 0.0); T(entities, 230, 1.0);
            T(entities, 2, "SOLID"); T(entities, 70, 1); T(entities, 71, 0);
            T(entities, 91, static_cast<int>(subs.size()));
            for (const auto& sp : subs) {
                T(entities, 92, 0);   // edge-defined boundary
                bool needsClose =
                        std::fabs(sp.segs.front().a.x - sp.segs.back().b.x) > 1e-9 ||
                        std::fabs(sp.segs.front().a.y - sp.segs.back().b.y) > 1e-9;
                T(entities, 93, static_cast<int>(sp.segs.size()) + (needsClose ? 1 : 0));
                for (const auto& g : sp.segs) {
                    if (!g.curve) {
                        T(entities, 72, 1);
                        T(entities, 10, g.a.x); T(entities, 20, g.a.y);
                        T(entities, 11, g.b.x); T(entities, 21, g.b.y);
                    } else {
                        T(entities, 72, 4);
                        T(entities, 94, 3); T(entities, 73, 0); T(entities, 74, 0);
                        T(entities, 95, 8); T(entities, 96, 4);
                        for (int k = 0; k < 4; ++k) T(entities, 40, 0.0);
                        for (int k = 0; k < 4; ++k) T(entities, 40, 1.0);
                        for (const Point2Dd* p : {&g.a, &g.c1, &g.c2, &g.b}) {
                            T(entities, 10, p->x); T(entities, 20, p->y);
                        }
                    }
                }
                if (needsClose) {
                    T(entities, 72, 1);
                    T(entities, 10, sp.segs.back().b.x);
                    T(entities, 20, sp.segs.back().b.y);
                    T(entities, 11, sp.segs.front().a.x);
                    T(entities, 21, sp.segs.front().a.y);
                }
                T(entities, 97, 0);
            }
            T(entities, 75, 0); T(entities, 76, 1); T(entities, 98, 0);
        }

        if (stroked) {
            const StrokeData& st = *style.Stroke;
            Color sc(0, 0, 0, 255);
            if (const Color* c = std::get_if<Color>(&st.Fill)) sc = *c;
            else warn("DXF export: non-solid stroke paint replaced with black");
            NoteOpacity(style, sc.a);
            int lw = SnapLineweight(st.Width * AvgScale(ctm));
            std::string linetype = st.DashArray.empty()
                    ? std::string() : RegisterDash(st.DashArray);

            for (const auto& sp : subs) {
                bool hasCurve = false;
                for (const auto& g : sp.segs) hasCurve |= g.curve;

                if (!hasCurve) {
                    BeginEntity("LWPOLYLINE", "AcDbPolyline");
                    EntityColor(sc);
                    if (!linetype.empty()) T(entities, 6, linetype);
                    T(entities, 370, lw);
                    T(entities, 100, "AcDbPolyline");
                    T(entities, 90, static_cast<int>(sp.segs.size()) + 1);
                    T(entities, 70, sp.closed ? 1 : 0);
                    T(entities, 43, 0.0);
                    T(entities, 10, sp.segs.front().a.x);
                    T(entities, 20, sp.segs.front().a.y);
                    for (const auto& g : sp.segs) {
                        T(entities, 10, g.b.x); T(entities, 20, g.b.y);
                    }
                } else {
                    // Piecewise-bezier NURBS: degree 3, clamped knot vector
                    // with interior multiplicity 3 - reproduces the cubics
                    // exactly (lines become degenerate cubics).
                    BeginEntity("SPLINE", "AcDbSpline");
                    EntityColor(sc);
                    if (!linetype.empty()) T(entities, 6, linetype);
                    T(entities, 370, lw);
                    T(entities, 100, "AcDbSpline");
                    T(entities, 210, 0.0); T(entities, 220, 0.0); T(entities, 230, 1.0);
                    int n = static_cast<int>(sp.segs.size());
                    T(entities, 70, 8 | (sp.closed ? 1 : 0));   // planar
                    T(entities, 71, 3);
                    T(entities, 72, 3 * n + 5);   // knots
                    T(entities, 73, 3 * n + 1);   // control points
                    T(entities, 74, 0);
                    for (int k = 0; k < 4; ++k) T(entities, 40, 0.0);
                    for (int seg = 1; seg < n; ++seg) {
                        for (int k = 0; k < 3; ++k)
                            T(entities, 40, static_cast<double>(seg));
                    }
                    for (int k = 0; k < 4; ++k) T(entities, 40, static_cast<double>(n));
                    auto ctrl = [&](const Point2Dd& p) {
                        T(entities, 10, p.x); T(entities, 20, p.y); T(entities, 30, 0.0);
                    };
                    ctrl(sp.segs.front().a);
                    for (const auto& g : sp.segs) {
                        if (g.curve) {
                            ctrl(g.c1); ctrl(g.c2);
                        } else {
                            Point2Dd c1(g.a.x + (g.b.x - g.a.x) / 3,
                                        g.a.y + (g.b.y - g.a.y) / 3);
                            Point2Dd c2(g.a.x + 2 * (g.b.x - g.a.x) / 3,
                                        g.a.y + 2 * (g.b.y - g.a.y) / 3);
                            ctrl(c1); ctrl(c2);
                        }
                        ctrl(g.b);
                    }
                }
            }
        }
    }

    // ===== TEXT =====

    void EmitText(const VectorText& text, const VectorStyle& style,
                  const Matrix3x3& ctm) {
        if (!AxisAligned(ctm)) {
            warn("DXF export: rotated/skewed text is exported without its rotation");
        }
        Color tc(0, 0, 0, 255);
        if (style.Fill.has_value() &&
            !std::holds_alternative<std::monostate>(*style.Fill)) {
            tc = ResolveFill(*style.Fill);
        }
        NoteOpacity(style, tc.a);
        double scale = AvgScale(ctm);
        float baseSize = text.BaseStyle.FontSize > 0 ? text.BaseStyle.FontSize : 12.0f;
        float leading = baseSize * (text.BaseStyle.LineHeight > 0
                                            ? text.BaseStyle.LineHeight : 1.2f);
        std::string styleName = "Standard";
        auto it = fontStyles.find(text.BaseStyle.FontFamily);
        if (it != fontStyles.end()) styleName = it->second;

        // DXF TEXT has no spans: each line concatenates its chunks.
        std::vector<std::string> lines(1);
        bool mixedStyles = false;
        for (const auto& span : text.Spans) {
            if (span.Style.Weight != text.BaseStyle.Weight ||
                span.Style.Slant != text.BaseStyle.Slant ||
                span.Style.FontFamily != text.BaseStyle.FontFamily) {
                mixedStyles = true;
            }
            for (char ch : span.Text) {
                if (ch == '\n') lines.emplace_back();
                else lines.back().push_back(ch);
            }
        }
        if (mixedStyles && !warnedSpanStyles) {
            warnedSpanStyles = true;
            warn("DXF export: per-span text styles flatten to the base style "
                 "(DXF TEXT entities have a single style)");
        }

        for (size_t li = 0; li < lines.size(); ++li) {
            if (lines[li].empty()) continue;
            Point2Dd anchor = ctm.Transform(
                    Point2Dd(text.Position.x, text.Position.y + li * leading));
            double ax = anchor.x, ay = Y(anchor.y);

            BeginEntity("TEXT", "AcDbText");
            EntityColor(tc);
            T(entities, 100, "AcDbText");
            T(entities, 10, ax); T(entities, 20, ay); T(entities, 30, 0.0);
            T(entities, 40, baseSize * scale);
            T(entities, 1, lines[li]);
            T(entities, 7, styleName);
            if (text.BaseStyle.Anchor != TextAnchor::Start) {
                T(entities, 72, text.BaseStyle.Anchor == TextAnchor::Middle ? 1 : 2);
                T(entities, 11, ax); T(entities, 21, ay); T(entities, 31, 0.0);
            }
            T(entities, 100, "AcDbText");
        }
    }
};

}   // anonymous namespace

// ===== PUBLIC INTERFACE =====

FormatCapabilities DXFConverter::GetCapabilities() const {
    FormatCapabilities caps;
    caps.SupportsRectangle = true;
    caps.SupportsCircle = true;
    caps.SupportsEllipse = true;
    caps.SupportsLine = true;
    caps.SupportsPolyline = true;
    caps.SupportsPolygon = true;
    caps.SupportsPath = true;
    caps.SupportsCubicBezier = true;       // exact, as splines
    caps.SupportsQuadraticBezier = true;   // converted to cubics
    caps.SupportsArc = true;               // converted to cubics
    caps.SupportsCompoundPaths = true;
    caps.SupportsText = true;
    caps.SupportsSolidFill = true;
    caps.SupportsDashing = true;
    caps.SupportsGroups = true;            // flattened
    caps.SupportsLayers = true;            // real DXF layers
    return caps;
}

std::string DXFConverter::ExportToString(
        const VectorStorage::VectorDocument& document,
        const ConversionOptions& options) {
    DxfEmitter emitter(document, [&options](const std::string& msg) {
        if (options.WarningCallback) options.WarningCallback(msg);
    });
    std::string data = emitter.Build();
    if (options.ProgressCallback) options.ProgressCallback(1.0f);
    return data;
}

bool DXFConverter::ValidateFile(const std::string& filename) const {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;
    std::string head(64, '\0');
    file.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(file.gcount()));
    return ValidateData(head);
}

bool DXFConverter::ValidateData(const std::string& data) const {
    // Tagged ASCII: "  0\nSECTION" near the start (allowing a BOM/comments).
    return data.find("SECTION") != std::string::npos &&
           data.find("0") != std::string::npos;
}

} // namespace VectorConverter
} // namespace UltraCanvas
