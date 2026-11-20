// UltraCanvas/Plugins/Vector/UltraCanvasODGReader.cpp
// OpenDocument Drawing reader: a LibreOffice Draw package (.odg / .otg) or
// flat document (.fodg) into a VectorStorage::VectorDocument. See
// UltraCanvasODGConverter.h for what is read.
//
// Geometry is converted to points (the model's unit) as it is read. The
// conventions that are not obvious from the specification, and that
// LibreOffice - the writer of nearly every .odg there is - follows:
//  - draw:transform applies its operations left to right (each one to the
//    result of the one before), and rotate(a) turns the shape a radians
//    counter-clockwise on the page, which is Rotate(-a) in the model's
//    y-down space (LibreOffice's #i78696#); skewX likewise negated;
//  - a shape with a draw:transform carries its size and no position: the
//    translate places its top-left corner after the rotation;
//  - an enhanced path is drawn in its viewBox (21600 x 21600 for the stock
//    shapes) stretched to the frame - except past a draw:path-stretchpoint,
//    where coordinates move instead of stretch, which is what keeps a
//    rounded rectangle's corners round;
//  - gradients: the start colour is at the top of a linear one (draw:angle
//    turning it counter-clockwise), at the edges of an axial one and on the
//    outside of the radial family, the end colour opposite; draw:border is
//    the share of the axis held at the start colour.
// Version: 1.1.0 - ellipsoid gradients drawn elliptical and turned (a placed unit circle)
// Version: 1.0.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework

#include "UltraCanvasODGConverter.h"
#include "DataFormats/UltraCanvasVectorPathOps.h"
#include "UltraCanvasZipPackage.h"
#include "UltraCanvasTextUtils.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>

namespace UltraCanvas {
namespace VectorConverter {

using namespace VectorStorage;
using tinyxml2::XMLElement;
using tinyxml2::XMLNode;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kPointsPerCm = 72.0 / 2.54;
// LibreOffice's hairline (stroke width 0) is one device pixel; in the model
// that is drawn as a thin line of this width.
constexpr float kHairline = 0.5f;
// LibreOffice's built-in defaults for a shape whose style chain says nothing.
const Color kDefaultFill(0x72, 0x9f, 0xcf, 255);
const Color kDefaultStroke(0x34, 0x65, 0xa4, 255);

// ===== XML PARTS AND THEIR NAMESPACE PREFIXES =====

// The prefixes this reader is written against, and their namespaces. A
// document that binds a namespace to another prefix is read through the
// mapping; every writer seen in practice uses these.
const std::pair<const char*, const char*> kNamespaces[] = {
    {"office", "urn:oasis:names:tc:opendocument:xmlns:office:1.0"},
    {"style", "urn:oasis:names:tc:opendocument:xmlns:style:1.0"},
    {"text", "urn:oasis:names:tc:opendocument:xmlns:text:1.0"},
    {"draw", "urn:oasis:names:tc:opendocument:xmlns:drawing:1.0"},
    {"fo", "urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0"},
    {"xlink", "http://www.w3.org/1999/xlink"},
    {"svg", "urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0"},
    {"dr3d", "urn:oasis:names:tc:opendocument:xmlns:dr3d:1.0"},
    {"meta", "urn:oasis:names:tc:opendocument:xmlns:meta:1.0"},
    {"dc", "http://purl.org/dc/elements/1.1/"},
    {"loext", "urn:org:documentfoundation:names:experimental:office:xmlns:loext:1.0"},
    {"xml", "http://www.w3.org/XML/1998/namespace"},
};

class XmlPart {
public:
    tinyxml2::XMLDocument Doc;

    bool Load(const std::string& text) {
        if (Doc.Parse(text.data(), text.size()) != tinyxml2::XML_SUCCESS) return false;
        const XMLElement* root = Doc.RootElement();
        if (!root) return false;
        std::map<std::string, std::string> uriToPrefix;
        for (const tinyxml2::XMLAttribute* a = root->FirstAttribute(); a; a = a->Next()) {
            const char* n = a->Name();
            if (std::strncmp(n, "xmlns:", 6) == 0) uriToPrefix[a->Value()] = n + 6;
        }
        for (const auto& [prefix, uri] : kNamespaces) {
            auto it = uriToPrefix.find(uri);
            if (it == uriToPrefix.end() || it->second == prefix) continue;
            toActual_[prefix] = it->second;
            toCanonical_[it->second] = prefix;
        }
        return true;
    }

    // The name a canonical "draw:rect" has in this document.
    const char* Q(const char* canonical) const {
        if (toActual_.empty()) return canonical;
        auto hit = cache_.find(canonical);
        if (hit != cache_.end()) return hit->second.c_str();
        std::string name = canonical;
        const size_t colon = name.find(':');
        if (colon != std::string::npos) {
            auto it = toActual_.find(name.substr(0, colon));
            if (it != toActual_.end()) name = it->second + name.substr(colon);
        }
        return cache_.emplace(canonical, std::move(name)).first->second.c_str();
    }

    // The canonical form of a name read from this document.
    std::string Canonical(const char* actual) const {
        if (toCanonical_.empty()) return actual;
        std::string name = actual;
        const size_t colon = name.find(':');
        if (colon != std::string::npos) {
            auto it = toCanonical_.find(name.substr(0, colon));
            if (it != toCanonical_.end()) return it->second + name.substr(colon);
        }
        return name;
    }

    const char* Attr(const XMLElement* e, const char* canonical) const {
        return e ? e->Attribute(Q(canonical)) : nullptr;
    }
    std::string AttrS(const XMLElement* e, const char* canonical, const std::string& fallback = {}) const {
        const char* v = Attr(e, canonical);
        return v ? std::string(v) : fallback;
    }
    bool Is(const XMLElement* e, const char* canonical) const {
        return e && std::strcmp(e->Name(), Q(canonical)) == 0;
    }
    const XMLElement* Child(const XMLElement* e, const char* canonical) const {
        return e ? e->FirstChildElement(Q(canonical)) : nullptr;
    }

private:
    std::map<std::string, std::string> toActual_, toCanonical_;
    mutable std::unordered_map<std::string, std::string> cache_;
};

// ===== VALUES =====

std::string Trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// The number at the front of `s` and the unit after it.
bool SplitNumber(const std::string& s, double& value, std::string& unit) {
    const std::string t = Trim(s);
    if (t.empty()) return false;
    const char* first = t.data();
    const char* last = t.data() + t.size();
    const char* p = first;
    if (*p == '+') ++p;
    const char* end = ParseFloatClassic(p, last, value);
    if (end == p) return false;
    unit = Lower(Trim(std::string(end, last)));
    return true;
}

// A length in points. Unitless values are taken as points.
bool ParseLength(const std::string& s, double& out) {
    double v;
    std::string unit;
    if (!SplitNumber(s, v, unit)) return false;
    if (unit == "cm") v *= kPointsPerCm;
    else if (unit == "mm") v *= kPointsPerCm / 10.0;
    else if (unit == "in" || unit == "inch") v *= 72.0;
    else if (unit == "pc") v *= 12.0;
    else if (unit == "px") v *= 0.75;
    else if (unit == "m") v *= kPointsPerCm * 100.0;
    out = v;
    return true;
}

double LengthOr(const char* s, double fallback) {
    double v;
    return s && ParseLength(s, v) ? v : fallback;
}

// "50%" -> 0.5.
double PercentOr(const char* s, double fallback) {
    double v;
    std::string unit;
    if (!s || !SplitNumber(s, v, unit)) return fallback;
    return unit == "%" ? v / 100.0 : v;
}

// An angle in degrees. `unitlessScale` is what a bare number is worth in
// degrees: 1 for the ellipse angles, 0.1 for the gradient and hatch angles
// that ODF 1.2 counts in tenths.
double AngleDegrees(const char* s, double unitlessScale, double fallback = 0) {
    double v;
    std::string unit;
    if (!s || !SplitNumber(s, v, unit)) return fallback;
    if (unit == "deg") return v;
    if (unit == "rad") return v * 180.0 / kPi;
    if (unit == "grad") return v * 0.9;
    return v * unitlessScale;
}

bool ParseColor(const std::string& s, Color& out) {
    const std::string t = Trim(s);
    if (t.size() != 7 || t[0] != '#') return false;
    unsigned v = 0;
    for (size_t i = 1; i < 7; ++i) {
        const char c = static_cast<char>(std::tolower(static_cast<unsigned char>(t[i])));
        v <<= 4;
        if (c >= '0' && c <= '9') v |= static_cast<unsigned>(c - '0');
        else if (c >= 'a' && c <= 'f') v |= static_cast<unsigned>(c - 'a' + 10);
        else return false;
    }
    out = Color(static_cast<uint8_t>(v >> 16), static_cast<uint8_t>((v >> 8) & 0xff),
                static_cast<uint8_t>(v & 0xff), 255);
    return true;
}

Color ColorOr(const char* s, const Color& fallback) {
    Color c;
    return s && ParseColor(s, c) ? c : fallback;
}

Color WithAlpha(Color c, double opacity) {
    opacity = std::clamp(opacity, 0.0, 1.0);
    c.a = static_cast<uint8_t>(std::lround(c.a * opacity));
    return c;
}

Color Scaled(const Color& c, double intensity) {
    auto ch = [&](uint8_t v) { return static_cast<uint8_t>(std::clamp(std::lround(v * intensity), 0L, 255L)); };
    return Color(ch(c.r), ch(c.g), ch(c.b), c.a);
}

// Lighten (t > 0) or darken (t < 0) - the enhanced path's shading commands.
Color Shaded(const Color& c, double t) {
    auto ch = [&](uint8_t v) {
        const double d = t >= 0 ? v + (255 - v) * t : v * (1 + t);
        return static_cast<uint8_t>(std::clamp(std::lround(d), 0L, 255L));
    };
    return Color(ch(c.r), ch(c.g), ch(c.b), c.a);
}

std::vector<std::string> SplitList(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
            if (!cur.empty()) out.push_back(cur), cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

std::vector<double> NumberList(const std::string& s) {
    std::vector<double> out;
    for (const std::string& t : SplitList(s)) {
        double v;
        if (TryParseFloat(t, v)) out.push_back(v);
    }
    return out;
}

// An image's size in pixels from its header: PNG, JPEG, GIF and BMP.
bool ImagePixelSize(const std::vector<uint8_t>& b, int& w, int& h) {
    auto be32 = [&](size_t i) { return (b[i] << 24) | (b[i + 1] << 16) | (b[i + 2] << 8) | b[i + 3]; };
    if (b.size() >= 24 && b[0] == 0x89 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G') {
        w = be32(16);
        h = be32(20);
        return w > 0 && h > 0;
    }
    if (b.size() >= 10 && b[0] == 'G' && b[1] == 'I' && b[2] == 'F') {
        w = b[6] | (b[7] << 8);
        h = b[8] | (b[9] << 8);
        return w > 0 && h > 0;
    }
    if (b.size() >= 26 && b[0] == 'B' && b[1] == 'M') {
        w = b[18] | (b[19] << 8) | (b[20] << 16) | (b[21] << 24);
        h = std::abs(static_cast<int32_t>(b[22] | (b[23] << 8) | (b[24] << 16) | (b[25] << 24)));
        return w > 0 && h > 0;
    }
    if (b.size() >= 4 && b[0] == 0xFF && b[1] == 0xD8) {
        size_t i = 2;
        while (i + 9 < b.size()) {
            if (b[i] != 0xFF) return false;
            const uint8_t marker = b[i + 1];
            const size_t len = (b[i + 2] << 8) | b[i + 3];
            if ((marker >= 0xC0 && marker <= 0xC3) || (marker >= 0xC5 && marker <= 0xC7) ||
                (marker >= 0xC9 && marker <= 0xCB) || (marker >= 0xCD && marker <= 0xCF)) {
                h = (b[i + 5] << 8) | b[i + 6];
                w = (b[i + 7] << 8) | b[i + 8];
                return w > 0 && h > 0;
            }
            i += 2 + len;
        }
    }
    return false;
}

std::string MimeTypeForPath(const std::string& path) {
    const std::string p = Lower(path);
    auto ends = [&](const char* s) {
        const size_t n = std::strlen(s);
        return p.size() >= n && p.compare(p.size() - n, n, s) == 0;
    };
    if (ends(".png")) return "image/png";
    if (ends(".jpg") || ends(".jpeg")) return "image/jpeg";
    if (ends(".gif")) return "image/gif";
    if (ends(".bmp")) return "image/bmp";
    if (ends(".svg")) return "image/svg+xml";
    if (ends(".tif") || ends(".tiff")) return "image/tiff";
    if (ends(".webp")) return "image/webp";
    return "image/png";
}

// ===== TRANSFORMS =====

Matrix3x3 ParseTransform(const std::string& s) {
    Matrix3x3 m = Matrix3x3::Identity();
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && (std::isspace(static_cast<unsigned char>(s[i])) || s[i] == ',')) ++i;
        size_t nameStart = i;
        while (i < s.size() && std::isalpha(static_cast<unsigned char>(s[i]))) ++i;
        const std::string name = Lower(s.substr(nameStart, i - nameStart));
        const size_t open = s.find('(', i);
        const size_t close = open == std::string::npos ? open : s.find(')', open);
        if (name.empty() || open == std::string::npos || close == std::string::npos) break;
        const std::vector<std::string> args = SplitList(s.substr(open + 1, close - open - 1));
        i = close + 1;
        auto num = [&](size_t k, double fallback) {
            double v = fallback;
            if (k < args.size()) TryParseFloat(args[k], v);
            return v;
        };
        auto len = [&](size_t k) {
            double v = 0;
            if (k < args.size()) ParseLength(args[k], v);
            return v;
        };
        Matrix3x3 op = Matrix3x3::Identity();
        if (name == "rotate") {
            op = Matrix3x3::Rotate(-num(0, 0));
        } else if (name == "translate") {
            op = Matrix3x3::Translate(len(0), args.size() > 1 ? len(1) : 0);
        } else if (name == "scale") {
            const double sx = num(0, 1);
            op = Matrix3x3::Scale(sx, args.size() > 1 ? num(1, sx) : sx);
        } else if (name == "skewx") {
            op = Matrix3x3::FromValues(1, -std::tan(num(0, 0)), 0, 1, 0, 0);
        } else if (name == "skewy") {
            op = Matrix3x3::FromValues(1, 0, -std::tan(num(0, 0)), 1, 0, 0);
        } else if (name == "matrix" && args.size() >= 6) {
            op = Matrix3x3::FromValues(num(0, 1), num(2, 0), num(1, 0), num(3, 1), len(4), len(5));
        } else {
            continue;
        }
        m = op * m;   // left to right: each operation after the ones before
    }
    return m;
}

// ===== STYLES =====

using PropMap = std::map<std::string, std::string>;

struct StyleEntry {
    std::string Parent;
    PropMap Props;
};

struct NamedObject {
    const XMLElement* Element = nullptr;
    const XmlPart* Part = nullptr;
};

class StyleBook {
public:
    // family|name -> entry, for the common (office:styles) and the two sets
    // of automatic styles.
    std::map<std::string, StyleEntry> Common, AutoContent, AutoStyles;
    std::map<std::string, PropMap> Defaults;            // family -> properties
    std::map<std::string, std::string> FontFaces;       // style:name -> family
    std::map<std::string, NamedObject> Gradients, Hatches, Markers, Dashes, Opacities, FillImages;
    std::map<std::string, NamedObject> PageLayouts, MasterPages;

