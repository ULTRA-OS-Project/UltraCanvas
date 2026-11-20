// UltraCanvas/Plugins/Vector/UltraCanvasODGConverter.cpp
// OpenDocument Drawing converter: the IVectorFormatConverter glue and the
// writer. The reader is UltraCanvasODGReader.cpp. See the header for what
// each direction covers.
//
// The writer folds every transform into the geometry, since ODF groups
// carry none and a draw:transform on a shape is only rotate / translate /
// skew / scale in LibreOffice's own convention: shapes are written in page
// coordinates (a rotated rectangle or ellipse becomes a path), a text or a
// picture that is rotated keeps a draw:transform as LibreOffice writes one.
// Numbers go through FormatFixedClassic: centimetres to four places (a
// micrometre) for lengths, path data in 1/100 mm like LibreOffice's own.
// Version: 1.2.0 - a radial gradient placed as an ellipse is written as an ellipsoid with its angle
// Version: 1.1.0 - the package carries Thumbnails/thumbnail.png, the page at 256 pixels
// Version: 1.0.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework

#include "UltraCanvasODGConverter.h"
#include "DataFormats/UltraCanvasVectorPathOps.h"
#include "UltraCanvasZipPackage.h"
#include "UltraCanvasTextUtils.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasVectorRaster.h"      // the page drawn for the package's thumbnail
#include "UltraCanvasClipboardDib.h"      // ... and encoded as PNG
#include "UltraCanvasImage.h"             // libvips started for the pictures it draws

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>

namespace UltraCanvas {
namespace VectorConverter {

using namespace VectorStorage;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kPointsPerCm = 72.0 / 2.54;
constexpr double kHundredthMmPerPoint = 2540.0 / 72.0;
const char* const kMimeDrawing = "application/vnd.oasis.opendocument.graphics";
const char* const kMimeTemplate = "application/vnd.oasis.opendocument.graphics-template";

const char* const kNamespaceDecls =
    " xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\""
    " xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\""
    " xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\""
    " xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\""
    " xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\""
    " xmlns:xlink=\"http://www.w3.org/1999/xlink\""
    " xmlns:dc=\"http://purl.org/dc/elements/1.1/\""
    " xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\""
    " xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\""
    " xmlns:loext=\"urn:org:documentfoundation:names:experimental:office:xmlns:loext:1.0\""
    " office:version=\"1.3\"";

std::string Cm(double points) { return FormatFixedClassic(points / kPointsPerCm, 4) + "cm"; }
std::string Pt(double points) { return FormatFixedClassic(points, 3) + "pt"; }
std::string H(double points) { return FormatFixedClassic(points * kHundredthMmPerPoint, 2); }
std::string Pct(double fraction) { return FormatFixedClassic(fraction * 100.0, 2) + "%"; }

std::string Escape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default:
                if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') continue;   // not allowed in XML 1.0
                out += static_cast<char>(c);
        }
    }
    return out;
}

std::string Hex(const Color& c) {
    static const char* digits = "0123456789abcdef";
    std::string s = "#";
    for (uint8_t v : {c.r, c.g, c.b}) {
        s += digits[v >> 4];
        s += digits[v & 15];
    }
    return s;
}

// A name usable as an ODF style name (an NCName): LibreOffice encodes the
// characters it cannot hold as _xx_ hex, which a reader shows decoded.
std::string StyleName(const std::string& display) {
    std::string out;
    for (unsigned char c : display) {
        if (std::isalnum(c) || c == '-' || c == '.' || (c == '_' && !out.empty())) out += static_cast<char>(c);
        else {
            static const char* digits = "0123456789abcdef";
            out += '_';
            out += digits[c >> 4];
            out += digits[c & 15];
            out += '_';
        }
    }
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0])) || out[0] == '-' || out[0] == '.')
        out = "s" + out;
    return out;
}

double Scale(const Matrix3x3& m) { return std::sqrt(std::fabs(m.Determinant())); }
bool AxisAligned(const Matrix3x3& m) {
    return std::fabs(m.m[0][1]) < 1e-9 && std::fabs(m.m[1][0]) < 1e-9 && m.m[0][0] > 0 && m.m[1][1] > 0;
}

Color Average(const std::vector<GradientStop>& stops) {
    if (stops.empty()) return Colors::Black;
    double r = 0, g = 0, b = 0, a = 0;
    for (const auto& s : stops) { r += s.color.r; g += s.color.g; b += s.color.b; a += s.color.a; }
    const double n = static_cast<double>(stops.size());
    return Color(static_cast<uint8_t>(r / n), static_cast<uint8_t>(g / n), static_cast<uint8_t>(b / n),
                 static_cast<uint8_t>(a / n));
}

// The colour of a stop list at t (stops sorted).
Color ColorAt(const std::vector<GradientStop>& stops, double t) {
    if (stops.empty()) return Colors::Black;
    if (t <= stops.front().position) return stops.front().color;
    for (size_t i = 1; i < stops.size(); ++i) {
        if (t <= stops[i].position) {
            const auto& a = stops[i - 1];
            const auto& b = stops[i];
            const double span = b.position - a.position;
            const double f = span > 1e-12 ? (t - a.position) / span : 1.0;
            auto mix = [&](uint8_t x, uint8_t y) { return static_cast<uint8_t>(std::lround(x + (y - x) * f)); };
            return Color(mix(a.color.r, b.color.r), mix(a.color.g, b.color.g), mix(a.color.b, b.color.b),
                         mix(a.color.a, b.color.a));
        }
    }
    return stops.back().color;
}

std::string ImageExtension(const std::string& mime) {
    if (mime == "image/jpeg") return "jpg";
    if (mime == "image/gif") return "gif";
    if (mime == "image/bmp") return "bmp";
    if (mime == "image/svg+xml") return "svg";
    if (mime == "image/tiff") return "tif";
    if (mime == "image/webp") return "webp";
    return "png";
}

// Straight segments of `path` (moves and lines only), or false when it has
// curves.
bool StraightSegments(const PathData& path, std::vector<std::pair<Point2Dd, Point2Dd>>& out) {
    Point2Dd cur, start;
    bool have = false;
    for (const auto& s : PathOps::NormalizePath(path)) {
        if (s.kind == PathOps::FlatSeg::Cubic) return false;
        if (s.kind == PathOps::FlatSeg::Move) {
            cur = start = s.p[0];
            have = true;
        } else if (have) {
            out.emplace_back(cur, s.p[0]);
            cur = s.p[0];
        }
        if (s.closeAfter && have) {
            out.emplace_back(cur, start);
            cur = start;
        }
    }
    return true;
}

bool InsidePolygons(const std::vector<FlatSubpath>& rings, const Point2Dd& p) {
    bool inside = false;
    for (const auto& ring : rings) {
        const auto& v = ring.Points;
        for (size_t i = 0, j = v.size() - 1; i < v.size(); j = i++) {
            if (((v[i].y > p.y) != (v[j].y > p.y)) &&
                (p.x < (v[j].x - v[i].x) * (p.y - v[i].y) / (v[j].y - v[i].y) + v[i].x))
                inside = !inside;
        }
    }
    return inside;
}

// The parts of segment a-b inside the rings (even-odd).
void ClipSegment(const std::vector<FlatSubpath>& rings, const Point2Dd& a, const Point2Dd& b,
                 std::vector<std::pair<Point2Dd, Point2Dd>>& out) {
    std::vector<double> ts{0.0, 1.0};
    const Point2Dd d{b.x - a.x, b.y - a.y};
    for (const auto& ring : rings) {
        const auto& v = ring.Points;
        if (v.size() < 2) continue;
        for (size_t i = 0, j = v.size() - 1; i < v.size(); j = i++) {
            const Point2Dd e{v[i].x - v[j].x, v[i].y - v[j].y};
            const double den = d.x * e.y - d.y * e.x;
            if (std::fabs(den) < 1e-12) continue;
            const Point2Dd w{v[j].x - a.x, v[j].y - a.y};
            const double t = (w.x * e.y - w.y * e.x) / den;
            const double u = (w.x * d.y - w.y * d.x) / den;
            if (t > 0 && t < 1 && u >= 0 && u <= 1) ts.push_back(t);
        }
    }
    std::sort(ts.begin(), ts.end());
    for (size_t i = 1; i < ts.size(); ++i) {
        if (ts[i] - ts[i - 1] < 1e-9) continue;
        const double m = (ts[i] + ts[i - 1]) / 2;
        if (InsidePolygons(rings, Point2Dd(a.x + d.x * m, a.y + d.y * m)))
            out.emplace_back(Point2Dd(a.x + d.x * ts[i - 1], a.y + d.y * ts[i - 1]),
                             Point2Dd(a.x + d.x * ts[i], a.y + d.y * ts[i]));
    }
}

