// UltraCanvas/Plugins/Vector/UltraCanvasSVGConverter.cpp
// SVG converter for the Vector plugin: implements the SVGConverter declared
// in UltraCanvasVectorConverter.h (which previously had no implementation).
//
// Unlike the XAR/EPS/CDR writers, SVG maps to the VectorStorage model almost
// one-to-one, so the writer keeps full fidelity: groups and layers stay
// <g> elements, transforms stay matrix attributes, gradients keep all their
// stops in <defs>, text keeps its spans, and nothing is flattened. The
// importer parses with tinyxml2 and leans on the storage utilities
// (ParsePathString, ParseColorString, ParseTransformString), and applies
// <style> sheets through the HTMLReader's CSS parser.
// Version: 1.5.0 - width profiles and brushes exported as the shapes they draw,
//                  with the data the reader rebuilds the stroke from
// Version: 1.4.0 - line-gallery arrowheads exported as <marker>s and read back
// Version: 1.3.0 - <marker>: marker-start/-mid/-end drawn as grouped shapes
// Version: 1.2.0 - <style> sheets: class/id/type/descendant selectors cascade
//                  with presentation attributes and style="" (SVG 2 order)
// Version: 1.1.0
// Last Modified: 2026-10-07
// Author: UltraCanvas Framework

#include "UltraCanvasVectorConverter.h"
#include "DataFormats/UltraCanvasVectorStorage.h"
#include "HTMLReader/CSSStyleSheet.h"   // HTML::StyleSheet: <style> parsing
#include "UltraCanvasTextUtils.h"   // TryParseFloat / ParseFloatClassic
#include "UltraCanvasFileLoader.h"   // LoadFile: inflates .svgz

#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <locale>
#include <map>
#include <set>
#include <sstream>
#include <variant>
#include "UltraCanvasPathUtf8.h"

namespace UltraCanvas {
namespace VectorConverter {

using namespace VectorStorage;

namespace {

// ===== SHARED SMALL HELPERS =====

// The write side of the same locale problem the parsers below avoid, and the
// worse half: snprintf("%.6g") renders through LC_NUMERIC, so on a
// comma-decimal desktop this wrote `stroke-width="1,5"` - and inside path data
// a comma is the coordinate separator, so `M 1,5` silently became "move to
// (1, 5)" rather than "move to 1.5". A different picture, not a broken file.
// An imbued stream formats the same as "%.6g" (six significant digits,
// defaultfloat) with the decimal point pinned to '.'.
std::string Num(double v) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(6) << v;
    return out.str();
}

std::string HexColor(const Color& c) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", c.r, c.g, c.b);
    return buf;
}

std::string XmlEscape(const std::string& s) {
    std::string r;
    r.reserve(s.size());
    for (char ch : s) {
        switch (ch) {
            case '&': r += "&amp;"; break;
            case '<': r += "&lt;"; break;
            case '>': r += "&gt;"; break;
            case '"': r += "&quot;"; break;
            default: r.push_back(ch);
        }
    }
    return r;
}

bool NearlyOne(float v) { return std::fabs(v - 1.0f) < 1e-4f; }

// The line gallery's arrowheads by the name the writer tags its <marker>s
// with (data-ultracanvas-arrowhead), so the reader gives them back as
// arrowheads rather than as shapes. File-format names, fixed: not the
// editor's labels.
struct ArrowheadName {
    ArrowheadKind Kind;
    const char* Name;
};
constexpr ArrowheadName kArrowheadNames[] = {
    {ArrowheadKind::Triangle, "triangle"},         {ArrowheadKind::OpenArrow, "open-arrow"},
    {ArrowheadKind::Circle, "circle"},             {ArrowheadKind::Square, "square"},
    {ArrowheadKind::Diamond, "diamond"},           {ArrowheadKind::Bar, "bar"},
    {ArrowheadKind::StraightArrow, "straight-arrow"}, {ArrowheadKind::AngledArrow, "angled-arrow"},
    {ArrowheadKind::RoundedArrow, "rounded-arrow"}, {ArrowheadKind::Spot, "spot"},
    {ArrowheadKind::SolidDiamond, "solid-diamond"}, {ArrowheadKind::Feather, "feather"},
    {ArrowheadKind::Feather2, "feather2"},         {ArrowheadKind::HollowDiamond, "hollow-diamond"},
};

const char* ArrowheadNameOf(ArrowheadKind kind) {
    for (const auto& n : kArrowheadNames) {
        if (n.Kind == kind) return n.Name;
    }
    return nullptr;
}

// ===== WRITER =====

class SvgWriter {
public:
    SvgWriter(const VectorDocument& document, const SVGConverter::SVGOptions& options,
              std::function<void(const std::string&)> warnFn)
            : doc(document), opts(options), warn(std::move(warnFn)) {}

    std::string Build() {
        std::ostringstream body;
        for (const auto& layer : doc.Layers) {
            if (!layer) continue;
            std::ostringstream attrs;
            if (!layer->Name.empty()) attrs << " id=\"" << XmlEscape(layer->Name) << "\"";
            if (!layer->Visible) attrs << " display=\"none\"";
            if (layer->Opacity < 0.999f) attrs << " opacity=\"" << Num(layer->Opacity) << "\"";
            attrs << StyleAttrs(layer->Style);
            OpenTag(body, "g", attrs.str(), false);
            ++depth;
            for (const auto& child : layer->Children) {
                if (child) WriteElement(body, *child);
            }
            --depth;
            CloseTag(body, "g");
        }

        // Definitions referenced by <use>.
        std::ostringstream defsExtra;
        ++depth; ++depth;
        for (const auto& [id, element] : doc.Definitions) {
            if (element) WriteElement(defsExtra, *element, id);
        }
        --depth; --depth;

        std::ostringstream out;
        if (opts.IncludeXMLDeclaration) {
            out << "<?xml version=\"1.0\" encoding=\"" << opts.Encoding << "\"?>" << NL();
        }
        double w = doc.Size.width, h = doc.Size.height;
        if (w <= 0 || h <= 0) {
            Rect2Dd bbox = doc.GetBoundingBox();
            w = bbox.x + bbox.width;
            h = bbox.y + bbox.height;
            if (w <= 0) w = 100;
            if (h <= 0) h = 100;
        }
        out << "<svg xmlns=\"http://www.w3.org/2000/svg\""
               " xmlns:xlink=\"http://www.w3.org/1999/xlink\""
               " version=\"" << opts.Version << "\""
               " width=\"" << Num(w) << "\" height=\"" << Num(h) << "\"";
        if (opts.UseViewBox) {
            Rect2Dd vb = doc.ViewBox;
            if (vb.width <= 0 || vb.height <= 0) vb = Rect2Dd{0, 0, w, h};
            out << " viewBox=\"" << Num(vb.x) << " " << Num(vb.y) << " "
                << Num(vb.width) << " " << Num(vb.height) << "\"";
        }
        out << ">" << NL();
        if (!doc.Title.empty())
            out << Ind(1) << "<title>" << XmlEscape(doc.Title) << "</title>" << NL();
        if (!doc.Description.empty())
            out << Ind(1) << "<desc>" << XmlEscape(doc.Description) << "</desc>" << NL();
        if (!defs.str().empty() || !defsExtra.str().empty()) {
            out << Ind(1) << "<defs>" << NL() << defs.str() << defsExtra.str()
                << Ind(1) << "</defs>" << NL();
        }
        if (doc.BackgroundColor) {
            out << Ind(1) << "<rect width=\"" << Num(w) << "\" height=\"" << Num(h)
                << "\" fill=\"" << HexColor(*doc.BackgroundColor) << "\"/>" << NL();
        }
        out << body.str();
        out << "</svg>" << NL();
        return out.str();
    }

private:
    const VectorDocument& doc;
    SVGConverter::SVGOptions opts;
    std::function<void(const std::string&)> warn;
    std::ostringstream defs;
    int depth = 1;
    int nextDefId = 1;
    // Arrowhead markers already written, by kind, scale, end, width and colour.
    std::map<std::string, std::string> arrowheadMarkers;
    // Brush stamps already written into <defs>.
    std::map<const VectorGroup*, std::string> stampDefinitions;

    std::string NL() const { return opts.Minify ? "" : "\n"; }
    std::string Ind(int level) const {
        if (opts.Minify || !opts.PrettyPrint) return "";
        return std::string(static_cast<size_t>(level) *
                           static_cast<size_t>(std::max(1, opts.IndentSize)), ' ');
    }

    void OpenTag(std::ostringstream& out, const char* tag, const std::string& attrs,
                 bool selfClose) {
        out << Ind(depth) << "<" << tag << attrs << (selfClose ? "/>" : ">") << NL();
    }
    void CloseTag(std::ostringstream& out, const char* tag) {
        out << Ind(depth) << "</" << tag << ">" << NL();
    }

    // ===== PAINT =====

    std::string RegisterGradient(const GradientData& g) {
        std::string id = "grad" + std::to_string(nextDefId++);
        std::ostringstream d;
        auto writeStops = [&](const std::vector<GradientStop>& stops, int level) {
            for (const auto& st : stops) {
                d << Ind(level) << "<stop offset=\"" << Num(st.position)
                  << "\" stop-color=\"" << HexColor(st.color) << "\"";
                if (st.color.a < 255)
                    d << " stop-opacity=\"" << Num(st.color.a / 255.0) << "\"";
                d << "/>" << NL();
            }
        };
        auto unitsAttr = [](GradientUnits u) {
            return u == GradientUnits::UserSpaceOnUse
                           ? " gradientUnits=\"userSpaceOnUse\"" : "";
        };
        auto spreadAttr = [](GradientSpreadMethod s) {
            if (s == GradientSpreadMethod::Reflect) return " spreadMethod=\"reflect\"";
            if (s == GradientSpreadMethod::Repeat) return " spreadMethod=\"repeat\"";
            return "";
        };
        if (const auto* lg = std::get_if<LinearGradientData>(&g)) {
            d << Ind(2) << "<linearGradient id=\"" << id << "\""
              << " x1=\"" << Num(lg->Start.x) << "\" y1=\"" << Num(lg->Start.y) << "\""
              << " x2=\"" << Num(lg->End.x) << "\" y2=\"" << Num(lg->End.y) << "\""
              << unitsAttr(lg->Units) << spreadAttr(lg->SpreadMethod);
            if (lg->Transform) {
                std::string t = SerializeTransform(*lg->Transform);
                if (!t.empty()) d << " gradientTransform=\"" << t << "\"";
            }
            d << ">" << NL();
            writeStops(lg->Stops, 3);
            d << Ind(2) << "</linearGradient>" << NL();
        } else if (const auto* rg = std::get_if<RadialGradientData>(&g)) {
            d << Ind(2) << "<radialGradient id=\"" << id << "\""
              << " cx=\"" << Num(rg->Center.x) << "\" cy=\"" << Num(rg->Center.y) << "\""
              << " r=\"" << Num(rg->Radius) << "\""
              << " fx=\"" << Num(rg->FocalPoint.x) << "\" fy=\"" << Num(rg->FocalPoint.y) << "\""
              << unitsAttr(rg->Units) << spreadAttr(rg->SpreadMethod);
            if (rg->Transform) {
                std::string t = SerializeTransform(*rg->Transform);
                if (!t.empty()) d << " gradientTransform=\"" << t << "\"";
            }
            d << ">" << NL();
            writeStops(rg->Stops, 3);
            d << Ind(2) << "</radialGradient>" << NL();
        } else if (const auto* cg = std::get_if<ConicalGradientData>(&g)) {
            // SVG 1.1 has no conic gradient; approximate with a radial one.
            warn("SVG export: conical gradient approximated as radial");
            d << Ind(2) << "<radialGradient id=\"" << id << "\""
              << " cx=\"" << Num(cg->Center.x) << "\" cy=\"" << Num(cg->Center.y) << "\""
              << " r=\"0.5\"" << unitsAttr(cg->Units) << ">" << NL();
            writeStops(cg->Stops, 3);
            d << Ind(2) << "</radialGradient>" << NL();
        } else {
            warn("SVG export: mesh gradients are not representable in SVG 1.1; "
                 "using a flat mid-grey");
            return "";
        }
        defs << d.str();
        return id;
    }

    std::string RegisterPattern(const PatternData& p) {
        std::string id = "pat" + std::to_string(nextDefId++);
        std::ostringstream d;
        d << Ind(2) << "<pattern id=\"" << id << "\""
          << " x=\"" << Num(p.PatternRect.x) << "\" y=\"" << Num(p.PatternRect.y) << "\""
          << " width=\"" << Num(p.PatternRect.width) << "\""
          << " height=\"" << Num(p.PatternRect.height) << "\"";
        if (p.Units == GradientUnits::UserSpaceOnUse)
            d << " patternUnits=\"userSpaceOnUse\"";
        if (p.ViewBox.width > 0 && p.ViewBox.height > 0) {
            d << " viewBox=\"" << Num(p.ViewBox.x) << " " << Num(p.ViewBox.y) << " "
              << Num(p.ViewBox.width) << " " << Num(p.ViewBox.height) << "\"";
        }
        if (p.Transform) {
            std::string t = SerializeTransform(*p.Transform);
            if (!t.empty()) d << " patternTransform=\"" << t << "\"";
        }
        d << ">" << NL();
        if (p.Content) {
            int savedDepth = depth;
            depth = 3;
            for (const auto& child : p.Content->Children) {
                if (child) WriteElement(d, *child);
            }
            depth = savedDepth;
        }
        d << Ind(2) << "</pattern>" << NL();
        defs << d.str();
        return id;
    }

    // Paint attribute value for a FillData; empty string means "omit".
    std::string PaintValue(const FillData& fill, float* alphaOut) {
        if (alphaOut) *alphaOut = 1.0f;
        if (std::holds_alternative<std::monostate>(fill)) return "none";
        if (const Color* c = std::get_if<Color>(&fill)) {
            if (alphaOut) *alphaOut = c->a / 255.0f;
            return HexColor(*c);
        }
        if (const GradientData* g = std::get_if<GradientData>(&fill)) {
            std::string id = RegisterGradient(*g);
            return id.empty() ? std::string("#808080") : "url(#" + id + ")";
        }
        if (const PatternData* p = std::get_if<PatternData>(&fill)) {
            return "url(#" + RegisterPattern(*p) + ")";
        }
        if (const std::string* ref = std::get_if<std::string>(&fill)) return *ref;
        return "";
    }