    static std::string Key(const std::string& family, const std::string& name) { return family + "|" + name; }

    void ReadFontFaces(const XmlPart& part, const XMLElement* decls) {
        for (const XMLElement* f = decls ? decls->FirstChildElement() : nullptr; f; f = f->NextSiblingElement()) {
            if (!part.Is(f, "style:font-face")) continue;
            std::string family = part.AttrS(f, "svg:font-family");
            if (family.size() >= 2 && (family.front() == '\'' || family.front() == '"')) family = family.substr(1, family.size() - 2);
            const std::string name = part.AttrS(f, "style:name");
            if (!name.empty()) FontFaces[name] = family.empty() ? name : family;
        }
    }

    // office:styles / office:automatic-styles (and the master styles' layouts).
    void ReadStyles(const XmlPart& part, const XMLElement* container, std::map<std::string, StyleEntry>& into) {
        for (const XMLElement* s = container ? container->FirstChildElement() : nullptr; s; s = s->NextSiblingElement()) {
            if (part.Is(s, "style:style")) {
                StyleEntry entry;
                entry.Parent = part.AttrS(s, "style:parent-style-name");
                CollectProps(part, s, entry.Props);
                into[Key(part.AttrS(s, "style:family"), part.AttrS(s, "style:name"))] = std::move(entry);
            } else if (part.Is(s, "style:default-style")) {
                CollectProps(part, s, Defaults[part.AttrS(s, "style:family")]);
            } else if (part.Is(s, "style:page-layout")) {
                PageLayouts[part.AttrS(s, "style:name")] = {s, &part};
            } else if (part.Is(s, "draw:gradient")) {
                Gradients[part.AttrS(s, "draw:name")] = {s, &part};
            } else if (part.Is(s, "draw:hatch")) {
                Hatches[part.AttrS(s, "draw:name")] = {s, &part};
            } else if (part.Is(s, "draw:marker")) {
                Markers[part.AttrS(s, "draw:name")] = {s, &part};
            } else if (part.Is(s, "draw:stroke-dash")) {
                Dashes[part.AttrS(s, "draw:name")] = {s, &part};
            } else if (part.Is(s, "draw:opacity")) {
                Opacities[part.AttrS(s, "draw:name")] = {s, &part};
            } else if (part.Is(s, "draw:fill-image")) {
                FillImages[part.AttrS(s, "draw:name")] = {s, &part};
            }
        }
    }

    // A style's properties with its parents' and the family default under
    // them. `automatic` is the set the name is looked up in first.
    PropMap Resolve(const std::string& family, const std::string& name,
                    const std::map<std::string, StyleEntry>* automatic) const {
        std::vector<const StyleEntry*> chain;
        std::set<std::string> seen;
        std::string current = name;
        bool first = true;
        while (!current.empty() && seen.insert(current).second && chain.size() < 32) {
            const StyleEntry* e = nullptr;
            if (first && automatic) {
                auto it = automatic->find(Key(family, current));
                if (it != automatic->end()) e = &it->second;
            }
            if (!e) {
                auto it = Common.find(Key(family, current));
                if (it != Common.end()) e = &it->second;
            }
            first = false;
            if (!e) break;
            chain.push_back(e);
            current = e->Parent;
        }
        PropMap out;
        auto d = Defaults.find(family);
        if (d != Defaults.end()) out = d->second;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it)
            for (const auto& [k, v] : (*it)->Props) out[k] = v;
        return out;
    }

private:
    static void CollectProps(const XmlPart& part, const XMLElement* style, PropMap& into) {
        for (const XMLElement* p = style->FirstChildElement(); p; p = p->NextSiblingElement()) {
            const std::string n = part.Canonical(p->Name());
            // loext:graphic-properties on a paragraph style repeats the
            // shape's fill for the text area; it is not the text's.
            if (n.size() < 11 || n.compare(n.size() - 11, 11, "-properties") != 0 || n == "loext:graphic-properties") continue;
            for (const tinyxml2::XMLAttribute* a = p->FirstAttribute(); a; a = a->Next())
                into[part.Canonical(a->Name())] = a->Value();
        }
    }
};

const char* Prop(const PropMap& m, const char* key) {
    auto it = m.find(key);
    return it == m.end() ? nullptr : it->second.c_str();
}

std::string PropS(const PropMap& m, const char* key, const std::string& fallback = {}) {
    auto it = m.find(key);
    return it == m.end() ? fallback : it->second;
}

// ===== ENHANCED GEOMETRY (draw:custom-shape) =====

struct ShapePart {
    PathData Path;
    bool NoFill = false;
    bool NoStroke = false;
    double Shade = 0;   // lighten > 0, darken < 0
};

class EnhancedGeometry {
public:
    Rect2Dd ViewBox{0, 0, 21600, 21600};
    Rect2Dd Frame;   // the shape's rectangle, points
    std::vector<double> Modifiers;
    std::map<std::string, std::string> Equations;
    bool MirrorH = false, MirrorV = false;
    std::optional<double> StretchX, StretchY;
    bool HasStroke = true, HasFill = true;

    // ---- formulas ----
    double Formula(const std::string& text) {
        Parser p{this, text, 0};
        const double v = p.Expr();
        return std::isfinite(v) ? v : 0.0;
    }

    double Equation(const std::string& name) {
        auto cached = cache_.find(name);
        if (cached != cache_.end()) return cached->second;
        auto it = Equations.find(name);
        if (it == Equations.end() || !evaluating_.insert(name).second || evaluating_.size() > 256) return 0;
        const double v = Formula(it->second);
        evaluating_.erase(name);
        cache_[name] = v;
        return v;
    }

    double Constant(const std::string& n) const {
        if (n == "pi") return kPi;
        if (n == "left") return ViewBox.x;
        if (n == "top") return ViewBox.y;
        if (n == "right") return ViewBox.x + ViewBox.width;
        if (n == "bottom") return ViewBox.y + ViewBox.height;
        if (n == "width") return ViewBox.width;
        if (n == "height") return ViewBox.height;
        if (n == "xstretch") return StretchX.value_or(0);
        if (n == "ystretch") return StretchY.value_or(0);
        if (n == "hasstroke") return HasStroke ? 1 : 0;
        if (n == "hasfill") return HasFill ? 1 : 0;
        // The frame in 1/100 mm, LibreOffice's logical unit.
        if (n == "logwidth") return Frame.width * 2540.0 / 72.0;
        if (n == "logheight") return Frame.height * 2540.0 / 72.0;
        return 0;
    }

    // ---- the frame ----
    Point2Dd Map(double x, double y) const {
        const double sx = ViewBox.width != 0 ? Frame.width / ViewBox.width : 0;
        const double sy = ViewBox.height != 0 ? Frame.height / ViewBox.height : 0;
        const bool stretch = StretchX.has_value() || StretchY.has_value();
        const double s = std::min(sx, sy);
        double X, Y;
        if (stretch && StretchX) X = (x - ViewBox.x) * s + (x > *StretchX ? Frame.width - ViewBox.width * s : 0);
        else X = (x - ViewBox.x) * sx;
        if (stretch && StretchY) Y = (y - ViewBox.y) * s + (y > *StretchY ? Frame.height - ViewBox.height * s : 0);
        else Y = (y - ViewBox.y) * sy;
        if (MirrorH) X = Frame.width - X;
        if (MirrorV) Y = Frame.height - Y;
        return {Frame.x + X, Frame.y + Y};
    }