// ===== THE WRITER =====

class OdgWriter {
public:
    OdgWriter(const VectorDocument& doc, const ConversionOptions& options, bool package)
        : doc_(doc), options_(options), package_(package) {}

    std::string Run(bool templateType);

private:
    const VectorDocument& doc_;
    const ConversionOptions& options_;
    bool package_;

    std::string body_;
    // Automatic styles, deduplicated on their XML: properties -> name.
    std::map<std::string, std::string> graphicStyles_, paragraphStyles_, textStyles_, pageStyles_;
    std::string autoStylesXml_;
    // Named drawing styles (office:styles) and font faces.
    std::map<std::string, std::string> namedObjects_;   // xml-without-name -> name
    std::string namedXml_;
    int namedCount_ = 0;
    std::set<std::string> fonts_;
    std::vector<std::string> layers_;
    std::map<std::string, std::pair<bool, bool>> layerFlags_;
    struct Picture { std::string Path, Mime; std::vector<uint8_t> Bytes; };
    std::vector<Picture> pictures_;
    std::map<std::string, int> skipped_;

    void Skip(const std::string& what) { ++skipped_[what]; }

    // What the format could not hold, once per export.
    void Report() const {
        if (!options_.WarningCallback) return;
        for (const auto& [what, count] : skipped_)
            options_.WarningCallback("ODG: " + what + (count > 1 ? " (x" + std::to_string(count) + ")" : ""));
    }

    std::string AutoStyle(std::map<std::string, std::string>& set, const char* prefix, const char* family,
                          const std::string& props) {
        auto it = set.find(props);
        if (it != set.end()) return it->second;
        const std::string name = prefix + std::to_string(set.size() + 1);
        set[props] = name;
        autoStylesXml_ += "<style:style style:name=\"" + name + "\" style:family=\"" + family + "\">" + props + "</style:style>\n";
        return name;
    }

    // A named drawing style: `make(name)` gives its XML for a name.
    template <typename Make>
    std::string Named(const std::string& kind, const std::string& display, Make make) {
        const std::string probe = make(std::string("\x01"));
        auto it = namedObjects_.find(kind + probe);
        if (it != namedObjects_.end()) return it->second;
        const std::string name = StyleName(display + " " + std::to_string(++namedCount_));
        namedObjects_[kind + probe] = name;
        namedXml_ += make(name) + "\n";
        return name;
    }

    // ----- fills -----
    std::string GradientStyle(const GradientData& g, const Rect2Dd& b, const Matrix3x3& m, double opacity) {
        auto stopsXml = [&](const std::vector<GradientStop>& stops) {
            std::string x;
            for (const auto& s : stops)
                x += "<loext:gradient-stop svg:offset=\"" + FormatFixedClassic(s.position, 4) +
                     "\" loext:color-type=\"rgb\" loext:color-value=\"" + Hex(s.color) + "\"/>";
            return x;
        };
        (void)opacity;
        if (const auto* l = std::get_if<LinearGradientData>(&g)) {
            if (l->Stops.empty() || b.width <= 0 || b.height <= 0) return {};
            Point2Dd s = l->Start, e = l->End;
            if (l->Units == GradientUnits::ObjectBoundingBox) {
                // Fractions of the element's own box: resolved by the caller
                // into page coordinates through `m` with `b` the local box.
                s = Point2Dd(localBox_.x + s.x * localBox_.width, localBox_.y + s.y * localBox_.height);
                e = Point2Dd(localBox_.x + e.x * localBox_.width, localBox_.y + e.y * localBox_.height);
            }
            if (l->Transform) { s = l->Transform->Transform(s); e = l->Transform->Transform(e); }
            s = m.Transform(s);
            e = m.Transform(e);
            Point2Dd u{e.x - s.x, e.y - s.y};
            const double len = std::hypot(u.x, u.y);
            if (len < 1e-9) return {};
            u = {u.x / len, u.y / len};
            const double angle = std::atan2(u.x, u.y) * 180.0 / kPi;
            // ODF's axis runs across the whole box; the model's from Start
            // to End: the stops are moved onto the box's axis.
            const Point2Dd c{b.x + b.width / 2, b.y + b.height / 2};
            const double half = (std::fabs(b.width * u.x) + std::fabs(b.height * u.y)) / 2;
            const double pmin = -half, span = 2 * half;
            auto proj = [&](const Point2Dd& p) { return (p.x - c.x) * u.x + (p.y - c.y) * u.y; };
            const double s0 = proj(s);
            std::vector<GradientStop> stops = l->Stops;
            std::sort(stops.begin(), stops.end(), [](auto& a, auto& b2) { return a.position < b2.position; });
            std::vector<GradientStop> mapped;
            mapped.emplace_back(0.0, ColorAt(stops, ((pmin - s0) / len)));
            for (const auto& st : stops) {
                const double t = (s0 + st.position * len - pmin) / span;
                if (t > 0 && t < 1) mapped.emplace_back(t, st.color);
            }
            mapped.emplace_back(1.0, ColorAt(stops, ((pmin + span - s0) / len)));
            return Named("gradient", "Gradient", [&](const std::string& name) {
                return "<draw:gradient draw:name=\"" + name + "\" draw:style=\"linear\" draw:start-color=\"" +
                       Hex(mapped.front().color) + "\" draw:end-color=\"" + Hex(mapped.back().color) +
                       "\" draw:start-intensity=\"100%\" draw:end-intensity=\"100%\" draw:angle=\"" +
                       FormatFixedClassic(std::fmod(angle + 360.0, 360.0), 2) + "deg\" draw:border=\"0%\">" +
                       stopsXml(mapped) + "</draw:gradient>";
            });
        }
        if (const auto* r = std::get_if<RadialGradientData>(&g)) {
            if (r->Stops.empty() || b.width <= 0 || b.height <= 0) return {};
            // The circle's centre and its two radii as vectors, carried onto
            // the page: through the bounding box (ObjectBoundingBox units),
            // the gradient's own Transform and the element's placement. Equal
            // and square to each other, it is still a circle; otherwise the
            // ellipse it has become.
            // As the renderer draws it (MakeRadialGradient): a bounding-box
            // radial with no Transform is the circle of the box's longer side,
            // one with a Transform the box's unit square stretched.
            Matrix3x3 place = m;
            Point2Dd centre = r->Center;
            double radius = r->Radius;
            const bool box = r->Units == GradientUnits::ObjectBoundingBox;
            if (r->Transform) {
                place = place * *r->Transform;
                if (box)
                    place = place * Matrix3x3::Translate(localBox_.x, localBox_.y) *
                            Matrix3x3::Scale(localBox_.width, localBox_.height);
            } else if (box) {
                centre = Point2Dd(localBox_.x + centre.x * localBox_.width, localBox_.y + centre.y * localBox_.height);
                radius *= std::max(localBox_.width, localBox_.height);
            }
            const Point2Dd c = place.Transform(centre);
            const Point2Dd ax{place.m[0][0] * radius, place.m[1][0] * radius};
            const Point2Dd ay{place.m[0][1] * radius, place.m[1][1] * radius};
            const double rx = std::hypot(ax.x, ax.y), ry = std::hypot(ay.x, ay.y);
            if (rx < 1e-9 || ry < 1e-9) return {};
            const bool elliptical = std::fabs(rx - ry) > 1e-3 * std::max(rx, ry);
            if (elliptical && std::fabs(ax.x * ay.x + ax.y * ay.y) > 1e-3 * rx * ry)
                Skip("sheared radial gradient (written as the ellipse of its axes)");
            // ODF's reach: half the diagonal for a radial gradient, the box's
            // width and height over the square root of two for an ellipsoid
            // (UltraCanvasODGReader.cpp) - the stops are moved onto it.
            const double scale = elliptical ? std::sqrt((rx * ry) / ((b.width / std::sqrt(2.0)) * (b.height / std::sqrt(2.0))))
                                            : rx / (std::hypot(b.width, b.height) / 2);
            std::vector<GradientStop> stops = r->Stops;
            std::sort(stops.begin(), stops.end(), [](auto& a, auto& b2) { return a.position < b2.position; });
            // ODF offset 0 is the outside (start colour), 1 the centre.
            std::vector<GradientStop> mapped;
            mapped.emplace_back(0.0, ColorAt(stops, 1.0 / scale));
            for (auto it = stops.rbegin(); it != stops.rend(); ++it) {
                const double o = 1.0 - it->position * scale;
                if (o > 0 && o < 1) mapped.emplace_back(o, it->color);
            }
            mapped.emplace_back(1.0, stops.front().color);
            const double cx = (c.x - b.x) / b.width, cy = (c.y - b.y) / b.height;
            // The ellipse's turn, counter-clockwise on the page as draw:angle
            // counts it: its first axis's direction, negated for y-down.
            std::string shape = " draw:style=\"radial\"";
            if (elliptical) {
                const double angle = -std::atan2(ax.y, ax.x) * 180.0 / kPi;
                shape = " draw:style=\"ellipsoid\" draw:angle=\"" +
                        FormatFixedClassic(std::fmod(angle + 360.0, 360.0), 2) + "deg\"";
            }
            return Named("gradient", "Gradient", [&](const std::string& name) {
                return "<draw:gradient draw:name=\"" + name + "\"" + shape + " draw:cx=\"" + Pct(cx) +
                       "\" draw:cy=\"" + Pct(cy) + "\" draw:start-color=\"" + Hex(mapped.front().color) +
                       "\" draw:end-color=\"" + Hex(mapped.back().color) +
                       "\" draw:start-intensity=\"100%\" draw:end-intensity=\"100%\" draw:border=\"0%\">" +
                       stopsXml(mapped) + "</draw:gradient>";
            });
        }
        return {};
    }