    // The stroke's presentation properties, name and value, in the order the
    // attributes are written. `paintOut` / `alphaOut` receive the paint and
    // its colour's alpha, for a caller drawing in the stroke's paint.
    std::vector<std::pair<std::string, std::string>> StrokeProps(const VectorStyle& s, std::string* paintOut = nullptr,
                                                                 float* alphaOut = nullptr) {
        std::vector<std::pair<std::string, std::string>> p;
        const StrokeData& st = *s.Stroke;
        float alpha = 1.0f;
        const std::string v = PaintValue(st.Fill, &alpha);
        if (paintOut) *paintOut = v;
        if (alphaOut) *alphaOut = alpha;
        if (!v.empty()) p.emplace_back("stroke", v);
        p.emplace_back("stroke-width", Num(st.Width));
        if (st.LineCap == StrokeLineCap::Round) p.emplace_back("stroke-linecap", "round");
        else if (st.LineCap == StrokeLineCap::Square) p.emplace_back("stroke-linecap", "square");
        if (st.LineJoin == StrokeLineJoin::Round) p.emplace_back("stroke-linejoin", "round");
        else if (st.LineJoin == StrokeLineJoin::Bevel) p.emplace_back("stroke-linejoin", "bevel");
        if (std::fabs(st.MiterLimit - 4.0f) > 1e-4f) p.emplace_back("stroke-miterlimit", Num(st.MiterLimit));
        if (!st.DashArray.empty()) {
            std::string dash;
            for (size_t i = 0; i < st.DashArray.size(); ++i) dash += (i ? " " : "") + Num(st.DashArray[i]);
            p.emplace_back("stroke-dasharray", dash);
            if (std::fabs(st.DashOffset) > 1e-9) p.emplace_back("stroke-dashoffset", Num(st.DashOffset));
        }
        const float so = alpha * st.Opacity * s.StrokeOpacity;
        if (!NearlyOne(so)) p.emplace_back("stroke-opacity", Num(so));
        return p;
    }

    // `shape` marks a geometric element: with no fill in the model it has
    // no fill at all, which SVG must be told (its default is black).
    std::string StyleAttrs(const VectorStyle& s, bool shape = false) {
        std::ostringstream a;
        if (s.Fill) {
            float alpha = 1.0f;
            std::string v = PaintValue(*s.Fill, &alpha);
            if (!v.empty()) a << " fill=\"" << v << "\"";
            float fo = alpha * s.FillOpacity;
            if (!NearlyOne(fo)) a << " fill-opacity=\"" << Num(fo) << "\"";
        } else {
            if (shape) a << " fill=\"none\"";
            if (!NearlyOne(s.FillOpacity)) a << " fill-opacity=\"" << Num(s.FillOpacity) << "\"";
        }
        if (s.Stroke) {
            for (const auto& [name, value] : StrokeProps(s)) a << " " << name << "=\"" << value << "\"";
        }
        if (!NearlyOne(s.Opacity)) a << " opacity=\"" << Num(s.Opacity) << "\"";
        if (!s.Visible || !s.Display) a << " display=\"none\"";
        return a.str();
    }

    std::string CommonAttrs(const VectorElement& e, const std::string& forcedId = "") {
        std::ostringstream a;
        const std::string& id = forcedId.empty() ? e.Id : forcedId;
        if (!id.empty()) a << " id=\"" << XmlEscape(id) << "\"";
        if (!e.Classes.empty()) {
            a << " class=\"";
            for (size_t i = 0; i < e.Classes.size(); ++i) {
                if (i) a << " ";
                a << XmlEscape(e.Classes[i]);
            }
            a << "\"";
        }
        if (e.Transform) {
            std::string t = SerializeTransform(*e.Transform);
            if (!t.empty()) a << " transform=\"" << t << "\"";
        }
        const bool shape = e.Type == VectorElementType::Rectangle ||
                           e.Type == VectorElementType::RoundedRectangle ||
                           e.Type == VectorElementType::Circle ||
                           e.Type == VectorElementType::Ellipse ||
                           e.Type == VectorElementType::Line ||
                           e.Type == VectorElementType::Polyline ||
                           e.Type == VectorElementType::Polygon ||
                           e.Type == VectorElementType::Path;
        a << StyleAttrs(e.Style, shape);
        if (e.Type == VectorElementType::Line || e.Type == VectorElementType::Polyline ||
            e.Type == VectorElementType::Path)
            a << ArrowheadAttrs(e);
        return a.str();
    }

    // ===== ARROWHEADS =====

    // The line gallery's arrowheads as SVG markers, so every SVG reader draws
    // them: marker-start / marker-end on the line, each pointing at a
    // <marker> whose content is the very outline the renderer fills or
    // strokes (ArrowheadOutline). The renderer draws them only on a path whose
    // first and last subpaths are open, and so does this.
    std::string ArrowheadAttrs(const VectorElement& e) {
        std::string startId, endId, a;
        ArrowheadMarkers(e, startId, endId);
        if (!startId.empty()) a += " marker-start=\"url(#" + startId + ")\"";
        if (!endId.empty()) a += " marker-end=\"url(#" + endId + ")\"";
        return a;
    }

    // The <marker> ids for the element's arrowheads; empty where it has none.
    void ArrowheadMarkers(const VectorElement& e, std::string& startId, std::string& endId) {
        const VectorStyle& s = e.Style;
        if (!s.Stroke || !s.Stroke->HasArrowheads() || s.Stroke->Width <= 0 ||
            std::holds_alternative<std::monostate>(s.Stroke->Fill))
            return;
        PathData outline;
        Point2Dd start, startDir, end, endDir;
        if (!BuildOutlinePath(e, outline) || !PathEndpoints(outline, start, startDir, end, endDir)) return;
        if (s.Stroke->StartArrow.IsSet())
            startId = ArrowheadMarker(s.Stroke->StartArrow, true, *s.Stroke, s.StrokeOpacity);
        if (s.Stroke->EndArrow.IsSet())
            endId = ArrowheadMarker(s.Stroke->EndArrow, false, *s.Stroke, s.StrokeOpacity);
    }

    // ===== WIDTH PROFILES AND BRUSHES =====

    // SVG has neither a variable-width stroke nor a brush, so a shape with
    // either is written as what the renderer draws: a group holding the shape
    // with its fill and no stroke, then the band a width profile makes
    // (filled even-odd in the stroke's paint) or the brush's stamps (<use>s
    // of the stamp, written once in <defs>), then the arrowheads on top. The
    // group carries what draws them - the stroke as a style declaration list
    // (data-ultracanvas-stroke), the profile and the brush - so the reader
    // gives the shape back with its stroke, while any other reader draws the
    // shapes. A profile wins over a brush, as in the renderer.
    static bool HasGalleryStroke(const VectorElement& e) {
        switch (e.Type) {
            case VectorElementType::Rectangle: case VectorElementType::RoundedRectangle:
            case VectorElementType::Circle: case VectorElementType::Ellipse:
            case VectorElementType::Line: case VectorElementType::Polyline:
            case VectorElementType::Polygon: case VectorElementType::Path:
                break;
            default:
                return false;
        }
        const VectorStyle& s = e.Style;
        return s.Stroke && s.Stroke->Width > 0 && !std::holds_alternative<std::monostate>(s.Stroke->Fill) &&
               (s.Stroke->HasWidthProfile() || s.Stroke->HasBrush());
    }

    // A matrix as an SVG transform, dot-decimal.
    static std::string MatrixValue(const Matrix3x3& m) {
        return "matrix(" + Num(m.m[0][0]) + " " + Num(m.m[1][0]) + " " + Num(m.m[0][1]) + " " +
               Num(m.m[1][1]) + " " + Num(m.m[0][2]) + " " + Num(m.m[1][2]) + ")";
    }

    // The brush's stamp in <defs>, written once however many lines use it.
    std::string StampDefinition(const VectorGroup& stamp) {
        auto it = stampDefinitions.find(&stamp);
        if (it != stampDefinitions.end()) return it->second;
        const std::string id = "ucstamp" + std::to_string(nextDefId++);
        std::ostringstream s;   // gradients the stamp registers land in <defs> before it
        const int savedDepth = depth;
        depth = 2;
        WriteElement(s, stamp, id);
        depth = savedDepth;
        defs << s.str();
        stampDefinitions.emplace(&stamp, id);
        return id;
    }

    void WriteLineGallery(std::ostringstream& out, const VectorElement& e, const std::string& forcedId) {
        const VectorStyle& style = e.Style;
        const StrokeData& st = *style.Stroke;
        PathData outline;
        if (!BuildOutlinePath(e, outline)) return;
        std::string outlineD = SerializePathData(outline);
        while (!outlineD.empty() && outlineD.back() == ' ') outlineD.pop_back();

        // The group: the element's place and presence, and what its stroke is.
        std::ostringstream a;
        const std::string& id = forcedId.empty() ? e.Id : forcedId;
        if (!id.empty()) a << " id=\"" << XmlEscape(id) << "\"";
        if (!e.Classes.empty()) {
            a << " class=\"";
            for (size_t i = 0; i < e.Classes.size(); ++i) a << (i ? " " : "") << XmlEscape(e.Classes[i]);
            a << "\"";
        }
        if (e.Transform) {
            const std::string t = MatrixValue(*e.Transform);
            a << " transform=\"" << t << "\"";
        }
        if (!NearlyOne(style.Opacity)) a << " opacity=\"" << Num(style.Opacity) << "\"";
        if (!style.Visible || !style.Display) a << " display=\"none\"";
        std::string paint;
        float alpha = 1.0f;
        std::string declarations;
        for (const auto& [name, value] : StrokeProps(style, &paint, &alpha)) declarations += name + ": " + value + "; ";
        std::string startId, endId;
        ArrowheadMarkers(e, startId, endId);
        if (!startId.empty()) declarations += "marker-start: url(#" + startId + "); ";
        if (!endId.empty()) declarations += "marker-end: url(#" + endId + "); ";
        while (!declarations.empty() && declarations.back() == ' ') declarations.pop_back();
        a << " data-ultracanvas-stroke=\"" << XmlEscape(declarations) << "\"";
        if (st.HasWidthProfile()) {
            a << " data-ultracanvas-width-profile=\"";
            for (size_t i = 0; i < st.WidthProfile.size(); ++i)
                a << (i ? " " : "") << Num(st.WidthProfile[i].T) << " " << Num(st.WidthProfile[i].Factor);
            a << "\"";
        }
        std::string stampId;
        if (st.HasBrush()) {
            stampId = StampDefinition(*st.Brush->Stamp);
            a << " data-ultracanvas-brush=\"" << stampId << " " << Num(st.Brush->Spacing) << " "
              << Num(st.Brush->Scale) << " " << (st.Brush->Rotate ? 1 : 0) << "\"";
        }
        OpenTag(out, "g", a.str(), false);
        ++depth;

        // The shape with its fill, told it has no stroke (an ancestor's
        // would otherwise apply).
        auto bare = e.Clone();
        bare->Id.clear();
        bare->Classes.clear();
        bare->Transform.reset();
        bare->Style.Opacity = 1.0f;
        bare->Style.Visible = bare->Style.Display = true;
        StrokeData none;
        none.Fill = std::monostate{};
        bare->Style.Stroke = none;
        WriteElement(out, *bare);

        // What the renderer draws in place of the stroke.
        const float o = alpha * st.Opacity * style.StrokeOpacity;
        if (st.HasWidthProfile()) {
            const PathData band = VariableWidthOutline(outline, st);
            std::string d = SerializePathData(band);
            while (!d.empty() && d.back() == ' ') d.pop_back();
            if (!d.empty() && !paint.empty()) {
                std::string attrs = " d=\"" + d + "\" fill=\"" + paint + "\" fill-rule=\"evenodd\"";
                if (!NearlyOne(o)) attrs += " fill-opacity=\"" + Num(o) + "\"";
                OpenTag(out, "path", attrs, true);
            }
        } else if (!stampId.empty()) {
            OpenTag(out, "g", "", false);
            ++depth;
            for (const auto& sub : FlattenPathData(outline)) {
                for (const Matrix3x3& m : BrushStampPlacements(sub.Points, st))
                    OpenTag(out, "use", " href=\"#" + stampId + "\" transform=\"" + MatrixValue(m) + "\"", true);
            }
            --depth;
            CloseTag(out, "g");
        }

        // The arrowheads, over the band or the stamps.
        if (!startId.empty() || !endId.empty()) {
            std::string attrs = " d=\"" + outlineD + "\" fill=\"none\" stroke=\"none\"";
            if (!startId.empty()) attrs += " marker-start=\"url(#" + startId + ")\"";
            if (!endId.empty()) attrs += " marker-end=\"url(#" + endId + ")\"";
            OpenTag(out, "path", attrs, true);
        }
        --depth;
        CloseTag(out, "g");
    }

