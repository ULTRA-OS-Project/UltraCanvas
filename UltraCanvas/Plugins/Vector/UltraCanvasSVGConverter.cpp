// UltraCanvas/Plugins/Vector/UltraCanvasSVGConverter.cpp
// SVG converter for the Vector plugin: implements the SVGConverter declared
// in UltraCanvasVectorConverter.h (which previously had no implementation).
//
// Unlike the XAR/EPS/CDR writers, SVG maps to the VectorStorage model almost
// one-to-one, so the writer keeps full fidelity: groups and layers stay
// <g> elements, transforms stay matrix attributes, gradients keep all their
// stops in <defs>, text keeps its spans, and nothing is flattened. The
// importer parses with tinyxml2 and leans on the storage utilities
// (ParsePathString, ParseColorString, ParseTransformString).
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "UltraCanvasVectorConverter.h"
#include "UltraCanvasVectorStorage.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <variant>

namespace UltraCanvas {
namespace VectorConverter {

using namespace VectorStorage;

namespace {

// ===== SHARED SMALL HELPERS =====

std::string Num(double v) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.6g", v);
    return buf;
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

    std::string StyleAttrs(const VectorStyle& s) {
        std::ostringstream a;
        if (s.Fill) {
            float alpha = 1.0f;
            std::string v = PaintValue(*s.Fill, &alpha);
            if (!v.empty()) a << " fill=\"" << v << "\"";
            float fo = alpha * s.FillOpacity;
            if (!NearlyOne(fo)) a << " fill-opacity=\"" << Num(fo) << "\"";
        } else if (!NearlyOne(s.FillOpacity)) {
            a << " fill-opacity=\"" << Num(s.FillOpacity) << "\"";
        }
        if (s.Stroke) {
            const StrokeData& st = *s.Stroke;
            float alpha = 1.0f;
            std::string v = PaintValue(st.Fill, &alpha);
            if (!v.empty()) a << " stroke=\"" << v << "\"";
            a << " stroke-width=\"" << Num(st.Width) << "\"";
            if (st.LineCap == StrokeLineCap::Round) a << " stroke-linecap=\"round\"";
            else if (st.LineCap == StrokeLineCap::Square) a << " stroke-linecap=\"square\"";
            if (st.LineJoin == StrokeLineJoin::Round) a << " stroke-linejoin=\"round\"";
            else if (st.LineJoin == StrokeLineJoin::Bevel) a << " stroke-linejoin=\"bevel\"";
            if (std::fabs(st.MiterLimit - 4.0f) > 1e-4f)
                a << " stroke-miterlimit=\"" << Num(st.MiterLimit) << "\"";
            if (!st.DashArray.empty()) {
                a << " stroke-dasharray=\"";
                for (size_t i = 0; i < st.DashArray.size(); ++i) {
                    if (i) a << " ";
                    a << Num(st.DashArray[i]);
                }
                a << "\"";
                if (std::fabs(st.DashOffset) > 1e-9)
                    a << " stroke-dashoffset=\"" << Num(st.DashOffset) << "\"";
            }
            float so = alpha * st.Opacity * s.StrokeOpacity;
            if (!NearlyOne(so)) a << " stroke-opacity=\"" << Num(so) << "\"";
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
        a << StyleAttrs(e.Style);
        return a.str();
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
            std::istringstream iss(v);
            hasViewBox = static_cast<bool>(iss >> vb[0] >> vb[1] >> vb[2] >> vb[3]);
        }
        double w = LengthAttr(svg, "width", hasViewBox ? vb[2] : 0);
        double h = LengthAttr(svg, "height", hasViewBox ? vb[3] : 0);
        doc->Size = Size2Dd{w, h};
        if (hasViewBox) doc->ViewBox = Rect2Dd{vb[0], vb[1], vb[2], vb[3]};

        // Definitions pre-pass: gradients can be referenced before (or after)
        // their definition, so collect them document-wide first.
        CollectGradients(svg);

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
                // SVG's default fill is black; the framework treats an unset
                // fill as inherited, so the black lives at the layer root.
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
        return doc;
    }

    static bool IsNonDrawable(const tinyxml2::XMLElement* e) {
        const char* n = StripNs(e->Name());
        return std::strcmp(n, "defs") == 0 || std::strcmp(n, "title") == 0 ||
               std::strcmp(n, "desc") == 0 || std::strcmp(n, "style") == 0 ||
               std::strcmp(n, "metadata") == 0 ||
               std::strcmp(n, "linearGradient") == 0 ||
               std::strcmp(n, "radialGradient") == 0;
    }

private:
    std::function<void(const std::string&)> warn;
    std::map<std::string, GradientData> gradients;
    bool warnedCss = false;