    const GradientData* ResolveGradient(const FillData& f) const {
        if (const auto* g = std::get_if<GradientData>(&f)) return g;
        if (const auto* id = std::get_if<std::string>(&f)) {
            auto d = doc_.GetDefinition(*id);
            if (!d) d = doc_.FindElementById(*id);
            if (auto* vg = dynamic_cast<const VectorGradient*>(d.get())) return &vg->Data;
        }
        return nullptr;
    }

    Rect2Dd localBox_;   // the element's own bounds, for bounding-box gradients

    std::string FillProps(const VectorStyle& st, bool fillable, const Rect2Dd& pageBox, const Matrix3x3& m,
                          double opacity) {
        if (!fillable || !st.Fill || std::holds_alternative<std::monostate>(*st.Fill)) return " draw:fill=\"none\"";
        double alpha = st.FillOpacity * opacity;
        if (st.Transparency && !st.Transparency->IsGradient()) alpha *= 1.0 - st.Transparency->Level;
        std::string p;
        if (const auto* c = std::get_if<Color>(&*st.Fill)) {
            alpha *= c->a / 255.0;
            p = " draw:fill=\"solid\" draw:fill-color=\"" + Hex(*c) + "\"";
        } else if (const GradientData* g = ResolveGradient(*st.Fill)) {
            const std::string name = GradientStyle(*g, pageBox, m, opacity);
            if (!name.empty()) {
                p = " draw:fill=\"gradient\" draw:fill-gradient-name=\"" + name + "\"";
            } else {
                std::vector<GradientStop> stops;
                if (auto* l = std::get_if<LinearGradientData>(g)) stops = l->Stops;
                else if (auto* r = std::get_if<RadialGradientData>(g)) stops = r->Stops;
                else if (auto* k = std::get_if<ConicalGradientData>(g)) stops = k->Stops;
                Skip("conical or mesh gradient (written as its average colour)");
                p = " draw:fill=\"solid\" draw:fill-color=\"" + Hex(Average(stops)) + "\"";
            }
        } else {
            Skip("pattern fill (written unfilled)");
            return " draw:fill=\"none\"";
        }
        if (alpha < 0.999) p += " draw:opacity=\"" + Pct(std::clamp(alpha, 0.0, 1.0)) + "\"";
        if (st.Transparency && st.Transparency->IsGradient()) p += OpacityGradient(*st.Transparency, pageBox, m);
        return p;
    }

    std::string OpacityGradient(const TransparencyData& t, const Rect2Dd& b, const Matrix3x3& m) {
        if (b.width <= 0 || b.height <= 0) return {};
        const float first = t.Stops.front().Level, last = t.Stops.back().Level;
        std::string attrs;
        if (t.Shape == TransparencyShape::Linear) {
            const Point2Dd s = m.Transform(t.Start), e = m.Transform(t.End);
            const double angle = std::atan2(e.x - s.x, e.y - s.y) * 180.0 / kPi;
            attrs = "draw:style=\"linear\" draw:angle=\"" + FormatFixedClassic(std::fmod(angle + 360.0, 360.0), 2) +
                    "deg\" draw:start=\"" + Pct(1.0 - first) + "\" draw:end=\"" + Pct(1.0 - last) + "\"";
        } else {
            const Point2Dd c = m.Transform(t.Start);
            attrs = "draw:style=\"radial\" draw:cx=\"" + Pct((c.x - b.x) / b.width) + "\" draw:cy=\"" +
                    Pct((c.y - b.y) / b.height) + "\" draw:start=\"" + Pct(1.0 - last) + "\" draw:end=\"" +
                    Pct(1.0 - first) + "\"";
        }
        const std::string name = Named("opacity", "Transparency", [&](const std::string& n) {
            return "<draw:opacity draw:name=\"" + n + "\" " + attrs + " draw:border=\"0%\"/>";
        });
        return " draw:opacity-name=\"" + name + "\"";
    }

    // ----- strokes -----
    std::string MarkerStyle(ArrowheadKind kind) {
        struct M { const char* display; const char* viewBox; const char* d; };
        M m{"Arrow", "0 0 20 30", "M10 0l-10 30h20z"};
        switch (kind) {
            case ArrowheadKind::OpenArrow: m = {"Line Arrow", "0 0 20 30", "M10 0l-10 30h4l6-20 6 20h4z"}; break;
            case ArrowheadKind::Circle:
            case ArrowheadKind::Spot: m = {"Circle", "0 0 20 20", "M10 0a10 10 0 1 1 0 20a10 10 0 1 1 0-20z"}; break;
            case ArrowheadKind::Square: m = {"Square", "0 0 10 10", "M0 0h10v10h-10z"}; break;
            case ArrowheadKind::Diamond:
            case ArrowheadKind::SolidDiamond:
            case ArrowheadKind::HollowDiamond: m = {"Square 45", "0 0 20 20", "M10 0l10 10-10 10-10-10z"}; break;
            case ArrowheadKind::Bar: m = {"Line short", "0 0 30 6", "M0 0h30v6h-30z"}; break;
            default: break;
        }
        // Markers are matched by name, so each kind keeps LibreOffice's.
        const std::string name = StyleName(m.display);
        if (namedObjects_.emplace("marker" + name, name).second)
            namedXml_ += "<draw:marker draw:name=\"" + name + "\" draw:display-name=\"" + m.display +
                         "\" svg:viewBox=\"" + m.viewBox + "\" svg:d=\"" + m.d + "\"/>\n";
        return name;
    }