    // The <marker> for one arrowhead. It is drawn in the line's own units
    // (markerUnits="userSpaceOnUse": the arrowhead is sized from the line
    // width, with the renderer's 0.5 floor, not simply scaled by it) with
    // its tip on the vertex. orient="auto" turns +x along the path, so an end
    // arrowhead points along +x and a start one along -x: the renderer points
    // both away from the line, and SVG 1.1 readers know no
    // auto-start-reverse. The viewBox covers the outline, so a reader that
    // clips to the marker's viewport loses nothing either.
    std::string ArrowheadMarker(const ArrowheadData& arrow, bool atStart, const StrokeData& stroke,
                                float strokeOpacity) {
        const char* name = ArrowheadNameOf(arrow.Kind);
        if (!name) return {};
        bool stroked = false;
        const PathData outline = ArrowheadOutline(arrow, Point2Dd(0, 0), Point2Dd(atStart ? -1 : 1, 0),
                                                  stroke.Width, stroked);
        if (outline.commands.empty()) return {};

        // The same arrowhead in the same colour is written once.
        const Color* colour = std::get_if<Color>(&stroke.Fill);
        const float opacity = stroke.Opacity * strokeOpacity;
        std::string key;
        if (colour) {
            key = std::string(name) + "|" + Num(arrow.Scale) + "|" + (atStart ? "s" : "e") + "|" +
                  Num(stroke.Width) + "|" + HexColor(*colour) + "|" + std::to_string(colour->a) + "|" + Num(opacity);
            auto it = arrowheadMarkers.find(key);
            if (it != arrowheadMarkers.end()) return it->second;
        }
        float alpha = 1.0f;
        const std::string paint = PaintValue(stroke.Fill, &alpha);
        if (paint.empty() || paint == "none") return {};

        const Rect2Dd box = outline.GetBounds();
        const double pad = std::max(0.5 * stroke.Width, 1e-3);   // a stroked outline's half width
        const Rect2Dd view{box.x - pad, box.y - pad, box.width + 2 * pad, box.height + 2 * pad};
        const std::string id = "arrow" + std::to_string(nextDefId++);
        std::ostringstream m;
        m << Ind(2) << "<marker id=\"" << id << "\" data-ultracanvas-arrowhead=\"" << name << "\""
          << " data-ultracanvas-scale=\"" << Num(arrow.Scale) << "\""
          << " data-ultracanvas-end=\"" << (atStart ? "start" : "end") << "\""
          << " markerUnits=\"userSpaceOnUse\" orient=\"auto\" overflow=\"visible\""
          << " viewBox=\"" << Num(view.x) << " " << Num(view.y) << " " << Num(view.width) << " "
          << Num(view.height) << "\" markerWidth=\"" << Num(view.width) << "\" markerHeight=\""
          << Num(view.height) << "\" refX=\"0\" refY=\"0\">" << NL();
        std::string d = SerializePathData(outline);
        while (!d.empty() && d.back() == ' ') d.pop_back();
        const float o = alpha * opacity;
        m << Ind(3) << "<path d=\"" << d << "\"";
        if (stroked) {
            m << " fill=\"none\" stroke=\"" << paint << "\" stroke-width=\"" << Num(stroke.Width) << "\""
              << " stroke-linecap=\"round\" stroke-linejoin=\"round\"";
            if (!NearlyOne(o)) m << " stroke-opacity=\"" << Num(o) << "\"";
        } else {
            m << " fill=\"" << paint << "\"";
            if (!NearlyOne(o)) m << " fill-opacity=\"" << Num(o) << "\"";
        }
        m << "/>" << NL() << Ind(2) << "</marker>" << NL();
        defs << m.str();
        if (!key.empty()) arrowheadMarkers.emplace(key, id);
        return id;
    }

    // ===== TEXT =====

    static void FontAttrs(std::ostringstream& a, const VectorTextStyle& s,
                          const VectorTextStyle* base) {
        auto differs = [&](auto get) { return !base || get(s) != get(*base); };
        if (!s.FontFamily.empty() &&
            differs([](const VectorTextStyle& t) { return t.FontFamily; }))
            a << " font-family=\"" << XmlEscape(s.FontFamily) << "\"";
        if (s.FontSize > 0 &&
            differs([](const VectorTextStyle& t) { return t.FontSize; }))
            a << " font-size=\"" << Num(s.FontSize) << "\"";
        if (differs([](const VectorTextStyle& t) { return t.Weight; })) {
            if (s.Weight == FontWeight::Bold) a << " font-weight=\"bold\"";
            else if (s.Weight == FontWeight::ExtraBold) a << " font-weight=\"800\"";
            else if (s.Weight == FontWeight::Light) a << " font-weight=\"300\"";
            else if (base) a << " font-weight=\"normal\"";
        }
        if (differs([](const VectorTextStyle& t) { return t.Slant; })) {
            if (s.Slant == FontSlant::Italic) a << " font-style=\"italic\"";
            else if (s.Slant == FontSlant::Oblique) a << " font-style=\"oblique\"";
            else if (base) a << " font-style=\"normal\"";
        }
        if (differs([](const VectorTextStyle& t) { return t.Underline; }) ||
            differs([](const VectorTextStyle& t) { return t.StrikeThrough; })) {
            std::string deco;
            if (s.Underline) deco = "underline";
            if (s.StrikeThrough) deco += std::string(deco.empty() ? "" : " ") + "line-through";
            a << " text-decoration=\"" << (deco.empty() ? "none" : deco) << "\"";
        }
        if (std::fabs(s.LetterSpacing) > 1e-6f &&
            differs([](const VectorTextStyle& t) { return t.LetterSpacing; }))
            a << " letter-spacing=\"" << Num(s.LetterSpacing) << "\"";
    }

    void WriteText(std::ostringstream& out, const VectorText& t) {
        std::ostringstream a;
        a << " x=\"" << Num(t.Position.x) << "\" y=\"" << Num(t.Position.y) << "\"";
        FontAttrs(a, t.BaseStyle, nullptr);
        if (t.BaseStyle.Anchor == TextAnchor::Middle) a << " text-anchor=\"middle\"";
        else if (t.BaseStyle.Anchor == TextAnchor::End) a << " text-anchor=\"end\"";
        // Spans may carry meaningful leading/trailing spaces ("Hello " +
        // bold "SVG"); default XML whitespace handling would collapse them.
        a << " xml:space=\"preserve\"";
        a << CommonAttrs(t);

        out << Ind(depth) << "<text" << a.str() << ">";
        for (const auto& span : t.Spans) {
            std::ostringstream sa;
            if (span.Position) {
                sa << " x=\"" << Num(span.Position->x)
                   << "\" y=\"" << Num(span.Position->y) << "\"";
            }
            FontAttrs(sa, span.Style, &t.BaseStyle);
            std::string attrs = sa.str();
            if (attrs.empty() && t.Spans.size() == 1) {
                out << XmlEscape(span.Text);
            } else {
                out << "<tspan" << attrs << ">" << XmlEscape(span.Text) << "</tspan>";
            }
        }
        out << "</text>" << NL();
    }

    // ===== ELEMENTS =====

    void WriteElement(std::ostringstream& out, const VectorElement& e,
                      const std::string& forcedId = "") {
        if (HasGalleryStroke(e)) {
            WriteLineGallery(out, e, forcedId);
            return;
        }
        switch (e.Type) {
            case VectorElementType::Rectangle:
            case VectorElementType::RoundedRectangle: {
                const auto& r = static_cast<const VectorRect&>(e);
                std::ostringstream a;
                a << " x=\"" << Num(r.Bounds.x) << "\" y=\"" << Num(r.Bounds.y)
                  << "\" width=\"" << Num(r.Bounds.width)
                  << "\" height=\"" << Num(r.Bounds.height) << "\"";
                if (r.RadiusX > 0) a << " rx=\"" << Num(r.RadiusX) << "\"";
                if (r.RadiusY > 0 && std::fabs(r.RadiusY - r.RadiusX) > 1e-6f)
                    a << " ry=\"" << Num(r.RadiusY) << "\"";
                OpenTag(out, "rect", a.str() + CommonAttrs(e, forcedId), true);
                break;
            }
            case VectorElementType::Circle: {
                const auto& c = static_cast<const VectorCircle&>(e);
                std::ostringstream a;
                a << " cx=\"" << Num(c.Center.x) << "\" cy=\"" << Num(c.Center.y)
                  << "\" r=\"" << Num(c.Radius) << "\"";
                OpenTag(out, "circle", a.str() + CommonAttrs(e, forcedId), true);
                break;
            }
            case VectorElementType::Ellipse: {
                const auto& el = static_cast<const VectorEllipse&>(e);
                std::ostringstream a;
                a << " cx=\"" << Num(el.Center.x) << "\" cy=\"" << Num(el.Center.y)
                  << "\" rx=\"" << Num(el.RadiusX) << "\" ry=\"" << Num(el.RadiusY) << "\"";
                OpenTag(out, "ellipse", a.str() + CommonAttrs(e, forcedId), true);
                break;
            }
            case VectorElementType::Line: {
                const auto& ln = static_cast<const VectorLine&>(e);
                std::ostringstream a;
                a << " x1=\"" << Num(ln.Start.x) << "\" y1=\"" << Num(ln.Start.y)
                  << "\" x2=\"" << Num(ln.End.x) << "\" y2=\"" << Num(ln.End.y) << "\"";
                OpenTag(out, "line", a.str() + CommonAttrs(e, forcedId), true);
                break;
            }
            case VectorElementType::Polyline:
            case VectorElementType::Polygon: {
                const auto* pts = e.Type == VectorElementType::Polyline
                        ? &static_cast<const VectorPolyline&>(e).Points
                        : &static_cast<const VectorPolygon&>(e).Points;
                std::ostringstream a;
                a << " points=\"";
                for (size_t i = 0; i < pts->size(); ++i) {
                    if (i) a << " ";
                    a << Num((*pts)[i].x) << "," << Num((*pts)[i].y);
                }
                a << "\"";
                OpenTag(out, e.Type == VectorElementType::Polyline ? "polyline" : "polygon",
                        a.str() + CommonAttrs(e, forcedId), true);
                break;
            }
            case VectorElementType::Path: {
                const auto& p = static_cast<const VectorPath&>(e);
                std::string d = SerializePathData(p.Path);
                while (!d.empty() && d.back() == ' ') d.pop_back();
                OpenTag(out, "path", " d=\"" + d + "\"" + CommonAttrs(e, forcedId), true);
                break;
            }
            case VectorElementType::Text:
                WriteText(out, static_cast<const VectorText&>(e));
                break;
            case VectorElementType::Group:
            case VectorElementType::Layer: {
                const auto& g = static_cast<const VectorGroup&>(e);
                OpenTag(out, "g", CommonAttrs(e, forcedId), g.Children.empty());
                if (!g.Children.empty()) {
                    ++depth;
                    for (const auto& child : g.Children) {
                        if (child) WriteElement(out, *child);
                    }
                    --depth;
                    CloseTag(out, "g");
                }
                break;
            }
            case VectorElementType::Symbol: {
                const auto& g = static_cast<const VectorSymbol&>(e);
                std::ostringstream a;
                if (g.ViewBox.width > 0 && g.ViewBox.height > 0) {
                    a << " viewBox=\"" << Num(g.ViewBox.x) << " " << Num(g.ViewBox.y)
                      << " " << Num(g.ViewBox.width) << " " << Num(g.ViewBox.height) << "\"";
                }
                OpenTag(out, "symbol", a.str() + CommonAttrs(e, forcedId), false);
                ++depth;
                for (const auto& child : g.Children) {
                    if (child) WriteElement(out, *child);
                }
                --depth;
                CloseTag(out, "symbol");
                break;
            }
            case VectorElementType::Use: {
                const auto& u = static_cast<const VectorUse&>(e);
                std::ostringstream a;
                std::string href = u.Reference;
                if (!href.empty() && href[0] != '#') href = "#" + href;
                a << " href=\"" << XmlEscape(href) << "\"";
                if (std::fabs(u.Position.x) > 1e-9 || std::fabs(u.Position.y) > 1e-9)
                    a << " x=\"" << Num(u.Position.x) << "\" y=\"" << Num(u.Position.y) << "\"";
                if (u.Size.width > 0 && u.Size.height > 0)
                    a << " width=\"" << Num(u.Size.width)
                      << "\" height=\"" << Num(u.Size.height) << "\"";
                OpenTag(out, "use", a.str() + CommonAttrs(e, forcedId), true);
                break;
            }
            case VectorElementType::Image: {
                const auto& im = static_cast<const VectorImage&>(e);
                std::ostringstream a;
                a << " x=\"" << Num(im.Bounds.x) << "\" y=\"" << Num(im.Bounds.y)
                  << "\" width=\"" << Num(im.Bounds.width)
                  << "\" height=\"" << Num(im.Bounds.height) << "\"";
                if (!im.Source.empty()) {
                    a << " href=\"" << XmlEscape(im.Source) << "\"";
                } else if (!im.EmbeddedData.empty()) {
                    a << " href=\"data:" << (im.MimeType.empty() ? "image/png" : im.MimeType)
                      << ";base64," << Base64(im.EmbeddedData) << "\"";
                }
                OpenTag(out, "image", a.str() + CommonAttrs(e, forcedId), true);
                break;
            }
            default:
                warn("SVG export: element type not supported, skipped (type " +
                     std::to_string(static_cast<int>(e.Type)) + ")");
                break;
        }
    }

    static std::string Base64(const std::vector<uint8_t>& data) {
        static const char* alphabet =
                "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string out;
        out.reserve((data.size() + 2) / 3 * 4);
        for (size_t i = 0; i < data.size(); i += 3) {
            uint32_t v = static_cast<uint32_t>(data[i]) << 16;
            if (i + 1 < data.size()) v |= static_cast<uint32_t>(data[i + 1]) << 8;
            if (i + 2 < data.size()) v |= data[i + 2];
            out.push_back(alphabet[(v >> 18) & 63]);
            out.push_back(alphabet[(v >> 12) & 63]);
            out.push_back(i + 1 < data.size() ? alphabet[(v >> 6) & 63] : '=');
            out.push_back(i + 2 < data.size() ? alphabet[v & 63] : '=');
        }
        return out;
    }
};

// ===== READER =====

class SvgReader {
public:
    explicit SvgReader(std::function<void(const std::string&)> warnFn)
            : warn(std::move(warnFn)) {}