    static const char* StripNs(const char* name) {
        const char* colon = std::strchr(name, ':');
        return colon ? colon + 1 : name;
    }

    static double ParseLength(const char* s, double fallback) {
        if (!s || !*s) return fallback;
        char* end = nullptr;
        double v = std::strtod(s, &end);
        if (end == s) return fallback;
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

    // Presentation attribute or `style="…"` property (style wins, per CSS).
    static std::string Prop(const tinyxml2::XMLElement* e, const char* name) {
        std::string result;
        if (const char* a = e->Attribute(name)) result = a;
        if (const char* styleAttr = e->Attribute("style")) {
            std::string s = styleAttr;
            std::istringstream iss(s);
            std::string decl;
            while (std::getline(iss, decl, ';')) {
                size_t colon = decl.find(':');
                if (colon == std::string::npos) continue;
                std::string key = decl.substr(0, colon);
                key.erase(0, key.find_first_not_of(" \t"));
                key.erase(key.find_last_not_of(" \t") + 1);
                if (key == name) {
                    std::string value = decl.substr(colon + 1);
                    value.erase(0, value.find_first_not_of(" \t"));
                    value.erase(value.find_last_not_of(" \t") + 1);
                    result = value;
                }
            }
        }
        return result;
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
                gs.position = std::strtod(off.c_str(), nullptr);
                if (off.back() == '%') gs.position /= 100.0;
            }
            std::string sc = Prop(st, "stop-color");
            gs.color = sc.empty() ? Color(0, 0, 0, 255) : ParseColorString(sc);
            std::string so = Prop(st, "stop-opacity");
            if (!so.empty()) {
                gs.color.a = static_cast<uint8_t>(
                        std::max(0.0, std::min(1.0, std::strtod(so.c_str(), nullptr))) * 255);
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
        double d = std::strtod(v.c_str(), nullptr);
        if (!v.empty() && v.back() == '%') d /= 100.0;
        return static_cast<float>(std::max(0.0, std::min(1.0, d)));
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
        std::string strokeVal = Prop(e, "stroke");
        if (!strokeVal.empty() && strokeVal != "none" && strokeVal != "inherit") {
            StrokeData st;
            if (auto p = ParsePaint(strokeVal)) st.Fill = *p;
            std::string sw = Prop(e, "stroke-width");
            if (!sw.empty()) st.Width = static_cast<float>(ParseLength(sw.c_str(), 1.0));
            std::string cap = Prop(e, "stroke-linecap");
            if (cap == "round") st.LineCap = StrokeLineCap::Round;
            else if (cap == "square") st.LineCap = StrokeLineCap::Square;
            std::string join = Prop(e, "stroke-linejoin");
            if (join == "round") st.LineJoin = StrokeLineJoin::Round;
            else if (join == "bevel") st.LineJoin = StrokeLineJoin::Bevel;
            std::string ml = Prop(e, "stroke-miterlimit");
            if (!ml.empty()) st.MiterLimit = static_cast<float>(std::strtod(ml.c_str(), nullptr));
            std::string dash = Prop(e, "stroke-dasharray");
            if (!dash.empty() && dash != "none") {
                for (char& ch : dash) if (ch == ',') ch = ' ';
                std::istringstream iss(dash);
                double d;
                while (iss >> d) st.DashArray.push_back(d);
            }
            std::string doff = Prop(e, "stroke-dashoffset");
            if (!doff.empty()) st.DashOffset = std::strtod(doff.c_str(), nullptr);
            st.Opacity = ParseOpacity(Prop(e, "stroke-opacity"), 1.0f);
            s.Stroke = st;
        } else if (strokeVal == "none") {
            s.Stroke.reset();
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

    // ===== ELEMENTS =====

    void ParseNode(const tinyxml2::XMLElement* e, VectorDocument& doc,
                   VectorGroup* parent) {
        const char* name = StripNs(e->Name());

        if (std::strcmp(name, "defs") == 0) {
            for (const tinyxml2::XMLElement* child = e->FirstChildElement();
                 child; child = child->NextSiblingElement()) {
                const char* childName = StripNs(child->Name());
                if (std::strcmp(childName, "linearGradient") == 0 ||
                    std::strcmp(childName, "radialGradient") == 0) {
                    continue;   // collected in the pre-pass
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
        if (std::strcmp(name, "style") == 0) {
            if (!warnedCss) {
                warnedCss = true;
                warn("SVG import: CSS stylesheets are not supported; only "
                     "presentation attributes and inline style apply");
            }
            return;
        }
        if (std::strcmp(name, "linearGradient") == 0 ||
            std::strcmp(name, "radialGradient") == 0 ||
            std::strcmp(name, "metadata") == 0) {
            return;   // gradients were collected in the pre-pass
        }

        auto el = ParseShape(e, doc);
        if (el) parent->AddChild(el);
    }

    std::shared_ptr<VectorElement> ParseShape(const tinyxml2::XMLElement* e,
                                              VectorDocument& doc) {
        const char* name = StripNs(e->Name());

        if (std::strcmp(name, "rect") == 0) {
            auto r = std::make_shared<VectorRect>();
            r->Bounds = Rect2Dd{LengthAttr(e, "x", 0), LengthAttr(e, "y", 0),
                                LengthAttr(e, "width", 0), LengthAttr(e, "height", 0)};
            r->RadiusX = static_cast<float>(LengthAttr(e, "rx", 0));
            r->RadiusY = static_cast<float>(LengthAttr(e, "ry", r->RadiusX));
            if (r->RadiusX <= 0 && r->RadiusY > 0) r->RadiusX = r->RadiusY;
            if (r->RadiusX > 0) r->Type = VectorElementType::RoundedRectangle;
            ApplyStyle(e, *r);
            return r;
        }
        if (std::strcmp(name, "circle") == 0) {
            auto c = std::make_shared<VectorCircle>();
            c->Center = Point2Dd(LengthAttr(e, "cx", 0), LengthAttr(e, "cy", 0));
            c->Radius = static_cast<float>(LengthAttr(e, "r", 0));
            ApplyStyle(e, *c);
            return c;
        }
        if (std::strcmp(name, "ellipse") == 0) {
            auto el = std::make_shared<VectorEllipse>();
            el->Center = Point2Dd(LengthAttr(e, "cx", 0), LengthAttr(e, "cy", 0));
            el->RadiusX = static_cast<float>(LengthAttr(e, "rx", 0));
            el->RadiusY = static_cast<float>(LengthAttr(e, "ry", 0));
            ApplyStyle(e, *el);
            return el;
        }
        if (std::strcmp(name, "line") == 0) {
            auto ln = std::make_shared<VectorLine>();
            ln->Start = Point2Dd(LengthAttr(e, "x1", 0), LengthAttr(e, "y1", 0));
            ln->End = Point2Dd(LengthAttr(e, "x2", 0), LengthAttr(e, "y2", 0));
            ApplyStyle(e, *ln);
            return ln;
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
                return pl;
            }
            auto pg = std::make_shared<VectorPolygon>();
            pg->Points = std::move(pts);
            ApplyStyle(e, *pg);
            return pg;
        }
        if (std::strcmp(name, "path") == 0) {
            auto p = std::make_shared<VectorPath>();
            if (const char* d = e->Attribute("d")) p->Path = ParsePathString(d);
            ApplyStyle(e, *p);
            return p;
        }
        if (std::strcmp(name, "text") == 0) {
            return ParseText(e);
        }
        if (std::strcmp(name, "g") == 0 || std::strcmp(name, "a") == 0) {
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
        if (std::strcmp(name, "image") == 0) {
            auto im = std::make_shared<VectorImage>();
            im->Bounds = Rect2Dd{LengthAttr(e, "x", 0), LengthAttr(e, "y", 0),
                                 LengthAttr(e, "width", 0), LengthAttr(e, "height", 0)};
            const char* href = e->Attribute("href");
            if (!href) href = e->Attribute("xlink:href");
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
    caps.SupportsClipping = true;
    return caps;
}

std::shared_ptr<VectorStorage::VectorDocument> SVGConverter::Import(
        const std::string& filename, const ConversionOptions& options) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        if (options.WarningCallback)
            options.WarningCallback("Failed to open SVG file: " + filename);
        return nullptr;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    return ImportFromString(ss.str(), options);
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
    std::ofstream file(filename, std::ios::binary);
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
    std::ifstream file(filename, std::ios::binary);
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