    std::string StrokeProps(const VectorStyle& st, const Matrix3x3& m, double opacity, bool markers) {
        if (!st.Stroke || std::holds_alternative<std::monostate>(st.Stroke->Fill) || st.Stroke->Width <= 0)
            return " draw:stroke=\"none\"";
        const StrokeData& s = *st.Stroke;
        const double scale = Scale(m);
        const double width = s.Width * scale;
        Color colour = Colors::Black;
        if (const auto* c = std::get_if<Color>(&s.Fill)) colour = *c;
        else if (const GradientData* g = ResolveGradient(s.Fill)) {
            std::vector<GradientStop> stops;
            if (auto* l = std::get_if<LinearGradientData>(g)) stops = l->Stops;
            else if (auto* r = std::get_if<RadialGradientData>(g)) stops = r->Stops;
            colour = Average(stops);
            Skip("gradient stroke (written as its average colour)");
        }
        double alpha = st.StrokeOpacity * s.Opacity * opacity * colour.a / 255.0;
        std::string p;
        if (!s.DashArray.empty()) {
            const std::vector<double>& d = s.DashArray;
            const double l1 = d[0] * scale, gap = (d.size() > 1 ? d[1] : d[0]) * scale;
            std::string attrs = "draw:style=\"" + std::string(s.LineCap == StrokeLineCap::Round ? "round" : "rect") +
                                "\" draw:dots1=\"1\" draw:dots1-length=\"" + Cm(l1) + "\"";
            if (d.size() >= 4) attrs += " draw:dots2=\"1\" draw:dots2-length=\"" + Cm(d[2] * scale) + "\"";
            attrs += " draw:distance=\"" + Cm(gap) + "\"";
            const std::string name = Named("dash", "Dash", [&](const std::string& n) {
                return "<draw:stroke-dash draw:name=\"" + n + "\" " + attrs + "/>";
            });
            p = " draw:stroke=\"dash\" draw:stroke-dash=\"" + name + "\"";
        } else {
            p = " draw:stroke=\"solid\"";
        }
        p += " svg:stroke-width=\"" + Cm(width) + "\" svg:stroke-color=\"" + Hex(colour) + "\"";
        if (alpha < 0.999) p += " svg:stroke-opacity=\"" + Pct(std::clamp(alpha, 0.0, 1.0)) + "\"";
        p += std::string(" draw:stroke-linejoin=\"") +
             (s.LineJoin == StrokeLineJoin::Miter ? "miter" : s.LineJoin == StrokeLineJoin::Bevel ? "bevel" : "round") + "\"";
        p += std::string(" svg:stroke-linecap=\"") +
             (s.LineCap == StrokeLineCap::Round ? "round" : s.LineCap == StrokeLineCap::Square ? "square" : "butt") + "\"";
        if (markers) {
            auto marker = [&](const ArrowheadData& a, const char* which) {
                if (!a.IsSet()) return;
                const double markerWidth = 2.0 * width * a.Scale;
                p += std::string(" draw:marker-") + which + "=\"" + MarkerStyle(a.Kind) + "\" draw:marker-" + which +
                     "-width=\"" + Cm(markerWidth) + "\"";
                const bool centred = a.Kind == ArrowheadKind::Circle || a.Kind == ArrowheadKind::Spot ||
                                     a.Kind == ArrowheadKind::Square || a.Kind == ArrowheadKind::SolidDiamond;
                if (centred) p += std::string(" draw:marker-") + which + "-center=\"true\"";
            };
            marker(s.StartArrow, "start");
            marker(s.EndArrow, "end");
        }
        if (s.HasWidthProfile() || s.HasBrush()) Skip("width profile or brush (written as a plain line)");
        return p;
    }

    std::string ShadowProps(const VectorEffects& fx, const Matrix3x3& m) {
        if (fx.Feather || fx.Contour || fx.Bevel) Skip("feather, contour or bevel effect");
        if (!fx.Shadow) return " draw:shadow=\"hidden\"";
        const ShadowEffect& s = *fx.Shadow;
        if (s.Kind != ShadowKind::Wall) Skip("floor or glow shadow (written as an offset shadow)");
        const Point2Dd o{m.m[0][0] * s.Offset.x + m.m[0][1] * s.Offset.y, m.m[1][0] * s.Offset.x + m.m[1][1] * s.Offset.y};
        std::string p = " draw:shadow=\"visible\" draw:shadow-offset-x=\"" + Cm(o.x) + "\" draw:shadow-offset-y=\"" +
                        Cm(o.y) + "\" draw:shadow-color=\"" + Hex(s.Colour) + "\" draw:shadow-opacity=\"" +
                        Pct(std::clamp(static_cast<double>(s.Darkness), 0.0, 1.0)) + "\"";
        if (s.Blur > 0) p += " loext:shadow-blur=\"" + Cm(s.Blur * Scale(m)) + "\"";
        return p;
    }

    std::string ShapeStyle(const VectorElement& e, bool fillable, bool markers, const Rect2Dd& pageBox,
                           const Matrix3x3& m, double opacity) {
        localBox_ = e.GetBoundingBox();
        const double alpha = opacity * e.Style.Opacity;
        const std::string props = "<style:graphic-properties" + StrokeProps(e.Style, m, alpha, markers) +
                                  FillProps(e.Style, fillable, pageBox, m, alpha) + ShadowProps(e.Effects, m) +
                                  " draw:textarea-horizontal-align=\"center\" draw:textarea-vertical-align=\"middle\"/>";
        return AutoStyle(graphicStyles_, "gr", "graphic", props);
    }

    // ----- shapes -----
    std::string Common(const std::string& style, const std::string& layer, const VectorElement& e) {
        std::string a = " draw:style-name=\"" + style + "\" draw:layer=\"" + Escape(layer) + "\"";
        if (!e.Id.empty()) a += " draw:name=\"" + Escape(e.Id) + "\"";
        return a;
    }

    void WritePath(const VectorElement& e, const PathData& local, const Matrix3x3& m, const std::string& layer,
                   double opacity) {
        // The path on the page, absolute moves, lines and cubics.
        PathData page;
        bool closed = false;
        auto add = [&](PathCommandType t, std::initializer_list<Point2Dd> pts) {
            PathCommand c;
            c.Type = t;
            for (const Point2Dd& p : pts) {
                const Point2Dd q = m.Transform(p);
                c.Parameters.push_back(static_cast<float>(q.x));
                c.Parameters.push_back(static_cast<float>(q.y));
            }
            page.commands.push_back(std::move(c));
        };
        // LibreOffice fills a path only when every subpath is closed, so a
        // filled one has its open subpaths closed (the fill is the same).
        const bool filled = e.Style.Fill && !std::holds_alternative<std::monostate>(*e.Style.Fill);
        bool open = false, anyOpen = false;
        for (const auto& s : PathOps::NormalizePath(local)) {
            if (s.kind == PathOps::FlatSeg::Move) {
                if (open) { anyOpen = true; if (filled) add(PathCommandType::ClosePath, {}); }
                add(PathCommandType::MoveTo, {s.p[0]});
                open = false;
            } else {
                if (s.kind == PathOps::FlatSeg::Line) add(PathCommandType::LineTo, {s.p[0]});
                else add(PathCommandType::CurveTo, {s.p[0], s.p[1], s.p[2]});
                open = true;
            }
            if (s.closeAfter) { add(PathCommandType::ClosePath, {}); open = false; }
        }
        if (open) { anyOpen = true; if (filled) add(PathCommandType::ClosePath, {}); }
        closed = filled || !anyOpen;
        if (page.commands.empty()) return;
        // LibreOffice places a path by the curve's own bounds (its control
        // points may lie outside them), as it writes them itself; a
        // straight line has no width or height, which the viewBox cannot
        // hold.
        Rect2Dd box = page.GetBounds();
        if (box.width < 1e-3) { box.x -= 0.0005; box.width = 0.001; }
        if (box.height < 1e-3) { box.y -= 0.0005; box.height = 0.001; }
        std::string d;
        for (const auto& c : page.commands) {
            const auto& v = c.Parameters;
            switch (c.Type) {
                case PathCommandType::MoveTo: d += "M" + H(v[0]) + " " + H(v[1]) + " "; break;
                case PathCommandType::LineTo: d += "L" + H(v[0]) + " " + H(v[1]) + " "; break;
                case PathCommandType::CurveTo:
                    d += "C" + H(v[0]) + " " + H(v[1]) + " " + H(v[2]) + " " + H(v[3]) + " " + H(v[4]) + " " + H(v[5]) + " ";
                    break;
                default: d += "Z "; break;
            }
        }
        d.pop_back();
        const std::string style = ShapeStyle(e, closed, !closed, box, m, opacity);
        body_ += "<draw:path" + Common(style, layer, e) + " svg:x=\"" + Cm(box.x) + "\" svg:y=\"" + Cm(box.y) +
                 "\" svg:width=\"" + Cm(box.width) + "\" svg:height=\"" + Cm(box.height) + "\" svg:viewBox=\"" +
                 H(box.x) + " " + H(box.y) + " " + H(box.width) + " " + H(box.height) + "\" svg:d=\"" + d + "\"/>\n";
    }

    void WritePoly(const VectorElement& e, const std::vector<Point2Dd>& local, bool closed, const Matrix3x3& m,
                   const std::string& layer, double opacity) {
        if (local.empty()) return;
        std::vector<Point2Dd> pts;
        for (const auto& p : local) pts.push_back(m.Transform(p));
        double x0 = pts[0].x, y0 = pts[0].y, x1 = x0, y1 = y0;
        for (const auto& p : pts) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
        const double w = std::max(x1 - x0, 0.001), h = std::max(y1 - y0, 0.001);
        std::string points;
        for (const auto& p : pts) points += H(p.x - x0) + "," + H(p.y - y0) + " ";
        points.pop_back();
        const Rect2Dd box(x0, y0, w, h);
        const std::string style = ShapeStyle(e, closed, !closed, box, m, opacity);
        body_ += std::string("<draw:") + (closed ? "polygon" : "polyline") + Common(style, layer, e) + " svg:x=\"" +
                 Cm(x0) + "\" svg:y=\"" + Cm(y0) + "\" svg:width=\"" + Cm(w) + "\" svg:height=\"" + Cm(h) +
                 "\" svg:viewBox=\"0 0 " + H(w) + " " + H(h) + "\" draw:points=\"" + points + "\"/>\n";
    }