    std::shared_ptr<VectorDocument> Parse(const std::string& data) {
        tinyxml2::XMLDocument xml;
        if (xml.Parse(data.c_str(), data.size()) != tinyxml2::XML_SUCCESS) {
            warn(std::string("SVG import: XML parse error: ") + xml.ErrorStr());
            return nullptr;
        }
        const tinyxml2::XMLElement* svg = xml.RootElement();
        if (!svg || std::strcmp(StripNs(svg->Name()), "svg") != 0) {
            warn("SVG import: no <svg> root element");
            return nullptr;
        }

        auto doc = std::make_shared<VectorDocument>();

        double vb[4] = {0, 0, 0, 0};
        bool hasViewBox = false;
        if (const char* v = svg->Attribute("viewBox")) {
            std::string list = v;   // "0 0 100 100" or "0,0,100,100"
            for (char& ch : list) if (ch == ',') ch = ' ';
            std::istringstream iss(list);
            iss.imbue(std::locale::classic());
            hasViewBox = static_cast<bool>(iss >> vb[0] >> vb[1] >> vb[2] >> vb[3]);
        }
        double w = LengthAttr(svg, "width", hasViewBox ? vb[2] : 0);
        double h = LengthAttr(svg, "height", hasViewBox ? vb[3] : 0);
        doc->Size = Size2Dd{w, h};
        if (hasViewBox) doc->ViewBox = Rect2Dd{vb[0], vb[1], vb[2], vb[3]};

        // Style sheets first: every property read below, gradient stops
        // included, goes through them. The cache is keyed by element
        // address, which a second document could reuse.
        styleSheet.Clear();
        sheetValues.clear();
        if (w > 0) styleSheet.SetMediaWidth(static_cast<float>(w));
        CollectStyleSheets(svg);

        // Definitions pre-pass: gradients and markers can be referenced
        // before (or after) their definition, so collect them document-wide
        // first.
        CollectGradients(svg);
        markers.clear();
        markerClips.clear();
        CollectMarkers(svg);

        // When every drawable at the top level is a <g>, treat each as a
        // layer (the shape this exporter and layered editors like Inkscape
        // produce); otherwise everything lands in one synthesized layer.
        bool allGroups = true;
        bool anyDrawable = false;
        for (const tinyxml2::XMLElement* child = svg->FirstChildElement();
             child; child = child->NextSiblingElement()) {
            if (IsNonDrawable(child)) continue;
            anyDrawable = true;
            if (std::strcmp(StripNs(child->Name()), "g") != 0) allGroups = false;
        }

        if (anyDrawable && allGroups) {
            for (const tinyxml2::XMLElement* child = svg->FirstChildElement();
                 child; child = child->NextSiblingElement()) {
                if (IsNonDrawable(child)) {
                    // still consume defs/title/desc/style
                    auto scratch = std::make_shared<VectorGroup>();
                    ParseNode(child, *doc, scratch.get());
                    continue;
                }
                auto g = std::dynamic_pointer_cast<VectorGroup>(ParseShape(child, *doc));
                if (!g) continue;
                auto layer = std::make_shared<VectorLayer>();
                layer->Name = g->Id;
                layer->Style = g->Style;
                layer->Transform = g->Transform;
                layer->Visible = g->Style.Display && g->Style.Visible;
                layer->Children = std::move(g->Children);
                // SVG's default fill is black; it starts at the layer root
                // and ResolveInheritedPaint hands it down.
                if (!layer->Style.Fill) layer->Style.Fill = Color(0, 0, 0, 255);
                doc->Layers.push_back(layer);
            }
        } else {
            auto layer = doc->AddLayer("Layer 1");
            layer->Style.Fill = Color(0, 0, 0, 255);
            for (const tinyxml2::XMLElement* child = svg->FirstChildElement();
                 child; child = child->NextSiblingElement()) {
                ParseNode(child, *doc, layer.get());
            }
        }

        // The editor and the renderer place document coordinates straight on
        // the page (0,0 to Size) and do not apply a layer's own Transform.
        // SVG instead maps the viewBox onto width x height, and the top-level
        // <g> a layer came from may carry a transform (Xara writes
        // scale(1 -1), svgo a translate). Both go into one group inside each
        // layer, where the renderer, hit-testing and the writers all honour
        // them, and the viewBox becomes the page.
        // A width in inches or points rarely lands exactly on the viewBox
        // ("8.333in" is 799.97 px for an 800-wide viewBox): within 0.1% at the
        // origin that is rounding, not a mapping.
        const bool sameSpace = hasViewBox && vb[0] == 0 && vb[1] == 0 && vb[2] > 0 && vb[3] > 0 &&
                               std::fabs(w / vb[2] - 1.0) < 1e-3 && std::fabs(h / vb[3] - 1.0) < 1e-3;
        if (hasViewBox && !sameSpace && vb[2] > 0 && vb[3] > 0 && w > 0 && h > 0) {
            double sx = w / vb[2], sy = h / vb[3];
            double tx = 0, ty = 0;
            const char* par = svg->Attribute("preserveAspectRatio");
            const std::string align = par ? par : "xMidYMid meet";
            if (align.rfind("none", 0) != 0) {
                const bool slice = align.find("slice") != std::string::npos;
                const double k = slice ? std::max(sx, sy) : std::min(sx, sy);
                const double fx = align.find("xMin") != std::string::npos ? 0.0
                                : align.find("xMax") != std::string::npos ? 1.0 : 0.5;
                const double fy = align.find("YMin") != std::string::npos ? 0.0
                                : align.find("YMax") != std::string::npos ? 1.0 : 0.5;
                tx = (w - vb[2] * k) * fx;
                ty = (h - vb[3] * k) * fy;
                sx = sy = k;
            }
            viewBoxMatrix = Matrix3x3::Translate(tx, ty) * Matrix3x3::Scale(sx, sy) *
                            Matrix3x3::Translate(-vb[0], -vb[1]);
            doc->ViewBox = Rect2Dd{0, 0, w, h};
        }
        for (auto& layer : doc->Layers) {
            if (!layer) continue;
            Matrix3x3 m = viewBoxMatrix;
            if (layer->Transform) m = m * *layer->Transform;
            layer->Transform.reset();
            if (m.IsIdentity() || layer->Children.empty()) continue;
            auto content = std::make_shared<VectorGroup>();
            content->Transform = m;
            layer->AddChild(content);
            for (auto& child : layer->Children) {
                if (child && child != content) content->AddChild(child);
            }
            layer->Children.assign(1, content);
        }

        // The renderer draws each element with its own style only - an unset
        // fill draws nothing - so the fill and stroke SVG inherits from the
        // enclosing <g> elements (and the black default) are written into
        // every element that does not set its own.
        for (auto& layer : doc->Layers) {
            if (layer) ResolveInheritedPaint(*layer, layer->Style.Fill, layer->Style.Stroke);
        }
        const std::optional<FillData> black = FillData(Color(0, 0, 0, 255));
        for (auto& [id, def] : doc->Definitions) {
            if (def) ResolveInheritedPaint(*def, black, std::nullopt);
        }
        return doc;
    }

    // Hands the inherited fill and stroke down the tree. An element keeps
    // what it sets itself, including an explicit "none" (a monostate fill,
    // or a stroke recorded in strokeNone), and passes its resolved paint on
    // to its own children.
    void ResolveInheritedPaint(VectorElement& e, const std::optional<FillData>& fill,
                               const std::optional<StrokeData>& stroke) {
        if (!e.Style.Fill && fill) e.Style.Fill = fill;
        if (!e.Style.Stroke && stroke && !strokeNone.count(&e)) e.Style.Stroke = stroke;
        if (auto* g = dynamic_cast<VectorGroup*>(&e)) {
            for (auto& child : g->Children) {
                if (child) ResolveInheritedPaint(*child, e.Style.Fill, e.Style.Stroke);
            }
        }
    }

    static bool IsNonDrawable(const tinyxml2::XMLElement* e) {
        const char* n = StripNs(e->Name());
        return std::strcmp(n, "defs") == 0 || std::strcmp(n, "title") == 0 ||
               std::strcmp(n, "desc") == 0 || std::strcmp(n, "style") == 0 ||
               std::strcmp(n, "metadata") == 0 || std::strcmp(n, "marker") == 0 ||
               std::strcmp(n, "linearGradient") == 0 ||
               std::strcmp(n, "radialGradient") == 0;
    }

private:
    std::function<void(const std::string&)> warn;
    std::map<std::string, GradientData> gradients;
    // Every <style> in the document, and what it gives each element (worked
    // out on the element's first property read).
    HTML::StyleSheet styleSheet;
    std::map<const tinyxml2::XMLElement*, std::map<std::string, HTML::Declaration>> sheetValues;
    // Maps viewBox units onto the page (identity when they coincide).
    Matrix3x3 viewBoxMatrix = Matrix3x3::Identity();
    // SVG images inside SVG images: how deep this reader is, and how many it
    // has read (for unique definition prefixes).
    int depth = 0;
    int nestedCount = 0;
    // Elements that say stroke="none" themselves: they must not inherit a
    // group's stroke, and an unset Stroke cannot tell the two apart.
    std::set<const VectorElement*> strokeNone;
    // <marker> elements by id; the ones being drawn right now (a marker whose
    // content uses itself would never end); the viewport clip made for each;
    // what context-fill / context-stroke stand for while one is drawn; and
    // how deep in <clipPath> content the reader is (no markers there).
    std::map<std::string, const tinyxml2::XMLElement*> markers;
    std::set<const tinyxml2::XMLElement*> expandingMarkers;
    std::map<const tinyxml2::XMLElement*, std::string> markerClips;
    std::optional<FillData> contextFill, contextStroke;
    int clipDepth = 0;

    static const char* StripNs(const char* name) {
        const char* colon = std::strchr(name, ':');
        return colon ? colon + 1 : name;
    }

    // ParseFloatClassic, not strtod: SVG lengths are dot-decimal by
    // specification, and strtod reads them through LC_NUMERIC, which the Linux
    // backend sets from the environment for XIM. See UltraCanvasTextUtils.h.
    static double ParseLength(const char* s, double fallback) {
        if (!s || !*s) return fallback;
        // strtod skipped leading whitespace and a leading '+'; tinyxml2 hands
        // attribute values over untrimmed and the SVG number grammar allows the
        // sign, so keep both here - ParseFloatClassic takes neither, by design.
        const char* begin = s;
        while (*begin && std::isspace(static_cast<unsigned char>(*begin))) ++begin;
        if (*begin == '+') ++begin;
        double v = fallback;
        const char* end = ParseFloatClassic(begin, begin + std::strlen(begin), v);
        if (end == begin) return fallback;
        // Unit handling: user units and px are the native unit; the absolute
        // units convert at CSS's 96 dpi.
        if (std::strncmp(end, "pt", 2) == 0) v *= 96.0 / 72.0;
        else if (std::strncmp(end, "pc", 2) == 0) v *= 16.0;
        else if (std::strncmp(end, "mm", 2) == 0) v *= 96.0 / 25.4;
        else if (std::strncmp(end, "cm", 2) == 0) v *= 96.0 / 2.54;
        else if (std::strncmp(end, "in", 2) == 0) v *= 96.0;
        else if (*end == '%') return fallback;   // percentages need context
        return v;
    }

    static double LengthAttr(const tinyxml2::XMLElement* e, const char* name,
                             double fallback) {
        return ParseLength(e->Attribute(name), fallback);
    }

    // x, y, width, height, rx, ry, cx, cy and r are properties in SVG 2, so a
    // style sheet can set them too (`.card { rx: 8 }`).
    double GeometryLength(const tinyxml2::XMLElement* e, const char* name, double fallback) {
        const std::string v = Prop(e, name);
        return ParseLength(v.empty() ? nullptr : v.c_str(), fallback);
    }

    // A property of `e` by the CSS cascade SVG 2 specifies: the presentation
    // attribute is weakest, then the <style> sheets, then style="…"; an
    // !important declaration beats every normal one, and an !important
    // style="…" beats an !important sheet.
    std::string Prop(const tinyxml2::XMLElement* e, const char* name) {
        std::string result;
        if (const char* a = e->Attribute(name)) result = a;
        const HTML::Declaration* sheet = nullptr;
        if (!styleSheet.rules.empty()) {
            const auto& values = SheetValues(e);
            auto it = values.find(name);
            if (it != values.end()) sheet = &it->second;
        }
        std::vector<HTML::Declaration> inlineDecls;
        const HTML::Declaration* inl = nullptr;
        if (const char* styleAttr = e->Attribute("style")) {
            inlineDecls = HTML::StyleSheet::ParseDeclarationList(styleAttr);
            for (const auto& d : inlineDecls) {
                if (d.property == name && (d.important || !inl || !inl->important)) inl = &d;
            }
        }
        if (sheet && !sheet->important) result = sheet->value;
        if (inl && !inl->important) result = inl->value;
        if (sheet && sheet->important) result = sheet->value;
        if (inl && inl->important) result = inl->value;
        return result;
    }

    // ===== CSS =====

    // The <style> elements, wherever they are (editors write them at the top,
    // hand-written files often inside <defs>), in document order so that of
    // two equally specific rules the later wins.
    void CollectStyleSheets(const tinyxml2::XMLElement* e) {
        if (std::strcmp(StripNs(e->Name()), "style") == 0) {
            const char* type = e->Attribute("type");
            if (type && *type && !std::strstr(type, "css")) return;
            const char* media = e->Attribute("media");
            if (media && !HTML::StyleSheet::MediaMatches(media, styleSheet.GetMediaWidth())) return;
            std::string css;   // the text and CDATA sections, in order
            for (const tinyxml2::XMLNode* n = e->FirstChild(); n; n = n->NextSibling()) {
                if (const tinyxml2::XMLText* t = n->ToText()) css += t->Value();
            }
            styleSheet.ParseAppend(css);
            return;
        }
        for (const tinyxml2::XMLElement* child = e->FirstChildElement();
             child; child = child->NextSiblingElement()) {
            CollectStyleSheets(child);
        }
    }

    // What the sheets say about `e`: matching rules in specificity, then
    // source order, the !important declarations after all the normal ones.
    const std::map<std::string, HTML::Declaration>& SheetValues(const tinyxml2::XMLElement* e) {
        auto [it, inserted] = sheetValues.try_emplace(e);
        if (!inserted) return it->second;
        struct Match {
            int specificity;
            int order;
            const HTML::Rule* rule;
        };
        std::vector<Match> matches;
        for (const auto& rule : styleSheet.rules) {
            int best = -1;
            for (const auto& selector : rule.selectors) {
                if (SelectorMatches(selector, e)) best = std::max(best, selector.Specificity());
            }
            if (best >= 0) matches.push_back({best, rule.sourceOrder, &rule});
        }
        std::sort(matches.begin(), matches.end(), [](const Match& a, const Match& b) {
            return a.specificity != b.specificity ? a.specificity < b.specificity : a.order < b.order;
        });
        for (bool important : {false, true}) {
            for (const auto& m : matches) {
                for (const auto& d : m.rule->declarations) {
                    if (d.important == important) it->second[d.property] = d;
                }
            }
        }
        return it->second;
    }

    static std::string Lower(std::string s) {
        for (char& ch : s) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        return s;
    }

    // The last compound matches `e`, the ones before it ancestors in order.
    // The parser reads `a > b` as `a b`, and drops sibling combinators and
    // dynamic pseudo-classes (:hover) - nothing is hovered in a file.
    static bool SelectorMatches(const HTML::Selector& selector, const tinyxml2::XMLElement* e) {
        if (selector.path.empty() || !CompoundMatches(selector.path.back(), e)) return false;
        int index = static_cast<int>(selector.path.size()) - 2;
        for (const tinyxml2::XMLNode* n = e->Parent(); n && index >= 0; n = n->Parent()) {
            const tinyxml2::XMLElement* ancestor = n->ToElement();
            if (ancestor && CompoundMatches(selector.path[static_cast<size_t>(index)], ancestor)) --index;
        }
        return index < 0;
    }