    std::vector<ShapePart> Build(const std::string& enhancedPath) {
        Tokens tokens = Tokenize(enhancedPath);
        std::vector<ShapePart> parts(1);
        Point2Dd cur{0, 0}, subStart{0, 0};
        bool open = false;
        char command = 0;
        int quadrantToggle = 0;
        size_t i = 0;
        auto P = [&](double x, double y) { return Map(x, y); };
        auto add = [&](PathCommandType t, std::initializer_list<double> v) {
            PathCommand c;
            c.Type = t;
            for (double d : v) c.Parameters.push_back(static_cast<float>(d));
            parts.back().Path.commands.push_back(std::move(c));
        };
        auto moveTo = [&](double x, double y) {
            const Point2Dd p = P(x, y);
            add(PathCommandType::MoveTo, {p.x, p.y});
            cur = subStart = {x, y};
            open = true;
        };
        auto lineTo = [&](double x, double y) {
            if (!open) { moveTo(x, y); return; }
            const Point2Dd p = P(x, y);
            add(PathCommandType::LineTo, {p.x, p.y});
            cur = {x, y};
        };
        // An elliptical arc in viewBox space, y-down parameter t: start t0,
        // sweep dt (positive = clockwise on the page). `connect` draws a line
        // from the current point to the arc's start, otherwise a new subpath
        // starts there.
        auto arc = [&](double cx, double cy, double rx, double ry, double t0, double dt, bool connect) {
            const Point2Dd start{cx + rx * std::cos(t0), cy + ry * std::sin(t0)};
            if (connect && open) lineTo(start.x, start.y);
            else moveTo(start.x, start.y);
            const int n = std::max(1, static_cast<int>(std::ceil(std::fabs(dt) / (kPi / 2) - 1e-9)));
            const double step = dt / n;
            const double k = 4.0 / 3.0 * std::tan(step / 4);
            double t = t0;
            for (int s = 0; s < n; ++s) {
                const double t1 = t + step;
                const double c0 = std::cos(t), s0 = std::sin(t), c1 = std::cos(t1), s1 = std::sin(t1);
                const Point2Dd a{cx + rx * (c0 - k * s0), cy + ry * (s0 + k * c0)};
                const Point2Dd b{cx + rx * (c1 + k * s1), cy + ry * (s1 - k * c1)};
                const Point2Dd e{cx + rx * c1, cy + ry * s1};
                const Point2Dd pa = P(a.x, a.y), pb = P(b.x, b.y), pe = P(e.x, e.y);
                add(PathCommandType::CurveTo, {pa.x, pa.y, pb.x, pb.y, pe.x, pe.y});
                cur = e;
                t = t1;
            }
        };
        // The y-down parameter of the ray from the centre through (x, y).
        auto paramOf = [](double cx, double cy, double rx, double ry, double x, double y) {
            return std::atan2(ry != 0 ? (y - cy) / ry : 0, rx != 0 ? (x - cx) / rx : 0);
        };
        auto boxArc = [&](const std::vector<double>& v, bool clockwise, bool connect) {
            const double cx = (v[0] + v[2]) / 2, cy = (v[1] + v[3]) / 2;
            const double rx = std::fabs(v[2] - v[0]) / 2, ry = std::fabs(v[3] - v[1]) / 2;
            const double t0 = paramOf(cx, cy, rx, ry, v[4], v[5]);
            const double t1 = paramOf(cx, cy, rx, ry, v[6], v[7]);
            double dt = t1 - t0;
            if (clockwise) { while (dt <= 1e-9) dt += 2 * kPi; }
            else { while (dt >= -1e-9) dt -= 2 * kPi; }
            arc(cx, cy, rx, ry, t0, dt, connect);
        };
        auto angleEllipse = [&](const std::vector<double>& v, bool connect) {
            // Centre, radii, then start and end angle in degrees counted
            // counter-clockwise on the page.
            double sweep = v[5] - v[4];
            while (sweep <= 1e-9) sweep += 360.0;
            while (sweep > 360.0 + 1e-9) sweep -= 360.0;
            arc(v[0], v[1], v[2], v[3], -v[4] * kPi / 180.0, -sweep * kPi / 180.0, connect);
        };
        auto endPart = [&]() {
            if (!parts.back().Path.commands.empty() || parts.back().NoFill || parts.back().NoStroke || parts.back().Shade != 0)
                parts.emplace_back();
            open = false;
            quadrantToggle = 0;
        };

        static const std::map<char, int> kArgs = {
            {'M', 2}, {'L', 2}, {'C', 6}, {'Z', 0}, {'N', 0}, {'F', 0}, {'S', 0},
            {'T', 6}, {'U', 6}, {'A', 8}, {'B', 8}, {'W', 8}, {'V', 8},
            {'X', 2}, {'Y', 2}, {'Q', 4}, {'G', 4}, {'H', 0}, {'I', 0}, {'J', 0}, {'K', 0}};

        while (i < tokens.size()) {
            if (tokens[i].IsCommand) {
                command = tokens[i].Command;
                ++i;
                if (command == 'X' || command == 'Y') quadrantToggle = command == 'X' ? 0 : 1;
                switch (command) {
                    case 'Z':
                        if (open) {
                            add(PathCommandType::ClosePath, {});
                            cur = subStart;
                            open = false;
                        }
                        continue;
                    case 'N': endPart(); continue;
                    case 'F': parts.back().NoFill = true; continue;
                    case 'S': parts.back().NoStroke = true; continue;
                    case 'H': parts.back().Shade = -0.4; continue;
                    case 'I': parts.back().Shade = -0.2; continue;
                    case 'J': parts.back().Shade = 0.4; continue;
                    case 'K': parts.back().Shade = 0.2; continue;
                    default: break;
                }
            }
            auto argc = kArgs.find(command);
            if (command == 0 || argc == kArgs.end() || argc->second == 0) { ++i; continue; }
            if (i + static_cast<size_t>(argc->second) > tokens.size()) break;
            std::vector<double> v;
            bool interrupted = false;
            for (int k = 0; k < argc->second; ++k) {
                if (tokens[i + static_cast<size_t>(k)].IsCommand) { interrupted = true; break; }
                v.push_back(Value(tokens[i + static_cast<size_t>(k)].Text));
            }
            if (interrupted) { ++i; continue; }
            i += static_cast<size_t>(argc->second);
            switch (command) {
                case 'M': moveTo(v[0], v[1]); command = 'L'; break;
                case 'L': lineTo(v[0], v[1]); break;
                case 'C': {
                    if (!open) moveTo(cur.x, cur.y);
                    const Point2Dd a = P(v[0], v[1]), b = P(v[2], v[3]), e = P(v[4], v[5]);
                    add(PathCommandType::CurveTo, {a.x, a.y, b.x, b.y, e.x, e.y});
                    cur = {v[4], v[5]};
                    break;
                }
                case 'Q': {
                    if (!open) moveTo(cur.x, cur.y);
                    const Point2Dd a = P(v[0], v[1]), e = P(v[2], v[3]);
                    add(PathCommandType::QuadraticTo, {a.x, a.y, e.x, e.y});
                    cur = {v[2], v[3]};
                    break;
                }
                case 'T': angleEllipse(v, true); break;
                case 'U': angleEllipse(v, false); break;
                case 'A': boxArc(v, false, true); break;
                case 'B': boxArc(v, false, false); break;
                case 'W': boxArc(v, true, true); break;
                case 'V': boxArc(v, true, false); break;
                case 'X':
                case 'Y': {
                    // A quarter ellipse to (x, y), leaving the current point
                    // horizontally (X) or vertically (Y); the two alternate.
                    if (!open) { moveTo(v[0], v[1]); break; }
                    const Point2Dd p0 = cur, p1{v[0], v[1]};
                    const double k = 0.5522847498;
                    Point2Dd c1, c2;
                    if (quadrantToggle == 0) {
                        c1 = {p0.x + k * (p1.x - p0.x), p0.y};
                        c2 = {p1.x, p1.y - k * (p1.y - p0.y)};
                    } else {
                        c1 = {p0.x, p0.y + k * (p1.y - p0.y)};
                        c2 = {p1.x - k * (p1.x - p0.x), p1.y};
                    }
                    const Point2Dd a = P(c1.x, c1.y), b = P(c2.x, c2.y), e = P(p1.x, p1.y);
                    add(PathCommandType::CurveTo, {a.x, a.y, b.x, b.y, e.x, e.y});
                    cur = p1;
                    quadrantToggle ^= 1;
                    break;
                }
                case 'G': {
                    // OOXML arcTo: radii, start angle and sweep in degrees,
                    // clockwise on the page, from the current point.
                    if (!open) moveTo(cur.x, cur.y);
                    const double rx = v[0], ry = v[1];
                    const double st = v[2] * kPi / 180.0, sw = v[3] * kPi / 180.0;
                    const double cx = cur.x - rx * std::cos(st), cy = cur.y - ry * std::sin(st);
                    if (std::fabs(sw) > 1e-12) arc(cx, cy, rx, ry, st, sw, true);
                    break;
                }
                default: break;
            }
        }
        if (parts.back().Path.commands.empty()) parts.pop_back();
        return parts;
    }

private:
    struct Token {
        bool IsCommand = false;
        char Command = 0;
        std::string Text;
    };
    using Tokens = std::vector<Token>;

    static bool IsCommandLetter(char c) { return std::strchr("MLCZNFSTUABWVXYQGHIJK", c) != nullptr; }

    static Tokens Tokenize(const std::string& s) {
        Tokens out;
        size_t i = 0;
        while (i < s.size()) {
            const char c = s[i];
            if (std::isspace(static_cast<unsigned char>(c)) || c == ',') { ++i; continue; }
            if (c == '?' || c == '$') {
                size_t j = i + 1;
                while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) ++j;
                out.push_back({false, 0, s.substr(i, j - i)});
                i = j;
            } else if (std::isalpha(static_cast<unsigned char>(c))) {
                size_t j = i;
                while (j < s.size() && std::isalpha(static_cast<unsigned char>(s[j]))) ++j;
                const std::string word = s.substr(i, j - i);
                if (word.size() == 1 && IsCommandLetter(word[0])) out.push_back({true, word[0], word});
                else out.push_back({false, 0, word});
                i = j;
            } else {
                size_t j = i + 1;
                while (j < s.size() && (std::isdigit(static_cast<unsigned char>(s[j])) || s[j] == '.' ||
                                        ((s[j] == 'e' || s[j] == 'E') && j + 1 < s.size() &&
                                         (std::isdigit(static_cast<unsigned char>(s[j + 1])) || s[j + 1] == '-'))))
                    j += (s[j] == 'e' || s[j] == 'E') ? 2 : 1;
                out.push_back({false, 0, s.substr(i, j - i)});
                i = j;
            }
        }
        return out;
    }

    double Value(const std::string& t) {
        if (t.empty()) return 0;
        if (t[0] == '?') return Equation(t.substr(1));
        if (t[0] == '$') {
            double idx = 0;
            TryParseFloat(t.substr(1), idx);
            const size_t k = static_cast<size_t>(std::max(0.0, idx));
            return k < Modifiers.size() ? Modifiers[k] : 0;
        }
        double v;
        if (TryParseFloat(t, v)) return v;
        return Constant(t);
    }

    struct Parser {
        EnhancedGeometry* g;
        const std::string& s;
        size_t i;

        void Skip() { while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i; }
        bool Eat(char c) {
            Skip();
            if (i < s.size() && s[i] == c) { ++i; return true; }
            return false;
        }
        double Expr() {
            double v = Term();
            for (;;) {
                if (Eat('+')) v += Term();
                else if (Eat('-')) v -= Term();
                else return v;
            }
        }
        double Term() {
            double v = Unary();
            for (;;) {
                if (Eat('*')) v *= Unary();
                else if (Eat('/')) {
                    const double d = Unary();
                    v = d != 0 ? v / d : 0;
                } else return v;
            }
        }
        double Unary() {
            if (Eat('-')) return -Unary();
            if (Eat('+')) return Unary();
            return Primary();
        }
        double Primary() {
            Skip();
            if (i >= s.size()) return 0;
            if (Eat('(')) {
                const double v = Expr();
                Eat(')');
                return v;
            }
            const char c = s[i];
            if (c == '?' || c == '$') {
                size_t j = i + 1;
                while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) ++j;
                const std::string tok = s.substr(i, j - i);
                i = j;
                return g->Value(tok);
            }
            if (std::isalpha(static_cast<unsigned char>(c))) {
                size_t j = i;
                while (j < s.size() && (std::isalnum(static_cast<unsigned char>(s[j])) || s[j] == '_')) ++j;
                const std::string name = Lower(s.substr(i, j - i));
                i = j;
                if (Eat('(')) {
                    std::vector<double> a;
                    Skip();
                    if (!Eat(')')) {
                        do { a.push_back(Expr()); } while (Eat(','));
                        Eat(')');
                    }
                    auto arg = [&](size_t k) { return k < a.size() ? a[k] : 0.0; };
                    if (name == "abs") return std::fabs(arg(0));
                    if (name == "sqrt") return std::sqrt(std::max(0.0, arg(0)));
                    if (name == "sin") return std::sin(arg(0));
                    if (name == "cos") return std::cos(arg(0));
                    if (name == "tan") return std::tan(arg(0));
                    if (name == "atan") return std::atan(arg(0));
                    if (name == "atan2") return std::atan2(arg(0), arg(1));
                    if (name == "min") return std::min(arg(0), arg(1));
                    if (name == "max") return std::max(arg(0), arg(1));
                    if (name == "if") return arg(0) > 0 ? arg(1) : arg(2);
                    return 0;
                }
                return g->Constant(name);
            }
            double v = 0;
            const char* end = ParseFloatClassic(s.data() + i, s.data() + s.size(), v);
            if (end == s.data() + i) { ++i; return 0; }
            i = static_cast<size_t>(end - s.data());
            return v;
        }
    };

    std::map<std::string, double> cache_;
    std::set<std::string> evaluating_;
};

// ===== TEXT =====

struct TextRun {
    std::string Text;
    VectorTextStyle Style;
    Color Colour{0, 0, 0, 255};
};

struct TextLine {
    std::vector<TextRun> Runs;
    std::string Align;   // start / center / end / justify (per paragraph)
    float MaxSize = 0;
};

// ===== THE READER =====

class OdgReader {
public:
    OdgReader(const ConversionOptions& options) : options_(options) {}

    std::shared_ptr<VectorDocument> Read(const std::string& data);

private:
    const ConversionOptions& options_;
    UCZipPackageReader zip_;
    bool isPackage_ = false;
    XmlPart content_, styles_, meta_;
    bool haveStyles_ = false, haveMeta_ = false;
    StyleBook book_;
    std::map<std::string, std::pair<bool, bool>> layerState_;   // name -> (visible, locked)
    std::map<std::string, int> skipped_;
    int imageCount_ = 0;

    // The style set the shapes being read look their automatic styles up in.
    const std::map<std::string, StyleEntry>* autoStyles_ = nullptr;
    const XmlPart* part_ = nullptr;

    void Warn(const std::string& msg) const {
        if (options_.WarningCallback) options_.WarningCallback("ODG: " + msg);
    }
    void Skip(const std::string& what) { ++skipped_[what]; }

    // ----- package -----
    bool ReadEntry(const std::string& name, std::string& out) {
        return isPackage_ && zip_.ReadEntry(name, out);
    }
    bool ReadBytes(const std::string& href, std::vector<uint8_t>& out) {
        std::string h = href;
        if (h.compare(0, 2, "./") == 0) h.erase(0, 2);
        return isPackage_ && !h.empty() && h[0] != '/' && h.find("://") == std::string::npos && zip_.ReadEntry(h, out);
    }

    // The bytes of a draw:image / draw:fill-image: an entry of the package
    // or office:binary-data inline.
    bool ImageBytes(const XmlPart& part, const XMLElement* e, std::vector<uint8_t>& bytes, std::string& mime,
                    std::string& externalHref) {
        mime = part.AttrS(e, "draw:mime-type", part.AttrS(e, "loext:mime-type"));
        if (const XMLElement* bin = part.Child(e, "office:binary-data")) {
            bytes = Base64Decode(bin->GetText() ? bin->GetText() : "");
            if (mime.empty()) mime = "image/png";
            return !bytes.empty();
        }
        const std::string href = part.AttrS(e, "xlink:href");
        if (href.empty()) return false;
        if (ReadBytes(href, bytes)) {
            if (mime.empty()) mime = MimeTypeForPath(href);
            return true;
        }
        externalHref = href;
        return false;
    }

    // ----- styles -----
    PropMap GraphicProps(const XMLElement* e) const {
        return book_.Resolve("graphic", part_->AttrS(e, "draw:style-name"), autoStyles_);
    }