    void WriteShape(const VectorElement& e, const Matrix3x3& m, const std::string& layer, double opacity) {
        switch (e.Type) {
            case VectorElementType::Rectangle:
            case VectorElementType::RoundedRectangle: {
                const auto& r = static_cast<const VectorRect&>(e);
                if (AxisAligned(m) && std::fabs(r.RadiusX - r.RadiusY) < 1e-6 &&
                    std::fabs(m.m[0][0] - m.m[1][1]) < 1e-9) {
                    const Rect2Dd box = m.Transform(r.Bounds);
                    const std::string style = ShapeStyle(e, true, false, box, m, opacity);
                    body_ += "<draw:rect" + Common(style, layer, e) + " svg:x=\"" + Cm(box.x) + "\" svg:y=\"" +
                             Cm(box.y) + "\" svg:width=\"" + Cm(box.width) + "\" svg:height=\"" + Cm(box.height) + "\"";
                    if (r.RadiusX > 0) body_ += " draw:corner-radius=\"" + Cm(r.RadiusX * m.m[0][0]) + "\"";
                    body_ += "/>\n";
                    return;
                }
                break;
            }
            case VectorElementType::Circle:
            case VectorElementType::Ellipse: {
                if (!AxisAligned(m)) break;
                Point2Dd c;
                double rx, ry;
                if (e.Type == VectorElementType::Circle) {
                    const auto& ci = static_cast<const VectorCircle&>(e);
                    c = ci.Center;
                    rx = ry = ci.Radius;
                } else {
                    const auto& el = static_cast<const VectorEllipse&>(e);
                    c = el.Center;
                    rx = el.RadiusX;
                    ry = el.RadiusY;
                }
                const Rect2Dd box = m.Transform(Rect2Dd(c.x - rx, c.y - ry, 2 * rx, 2 * ry));
                const std::string style = ShapeStyle(e, true, false, box, m, opacity);
                body_ += "<draw:ellipse" + Common(style, layer, e) + " svg:x=\"" + Cm(box.x) + "\" svg:y=\"" +
                         Cm(box.y) + "\" svg:width=\"" + Cm(box.width) + "\" svg:height=\"" + Cm(box.height) + "\"/>\n";
                return;
            }
            case VectorElementType::Line: {
                const auto& l = static_cast<const VectorLine&>(e);
                const Point2Dd a = m.Transform(l.Start), b = m.Transform(l.End);
                const Rect2Dd box(std::min(a.x, b.x), std::min(a.y, b.y), std::fabs(b.x - a.x), std::fabs(b.y - a.y));
                const std::string style = ShapeStyle(e, false, true, box, m, opacity);
                body_ += "<draw:line" + Common(style, layer, e) + " svg:x1=\"" + Cm(a.x) + "\" svg:y1=\"" + Cm(a.y) +
                         "\" svg:x2=\"" + Cm(b.x) + "\" svg:y2=\"" + Cm(b.y) + "\"/>\n";
                return;
            }
            case VectorElementType::Polyline:
                WritePoly(e, static_cast<const VectorPolyline&>(e).Points, false, m, layer, opacity);
                return;
            case VectorElementType::Polygon:
                WritePoly(e, static_cast<const VectorPolygon&>(e).Points, true, m, layer, opacity);
                return;
            case VectorElementType::Path:
                WritePath(e, static_cast<const VectorPath&>(e).Path, m, layer, opacity);
                return;
            default:
                break;
        }
        // A rotated, sheared or unevenly scaled rectangle or ellipse: its outline.
        PathData outline;
        if (BuildOutlinePath(e, outline)) WritePath(e, outline, m, layer, opacity);
    }

    // A frame's place: its local top-left, size, and the matrix taking it to
    // the page, as svg:x/y/width/height or a draw:transform.
    std::string FrameGeometry(const Rect2Dd& local, const Matrix3x3& m, double& sizeScale) {
        if (AxisAligned(m)) {
            const Rect2Dd box = m.Transform(local);
            sizeScale = std::sqrt(m.m[0][0] * m.m[1][1]);
            return " svg:x=\"" + Cm(box.x) + "\" svg:y=\"" + Cm(box.y) + "\" svg:width=\"" + Cm(box.width) +
                   "\" svg:height=\"" + Cm(box.height) + "\"";
        }
        const double theta = std::atan2(m.m[1][0], m.m[0][0]);
        const double sx = std::hypot(m.m[0][0], m.m[1][0]);
        const double sy = sx > 1e-12 ? m.Determinant() / sx : 0;
        if (sy < 0) Skip("mirrored text or picture (written unmirrored)");
        const double shear = sx > 1e-12 ? (m.m[0][0] * m.m[0][1] + m.m[1][0] * m.m[1][1]) / (sx * sx) : 0;
        if (std::fabs(shear) > 1e-6) Skip("sheared text or picture (written unsheared)");
        sizeScale = std::sqrt(std::fabs(sx * sy));
        const Point2Dd origin = m.Transform(Point2Dd(local.x, local.y));
        return " svg:width=\"" + Cm(local.width * sx) + "\" svg:height=\"" + Cm(local.height * std::fabs(sy)) +
               "\" draw:transform=\"rotate (" + FormatFixedClassic(-theta, 12) + ") translate (" + Cm(origin.x) + " " +
               Cm(origin.y) + ")\"";
    }

    std::string TextSpanStyle(const VectorTextStyle& st, double scale, const Color& colour) {
        std::string p = "<style:text-properties";
        if (!st.FontFamily.empty()) {
            fonts_.insert(st.FontFamily);
            p += " style:font-name=\"" + Escape(st.FontFamily) + "\"";
        }
        p += " fo:font-size=\"" + Pt(st.FontSize * scale) + "\"";
        p += std::string(" fo:font-weight=\"") +
             (st.Weight == FontWeight::Bold ? "bold" : st.Weight == FontWeight::ExtraBold ? "800"
                                                      : st.Weight == FontWeight::Light ? "300" : "normal") + "\"";
        p += std::string(" fo:font-style=\"") +
             (st.Slant == FontSlant::Italic ? "italic" : st.Slant == FontSlant::Oblique ? "oblique" : "normal") + "\"";
        p += " fo:color=\"" + Hex(colour) + "\"";
        if (st.Underline) p += " style:text-underline-style=\"solid\" style:text-underline-width=\"auto\" style:text-underline-color=\"font-color\"";
        if (st.StrikeThrough) p += " style:text-line-through-style=\"solid\"";
        if (std::fabs(st.LetterSpacing) > 1e-6) p += " fo:letter-spacing=\"" + Cm(st.LetterSpacing * scale) + "\"";
        p += "/>";
        return AutoStyle(textStyles_, "T", "text", p);
    }

    static std::string TextContent(const std::string& s) {
        // Runs of spaces, tabs and line breaks are ODF elements.
        std::string out;
        size_t i = 0;
        while (i < s.size()) {
            const char c = s[i];
            if (c == ' ') {
                size_t j = i;
                while (j < s.size() && s[j] == ' ') ++j;
                const size_t n = j - i;
                const bool edge = i == 0 || j == s.size();
                if (n == 1 && !edge) out += ' ';
                else {
                    if (!edge) { out += ' '; }
                    const size_t extra = edge ? n : n - 1;
                    out += extra == 1 ? "<text:s/>" : "<text:s text:c=\"" + std::to_string(extra) + "\"/>";
                }
                i = j;
            } else if (c == '\t') {
                out += "<text:tab/>";
                ++i;
            } else if (c == '\n') {
                out += "<text:line-break/>";
                ++i;
            } else if (c == '\r') {
                ++i;
            } else {
                const size_t j = s.find_first_of(" \t\n\r", i);
                out += Escape(s.substr(i, j == std::string::npos ? std::string::npos : j - i));
                i = j == std::string::npos ? s.size() : j;
            }
        }
        return out;
    }

