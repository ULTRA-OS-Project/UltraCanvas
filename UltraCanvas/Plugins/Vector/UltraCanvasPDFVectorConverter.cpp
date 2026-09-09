// UltraCanvas/Plugins/Vector/UltraCanvasPDFVectorConverter.cpp
// PDF (vector) writer for the Vector plugin: implements the
// PDFVectorConverter declared in UltraCanvasVectorConverter.h (which
// previously had no implementation).
//
// Writes a self-contained PDF 1.4 by hand: catalog/pages/page objects, an
// uncompressed content stream of path and text operators, base-14 Type1
// fonts, and ExtGState entries for opacity. The document tree walks exactly
// like the EPS writer's - styles resolve by inheritance, transforms
// accumulate and bake into the emitted coordinates through the shared
// PathOps normalisation, and the Y axis flips to PDF's Y-up page space.
//
// Export-only: reading PDF is the MuPDF-based PDF plugin's job, so
// CanImport() is false and the Import methods return null. Gradients fall
// back to the blend of their end stops (no shading dictionaries yet), and
// centre/right text anchoring is approximated from an average glyph width
// (exact metrics would need embedded font programs) - each warned.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "UltraCanvasVectorConverter.h"
#include "UltraCanvasVectorStorage.h"
#include "UltraCanvasVectorPathOps.h"

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

using namespace PathOps;

std::string Num(double v) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.3f", v);
    std::string s = buf;
    size_t last = s.find_last_not_of('0');
    if (s[last] == '.') --last;
    return s.substr(0, last + 1);
}

class PdfEmitter {
public:
    PdfEmitter(const VectorDocument& document,
               std::function<void(const std::string&)> warnFn)
            : doc(document), warn(std::move(warnFn)) {}

    std::string Build() {
        pageW = doc.Size.width;
        pageH = doc.Size.height;
        if (pageW <= 0 || pageH <= 0) {
            Rect2Dd bbox = doc.GetBoundingBox();
            pageW = bbox.x + bbox.width;
            pageH = bbox.y + bbox.height;
            if (pageW <= 0) pageW = 595;   // A4 fallback
            if (pageH <= 0) pageH = 842;
        }

        for (const auto& layer : doc.Layers) {
            if (!layer || !layer->Visible) continue;
            for (const auto& child : layer->Children) {
                if (child) EmitElement(*child, layer->Style, Matrix3x3::Identity());
            }
        }

        // Objects: 1 catalog, 2 pages, 3 page, 4 contents, then fonts, then
        // graphic states. Object numbers are assigned here, in emission
        // order (registration order interleaves fonts and states).
        int next = 5;
        for (auto& [name, info] : fonts) info.object = next++;
        for (auto& [alpha, info] : gstates) info.object = next++;

        std::vector<std::string> objects;
        objects.push_back("<< /Type /Catalog /Pages 2 0 R >>");
        objects.push_back("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");

        std::ostringstream res;
        res << "<<";
        if (!fonts.empty()) {
            res << " /Font <<";
            for (const auto& [name, info] : fonts) {
                res << " /" << info.resource << " " << info.object << " 0 R";
            }
            res << " >>";
        }
        if (!gstates.empty()) {
            res << " /ExtGState <<";
            for (const auto& [alpha, info] : gstates) {
                res << " /" << info.resource << " " << info.object << " 0 R";
            }
            res << " >>";
        }
        res << " >>";

        std::ostringstream page;
        page << "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 " << Num(pageW)
             << " " << Num(pageH) << "] /Contents 4 0 R /Resources "
             << res.str() << " >>";
        objects.push_back(page.str());

        std::string cs = content.str();
        std::ostringstream stream;
        stream << "<< /Length " << cs.size() << " >>\nstream\n" << cs << "endstream";
        objects.push_back(stream.str());

        for (const auto& [name, info] : fonts) {
            objects.push_back("<< /Type /Font /Subtype /Type1 /BaseFont /" + name +
                              " /Encoding /WinAnsiEncoding >>");
        }
        for (const auto& [alpha, info] : gstates) {
            objects.push_back("<< /Type /ExtGState /ca " + Num(alpha) +
                              " /CA " + Num(alpha) + " >>");
        }

        std::ostringstream out;
        out << "%PDF-1.4\n%\xE2\xE3\xCF\xD3\n";
        std::vector<size_t> offsets;
        for (size_t i = 0; i < objects.size(); ++i) {
            offsets.push_back(static_cast<size_t>(out.tellp()));
            out << (i + 1) << " 0 obj\n" << objects[i] << "\nendobj\n";
        }
        size_t xrefPos = static_cast<size_t>(out.tellp());
        out << "xref\n0 " << (objects.size() + 1) << "\n";
        out << "0000000000 65535 f \n";
        for (size_t off : offsets) {
            char line[24];
            std::snprintf(line, sizeof(line), "%010zu 00000 n \n", off);
            out << line;
        }
        std::string title = doc.Title;
        out << "trailer\n<< /Size " << (objects.size() + 1) << " /Root 1 0 R";
        if (!title.empty()) out << " /Info << /Title (" << EscapeString(title) << ") >>";
        out << " >>\nstartxref\n" << xrefPos << "\n%%EOF\n";
        return out.str();
    }

private:
    const VectorDocument& doc;
    std::function<void(const std::string&)> warn;
    std::ostringstream content;
    double pageW = 0, pageH = 0;
    bool warnedGradient = false, warnedAnchor = false, warnedNonLatin = false;