    VectorTextStyle TextStyleFrom(const PropMap& p, const VectorTextStyle& base, Color& colour) const {
        VectorTextStyle st = base;
        if (const char* fn = Prop(p, "style:font-name")) {
            auto it = book_.FontFaces.find(fn);
            st.FontFamily = it != book_.FontFaces.end() ? it->second : fn;
        }
        if (const char* ff = Prop(p, "fo:font-family")) {
            std::string f = Trim(ff);
            if (f.size() >= 2 && (f.front() == '\'' || f.front() == '"')) f = f.substr(1, f.size() - 2);
            if (!f.empty()) st.FontFamily = f;
        }
        if (const char* fs = Prop(p, "fo:font-size")) {
            double v;
            std::string unit;
            if (SplitNumber(fs, v, unit)) {
                if (unit == "%") st.FontSize = static_cast<float>(st.FontSize * v / 100.0);
                else if (ParseLength(fs, v) && v > 0) st.FontSize = static_cast<float>(v);
            }
        }
        if (const char* w = Prop(p, "fo:font-weight")) {
            const std::string s = Lower(w);
            double n = 400;
            if (s == "bold") n = 700;
            else if (s == "normal") n = 400;
            else TryParseFloat(s, n);
            st.Weight = n >= 800 ? FontWeight::ExtraBold : n >= 600 ? FontWeight::Bold
                      : n <= 300 ? FontWeight::Light : FontWeight::Normal;
        }
        if (const char* s = Prop(p, "fo:font-style")) {
            const std::string v = Lower(s);
            st.Slant = v == "italic" ? FontSlant::Italic : v == "oblique" ? FontSlant::Oblique : FontSlant::Normal;
        }
        if (const char* u = Prop(p, "style:text-underline-style")) st.Underline = Lower(u) != "none";
        if (const char* l = Prop(p, "style:text-line-through-style")) st.StrikeThrough = Lower(l) != "none";
        if (const char* ls = Prop(p, "fo:letter-spacing")) {
            double v = 0;
            st.LetterSpacing = Lower(ls) == "normal" ? 0.0f : (ParseLength(ls, v) ? static_cast<float>(v) : st.LetterSpacing);
        }
        if (const char* c = Prop(p, "fo:color")) colour = ColorOr(c, colour);
        else if (const char* auto_ = Prop(p, "style:use-window-font-color"); auto_ && std::strcmp(auto_, "true") == 0)
            colour = autoTextColour_;
        return st;
    }

    void CollectParagraphText(const XMLElement* e, const VectorTextStyle& style, const Color& colour,
                              std::vector<TextLine>& lines, bool& lastSpace) {
        for (const XMLNode* n = e->FirstChild(); n; n = n->NextSibling()) {
            if (const tinyxml2::XMLText* t = n->ToText()) {
                std::string out;
                for (const char* c = t->Value(); *c; ++c) {
                    const bool ws = *c == ' ' || *c == '\t' || *c == '\n' || *c == '\r';
                    if (ws) {
                        if (!lastSpace) out += ' ';
                        lastSpace = true;
                    } else {
                        out += *c;
                        lastSpace = false;
                    }
                }
                AppendRun(lines, out, style, colour);
                continue;
            }
            const XMLElement* c = n->ToElement();
            if (!c) continue;
            if (part_->Is(c, "text:span")) {
                Color col = colour;
                const PropMap sp = book_.Resolve("text", part_->AttrS(c, "text:style-name"), autoStyles_);
                const VectorTextStyle st = TextStyleFrom(sp, style, col);
                CollectParagraphText(c, st, col, lines, lastSpace);
            } else if (part_->Is(c, "text:s")) {
                int count = 1;
                c->QueryIntAttribute(part_->Q("text:c"), &count);
                AppendRun(lines, std::string(static_cast<size_t>(std::clamp(count, 1, 1000)), ' '), style, colour);
                lastSpace = true;
            } else if (part_->Is(c, "text:tab")) {
                AppendRun(lines, "    ", style, colour);
                lastSpace = true;
            } else if (part_->Is(c, "text:line-break")) {
                TextLine next;
                next.Align = lines.back().Align;
                lines.push_back(std::move(next));
                lastSpace = true;
            } else if (part_->Is(c, "text:bookmark") || part_->Is(c, "text:bookmark-start") ||
                       part_->Is(c, "text:bookmark-end") || part_->Is(c, "text:soft-page-break") ||
                       part_->Is(c, "office:annotation") || part_->Is(c, "text:note")) {
                continue;
            } else {
                // Links, fields and the rest show their text.
                CollectParagraphText(c, style, colour, lines, lastSpace);
            }
        }
    }

    static void AppendRun(std::vector<TextLine>& lines, const std::string& text, const VectorTextStyle& st,
                          const Color& colour) {
        if (text.empty()) return;
        TextLine& line = lines.back();
        if (!line.Runs.empty()) {
            TextRun& last = line.Runs.back();
            if (last.Colour == colour && last.Style.FontFamily == st.FontFamily && last.Style.FontSize == st.FontSize &&
                last.Style.Weight == st.Weight && last.Style.Slant == st.Slant && last.Style.Underline == st.Underline &&
                last.Style.StrikeThrough == st.StrikeThrough && last.Style.LetterSpacing == st.LetterSpacing) {
                last.Text += text;
                return;
            }
        }
        line.Runs.push_back({text, st, colour});
    }

    void CollectParagraphs(const XMLElement* container, const PropMap& shapeProps, const std::string& defaultParaStyle,
                           std::vector<TextLine>& lines) {
        for (const XMLElement* c = container->FirstChildElement(); c; c = c->NextSiblingElement()) {
            if (part_->Is(c, "text:list") || part_->Is(c, "text:list-item") || part_->Is(c, "text:list-header") ||
                part_->Is(c, "text:section")) {
                CollectParagraphs(c, shapeProps, defaultParaStyle, lines);
                continue;
            }
            if (!part_->Is(c, "text:p") && !part_->Is(c, "text:h")) continue;
            // The shape's text properties, then the paragraph style's.
            Color colour = autoTextColour_;
            VectorTextStyle base;
            base.FontFamily = "Liberation Sans";
            base.FontSize = 18.0f;
            base = TextStyleFrom(shapeProps, base, colour);
            const std::string paraName = part_->AttrS(c, "text:style-name", defaultParaStyle);
            const PropMap pp = book_.Resolve("paragraph", paraName, autoStyles_);
            base = TextStyleFrom(pp, base, colour);
            std::string align = Lower(PropS(pp, "fo:text-align", PropS(shapeProps, "fo:text-align")));
            TextLine line;
            line.Align = align;
            lines.push_back(line);
            bool lastSpace = true;
            CollectParagraphText(c, base, colour, lines, lastSpace);
            // An empty paragraph still takes a line at its font's height.
            if (lines.back().Runs.empty()) lines.back().MaxSize = base.FontSize;
        }
    }

    // Lines too wide for the text area are broken at spaces, on an estimate
    // of the glyph advances (half an em, a little more in bold): the model
    // has no font metrics at read time.
    static std::vector<TextLine> Wrap(const std::vector<TextLine>& lines, double width) {
        if (width <= 0) return lines;
        auto advance = [](const TextRun& r, const std::string& s) {
            const double em = r.Style.Weight == FontWeight::Bold || r.Style.Weight == FontWeight::ExtraBold ? 0.56 : 0.52;
            size_t chars = 0;
            for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++chars;
            return chars * r.Style.FontSize * em + chars * r.Style.LetterSpacing;
        };
        std::vector<TextLine> out;
        for (const TextLine& line : lines) {
            TextLine cur;
            cur.Align = line.Align;
            cur.MaxSize = line.MaxSize;
            double used = 0;
            for (const TextRun& run : line.Runs) {
                size_t pos = 0;
                while (pos < run.Text.size()) {
                    size_t space = run.Text.find(' ', pos);
                    const size_t end = space == std::string::npos ? run.Text.size() : space + 1;
                    const std::string word = run.Text.substr(pos, end - pos);
                    const double w = advance(run, word);
                    std::string trimmedWord = word;
                    while (!trimmedWord.empty() && trimmedWord.back() == ' ') trimmedWord.pop_back();
                    if (used > 0 && used + advance(run, trimmedWord) > width) {
                        // Trailing spaces stay on the line they end.
                        if (!cur.Runs.empty()) {
                            std::string& t = cur.Runs.back().Text;
                            while (!t.empty() && t.back() == ' ') t.pop_back();
                        }
                        out.push_back(std::move(cur));
                        cur = TextLine();
                        cur.Align = line.Align;
                        used = 0;
                    }
                    if (!cur.Runs.empty() && cur.Runs.back().Colour == run.Colour &&
                        cur.Runs.back().Style.FontSize == run.Style.FontSize &&
                        cur.Runs.back().Style.FontFamily == run.Style.FontFamily &&
                        cur.Runs.back().Style.Weight == run.Style.Weight &&
                        cur.Runs.back().Style.Slant == run.Style.Slant &&
                        cur.Runs.back().Style.Underline == run.Style.Underline &&
                        cur.Runs.back().Style.StrikeThrough == run.Style.StrikeThrough)
                        cur.Runs.back().Text += word;
                    else
                        cur.Runs.push_back({word, run.Style, run.Colour});
                    used += w;
                    pos = end;
                }
            }
            out.push_back(std::move(cur));
        }
        return out;
    }

    // The text of a shape laid out in `area` (the frame, in the shape's own
    // coordinates). `isTextBox`: a text frame, whose text area defaults to
    // the top left; a shape's text is centred on it unless its style says.
    std::vector<std::shared_ptr<VectorElement>> BuildText(const XMLElement* container, const PropMap& props,
                                                          const Rect2Dd& area, bool isTextBox,
                                                          const std::string& defaultParaStyle) {
        std::vector<std::shared_ptr<VectorElement>> out;
        std::vector<TextLine> lines;
        autoTextColour_ = AutomaticTextColour(props);
        CollectParagraphs(container, props, defaultParaStyle, lines);
        bool anyText = false;
        for (const TextLine& l : lines)
            for (const TextRun& r : l.Runs)
                if (r.Text.find_first_not_of(' ') != std::string::npos) anyText = true;
        if (!anyText) return out;

        const double pad = LengthOr(Prop(props, "fo:padding"), 0.125 * kPointsPerCm);
        const double padL = LengthOr(Prop(props, "fo:padding-left"), pad);
        const double padR = LengthOr(Prop(props, "fo:padding-right"), pad);
        const double padT = LengthOr(Prop(props, "fo:padding-top"), pad);
        const double padB = LengthOr(Prop(props, "fo:padding-bottom"), pad);
        const Rect2Dd inner(area.x + padL, area.y + padT, std::max(0.0, area.width - padL - padR),
                            std::max(0.0, area.height - padT - padB));

        const bool growWidth = PropS(props, "draw:auto-grow-width") == "true";
        const bool noWrap = PropS(props, "fo:wrap-option") == "no-wrap";
        if (!growWidth && !noWrap) lines = Wrap(lines, inner.width);

        const double kAscent = 0.905, kLineHeight = 1.15;
        double total = 0;
        for (TextLine& l : lines) {
            for (const TextRun& r : l.Runs) l.MaxSize = std::max(l.MaxSize, r.Style.FontSize);
            if (l.MaxSize <= 0) l.MaxSize = 18.0f;
            total += l.MaxSize * kLineHeight;
        }
        const std::string vAlign = PropS(props, "draw:textarea-vertical-align", "top");
        double y = inner.y;
        if (vAlign == "middle") y = inner.y + (inner.height - total) / 2;
        else if (vAlign == "bottom") y = inner.y + inner.height - total;
        const std::string hArea = PropS(props, "draw:textarea-horizontal-align", isTextBox ? "left" : "justify");

        for (const TextLine& l : lines) {
            const double baseline = y + l.MaxSize * kAscent;
            y += l.MaxSize * kLineHeight;
            if (l.Runs.empty()) continue;
            std::string align = l.Align;
            if (align.empty() || align == "justify") {
                if (hArea == "center") align = "center";
                else if (hArea == "right") align = "end";
                else align = "start";
            }
            auto text = std::make_shared<VectorText>();
            TextAnchor anchor = TextAnchor::Start;
            double x = inner.x;
            if (align == "center") { anchor = TextAnchor::Middle; x = inner.x + inner.width / 2; }
            else if (align == "end" || align == "right") { anchor = TextAnchor::End; x = inner.x + inner.width; }
            text->Position = Point2Dd(x, baseline);
            text->BaseStyle = l.Runs.front().Style;
            text->BaseStyle.Anchor = anchor;
            text->Style.Fill = l.Runs.front().Colour;
            for (const TextRun& r : l.Runs) {
                TextSpanData span;
                span.Text = r.Text;
                span.Style = r.Style;
                span.Style.Anchor = anchor;
                span.Style.FillColor = r.Colour;
                text->Spans.push_back(std::move(span));
            }
            out.push_back(text);
        }
        return out;
    }

    // "Automatic" font colour, as LibreOffice picks it: white on a dark
    // fill, black otherwise. Its test is the luminance (76 R + 151 G + 29 B)
    // / 256 at most 156 - except for its own default shape blue, which it
    // keeps black text on.
    Color autoTextColour_{0, 0, 0, 255};