    void WriteText(const VectorText& t, const Matrix3x3& m, const std::string& layer, double opacity) {
        (void)opacity;
        Color base = Colors::Black;
        if (t.Style.Fill)
            if (const auto* c = std::get_if<Color>(&*t.Style.Fill)) base = *c;
        // Lines: a span with its own position starts one, as the renderer
        // reads them.
        struct Chunk { Point2Dd Start; std::vector<const TextSpanData*> Spans; };
        std::vector<Chunk> chunks(1);
        chunks[0].Start = t.Position;
        for (const auto& span : t.Spans) {
            if (span.Position) {
                if (!chunks.back().Spans.empty()) chunks.emplace_back();
                chunks.back().Start = *span.Position;
            }
            chunks.back().Spans.push_back(&span);
        }
        for (const Chunk& chunk : chunks) {
            if (chunk.Spans.empty()) continue;
            double size = 0, width = 0;
            for (const TextSpanData* s : chunk.Spans) {
                const float fs = s->Style.FontSize > 0 ? s->Style.FontSize : t.BaseStyle.FontSize;
                size = std::max(size, static_cast<double>(fs));
                size_t chars = 0;
                for (unsigned char c : s->Text) if ((c & 0xC0) != 0x80) ++chars;
                width += chars * fs * 0.52 + chars * s->Style.LetterSpacing;
            }
            if (size <= 0) size = 12;
            const TextAnchor anchor = t.BaseStyle.Anchor;
            double x0 = chunk.Start.x;
            if (anchor == TextAnchor::Middle) x0 -= width / 2;
            else if (anchor == TextAnchor::End) x0 -= width;
            const Rect2Dd local(x0, chunk.Start.y - size * 0.905, std::max(width, 1.0), size * 1.15);
            double scale = 1;
            const std::string geometry = FrameGeometry(local, m, scale);
            const char* hAlign = anchor == TextAnchor::Middle ? "center" : anchor == TextAnchor::End ? "right" : "left";
            const std::string gstyle = AutoStyle(graphicStyles_, "gr", "graphic",
                "<style:graphic-properties draw:stroke=\"none\" draw:fill=\"none\" draw:shadow=\"hidden\""
                " draw:auto-grow-width=\"true\" draw:auto-grow-height=\"true\" fo:wrap-option=\"no-wrap\""
                " fo:min-height=\"0cm\" fo:min-width=\"0cm\" fo:padding-top=\"0cm\" fo:padding-bottom=\"0cm\""
                " fo:padding-left=\"0cm\" fo:padding-right=\"0cm\" draw:textarea-vertical-align=\"top\""
                " draw:textarea-horizontal-align=\"" + std::string(hAlign) + "\"/>");
            const std::string pstyle = AutoStyle(paragraphStyles_, "P", "paragraph",
                std::string("<style:paragraph-properties fo:text-align=\"") +
                (anchor == TextAnchor::Middle ? "center" : anchor == TextAnchor::End ? "end" : "start") + "\"/>");
            std::string para;
            for (const TextSpanData* s : chunk.Spans) {
                VectorTextStyle st = s->Style;
                if (st.FontFamily.empty()) st.FontFamily = t.BaseStyle.FontFamily;
                if (st.FontSize <= 0) st.FontSize = t.BaseStyle.FontSize;
                const Color colour = s->Style.FillColor.value_or(base);
                para += "<text:span text:style-name=\"" + TextSpanStyle(st, scale, colour) + "\">" +
                        TextContent(s->Text) + "</text:span>";
            }
            body_ += "<draw:frame" + Common(gstyle, layer, t) + geometry + "><draw:text-box><text:p text:style-name=\"" +
                     pstyle + "\">" + para + "</text:p></draw:text-box></draw:frame>\n";
        }
    }

    void WriteImage(const VectorImage& img, const Matrix3x3& m, const std::string& layer) {
        std::vector<uint8_t> bytes = img.EmbeddedData;
        std::string mime = img.MimeType;
        std::string href;
        if (bytes.empty() && img.Source.compare(0, 5, "data:") == 0) {
            const size_t comma = img.Source.find(',');
            const std::string header = img.Source.substr(5, comma == std::string::npos ? 0 : comma - 5);
            if (comma != std::string::npos && header.find(";base64") != std::string::npos) {
                bytes = Base64Decode(img.Source.substr(comma + 1));
                if (mime.empty()) mime = header.substr(0, header.find(';'));
            }
        }
        if (mime.empty()) mime = "image/png";
        if (bytes.empty()) {
            if (img.Source.empty()) { Skip("picture without data"); return; }
            href = img.Source;   // a linked picture stays linked
        }
        double scale = 1;
        const std::string geometry = FrameGeometry(img.Bounds, m, scale);
        const std::string gstyle = AutoStyle(graphicStyles_, "gr", "graphic",
            "<style:graphic-properties draw:stroke=\"none\" draw:fill=\"none\" draw:shadow=\"hidden\"/>");
        std::string image;
        if (!bytes.empty() && package_) {
            const std::string path = "Pictures/image" + std::to_string(pictures_.size() + 1) + "." + ImageExtension(mime);
            pictures_.push_back({path, mime, std::move(bytes)});
            image = "<draw:image xlink:href=\"" + path + "\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\" draw:mime-type=\"" + mime + "\"/>";
        } else if (!bytes.empty()) {
            image = "<draw:image draw:mime-type=\"" + mime + "\"><office:binary-data>" + Base64Encode(bytes, true) +
                    "</office:binary-data></draw:image>";
        } else {
            image = "<draw:image xlink:href=\"" + Escape(href) + "\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\"/>";
        }
        body_ += "<draw:frame" + Common(gstyle, layer, img) + geometry + ">" + image + "</draw:frame>\n";
    }

    // A clip view: ODF has no clipping groups. Straight lines (a hatch) are
    // clipped to the outline here; other contents are written whole.
    void WriteClipView(const VectorClipView& cv, const Matrix3x3& m, const std::string& layer, double opacity) {
        std::vector<FlatSubpath> rings;
        for (const auto& k : cv.KeyholeShapes()) {
            PathData outline;
            if (!k || !BuildOutlinePath(*k, outline)) continue;
            const Matrix3x3 km = k->Transform ? *k->Transform : Matrix3x3::Identity();
            for (auto ring : FlattenPathData(outline)) {
                for (auto& p : ring.Points) p = km.Transform(p);
                if (ring.Points.size() >= 3) rings.push_back(std::move(ring));
            }
        }
        for (const auto& c : cv.Contents()) {
            if (!c) continue;
            std::vector<std::pair<Point2Dd, Point2Dd>> segs;
            if (!rings.empty() && c->Type == VectorElementType::Path && !c->Transform &&
                StraightSegments(static_cast<const VectorPath&>(*c).Path, segs)) {
                std::vector<std::pair<Point2Dd, Point2Dd>> kept;
                for (const auto& [a, b] : segs) ClipSegment(rings, a, b, kept);
                if (kept.empty()) continue;
                auto clipped = std::make_shared<VectorPath>();
                clipped->Style = c->Style;
                clipped->Id = c->Id;
                for (const auto& [a, b] : kept) {
                    clipped->MoveTo(static_cast<float>(a.x), static_cast<float>(a.y));
                    clipped->LineTo(static_cast<float>(b.x), static_cast<float>(b.y));
                }
                WriteShape(*clipped, m, layer, opacity);
                continue;
            }
            if (!rings.empty()) Skip("clip view (its contents written unclipped)");
            Walk(*c, m, layer, opacity);
        }
    }

    void Walk(const VectorElement& e, const Matrix3x3& parent, const std::string& layer, double opacity) {
        if (!e.Style.Visible || !e.Style.Display) { Skip("hidden object"); return; }
        const Matrix3x3 m = e.Transform ? parent * *e.Transform : parent;
        switch (e.Type) {
            case VectorElementType::Group:
            case VectorElementType::Layer:
            case VectorElementType::Symbol:
            case VectorElementType::Blend:
            case VectorElementType::Mould: {
                const auto& g = static_cast<const VectorGroup&>(e);
                if (e.Type == VectorElementType::Blend) Skip("blend (its end objects written)");
                if (e.Type == VectorElementType::Mould) Skip("mould (its objects written unmoulded)");
                const size_t mark = body_.size();
                body_ += "<draw:g>\n";
                const size_t inner = body_.size();
                for (const auto& c : g.Children)
                    if (c) Walk(*c, m, layer, opacity * e.Style.Opacity);
                if (body_.size() == inner) body_.resize(mark);   // nothing drawn
                else body_ += "</draw:g>\n";
                return;
            }
            case VectorElementType::ClipView: {
                const size_t mark = body_.size();
                body_ += "<draw:g>\n";
                const size_t inner = body_.size();
                WriteClipView(static_cast<const VectorClipView&>(e), m, layer, opacity * e.Style.Opacity);
                if (body_.size() == inner) body_.resize(mark);
                else body_ += "</draw:g>\n";
                return;
            }
            case VectorElementType::Use: {
                const auto& u = static_cast<const VectorUse&>(e);
                auto ref = doc_.GetDefinition(u.Reference);
                if (!ref) ref = doc_.FindElementById(u.Reference);
                if (!ref || ref.get() == &e) { Skip("reference to a missing definition"); return; }
                Matrix3x3 um = m * Matrix3x3::Translate(u.Position.x, u.Position.y);
                const Rect2Dd rb = ref->GetBoundingBox();
                if (u.Size.width > 0 && u.Size.height > 0 && rb.width > 0 && rb.height > 0)
                    um = um * Matrix3x3::Scale(u.Size.width / rb.width, u.Size.height / rb.height);
                if (++useDepth_ < 32) Walk(*ref, um, layer, opacity * e.Style.Opacity);
                --useDepth_;
                return;
            }
            case VectorElementType::Text:
                WriteText(static_cast<const VectorText&>(e), m, layer, opacity);
                return;
            case VectorElementType::TextPath:
                Skip("text on a path");
                return;
            case VectorElementType::Image:
                WriteImage(static_cast<const VectorImage&>(e), m, layer);
                return;
            case VectorElementType::LinearGradient:
            case VectorElementType::RadialGradient:
            case VectorElementType::ConicalGradient:
            case VectorElementType::MeshGradient:
            case VectorElementType::Pattern:
            case VectorElementType::Filter:
            case VectorElementType::ClipPath:
            case VectorElementType::Mask:
            case VectorElementType::Marker:
                return;   // definitions, drawn where they are used
            default:
                if (e.Style.ClipPath || e.Style.Mask) Skip("clip path or mask");
                WriteShape(e, m, layer, opacity);
                return;
        }
    }
    int useDepth_ = 0;

