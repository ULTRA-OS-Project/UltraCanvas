// UltraCanvas/Plugins/Vector/UltraCanvasEPSConverter.cpp
// EPS (Encapsulated PostScript) writer - see the header for scope.
//
// The document tree walks exactly like the XAR writer's: styles resolve by
// inheritance down the tree, transforms accumulate and are baked into the
// emitted coordinates (EPS could carry matrices, but baked points keep the
// output trivially portable and share the PathOps normalisation), and the
// Y axis flips from the document's Y-down points to PostScript's Y-up.
// Version: 1.0.0
// Last Modified: 2026-08-26
// Author: UltraCanvas Framework

#include "UltraCanvasEPSConverter.h"
#include "UltraCanvasVectorStorage.h"
#include "UltraCanvasVectorPathOps.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <variant>

namespace UltraCanvas {
    namespace VectorConverter {

        using namespace VectorStorage;

        namespace {

            using namespace PathOps;

            class EpsEmitter {
            public:
                EpsEmitter(const VectorDocument& document,
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

                    out << "%!PS-Adobe-3.0 EPSF-3.0\n";
                    out << "%%BoundingBox: 0 0 " << static_cast<long>(std::ceil(pageW))
                        << " " << static_cast<long>(std::ceil(pageH)) << "\n";
                    out << "%%HiResBoundingBox: 0 0 " << Num(pageW) << " " << Num(pageH) << "\n";
                    out << "%%Creator: UltraCanvas\n";
                    if (!doc.Title.empty()) out << "%%Title: (" << Escape(doc.Title) << ")\n";
                    out << "%%Pages: 1\n";
                    out << "%%LanguageLevel: 2\n";
                    out << "%%EndComments\n";
                    out << "%%BeginProlog\n"
                           "/m { moveto } bind def\n"
                           "/l { lineto } bind def\n"
                           "/c { curveto } bind def\n"
                           "/cp { closepath } bind def\n"
                           "/n { newpath } bind def\n"
                           "/rgb { setrgbcolor } bind def\n"
                           "%%EndProlog\n";

                    for (const auto& layer : doc.Layers) {
                        if (!layer) continue;
                        if (!layer->Visible) continue;
                        out << "% Layer: " << (layer->Name.empty() ? "unnamed" : layer->Name)
                            << "\n";
                        for (const auto& child : layer->Children) {
                            if (child) EmitElement(*child, layer->Style, Matrix3x3::Identity());
                        }
                    }

                    out << "showpage\n%%EOF\n";
                    return out.str();
                }

            private:
                const VectorDocument& doc;
                std::function<void(const std::string&)> warn;
                std::ostringstream out;
                double pageW = 0, pageH = 0;   // points
                bool warnedOpacity = false;
                bool warnedGradient = false;

                // ===== FORMATTING =====

                static std::string Num(double v) {
                    char buf[40];
                    std::snprintf(buf, sizeof(buf), "%.3f", v);
                    std::string s = buf;
                    size_t last = s.find_last_not_of('0');
                    if (s[last] == '.') --last;
                    return s.substr(0, last + 1);
                }

                // Doc space (points, Y down) -> PostScript user space (Y up).
                std::string Pt(const Point2Dd& p) const {
                    return Num(p.x) + " " + Num(pageH - p.y);
                }

                static std::string Escape(const std::string& s) {
                    std::string r;
                    for (unsigned char ch : s) {
                        if (ch == '(' || ch == ')' || ch == '\\') {
                            r.push_back('\\');
                            r.push_back(static_cast<char>(ch));
                        } else if (ch < 32) {
                            char buf[8];
                            std::snprintf(buf, sizeof(buf), "\\%03o", ch);
                            r += buf;
                        } else {
                            r.push_back(static_cast<char>(ch));
                        }
                    }
                    return r;
                }

                static double AvgScale(const Matrix3x3& m) {
                    double det = std::fabs(static_cast<double>(m.m[0][0]) * m.m[1][1] -
                                           static_cast<double>(m.m[0][1]) * m.m[1][0]);
                    return det > 0 ? std::sqrt(det) : 1.0;
                }
                static bool AxisAligned(const Matrix3x3& m) {
                    return std::fabs(m.m[0][1]) < 1e-6 && std::fabs(m.m[1][0]) < 1e-6;
                }