    Color AutomaticTextColour(const PropMap& props) const {
        const std::string fill = Lower(PropS(props, "draw:fill", "solid"));
        Color under = Colors::White;
        if (fill == "solid") {
            under = ColorOr(Prop(props, "draw:fill-color"), kDefaultFill);
        } else if (fill == "gradient") {
            auto it = book_.Gradients.find(PropS(props, "draw:fill-gradient-name"));
            if (it == book_.Gradients.end()) return Colors::Black;
            const auto ramp = GradientRamp(it->second);
            under = Color(static_cast<uint8_t>((ramp.front().color.r + ramp.back().color.r) / 2),
                          static_cast<uint8_t>((ramp.front().color.g + ramp.back().color.g) / 2),
                          static_cast<uint8_t>((ramp.front().color.b + ramp.back().color.b) / 2), 255);
        } else {
            return Colors::Black;
        }
        if (under.r == kDefaultFill.r && under.g == kDefaultFill.g && under.b == kDefaultFill.b) return Colors::Black;
        const int luminance = (under.r * 76 + under.g * 151 + under.b * 29) >> 8;
        return luminance <= 156 ? Colors::White : Colors::Black;
    }

    // ----- fills and strokes -----
    std::optional<LinearGradientData> LinearAxis(double angleDeg, const Rect2Dd& b) const {
        const double a = angleDeg * kPi / 180.0;
        const Point2Dd u{std::sin(a), std::cos(a)};
        const Point2Dd c{b.x + b.width / 2, b.y + b.height / 2};
        const double half = (std::fabs(b.width * u.x) + std::fabs(b.height * u.y)) / 2;
        LinearGradientData g;
        g.Units = GradientUnits::UserSpaceOnUse;
        g.Start = {c.x - u.x * half, c.y - u.y * half};
        g.End = {c.x + u.x * half, c.y + u.y * half};
        return g;
    }

    // The gradient's colour ramp: t = 0 the start colour, 1 the end colour.
    std::vector<GradientStop> GradientRamp(const NamedObject& g) const {
        const XmlPart& p = *g.Part;
        std::vector<GradientStop> stops;
        for (const XMLElement* s = g.Element->FirstChildElement(); s; s = s->NextSiblingElement()) {
            if (!p.Is(s, "loext:gradient-stop")) continue;
            double off = 0;
            if (const char* o = p.Attr(s, "svg:offset")) TryParseFloat(o, off);
            stops.emplace_back(std::clamp(off, 0.0, 1.0), ColorOr(p.Attr(s, "loext:color-value"), Colors::Black));
        }
        if (stops.size() >= 2) return stops;
        stops.clear();
        const Color start = Scaled(ColorOr(p.Attr(g.Element, "draw:start-color"), Colors::Black),
                                   PercentOr(p.Attr(g.Element, "draw:start-intensity"), 1.0));
        const Color end = Scaled(ColorOr(p.Attr(g.Element, "draw:end-color"), Colors::White),
                                 PercentOr(p.Attr(g.Element, "draw:end-intensity"), 1.0));
        stops.emplace_back(0.0, start);
        stops.emplace_back(1.0, end);
        return stops;
    }

    std::optional<GradientData> BuildGradient(const std::string& name, const Rect2Dd& b, double opacity) {
        auto it = book_.Gradients.find(name);
        if (it == book_.Gradients.end()) return std::nullopt;
        const XmlPart& p = *it->second.Part;
        const XMLElement* e = it->second.Element;
        const std::string style = Lower(p.AttrS(e, "draw:style", "linear"));
        const double border = std::clamp(PercentOr(p.Attr(e, "draw:border"), 0.0), 0.0, 0.999);
        const double angle = AngleDegrees(p.Attr(e, "draw:angle"), 0.1);
        std::vector<GradientStop> ramp = GradientRamp(it->second);
        for (auto& s : ramp) s.color = WithAlpha(s.color, opacity);

        if (style == "linear" || style == "axial") {
            LinearGradientData g = *LinearAxis(angle, b);
            if (style == "linear") {
                for (const auto& s : ramp) g.Stops.emplace_back(border + s.position * (1 - border), s.color);
            } else {
                // Start colour at both edges, end colour on the axis.
                std::vector<GradientStop> half;
                for (const auto& s : ramp) half.emplace_back((border + s.position * (1 - border)) / 2, s.color);
                for (const auto& s : half) g.Stops.push_back(s);
                for (auto r = half.rbegin(); r != half.rend(); ++r) g.Stops.emplace_back(1.0 - r->position, r->color);
            }
            return GradientData(g);
        }
        // The radial family: the start colour outside, the end colour at the
        // centre. Radial is a circle of half the box's diagonal, exactly.
        // Ellipsoid's outer contour is the ellipse of the box's width and
        // height over the square root of two, turned by draw:angle - a unit
        // circle the gradient's Transform places, which the renderer draws as
        // that ellipse. LibreOffice then shrinks both radii by the same
        // length, so its inner contours narrow to a line, where a radial
        // gradient's stay the same shape: the outline and the turn are
        // LibreOffice's, the middle is rounder (checked against LibreOffice
        // 24.2's rendering). Rectangular has the same reach with rectangular
        // contours, square a square of the diagonal; they are drawn as the
        // ellipse and the circle of that reach.
        RadialGradientData g;
        g.Units = GradientUnits::UserSpaceOnUse;
        const double cx = PercentOr(p.Attr(e, "draw:cx"), 0.5), cy = PercentOr(p.Attr(e, "draw:cy"), 0.5);
        const Point2Dd centre(b.x + cx * b.width, b.y + cy * b.height);
        const bool elliptical = style == "ellipsoid" || style == "rectangular";
        if (elliptical && b.width > 1e-6 && b.height > 1e-6) {
            g.Center = g.FocalPoint = Point2Dd(0, 0);
            g.Radius = 1;
            // draw:angle turns counter-clockwise on the page: Rotate(-angle) in
            // the model's y-down space, as draw:transform's rotate.
            g.Transform = Matrix3x3::Translate(centre.x, centre.y) * Matrix3x3::RotateDegrees(-angle) *
                          Matrix3x3::Scale(b.width / std::sqrt(2.0), b.height / std::sqrt(2.0));
        } else {
            g.Center = g.FocalPoint = centre;
            g.Radius = static_cast<float>(std::max(std::hypot(b.width, b.height) / 2, 1e-3));
        }
        for (auto r = ramp.rbegin(); r != ramp.rend(); ++r)
            g.Stops.emplace_back((1 - border) * (1 - r->position), r->color);
        if (style == "ellipsoid") Skip("ellipsoid gradient (its outline and angle kept, its middle drawn rounder)");
        if (style == "rectangular") Skip("rectangular gradient (drawn as the ellipse of its reach)");
        if (style == "square") Skip("square gradient (drawn as the circle of its reach)");
        return GradientData(g);
    }

    std::optional<TransparencyData> BuildOpacityGradient(const std::string& name, const Rect2Dd& b) {
        auto it = book_.Opacities.find(name);
        if (it == book_.Opacities.end()) return std::nullopt;
        const XmlPart& p = *it->second.Part;
        const XMLElement* e = it->second.Element;
        const std::string style = Lower(p.AttrS(e, "draw:style", "linear"));
        const double border = std::clamp(PercentOr(p.Attr(e, "draw:border"), 0.0), 0.0, 0.999);
        const double start = PercentOr(p.Attr(e, "draw:start"), 0.0);
        const double end = PercentOr(p.Attr(e, "draw:end"), 1.0);
        TransparencyData t;
        auto level = [&](double opacity) { return static_cast<float>(std::clamp(1.0 - opacity, 0.0, 1.0)); };
        if (style == "linear" || style == "axial") {
            const LinearGradientData axis = *LinearAxis(AngleDegrees(p.Attr(e, "draw:angle"), 0.1), b);
            t.Shape = TransparencyShape::Linear;
            t.Start = axis.Start;
            t.End = axis.End;
            if (style == "linear") {
                t.Stops = {{0.0, level(start)}, {border, level(start)}, {1.0, level(end)}};
            } else {
                t.Stops = {{0.0, level(start)}, {border / 2, level(start)}, {0.5, level(end)},
                           {1 - border / 2, level(start)}, {1.0, level(start)}};
            }
        } else {
            t.Shape = TransparencyShape::Radial;
            const double cx = PercentOr(p.Attr(e, "draw:cx"), 0.5), cy = PercentOr(p.Attr(e, "draw:cy"), 0.5);
            t.Start = Point2Dd(b.x + cx * b.width, b.y + cy * b.height);
            t.End = Point2Dd(t.Start.x + std::hypot(b.width, b.height) / 2, t.Start.y);
            t.Stops = {{0.0, level(end)}, {1 - border, level(start)}, {1.0, level(start)}};
        }
        return t;
    }

    std::vector<double> DashPattern(const std::string& name, double width, bool& round) {
        std::vector<double> out;
        auto it = book_.Dashes.find(name);
        if (it == book_.Dashes.end()) return {4 * width, 2 * width};
        const XmlPart& p = *it->second.Part;
        const XMLElement* e = it->second.Element;
        round = Lower(p.AttrS(e, "draw:style")) == "round";
        auto len = [&](const char* attr, double fallback) {
            const char* v = p.Attr(e, attr);
            if (!v) return fallback;
            double n;
            std::string unit;
            if (SplitNumber(v, n, unit) && unit == "%") return n / 100.0 * width;
            double l;
            return ParseLength(v, l) ? l : fallback;
        };
        int dots1 = 0, dots2 = 0;
        e->QueryIntAttribute(p.Q("draw:dots1"), &dots1);
        e->QueryIntAttribute(p.Q("draw:dots2"), &dots2);
        const double l1 = std::max(len("draw:dots1-length", width), 0.01);
        const double l2 = std::max(len("draw:dots2-length", width), 0.01);
        const double gap = std::max(len("draw:distance", width), 0.01);
        for (int k = 0; k < std::clamp(dots1, 0, 64); ++k) { out.push_back(l1); out.push_back(gap); }
        for (int k = 0; k < std::clamp(dots2, 0, 64); ++k) { out.push_back(l2); out.push_back(gap); }
        if (out.empty()) out = {4 * width, 2 * width};
        return out;
    }

    ArrowheadData Arrowhead(const char* name, const char* widthAttr, double lineWidth) const {
        ArrowheadData a;
        if (!name || !*name) return a;
        const std::string n = Lower(name);
        if (n.find("circle") != std::string::npos) a.Kind = ArrowheadKind::Circle;
        else if (n.find("square_20_45") != std::string::npos || n.find("diamond") != std::string::npos)
            a.Kind = ArrowheadKind::Diamond;
        else if (n.find("square") != std::string::npos) a.Kind = ArrowheadKind::Square;
        else if (n.find("line_20_arrow") != std::string::npos || n.find("line arrow") != std::string::npos)
            a.Kind = ArrowheadKind::OpenArrow;
        else if (n.find("dimension") != std::string::npos || n.find("line_20_short") != std::string::npos ||
                 n.find("line_20_end") != std::string::npos)
            a.Kind = ArrowheadKind::Bar;
        else a.Kind = ArrowheadKind::Triangle;
        const double markerWidth = LengthOr(widthAttr, 0.2 * kPointsPerCm);
        a.Scale = static_cast<float>(std::max(0.05, markerWidth / (2.0 * std::max(lineWidth, 0.1))));
        return a;
    }

    // Applies the style's stroke and plain fill to `e` (bounds `b`, its own
    // coordinates). Returns what has to be drawn over the fill for a hatch
    // or bitmap fill (empty for the others).
    struct FillOverlay {
        std::vector<std::shared_ptr<VectorElement>> Content;   // clipped to the shape
    };

    void ApplyStroke(VectorElement& e, const PropMap& p, bool markers) {
        const std::string stroke = Lower(PropS(p, "draw:stroke", "solid"));
        if (stroke == "none") {
            e.Style.Stroke.reset();
            return;
        }
        StrokeData s;
        s.Fill = ColorOr(Prop(p, "svg:stroke-color"), kDefaultStroke);
        const double width = LengthOr(Prop(p, "svg:stroke-width"), 0);
        s.Width = static_cast<float>(width > 0 ? width : kHairline);
        if (const char* op = Prop(p, "svg:stroke-opacity")) e.Style.StrokeOpacity = static_cast<float>(PercentOr(op, 1));
        const std::string join = Lower(PropS(p, "draw:stroke-linejoin", "round"));
        s.LineJoin = join == "miter" ? StrokeLineJoin::Miter : join == "bevel" || join == "none" ? StrokeLineJoin::Bevel
                                                                                                 : StrokeLineJoin::Round;
        const std::string cap = Lower(PropS(p, "svg:stroke-linecap", "butt"));
        s.LineCap = cap == "round" ? StrokeLineCap::Round : cap == "square" ? StrokeLineCap::Square : StrokeLineCap::Butt;
        if (stroke == "dash") {
            bool round = false;
            s.DashArray = DashPattern(PropS(p, "draw:stroke-dash"), s.Width, round);
            if (round) s.LineCap = StrokeLineCap::Round;
        }
        if (markers) {
            s.StartArrow = Arrowhead(Prop(p, "draw:marker-start"), Prop(p, "draw:marker-start-width"), s.Width);
            s.EndArrow = Arrowhead(Prop(p, "draw:marker-end"), Prop(p, "draw:marker-end-width"), s.Width);
        }
        e.Style.Stroke = s;
    }