    struct Resource { std::string resource; int object; };
    std::map<std::string, Resource> fonts;   // BaseFont name -> resource
    std::map<double, Resource> gstates;      // alpha -> resource

    std::string FontResource(const VectorTextStyle& s, const VectorTextStyle& base) {
        std::string family = s.FontFamily.empty() ? base.FontFamily : s.FontFamily;
        std::string lower;
        for (char ch : family) lower.push_back(static_cast<char>(std::tolower(ch)));
        bool bold = s.Weight == FontWeight::Bold || s.Weight == FontWeight::ExtraBold;
        bool italic = s.Slant != FontSlant::Normal;

        std::string baseName;
        if (lower.find("times") != std::string::npos ||
            lower.find("serif") != std::string::npos) {
            baseName = "Times";
            if (bold && italic) baseName += "-BoldItalic";
            else if (bold) baseName += "-Bold";
            else if (italic) baseName += "-Italic";
            else baseName += "-Roman";
        } else if (lower.find("courier") != std::string::npos ||
                   lower.find("mono") != std::string::npos) {
            baseName = "Courier";
            if (bold && italic) baseName += "-BoldOblique";
            else if (bold) baseName += "-Bold";
            else if (italic) baseName += "-Oblique";
        } else {
            baseName = "Helvetica";
            if (bold && italic) baseName += "-BoldOblique";
            else if (bold) baseName += "-Bold";
            else if (italic) baseName += "-Oblique";
        }

        auto it = fonts.find(baseName);
        if (it != fonts.end()) return it->second.resource;
        Resource r{"F" + std::to_string(fonts.size() + 1), 0};
        fonts[baseName] = r;
        return r.resource;
    }

    std::string GStateResource(double alpha) {
        alpha = std::round(alpha * 1000.0) / 1000.0;
        auto it = gstates.find(alpha);
        if (it != gstates.end()) return it->second.resource;
        Resource r{"GS" + std::to_string(gstates.size() + 1), 0};
        gstates[alpha] = r;
        return r.resource;
    }

    // Document space (points, Y down) -> PDF page space (Y up).
    std::string Pt(const Point2Dd& p) const {
        return Num(p.x) + " " + Num(pageH - p.y);
    }
    static double AvgScale(const Matrix3x3& m) {
        double det = std::fabs(static_cast<double>(m.m[0][0]) * m.m[1][1] -
                               static_cast<double>(m.m[0][1]) * m.m[1][0]);
        return det > 0 ? std::sqrt(det) : 1.0;
    }
    static bool AxisAligned(const Matrix3x3& m) {
        return std::fabs(m.m[0][1]) < 1e-6 && std::fabs(m.m[1][0]) < 1e-6;
    }

    static std::string Rgb(const Color& c, const char* op) {
        return Num(c.r / 255.0) + " " + Num(c.g / 255.0) + " " + Num(c.b / 255.0) +
               " " + op;
    }

    std::string EscapeString(const std::string& utf8) {
        // WinAnsi is close enough to Latin-1 for the base-14 fonts; code
        // points beyond it become '?'.
        std::string out;
        size_t i = 0, n = utf8.size();
        while (i < n) {
            uint32_t cp = static_cast<uint8_t>(utf8[i]);
            size_t extra = 0;
            if (cp >= 0xF0) { cp &= 0x07; extra = 3; }
            else if (cp >= 0xE0) { cp &= 0x0F; extra = 2; }
            else if (cp >= 0xC0) { cp &= 0x1F; extra = 1; }
            if (i + extra >= n && extra > 0) break;
            for (size_t k = 0; k < extra; ++k) {
                cp = (cp << 6) | (static_cast<uint8_t>(utf8[i + 1 + k]) & 0x3F);
            }
            i += 1 + extra;
            if (cp > 0xFF) {
                if (!warnedNonLatin) {
                    warnedNonLatin = true;
                    warn("PDF export: characters outside Latin-1 are replaced "
                         "with '?' (the base-14 fonts are WinAnsi encoded)");
                }
                cp = '?';
            }
            char ch = static_cast<char>(cp);
            if (ch == '(' || ch == ')' || ch == '\\') {
                out.push_back('\\');
                out.push_back(ch);
            } else if (ch == '\n') out += "\\n";
            else if (ch == '\r') out += "\\r";
            else out.push_back(ch);
        }
        return out;
    }