    static bool CompoundMatches(const HTML::SimpleSelector& part, const tinyxml2::XMLElement* e) {
        // The parser lower-cases type selectors; SVG's names are camelCase.
        if (!part.tag.empty() && part.tag != "*" && Lower(StripNs(e->Name())) != part.tag) return false;
        if (!part.id.empty()) {
            const char* id = e->Attribute("id");
            if (!id || part.id != id) return false;
        }
        if (!part.classes.empty()) {
            std::set<std::string> have;
            if (const char* cls = e->Attribute("class")) {
                std::istringstream iss(cls);
                std::string c;
                while (iss >> c) have.insert(c);
            }
            for (const auto& c : part.classes) {
                if (!have.count(c)) return false;
            }
        }
        if (part.link && !(std::strcmp(StripNs(e->Name()), "a") == 0 &&
                           (e->Attribute("href") || e->Attribute("xlink:href")))) return false;
        for (const auto& pc : part.pseudos) {
            if (!PseudoMatches(pc, e)) return false;
        }
        for (const auto& attr : part.attributes) {
            if (!AttributeMatches(attr, e)) return false;
        }
        return true;
    }

    static bool PseudoMatches(const HTML::PseudoClass& pc, const tinyxml2::XMLElement* e) {
        if (pc.kind == HTML::PseudoClass::Kind::Root) return e->Parent() && e->Parent()->ToDocument();
        if (pc.kind == HTML::PseudoClass::Kind::Empty) {
            for (const tinyxml2::XMLNode* n = e->FirstChild(); n; n = n->NextSibling()) {
                if (n->ToElement() || (n->ToText() && *n->Value())) return false;
            }
            return true;
        }
        // The 1-based position among the element siblings (of the same name
        // for -of-type), from the first or the last, is a*n + b for an n >= 0.
        const tinyxml2::XMLNode* parent = e->Parent();
        if (!parent) return false;
        int index = 0, count = 0;
        for (const tinyxml2::XMLElement* sib = parent->FirstChildElement(); sib;
             sib = sib->NextSiblingElement()) {
            if (pc.ofType && std::strcmp(sib->Name(), e->Name()) != 0) continue;
            ++count;
            if (sib == e) index = count;
        }
        if (index == 0) return false;
        const int pos = pc.fromEnd ? count - index + 1 : index;
        if (pc.a == 0) return pos == pc.b;
        const int diff = pos - pc.b;
        return diff % pc.a == 0 && diff / pc.a >= 0;
    }

    static bool AttributeMatches(const HTML::AttributeSelector& attr, const tinyxml2::XMLElement* e) {
        // The parser lower-cases the name; SVG's attributes are camelCase.
        const char* value = nullptr;
        for (const tinyxml2::XMLAttribute* a = e->FirstAttribute(); a && !value; a = a->Next()) {
            if (Lower(a->Name()) == attr.name) value = a->Value();
        }
        if (!value) return false;
        if (attr.op == 0) return true;
        std::string have = value, want = attr.value;
        if (attr.ignoreCase) {
            have = Lower(have);
            want = Lower(want);
        }
        switch (attr.op) {
            case '=': return have == want;
            case '^': return !want.empty() && have.compare(0, want.size(), want) == 0;
            case '$': return !want.empty() && have.size() >= want.size() &&
                             have.compare(have.size() - want.size(), want.size(), want) == 0;
            case '*': return !want.empty() && have.find(want) != std::string::npos;
            case '|': return have == want || have.compare(0, want.size() + 1, want + "-") == 0;
            case '~': {
                std::istringstream iss(have);
                std::string word;
                while (iss >> word) {
                    if (word == want) return true;
                }
                return false;
            }
        }
        return false;
    }

    // ===== GRADIENTS =====

    void CollectGradients(const tinyxml2::XMLElement* e) {
        const char* name = StripNs(e->Name());
        if (std::strcmp(name, "linearGradient") == 0 ||
            std::strcmp(name, "radialGradient") == 0) {
            if (const char* id = e->Attribute("id")) ParseGradient(e, id);
        }
        for (const tinyxml2::XMLElement* child = e->FirstChildElement();
             child; child = child->NextSiblingElement()) {
            CollectGradients(child);
        }
    }

    void ParseGradient(const tinyxml2::XMLElement* e, const std::string& id) {
        bool linear = std::strcmp(StripNs(e->Name()), "linearGradient") == 0;

        std::vector<GradientStop> stops;
        for (const tinyxml2::XMLElement* st = e->FirstChildElement();
             st; st = st->NextSiblingElement()) {
            if (std::strcmp(StripNs(st->Name()), "stop") != 0) continue;
            GradientStop gs;
            std::string off = Prop(st, "offset");
            if (!off.empty()) {
                TryParseFloat(off, gs.position);
                if (off.back() == '%') gs.position /= 100.0;
            }
            std::string sc = Prop(st, "stop-color");
            gs.color = sc.empty() ? Color(0, 0, 0, 255) : ParseColorString(sc);
            std::string so = Prop(st, "stop-opacity");
            if (!so.empty()) {
                double alpha = 1.0;
                TryParseFloat(so, alpha);
                gs.color.a = static_cast<uint8_t>(
                        std::max(0.0, std::min(1.0, alpha)) * 255);
            }
            stops.push_back(gs);
        }
        // One level of href inheritance for the stop list.
        if (stops.empty()) {
            const char* href = e->Attribute("href");
            if (!href) href = e->Attribute("xlink:href");
            if (href && href[0] == '#') {
                auto it = gradients.find(href + 1);
                if (it != gradients.end()) {
                    if (const auto* lg = std::get_if<LinearGradientData>(&it->second))
                        stops = lg->Stops;
                    else if (const auto* rg = std::get_if<RadialGradientData>(&it->second))
                        stops = rg->Stops;
                }
            }
        }

        GradientUnits units = GradientUnits::ObjectBoundingBox;
        if (const char* u = e->Attribute("gradientUnits")) {
            if (std::strcmp(u, "userSpaceOnUse") == 0) units = GradientUnits::UserSpaceOnUse;
        }
        GradientSpreadMethod spread = GradientSpreadMethod::Pad;
        if (const char* sm = e->Attribute("spreadMethod")) {
            if (std::strcmp(sm, "reflect") == 0) spread = GradientSpreadMethod::Reflect;
            else if (std::strcmp(sm, "repeat") == 0) spread = GradientSpreadMethod::Repeat;
        }
        std::optional<Matrix3x3> gradTransform;
        if (const char* gt = e->Attribute("gradientTransform")) {
            gradTransform = ParseTransformString(gt);
        }

        if (linear) {
            LinearGradientData g;
            g.Start = Point2Dd(LengthAttr(e, "x1", 0), LengthAttr(e, "y1", 0));
            g.End = Point2Dd(LengthAttr(e, "x2", 1), LengthAttr(e, "y2", 0));
            g.Stops = std::move(stops);
            g.Units = units;
            g.SpreadMethod = spread;
            g.Transform = gradTransform;
            gradients[id] = GradientData(std::move(g));
        } else {
            RadialGradientData g;
            g.Center = Point2Dd(LengthAttr(e, "cx", 0.5), LengthAttr(e, "cy", 0.5));
            g.Radius = static_cast<float>(LengthAttr(e, "r", 0.5));
            g.FocalPoint = Point2Dd(LengthAttr(e, "fx", g.Center.x),
                                    LengthAttr(e, "fy", g.Center.y));
            g.Stops = std::move(stops);
            g.Units = units;
            g.SpreadMethod = spread;
            g.Transform = gradTransform;
            gradients[id] = GradientData(std::move(g));
        }
    }

    // ===== STYLE =====

    std::optional<FillData> ParsePaint(const std::string& v) {
        if (v.empty() || v == "inherit") return std::nullopt;
        if (v == "none" || v == "transparent") return FillData(std::monostate{});
        // SVG 2: inside a marker, the paint of the shape it is drawn on.
        if (v == "context-fill") return contextFill ? *contextFill : FillData(std::monostate{});
        if (v == "context-stroke") return contextStroke ? *contextStroke : FillData(std::monostate{});
        if (v.rfind("url(#", 0) == 0) {
            size_t end = v.find(')');
            std::string id = v.substr(5, end == std::string::npos ? std::string::npos
                                                                  : end - 5);
            auto it = gradients.find(id);
            if (it != gradients.end()) return FillData(it->second);
            return FillData(v);   // unresolved reference kept as a string
        }
        return FillData(ParseColorString(v));
    }

    static float ParseOpacity(const std::string& v, float fallback) {
        if (v.empty()) return fallback;
        double d = fallback;
        TryParseFloat(v, d);
        if (v.back() == '%') d /= 100.0;
        return static_cast<float>(std::max(0.0, std::min(1.0, d)));
    }

    // A stroke from its presentation properties, `prop` answering each by
    // name; false when there is none ("none", "inherit" or unset).
    bool ParseStroke(const std::function<std::string(const char*)>& prop, StrokeData& st) {
        const std::string strokeVal = prop("stroke");
        if (strokeVal.empty() || strokeVal == "none" || strokeVal == "inherit") return false;
        if (auto p = ParsePaint(strokeVal)) st.Fill = *p;
        const std::string sw = prop("stroke-width");
        if (!sw.empty()) st.Width = static_cast<float>(ParseLength(sw.c_str(), 1.0));
        const std::string cap = prop("stroke-linecap");
        if (cap == "round") st.LineCap = StrokeLineCap::Round;
        else if (cap == "square") st.LineCap = StrokeLineCap::Square;
        const std::string join = prop("stroke-linejoin");
        if (join == "round") st.LineJoin = StrokeLineJoin::Round;
        else if (join == "bevel") st.LineJoin = StrokeLineJoin::Bevel;
        const std::string ml = prop("stroke-miterlimit");
        if (!ml.empty()) TryParseFloat(ml, st.MiterLimit);
        std::string dash = prop("stroke-dasharray");
        if (!dash.empty() && dash != "none") {
            for (char& ch : dash) if (ch == ',') ch = ' ';
            std::istringstream iss(dash);
            double d;
            while (iss >> d) st.DashArray.push_back(d);
        }
        const std::string doff = prop("stroke-dashoffset");
        if (!doff.empty()) TryParseFloat(doff, st.DashOffset);
        st.Opacity = ParseOpacity(prop("stroke-opacity"), 1.0f);
        return true;
    }

    void ApplyStyle(const tinyxml2::XMLElement* e, VectorElement& out) {
        if (const char* id = e->Attribute("id")) out.Id = id;
        if (const char* cls = e->Attribute("class")) {
            std::istringstream iss(cls);
            std::string c;
            while (iss >> c) out.Classes.push_back(c);
        }
        if (const char* tr = e->Attribute("transform")) {
            Matrix3x3 m = ParseTransformString(tr);
            out.Transform = m;
        }

        VectorStyle& s = out.Style;
        if (auto f = ParsePaint(Prop(e, "fill"))) s.Fill = *f;
        const std::string strokeVal = Prop(e, "stroke");
        StrokeData st;
        if (ParseStroke([&](const char* name) { return Prop(e, name); }, st)) {
            s.Stroke = st;
        } else if (strokeVal == "none") {
            s.Stroke.reset();
            strokeNone.insert(&out);
        }
        // clip-path="url(#id)": the element is clipped to that <clipPath>.
        std::string clip = Prop(e, "clip-path");
        if (clip.rfind("url(#", 0) == 0) {
            const size_t end = clip.find(')');
            s.ClipPath = clip.substr(5, end == std::string::npos ? std::string::npos : end - 5);
        }
        s.Opacity = ParseOpacity(Prop(e, "opacity"), 1.0f);
        s.FillOpacity = ParseOpacity(Prop(e, "fill-opacity"), 1.0f);
        std::string display = Prop(e, "display");
        if (display == "none") s.Display = false;
        std::string visibility = Prop(e, "visibility");
        if (visibility == "hidden" || visibility == "collapse") s.Visible = false;
    }

    void ApplyTextStyle(const tinyxml2::XMLElement* e, VectorTextStyle& s) {
        std::string ff = Prop(e, "font-family");
        if (!ff.empty()) {
            // First family of the list, quotes stripped.
            size_t comma = ff.find(',');
            if (comma != std::string::npos) ff = ff.substr(0, comma);
            ff.erase(std::remove(ff.begin(), ff.end(), '\''), ff.end());
            ff.erase(std::remove(ff.begin(), ff.end(), '"'), ff.end());
            while (!ff.empty() && ff.back() == ' ') ff.pop_back();
            while (!ff.empty() && ff.front() == ' ') ff.erase(ff.begin());
            s.FontFamily = ff;
        }
        std::string fs = Prop(e, "font-size");
        if (!fs.empty()) s.FontSize = static_cast<float>(ParseLength(fs.c_str(), s.FontSize));
        std::string fw = Prop(e, "font-weight");
        if (!fw.empty()) {
            if (fw == "bold" || fw == "bolder") s.Weight = FontWeight::Bold;
            else if (fw == "normal") s.Weight = FontWeight::Normal;
            else {
                long n = std::strtol(fw.c_str(), nullptr, 10);
                if (n >= 800) s.Weight = FontWeight::ExtraBold;
                else if (n >= 600) s.Weight = FontWeight::Bold;
                else if (n > 0 && n <= 300) s.Weight = FontWeight::Light;
                else if (n > 300) s.Weight = FontWeight::Normal;
            }
        }
        std::string fst = Prop(e, "font-style");
        if (fst == "italic") s.Slant = FontSlant::Italic;
        else if (fst == "oblique") s.Slant = FontSlant::Oblique;
        else if (fst == "normal") s.Slant = FontSlant::Normal;
        std::string anchor = Prop(e, "text-anchor");
        if (anchor == "middle") s.Anchor = TextAnchor::Middle;
        else if (anchor == "end") s.Anchor = TextAnchor::End;
        else if (anchor == "start") s.Anchor = TextAnchor::Start;
        std::string deco = Prop(e, "text-decoration");
        if (!deco.empty()) {
            s.Underline = deco.find("underline") != std::string::npos;
            s.StrikeThrough = deco.find("line-through") != std::string::npos;
        }
        std::string ls = Prop(e, "letter-spacing");
        if (!ls.empty() && ls != "normal")
            s.LetterSpacing = static_cast<float>(ParseLength(ls.c_str(), 0));
    }