    FillOverlay ApplyFill(VectorElement& e, const PropMap& p, const Rect2Dd& b, bool fillable) {
        FillOverlay overlay;
        const std::string fill = Lower(PropS(p, "draw:fill", "solid"));
        const double opacity = PercentOr(Prop(p, "draw:opacity"), 1.0);
        if (!fillable || fill == "none") {
            e.Style.Fill = FillData(std::monostate{});
        } else if (fill == "gradient") {
            auto g = BuildGradient(PropS(p, "draw:fill-gradient-name"), b, 1.0);
            if (g) e.Style.Fill = FillData(*g);
            else e.Style.Fill = FillData(ColorOr(Prop(p, "draw:fill-color"), kDefaultFill));
        } else if (fill == "hatch") {
            if (PropS(p, "draw:fill-hatch-solid") == "true")
                e.Style.Fill = FillData(ColorOr(Prop(p, "draw:fill-color"), kDefaultFill));
            else
                e.Style.Fill = FillData(std::monostate{});
            if (auto lines = HatchLines(PropS(p, "draw:fill-hatch-name"), b)) overlay.Content.push_back(lines);
        } else if (fill == "bitmap") {
            e.Style.Fill = FillData(std::monostate{});
            overlay.Content = BitmapTiles(p, b);
        } else {
            e.Style.Fill = FillData(ColorOr(Prop(p, "draw:fill-color"), kDefaultFill));
        }
        if (fillable && fill != "none") {
            if (opacity < 1.0) e.Style.FillOpacity = static_cast<float>(opacity);
            if (const char* og = Prop(p, "draw:opacity-name"))
                if (auto t = BuildOpacityGradient(og, b)) e.Style.Transparency = *t;
        }
        return overlay;
    }

    std::shared_ptr<VectorElement> HatchLines(const std::string& name, const Rect2Dd& b) {
        auto it = book_.Hatches.find(name);
        if (it == book_.Hatches.end() || b.width <= 0 || b.height <= 0) return nullptr;
        const XmlPart& p = *it->second.Part;
        const XMLElement* e = it->second.Element;
        const std::string style = Lower(p.AttrS(e, "draw:style", "single"));
        const Color colour = ColorOr(p.Attr(e, "draw:color"), Colors::Black);
        double distance = LengthOr(p.Attr(e, "draw:distance"), 0.1 * kPointsPerCm);
        const double radius = std::hypot(b.width, b.height) / 2;
        distance = std::max(distance, radius / 500.0);
        const double rotation = AngleDegrees(p.Attr(e, "draw:rotation"), 0.1);
        auto path = std::make_shared<VectorPath>();
        const Point2Dd c{b.x + b.width / 2, b.y + b.height / 2};
        auto family = [&](double deg) {
            const double a = deg * kPi / 180.0;
            const Point2Dd u{std::cos(a), -std::sin(a)}, n{std::sin(a), std::cos(a)};
            const int count = static_cast<int>(std::ceil(radius / distance));
            for (int k = -count; k <= count; ++k) {
                const Point2Dd o{c.x + n.x * k * distance, c.y + n.y * k * distance};
                path->MoveTo(static_cast<float>(o.x - u.x * radius), static_cast<float>(o.y - u.y * radius));
                path->LineTo(static_cast<float>(o.x + u.x * radius), static_cast<float>(o.y + u.y * radius));
            }
        };
        family(rotation);
        if (style == "double" || style == "triple") family(rotation + 90);
        if (style == "triple") family(rotation + 45);
        path->Style.Fill = FillData(std::monostate{});
        StrokeData s;
        s.Fill = colour;
        s.Width = kHairline;
        path->Style.Stroke = s;
        return path;
    }

    std::vector<std::shared_ptr<VectorElement>> BitmapTiles(const PropMap& p, const Rect2Dd& b) {
        std::vector<std::shared_ptr<VectorElement>> out;
        auto it = book_.FillImages.find(PropS(p, "draw:fill-image-name"));
        if (it == book_.FillImages.end() || b.width <= 0 || b.height <= 0) return out;
        std::vector<uint8_t> bytes;
        std::string mime, external;
        if (!ImageBytes(*it->second.Part, it->second.Element, bytes, mime, external)) return out;
        const std::string uri = "data:" + mime + ";base64," + Base64Encode(bytes, false);
        auto image = [&](const Rect2Dd& r) {
            auto img = std::make_shared<VectorImage>();
            img->Bounds = r;
            img->EmbeddedData = bytes;
            img->MimeType = mime;
            img->Source = uri;
            out.push_back(img);
        };
        const std::string repeat = Lower(PropS(p, "style:repeat", "repeat"));
        if (repeat == "stretch") {
            image(b);
            return out;
        }
        int pw = 0, ph = 0;
        double tw = 0, th = 0;
        if (ImagePixelSize(bytes, pw, ph)) { tw = pw * 0.75; th = ph * 0.75; }   // 96 dpi
        tw = LengthOr(Prop(p, "draw:fill-image-width"), tw);
        th = LengthOr(Prop(p, "draw:fill-image-height"), th);
        if (tw <= 0 || th <= 0) { image(b); return out; }
        if (repeat == "no-repeat") {
            image(Rect2Dd(b.x + (b.width - tw) / 2, b.y + (b.height - th) / 2, tw, th));
            return out;
        }
        const int cols = static_cast<int>(std::ceil(b.width / tw)), rows = static_cast<int>(std::ceil(b.height / th));
        if (static_cast<long>(cols) * rows > 2500) { image(b); Skip("bitmap fill with too many tiles (stretched)"); return out; }
        for (int r = 0; r < rows; ++r)
            for (int c = 0; c < cols; ++c) image(Rect2Dd(b.x + c * tw, b.y + r * th, tw, th));
        return out;
    }

    void ApplyShadow(VectorElement& e, const PropMap& p) {
        if (PropS(p, "draw:shadow") != "visible") return;
        ShadowEffect s;
        s.Kind = ShadowKind::Wall;
        s.Offset = Point2Dd(LengthOr(Prop(p, "draw:shadow-offset-x"), 0.2 * kPointsPerCm),
                            LengthOr(Prop(p, "draw:shadow-offset-y"), 0.2 * kPointsPerCm));
        s.Colour = ColorOr(Prop(p, "draw:shadow-color"), Color(0x80, 0x80, 0x80, 255));
        s.Darkness = static_cast<float>(PercentOr(Prop(p, "draw:shadow-opacity"), 1.0));
        s.Blur = static_cast<float>(LengthOr(Prop(p, "loext:shadow-blur"), LengthOr(Prop(p, "draw:shadow-blur"), 0)));
        e.Effects.Shadow = s;
    }

    // The shape `shape` (with its geometry in the frame's coordinates)
    // styled, with its fill overlay and text, under one transform.
    std::shared_ptr<VectorElement> Finish(const XMLElement* xml, std::vector<std::shared_ptr<VectorElement>> shapes,
                                          const PropMap& props, const Rect2Dd& frame, bool fillable, bool markers,
                                          const XMLElement* textContainer, bool isTextBox,
                                          const std::vector<ShapePart>* parts = nullptr) {
        std::vector<std::shared_ptr<VectorElement>> out;
        for (size_t k = 0; k < shapes.size(); ++k) {
            auto& shape = shapes[k];
            if (!shape) continue;
            const ShapePart* part = parts && k < parts->size() ? &(*parts)[k] : nullptr;
            ApplyStroke(*shape, props, markers);
            Rect2Dd bounds = shape->GetBoundingBox();
            if (bounds.width <= 0 && bounds.height <= 0) bounds = frame;
            FillOverlay overlay = ApplyFill(*shape, props, bounds, fillable && !(part && part->NoFill));
            if (part && part->NoStroke) shape->Style.Stroke.reset();
            if (part && part->Shade != 0 && shape->Style.Fill)
                if (auto* c = std::get_if<Color>(&*shape->Style.Fill)) *c = Shaded(*c, part->Shade);
            ApplyShadow(*shape, props);
            shape->Style.ShapeFillRule = VectorStorage::FillRule::EvenOdd;   // as LibreOffice fills every shape
            if (overlay.Content.empty()) {
                out.push_back(shape);
                continue;
            }
            // Fill, then the hatch or picture clipped to the outline, then
            // the outline itself on top.
            auto stroke = shape->Style.Stroke;
            shape->Style.Stroke.reset();
            out.push_back(shape);
            auto clip = std::make_shared<VectorClipView>();
            auto keyhole = shape->Clone();
            keyhole->Style = VectorStyle();
            keyhole->Effects = VectorEffects();
            clip->AddChild(keyhole);
            for (auto& c : overlay.Content) clip->AddChild(c);
            out.push_back(clip);
            if (stroke) {
                auto outline = shape->Clone();
                outline->Style.Fill = FillData(std::monostate{});
                outline->Style.Transparency.reset();
                outline->Style.Stroke = stroke;
                outline->Effects = VectorEffects();
                out.push_back(outline);
            }
        }
        if (textContainer) {
            const std::string defaultPara = part_->AttrS(xml, "draw:text-style-name");
            for (auto& t : BuildText(textContainer, props, frame, isTextBox, defaultPara)) out.push_back(t);
        }
        if (out.empty()) return nullptr;

        const char* transform = part_->Attr(xml, "draw:transform");
        std::shared_ptr<VectorElement> result;
        if (out.size() == 1) {
            result = out.front();
        } else {
            auto group = std::make_shared<VectorGroup>();
            for (auto& o : out) group->AddChild(o);
            result = group;
        }
        if (transform) {
            const Matrix3x3 m = ParseTransform(transform);
            if (!m.IsIdentity()) result->Transform = result->Transform ? m * *result->Transform : m;
        }
        std::string id = part_->AttrS(xml, "draw:name");
        if (id.empty()) id = part_->AttrS(xml, "xml:id", part_->AttrS(xml, "draw:id"));
        if (!id.empty()) result->Id = id;
        return result;
    }

    Rect2Dd FrameOf(const XMLElement* e) const {
        return Rect2Dd(LengthOr(part_->Attr(e, "svg:x"), 0), LengthOr(part_->Attr(e, "svg:y"), 0),
                       LengthOr(part_->Attr(e, "svg:width"), 0), LengthOr(part_->Attr(e, "svg:height"), 0));
    }

    // svg:viewBox and points / path data mapped into the frame.
    static Point2Dd ViewMap(const std::vector<double>& vb, const Rect2Dd& f, double x, double y) {
        if (vb.size() < 4) return {f.x + x, f.y + y};
        const double sx = vb[2] != 0 ? f.width / vb[2] : 1.0, sy = vb[3] != 0 ? f.height / vb[3] : 1.0;
        return {f.x + (x - vb[0]) * sx, f.y + (y - vb[1]) * sy};
    }

    static std::vector<Point2Dd> Points(const std::string& pts, const std::vector<double>& vb, const Rect2Dd& f) {
        std::vector<double> n = NumberList(pts);
        std::vector<Point2Dd> out;
        for (size_t i = 0; i + 1 < n.size(); i += 2) out.push_back(ViewMap(vb, f, n[i], n[i + 1]));
        return out;
    }

    static PathData MapPath(const std::string& d, const std::function<Point2Dd(double, double)>& map, bool& closed) {
        const PathData raw = ParsePathString(d);
        PathData out;
        // Closed when every subpath is (LibreOffice's B2DPolyPolygon::isClosed).
        bool anyOpen = false, open = false;
        for (const auto& s : PathOps::NormalizePath(raw)) {
            PathCommand c;
            if (s.kind == PathOps::FlatSeg::Move) {
                if (open) anyOpen = true;
                open = false;
                const Point2Dd p = map(s.p[0].x, s.p[0].y);
                c.Type = PathCommandType::MoveTo;
                c.Parameters = {static_cast<float>(p.x), static_cast<float>(p.y)};
            } else if (s.kind == PathOps::FlatSeg::Line) {
                const Point2Dd p = map(s.p[0].x, s.p[0].y);
                c.Type = PathCommandType::LineTo;
                c.Parameters = {static_cast<float>(p.x), static_cast<float>(p.y)};
            } else {
                c.Type = PathCommandType::CurveTo;
                for (int k = 0; k < 3; ++k) {
                    const Point2Dd p = map(s.p[k].x, s.p[k].y);
                    c.Parameters.push_back(static_cast<float>(p.x));
                    c.Parameters.push_back(static_cast<float>(p.y));
                }
            }
            if (s.kind != PathOps::FlatSeg::Move) open = true;
            out.commands.push_back(std::move(c));
            if (s.closeAfter) {
                PathCommand z;
                z.Type = PathCommandType::ClosePath;
                out.commands.push_back(z);
                open = false;
            }
        }
        closed = !out.commands.empty() && !anyOpen && !open;
        return out;
    }