    static bool HasVisibleFill(const VectorStyle& s) {
        return s.Fill.has_value() && !std::holds_alternative<std::monostate>(*s.Fill);
    }
    static bool HasVisibleStroke(const VectorStyle& s) {
        return s.Stroke.has_value() && s.Stroke->Width > 0 &&
               !std::holds_alternative<std::monostate>(s.Stroke->Fill);
    }

    Color FlattenFill(const FillData& fill) {
        if (const Color* c = std::get_if<Color>(&fill)) return *c;
        if (const GradientData* g = std::get_if<GradientData>(&fill)) {
            if (!warnedGradient) {
                warnedGradient = true;
                warn("PDF export: gradients are not written yet (no shading "
                     "dictionaries); filling with the blend of the end stops");
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
                             static_cast<uint8_t>((c0.b + c1.b) / 2),
                             static_cast<uint8_t>((c0.a + c1.a) / 2));
            }
            return Color(128, 128, 128, 255);
        }
        warn("PDF export: pattern/reference fills are not supported, "
             "filling flat black");
        return Color(0, 0, 0, 255);
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
                warn("PDF export: element type not supported, skipped (type " +
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

        content << "q\n";

        Color fc(0, 0, 0, 255);
        double fillAlpha = 1.0;
        if (filled) {
            fc = FlattenFill(*style.Fill);
            fillAlpha = (fc.a / 255.0) * style.Opacity * style.FillOpacity;
            content << Rgb(fc, "rg") << "\n";
        }
        if (stroked) {
            const StrokeData& st = *style.Stroke;
            Color sc(0, 0, 0, 255);
            if (const Color* c = std::get_if<Color>(&st.Fill)) sc = *c;
            else warn("PDF export: non-solid stroke paint replaced with black");
            content << Rgb(sc, "RG") << "\n";
            double scale = AvgScale(ctm);
            content << Num(st.Width * scale) << " w ";
            content << (st.LineCap == StrokeLineCap::Round ? 1
                      : st.LineCap == StrokeLineCap::Square ? 2 : 0) << " J ";
            content << (st.LineJoin == StrokeLineJoin::Round ? 1
                      : st.LineJoin == StrokeLineJoin::Bevel ? 2 : 0) << " j ";
            content << Num(std::max(1.0f, st.MiterLimit)) << " M\n";
            if (!st.DashArray.empty()) {
                content << "[";
                for (size_t i = 0; i < st.DashArray.size(); ++i) {
                    if (i) content << " ";
                    content << Num(st.DashArray[i] * scale);
                }
                content << "] " << Num(st.DashOffset * scale) << " d\n";
            }
        }
        if (fillAlpha < 0.999 && filled) {
            content << "/" << GStateResource(std::max(0.0, fillAlpha)) << " gs\n";
        }

        for (const auto& s : segs) {
            switch (s.kind) {
                case FlatSeg::Move:
                    content << Pt(ctm.Transform(s.p[0])) << " m\n";
                    break;
                case FlatSeg::Line:
                    content << Pt(ctm.Transform(s.p[0])) << " l\n";
                    break;
                case FlatSeg::Cubic:
                    content << Pt(ctm.Transform(s.p[0])) << " "
                            << Pt(ctm.Transform(s.p[1])) << " "
                            << Pt(ctm.Transform(s.p[2])) << " c\n";
                    break;
            }
            if (s.closeAfter) content << "h\n";
        }

        if (filled && stroked) content << "B\n";
        else if (filled) content << "f\n";
        else content << "S\n";
        content << "Q\n";
    }

    // ===== TEXT =====