    // ===== NESTED SVG IMAGES =====

    // <image href="data:image/svg+xml;..."> as a group: the embedded drawing
    // read by a reader of its own, its page mapped onto the image box the
    // way preserveAspectRatio says, and its definitions (clip paths) moved
    // into this document under names that cannot collide.
    std::shared_ptr<VectorElement> ParseNestedSvg(const tinyxml2::XMLElement* e, const char* href,
                                                  VectorDocument& doc) {
        if (depth >= 8) {
            warn("SVG import: SVG images nested more than 8 deep are skipped");
            return nullptr;
        }
        const std::string uri = href;
        const size_t comma = uri.find(',');
        if (comma == std::string::npos) return nullptr;
        std::string data;
        if (uri.substr(0, comma).find(";base64") != std::string::npos) {
            const std::vector<uint8_t> bytes = Base64Decode(uri.substr(comma + 1));
            data.assign(bytes.begin(), bytes.end());
        } else {
            data = PercentDecode(uri.substr(comma + 1));
        }
        SvgReader inner(warn);
        inner.depth = depth + 1;
        auto nested = inner.Parse(data);
        if (!nested || nested->Size.width <= 0 || nested->Size.height <= 0) return nullptr;

        const double x = LengthAttr(e, "x", 0), y = LengthAttr(e, "y", 0);
        const double w = LengthAttr(e, "width", nested->Size.width);
        const double h = LengthAttr(e, "height", nested->Size.height);
        double sx = w / nested->Size.width, sy = h / nested->Size.height;
        double tx = x, ty = y;
        const char* par = e->Attribute("preserveAspectRatio");
        const std::string align = par ? par : "xMidYMid meet";
        if (align.rfind("none", 0) != 0) {
            const bool slice = align.find("slice") != std::string::npos;
            const double k = slice ? std::max(sx, sy) : std::min(sx, sy);
            tx += (w - nested->Size.width * k) * (align.find("xMin") != std::string::npos ? 0.0
                                                  : align.find("xMax") != std::string::npos ? 1.0 : 0.5);
            ty += (h - nested->Size.height * k) * (align.find("YMin") != std::string::npos ? 0.0
                                                   : align.find("YMax") != std::string::npos ? 1.0 : 0.5);
            sx = sy = k;
        }

        // Definitions keep their content, under a prefix unique to this image.
        const std::string prefix = "svgimg" + std::to_string(++nestedCount) + "-";
        for (auto& [id, def] : nested->Definitions) {
            if (!def) continue;
            def->Id = prefix + id;
            doc.AddDefinition(def->Id, def);
        }
        auto group = std::make_shared<VectorGroup>();
        Matrix3x3 m = Matrix3x3::Translate(tx, ty) * Matrix3x3::Scale(sx, sy);
        if (const char* tr = e->Attribute("transform")) m = ParseTransformString(tr) * m;
        group->Transform = m;
        std::function<void(VectorElement&)> rename = [&](VectorElement& el) {
            if (el.Style.ClipPath && !el.Style.ClipPath->empty()) el.Style.ClipPath = prefix + *el.Style.ClipPath;
            if (auto* g = dynamic_cast<VectorGroup*>(&el))
                for (auto& c : g->Children) if (c) rename(*c);
            if (auto* cp = dynamic_cast<VectorClipPath*>(&el))
                for (auto& c : cp->Data.Elements) if (c) rename(*c);
        };
        for (auto& [id, def] : nested->Definitions) if (def) rename(*def);
        for (auto& layer : nested->Layers) {
            if (!layer) continue;
            rename(*layer);
            auto sub = std::make_shared<VectorGroup>();
            sub->Style = layer->Style;
            sub->Children = std::move(layer->Children);
            for (auto& c : sub->Children) if (c) c->Parent.reset();
            group->AddChild(sub);
        }
        // The <image>'s own presentation (opacity, clip-path, visibility).
        const Matrix3x3 placed = *group->Transform;
        ApplyStyle(e, *group);
        group->Transform = placed;   // ApplyStyle read `transform`; it is folded in above
        return group;
    }

    static std::string PercentDecode(const std::string& in) {
        std::string out;
        out.reserve(in.size());
        for (size_t i = 0; i < in.size(); ++i) {
            if (in[i] == '%' && i + 2 < in.size() && std::isxdigit(static_cast<unsigned char>(in[i + 1])) &&
                std::isxdigit(static_cast<unsigned char>(in[i + 2]))) {
                out.push_back(static_cast<char>(std::stoi(in.substr(i + 1, 2), nullptr, 16)));
                i += 2;
            } else {
                out.push_back(in[i]);
            }
        }
        return out;
    }

    // ===== MARKERS =====

    // Nothing in the model draws a <marker> by reference, so marker-start,
    // -mid and -end are drawn here: each marker's content is read again as
    // shapes at every vertex it applies to, placed, turned and scaled as SVG 2
    // says, and the shape and its markers become one group, so an arrow is
    // one object to select and move. `context-fill` / `context-stroke` in the
    // content take the shape's paint.

    void CollectMarkers(const tinyxml2::XMLElement* e) {
        if (std::strcmp(StripNs(e->Name()), "marker") == 0) {
            if (const char* id = e->Attribute("id")) markers.emplace(id, e);
        }
        for (const tinyxml2::XMLElement* child = e->FirstChildElement();
             child; child = child->NextSiblingElement()) {
            CollectMarkers(child);
        }
    }

    // An inherited property (the marker properties, stroke-width, fill and
    // stroke all are): the element's own value, or its nearest ancestor's.
    std::string InheritedProp(const tinyxml2::XMLElement* e, const char* name) {
        for (const tinyxml2::XMLElement* at = e; at;
             at = at->Parent() ? at->Parent()->ToElement() : nullptr) {
            std::string v = Prop(at, name);
            if (!v.empty() && v != "inherit") return v;
        }
        return {};
    }

    // The <marker> a `url(#id)` value names; null for "none" or a missing id.
    const tinyxml2::XMLElement* MarkerReference(const std::string& v) {
        if (v.rfind("url(", 0) != 0) return nullptr;
        std::string id = v.substr(4, v.find(')') == std::string::npos ? std::string::npos : v.find(')') - 4);
        id.erase(std::remove(id.begin(), id.end(), '"'), id.end());
        id.erase(std::remove(id.begin(), id.end(), '\''), id.end());
        if (id.empty() || id[0] != '#') return nullptr;
        auto it = markers.find(id.substr(1));
        return it == markers.end() ? nullptr : it->second;
    }

    // An angle with its unit (deg by default, rad, grad, turn) in degrees.
    static double ParseAngle(const std::string& s) {
        const char* begin = s.c_str();
        while (*begin && std::isspace(static_cast<unsigned char>(*begin))) ++begin;
        double v = 0;
        const char* end = ParseFloatClassic(begin, begin + std::strlen(begin), v);
        if (end == begin) return 0;
        if (std::strncmp(end, "rad", 3) == 0) v *= 180.0 / M_PI;
        else if (std::strncmp(end, "grad", 4) == 0) v *= 0.9;
        else if (std::strncmp(end, "turn", 4) == 0) v *= 360.0;
        return v;
    }

    // One segment of a path: where it ends, and its direction as it leaves
    // the previous vertex and as it arrives at this one.
    struct PathSegment {
        Point2Dd End, DirStart, DirEnd;
    };
    struct PathSubpath {
        Point2Dd Start;
        std::vector<PathSegment> Segments;
        bool Closed = false;
    };

    // The first of the candidate directions that has a length.
    static Point2Dd FirstDirection(std::initializer_list<Point2Dd> candidates) {
        for (const Point2Dd& d : candidates) {
            if (d.x != 0 || d.y != 0) return d;
        }
        return Point2Dd(0, 0);
    }

    // The directions an elliptical arc leaves `p0` and arrives at `p1` in,
    // through its centre parameterisation (SVG implementation notes B.2.4).
    static void ArcDirections(const Point2Dd& p0, double rx, double ry, double phiDeg, bool large,
                              bool sweep, const Point2Dd& p1, Point2Dd& d0, Point2Dd& d1) {
        rx = std::fabs(rx);
        ry = std::fabs(ry);
        if (rx == 0 || ry == 0 || p0 == p1) {   // drawn as a straight line
            d0 = d1 = p1 - p0;
            return;
        }
        const double phi = phiDeg * M_PI / 180.0, cs = std::cos(phi), sn = std::sin(phi);
        const double hx = (p0.x - p1.x) / 2, hy = (p0.y - p1.y) / 2;
        const double x1 = cs * hx + sn * hy, y1 = -sn * hx + cs * hy;
        const double lambda = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
        if (lambda > 1) {
            rx *= std::sqrt(lambda);
            ry *= std::sqrt(lambda);
        }
        const double num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
        const double den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
        double k = den > 0 ? std::sqrt(std::max(0.0, num / den)) : 0;
        if (large == sweep) k = -k;
        const double cx = k * rx * y1 / ry, cy = -k * ry * x1 / rx;
        const double t0 = std::atan2((y1 - cy) / ry, (x1 - cx) / rx);
        const double t1 = std::atan2((-y1 - cy) / ry, (-x1 - cx) / rx);
        auto tangent = [&](double t) {
            double dx = -rx * std::sin(t), dy = ry * std::cos(t);
            if (!sweep) {
                dx = -dx;
                dy = -dy;
            }
            return Point2Dd(cs * dx - sn * dy, sn * dx + cs * dy);
        };
        d0 = tangent(t0);
        d1 = tangent(t1);
    }

    // The path's subpaths in absolute coordinates, every command kind
    // reduced to its end point and the directions at its two ends.
    static std::vector<PathSubpath> PathSubpaths(const PathData& path) {
        std::vector<PathSubpath> out;
        Point2Dd cur(0, 0), start(0, 0), lastCubic(0, 0), lastQuad(0, 0);
        bool cubicBefore = false, quadBefore = false;
        auto segment = [&](const Point2Dd& end, const Point2Dd& d0, const Point2Dd& d1) {
            if (out.empty() || out.back().Closed) out.push_back({cur, {}, false});   // no moveto after Z
            out.back().Segments.push_back({end, d0, d1});
            cur = end;
        };
        for (const PathCommand& c : path.commands) {
            const std::vector<float>& p = c.Parameters;
            auto at = [&](size_t i) {
                return c.Relative ? Point2Dd(cur.x + p[i], cur.y + p[i + 1]) : Point2Dd(p[i], p[i + 1]);
            };
            size_t arity = 2;
            switch (c.Type) {
                case PathCommandType::HorizontalLineTo: case PathCommandType::VerticalLineTo: arity = 1; break;
                case PathCommandType::CurveTo: arity = 6; break;
                case PathCommandType::SmoothCurveTo: case PathCommandType::QuadraticTo: arity = 4; break;
                case PathCommandType::ArcTo: arity = 7; break;
                case PathCommandType::ClosePath: arity = 0; break;
                default: break;
            }
            if (c.Type == PathCommandType::ClosePath) {
                if (!out.empty() && !out.back().Closed) {
                    segment(start, start - cur, start - cur);
                    out.back().Closed = true;
                }
                cur = start;
                cubicBefore = quadBefore = false;
                continue;
            }
            for (size_t i = 0; i + arity <= p.size(); i += arity) {
                bool cubic = false, quad = false;
                switch (c.Type) {
                    case PathCommandType::MoveTo:
                        if (i == 0) {   // later pairs are implicit linetos
                            cur = start = at(0);
                            out.push_back({cur, {}, false});
                        } else {
                            const Point2Dd end = at(i);
                            segment(end, end - cur, end - cur);
                        }
                        break;
                    case PathCommandType::LineTo: {
                        const Point2Dd end = at(i);
                        segment(end, end - cur, end - cur);
                        break;
                    }
                    case PathCommandType::HorizontalLineTo: {
                        const Point2Dd end(c.Relative ? cur.x + p[i] : p[i], cur.y);
                        segment(end, end - cur, end - cur);
                        break;
                    }
                    case PathCommandType::VerticalLineTo: {
                        const Point2Dd end(cur.x, c.Relative ? cur.y + p[i] : p[i]);
                        segment(end, end - cur, end - cur);
                        break;
                    }
                    case PathCommandType::CurveTo:
                    case PathCommandType::SmoothCurveTo: {
                        const bool smooth = c.Type == PathCommandType::SmoothCurveTo;
                        const Point2Dd c1 = smooth ? (cubicBefore ? cur * 2.0f - lastCubic : cur) : at(i);
                        const Point2Dd c2 = at(smooth ? i : i + 2), end = at(smooth ? i + 2 : i + 4);
                        segment(end, FirstDirection({c1 - cur, c2 - cur, end - cur}),
                                FirstDirection({end - c2, end - c1, end - cur}));
                        lastCubic = c2;
                        cubic = true;
                        break;
                    }
                    case PathCommandType::QuadraticTo:
                    case PathCommandType::SmoothQuadraticTo: {
                        const bool smooth = c.Type == PathCommandType::SmoothQuadraticTo;
                        const Point2Dd q = smooth ? (quadBefore ? cur * 2.0f - lastQuad : cur) : at(i);
                        const Point2Dd end = at(smooth ? i : i + 2);
                        segment(end, FirstDirection({q - cur, end - cur}), FirstDirection({end - q, end - cur}));
                        lastQuad = q;
                        quad = true;
                        break;
                    }
                    case PathCommandType::ArcTo: {
                        const Point2Dd end = at(i + 5);
                        Point2Dd d0, d1;
                        ArcDirections(cur, p[i], p[i + 1], p[i + 2], p[i + 3] != 0, p[i + 4] != 0, end, d0, d1);
                        segment(end, d0, d1);
                        break;
                    }
                    default:
                        break;
                }
                cubicBefore = cubic;
                quadBefore = quad;
            }
        }
        return out;
    }

    struct MarkerVertex {
        Point2Dd At;
        double Angle = 0;   // degrees: where orient="auto" turns the marker
    };