    // An ellipse arc from `startDeg` to `endDeg`, counter-clockwise on the
    // page, as a path: "section" closes through the centre, "cut" across the
    // chord, "arc" stays open.
    static std::shared_ptr<VectorPath> EllipsePart(const Rect2Dd& f, double startDeg, double endDeg, const std::string& kind) {
        const double cx = f.x + f.width / 2, cy = f.y + f.height / 2, rx = f.width / 2, ry = f.height / 2;
        double sweep = endDeg - startDeg;
        while (sweep <= 0) sweep += 360;
        while (sweep > 360) sweep -= 360;
        auto path = std::make_shared<VectorPath>();
        const double t0 = -startDeg * kPi / 180.0, dt = -sweep * kPi / 180.0;
        auto pt = [&](double t) { return Point2Dd(cx + rx * std::cos(t), cy + ry * std::sin(t)); };
        if (kind == "section") {
            path->MoveTo(static_cast<float>(cx), static_cast<float>(cy));
            const Point2Dd s = pt(t0);
            path->LineTo(static_cast<float>(s.x), static_cast<float>(s.y));
        } else {
            const Point2Dd s = pt(t0);
            path->MoveTo(static_cast<float>(s.x), static_cast<float>(s.y));
        }
        const int n = std::max(1, static_cast<int>(std::ceil(std::fabs(dt) / (kPi / 2) - 1e-9)));
        const double step = dt / n, k = 4.0 / 3.0 * std::tan(step / 4);
        double t = t0;
        for (int i = 0; i < n; ++i) {
            const double t1 = t + step;
            const double c0 = std::cos(t), s0 = std::sin(t), c1 = std::cos(t1), s1 = std::sin(t1);
            path->CurveTo(static_cast<float>(cx + rx * (c0 - k * s0)), static_cast<float>(cy + ry * (s0 + k * c0)),
                          static_cast<float>(cx + rx * (c1 + k * s1)), static_cast<float>(cy + ry * (s1 - k * c1)),
                          static_cast<float>(cx + rx * c1), static_cast<float>(cy + ry * s1));
            t = t1;
        }
        if (kind != "arc") path->ClosePath();
        return path;
    }

    // The shading LibreOffice gives some stock shapes' parts from its preset
    // tables rather than from the file (the enhanced path has no H-K
    // commands for them): a can's lid, a cube's faces, a bevel's sides.
    // Measured from LibreOffice 24.2's own rendering.
    static void ApplyPresetShading(const std::string& type, std::vector<ShapePart>& parts) {
        static const std::map<std::string, std::vector<double>> kShades = {
            {"can", {0, 0.4}},
            {"cube", {0, 0.2, -0.2}},
            {"quad-bevel", {0, 0.2, -0.4, -0.2, 0.4}},
            {"vertical-scroll", {0, -0.2, -0.2}},
            {"horizontal-scroll", {0, -0.2, -0.2}},
            {"paper", {0, -0.2}},
            {"smiley", {0, -0.2, -0.2}},
        };
        auto it = kShades.find(type);
        if (it == kShades.end()) return;
        for (size_t k = 0; k < parts.size() && k < it->second.size(); ++k)
            if (parts[k].Shade == 0) parts[k].Shade = it->second[k];
    }

    std::shared_ptr<VectorElement> ReadCustomShape(const XMLElement* e, const PropMap& props) {
        const Rect2Dd frame = FrameOf(e);
        const XMLElement* geo = part_->Child(e, "draw:enhanced-geometry");
        std::vector<std::shared_ptr<VectorElement>> shapes;
        std::vector<ShapePart> parts;
        if (geo) {
            EnhancedGeometry g;
            g.Frame = frame;
            const std::vector<double> vb = NumberList(part_->AttrS(geo, "svg:viewBox"));
            if (vb.size() >= 4) g.ViewBox = Rect2Dd(vb[0], vb[1], vb[2], vb[3]);
            g.Modifiers = NumberList(part_->AttrS(geo, "draw:modifiers"));
            g.MirrorH = part_->AttrS(geo, "draw:mirror-horizontal") == "true";
            g.MirrorV = part_->AttrS(geo, "draw:mirror-vertical") == "true";
            double v;
            if (const char* sx = part_->Attr(geo, "draw:path-stretchpoint-x"); sx && TryParseFloat(sx, v)) g.StretchX = v;
            if (const char* sy = part_->Attr(geo, "draw:path-stretchpoint-y"); sy && TryParseFloat(sy, v)) g.StretchY = v;
            g.HasStroke = Lower(PropS(props, "draw:stroke", "solid")) != "none";
            g.HasFill = Lower(PropS(props, "draw:fill", "solid")) != "none";
            for (const XMLElement* q = geo->FirstChildElement(); q; q = q->NextSiblingElement())
                if (part_->Is(q, "draw:equation"))
                    g.Equations[part_->AttrS(q, "draw:name")] = part_->AttrS(q, "draw:formula");
            const std::string type = part_->AttrS(geo, "draw:type");
            const std::string enhanced = part_->AttrS(geo, "draw:enhanced-path");
            if (!enhanced.empty()) {
                parts = g.Build(enhanced);
                ApplyPresetShading(type, parts);
                for (ShapePart& sp : parts) {
                    auto path = std::make_shared<VectorPath>();
                    path->Path = sp.Path;
                    path->Path.InvalidateCache();
                    shapes.push_back(path);
                }
            } else if (type == "ellipse" || type == "ooxml-ellipse") {
                auto el = std::make_shared<VectorEllipse>();
                el->Center = Point2Dd(frame.x + frame.width / 2, frame.y + frame.height / 2);
                el->RadiusX = static_cast<float>(frame.width / 2);
                el->RadiusY = static_cast<float>(frame.height / 2);
                shapes.push_back(el);
            } else if (type == "rectangle" || type == "ooxml-rect" || type.empty()) {
                auto r = std::make_shared<VectorRect>();
                r->Bounds = frame;
                shapes.push_back(r);
            } else {
                // A preset named without its path, which LibreOffice draws
                // as nothing either (only its text).
                Skip("custom shape '" + type + "' without a path");
            }
        }
        // The parts' own fill / stroke switches (F, S) and shading.
        return Finish(e, shapes, props, frame, true, true, e, false, parts.empty() ? nullptr : &parts);
    }

    std::shared_ptr<VectorElement> ReadFrame(const XMLElement* e, const PropMap& props) {
        const Rect2Dd frame = FrameOf(e);
        // A picture: prefer a bitmap over the SVG LibreOffice puts first for
        // a vector picture (the bitmap is its fallback).
        const XMLElement* chosen = nullptr;
        for (const XMLElement* c = e->FirstChildElement(); c; c = c->NextSiblingElement()) {
            if (!part_->Is(c, "draw:image")) continue;
            std::string mime = part_->AttrS(c, "draw:mime-type", part_->AttrS(c, "loext:mime-type"));
            if (mime.empty()) mime = MimeTypeForPath(part_->AttrS(c, "xlink:href"));
            if (!chosen || mime != "image/svg+xml") chosen = c;
            if (mime != "image/svg+xml") break;
        }
        if (chosen) {
            std::vector<uint8_t> bytes;
            std::string mime, external;
            auto img = std::make_shared<VectorImage>();
            img->Bounds = frame;
            if (ImageBytes(*part_, chosen, bytes, mime, external)) {
                img->Source = "data:" + mime + ";base64," + Base64Encode(bytes, false);
                img->EmbeddedData = std::move(bytes);
                img->MimeType = mime;
                ++imageCount_;
            } else if (!external.empty()) {
                img->Source = external;
                img->MimeType = MimeTypeForPath(external);
            } else {
                Skip("picture whose data is missing");
                return nullptr;
            }
            PropMap noLine = props;
            return Finish(e, {img}, noLine, frame, false, false, nullptr, false);
        }
        if (const XMLElement* box = part_->Child(e, "draw:text-box")) {
            // The frame's own fill and line, if its style has them.
            std::vector<std::shared_ptr<VectorElement>> shapes;
            const bool visibleFill = Lower(PropS(props, "draw:fill", "solid")) != "none";
            const bool visibleLine = Lower(PropS(props, "draw:stroke", "solid")) != "none";
            if (visibleFill || visibleLine) {
                auto r = std::make_shared<VectorRect>();
                r->Bounds = frame;
                shapes.push_back(r);
            }
            return Finish(e, shapes, props, frame, true, false, box, true);
        }
        for (const XMLElement* c = e->FirstChildElement(); c; c = c->NextSiblingElement()) {
            if (part_->Is(c, "draw:object") || part_->Is(c, "draw:object-ole")) { Skip("embedded object without a replacement picture"); return nullptr; }
            if (part_->Is(c, "draw:plugin") || part_->Is(c, "draw:applet") || part_->Is(c, "draw:floating-frame")) {
                Skip("plug-in, applet or floating frame");
                return nullptr;
            }
        }
        return nullptr;
    }

    std::shared_ptr<VectorElement> ReadShape(const XMLElement* e) {
        if (part_->Is(e, "draw:g")) {
            auto group = std::make_shared<VectorGroup>();
            for (const XMLElement* c = e->FirstChildElement(); c; c = c->NextSiblingElement())
                if (auto child = ReadShape(c)) group->AddChild(child);
            if (group->Children.empty()) return nullptr;
            if (const char* t = part_->Attr(e, "draw:transform")) group->Transform = ParseTransform(t);
            const std::string id = part_->AttrS(e, "draw:name");
            if (!id.empty()) group->Id = id;
            return group;
        }
        if (part_->Is(e, "draw:a")) {
            // A hyperlink around shapes: the shapes.
            auto group = std::make_shared<VectorGroup>();
            for (const XMLElement* c = e->FirstChildElement(); c; c = c->NextSiblingElement())
                if (auto child = ReadShape(c)) group->AddChild(child);
            if (group->Children.empty()) return nullptr;
            if (group->Children.size() == 1) return group->Children.front();
            return group;
        }
        const PropMap props = GraphicProps(e);
        const Rect2Dd frame = FrameOf(e);
        if (part_->Is(e, "draw:rect") || part_->Is(e, "draw:caption")) {
            auto r = std::make_shared<VectorRect>();
            r->Bounds = frame;
            double radius = LengthOr(part_->Attr(e, "draw:corner-radius"), 0);
            radius = std::min(radius, std::min(frame.width, frame.height) / 2);
            r->RadiusX = r->RadiusY = static_cast<float>(radius);
            if (part_->Is(e, "draw:caption")) Skip("caption's pointer (its frame is drawn)");
            return Finish(e, {r}, props, frame, true, false, e, false);
        }
        if (part_->Is(e, "draw:ellipse") || part_->Is(e, "draw:circle")) {
            const std::string kind = Lower(part_->AttrS(e, "draw:kind", "full"));
            std::shared_ptr<VectorElement> shape;
            Rect2Dd f = frame;
            if (part_->Is(e, "draw:circle") && !part_->Attr(e, "svg:width") && part_->Attr(e, "svg:r")) {
                const double r = LengthOr(part_->Attr(e, "svg:r"), 0);
                f = Rect2Dd(LengthOr(part_->Attr(e, "svg:cx"), 0) - r, LengthOr(part_->Attr(e, "svg:cy"), 0) - r, 2 * r, 2 * r);
            }
            const double s = AngleDegrees(part_->Attr(e, "draw:start-angle"), 1.0, 0);
            const double en = AngleDegrees(part_->Attr(e, "draw:end-angle"), 1.0, 360);
            const bool whole = std::fabs(std::fmod(en - s, 360.0)) < 1e-9 && en != s;
            if (kind == "full" || (whole && kind != "arc")) {
                auto el = std::make_shared<VectorEllipse>();
                el->Center = Point2Dd(f.x + f.width / 2, f.y + f.height / 2);
                el->RadiusX = static_cast<float>(f.width / 2);
                el->RadiusY = static_cast<float>(f.height / 2);
                shape = el;
            } else {
                shape = EllipsePart(f, s, en, kind);
            }
            return Finish(e, {shape}, props, f, kind != "arc", kind == "arc", e, false);
        }
        if (part_->Is(e, "draw:line") || part_->Is(e, "draw:measure")) {
            auto l = std::make_shared<VectorLine>();
            l->Start = Point2Dd(LengthOr(part_->Attr(e, "svg:x1"), 0), LengthOr(part_->Attr(e, "svg:y1"), 0));
            l->End = Point2Dd(LengthOr(part_->Attr(e, "svg:x2"), 0), LengthOr(part_->Attr(e, "svg:y2"), 0));
            const Rect2Dd f(std::min(l->Start.x, l->End.x), std::min(l->Start.y, l->End.y),
                            std::fabs(l->End.x - l->Start.x), std::fabs(l->End.y - l->Start.y));
            if (part_->Is(e, "draw:measure")) Skip("dimension line's extension lines and value");
            return Finish(e, {l}, props, f, false, true, part_->Is(e, "draw:line") ? e : nullptr, false);
        }
        if (part_->Is(e, "draw:connector")) {
            std::shared_ptr<VectorElement> shape;
            const std::string d = part_->AttrS(e, "svg:d");
            if (!d.empty()) {
                // The routed connector: its path in 1/100 mm on the page.
                bool closed;
                auto path = std::make_shared<VectorPath>();
                path->Path = MapPath(d, [](double x, double y) { return Point2Dd(x * 72.0 / 2540.0, y * 72.0 / 2540.0); }, closed);
                shape = path;
            } else {
                auto l = std::make_shared<VectorLine>();
                l->Start = Point2Dd(LengthOr(part_->Attr(e, "svg:x1"), 0), LengthOr(part_->Attr(e, "svg:y1"), 0));
                l->End = Point2Dd(LengthOr(part_->Attr(e, "svg:x2"), 0), LengthOr(part_->Attr(e, "svg:y2"), 0));
                shape = l;
            }
            return Finish(e, {shape}, props, shape->GetBoundingBox(), false, true, e, false);
        }
        if (part_->Is(e, "draw:polyline") || part_->Is(e, "draw:polygon")) {
            const std::vector<double> vb = NumberList(part_->AttrS(e, "svg:viewBox"));
            const std::vector<Point2Dd> pts = Points(part_->AttrS(e, "draw:points"), vb, frame);
            if (pts.empty()) return nullptr;
            if (part_->Is(e, "draw:polygon")) {
                auto pg = std::make_shared<VectorPolygon>();
                pg->Points = pts;
                return Finish(e, {pg}, props, frame, true, false, e, false);
            }
            auto pl = std::make_shared<VectorPolyline>();
            pl->Points = pts;
            return Finish(e, {pl}, props, frame, false, true, e, false);
        }
        if (part_->Is(e, "draw:path")) {
            const std::vector<double> vb = NumberList(part_->AttrS(e, "svg:viewBox"));
            bool closed = false;
            auto path = std::make_shared<VectorPath>();
            path->Path = MapPath(part_->AttrS(e, "svg:d"), [&](double x, double y) { return ViewMap(vb, frame, x, y); }, closed);
            if (path->Path.commands.empty()) return nullptr;
            path->Path.InvalidateCache();
            // LibreOffice fills a path only when every subpath is closed.
            return Finish(e, {path}, props, frame, closed, !closed, e, false);
        }
        if (part_->Is(e, "draw:regular-polygon")) {
            int corners = 5;
            e->QueryIntAttribute(part_->Q("draw:corners"), &corners);
            corners = std::clamp(corners, 3, 1000);
            const bool concave = part_->AttrS(e, "draw:concave") == "true";
            const double sharp = PercentOr(part_->Attr(e, "draw:sharpness"), 0.5);
            auto pg = std::make_shared<VectorPolygon>();
            const double cx = frame.x + frame.width / 2, cy = frame.y + frame.height / 2;
            const int n = concave ? corners * 2 : corners;
            for (int k = 0; k < n; ++k) {
                const double a = -kPi / 2 + k * 2 * kPi / n;
                const double r = concave && (k % 2) ? 1.0 - sharp : 1.0;
                pg->Points.emplace_back(cx + std::cos(a) * r * frame.width / 2, cy + std::sin(a) * r * frame.height / 2);
            }
            return Finish(e, {pg}, props, frame, true, false, e, false);
        }
        if (part_->Is(e, "draw:custom-shape")) return ReadCustomShape(e, props);
        if (part_->Is(e, "draw:frame")) return ReadFrame(e, props);
        if (part_->Is(e, "dr3d:scene")) { Skip("3D scene"); return nullptr; }
        if (part_->Is(e, "draw:control")) { Skip("form control"); return nullptr; }
        if (part_->Is(e, "draw:page-thumbnail")) { Skip("page thumbnail"); return nullptr; }
        if (part_->Is(e, "office:forms") || part_->Is(e, "presentation:notes") || part_->Is(e, "office:annotation") ||
            part_->Is(e, "svg:title") || part_->Is(e, "svg:desc") || part_->Is(e, "draw:layer-set") ||
            part_->Is(e, "text:p") || part_->Is(e, "draw:glue-point") || part_->Is(e, "loext:theme"))
            return nullptr;
        Skip(std::string("element <") + e->Name() + ">");
        return nullptr;
    }