    void EmitText(const VectorText& text, const VectorStyle& style,
                  const Matrix3x3& ctm) {
        if (!AxisAligned(ctm)) {
            warn("PDF export: rotated/skewed text is exported without its rotation");
        }
        Color tc(0, 0, 0, 255);
        if (style.Fill.has_value() &&
            !std::holds_alternative<std::monostate>(*style.Fill)) {
            tc = FlattenFill(*style.Fill);
        }
        float scale = static_cast<float>(AvgScale(ctm));
        float baseSize = (text.BaseStyle.FontSize > 0 ? text.BaseStyle.FontSize : 12.0f);
        float leading = baseSize * (text.BaseStyle.LineHeight > 0
                                            ? text.BaseStyle.LineHeight : 1.2f);

        // Flatten spans into lines on '\n'.
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

        content << "q\n" << Rgb(tc, "rg") << "\nBT\n";
        double alpha = (tc.a / 255.0) * style.Opacity * style.FillOpacity;
        if (alpha < 0.999) content << "/" << GStateResource(alpha) << " gs\n";

        for (size_t li = 0; li < lines.size(); ++li) {
            const auto& line = lines[li];
            if (line.empty()) continue;
            Point2Dd anchor = ctm.Transform(
                    Point2Dd(text.Position.x, text.Position.y + li * leading));

            double shift = 0;
            if (text.BaseStyle.Anchor != TextAnchor::Start) {
                if (!warnedAnchor) {
                    warnedAnchor = true;
                    warn("PDF export: centre/right text anchoring is approximated "
                         "from an average glyph width");
                }
                size_t chars = 0;
                double est = 0;
                for (const auto& chunk : line) {
                    chars = chunk.text.size();
                    float size = (chunk.style->FontSize > 0 ? chunk.style->FontSize
                                                            : baseSize) * scale;
                    est += 0.5 * size * chars;
                }
                shift = text.BaseStyle.Anchor == TextAnchor::Middle ? -est / 2 : -est;
            }

            content << "1 0 0 1 " << Num(anchor.x + shift) << " "
                    << Num(pageH - anchor.y) << " Tm\n";
            for (const auto& chunk : line) {
                float size = (chunk.style->FontSize > 0 ? chunk.style->FontSize
                                                        : baseSize) * scale;
                content << "/" << FontResource(*chunk.style, text.BaseStyle)
                        << " " << Num(size) << " Tf ";
                content << "(" << EscapeString(chunk.text) << ") Tj\n";
            }
        }
        content << "ET\nQ\n";
    }
};

}   // anonymous namespace

// ===== PUBLIC INTERFACE =====

FormatCapabilities PDFVectorConverter::GetCapabilities() const {
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
    caps.SupportsOpacity = true;
    caps.SupportsDashing = true;
    caps.SupportsGroups = true;            // flattened
    caps.SupportsLayers = true;            // flattened
    return caps;
}

std::shared_ptr<VectorStorage::VectorDocument> PDFVectorConverter::Import(
        const std::string& filename, const ConversionOptions& options) {
    (void)filename;
    if (options.WarningCallback) {
        options.WarningCallback(
                "PDFVectorConverter cannot import; render PDF files through the "
                "PDF plugin (MuPDF) instead");
    }
    return nullptr;
}

std::shared_ptr<VectorStorage::VectorDocument> PDFVectorConverter::ImportFromString(
        const std::string& data, const ConversionOptions& options) {
    (void)data;
    return Import("", options);
}

std::shared_ptr<VectorStorage::VectorDocument> PDFVectorConverter::ImportFromStream(
        std::istream& stream, const ConversionOptions& options) {
    (void)stream;
    return Import("", options);
}

bool PDFVectorConverter::Export(
        const VectorStorage::VectorDocument& document,
        const std::string& filename,
        const ConversionOptions& options) {
    std::string data = ExportToString(document, options);
    if (data.empty()) return false;
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        if (options.WarningCallback) {
            options.WarningCallback("Failed to create PDF file: " + filename);
        }
        return false;
    }
    file.write(data.data(), static_cast<std::streamsize>(data.size()));
    return file.good();
}

std::string PDFVectorConverter::ExportToString(
        const VectorStorage::VectorDocument& document,
        const ConversionOptions& options) {
    PdfEmitter emitter(document, [&options](const std::string& msg) {
        if (options.WarningCallback) options.WarningCallback(msg);
    });
    std::string data = emitter.Build();
    if (options.ProgressCallback) options.ProgressCallback(1.0f);
    return data;
}

bool PDFVectorConverter::ExportToStream(
        const VectorStorage::VectorDocument& document,
        std::ostream& stream,
        const ConversionOptions& options) {
    std::string data = ExportToString(document, options);
    stream.write(data.data(), static_cast<std::streamsize>(data.size()));
    return stream.good();
}

bool PDFVectorConverter::ValidateFile(const std::string& filename) const {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) return false;
    char head[5] = {0};
    file.read(head, 5);
    if (file.gcount() < 5) return false;
    return ValidateData(std::string(head, 5));
}

bool PDFVectorConverter::ValidateData(const std::string& data) const {
    return data.size() >= 5 && data.compare(0, 5, "%PDF-") == 0;
}

} // namespace VectorConverter
} // namespace UltraCanvas