                // ===== COLOUR =====
                // PostScript has no transparency: alpha and style opacity are
                // flattened by blending toward the white page.

                std::string Rgb(const Color& c, float opacity) {
                    float a = (static_cast<float>(c.a) / 255.0f) * opacity;
                    if (a < 0.999f && !warnedOpacity) {
                        warnedOpacity = true;
                        warn("EPS export: PostScript has no transparency; "
                             "opacity is flattened toward white");
                    }
                    a = std::max(0.0f, std::min(1.0f, a));
                    auto ch = [a](uint8_t v) {
                        double blended = (v / 255.0) * a + (1.0 - a);
                        return Num(blended);
                    };
                    return ch(c.r) + " " + ch(c.g) + " " + ch(c.b) + " rgb";
                }

                Color FlattenFill(const FillData& fill) {
                    if (const Color* c = std::get_if<Color>(&fill)) return *c;
                    if (const GradientData* g = std::get_if<GradientData>(&fill)) {
                        if (!warnedGradient) {
                            warnedGradient = true;
                            warn("EPS export: gradients are not supported by the plain "
                                 "operator set; filling with the blend of the end stops");
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
                    warn("EPS export: pattern/reference fills are not supported, "
                         "filling flat black");
                    return Color(0, 0, 0, 255);
                }

                static bool HasVisibleFill(const VectorStyle& s) {
                    return s.Fill.has_value() &&
                           !std::holds_alternative<std::monostate>(*s.Fill);
                }
                static bool HasVisibleStroke(const VectorStyle& s) {
                    return s.Stroke.has_value() && s.Stroke->Width > 0 &&
                           !std::holds_alternative<std::monostate>(s.Stroke->Fill);
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
                            EmitPath(EllipseSegs(el.Center, el.RadiusX, el.RadiusY),
                                     eff, ctm, true);
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
                        case VectorElementType::Polyline:
                            EmitPoly(static_cast<const VectorPolyline&>(e).Points,
                                     false, eff, ctm);
                            break;
                        case VectorElementType::Polygon:
                            EmitPoly(static_cast<const VectorPolygon&>(e).Points,
                                     true, eff, ctm);
                            break;
                        case VectorElementType::Path: {
                            const auto& p = static_cast<const VectorPath&>(e);
                            EmitPath(NormalizePath(p.Path), eff, ctm, true);
                            break;
                        }
                        case VectorElementType::Text:
                            EmitText(static_cast<const VectorText&>(e), eff, ctm);
                            break;
                        default:
                            warn("EPS export: element type not supported, skipped (type " +
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

                    out << "n";
                    for (const auto& s : segs) {
                        switch (s.kind) {
                            case FlatSeg::Move:
                                out << " " << Pt(ctm.Transform(s.p[0])) << " m";
                                break;
                            case FlatSeg::Line:
                                out << " " << Pt(ctm.Transform(s.p[0])) << " l";
                                break;
                            case FlatSeg::Cubic:
                                out << " " << Pt(ctm.Transform(s.p[0]))
                                    << " " << Pt(ctm.Transform(s.p[1]))
                                    << " " << Pt(ctm.Transform(s.p[2])) << " c";
                                break;
                        }
                        if (s.closeAfter) out << " cp";
                    }
                    out << "\n";

                    if (filled) {
                        Color fc = FlattenFill(*style.Fill);
                        float fo = style.Opacity * style.FillOpacity;
                        if (stroked) out << "gsave " << Rgb(fc, fo) << " fill grestore\n";
                        else out << Rgb(fc, fo) << " fill\n";
                    }
                    if (stroked) {
                        const StrokeData& st = *style.Stroke;
                        Color sc(0, 0, 0, 255);
                        if (const Color* c = std::get_if<Color>(&st.Fill)) sc = *c;
                        else warn("EPS export: non-solid stroke paint replaced with black");

                        double scale = AvgScale(ctm);
                        out << Num(st.Width * scale) << " setlinewidth ";
                        out << (st.LineCap == StrokeLineCap::Round ? 1
                              : st.LineCap == StrokeLineCap::Square ? 2 : 0)
                            << " setlinecap ";
                        out << (st.LineJoin == StrokeLineJoin::Round ? 1
                              : st.LineJoin == StrokeLineJoin::Bevel ? 2 : 0)
                            << " setlinejoin ";
                        out << Num(std::max(1.0f, st.MiterLimit)) << " setmiterlimit ";
                        if (!st.DashArray.empty()) {
                            out << "[";
                            for (size_t i = 0; i < st.DashArray.size(); ++i) {
                                if (i) out << " ";
                                out << Num(st.DashArray[i] * scale);
                            }
                            out << "] " << Num(st.DashOffset * scale) << " setdash ";
                        }
                        out << Rgb(sc, style.Opacity * style.StrokeOpacity * st.Opacity)
                            << " stroke";
                        if (!st.DashArray.empty()) out << " [] 0 setdash";
                        out << "\n";
                    }
                }

                // ===== TEXT =====

                static std::string PsFontName(const VectorTextStyle& s,
                                              const VectorTextStyle& base) {
                    std::string family = s.FontFamily.empty() ? base.FontFamily : s.FontFamily;
                    if (family.empty()) family = "Helvetica";
                    std::string name;
                    for (char ch : family) {
                        if (ch != ' ' && ch != '(' && ch != ')' && ch != '/' &&
                            ch != '%' && ch != '<' && ch != '>' && ch != '[' &&
                            ch != ']' && ch != '{' && ch != '}') {
                            name.push_back(ch);
                        }
                    }
                    bool bold = s.Weight == FontWeight::Bold || s.Weight == FontWeight::ExtraBold;
                    bool italic = s.Slant != FontSlant::Normal;
                    if (bold && italic) name += "-BoldItalic";
                    else if (bold) name += "-Bold";
                    else if (italic) name += "-Italic";
                    return name;
                }

                void EmitText(const VectorText& text, const VectorStyle& style,
                              const Matrix3x3& ctm) {
                    if (!AxisAligned(ctm)) {
                        warn("EPS export: rotated/skewed text is exported without "
                             "its rotation");
                    }
                    Color tc(0, 0, 0, 255);
                    if (style.Fill.has_value() &&
                        !std::holds_alternative<std::monostate>(*style.Fill)) {
                        tc = FlattenFill(*style.Fill);
                    }
                    float scale = static_cast<float>(AvgScale(ctm));

                    // Flatten spans into lines on '\n' (same shape as the XAR
                    // writer's text emission).
                    struct Chunk { std::string text; const VectorTextStyle* style; };
                    std::vector<std::vector<Chunk>> lines(1);
                    for (const auto& span : text.Spans) {
                        std::string piece;
                        for (char ch : span.Text) {
                            if (ch == '\n') {
                                if (!piece.empty())
                                    lines.back().push_back({piece, &span.Style});
                                piece.clear();
                                lines.emplace_back();
                            } else {
                                piece.push_back(ch);
                            }
                        }
                        if (!piece.empty()) lines.back().push_back({piece, &span.Style});
                    }

                    float baseSize = (text.BaseStyle.FontSize > 0 ? text.BaseStyle.FontSize
                                                                  : 12.0f) * scale;
                    float leading = baseSize * (text.BaseStyle.LineHeight > 0
                                                        ? text.BaseStyle.LineHeight : 1.2f);
                    out << Rgb(tc, style.Opacity * style.FillOpacity) << "\n";

                    std::string currentFont;
                    for (size_t li = 0; li < lines.size(); ++li) {
                        const auto& line = lines[li];
                        if (line.empty()) continue;
                        Point2Dd anchor = ctm.Transform(
                                Point2Dd(text.Position.x,
                                         text.Position.y + li * (leading / scale)));

                        bool shift = text.BaseStyle.Anchor != TextAnchor::Start;
                        if (shift && line.size() > 1) {
                            warn("EPS export: centre/right anchoring of multi-style "
                                 "lines is not supported; using left anchoring");
                            shift = false;
                        }

                        for (size_t ci = 0; ci < line.size(); ++ci) {
                            const Chunk& chunk = line[ci];
                            std::string font = PsFontName(*chunk.style, text.BaseStyle);
                            float size = (chunk.style->FontSize > 0 ? chunk.style->FontSize
                                          : text.BaseStyle.FontSize > 0 ? text.BaseStyle.FontSize
                                                                        : 12.0f) * scale;
                            std::string fontCmd = "/" + font + " " + Num(size) + " selectfont";
                            if (fontCmd != currentFont) {
                                out << fontCmd << "\n";
                                currentFont = fontCmd;
                            }
                            if (ci == 0) out << Pt(anchor) << " m ";
                            out << "(" << Escape(chunk.text) << ")";
                            if (shift) {
                                out << " dup stringwidth pop ";
                                if (text.BaseStyle.Anchor == TextAnchor::Middle)
                                    out << "2 div ";
                                out << "neg 0 rmoveto";
                            }
                            out << " show\n";
                        }
                    }
                }
            };

        }   // anonymous namespace

// ===== PUBLIC INTERFACE =====

        FormatCapabilities EPSConverter::GetCapabilities() const {
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

        std::shared_ptr<VectorStorage::VectorDocument> EPSConverter::Import(
                const std::string& filename, const ConversionOptions& options) {
            (void)filename;
            if (options.WarningCallback) {
                options.WarningCallback(
                        "EPSConverter cannot import; render EPS files through the "
                        "EPS plugin (UltraCanvasEPSPlugin) instead");
            }
            return nullptr;
        }

        std::shared_ptr<VectorStorage::VectorDocument> EPSConverter::ImportFromString(
                const std::string& data, const ConversionOptions& options) {
            (void)data;
            return Import("", options);
        }

        std::shared_ptr<VectorStorage::VectorDocument> EPSConverter::ImportFromStream(
                std::istream& stream, const ConversionOptions& options) {
            (void)stream;
            return Import("", options);
        }

        bool EPSConverter::Export(
                const VectorStorage::VectorDocument& document,
                const std::string& filename,
                const ConversionOptions& options) {
            std::string data = ExportToString(document, options);
            if (data.empty()) return false;
            std::ofstream file(filename, std::ios::binary);
            if (!file.is_open()) {
                if (options.WarningCallback) {
                    options.WarningCallback("Failed to create EPS file: " + filename);
                }
                return false;
            }
            file.write(data.data(), static_cast<std::streamsize>(data.size()));
            return file.good();
        }

        std::string EPSConverter::ExportToString(
                const VectorStorage::VectorDocument& document,
                const ConversionOptions& options) {
            EpsEmitter emitter(document, [&options](const std::string& msg) {
                if (options.WarningCallback) options.WarningCallback(msg);
            });
            std::string data = emitter.Build();
            if (options.ProgressCallback) options.ProgressCallback(1.0f);
            return data;
        }

        bool EPSConverter::ExportToStream(
                const VectorStorage::VectorDocument& document,
                std::ostream& stream,
                const ConversionOptions& options) {
            std::string data = ExportToString(document, options);
            stream.write(data.data(), static_cast<std::streamsize>(data.size()));
            return stream.good();
        }

        bool EPSConverter::ValidateFile(const std::string& filename) const {
            std::ifstream file(filename, std::ios::binary);
            if (!file.is_open()) return false;
            char head[4] = {0};
            file.read(head, 4);
            if (file.gcount() < 4) return false;
            return ValidateData(std::string(head, 4));
        }

        bool EPSConverter::ValidateData(const std::string& data) const {
            if (data.size() >= 4 && data.compare(0, 4, "%!PS") == 0) return true;
            // DOS EPS binary header: C5 D0 D3 C6
            return data.size() >= 4 &&
                   static_cast<uint8_t>(data[0]) == 0xC5 &&
                   static_cast<uint8_t>(data[1]) == 0xD0 &&
                   static_cast<uint8_t>(data[2]) == 0xD3 &&
                   static_cast<uint8_t>(data[3]) == 0xC6;
        }

    } // namespace VectorConverter
} // namespace UltraCanvas