    // The layer a shape (or group) is on.
    std::string LayerOf(const XMLElement* e) const {
        if (const char* l = part_->Attr(e, "draw:layer")) return l;
        if (part_->Is(e, "draw:g") || part_->Is(e, "draw:a"))
            for (const XMLElement* c = e->FirstChildElement(); c; c = c->NextSiblingElement())
                if (const char* l = part_->Attr(c, "draw:layer")) return l;
        return "layout";
    }

    void ReadShapes(const XMLElement* container, std::vector<std::pair<std::string, std::shared_ptr<VectorElement>>>& out) {
        for (const XMLElement* c = container->FirstChildElement(); c; c = c->NextSiblingElement()) {
            if (auto el = ReadShape(c)) out.emplace_back(LayerOf(c), el);
        }
    }
};

std::shared_ptr<VectorDocument> OdgReader::Read(const std::string& data) {
    std::string contentXml, stylesXml, metaXml;
    const bool zipped = data.size() >= 4 && data[0] == 'P' && data[1] == 'K';
    if (zipped) {
        if (!zip_.OpenFromMemory(std::vector<uint8_t>(data.begin(), data.end()))) {
            Warn("not a valid ZIP package: " + zip_.GetLastError());
            return nullptr;
        }
        isPackage_ = true;
        std::string mime;
        if (zip_.ReadEntry("mimetype", mime) && mime.find("opendocument.graphics") == std::string::npos)
            Warn("package media type is '" + mime + "', not an OpenDocument drawing; reading it anyway");
        if (!zip_.ReadEntry("content.xml", contentXml)) {
            Warn("the package has no content.xml");
            return nullptr;
        }
        zip_.ReadEntry("styles.xml", stylesXml);
        zip_.ReadEntry("meta.xml", metaXml);
    } else {
        contentXml = data;
    }
    if (!content_.Load(contentXml)) {
        Warn("content.xml is not well-formed XML");
        return nullptr;
    }
    haveStyles_ = !stylesXml.empty() && styles_.Load(stylesXml);
    haveMeta_ = !metaXml.empty() && meta_.Load(metaXml);
    const XmlPart& sp = haveStyles_ ? styles_ : content_;   // a flat document is all one part
    const XmlPart& mp = haveMeta_ ? meta_ : content_;

    const XMLElement* croot = content_.Doc.RootElement();
    const XMLElement* sroot = sp.Doc.RootElement();
    if (!croot) return nullptr;

    // Styles: the styles part's (common, automatic, masters), then content's
    // automatic ones. In a flat document both are the same element.
    book_.ReadFontFaces(sp, sp.Child(sroot, "office:font-face-decls"));
    if (haveStyles_) book_.ReadFontFaces(content_, content_.Child(croot, "office:font-face-decls"));
    book_.ReadStyles(sp, sp.Child(sroot, "office:styles"), book_.Common);
    if (haveStyles_) {
        book_.ReadStyles(sp, sp.Child(sroot, "office:automatic-styles"), book_.AutoStyles);
        book_.ReadStyles(content_, content_.Child(croot, "office:automatic-styles"), book_.AutoContent);
    } else {
        book_.ReadStyles(content_, content_.Child(croot, "office:automatic-styles"), book_.AutoContent);
    }
    // A flat document's master pages use the same automatic styles.
    const std::map<std::string, StyleEntry>& masterAuto = haveStyles_ ? book_.AutoStyles : book_.AutoContent;
    const XMLElement* masters = sp.Child(sroot, "office:master-styles");
    for (const XMLElement* m = masters ? masters->FirstChildElement() : nullptr; m; m = m->NextSiblingElement()) {
        if (sp.Is(m, "style:master-page")) book_.MasterPages[sp.AttrS(m, "style:name")] = {m, &sp};
        if (sp.Is(m, "draw:layer-set")) {
            for (const XMLElement* l = m->FirstChildElement(); l; l = l->NextSiblingElement()) {
                if (!sp.Is(l, "draw:layer")) continue;
                const std::string display = sp.AttrS(l, "draw:display", "always");
                layerState_[sp.AttrS(l, "draw:name")] = {display != "none", sp.AttrS(l, "draw:protected") == "true"};
            }
        }
    }

    const XMLElement* body = content_.Child(croot, "office:body");
    const XMLElement* drawing = content_.Child(body, "office:drawing");
    if (!drawing) drawing = content_.Child(body, "office:presentation");
    if (!drawing) {
        Warn("the document has no drawing body (not an OpenDocument drawing)");
        return nullptr;
    }
    const XMLElement* page = content_.Child(drawing, "draw:page");
    int pageCount = 0;
    for (const XMLElement* p = page; p; p = p->NextSiblingElement(content_.Q("draw:page"))) ++pageCount;

    auto doc = std::make_shared<VectorDocument>();

    // Page size from the master page's layout.
    double pageW = 0, pageH = 0;
    LengthUnit unit = LengthUnit::Unspecified;
    double unitPoints = 1.0;
    const XMLElement* master = nullptr;
    if (page) {
        auto mit = book_.MasterPages.find(content_.AttrS(page, "draw:master-page-name"));
        if (mit == book_.MasterPages.end() && !book_.MasterPages.empty()) mit = book_.MasterPages.begin();
        if (mit != book_.MasterPages.end()) {
            master = mit->second.Element;
            auto lit = book_.PageLayouts.find(sp.AttrS(master, "style:page-layout-name"));
            if (lit != book_.PageLayouts.end()) {
                const XmlPart& lp = *lit->second.Part;
                const XMLElement* props = lp.Child(lit->second.Element, "style:page-layout-properties");
                const std::string w = lp.AttrS(props, "fo:page-width");
                pageW = LengthOr(w.c_str(), 0);
                pageH = LengthOr(lp.Attr(props, "fo:page-height"), 0);
                double v;
                std::string u;
                if (SplitNumber(w, v, u)) {
                    if (u == "cm") { unit = LengthUnit::Centimeter; unitPoints = kPointsPerCm; }
                    else if (u == "mm") { unit = LengthUnit::Millimeter; unitPoints = kPointsPerCm / 10; }
                    else if (u == "in" || u == "inch") { unit = LengthUnit::Inch; unitPoints = 72; }
                    else if (u == "pt") { unit = LengthUnit::Point; unitPoints = 1; }
                }
            }
        }
    }

    // Shapes: the master page's (beneath), then the page's.
    std::vector<std::pair<std::string, std::shared_ptr<VectorElement>>> shapes;
    if (master) {
        part_ = &sp;
        autoStyles_ = &masterAuto;
        ReadShapes(master, shapes);
    }
    if (page) {
        part_ = &content_;
        autoStyles_ = &book_.AutoContent;
        ReadShapes(page, shapes);

        // A solid page background.
        PropMap bg = book_.Resolve("drawing-page", content_.AttrS(page, "draw:style-name"), &book_.AutoContent);
        if (PropS(bg, "draw:fill") != "solid" && master)
            bg = book_.Resolve("drawing-page", sp.AttrS(master, "draw:style-name"), &masterAuto);
        if (PropS(bg, "draw:fill") == "solid") doc->BackgroundColor = ColorOr(Prop(bg, "draw:fill-color"), Colors::White);
    }

    // Layers in the order they first appear - when that keeps the drawing
    // order (each layer's shapes together); otherwise one layer, since a
    // layer in LibreOffice does not change what is drawn over what.
    std::vector<std::string> order;
    bool contiguous = true;
    for (size_t i = 0; i < shapes.size(); ++i) {
        const std::string& l = shapes[i].first;
        if (std::find(order.begin(), order.end(), l) == order.end()) order.push_back(l);
        else if (i > 0 && shapes[i - 1].first != l) contiguous = false;
    }
    auto layerFlags = [&](const std::string& name, VectorLayer& layer) {
        auto st = layerState_.find(name);
        if (st == layerState_.end()) return;
        layer.Visible = st->second.first;
        layer.Style.Visible = st->second.first;
        layer.Locked = st->second.second;
    };
    if (shapes.empty()) {
        doc->AddLayer("layout");
    } else if (contiguous) {
        for (const std::string& name : order) {
            auto layer = doc->AddLayer(name);
            layerFlags(name, *layer);
            for (auto& [l, el] : shapes)
                if (l == name) layer->AddChild(el);
        }
    } else {
        auto layer = doc->AddLayer("layout");
        for (auto& [l, el] : shapes) {
            auto st = layerState_.find(l);
            if (st != layerState_.end() && !st->second.first) el->Style.Visible = false;
            layer->AddChild(el);
        }
        Warn("shapes of different layers are interleaved; read onto one layer to keep their stacking order");
    }

    if (pageW <= 0 || pageH <= 0) {
        const Rect2Dd b = ContentBounds(*doc);
        pageW = std::max(1.0, b.x + b.width);
        pageH = std::max(1.0, b.y + b.height);
    }
    doc->Size = Size2Dd{pageW, pageH};
    doc->ViewBox = Rect2Dd(0, 0, pageW, pageH);
    doc->SourceUnit = unit;
    doc->PointsPerSourceUnit = unitPoints;

    // Metadata.
    if (const XMLElement* mroot = mp.Doc.RootElement()) {
        if (const XMLElement* m = mp.Child(mroot, "office:meta")) {
            auto text = [&](const char* n) {
                const XMLElement* x = mp.Child(m, n);
                return x && x->GetText() ? std::string(x->GetText()) : std::string();
            };
            doc->Title = text("dc:title");
            doc->Description = text("dc:description");
            doc->Author = text("meta:initial-creator");
            if (doc->Author.empty()) doc->Author = text("dc:creator");
            const std::string generator = text("meta:generator");
            if (!generator.empty()) doc->Metadata["generator"] = generator;
        }
    }
    doc->Metadata["format"] = "OpenDocument Drawing";

    if (pageCount > 1)
        Warn("the drawing has " + std::to_string(pageCount) + " pages; the first is read");
    for (const auto& [what, count] : skipped_)
        Warn(what + (count > 1 ? " (x" + std::to_string(count) + ")" : "") + " not read");
    return doc;
}

}   // namespace

std::shared_ptr<VectorDocument> ReadODGDocument(const std::string& data, const ConversionOptions& options) {
    OdgReader reader(options);
    return reader.Read(data);
}

} // namespace VectorConverter
} // namespace UltraCanvas