    // The direction half-way between the incoming and outgoing ones; at an
    // open end, the one there is.
    static double BisectorDegrees(const Point2Dd* in, const Point2Dd* out) {
        auto usable = [](const Point2Dd* d) { return d && (d->x != 0 || d->y != 0); };
        double a = 0;
        if (usable(in) && usable(out)) {
            const double i = std::atan2(in->y, in->x);
            double o = std::atan2(out->y, out->x);
            if (o - i > M_PI) o -= 2 * M_PI;
            else if (i - o > M_PI) o += 2 * M_PI;
            a = (i + o) / 2;
        } else if (usable(in)) {
            a = std::atan2(in->y, in->x);
        } else if (usable(out)) {
            a = std::atan2(out->y, out->x);
        }
        return a * 180.0 / M_PI;
    }

    // SVG 2's path vertices: each subpath's start and every segment's end
    // (a closepath's at the subpath's start), the first carrying
    // marker-start, the last marker-end, the rest marker-mid. A closed
    // subpath's ends turn to the bisector of its first and last segments.
    static std::vector<MarkerVertex> MarkerVertices(const PathData& path) {
        std::vector<MarkerVertex> out;
        for (const PathSubpath& sp : PathSubpaths(path)) {
            const size_t n = sp.Segments.size();
            for (size_t i = 0; i <= n; ++i) {
                const Point2Dd* in = i > 0 ? &sp.Segments[i - 1].DirEnd
                                           : (sp.Closed && n ? &sp.Segments[n - 1].DirEnd : nullptr);
                const Point2Dd* dirOut = i < n ? &sp.Segments[i].DirStart
                                               : (sp.Closed && n ? &sp.Segments[0].DirStart : nullptr);
                out.push_back({i == 0 ? sp.Start : sp.Segments[i - 1].End, BisectorDegrees(in, dirOut)});
            }
        }
        return out;
    }

    // The clip a marker's viewport makes, in its content's coordinates -
    // the same for every place the marker is drawn, so made once.
    std::string MarkerClip(const tinyxml2::XMLElement* m, const Rect2Dd& viewport, VectorDocument& doc) {
        auto it = markerClips.find(m);
        if (it != markerClips.end()) return it->second;
        auto clip = std::make_shared<VectorClipPath>();
        clip->Id = "svgmarker" + std::to_string(markerClips.size() + 1) + "-viewport";
        auto box = std::make_shared<VectorRect>();
        box->Bounds = viewport;
        clip->Data.Elements.push_back(box);
        doc.AddDefinition(clip->Id, clip);
        markerClips.emplace(m, clip->Id);
        return clip->Id;
    }

    // One drawing of marker `m` at `v`: its content as a group, mapped from
    // the marker's viewBox onto markerWidth x markerHeight, scaled by the
    // stroke width (markerUnits="strokeWidth", the default), turned by
    // `orient`, with (refX, refY) on the vertex.
    std::shared_ptr<VectorGroup> MarkerInstance(const tinyxml2::XMLElement* m, const MarkerVertex& v, bool start,
                                                double strokeWidth, const std::optional<FillData>& fill,
                                                const std::optional<FillData>& stroke, VectorDocument& doc) {
        if (expandingMarkers.count(m)) return nullptr;
        const double mw = LengthAttr(m, "markerWidth", 3), mh = LengthAttr(m, "markerHeight", 3);
        if (mw <= 0 || mh <= 0) return nullptr;   // a zero-sized viewport draws nothing

        double angle = 0;
        const std::string orient = Prop(m, "orient");
        if (orient == "auto") angle = v.Angle;
        else if (orient == "auto-start-reverse") angle = start ? v.Angle + 180 : v.Angle;
        else if (!orient.empty()) angle = ParseAngle(orient);
        const double units = Prop(m, "markerUnits") == "userSpaceOnUse" ? 1.0 : strokeWidth;

        double sx = 1, sy = 1;
        Rect2Dd viewport{0, 0, mw, mh};   // in content coordinates
        double vb[4] = {0, 0, 0, 0};
        if (const char* attr = m->Attribute("viewBox")) {
            std::string list = attr;
            for (char& ch : list) if (ch == ',') ch = ' ';
            std::istringstream iss(list);
            iss.imbue(std::locale::classic());
            if (iss >> vb[0] >> vb[1] >> vb[2] >> vb[3] && vb[2] > 0 && vb[3] > 0) {
                sx = mw / vb[2];
                sy = mh / vb[3];
                double tx = 0, ty = 0;
                const char* par = m->Attribute("preserveAspectRatio");
                const std::string align = par ? par : "xMidYMid meet";
                if (align.rfind("none", 0) != 0) {
                    const double k = align.find("slice") != std::string::npos ? std::max(sx, sy) : std::min(sx, sy);
                    tx = (mw - vb[2] * k) * (align.find("xMin") != std::string::npos ? 0.0
                                             : align.find("xMax") != std::string::npos ? 1.0 : 0.5);
                    ty = (mh - vb[3] * k) * (align.find("YMin") != std::string::npos ? 0.0
                                             : align.find("YMax") != std::string::npos ? 1.0 : 0.5);
                    sx = sy = k;
                }
                viewport = Rect2Dd{vb[0] - tx / sx, vb[1] - ty / sy, mw / sx, mh / sy};
            }
        }

        // The content inherits from the marker, not from the shape it is on.
        auto inst = std::make_shared<VectorGroup>();
        ApplyStyle(m, *inst);
        inst->Id.clear();
        inst->Classes.clear();
        inst->Transform.reset();
        inst->Style.Display = true;   // display does not apply to <marker>
        if (!inst->Style.Fill) inst->Style.Fill = Color(0, 0, 0, 255);
        if (!inst->Style.Stroke) strokeNone.insert(inst.get());

        expandingMarkers.insert(m);
        const auto outerFill = contextFill, outerStroke = contextStroke;
        contextFill = fill;
        contextStroke = stroke;
        for (const tinyxml2::XMLElement* child = m->FirstChildElement();
             child; child = child->NextSiblingElement()) {
            ParseNode(child, doc, inst.get());
        }
        contextFill = outerFill;
        contextStroke = outerStroke;
        expandingMarkers.erase(m);
        if (inst->Children.empty()) return nullptr;

        // overflow is hidden on a marker unless it says otherwise; a clip is
        // only made when the content actually reaches past the viewport.
        const std::string overflow = Prop(m, "overflow");
        if (overflow != "visible" && overflow != "auto") {
            const Rect2Dd box = inst->GetBoundingBox();
            const double eps = 1e-6 * std::max(viewport.width, viewport.height);
            if (box.x < viewport.x - eps || box.y < viewport.y - eps ||
                box.x + box.width > viewport.x + viewport.width + eps ||
                box.y + box.height > viewport.y + viewport.height + eps) {
                inst->Style.ClipPath = MarkerClip(m, viewport, doc);
            }
        }
        inst->Transform = Matrix3x3::Translate(v.At.x, v.At.y) * Matrix3x3::RotateDegrees(angle) *
                          Matrix3x3::Scale(units * sx, units * sy) *
                          Matrix3x3::Translate(-LengthAttr(m, "refX", 0), -LengthAttr(m, "refY", 0));
        return inst;
    }

    // A <marker> the writer made for a line-gallery arrowhead at this end
    // (data-ultracanvas-arrowhead / -scale / -end), read into `out`.
    static bool GalleryArrowhead(const tinyxml2::XMLElement* m, bool atStart, ArrowheadData& out) {
        if (!m) return false;
        const char* name = m->Attribute("data-ultracanvas-arrowhead");
        const char* end = m->Attribute("data-ultracanvas-end");
        if (!name || !end || std::strcmp(end, atStart ? "start" : "end") != 0) return false;
        for (const auto& n : kArrowheadNames) {
            if (std::strcmp(n.Name, name) != 0) continue;
            ArrowheadData arrow;
            arrow.Kind = n.Kind;
            if (const char* scale = m->Attribute("data-ultracanvas-scale")) {
                float v = 1.0f;
                if (TryParseFloat(scale, v) && v > 0) arrow.Scale = v;
            }
            out = arrow;
            return true;
        }
        return false;
    }

    // `shape` with the markers its marker properties ask for, as a group
    // (the shape first, then marker-start, the marker-mids and marker-end,
    // which is SVG's drawing order) - or `shape` itself when there are none.
    std::shared_ptr<VectorElement> WithMarkers(const tinyxml2::XMLElement* e,
                                               std::shared_ptr<VectorElement> shape, VectorDocument& doc) {
        if (markers.empty() || clipDepth > 0) return shape;
        const std::string all = InheritedProp(e, "marker");   // the CSS shorthand
        const tinyxml2::XMLElement* refs[3] = {nullptr, nullptr, nullptr};
        const char* names[3] = {"marker-start", "marker-mid", "marker-end"};
        bool any = false;
        for (int i = 0; i < 3; ++i) {
            const std::string v = InheritedProp(e, names[i]);
            refs[i] = MarkerReference(v.empty() ? all : v);
            any = any || refs[i];
        }
        if (!any) return shape;
        PathData outline;
        if (!BuildOutlinePath(*shape, outline)) return shape;

        // This writer's own arrowheads come back as what they were: the line
        // gallery's StartArrow / EndArrow on the stroke, editable as such.
        // Only where the renderer would draw them (the path's ends open) and
        // only in the slot each was written for.
        Point2Dd start, startDir, end, endDir;
        if (shape->Style.Stroke && PathEndpoints(outline, start, startDir, end, endDir)) {
            if (GalleryArrowhead(refs[0], true, shape->Style.Stroke->StartArrow)) refs[0] = nullptr;
            if (GalleryArrowhead(refs[2], false, shape->Style.Stroke->EndArrow)) refs[2] = nullptr;
            if (!refs[0] && !refs[1] && !refs[2]) return shape;
        }
        const std::vector<MarkerVertex> vertices = MarkerVertices(outline);
        if (vertices.empty()) return shape;

        const std::string sw = InheritedProp(e, "stroke-width");
        const double strokeWidth = sw.empty() ? 1.0 : ParseLength(sw.c_str(), 1.0);
        const std::string fillVal = InheritedProp(e, "fill"), strokeVal = InheritedProp(e, "stroke");
        const std::optional<FillData> fill = fillVal.empty() ? FillData(Color(0, 0, 0, 255)) : ParsePaint(fillVal);
        const std::optional<FillData> stroke = strokeVal.empty() ? FillData(std::monostate{}) : ParsePaint(strokeVal);

        std::vector<std::shared_ptr<VectorGroup>> drawn;
        auto place = [&](const tinyxml2::XMLElement* m, const MarkerVertex& v, bool start) {
            if (!m) return;
            if (auto inst = MarkerInstance(m, v, start, strokeWidth, fill, stroke, doc)) drawn.push_back(inst);
        };
        place(refs[0], vertices.front(), true);
        for (size_t i = 1; i + 1 < vertices.size(); ++i) place(refs[1], vertices[i], false);
        place(refs[2], vertices.back(), false);
        if (drawn.empty()) return shape;

        // What applies to the element as a whole moves to the group: its
        // place, opacity, clip and whether it shows.
        auto group = std::make_shared<VectorGroup>();
        group->Transform = shape->Transform;
        shape->Transform.reset();
        group->Style.Opacity = shape->Style.Opacity;
        shape->Style.Opacity = 1.0f;
        group->Style.ClipPath = shape->Style.ClipPath;
        shape->Style.ClipPath.reset();
        group->Style.Display = shape->Style.Display;
        shape->Style.Display = true;
        group->AddChild(shape);
        for (auto& inst : drawn) group->AddChild(inst);
        return group;
    }

    // ===== WIDTH PROFILES AND BRUSHES =====

    // A shape the writer drew as its width profile or brush: the group's
    // first child is the shape, and its stroke comes back from the group's
    // data - the stroke's properties, the profile, and the brush with its
    // stamp taken back out of <defs>. What the group draws besides is the
    // stroke's picture and is left out. Null when the data does not hold
    // together (a stamp that is not there), so the group is read as drawn.
    std::shared_ptr<VectorElement> RebuildLineGallery(const tinyxml2::XMLElement* g, const char* strokeDecl,
                                                      VectorDocument& doc) {
        const tinyxml2::XMLElement* first = g->FirstChildElement();
        if (!first) return nullptr;
        std::map<std::string, std::string> props;
        for (const auto& d : HTML::StyleSheet::ParseDeclarationList(strokeDecl)) props[d.property] = d.value;
        auto prop = [&](const char* n) {
            auto it = props.find(n);
            return it == props.end() ? std::string() : it->second;
        };
        StrokeData st;
        if (!ParseStroke(prop, st)) return nullptr;
        GalleryArrowhead(MarkerReference(prop("marker-start")), true, st.StartArrow);
        GalleryArrowhead(MarkerReference(prop("marker-end")), false, st.EndArrow);
        if (const char* p = g->Attribute("data-ultracanvas-width-profile")) {
            std::istringstream iss(p);
            std::string t, f;
            while (iss >> t >> f) {
                WidthSample w;
                if (TryParseFloat(t, w.T) && TryParseFloat(f, w.Factor)) st.WidthProfile.push_back(w);
            }
        }
        std::string stampId;
        if (const char* b = g->Attribute("data-ultracanvas-brush")) {
            std::istringstream iss(b);
            std::string spacing, scale, rotate;
            iss >> stampId >> spacing >> scale >> rotate;
            auto stamp = std::dynamic_pointer_cast<VectorGroup>(doc.GetDefinition(stampId));
            if (!stamp) return nullptr;
            BrushData brush;
            brush.Stamp = stamp;
            TryParseFloat(spacing, brush.Spacing);
            TryParseFloat(scale, brush.Scale);
            brush.Rotate = rotate != "0";
            st.Brush = brush;
        }

        auto shape = ParseShape(first, doc);
        if (!shape) return nullptr;
        if (!stampId.empty()) {
            // The stamp lives in the stroke now, not among the definitions;
            // its unpainted parts are black, as in <defs>.
            doc.Definitions.erase(stampId);
            st.Brush->Stamp->Id.clear();
            ResolveInheritedPaint(*st.Brush->Stamp, FillData(Color(0, 0, 0, 255)), std::nullopt);
        }
        shape->Style.Stroke = st;
        // The group's own place and presence are the shape's.
        auto frame = std::make_shared<VectorGroup>();
        ApplyStyle(g, *frame);
        strokeNone.erase(frame.get());
        shape->Id = frame->Id;
        shape->Classes = frame->Classes;
        shape->Transform = frame->Transform;
        shape->Style.Opacity = frame->Style.Opacity;
        shape->Style.Visible = frame->Style.Visible;
        shape->Style.Display = frame->Style.Display;
        return shape;
    }