    std::string LayerName(const VectorLayer& l, size_t index) {
        std::string n = l.Name.empty() ? "Layer " + std::to_string(index + 1) : l.Name;
        // The master page's layers cannot hold shapes of a page.
        if (n == "background" || n == "backgroundobjects") n = "layout";
        return n;
    }

    std::string Meta() const {
        std::string m = "<office:meta><meta:generator>UltraCanvas</meta:generator>";
        if (!doc_.Title.empty()) m += "<dc:title>" + Escape(doc_.Title) + "</dc:title>";
        if (!doc_.Description.empty()) m += "<dc:description>" + Escape(doc_.Description) + "</dc:description>";
        if (!doc_.Author.empty()) m += "<meta:initial-creator>" + Escape(doc_.Author) + "</meta:initial-creator>";
        return m + "</office:meta>";
    }

    // The page drawn at `longSide` pixels on its longer edge, over white,
    // as a PNG - the package's Thumbnails/thumbnail.png, which a file
    // manager shows without a reader for the drawing (LibreOffice's file
    // dialogs, desktop thumbnailers, the Filer: UltraCanvasEmbeddedPreview).
    // Empty when the page cannot be drawn; the thumbnail is optional.
    std::vector<uint8_t> Thumbnail(double pageW, double pageH, int longSide) const {
        if (pageW <= 0 || pageH <= 0) return {};
        // The pictures in the drawing decode through libvips, which wants its
        // start-up call; a no-op once the application has made it.
        UCImage::InitializeImageSubsysterm(nullptr);
        VectorRasterOptions options;
        if (pageW >= pageH) options.width = longSide;
        else options.height = longSide;
        options.background = RasterPixel(255, 255, 255, 255);
        std::string error;
        auto layer = RasterizeVectorDocument(doc_, options, error);
        if (!layer || layer->GetWidth() <= 0 || layer->GetHeight() <= 0) return {};
        ClipboardDib::RgbaImage image;
        image.width = layer->GetWidth();
        image.height = layer->GetHeight();
        image.pixels.reserve(static_cast<size_t>(image.width) * static_cast<size_t>(image.height) * 4);
        for (int y = 0; y < image.height; ++y)
            for (int x = 0; x < image.width; ++x) {
                const RasterPixel p = layer->GetPixel(x, y);
                image.pixels.insert(image.pixels.end(), {p.r, p.g, p.b, p.a});
            }
        std::vector<uint8_t> png;
        if (!ClipboardDib::EncodePng(image, png)) return {};
        return png;
    }

    std::string FontDecls() const {
        std::string f = "<office:font-face-decls>";
        for (const auto& name : fonts_)
            f += "<style:font-face style:name=\"" + Escape(name) + "\" svg:font-family=\"'" + Escape(name) + "'\"/>";
        return f + "</office:font-face-decls>";
    }
};

std::string OdgWriter::Run(bool templateType) {
    // Page: the document's size; drawing coordinates fitted into it.
    Rect2Dd content = ContentBounds(doc_);
    double pageW = doc_.Size.width, pageH = doc_.Size.height;
    Rect2Dd view = doc_.ViewBox;
    if (pageW <= 0 || pageH <= 0) {
        if (view.width > 0 && view.height > 0) { pageW = view.width; pageH = view.height; }
        else { pageW = std::max(1.0, content.x + content.width); pageH = std::max(1.0, content.y + content.height); }
    }
    Matrix3x3 root = Matrix3x3::Identity();
    if (view.width > 0 && view.height > 0 && (std::fabs(view.width - pageW) > 1e-6 || std::fabs(view.height - pageH) > 1e-6 ||
                                              std::fabs(view.x) > 1e-9 || std::fabs(view.y) > 1e-9)) {
        double sx = pageW / view.width, sy = pageH / view.height;
        double tx = 0, ty = 0;
        if (doc_.PreserveAspectRatio != "none") {
            const double s = std::min(sx, sy);
            tx = (pageW - view.width * s) / 2;
            ty = (pageH - view.height * s) / 2;
            sx = sy = s;
        }
        root = Matrix3x3::Translate(tx, ty) * Matrix3x3::Scale(sx, sy) * Matrix3x3::Translate(-view.x, -view.y);
    }

    for (size_t i = 0; i < doc_.Layers.size(); ++i) {
        const auto& layer = doc_.Layers[i];
        if (!layer) continue;
        const std::string name = LayerName(*layer, i);
        if (std::find(layers_.begin(), layers_.end(), name) == layers_.end()) layers_.push_back(name);
        auto& flags = layerFlags_[name];
        flags.first = flags.first || (layer->Visible && layer->Style.Visible);
        flags.second = flags.second || layer->Locked;
        const Matrix3x3 lm = layer->Transform ? root * *layer->Transform : root;
        for (const auto& c : layer->Children)
            if (c) Walk(*c, lm, name, layer->Opacity * layer->Style.Opacity);
    }

    // ----- styles part -----
    std::string layerSet = "<draw:layer-set>";
    std::vector<std::string> all = {"layout", "background", "backgroundobjects", "controls", "measurelines"};
    for (const auto& l : layers_)
        if (std::find(all.begin(), all.end(), l) == all.end()) all.push_back(l);
    for (const auto& l : all) {
        layerSet += "<draw:layer draw:name=\"" + Escape(l) + "\"";
        auto f = layerFlags_.find(l);
        if (f != layerFlags_.end()) {
            if (!f->second.first) layerSet += " draw:display=\"none\"";
            if (f->second.second) layerSet += " draw:protected=\"true\"";
        }
        layerSet += "/>";
    }
    layerSet += "</draw:layer-set>";

    std::string pageBackground = " draw:fill=\"none\"";
    if (doc_.BackgroundColor)
        pageBackground = " draw:fill=\"solid\" draw:fill-color=\"" + Hex(*doc_.BackgroundColor) + "\"";
    const std::string pageLayout =
        "<style:page-layout style:name=\"PM1\"><style:page-layout-properties fo:margin-top=\"0cm\" "
        "fo:margin-bottom=\"0cm\" fo:margin-left=\"0cm\" fo:margin-right=\"0cm\" fo:page-width=\"" + Cm(pageW) +
        "\" fo:page-height=\"" + Cm(pageH) + "\" style:print-orientation=\"" +
        (pageW > pageH ? "landscape" : "portrait") + "\"/></style:page-layout>"
        "<style:style style:name=\"Mdp1\" style:family=\"drawing-page\"><style:drawing-page-properties "
        "draw:background-size=\"full\"" + pageBackground + "/></style:style>";
    const std::string commonStyles =
        "<office:styles>\n" + namedXml_ +
        "<style:default-style style:family=\"graphic\"><style:graphic-properties svg:stroke-color=\"#000000\" "
        "draw:fill-color=\"#ffffff\"/><style:text-properties style:font-name=\"Liberation Sans\" fo:font-size=\"18pt\"/>"
        "</style:default-style>\n</office:styles>";
    const std::string masterStyles = "<office:master-styles>" + layerSet +
        "<style:master-page style:name=\"Default\" style:page-layout-name=\"PM1\" draw:style-name=\"Mdp1\"/>"
        "</office:master-styles>";
    const std::string pageStyle = AutoStyle(pageStyles_, "dp", "drawing-page",
        "<style:drawing-page-properties draw:background-size=\"full\"" + pageBackground + "/>");
    const std::string body = "<office:body><office:drawing><draw:page draw:name=\"page1\" draw:style-name=\"" +
                             pageStyle + "\" draw:master-page-name=\"Default\">\n" + body_ +
                             "</draw:page></office:drawing></office:body>";
    const char* mime = templateType ? kMimeTemplate : kMimeDrawing;

    if (!package_) {
        std::string flat = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document";
        flat += kNamespaceDecls;
        flat += std::string(" office:mimetype=\"") + mime + "\">\n" + Meta() + "\n" + FontDecls() + "\n" + commonStyles +
                "\n<office:automatic-styles>\n" + pageLayout + "\n" + autoStylesXml_ + "</office:automatic-styles>\n" +
                masterStyles + "\n" + body + "\n</office:document>\n";
        Report();
        return flat;
    }

    const std::string contentXml = std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-content") +
        kNamespaceDecls + ">\n" + FontDecls() + "\n<office:automatic-styles>\n" + autoStylesXml_ +
        "</office:automatic-styles>\n" + body + "\n</office:document-content>\n";
    const std::string stylesXml = std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-styles") +
        kNamespaceDecls + ">\n" + FontDecls() + "\n" + commonStyles + "\n<office:automatic-styles>" + pageLayout +
        "</office:automatic-styles>\n" + masterStyles + "\n</office:document-styles>\n";
    const std::string metaXml = std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<office:document-meta") +
        kNamespaceDecls + ">" + Meta() + "</office:document-meta>\n";
    std::string manifest = std::string("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<manifest:manifest xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" manifest:version=\"1.3\">\n"
        " <manifest:file-entry manifest:full-path=\"/\" manifest:version=\"1.3\" manifest:media-type=\"") + mime + "\"/>\n"
        " <manifest:file-entry manifest:full-path=\"content.xml\" manifest:media-type=\"text/xml\"/>\n"
        " <manifest:file-entry manifest:full-path=\"styles.xml\" manifest:media-type=\"text/xml\"/>\n"
        " <manifest:file-entry manifest:full-path=\"meta.xml\" manifest:media-type=\"text/xml\"/>\n";
    for (const auto& p : pictures_)
        manifest += " <manifest:file-entry manifest:full-path=\"" + p.Path + "\" manifest:media-type=\"" + p.Mime + "\"/>\n";
    // 256 pixels on the long side: a file manager's tile, at a few kilobytes.
    const std::vector<uint8_t> thumbnail = Thumbnail(pageW, pageH, 256);
    if (!thumbnail.empty())
        manifest += " <manifest:file-entry manifest:full-path=\"Thumbnails/thumbnail.png\" manifest:media-type=\"image/png\"/>\n";
    manifest += "</manifest:manifest>\n";

    UCZipPackageWriter zip;
    if (!zip.OpenInMemory()) return {};
    bool ok = zip.AddEntry("mimetype", std::string(mime), false) &&   // first and stored, as ODF requires
              zip.AddEntry("content.xml", contentXml) && zip.AddEntry("styles.xml", stylesXml) &&
              zip.AddEntry("meta.xml", metaXml);
    for (const auto& p : pictures_) ok = ok && zip.AddEntry(p.Path, p.Bytes.data(), p.Bytes.size(), false);
    if (!thumbnail.empty()) ok = ok && zip.AddEntry("Thumbnails/thumbnail.png", thumbnail.data(), thumbnail.size(), false);
    ok = ok && zip.AddEntry("META-INF/manifest.xml", manifest) && zip.Finalize();
    if (!ok) {
        if (options_.WarningCallback) options_.WarningCallback("ODG: could not build the package: " + zip.GetLastError());
        return {};
    }
    Report();
    const std::vector<uint8_t> bytes = zip.TakeBytes();
    return std::string(bytes.begin(), bytes.end());
}

}   // namespace