    // ===== ELEMENTS =====

    void ParseNode(const tinyxml2::XMLElement* e, VectorDocument& doc,
                   VectorGroup* parent) {
        const char* name = StripNs(e->Name());

        if (std::strcmp(name, "clipPath") == 0) {
            // A definition wherever it appears; it draws nothing itself.
            auto clip = ParseShape(e, doc);
            if (clip && !clip->Id.empty()) doc.AddDefinition(clip->Id, clip);
            return;
        }
        if (std::strcmp(name, "defs") == 0) {
            for (const tinyxml2::XMLElement* child = e->FirstChildElement();
                 child; child = child->NextSiblingElement()) {
                const char* childName = StripNs(child->Name());
                if (std::strcmp(childName, "linearGradient") == 0 ||
                    std::strcmp(childName, "radialGradient") == 0 ||
                    std::strcmp(childName, "style") == 0 ||
                    std::strcmp(childName, "marker") == 0) {
                    continue;   // collected in the pre-passes
                }
                auto el = ParseShape(child, doc);
                if (el && !el->Id.empty()) doc.AddDefinition(el->Id, el);
            }
            return;
        }
        if (std::strcmp(name, "title") == 0) {
            if (const char* t = e->GetText()) doc.Title = t;
            return;
        }
        if (std::strcmp(name, "desc") == 0) {
            if (const char* t = e->GetText()) doc.Description = t;
            return;
        }
        if (std::strcmp(name, "style") == 0 ||
            std::strcmp(name, "marker") == 0 ||
            std::strcmp(name, "linearGradient") == 0 ||
            std::strcmp(name, "radialGradient") == 0 ||
            std::strcmp(name, "metadata") == 0) {
            return;   // style sheets, markers and gradients were collected in the pre-passes
        }

        auto el = ParseShape(e, doc);
        if (el) parent->AddChild(el);
    }

    std::shared_ptr<VectorElement> ParseShape(const tinyxml2::XMLElement* e,
                                              VectorDocument& doc) {
        const char* name = StripNs(e->Name());

        if (std::strcmp(name, "rect") == 0) {
            auto r = std::make_shared<VectorRect>();
            r->Bounds = Rect2Dd{GeometryLength(e, "x", 0), GeometryLength(e, "y", 0),
                                GeometryLength(e, "width", 0), GeometryLength(e, "height", 0)};
            r->RadiusX = static_cast<float>(GeometryLength(e, "rx", 0));
            r->RadiusY = static_cast<float>(GeometryLength(e, "ry", r->RadiusX));
            if (r->RadiusX <= 0 && r->RadiusY > 0) r->RadiusX = r->RadiusY;
            if (r->RadiusX > 0) r->Type = VectorElementType::RoundedRectangle;
            ApplyStyle(e, *r);
            return r;
        }
        if (std::strcmp(name, "circle") == 0) {
            auto c = std::make_shared<VectorCircle>();
            c->Center = Point2Dd(GeometryLength(e, "cx", 0), GeometryLength(e, "cy", 0));
            c->Radius = static_cast<float>(GeometryLength(e, "r", 0));
            ApplyStyle(e, *c);
            return c;
        }
        if (std::strcmp(name, "ellipse") == 0) {
            auto el = std::make_shared<VectorEllipse>();
            el->Center = Point2Dd(GeometryLength(e, "cx", 0), GeometryLength(e, "cy", 0));
            el->RadiusX = static_cast<float>(GeometryLength(e, "rx", 0));
            el->RadiusY = static_cast<float>(GeometryLength(e, "ry", 0));
            ApplyStyle(e, *el);
            return el;
        }
        if (std::strcmp(name, "line") == 0) {
            auto ln = std::make_shared<VectorLine>();
            ln->Start = Point2Dd(LengthAttr(e, "x1", 0), LengthAttr(e, "y1", 0));
            ln->End = Point2Dd(LengthAttr(e, "x2", 0), LengthAttr(e, "y2", 0));
            ApplyStyle(e, *ln);
            return WithMarkers(e, ln, doc);
        }
        if (std::strcmp(name, "polyline") == 0 || std::strcmp(name, "polygon") == 0) {
            std::vector<Point2Dd> pts;
            if (const char* p = e->Attribute("points")) {
                std::string str = p;
                for (char& ch : str) if (ch == ',') ch = ' ';
                std::istringstream iss(str);
                double x, y;
                while (iss >> x >> y) pts.emplace_back(x, y);
            }
            if (std::strcmp(name, "polyline") == 0) {
                auto pl = std::make_shared<VectorPolyline>();
                pl->Points = std::move(pts);
                ApplyStyle(e, *pl);
                return WithMarkers(e, pl, doc);
            }
            auto pg = std::make_shared<VectorPolygon>();
            pg->Points = std::move(pts);
            ApplyStyle(e, *pg);
            return WithMarkers(e, pg, doc);
        }
        if (std::strcmp(name, "path") == 0) {
            auto p = std::make_shared<VectorPath>();
            if (const char* d = e->Attribute("d")) p->Path = ParsePathString(d);
            ApplyStyle(e, *p);
            return WithMarkers(e, p, doc);
        }
        if (std::strcmp(name, "text") == 0) {
            return ParseText(e);
        }
        if (std::strcmp(name, "g") == 0 || std::strcmp(name, "a") == 0) {
            if (const char* stroke = e->Attribute("data-ultracanvas-stroke")) {
                if (auto shape = RebuildLineGallery(e, stroke, doc)) return shape;
            }
            auto g = std::make_shared<VectorGroup>();
            ApplyStyle(e, *g);
            for (const tinyxml2::XMLElement* child = e->FirstChildElement();
                 child; child = child->NextSiblingElement()) {
                ParseNode(child, doc, g.get());
            }
            return g;
        }
        if (std::strcmp(name, "symbol") == 0) {
            auto sym = std::make_shared<VectorSymbol>();
            ApplyStyle(e, *sym);
            if (const char* v = e->Attribute("viewBox")) {
                std::istringstream iss(v);
                double a, b, c, d;
                if (iss >> a >> b >> c >> d) sym->ViewBox = Rect2Dd{a, b, c, d};
            }
            for (const tinyxml2::XMLElement* child = e->FirstChildElement();
                 child; child = child->NextSiblingElement()) {
                auto el = ParseShape(child, doc);
                if (el) sym->AddChild(el);
            }
            return sym;
        }
        if (std::strcmp(name, "use") == 0) {
            auto u = std::make_shared<VectorUse>();
            const char* href = e->Attribute("href");
            if (!href) href = e->Attribute("xlink:href");
            if (href) u->Reference = (href[0] == '#') ? href + 1 : href;
            u->Position = Point2Dd(LengthAttr(e, "x", 0), LengthAttr(e, "y", 0));
            u->Size = Size2Dd{LengthAttr(e, "width", 0), LengthAttr(e, "height", 0)};
            ApplyStyle(e, *u);
            return u;
        }
        if (std::strcmp(name, "clipPath") == 0) {
            auto clip = std::make_shared<VectorClipPath>();
            if (const char* id = e->Attribute("id")) clip->Id = id;
            if (Prop(e, "clip-rule") == "evenodd") clip->Data.ClipRule = VectorStorage::FillRule::EvenOdd;
            ++clipDepth;   // a clip is the shapes' geometry; markers take no part
            for (const tinyxml2::XMLElement* child = e->FirstChildElement();
                 child; child = child->NextSiblingElement()) {
                auto el = ParseShape(child, doc);
                if (!el) continue;
                // A clip shape's own clip-rule overrides the container's.
                if (Prop(child, "clip-rule") == "evenodd") clip->Data.ClipRule = VectorStorage::FillRule::EvenOdd;
                clip->Data.Elements.push_back(el);
            }
            --clipDepth;
            return clip;
        }

        if (std::strcmp(name, "image") == 0) {
            const char* href = e->Attribute("href");
            if (!href) href = e->Attribute("xlink:href");
            // An image that is itself SVG (libcdr places CorelDRAW PowerClip
            // contents this way) is read as drawing, not pixels, so it stays
            // editable and sharp at any zoom.
            if (href && std::strncmp(href, "data:image/svg+xml", 18) == 0) {
                if (auto nested = ParseNestedSvg(e, href, doc)) return nested;
            }
            auto im = std::make_shared<VectorImage>();
            im->Bounds = Rect2Dd{LengthAttr(e, "x", 0), LengthAttr(e, "y", 0),
                                 LengthAttr(e, "width", 0), LengthAttr(e, "height", 0)};
            if (href) im->Source = href;
            ApplyStyle(e, *im);
            return im;
        }

        warn(std::string("SVG import: <") + name + "> is not supported, skipped");
        return nullptr;
    }

    std::shared_ptr<VectorText> ParseText(const tinyxml2::XMLElement* e) {
        auto t = std::make_shared<VectorText>();
        t->Position = Point2Dd(LengthAttr(e, "x", 0), LengthAttr(e, "y", 0));
        ApplyStyle(e, *t);
        ApplyTextStyle(e, t->BaseStyle);

        for (const tinyxml2::XMLNode* node = e->FirstChild(); node;
             node = node->NextSibling()) {
            if (const tinyxml2::XMLText* txt = node->ToText()) {
                TextSpanData span;
                span.Text = txt->Value();
                span.Style = t->BaseStyle;
                t->Spans.push_back(std::move(span));
            } else if (const tinyxml2::XMLElement* child = node->ToElement()) {
                if (std::strcmp(StripNs(child->Name()), "tspan") != 0) continue;
                TextSpanData span;
                if (const char* txt = child->GetText()) span.Text = txt;
                span.Style = t->BaseStyle;
                ApplyTextStyle(child, span.Style);
                if (child->Attribute("x") || child->Attribute("y")) {
                    span.Position = Point2Dd(
                            LengthAttr(child, "x", t->Position.x),
                            LengthAttr(child, "y", t->Position.y));
                }
                t->Spans.push_back(std::move(span));
            }
        }
        return t;
    }
};

}   // anonymous namespace

// ===== PUBLIC INTERFACE =====

FormatCapabilities SVGConverter::GetCapabilities() const {
    FormatCapabilities caps;
    caps.SupportsRectangle = true;
    caps.SupportsCircle = true;
    caps.SupportsEllipse = true;
    caps.SupportsLine = true;
    caps.SupportsPolyline = true;
    caps.SupportsPolygon = true;
    caps.SupportsPath = true;
    caps.SupportsCubicBezier = true;
    caps.SupportsQuadraticBezier = true;
    caps.SupportsArc = true;
    caps.SupportsCompoundPaths = true;
    caps.SupportsText = true;
    caps.SupportsRichText = true;
    caps.SupportsSolidFill = true;
    caps.SupportsLinearGradient = true;
    caps.SupportsRadialGradient = true;
    caps.SupportsPattern = true;
    caps.SupportsDashing = true;
    caps.SupportsOpacity = true;
    caps.SupportsGroups = true;
    caps.SupportsLayers = true;
    caps.SupportsSymbols = true;
    // <clipPath> is read (VectorClipPath definitions, clip-path references)
    // but not written, and <mask> is neither: a document that relies on
    // them exports without them, so say so.
    caps.SupportsClipping = false;
    caps.SupportsMasking = false;
    return caps;
}

std::shared_ptr<VectorStorage::VectorDocument> SVGConverter::Import(
        const std::string& filename, const ConversionOptions& options) {
    // Through the FileLoader: an .svgz is gzip-compressed SVG, which LoadFile
    // inflates transparently (detected by content, not by name), and the
    // path is opened the same way on every platform.
    FileBytesResult bytes = UltraCanvasFileLoader::LoadFile(filename);
    if (!bytes.success) {
        if (options.WarningCallback)
            options.WarningCallback("Failed to open SVG file: " + filename +
                                    (bytes.error.empty() ? "" : " (" + bytes.error + ")"));
        return nullptr;
    }
    return ImportFromString(std::string(bytes.bytes.begin(), bytes.bytes.end()), options);
}

std::shared_ptr<VectorStorage::VectorDocument> SVGConverter::ImportFromString(
        const std::string& data, const ConversionOptions& options) {
    SvgReader reader([&options](const std::string& msg) {
        if (options.WarningCallback) options.WarningCallback(msg);
    });
    auto doc = reader.Parse(data);
    if (options.ProgressCallback) options.ProgressCallback(1.0f);
    return doc;
}

std::shared_ptr<VectorStorage::VectorDocument> SVGConverter::ImportFromStream(
        std::istream& stream, const ConversionOptions& options) {
    std::ostringstream ss;
    ss << stream.rdbuf();
    return ImportFromString(ss.str(), options);
}

bool SVGConverter::Export(
        const VectorStorage::VectorDocument& document,
        const std::string& filename,
        const ConversionOptions& options) {
    std::string data = ExportToString(document, options);
    if (data.empty()) return false;
    std::ofstream file(UltraCanvas::PathFromUtf8(filename), std::ios::binary);
    if (!file.is_open()) {
        if (options.WarningCallback)
            options.WarningCallback("Failed to create SVG file: " + filename);
        return false;
    }
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    return file.good();
}

std::string SVGConverter::ExportToString(
        const VectorStorage::VectorDocument& document,
        const ConversionOptions& options) {
    SvgWriter writer(document, svgOptions, [&options](const std::string& msg) {
        if (options.WarningCallback) options.WarningCallback(msg);
    });
    std::string data = writer.Build();
    if (options.ProgressCallback) options.ProgressCallback(1.0f);
    return data;
}

bool SVGConverter::ExportToStream(
        const VectorStorage::VectorDocument& document,
        std::ostream& stream,
        const ConversionOptions& options) {
    std::string data = ExportToString(document, options);
    stream.write(data.data(), static_cast<std::streamsize>(data.size()));
    return stream.good();
}

bool SVGConverter::ValidateFile(const std::string& filename) const {
    std::ifstream file(UltraCanvas::PathFromUtf8(filename), std::ios::binary);
    if (!file.is_open()) return false;
    std::string head(512, '\0');
    file.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(file.gcount()));
    return ValidateData(head);
}

bool SVGConverter::ValidateData(const std::string& data) const {
    return data.find("<svg") != std::string::npos ||
           data.find("<?xml") != std::string::npos;
}

} // namespace VectorConverter
} // namespace UltraCanvas