std::string WriteODGDocument(const VectorDocument& document, const ConversionOptions& options, bool asPackage,
                             bool templateType) {
    OdgWriter writer(document, options, asPackage);
    return writer.Run(templateType);
}

// ===== THE CONVERTER =====

FormatCapabilities ODGConverter::GetCapabilities() const {
    FormatCapabilities caps;
    caps.SupportsQuadraticBezier = true;
    caps.SupportsArc = true;
    caps.SupportsText = true;
    caps.SupportsRichText = true;
    caps.SupportsLinearGradient = true;
    caps.SupportsRadialGradient = true;
    caps.SupportsConicalGradient = false;
    caps.SupportsPattern = false;
    caps.SupportsDashing = true;
    caps.SupportsOpacity = true;
    caps.SupportsDropShadow = true;
    caps.SupportsClipping = false;
    caps.SupportsGroups = true;
    caps.SupportsLayers = true;
    caps.SupportsPages = false;
    return caps;
}

namespace {

bool IsFlatPath(const std::string& filename) {
    std::string ext = filename.size() >= 5 ? filename.substr(filename.size() - 5) : filename;
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".fodg";
}

bool IsTemplatePath(const std::string& filename) {
    std::string ext = filename.size() >= 4 ? filename.substr(filename.size() - 4) : filename;
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext == ".otg";
}

bool ReadWholeFile(const std::string& filename, std::string& out) {
    std::ifstream in(PathFromUtf8(filename), std::ios::binary);
    if (!in) return false;
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    return true;
}

}   // namespace

std::shared_ptr<VectorDocument> ODGConverter::Import(const std::string& filename, const ConversionOptions& options) {
    std::string data;
    if (!ReadWholeFile(filename, data)) {
        if (options.WarningCallback) options.WarningCallback("ODG: cannot open " + filename);
        return nullptr;
    }
    return ImportFromString(data, options);
}

std::shared_ptr<VectorDocument> ODGConverter::ImportFromString(const std::string& data, const ConversionOptions& options) {
    if (data.empty()) return nullptr;
    return ReadODGDocument(data, options);
}

std::shared_ptr<VectorDocument> ODGConverter::ImportFromStream(std::istream& stream, const ConversionOptions& options) {
    std::string data((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    return ImportFromString(data, options);
}

bool ODGConverter::Export(const VectorDocument& document, const std::string& filename, const ConversionOptions& options) {
    const std::string data = WriteODGDocument(document, options, !IsFlatPath(filename), IsTemplatePath(filename));
    if (data.empty()) return false;
    std::ofstream out(PathFromUtf8(filename), std::ios::binary | std::ios::trunc);
    if (!out) {
        if (options.WarningCallback) options.WarningCallback("ODG: cannot write " + filename);
        return false;
    }
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(out);
}

std::string ODGConverter::ExportToString(const VectorDocument& document, const ConversionOptions& options) {
    return WriteODGDocument(document, options, true, false);
}

bool ODGConverter::ExportToStream(const VectorDocument& document, std::ostream& stream, const ConversionOptions& options) {
    const std::string data = ExportToString(document, options);
    if (data.empty()) return false;
    stream.write(data.data(), static_cast<std::streamsize>(data.size()));
    return static_cast<bool>(stream);
}

std::string ODGConverter::ExportFlatXml(const VectorDocument& document, const ConversionOptions& options) {
    return WriteODGDocument(document, options, false, false);
}

bool ODGConverter::ValidateFile(const std::string& filename) const {
    std::ifstream in(PathFromUtf8(filename), std::ios::binary);
    if (!in) return false;
    std::string head(4096, '\0');
    in.read(head.data(), static_cast<std::streamsize>(head.size()));
    head.resize(static_cast<size_t>(in.gcount()));
    if (head.size() >= 4 && head[0] == 'P' && head[1] == 'K') {
        std::string all;
        return ReadWholeFile(filename, all) && ValidateData(all);
    }
    return ValidateData(head);
}

bool ODGConverter::ValidateData(const std::string& data) const {
    if (data.size() >= 4 && data[0] == 'P' && data[1] == 'K') {
        UCZipPackageReader zip;
        if (!zip.OpenFromMemory(std::vector<uint8_t>(data.begin(), data.end()))) return false;
        std::string mime;
        return zip.ReadEntry("mimetype", mime) && mime.find("application/vnd.oasis.opendocument.graphics") == 0;
    }
    // Flat XML: the root names the media type in its first few kilobytes.
    const std::string head = data.substr(0, 8192);
    return head.find("office:document") != std::string::npos &&
           head.find("application/vnd.oasis.opendocument.graphics") != std::string::npos;
}

} // namespace VectorConverter
} // namespace UltraCanvas
