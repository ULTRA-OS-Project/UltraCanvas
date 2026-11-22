// core/UltraCanvasPresentationOdp.cpp
// OpenDocument presentation (.odp / .otp / .fodp) reader - see UltraCanvasPresentationOdp.h.
// Version: 1.3.0 - the look as drawn: styles resolved through their parents and defaults (fills, gradients,
//                  outlines, dashes, arrowheads, text areas, fonts, sizes, colours, spacing, bullets and
//                  numbering), outlines (ODF and Office shape types, enhanced paths, rectangles, ellipses,
//                  polygons, paths, lines, connectors), turns and mirroring, table cells' looks, slide
//                  backgrounds, the theme's colours, and (withPictures) the pictures' bytes
// Version: 1.2.0 - charts (chart objects, UltraCanvasOdfChart): a package's "Object N" and a flat file's inline one
// Version: 1.1.0 - a password-protected deck opened with its password (UltraCanvasOdfEncryption.h)
// Version: 1.0.0
// Last Modified: 2026-11-22
// Author: UltraCanvas Framework
#include "UltraCanvasPresentationOdp.h"
#include "UltraCanvasOdfChart.h"
#include "UltraCanvasOdfEncryption.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasTextUtils.h"
#include "UltraCanvasZipPackage.h"
#include "tinyxml2.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <map>
#include <set>

namespace UltraCanvas {

namespace {

constexpr const char* kPresentationMediaType = "application/vnd.oasis.opendocument.presentation";
constexpr double kPi = 3.14159265358979323846;
constexpr double kEmuPerPoint = 12700.0;

const char* LocalName(const tinyxml2::XMLElement* e) {
    const char* name = e->Name();
    const char* colon = std::strrchr(name, ':');
    return colon ? colon + 1 : name;
}

bool Is(const tinyxml2::XMLElement* e, const char* localName) {
    return e && std::strcmp(LocalName(e), localName) == 0;
}

const tinyxml2::XMLElement* Child(const tinyxml2::XMLElement* parent, const char* localName) {
    for (auto* e = parent ? parent->FirstChildElement() : nullptr; e; e = e->NextSiblingElement())
        if (Is(e, localName)) return e;
    return nullptr;
}

// The attribute by its local name; null where the element has none. ODF
// writers use the standard prefixes, and `prefix` picks the one meant where
// an element could carry the same local name twice (draw:style-name beside
// presentation:style-name); a writer with prefixes of its own still finds the
// attribute by local name.
const char* AttrPtr(const tinyxml2::XMLElement* e, const char* localName, const char* prefix = nullptr) {
    const char* anyPrefix = nullptr;
    for (const tinyxml2::XMLAttribute* a = e ? e->FirstAttribute() : nullptr; a; a = a->Next()) {
        const char* name = a->Name();
        const char* colon = std::strrchr(name, ':');
        if (std::strcmp(colon ? colon + 1 : name, localName) != 0) continue;
        if (!prefix) return a->Value();
        const size_t length = colon ? static_cast<size_t>(colon - name) : 0;
        if (length == std::strlen(prefix) && std::strncmp(name, prefix, length) == 0) return a->Value();
        if (!anyPrefix) anyPrefix = a->Value();
    }
    return anyPrefix;
}

std::string Attr(const tinyxml2::XMLElement* e, const char* localName, const char* prefix = nullptr) {
    const char* value = AttrPtr(e, localName, prefix);
    return value ? value : "";
}

bool Number(const std::string& text, double& out) {
    size_t end = 0;
    while (end < text.size() && (std::isdigit(static_cast<unsigned char>(text[end])) || text[end] == '.' ||
                                 text[end] == '-' || text[end] == '+' || text[end] == 'e' || text[end] == 'E'))
        ++end;
    return end > 0 && TryParseFloat(text.substr(0, end), out);
}

// An ODF length ("2.54cm", "10mm", "1in", "72pt", "6pc", "96px") in EMU
// (914400 per inch); 0 for none or one it cannot read.
int64_t LengthToEmu(const std::string& value) {
    size_t unit = 0;
    while (unit < value.size() && (std::isdigit(static_cast<unsigned char>(value[unit])) ||
                                   value[unit] == '.' || value[unit] == '-' || value[unit] == '+'))
        ++unit;
    double number = 0.0;
    if (unit == 0 || !TryParseFloat(value.substr(0, unit), number)) return 0;
    std::string suffix = value.substr(unit);
    while (!suffix.empty() && std::isspace(static_cast<unsigned char>(suffix.back()))) suffix.pop_back();
    double perUnit = 0.0;
    if (suffix == "cm") perUnit = 360000.0;
    else if (suffix == "mm") perUnit = 36000.0;
    else if (suffix == "in" || suffix == "inch") perUnit = 914400.0;
    else if (suffix == "pt") perUnit = 12700.0;
    else if (suffix == "pc") perUnit = 152400.0;
    else if (suffix == "px") perUnit = 9525.0;
    else return 0;
    return static_cast<int64_t>(std::llround(number * perUnit));
}

bool IsPercent(const std::string& value) {
    return !value.empty() && value.back() == '%';
}

// "#4f81bd" as the model keeps a colour ("4F81BD"); empty for none,
// "transparent" or one it cannot read.
std::string Hex(const std::string& colour) {
    if (colour.size() != 7 || colour[0] != '#') return "";
    std::string out;
    for (size_t i = 1; i < 7; ++i) {
        if (!std::isxdigit(static_cast<unsigned char>(colour[i]))) return "";
        out += static_cast<char>(std::toupper(static_cast<unsigned char>(colour[i])));
    }
    return out;
}

// A colour at an intensity (ODF's gradient start- / end-intensity).
std::string Dimmed(const std::string& hex, double percent) {
    if (hex.size() != 6 || percent >= 99.99) return hex;
    static const char* const kDigits = "0123456789ABCDEF";
    std::string out;
    for (size_t i = 0; i < 6; i += 2) {
        const int value = static_cast<int>(std::strtol(hex.substr(i, 2).c_str(), nullptr, 16));
        const int scaled = std::clamp(static_cast<int>(std::lround(value * percent / 100.0)), 0, 255);
        out += kDigits[scaled >> 4];
        out += kDigits[scaled & 15];
    }
    return out;
}

std::string Unquoted(std::string name) {
    while (!name.empty() && (name.front() == '\'' || name.front() == '"' || name.front() == ' ')) name.erase(0, 1);
    while (!name.empty() && (name.back() == '\'' || name.back() == '"' || name.back() == ' ')) name.pop_back();
    // A list of families: the first.
    const size_t comma = name.find(',');
    if (comma != std::string::npos) return Unquoted(name.substr(0, comma));
    return name;
}

uint32_t FirstCodepoint(const std::string& text) {
    if (text.empty()) return 0;
    const unsigned char c = static_cast<unsigned char>(text[0]);
    if (c < 0x80) return c;
    int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    uint32_t cp = c & (0x3F >> extra);
    if (text.size() < static_cast<size_t>(extra) + 1) return 0;
    for (int i = 1; i <= extra; ++i) cp = (cp << 6) | (static_cast<unsigned char>(text[i]) & 0x3F);
    return cp;
}

std::string Utf8(uint32_t cp) {
    std::string out;
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return out;
}

// ----- Styles -----

// The styles one part of the deck sees: content.xml sees the common styles
// and its own automatic ones; the master pages see the common styles and
// styles.xml's automatic ones (whose names may repeat the content's).
struct StyleScope {
    std::map<std::string, const tinyxml2::XMLElement*> styles;   // "<family>:<name>"
    std::map<std::string, const tinyxml2::XMLElement*> lists;    // text:list-style by name
};

struct OdpContext {
    StyleScope content, master;
    std::map<std::string, const tinyxml2::XMLElement*> defaults;   // style:default-style by family
    std::map<std::string, const tinyxml2::XMLElement*> gradients, dashes, markers, templates;
    std::map<std::string, std::string> fonts;                      // font face -> its family
    struct Master {
        std::string pageLayout, pageStyle;
    };
    std::map<std::string, Master> masters;
    std::map<std::string, std::pair<int64_t, int64_t>> pageLayouts;   // page layout -> width, height
    std::map<std::string, std::string> themeColors;
    std::string firstMaster;
    // A part of the package by its path ("Object 1/content.xml"); none for a flat file.
    std::function<bool(const std::string&, std::string&)> readEntry;
    bool withPictures = false;
};

// What a container holds (office:styles, office:automatic-styles), into the
// scopes given. A later container's style of the same name replaces an
// earlier one's.
void CollectStyles(const tinyxml2::XMLElement* container, OdpContext& context,
                   std::initializer_list<StyleScope*> scopes) {
    for (auto* s = container ? container->FirstChildElement() : nullptr; s; s = s->NextSiblingElement()) {
        const std::string name = Attr(s, "name", "style");
        if (Is(s, "style")) {
            if (name.empty()) continue;
            for (StyleScope* scope : scopes) scope->styles[Attr(s, "family", "style") + ":" + name] = s;
        } else if (Is(s, "default-style")) {
            context.defaults[Attr(s, "family", "style")] = s;
        } else if (Is(s, "list-style")) {
            for (StyleScope* scope : scopes) scope->lists[name] = s;
        } else if (Is(s, "page-layout")) {
            const tinyxml2::XMLElement* props = Child(s, "page-layout-properties");
            context.pageLayouts[name] = {LengthToEmu(Attr(props, "page-width", "fo")),
                                         LengthToEmu(Attr(props, "page-height", "fo"))};
        } else if (Is(s, "gradient")) {
            context.gradients[Attr(s, "name", "draw")] = s;
        } else if (Is(s, "stroke-dash")) {
            context.dashes[Attr(s, "name", "draw")] = s;
        } else if (Is(s, "marker")) {
            context.markers[Attr(s, "name", "draw")] = s;
        } else if (Is(s, "table-template")) {
            std::string templateName = Attr(s, "name", "table");
            if (templateName.empty()) templateName = Attr(s, "name", "text");
            context.templates[templateName] = s;
        }
    }
}

void CollectFonts(const tinyxml2::XMLElement* decls, OdpContext& context) {
    for (auto* f = decls ? decls->FirstChildElement() : nullptr; f; f = f->NextSiblingElement()) {
        if (!Is(f, "font-face")) continue;
        std::string family = Unquoted(Attr(f, "font-family", "svg"));
        const std::string name = Attr(f, "name", "style");
        context.fonts[name] = family.empty() ? name : family;
    }
}

void CollectMasters(const tinyxml2::XMLElement* masterStyles, OdpContext& context) {
    static const std::pair<const char*, const char*> kThemeNames[] = {
        {"dark1", "dk1"},         {"light1", "lt1"},         {"dark2", "dk2"},         {"light2", "lt2"},
        {"accent1", "accent1"},   {"accent2", "accent2"},    {"accent3", "accent3"},   {"accent4", "accent4"},
        {"accent5", "accent5"},   {"accent6", "accent6"},    {"hyperlink", "hlink"},   {"followed-hyperlink", "folHlink"}};
    for (auto* m = Child(masterStyles, "master-page"); m; m = m->NextSiblingElement()) {
        if (!Is(m, "master-page")) continue;
        const std::string name = Attr(m, "name", "style");
        if (context.firstMaster.empty()) context.firstMaster = name;
        context.masters[name] = {Attr(m, "page-layout-name", "style"), Attr(m, "style-name", "draw")};
        // LibreOffice's theme (7.6 and later), its colours by Office's names.
        if (!context.themeColors.empty()) continue;
        for (auto* c = Child(Child(Child(m, "theme"), "theme-colors"), "color"); c; c = c->NextSiblingElement()) {
            const std::string colourName = Attr(c, "name");
            const std::string hex = Hex(Attr(c, "color"));
            for (const auto& [odf, office] : kThemeNames)
                if (colourName == odf && !hex.empty()) context.themeColors[office] = hex;
        }
    }
}

// Where a property comes from: a chain of named styles - the most particular
// first (a span's, then its paragraph's, then the shape's) - each through its
// parents, then the family defaults.
class Look {
public:
    explicit Look(const OdpContext& context) : context_(&context) {}

    // This look under a style of its own, which goes before the rest.
    Look With(const StyleScope& scope, const std::string& family, const std::string& name) const {
        Look look = *this;
        if (!name.empty()) look.sources_.insert(look.sources_.begin(), Source{&scope, family, name});
        return look;
    }

    // An outline's paragraph at `level` (0-based) takes the presentation
    // style for its level ("...-outline2") in place of the first level's.
    Look AtOutlineLevel(int level) const {
        Look look = *this;
        look.outlineLevel_ = level;
        return look;
    }

    // Calls `visit` with each `properties` element ("graphic-properties",
    // "text-properties" ...) in order until it answers true.
    bool Visit(const char* properties, const std::function<bool(const tinyxml2::XMLElement*)>& visit,
               bool defaults = true) const {
        std::vector<std::string> families;
        for (const Source& source : sources_) {
            if (std::find(families.begin(), families.end(), source.family) == families.end())
                families.push_back(source.family);
            std::string name = source.name;
            bool levelled = false;
            for (int depth = 0; depth < 24 && !name.empty(); ++depth) {
                if (outlineLevel_ > 0 && !levelled && name.size() > 9 &&
                    name.compare(name.size() - 9, 9, "-outline1") == 0) {
                    const std::string atLevel = name.substr(0, name.size() - 1) + std::to_string(outlineLevel_ + 1);
                    if (source.scope->styles.count(source.family + ":" + atLevel)) name = atLevel;
                    levelled = true;
                }
                const auto found = source.scope->styles.find(source.family + ":" + name);
                if (found == source.scope->styles.end()) break;
                for (auto* p = found->second->FirstChildElement(); p; p = p->NextSiblingElement())
                    if (Is(p, properties) && visit(p)) return true;
                name = Attr(found->second, "parent-style-name", "style");
            }
        }
        if (!defaults) return false;
        if (std::find(families.begin(), families.end(), "graphic") == families.end()) families.push_back("graphic");
        for (const std::string& family : families) {
            const auto found = context_->defaults.find(family);
            if (found == context_->defaults.end()) continue;
            for (auto* p = found->second->FirstChildElement(); p; p = p->NextSiblingElement())
                if (Is(p, properties) && visit(p)) return true;
        }
        return false;
    }

    // The first value of an attribute of `properties`; empty where none says.
    std::string Get(const char* properties, const char* attribute, const char* prefix = nullptr,
                    bool defaults = true) const {
        std::string value;
        Visit(properties, [&](const tinyxml2::XMLElement* p) {
            const char* v = AttrPtr(p, attribute, prefix);
            if (!v) return false;
            value = v;
            return true;
        }, defaults);
        return value;
    }

    // The list style the shape's own styles give its text: a style's
    // style:list-style-name, or a text:list-style inside its graphic properties.
    const tinyxml2::XMLElement* ListStyle() const {
        for (const Source& source : sources_) {
            std::string name = source.name;
            for (int depth = 0; depth < 24 && !name.empty(); ++depth) {
                const auto found = source.scope->styles.find(source.family + ":" + name);
                if (found == source.scope->styles.end()) break;
                const std::string listName = Attr(found->second, "list-style-name", "style");
                if (!listName.empty()) {
                    const auto list = source.scope->lists.find(listName);
                    if (list != source.scope->lists.end()) return list->second;
                }
                if (const tinyxml2::XMLElement* inner = Child(Child(found->second, "graphic-properties"), "list-style"))
                    return inner;
                name = Attr(found->second, "parent-style-name", "style");
            }
        }
        return nullptr;
    }

    const OdpContext& Context() const { return *context_; }

private:
    struct Source {
        const StyleScope* scope;
        std::string family, name;
    };
    const OdpContext* context_;
    std::vector<Source> sources_;
    int outlineLevel_ = 0;
};

// ----- Geometry -----

// An ODF shape type (draw:type) as the DrawingML preset it draws; empty for
// one with none.
std::string PresetOfOdfType(const std::string& type) {
    static const std::map<std::string, std::string> kTypes = {
        {"rectangle", "rect"}, {"round-rectangle", "roundRect"}, {"ellipse", "ellipse"}, {"circle", "ellipse"},
        {"diamond", "diamond"}, {"isosceles-triangle", "triangle"}, {"right-triangle", "rtTriangle"},
        {"parallelogram", "parallelogram"}, {"trapezoid", "trapezoid"}, {"pentagon", "pentagon"},
        {"hexagon", "hexagon"}, {"octagon", "octagon"}, {"cross", "plus"}, {"star4", "star4"}, {"star5", "star5"},
        {"star6", "star6"}, {"star8", "star8"}, {"star12", "star12"}, {"star24", "star24"},
        {"right-arrow", "rightArrow"}, {"left-arrow", "leftArrow"}, {"up-arrow", "upArrow"},
        {"down-arrow", "downArrow"}, {"left-right-arrow", "leftRightArrow"}, {"up-down-arrow", "upDownArrow"},
        {"notched-right-arrow", "notchedRightArrow"}, {"pentagon-right", "homePlate"}, {"chevron", "chevron"},
        {"can", "can"}, {"cube", "cube"}, {"frame", "frame"}, {"paper", "foldedCorner"}, {"quad-bevel", "bevel"},
        {"rectangular-callout", "wedgeRectCallout"}, {"round-rectangular-callout", "wedgeRoundRectCallout"},
        {"round-callout", "wedgeEllipseCallout"}, {"flowchart-process", "flowChartProcess"},
        {"flowchart-alternate-process", "flowChartAlternateProcess"}, {"flowchart-decision", "flowChartDecision"},
        {"flowchart-data", "flowChartInputOutput"}, {"flowchart-predefined-process", "flowChartPredefinedProcess"},
        {"flowchart-internal-storage", "flowChartInternalStorage"}, {"flowchart-document", "flowChartDocument"},
        {"flowchart-multidocument", "flowChartMultidocument"}, {"flowchart-terminator", "flowChartTerminator"},
        {"flowchart-preparation", "flowChartPreparation"}, {"flowchart-manual-input", "flowChartManualInput"},
        {"flowchart-manual-operation", "flowChartManualOperation"}, {"flowchart-connector", "flowChartConnector"},
        {"flowchart-off-page-connector", "flowChartOffpageConnector"}, {"flowchart-card", "flowChartPunchedCard"},
        {"flowchart-extract", "flowChartExtract"}, {"flowchart-merge", "flowChartMerge"}};
    const auto found = kTypes.find(type);
    return found == kTypes.end() ? std::string() : found->second;
}

bool IsKnownPreset(const std::string& preset) {
    RichShape probe;
    return probe.ApplyPreset(preset);
}

// Splits a path or point list into its commands (letters) and numbers.
std::vector<std::string> PathTokens(const std::string& d) {
    std::vector<std::string> tokens;
    size_t i = 0;
    while (i < d.size()) {
        const char c = d[i];
        if (std::isspace(static_cast<unsigned char>(c)) || c == ',') {
            ++i;
        } else if (std::isalpha(static_cast<unsigned char>(c)) && c != 'e' && c != 'E') {
            tokens.emplace_back(1, c);
            ++i;
        } else if (c == '?' || c == '$') {
            // A formula's or a modifier's value: an outline this reader cannot work out.
            size_t end = i + 1;
            while (end < d.size() && !std::isspace(static_cast<unsigned char>(d[end])) && d[end] != ',') ++end;
            tokens.push_back(d.substr(i, end - i));
            i = end;
        } else {
            size_t end = i;
            bool dot = false;
            if (d[end] == '-' || d[end] == '+') ++end;
            while (end < d.size()) {
                const char e = d[end];
                if (std::isdigit(static_cast<unsigned char>(e))) {
                    ++end;
                } else if (e == '.' && !dot) {
                    dot = true;
                    ++end;
                } else if ((e == 'e' || e == 'E') && end + 1 < d.size() &&
                           (std::isdigit(static_cast<unsigned char>(d[end + 1])) || d[end + 1] == '-')) {
                    end += 2;
                } else {
                    break;
                }
            }
            if (end == i) {
                ++i;   // a character nothing reads
                continue;
            }
            tokens.push_back(d.substr(i, end - i));
            i = end;
        }
    }
    return tokens;
}

bool IsNumberToken(const std::string& token) {
    return !token.empty() && (std::isdigit(static_cast<unsigned char>(token[0])) || token[0] == '-' ||
                              token[0] == '+' || token[0] == '.');
}

// A path in its own units, absolute; `closed` says whether it ends closed.
struct RawPath {
    std::vector<RichShape::PathPoint> points;
    bool closed = false, noFill = false;
};

// An enhanced-geometry path (draw:enhanced-path): M, L, C, Q, Z and the
// flags N, F (not filled) and S (not stroked), in plain numbers. False for
// one this reader cannot draw (formulas, arcs).
bool ParseEnhancedPath(const std::string& d, RawPath& out) {
    using Op = RichShape::PathPoint::Op;
    const std::vector<std::string> tokens = PathTokens(d);
    char command = 0;
    float lastX = 0, lastY = 0;
    for (size_t i = 0; i < tokens.size();) {
        const std::string& t = tokens[i];
        if (!IsNumberToken(t)) {
            if (t.size() != 1) return false;   // ?f1, $0
            command = t[0];
            ++i;
            switch (command) {
                case 'Z':
                    out.points.push_back({Op::Close});
                    out.closed = true;
                    continue;
                case 'N': continue;
                case 'F': out.noFill = true; continue;
                case 'S': continue;
                case 'M': case 'L': case 'C': case 'Q': continue;
                default: return false;   // arcs, angle ellipses, quadrant lines
            }
        }
        auto take = [&](int count, float* values) {
            if (i + static_cast<size_t>(count) > tokens.size()) return false;
            for (int k = 0; k < count; ++k) {
                double v = 0;
                if (!IsNumberToken(tokens[i + k]) || !TryParseFloat(tokens[i + k], v)) return false;
                values[k] = static_cast<float>(v);
            }
            i += static_cast<size_t>(count);
            return true;
        };
        float v[6];
        RichShape::PathPoint point;
        if (command == 'M' || command == 'L') {
            if (!take(2, v)) return false;
            point.op = command == 'M' ? Op::Move : Op::Line;
            point.x = v[0];
            point.y = v[1];
            if (command == 'M') command = 'L';   // further pairs draw lines
        } else if (command == 'C') {
            if (!take(6, v)) return false;
            point.op = Op::Curve;
            point.c1x = v[0]; point.c1y = v[1]; point.c2x = v[2]; point.c2y = v[3]; point.x = v[4]; point.y = v[5];
        } else if (command == 'Q') {
            if (!take(4, v)) return false;
            point.op = Op::Curve;   // a quadratic curve as the cubic it is
            point.c1x = lastX + 2.0f / 3.0f * (v[0] - lastX);
            point.c1y = lastY + 2.0f / 3.0f * (v[1] - lastY);
            point.c2x = v[2] + 2.0f / 3.0f * (v[0] - v[2]);
            point.c2y = v[3] + 2.0f / 3.0f * (v[1] - v[3]);
            point.x = v[2];
            point.y = v[3];
        } else {
            return false;
        }
        lastX = point.x;
        lastY = point.y;
        out.points.push_back(point);
    }
    return !out.points.empty();
}

// An SVG path (svg:d): absolute and relative moves, lines, curves (smooth
// and quadratic too) and closes; an arc is drawn as a line to its end.
bool ParseSvgPath(const std::string& d, RawPath& out) {
    using Op = RichShape::PathPoint::Op;
    const std::vector<std::string> tokens = PathTokens(d);
    char command = 0;
    double x = 0, y = 0, startX = 0, startY = 0, lastC2x = 0, lastC2y = 0, lastQx = 0, lastQy = 0;
    char previous = 0;
    for (size_t i = 0; i < tokens.size();) {
        if (!IsNumberToken(tokens[i])) {
            if (tokens[i].size() != 1) return false;
            command = tokens[i][0];
            ++i;
            if (command == 'Z' || command == 'z') {
                out.points.push_back({Op::Close});
                out.closed = true;
                x = startX;
                y = startY;
                previous = 'Z';
            }
            continue;
        }
        auto take = [&](int count, double* values) {
            if (i + static_cast<size_t>(count) > tokens.size()) return false;
            for (int k = 0; k < count; ++k)
                if (!IsNumberToken(tokens[i + k]) || !TryParseFloat(tokens[i + k], values[k])) return false;
            i += static_cast<size_t>(count);
            return true;
        };
        const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
        const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));
        double v[7];
        RichShape::PathPoint point;
        auto put = [&](double px, double py) {
            point.x = static_cast<float>(px);
            point.y = static_cast<float>(py);
        };
        switch (upper) {
            case 'M':
            case 'L':
                if (!take(2, v)) return false;
                if (relative) { v[0] += x; v[1] += y; }
                point.op = upper == 'M' ? Op::Move : Op::Line;
                put(v[0], v[1]);
                if (upper == 'M') {
                    startX = v[0];
                    startY = v[1];
                    command = relative ? 'l' : 'L';
                }
                x = v[0]; y = v[1];
                break;
            case 'H':
                if (!take(1, v)) return false;
                x = relative ? x + v[0] : v[0];
                point.op = Op::Line;
                put(x, y);
                break;
            case 'V':
                if (!take(1, v)) return false;
                y = relative ? y + v[0] : v[0];
                point.op = Op::Line;
                put(x, y);
                break;
            case 'C':
            case 'S': {
                double c1x, c1y;
                if (upper == 'C') {
                    if (!take(6, v)) return false;
                    if (relative) for (int k = 0; k < 6; k += 2) { v[k] += x; v[k + 1] += y; }
                    c1x = v[0]; c1y = v[1];
                } else {
                    if (!take(4, v)) return false;
                    if (relative) for (int k = 0; k < 4; k += 2) { v[k] += x; v[k + 1] += y; }
                    const bool smooth = previous == 'C' || previous == 'S';
                    c1x = smooth ? 2 * x - lastC2x : x;
                    c1y = smooth ? 2 * y - lastC2y : y;
                    v[4] = v[2]; v[5] = v[3]; v[2] = v[0]; v[3] = v[1];
                }
                point.op = Op::Curve;
                point.c1x = static_cast<float>(c1x);
                point.c1y = static_cast<float>(c1y);
                point.c2x = static_cast<float>(v[2]);
                point.c2y = static_cast<float>(v[3]);
                put(v[4], v[5]);
                lastC2x = v[2]; lastC2y = v[3];
                x = v[4]; y = v[5];
                break;
            }
            case 'Q':
            case 'T': {
                double qx, qy;
                if (upper == 'Q') {
                    if (!take(4, v)) return false;
                    if (relative) for (int k = 0; k < 4; k += 2) { v[k] += x; v[k + 1] += y; }
                    qx = v[0]; qy = v[1];
                    v[0] = v[2]; v[1] = v[3];
                } else {
                    if (!take(2, v)) return false;
                    if (relative) { v[0] += x; v[1] += y; }
                    const bool smooth = previous == 'Q' || previous == 'T';
                    qx = smooth ? 2 * x - lastQx : x;
                    qy = smooth ? 2 * y - lastQy : y;
                }
                point.op = Op::Curve;
                point.c1x = static_cast<float>(x + 2.0 / 3.0 * (qx - x));
                point.c1y = static_cast<float>(y + 2.0 / 3.0 * (qy - y));
                point.c2x = static_cast<float>(v[0] + 2.0 / 3.0 * (qx - v[0]));
                point.c2y = static_cast<float>(v[1] + 2.0 / 3.0 * (qy - v[1]));
                put(v[0], v[1]);
                lastQx = qx; lastQy = qy;
                x = v[0]; y = v[1];
                break;
            }
            case 'A':
                if (!take(7, v)) return false;
                if (relative) { v[5] += x; v[6] += y; }
                point.op = Op::Line;
                put(v[5], v[6]);
                x = v[5]; y = v[6];
                break;
            default:
                return false;
        }
        previous = upper;
        out.points.push_back(point);
    }
    return !out.points.empty();
}

// A path's points as shares (0..1) of the box `left, top, width, height`.
std::vector<RichShape::PathPoint> Normalized(const RawPath& path, double left, double top, double width,
                                             double height) {
    std::vector<RichShape::PathPoint> out = path.points;
    const double w = width > 0 ? width : 1, h = height > 0 ? height : 1;
    for (RichShape::PathPoint& p : out) {
        if (p.op == RichShape::PathPoint::Op::Close) continue;
        p.x = static_cast<float>((p.x - left) / w);
        p.y = static_cast<float>((p.y - top) / h);
        p.c1x = static_cast<float>((p.c1x - left) / w);
        p.c1y = static_cast<float>((p.c1y - top) / h);
        p.c2x = static_cast<float>((p.c2x - left) / w);
        p.c2y = static_cast<float>((p.c2y - top) / h);
    }
    return out;
}

// svg:viewBox's four numbers; false without a box of some size.
bool ViewBox(const tinyxml2::XMLElement* e, double box[4]) {
    const std::vector<std::string> tokens = PathTokens(Attr(e, "viewBox", "svg"));
    if (tokens.size() != 4) return false;
    for (int i = 0; i < 4; ++i)
        if (!TryParseFloat(tokens[i], box[i])) return false;
    return box[2] > 0 && box[3] > 0;
}

// ----- Reading the deck -----

class OdpReader {
public:
    OdpReader(const OdpContext& context, PptxPresentation& out) : context_(context), out_(out) {}

    void ReadPresentation(const tinyxml2::XMLElement* presentation) {
        std::string sizeMaster;
        int number = 0;
        std::map<std::string, int> pageNumbers;   // a page's name -> its number, for links to it
        for (auto* page = Child(presentation, "page"); page; page = page->NextSiblingElement()) {
            if (!Is(page, "page")) continue;
            const std::string masterName = Attr(page, "master-page-name", "draw");
            if (sizeMaster.empty()) sizeMaster = masterName;
            PptxSlide slide;
            slide.number = ++number;
            pageNumbers.emplace(Attr(page, "name", "draw"), number);
            const Look pageLook = Look(context_).With(context_.content, "drawing-page", Attr(page, "style-name", "draw"));
            slide.hidden = pageLook.Get("drawing-page-properties", "visibility", "presentation") == "hidden";
            slide.background = Background(pageLook);
            if (slide.background.empty()) {
                const auto master = context_.masters.find(masterName.empty() ? context_.firstMaster : masterName);
                if (master != context_.masters.end())
                    slide.background =
                        Background(Look(context_).With(context_.master, "drawing-page", master->second.pageStyle));
            }
            ReadShapes(page, slide.shapes);
            for (const PptxShape& shape : slide.shapes) {
                if (shape.kind != PptxShapeKind::Title || !slide.title.empty()) continue;
                for (const PptxParagraph& p : shape.paragraphs) {
                    const std::string text = p.Text();
                    if (text.empty()) continue;
                    slide.title += (slide.title.empty() ? "" : " ") + text;
                }
            }
            // The speaker notes: the notes frame of the page's notes page.
            if (const tinyxml2::XMLElement* notes = Child(page, "notes")) {
                std::vector<PptxShape> shapes;
                ReadShapes(notes, shapes);
                for (PptxShape& shape : shapes)
                    if (shape.kind == PptxShapeKind::Body && shape.name == kNotesFrame)
                        for (PptxParagraph& p : shape.paragraphs) slide.notes.push_back(std::move(p));
            }
            out_.slides.push_back(std::move(slide));
        }
        // The slide size: the page layout of the first slide's master page.
        auto master = context_.masters.find(sizeMaster);
        if (master == context_.masters.end()) master = context_.masters.find(context_.firstMaster);
        if (master != context_.masters.end()) {
            const auto layout = context_.pageLayouts.find(master->second.pageLayout);
            if (layout != context_.pageLayouts.end()) {
                out_.slideWidth = layout->second.first;
                out_.slideHeight = layout->second.second;
            }
        }
        out_.themeColors = context_.themeColors;
        // Links to a page by its name ("#Slide 3", the writer's "#page3") as jumps to its slide.
        auto relink = [&](std::vector<PptxParagraph>& paragraphs) {
            for (PptxParagraph& p : paragraphs)
                for (PptxRun& run : p.runs) {
                    if (run.hyperlink.size() < 2 || run.hyperlink[0] != '#') continue;
                    const auto found = pageNumbers.find(run.hyperlink.substr(1));
                    if (found != pageNumbers.end()) run.hyperlink = "#slide" + std::to_string(found->second);
                }
        };
        for (PptxSlide& slide : out_.slides)
            for (PptxShape& shape : slide.shapes) {
                relink(shape.paragraphs);
                for (auto& row : shape.rows)
                    for (auto& cell : row) relink(cell);
            }
    }

private:
    // A notes frame is told apart from the rest of a notes page by this
    // name while the shapes are read; the slide gets only its paragraphs.
    static constexpr const char* kNotesFrame = "\x01notes";

    const OdpContext& context_;
    PptxPresentation& out_;
    int inlinePictures_ = 0;

    // ---- fills, outlines, the text area ----

    // A drawing page's fill: solid, or a gradient's first colour.
    std::string Background(const Look& look) const {
        const std::string fill = look.Get("drawing-page-properties", "fill", "draw", false);
        if (fill == "solid") return Hex(look.Get("drawing-page-properties", "fill-color", "draw", false));
        if (fill == "gradient") {
            const auto g = context_.gradients.find(look.Get("drawing-page-properties", "fill-gradient-name", "draw", false));
            if (g != context_.gradients.end()) return Hex(Attr(g->second, "start-color", "draw"));
        }
        if (fill == "none") return "FFFFFF";
        return "";
    }

    // An ODF angle ("150deg", "2.6rad", "100grad", or LibreOffice's bare tenths of a degree) in degrees.
    static double Degrees(const std::string& value) {
        double number = 0;
        if (!Number(value, number)) return 0;
        if (value.find("rad") != std::string::npos) return number * 180.0 / kPi;
        if (value.find("grad") != std::string::npos) return number * 0.9;
        if (value.find("deg") != std::string::npos) return number;
        return number / 10.0;
    }

    void ReadGradient(const std::string& name, PptxShape& shape) const {
        const auto found = context_.gradients.find(name);
        if (found == context_.gradients.end()) return;
        const tinyxml2::XMLElement* g = found->second;
        const std::string style = Attr(g, "style", "draw");
        double startIntensity = 100, endIntensity = 100;
        Number(Attr(g, "start-intensity", "draw"), startIntensity);
        Number(Attr(g, "end-intensity", "draw"), endIntensity);
        std::vector<RichShape::GradientStop> stops;
        for (auto* s = Child(g, "gradient-stop"); s; s = s->NextSiblingElement()) {
            if (!Is(s, "gradient-stop")) continue;
            double offset = 0;
            Number(Attr(s, "offset", "svg"), offset);
            const std::string hex = Hex(Attr(s, "color-value"));
            if (!hex.empty()) stops.push_back({static_cast<float>(std::clamp(offset, 0.0, 1.0)), hex});
        }
        if (stops.size() < 2) {
            stops.clear();
            const std::string from = Dimmed(Hex(Attr(g, "start-color", "draw")), startIntensity);
            const std::string to = Dimmed(Hex(Attr(g, "end-color", "draw")), endIntensity);
            if (from.empty() || to.empty()) return;
            stops = {{0.0f, from}, {1.0f, to}};
        }
        if (style == "axial") {
            // The start colour at both edges, the end colour along the middle.
            std::vector<RichShape::GradientStop> mirrored;
            for (const auto& s : stops) mirrored.push_back({s.position / 2.0f, s.color});
            for (auto it = stops.rbegin(); it != stops.rend(); ++it)
                if (it->position < 1.0f) mirrored.push_back({1.0f - it->position / 2.0f, it->color});
            stops = std::move(mirrored);
        } else if (style == "radial" || style == "ellipsoid" || style == "square" || style == "rectangular") {
            // ODF starts at the edge; the model at the centre.
            std::vector<RichShape::GradientStop> inward;
            for (auto it = stops.rbegin(); it != stops.rend(); ++it) inward.push_back({1.0f - it->position, it->color});
            stops = std::move(inward);
            shape.gradientRadial = true;
        }
        shape.gradientStops = stops;
        shape.fill = stops.front().color;
        // ODF turns counter-clockwise from top-to-bottom, on the page (an
        // Office deck's turn is added to it when LibreOffice reads it);
        // DrawingML clockwise from left-to-right, in the shape.
        double angle = 90.0 - Degrees(Attr(g, "angle", "draw")) - shape.rotationDeg;
        if (shape.flipH) angle = 180.0 - angle;
        if (shape.flipV) angle = -angle;
        shape.gradientAngleDeg = std::fmod(std::fmod(angle, 360.0) + 360.0, 360.0);
    }

    // A dash's look: dots (no longer than the line is wide) or dashes.
    RichShape::Dash DashOf(const std::string& name) const {
        const auto found = context_.dashes.find(name);
        if (found == context_.dashes.end()) return RichShape::Dash::Dashed;
        auto shortDot = [](const std::string& length) {
            if (length.empty()) return true;
            double value = 0;
            if (IsPercent(length)) return Number(length, value) && value <= 100.0;
            return LengthToEmu(length) <= 36000;   // up to a millimetre
        };
        const tinyxml2::XMLElement* d = found->second;
        const bool dots = shortDot(Attr(d, "dots1-length", "draw")) &&
                          (!AttrPtr(d, "dots2", "draw") || shortDot(Attr(d, "dots2-length", "draw")));
        return dots ? RichShape::Dash::Dotted : RichShape::Dash::Dashed;
    }

    static int64_t Padding(const Look& look, const char* properties, const char* side) {
        std::string value = look.Get(properties, (std::string("padding-") + side).c_str(), "fo");
        if (value.empty()) value = look.Get(properties, "padding", "fo");
        return value.empty() ? -1 : LengthToEmu(value);
    }

    // The fill, outline and (with `text`) the text area a shape's styles give it.
    void ReadGraphic(const Look& look, PptxShape& shape, bool text) const {
        const std::string fill = look.Get("graphic-properties", "fill", "draw");
        if (fill == "solid") {
            shape.fill = Hex(look.Get("graphic-properties", "fill-color", "draw"));
        } else if (fill == "gradient") {
            ReadGradient(look.Get("graphic-properties", "fill-gradient-name", "draw"), shape);
        } else if (fill == "hatch" && look.Get("graphic-properties", "fill-hatch-solid", "draw") == "true") {
            shape.fill = Hex(look.Get("graphic-properties", "fill-color", "draw"));
        }
        const std::string stroke = look.Get("graphic-properties", "stroke", "draw");
        if (stroke == "solid" || stroke == "dash") {
            shape.line = Hex(look.Get("graphic-properties", "stroke-color", "svg"));
            if (shape.line.empty()) shape.line = "000000";
            shape.lineWidthPt = static_cast<double>(LengthToEmu(look.Get("graphic-properties", "stroke-width", "svg"))) /
                                kEmuPerPoint;
            if (stroke == "dash") shape.dash = DashOf(look.Get("graphic-properties", "stroke-dash", "draw"));
            shape.arrowStart = !look.Get("graphic-properties", "marker-start", "draw").empty();
            shape.arrowEnd = !look.Get("graphic-properties", "marker-end", "draw").empty();
        }
        if (!text) return;
        const std::string anchor = look.Get("graphic-properties", "textarea-vertical-align", "draw");
        shape.anchor = anchor == "middle" ? PptxAnchor::Middle : anchor == "bottom" ? PptxAnchor::Bottom : PptxAnchor::Top;
        shape.insetLeft = Padding(look, "graphic-properties", "left");
        shape.insetRight = Padding(look, "graphic-properties", "right");
        shape.insetTop = Padding(look, "graphic-properties", "top");
        shape.insetBottom = Padding(look, "graphic-properties", "bottom");
        // The family default is LibreOffice's for drawings, not a text box's.
        shape.wrap = look.Get("graphic-properties", "wrap-option", "fo", false) != "no-wrap";
        // Text shrunk to fit its box: a scale just under 1, so it is drawn
        // smaller where it does not fit.
        if (shape.fontScale >= 1.0f && (look.Get("graphic-properties", "shrink-to-fit", "style") == "true" ||
                                        look.Get("graphic-properties", "fit-to-size", "draw") == "shrink-to-fit"))
            shape.fontScale = 0.995f;
        const std::string mode = look.Get("graphic-properties", "writing-mode");
        if (mode == "tb-rl" || mode == "tb") shape.textRotation = 90;
        else if (mode == "bt-lr") shape.textRotation = 270;
    }

    // ---- places ----

    // svg:x / y / width / height, turned by draw:transform: ODF turns the box
    // about its top-left corner (counter-clockwise) and then moves that
    // corner, so the box's centre is worked out from the turn.
    static void ReadPlace(const tinyxml2::XMLElement* e, PptxShape& shape) {
        const int64_t w = LengthToEmu(Attr(e, "width", "svg")), h = LengthToEmu(Attr(e, "height", "svg"));
        shape.width = w;
        shape.height = h;
        double cornerX = static_cast<double>(LengthToEmu(Attr(e, "x", "svg")));
        double cornerY = static_cast<double>(LengthToEmu(Attr(e, "y", "svg")));
        double turn = 0;   // radians, counter-clockwise on the page
        const std::string transform = Attr(e, "transform", "draw");
        size_t at = 0;
        while (at < transform.size()) {
            const size_t open = transform.find('(', at);
            const size_t close = open == std::string::npos ? open : transform.find(')', open);
            if (close == std::string::npos) break;
            std::string name = transform.substr(at, open - at);
            name.erase(std::remove_if(name.begin(), name.end(), [](unsigned char c) { return std::isspace(c); }), name.end());
            std::vector<std::string> values;   // the numbers with their units again
            {
                const std::string inner = transform.substr(open + 1, close - open - 1);
                size_t i = 0;
                while (i < inner.size()) {
                    while (i < inner.size() && (std::isspace(static_cast<unsigned char>(inner[i])) || inner[i] == ',')) ++i;
                    size_t end = i;
                    while (end < inner.size() && !std::isspace(static_cast<unsigned char>(inner[end])) && inner[end] != ',') ++end;
                    if (end > i) values.push_back(inner.substr(i, end - i));
                    i = end;
                }
            }
            if (name == "rotate" && !values.empty()) {
                double r = 0;
                if (Number(values[0], r)) turn += r;
            } else if (name == "translate" && !values.empty()) {
                cornerX += static_cast<double>(LengthToEmu(values[0]));
                if (values.size() > 1) cornerY += static_cast<double>(LengthToEmu(values[1]));
            } else if (name == "matrix" && values.size() == 6) {
                double a = 1, b = 0;
                Number(values[0], a);
                Number(values[1], b);
                turn += std::atan2(-b, a);
                cornerX += static_cast<double>(LengthToEmu(values[4]));
                cornerY += static_cast<double>(LengthToEmu(values[5]));
            }
            at = close + 1;
        }
        const double a = -turn;   // clockwise
        const double cx = cornerX + w / 2.0 * std::cos(a) - h / 2.0 * std::sin(a);
        const double cy = cornerY + w / 2.0 * std::sin(a) + h / 2.0 * std::cos(a);
        shape.x = static_cast<int64_t>(std::llround(cx - w / 2.0));
        shape.y = static_cast<int64_t>(std::llround(cy - h / 2.0));
        double degrees = std::fmod(a * 180.0 / kPi, 360.0);
        if (degrees < 0) degrees += 360.0;
        if (std::abs(degrees) < 0.01 || std::abs(degrees - 360.0) < 0.01) degrees = 0;
        shape.rotationDeg = degrees;
    }

    // ODF mirrors a shape after turning it; PowerPoint (and the model)
    // before: a mirror in one direction turns the other way.
    static void Mirror(PptxShape& shape, bool horizontal, bool vertical) {
        shape.flipH = horizontal;
        shape.flipV = vertical;
        if (horizontal != vertical && shape.rotationDeg != 0) shape.rotationDeg = 360.0 - shape.rotationDeg;
    }

    static bool HasText(const PptxShape& shape) {
        for (const PptxParagraph& p : shape.paragraphs)
            if (!p.Text().empty()) return true;
        return false;
    }

    static bool Visible(const PptxShape& shape) {
        return HasText(shape) || !shape.fill.empty() || !shape.line.empty();
    }

    // The look of a shape's own styles (draw:style-name, presentation:style-name).
    Look ShapeLook(const tinyxml2::XMLElement* e) const {
        return Look(context_)
            .With(context_.content, "presentation", Attr(e, "style-name", "presentation"))
            .With(context_.content, "graphic", Attr(e, "style-name", "draw"));
    }

    void ReadShapes(const tinyxml2::XMLElement* parent, std::vector<PptxShape>& out) {
        for (auto* e = parent ? parent->FirstChildElement() : nullptr; e; e = e->NextSiblingElement()) {
            if (Is(e, "g") || Is(e, "a")) {
                ReadShapes(e, out);   // a group, or shapes behind a hyperlink
            } else if (Is(e, "frame")) {
                ReadFrame(e, out);
            } else if (Is(e, "line") || Is(e, "connector")) {
                ReadLine(e, out);
            } else if (Is(e, "custom-shape") || Is(e, "rect") || Is(e, "ellipse") || Is(e, "circle") ||
                       Is(e, "polygon") || Is(e, "polyline") || Is(e, "path") || Is(e, "regular-polygon") ||
                       Is(e, "caption") || Is(e, "measure")) {
                ReadDrawn(e, out);
            }
        }
    }

    // A shape drawn in an outline of its own, with its text.
    void ReadDrawn(const tinyxml2::XMLElement* e, std::vector<PptxShape>& out) {
        PptxShape shape;
        shape.kind = PptxShapeKind::Text;
        shape.name = Attr(e, "name", "draw");
        ReadPlace(e, shape);
        bool noFill = false;
        if (Is(e, "custom-shape")) {
            const tinyxml2::XMLElement* geometry = Child(e, "enhanced-geometry");
            Mirror(shape, Attr(geometry, "mirror-horizontal", "draw") == "true",
                   Attr(geometry, "mirror-vertical", "draw") == "true");
            std::string type = Attr(geometry, "type", "draw");
            std::string preset;
            if (type.rfind("ooxml-", 0) == 0) preset = type.substr(6);   // LibreOffice's name for an Office preset
            else preset = PresetOfOdfType(type);
            if (preset == "non-primitive") preset.clear();
            if (!preset.empty() && IsKnownPreset(preset)) {
                shape.geometry = preset == "rect" ? std::string() : preset;
            } else {
                RawPath path;
                double box[4] = {0, 0, 0, 0};
                bool sized = ViewBox(geometry, box);
                if (!sized) {
                    // LibreOffice's Office freeform: its first sub-path's size.
                    const std::vector<std::string> size = PathTokens(Attr(geometry, "sub-view-size"));
                    sized = size.size() >= 2 && TryParseFloat(size[0], box[2]) && TryParseFloat(size[1], box[3]) &&
                            box[2] > 0 && box[3] > 0;
                }
                if (sized && ParseEnhancedPath(Attr(geometry, "enhanced-path", "draw"), path)) {
                    shape.customPath = Normalized(path, box[0], box[1], box[2], box[3]);
                    noFill = path.noFill || !path.closed;
                } else if (!preset.empty()) {
                    shape.geometry = preset;   // one the renderer draws as a box
                }
            }
            // Vertical text: the text area turned (counter-clockwise).
            const std::string turn = Attr(geometry, "text-rotate-angle", "draw");
            if (turn == "270" || turn == "-90") shape.textRotation = 90;
            else if (turn == "90") shape.textRotation = 270;
        } else if (Is(e, "rect")) {
            if (LengthToEmu(Attr(e, "corner-radius", "draw")) > 0) shape.geometry = "roundRect";
        } else if (Is(e, "ellipse") || Is(e, "circle")) {
            shape.geometry = "ellipse";
        } else if (Is(e, "polygon") || Is(e, "polyline") || Is(e, "path")) {
            RawPath path;
            double box[4] = {0, 0, 0, 0};
            if (!ViewBox(e, box)) return;
            if (Is(e, "path")) {
                if (!ParseSvgPath(Attr(e, "d", "svg"), path)) return;
            } else {
                const std::vector<std::string> numbers = PathTokens(Attr(e, "points", "draw"));
                for (size_t i = 0; i + 1 < numbers.size(); i += 2) {
                    double px = 0, py = 0;
                    if (!TryParseFloat(numbers[i], px) || !TryParseFloat(numbers[i + 1], py)) return;
                    RichShape::PathPoint point;
                    point.op = path.points.empty() ? RichShape::PathPoint::Op::Move : RichShape::PathPoint::Op::Line;
                    point.x = static_cast<float>(px);
                    point.y = static_cast<float>(py);
                    path.points.push_back(point);
                }
                if (path.points.size() < 2) return;
                if (Is(e, "polygon")) {
                    path.points.push_back({RichShape::PathPoint::Op::Close});
                    path.closed = true;
                }
            }
            shape.customPath = Normalized(path, box[0], box[1], box[2], box[3]);
            // An open one is a line.
            if (!path.closed) shape.kind = PptxShapeKind::Connector;
        } else if (Is(e, "regular-polygon")) {
            double cornerCount = 3, sharpness = 0;
            Number(Attr(e, "corners", "draw"), cornerCount);
            const int corners = std::clamp(static_cast<int>(cornerCount), 3, 64);
            const bool concave = Attr(e, "concave", "draw") == "true";
            Number(Attr(e, "sharpness", "draw"), sharpness);
            const int points = concave ? corners * 2 : corners;
            for (int i = 0; i < points; ++i) {
                const double angle = -kPi / 2 + 2 * kPi * i / points;
                const double reach = concave && i % 2 ? 1.0 - std::clamp(sharpness, 0.0, 100.0) / 100.0 : 1.0;
                RichShape::PathPoint point;
                point.op = i == 0 ? RichShape::PathPoint::Op::Move : RichShape::PathPoint::Op::Line;
                point.x = static_cast<float>(0.5 + 0.5 * reach * std::cos(angle));
                point.y = static_cast<float>(0.5 + 0.5 * reach * std::sin(angle));
                shape.customPath.push_back(point);
            }
            shape.customPath.push_back({RichShape::PathPoint::Op::Close});
        }
        const Look look = ShapeLook(e);
        ReadGraphic(look, shape, shape.kind != PptxShapeKind::Connector);
        if (noFill) {
            shape.fill.clear();
            shape.gradientStops.clear();
        }
        if (shape.kind == PptxShapeKind::Connector) {
            if (shape.line.empty()) return;
        } else {
            ReadParagraphs(e, TextLook(e, look), shape.paragraphs);
            if (!Visible(shape)) return;
        }
        out.push_back(std::move(shape));
    }

    // A line, or a connector between two shapes: straight from its start
    // to its end, or along its path.
    void ReadLine(const tinyxml2::XMLElement* e, std::vector<PptxShape>& out) {
        PptxShape shape;
        shape.kind = PptxShapeKind::Connector;
        shape.name = Attr(e, "name", "draw");
        const double x1 = static_cast<double>(LengthToEmu(Attr(e, "x1", "svg")));
        const double y1 = static_cast<double>(LengthToEmu(Attr(e, "y1", "svg")));
        const double x2 = static_cast<double>(LengthToEmu(Attr(e, "x2", "svg")));
        const double y2 = static_cast<double>(LengthToEmu(Attr(e, "y2", "svg")));
        RawPath path;
        const std::string type = Attr(e, "type", "draw");
        if (Is(e, "connector") && type != "line" && ParseSvgPath(Attr(e, "d", "svg"), path) && path.points.size() > 2) {
            // LibreOffice's path runs on the page, in 1/100 mm.
            double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
            for (RichShape::PathPoint& p : path.points) {
                if (p.op == RichShape::PathPoint::Op::Close) continue;
                for (float* v : {&p.x, &p.c1x, &p.c2x}) *v *= 360.0f;
                for (float* v : {&p.y, &p.c1y, &p.c2y}) *v *= 360.0f;
                minX = std::min(minX, static_cast<double>(p.x));
                minY = std::min(minY, static_cast<double>(p.y));
                maxX = std::max(maxX, static_cast<double>(p.x));
                maxY = std::max(maxY, static_cast<double>(p.y));
            }
            shape.x = static_cast<int64_t>(minX);
            shape.y = static_cast<int64_t>(minY);
            shape.width = static_cast<int64_t>(std::max(1.0, maxX - minX));
            shape.height = static_cast<int64_t>(std::max(1.0, maxY - minY));
            shape.customPath = Normalized(path, minX, minY, static_cast<double>(shape.width),
                                          static_cast<double>(shape.height));
        } else {
            shape.x = static_cast<int64_t>(std::min(x1, x2));
            shape.y = static_cast<int64_t>(std::min(y1, y2));
            shape.width = static_cast<int64_t>(std::abs(x2 - x1));
            shape.height = static_cast<int64_t>(std::abs(y2 - y1));
            shape.flipH = x2 < x1;
            shape.flipV = y2 < y1;
            // A standard connector stored with only its ends: LibreOffice
            // routes it across, down and across again.
            if (Is(e, "connector") && (type.empty() || type == "standard") && shape.width > 0 && shape.height > 0)
                shape.geometry = "bentConnector3";
        }
        ReadGraphic(ShapeLook(e), shape, false);
        if (shape.line.empty()) return;
        out.push_back(std::move(shape));
    }

    // The look a shape's text starts from: its text style
    // (draw:text-style-name) over its own styles.
    Look TextLook(const tinyxml2::XMLElement* e, const Look& shapeLook) const {
        return shapeLook.With(context_.content, "paragraph", Attr(e, "text-style-name", "draw"));
    }

    void ReadFrame(const tinyxml2::XMLElement* frame, std::vector<PptxShape>& out) {
        const std::string presentationClass = Attr(frame, "class", "presentation");
        // Slide furniture, a notes page's slide picture, and a placeholder
        // still showing its prompt.
        if (presentationClass == "header" || presentationClass == "footer" ||
            presentationClass == "date-time" || presentationClass == "page-number" ||
            presentationClass == "page" || presentationClass == "handout" ||
            Attr(frame, "placeholder", "presentation") == "true")
            return;

        PptxShape shape;
        shape.name = Attr(frame, "name", "draw");
        ReadPlace(frame, shape);
        const Look look = ShapeLook(frame);
        if (const tinyxml2::XMLElement* textBox = Child(frame, "text-box")) {
            shape.kind = presentationClass == "title"                                  ? PptxShapeKind::Title
                       : presentationClass == "subtitle"                               ? PptxShapeKind::Subtitle
                       : presentationClass == "outline" || presentationClass == "notes" ? PptxShapeKind::Body
                                                                                        : PptxShapeKind::Text;
            if (presentationClass == "notes") shape.name = kNotesFrame;
            // A presentation frame is a placeholder (which the renderer shrinks to fit).
            if (shape.kind == PptxShapeKind::Title) shape.placeholder = "title";
            else if (shape.kind == PptxShapeKind::Subtitle) shape.placeholder = "subTitle";
            else if (presentationClass == "outline") shape.placeholder = "body";
            ReadGraphic(look, shape, true);
            ReadParagraphs(textBox, TextLook(frame, look), shape.paragraphs);
            if (shape.kind == PptxShapeKind::Text ? Visible(shape) : HasText(shape)) out.push_back(std::move(shape));
        } else if (const tinyxml2::XMLElement* table = Child(frame, "table")) {
            shape.kind = PptxShapeKind::Table;
            ReadTable(table, TextLook(frame, look), shape);
            out.push_back(std::move(shape));
        } else if (const tinyxml2::XMLElement* object = Child(frame, "object")) {
            // A chart object: the package's "Object N/content.xml", or a flat
            // file's chart document inline. Any other object is not read.
            std::string xml;
            if (const tinyxml2::XMLElement* inline_ = Child(object, "document")) {
                tinyxml2::XMLPrinter printer;
                inline_->Accept(&printer);
                xml = printer.CStr();
            } else if (context_.readEntry) {
                std::string href = Attr(object, "href", "xlink");
                if (href.rfind("./", 0) == 0) href = href.substr(2);
                if (!href.empty() && !context_.readEntry(href + "/content.xml", xml)) xml.clear();
            }
            if (!xml.empty() && ReadOdfChartObject(xml, shape.chart)) {
                shape.kind = PptxShapeKind::Chart;
                out.push_back(std::move(shape));
            }
        } else if (Child(frame, "object-ole") || Child(frame, "plugin") || Child(frame, "applet") ||
                   Child(frame, "floating-frame")) {
            return;   // another embedded object: not read
        } else if (const tinyxml2::XMLElement* image = Child(frame, "image")) {
            shape.kind = PptxShapeKind::Picture;
            ReadGraphic(look, shape, false);
            shape.fill.clear();
            shape.gradientStops.clear();
            std::string href = Attr(image, "href", "xlink");
            if (href.rfind("./", 0) == 0) href = href.substr(2);
            shape.pictureTarget = href;
            if (href.empty()) {
                // A flat file's picture, inline: kept under a name of its own.
                if (const tinyxml2::XMLElement* data = Child(image, "binary-data")) {
                    if (context_.withPictures && data->GetText()) {
                        shape.pictureTarget = "Pictures/inline" + std::to_string(++inlinePictures_);
                        std::string base64 = data->GetText();
                        base64.erase(std::remove_if(base64.begin(), base64.end(),
                                                    [](unsigned char c) { return std::isspace(c); }),
                                     base64.end());
                        std::vector<uint8_t> bytes = Base64Decode(base64);
                        if (!bytes.empty()) out_.pictures[shape.pictureTarget] = std::move(bytes);
                    }
                }
            } else if (context_.withPictures && context_.readEntry && !out_.pictures.count(href)) {
                std::string bytes;
                if (context_.readEntry(href, bytes) && !bytes.empty())
                    out_.pictures[href] = std::vector<uint8_t>(bytes.begin(), bytes.end());
            }
            const tinyxml2::XMLElement* desc = Child(frame, "desc");
            const tinyxml2::XMLElement* title = Child(frame, "title");
            shape.altText = desc && desc->GetText() ? desc->GetText()
                          : title && title->GetText() ? title->GetText() : "";
            while (!shape.altText.empty() && std::isspace(static_cast<unsigned char>(shape.altText.back())))
                shape.altText.pop_back();
            out.push_back(std::move(shape));
        }
    }

    // ---- tables ----

    struct Column {
        int64_t width = 0;
        std::string cellStyle;   // table:default-cell-style-name
    };

    static PptxCellStyle::Border BorderOf(const std::string& value) {
        PptxCellStyle::Border border;
        if (value.empty() || value == "none" || value == "hidden") return border;
        for (const std::string& part : [&] {
                 std::vector<std::string> parts;
                 size_t i = 0;
                 while (i < value.size()) {
                     while (i < value.size() && value[i] == ' ') ++i;
                     size_t end = value.find(' ', i);
                     if (end == std::string::npos) end = value.size();
                     if (end > i) parts.push_back(value.substr(i, end - i));
                     i = end;
                 }
                 return parts;
             }()) {
            if (part[0] == '#') border.color = Hex(part);
            else if (const int64_t emu = LengthToEmu(part); emu > 0) border.widthPt = static_cast<double>(emu) / kEmuPerPoint;
            else if (part == "thin") border.widthPt = 0.5;
            else if (part == "medium") border.widthPt = 1.0;
            else if (part == "thick") border.widthPt = 1.5;
        }
        if (border.color.empty() || border.widthPt <= 0) border = {};
        return border;
    }

    static PptxCellStyle::Border CellBorder(const Look& look, const char* side) {
        const std::string attr = std::string("border-") + side;
        for (const char* properties : {"table-cell-properties", "paragraph-properties"}) {
            std::string value = look.Get(properties, attr.c_str(), "fo", false);
            if (value.empty()) value = look.Get(properties, "border", "fo", false);
            if (!value.empty()) return BorderOf(value);
        }
        return {};
    }

    // A table template's cell style for row `r`, column `c` of `rows` x `columns`.
    std::string TemplateStyle(const tinyxml2::XMLElement* table, size_t r, size_t c, size_t rows, size_t columns) const {
        const auto found = context_.templates.find(Attr(table, "template-name", "table"));
        if (found == context_.templates.end()) return "";
        auto flag = [&](const char* name) { return Attr(table, name, "table") == "true"; };
        auto styleOf = [&](const char* part) {
            return Attr(Child(found->second, part), "style-name", "table");
        };
        std::string style;
        auto pick = [&](bool on, const char* part) {
            if (on && style.empty()) style = styleOf(part);
        };
        pick(flag("use-first-row-styles") && r == 0, "first-row");
        pick(flag("use-last-row-styles") && r + 1 == rows, "last-row");
        pick(flag("use-first-column-styles") && c == 0, "first-column");
        pick(flag("use-last-column-styles") && c + 1 == columns, "last-column");
        const size_t bandRow = r - (flag("use-first-row-styles") ? 1 : 0);
        pick(flag("use-banding-rows-styles") && r >= (flag("use-first-row-styles") ? 1u : 0u),
             bandRow % 2 == 0 ? "odd-rows" : "even-rows");
        pick(flag("use-banding-columns-styles"), c % 2 == 0 ? "odd-columns" : "even-columns");
        pick(true, "body");
        return style;
    }

    void ReadColumns(const tinyxml2::XMLElement* container, std::vector<Column>& columns) const {
        for (auto* e = container->FirstChildElement(); e; e = e->NextSiblingElement()) {
            if (Is(e, "table-columns") || Is(e, "table-header-columns") || Is(e, "table-column-group")) {
                ReadColumns(e, columns);
            } else if (Is(e, "table-column")) {
                Column column;
                column.width = LengthToEmu(Look(context_)
                                               .With(context_.content, "table-column", Attr(e, "style-name", "table"))
                                               .Get("table-column-properties", "column-width", "style", false));
                column.cellStyle = Attr(e, "default-cell-style-name", "table");
                double repeat = 1;
                Number(Attr(e, "number-columns-repeated", "table"), repeat);
                for (int i = 0; i < std::clamp(static_cast<int>(repeat), 1, 1024); ++i) columns.push_back(column);
            }
        }
    }

    static void CollectRows(const tinyxml2::XMLElement* container, std::vector<const tinyxml2::XMLElement*>& rows) {
        for (auto* e = container->FirstChildElement(); e; e = e->NextSiblingElement()) {
            if (Is(e, "table-header-rows") || Is(e, "table-rows") || Is(e, "table-row-group")) CollectRows(e, rows);
            else if (Is(e, "table-row")) rows.push_back(e);
        }
    }

    void ReadTable(const tinyxml2::XMLElement* table, const Look& frameLook, PptxShape& shape) {
        std::vector<Column> columns;
        ReadColumns(table, columns);
        std::vector<const tinyxml2::XMLElement*> rows;
        CollectRows(table, rows);
        size_t width = columns.size();
        for (const tinyxml2::XMLElement* row : rows) {
            size_t cells = 0;
            for (auto* cell = row->FirstChildElement(); cell; cell = cell->NextSiblingElement())
                if (Is(cell, "table-cell") || Is(cell, "covered-table-cell")) ++cells;
            width = std::max(width, cells);
        }
        bool widths = !columns.empty();
        for (const Column& column : columns) widths = widths && column.width > 0;
        if (widths)
            for (const Column& column : columns) shape.columnWidths.push_back(column.width);
        for (size_t r = 0; r < rows.size(); ++r) {
            const tinyxml2::XMLElement* row = rows[r];
            const Look rowLook = Look(context_).With(context_.content, "table-row", Attr(row, "style-name", "table"));
            std::string height = rowLook.Get("table-row-properties", "row-height", "style", false);
            if (height.empty()) height = rowLook.Get("table-row-properties", "min-row-height", "style", false);
            shape.rowHeights.push_back(LengthToEmu(height));
            const std::string rowCellStyle = Attr(row, "default-cell-style-name", "table");
            std::vector<std::vector<PptxParagraph>> texts;
            std::vector<PptxCellStyle> looks;
            size_t c = 0;
            for (auto* cell = row->FirstChildElement(); cell; cell = cell->NextSiblingElement()) {
                const bool covered = Is(cell, "covered-table-cell");
                if (!covered && !Is(cell, "table-cell")) continue;
                std::string styleName = Attr(cell, "style-name", "table");
                if (styleName.empty()) styleName = rowCellStyle;
                if (styleName.empty() && c < columns.size()) styleName = columns[c].cellStyle;
                const std::string templateStyle = TemplateStyle(table, r, c, rows.size(), width);
                const Look cellLook = Look(context_)
                                          .With(context_.content, "table-cell", templateStyle)
                                          .With(context_.content, "table-cell", styleName);
                PptxCellStyle look;
                look.mergedAway = covered;
                double span = 1;
                if (Number(Attr(cell, "number-columns-spanned", "table"), span)) look.gridSpan = std::max(1, static_cast<int>(span));
                span = 1;
                if (Number(Attr(cell, "number-rows-spanned", "table"), span)) look.rowSpan = std::max(1, static_cast<int>(span));
                if (cellLook.Get("graphic-properties", "fill", "draw", false) == "solid")
                    look.fill = Hex(cellLook.Get("graphic-properties", "fill-color", "draw", false));
                if (look.fill.empty())
                    look.fill = Hex(cellLook.Get("table-cell-properties", "background-color", "fo", false));
                look.left = CellBorder(cellLook, "left");
                look.right = CellBorder(cellLook, "right");
                look.top = CellBorder(cellLook, "top");
                look.bottom = CellBorder(cellLook, "bottom");
                for (const char* properties : {"graphic-properties", "table-cell-properties"}) {
                    if (const int64_t v = Padding(cellLook, properties, "left"); v >= 0) look.insetLeft = v;
                    if (const int64_t v = Padding(cellLook, properties, "right"); v >= 0) look.insetRight = v;
                    if (const int64_t v = Padding(cellLook, properties, "top"); v >= 0) look.insetTop = v;
                    if (const int64_t v = Padding(cellLook, properties, "bottom"); v >= 0) look.insetBottom = v;
                }
                std::string anchor = cellLook.Get("graphic-properties", "textarea-vertical-align", "draw", false);
                if (anchor.empty()) anchor = cellLook.Get("table-cell-properties", "vertical-align", "style", false);
                look.anchor = anchor == "middle" ? PptxAnchor::Middle : anchor == "bottom" ? PptxAnchor::Bottom : PptxAnchor::Top;
                std::vector<PptxParagraph> paragraphs;
                // The cell's text: its styles over the table frame's.
                ReadParagraphs(cell,
                               frameLook.With(context_.content, "table-cell", templateStyle)
                                   .With(context_.content, "table-cell", styleName),
                               paragraphs);
                texts.push_back(std::move(paragraphs));
                looks.push_back(look);
                ++c;
            }
            shape.rows.push_back(std::move(texts));
            shape.cellStyles.push_back(std::move(looks));
        }
    }

    // ---- paragraphs and lists ----

    // Where a list has got to: the number at each level, for its next item.
    struct Counters {
        int number[10] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    };

    struct TextState {
        Look look;
        const tinyxml2::XMLElement* defaultList = nullptr;   // the shape's styles' list style
        Counters last;                                        // the previous list's, for one continuing it
    };

    const tinyxml2::XMLElement* ListStyleNamed(const std::string& name) const {
        for (const StyleScope* scope : {&context_.content, &context_.master}) {
            const auto found = scope->lists.find(name);
            if (found != scope->lists.end()) return found->second;
        }
        return nullptr;
    }

    static const tinyxml2::XMLElement* LevelOf(const tinyxml2::XMLElement* listStyle, int level) {
        for (auto* e = listStyle ? listStyle->FirstChildElement() : nullptr; e; e = e->NextSiblingElement()) {
            double number = 0;
            if (Number(Attr(e, "level", "text"), number) && static_cast<int>(number) == level + 1) return e;
        }
        return nullptr;
    }

    // The paragraphs a text box, shape or cell holds, lists included. A
    // paragraph outside a list has no bullet. A numbered paragraph's
    // numberStartAt is worked out so that counting on as the renderer does
    // gives the number ODF shows.
    void ReadParagraphs(const tinyxml2::XMLElement* container, const Look& look, std::vector<PptxParagraph>& out) {
        TextState state{look, look.ListStyle(), {}};
        const size_t first = out.size();
        ReadBlocks(container, state, out);
        int numbers[9] = {0, 0, 0, 0, 0, 0, 0, 0, 0};
        for (size_t i = first; i < out.size(); ++i) {
            PptxParagraph& p = out[i];
            const int level = std::clamp(p.level, 0, 8);
            if (p.numbered) {
                p.numberStartAt -= numbers[level];
                ++numbers[level];
                for (int deeper = level + 1; deeper < 9; ++deeper) numbers[deeper] = 0;
            } else {
                for (int at = level; at < 9; ++at) numbers[at] = 0;
            }
        }
    }

    void ReadBlocks(const tinyxml2::XMLElement* container, TextState& state, std::vector<PptxParagraph>& out) {
        for (auto* e = container->FirstChildElement(); e; e = e->NextSiblingElement()) {
            if (Is(e, "p") || Is(e, "h")) {
                out.push_back(ReadParagraph(e, 0, nullptr, 0, state));
            } else if (Is(e, "list")) {
                Counters counters;
                if (Attr(e, "continue-numbering", "text") == "true" || !Attr(e, "continue-list", "text").empty())
                    counters = state.last;
                const tinyxml2::XMLElement* listStyle = ListStyleNamed(Attr(e, "style-name", "text"));
                ReadList(e, 0, listStyle ? listStyle : state.defaultList, counters, state, out);
                state.last = counters;
            } else if (Is(e, "section")) {
                ReadBlocks(e, state, out);
            }
        }
    }

    void ReadList(const tinyxml2::XMLElement* list, int level, const tinyxml2::XMLElement* listStyle, Counters& counters,
                  TextState& state, std::vector<PptxParagraph>& out) {
        if (level > 0)
            if (const tinyxml2::XMLElement* own = ListStyleNamed(Attr(list, "style-name", "text"))) listStyle = own;
        const int clamped = std::min(level, 8);
        const tinyxml2::XMLElement* levelStyle = LevelOf(listStyle, clamped);
        for (auto* item = list->FirstChildElement(); item; item = item->NextSiblingElement()) {
            const bool header = Is(item, "list-header");
            if (!header && !Is(item, "list-item")) continue;
            bool numberedHere = false;
            for (auto* e = item->FirstChildElement(); e; e = e->NextSiblingElement()) {
                if (Is(e, "p") || Is(e, "h")) {
                    int number = 0;
                    const bool numbered = !header && levelStyle && Is(levelStyle, "list-level-style-number") &&
                                          !Attr(levelStyle, "num-format", "style").empty();
                    if (numbered && !numberedHere) {
                        double start = 0;
                        if (Number(Attr(item, "start-value", "text"), start)) {
                            number = static_cast<int>(start);
                        } else if (counters.number[clamped] == 0) {
                            double levelStart = 1;
                            Number(Attr(levelStyle, "start-value", "text"), levelStart);
                            number = static_cast<int>(levelStart);
                        } else {
                            number = counters.number[clamped] + 1;
                        }
                        counters.number[clamped] = number;
                        for (int deeper = clamped + 1; deeper < 10; ++deeper) counters.number[deeper] = 0;
                        numberedHere = true;
                    } else if (numbered) {
                        number = counters.number[clamped];
                    }
                    // A second paragraph of an item carries no label of its own.
                    const bool labelled = !header && (e == FirstBlock(item));
                    out.push_back(ReadParagraph(e, clamped, labelled ? levelStyle : nullptr, number, state,
                                                labelled ? nullptr : levelStyle));
                } else if (Is(e, "list")) {
                    ReadList(e, level + 1, listStyle, counters, state, out);
                }
            }
        }
    }

    static const tinyxml2::XMLElement* FirstBlock(const tinyxml2::XMLElement* item) {
        for (auto* e = item->FirstChildElement(); e; e = e->NextSiblingElement())
            if (Is(e, "p") || Is(e, "h") || Is(e, "list")) return e;
        return nullptr;
    }

    // An ODF number format, prefix and suffix as an a:buAutoNum scheme.
    static std::string SchemeOf(const tinyxml2::XMLElement* levelStyle) {
        const std::string format = Attr(levelStyle, "num-format", "style");
        const std::string prefix = Attr(levelStyle, "num-prefix", "style");
        const std::string suffix = Attr(levelStyle, "num-suffix", "style");
        const std::string kind = format == "a" ? "alphaLc" : format == "A" ? "alphaUc" : format == "i" ? "romanLc"
                               : format == "I" ? "romanUc" : "arabic";
        if (prefix == "(" && suffix == ")") return kind + "ParenBoth";
        if (suffix == ")") return kind + "ParenR";
        if (suffix.empty() && prefix.empty()) return kind + "Plain";
        return kind + "Period";
    }

    std::string FontOf(const tinyxml2::XMLElement* textProperties) const {
        if (const char* face = AttrPtr(textProperties, "font-name", "style")) {
            const auto found = context_.fonts.find(face);
            return found == context_.fonts.end() ? std::string(face) : found->second;
        }
        if (const char* family = AttrPtr(textProperties, "font-family", "fo")) return Unquoted(family);
        return "";
    }

    // A paragraph at `level`: its alignment, spacing and margins from its
    // style and the shape's, its label from `levelStyle` (none: no bullet),
    // and its runs. `indentStyle` places a label-less paragraph of a list
    // item where its text stands.
    PptxParagraph ReadParagraph(const tinyxml2::XMLElement* p, int level, const tinyxml2::XMLElement* levelStyle,
                                int number, const TextState& state,
                                const tinyxml2::XMLElement* indentStyle = nullptr) {
        PptxParagraph paragraph;
        paragraph.level = level;
        const Look look = state.look.With(context_.content, "paragraph", Attr(p, "style-name", "text")).AtOutlineLevel(level);

        const std::string align = look.Get("paragraph-properties", "text-align", "fo");
        paragraph.align = align == "center"                     ? PptxAlign::Center
                        : align == "end" || align == "right"    ? PptxAlign::Right
                        : align == "justify"                    ? PptxAlign::Justify
                                                                : PptxAlign::Left;
        auto spacing = [&](const char* attribute, PptxSpacing& into) {
            const std::string value = look.Get("paragraph-properties", attribute, "fo");
            if (!value.empty() && !IsPercent(value)) into.points = static_cast<float>(LengthToEmu(value) / kEmuPerPoint);
        };
        spacing("margin-top", paragraph.spaceBefore);
        spacing("margin-bottom", paragraph.spaceAfter);
        const std::string lineHeight = look.Get("paragraph-properties", "line-height", "fo");
        double percent = 0;
        if (IsPercent(lineHeight) && Number(lineHeight, percent)) paragraph.lineSpacing.percent = static_cast<float>(percent / 100.0);
        else if (!lineHeight.empty() && lineHeight != "normal")
            paragraph.lineSpacing.points = static_cast<float>(LengthToEmu(lineHeight) / kEmuPerPoint);
        else if (const std::string atLeast = look.Get("paragraph-properties", "line-height-at-least", "style"); !atLeast.empty())
            paragraph.lineSpacing.points = static_cast<float>(LengthToEmu(atLeast) / kEmuPerPoint);
        const std::string marginText = look.Get("paragraph-properties", "margin-left", "fo");
        const std::string indentText = look.Get("paragraph-properties", "text-indent", "fo");
        const int64_t margin = LengthToEmu(marginText), indent = LengthToEmu(indentText);
        if (!marginText.empty() && !IsPercent(marginText)) paragraph.marginLeft = margin;
        if (!indentText.empty() && !IsPercent(indentText)) paragraph.indent = indent;

        // The label.
        const tinyxml2::XMLElement* placed = levelStyle ? levelStyle : indentStyle;
        if (placed) {
            const tinyxml2::XMLElement* position = Child(placed, "list-level-properties");
            const tinyxml2::XMLElement* alignment = Child(position, "list-level-label-alignment");
            if (Attr(position, "list-level-position-and-space-mode", "text") == "label-alignment" && alignment) {
                paragraph.marginLeft = LengthToEmu(Attr(alignment, "margin-left", "fo"));
                paragraph.indent = LengthToEmu(Attr(alignment, "text-indent", "fo"));
            } else if (AttrPtr(position, "space-before", "text") || AttrPtr(position, "min-label-width", "text")) {
                const int64_t before = LengthToEmu(Attr(position, "space-before", "text"));
                const int64_t label = LengthToEmu(Attr(position, "min-label-width", "text"));
                paragraph.marginLeft = margin + before + label;
                paragraph.indent = indent - label;
            }
            if (!levelStyle) paragraph.indent = 0;   // a continuation: no label to hang
        }
        if (levelStyle) {
            const tinyxml2::XMLElement* text = Child(levelStyle, "text-properties");
            if (Is(levelStyle, "list-level-style-number")) {
                if (!Attr(levelStyle, "num-format", "style").empty()) {
                    paragraph.bullet = paragraph.numbered = true;
                    paragraph.numberScheme = SchemeOf(levelStyle);
                    paragraph.numberStartAt = number;
                }
            } else if (Is(levelStyle, "list-level-style-bullet")) {
                std::string character = Attr(levelStyle, "bullet-char", "text");
                if (!character.empty()) {
                    paragraph.bullet = true;
                    // A symbol font's character in the private-use block: its code.
                    const uint32_t cp = FirstCodepoint(character);
                    if (cp >= 0xF020 && cp <= 0xF0FF) character = Utf8(cp - 0xF000);
                    paragraph.bulletChar = character;
                    paragraph.bulletFont = FontOf(text);
                }
            } else if (Is(levelStyle, "list-level-style-image")) {
                paragraph.bullet = true;
            }
            if (paragraph.bullet) {
                if (Attr(text, "use-window-font-color", "style") != "true")
                    paragraph.bulletColor = Hex(Attr(text, "color", "fo"));
                const std::string size = Attr(text, "font-size", "fo");
                double share = 0;
                if (IsPercent(size) && Number(size, share) && std::abs(share - 100.0) > 0.01)
                    paragraph.bulletSizePercent = static_cast<float>(share);
            }
        }

        Inline inlineState;
        inlineState.paragraph = &paragraph;
        ReadInline(p, RunOf(look), look, inlineState);
        // A space collapsed at the end goes.
        if (inlineState.trailingSpace && !paragraph.runs.empty()) {
            std::string& last = paragraph.runs.back().text;
            if (!last.empty() && last.back() == ' ') last.pop_back();
            if (last.empty()) paragraph.runs.pop_back();
        }
        return paragraph;
    }

    // ---- runs ----

    // A run's look from its styles: the span's, the paragraph's, the shape's.
    PptxRun RunOf(const Look& look) const {
        PptxRun run;
        // The size: a share of the size under it, or points.
        double factor = 1.0;
        look.Visit("text-properties", [&](const tinyxml2::XMLElement* t) {
            const std::string size = Attr(t, "font-size", "fo");
            if (size.empty()) return false;
            double value = 0;
            if (!Number(size, value)) return false;
            if (IsPercent(size)) {
                factor *= value / 100.0;
                return false;
            }
            run.sizePt = static_cast<float>(static_cast<double>(LengthToEmu(size)) / kEmuPerPoint * factor);
            return true;
        });
        look.Visit("text-properties", [&](const tinyxml2::XMLElement* t) {
            if (const char* c = AttrPtr(t, "color", "fo")) {
                run.color = Hex(c);
                return true;
            }
            return Attr(t, "use-window-font-color", "style") == "true";   // the slide's own text colour
        });
        look.Visit("text-properties", [&](const tinyxml2::XMLElement* t) {
            run.font = FontOf(t);
            return !run.font.empty();
        });
        const std::string weight = look.Get("text-properties", "font-weight", "fo");
        double numeric = 0;
        run.bold = weight == "bold" || (Number(weight, numeric) && numeric >= 600.0);
        const std::string style = look.Get("text-properties", "font-style", "fo");
        run.italic = style == "italic" || style == "oblique";
        const std::string underline = look.Get("text-properties", "text-underline-style", "style");
        run.underline = !underline.empty() && underline != "none";
        const std::string strike = look.Get("text-properties", "text-line-through-style", "style");
        run.strike = !strike.empty() && strike != "none";
        const std::string position = look.Get("text-properties", "text-position", "style");
        if (position.rfind("super", 0) == 0) run.baseline = 33;
        else if (position.rfind("sub", 0) == 0) run.baseline = -25;
        else if (double raise = 0; Number(position, raise)) run.baseline = static_cast<int>(std::lround(raise));
        run.allCaps = look.Get("text-properties", "text-transform", "fo") == "uppercase";
        run.smallCaps = look.Get("text-properties", "font-variant", "fo") == "small-caps";
        run.highlight = Hex(look.Get("text-properties", "background-color", "fo"));
        return run;
    }

    static bool SameLook(const PptxRun& a, const PptxRun& b) {
        return a.bold == b.bold && a.italic == b.italic && a.underline == b.underline && a.sizePt == b.sizePt &&
               a.color == b.color && a.font == b.font && a.strike == b.strike && a.baseline == b.baseline &&
               a.allCaps == b.allCaps && a.smallCaps == b.smallCaps && a.highlight == b.highlight &&
               a.hyperlink == b.hyperlink;
    }

    // ODF's white space: a run of spaces, tabs and line ends counts as one
    // space, and none at a paragraph's start or end; <text:s>, <text:tab>
    // and <text:line-break> are the ones that stay.
    struct Inline {
        PptxParagraph* paragraph = nullptr;
        bool afterSpace = true;          // drops a space at the start / after a collapsed one
        bool trailingSpace = false;      // the last character is a collapsed space
    };

    static void Append(Inline& state, const std::string& text, const PptxRun& look) {
        if (text.empty()) return;
        std::vector<PptxRun>& runs = state.paragraph->runs;
        if (runs.empty() || runs.back().text == "\n" || !SameLook(runs.back(), look)) {
            PptxRun run = look;
            run.text.clear();
            runs.push_back(std::move(run));
        }
        runs.back().text += text;
    }

    static void AppendCollapsed(Inline& state, const char* text, const PptxRun& look) {
        std::string piece;
        for (const char* c = text; *c; ++c) {
            if (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r') {
                if (state.afterSpace) continue;
                piece += ' ';
                state.afterSpace = true;
                state.trailingSpace = true;
            } else {
                piece += *c;
                state.afterSpace = false;
                state.trailingSpace = false;
            }
        }
        Append(state, piece, look);
    }

    static void AppendLiteral(Inline& state, const std::string& text, const PptxRun& look) {
        Append(state, text, look);
        state.afterSpace = false;
        state.trailingSpace = false;
    }

    void ReadInline(const tinyxml2::XMLNode* parent, const PptxRun& run, const Look& look, Inline& state) {
        for (const tinyxml2::XMLNode* node = parent->FirstChild(); node; node = node->NextSibling()) {
            if (const tinyxml2::XMLText* text = node->ToText()) {
                AppendCollapsed(state, text->Value() ? text->Value() : "", run);
                continue;
            }
            const tinyxml2::XMLElement* e = node->ToElement();
            if (!e) continue;
            if (Is(e, "span")) {
                const Look spanLook = look.With(context_.content, "text", Attr(e, "style-name", "text"));
                PptxRun spanRun = RunOf(spanLook);
                spanRun.hyperlink = run.hyperlink;
                ReadInline(e, spanRun, spanLook, state);
            } else if (Is(e, "a")) {
                const Look linkLook = look.With(context_.content, "text", Attr(e, "style-name", "text"));
                PptxRun linkRun = Attr(e, "style-name", "text").empty() ? run : RunOf(linkLook);
                linkRun.hyperlink = Attr(e, "href", "xlink");
                ReadInline(e, linkRun, linkLook, state);
            } else if (Is(e, "s")) {
                double count = 1.0;
                const std::string c = Attr(e, "c", "text");
                if (!c.empty() && !TryParseFloat(c, count)) count = 1.0;
                AppendLiteral(state, std::string(static_cast<size_t>(std::clamp(count, 1.0, 1000.0)), ' '), run);
            } else if (Is(e, "tab")) {
                AppendLiteral(state, "\t", run);
            } else if (Is(e, "line-break")) {
                PptxRun lineBreak = run;
                lineBreak.text = "\n";
                state.paragraph->runs.push_back(std::move(lineBreak));
                state.afterSpace = true;
                state.trailingSpace = false;
            } else if (Is(e, "annotation") || Is(e, "annotation-end") || Is(e, "frame") ||
                       Is(e, "bookmark") || Is(e, "bookmark-start") || Is(e, "bookmark-end") ||
                       Is(e, "reference-mark") || Is(e, "reference-mark-start") ||
                       Is(e, "reference-mark-end") || Is(e, "soft-page-break")) {
                continue;   // comments, anchored pictures and markers carry no text of the paragraph
            } else {
                // A field (page number, date, author...) and anything else: the text it holds.
                ReadInline(e, run, look, state);
            }
        }
    }
};

const tinyxml2::XMLElement* PresentationBody(const tinyxml2::XMLElement* root) {
    return Child(Child(root, "body"), "presentation");
}

bool ReadDocuments(const tinyxml2::XMLElement* contentRoot, const tinyxml2::XMLElement* stylesRoot,
                   PptxPresentation& out, std::string& error, bool withPictures,
                   std::function<bool(const std::string&, std::string&)> readEntry = nullptr) {
    const tinyxml2::XMLElement* presentation = PresentationBody(contentRoot);
    if (!presentation) {
        error = "The file is not an OpenDocument presentation - it has no presentation body";
        return false;
    }
    OdpContext context;
    context.readEntry = std::move(readEntry);
    context.withPictures = withPictures;
    // The common styles reach everything; a separate styles.xml's automatic
    // styles belong to its master pages, content.xml's to the slides. A flat
    // file keeps both in one place.
    const bool separate = stylesRoot && stylesRoot != contentRoot;
    if (separate) {
        CollectFonts(Child(stylesRoot, "font-face-decls"), context);
        CollectStyles(Child(stylesRoot, "styles"), context, {&context.content, &context.master});
        CollectStyles(Child(stylesRoot, "automatic-styles"), context, {&context.master});
        CollectMasters(Child(stylesRoot, "master-styles"), context);
    }
    CollectFonts(Child(contentRoot, "font-face-decls"), context);
    CollectStyles(Child(contentRoot, "styles"), context, {&context.content, &context.master});
    if (separate)
        CollectStyles(Child(contentRoot, "automatic-styles"), context, {&context.content});
    else
        CollectStyles(Child(contentRoot, "automatic-styles"), context, {&context.content, &context.master});
    CollectMasters(Child(contentRoot, "master-styles"), context);

    OdpReader reader(context, out);
    reader.ReadPresentation(presentation);
    return true;
}

bool IsZip(const uint8_t* data, size_t size) {
    return size >= 4 && data[0] == 'P' && data[1] == 'K' && data[2] == 3 && data[3] == 4;
}

// The media type a package declares: its mimetype entry, else the
// manifest's entry for the package itself.
std::string PackageMediaType(const UCZipPackageReader& zip) {
    std::string mimetype;
    if (zip.ReadEntry("mimetype", mimetype) && !mimetype.empty()) {
        while (!mimetype.empty() && std::isspace(static_cast<unsigned char>(mimetype.back()))) mimetype.pop_back();
        return mimetype;
    }
    std::string xml;
    tinyxml2::XMLDocument manifest;
    if (!zip.ReadEntry("META-INF/manifest.xml", xml) ||
        manifest.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS || !manifest.RootElement())
        return "";
    for (auto* entry = manifest.RootElement()->FirstChildElement(); entry; entry = entry->NextSiblingElement())
        if (Is(entry, "file-entry") && Attr(entry, "full-path") == "/") return Attr(entry, "media-type");
    return "";
}

bool IsPresentationMediaType(const std::string& mediaType) {
    return mediaType.rfind(kPresentationMediaType, 0) == 0;   // and "...-template"
}

bool ReadPackage(const UCZipPackageReader& zip, PptxPresentation& out, std::string& error,
                 const std::string& password, bool withPictures) {
    // A password-protected deck: decrypted into an ordinary package first.
    std::vector<uint8_t> decrypted;
    switch (DecryptOdfPackage(zip, password, decrypted, error)) {
        case OfficeDecryptStatus::NotEncrypted:
            error.clear();
            break;
        case OfficeDecryptStatus::Decrypted: {
            UCZipPackageReader plain;
            if (!plain.OpenFromMemory(std::move(decrypted))) {
                error = "The decrypted OpenDocument presentation is not valid";
                return false;
            }
            return ReadPackage(plain, out, error, std::string(), withPictures);
        }
        default:
            return false;   // `error` says why: no password, a wrong one, an unsupported cipher
    }
    const std::string mediaType = PackageMediaType(zip);
    if (!mediaType.empty() && !IsPresentationMediaType(mediaType)) {
        error = "The file is an OpenDocument file of another kind (" + mediaType + "), not a presentation";
        return false;
    }
    std::string contentXml, stylesXml;
    tinyxml2::XMLDocument content, styles;
    if (!zip.ReadEntry("content.xml", contentXml) ||
        content.Parse(contentXml.c_str(), contentXml.size()) != tinyxml2::XML_SUCCESS || !content.RootElement()) {
        error = "The OpenDocument package has no readable content.xml";
        return false;
    }
    const bool hasStyles = zip.ReadEntry("styles.xml", stylesXml) &&
                           styles.Parse(stylesXml.c_str(), stylesXml.size()) == tinyxml2::XML_SUCCESS &&
                           styles.RootElement();
    return ReadDocuments(content.RootElement(), hasStyles ? styles.RootElement() : nullptr, out, error, withPictures,
                         [&zip](const std::string& part, std::string& xml) { return zip.ReadEntry(part, xml); });
}

bool ReadFlat(const char* xml, size_t size, PptxPresentation& out, std::string& error, bool withPictures) {
    tinyxml2::XMLDocument doc;
    if (doc.Parse(xml, size) != tinyxml2::XML_SUCCESS || !doc.RootElement()) {
        error = "The file is not a valid OpenDocument presentation (.fodp) - the XML does not parse";
        return false;
    }
    const std::string mediaType = Attr(doc.RootElement(), "mimetype", "office");
    if (!mediaType.empty() && !IsPresentationMediaType(mediaType)) {
        error = "The file is an OpenDocument file of another kind (" + mediaType + "), not a presentation";
        return false;
    }
    return ReadDocuments(doc.RootElement(), nullptr, out, error, withPictures);
}

bool ReadBytes(const std::string& filePath, std::vector<uint8_t>& out, size_t limit) {
    std::ifstream file(PathFromUtf8(filePath), std::ios::binary);
    if (!file.is_open()) return false;
    out.resize(limit);
    file.read(reinterpret_cast<char*>(out.data()), static_cast<std::streamsize>(limit));
    out.resize(static_cast<size_t>(file.gcount()));
    return true;
}

} // namespace

bool IsOdpExtension(const std::string& extensionOrPath) {
    const size_t dot = extensionOrPath.rfind('.');
    std::string ext = dot == std::string::npos ? extensionOrPath : extensionOrPath.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext == "odp" || ext == "otp" || ext == "fodp";
}

bool IsOdpPresentationFile(const std::string& filePath) {
    std::vector<uint8_t> head;
    // A flat file names its type on the root element, after the namespaces
    // it declares - LibreOffice's run to some 2.5 KB.
    if (!ReadBytes(filePath, head, 16384) || head.empty()) return false;
    if (IsZip(head.data(), head.size())) {
        UCZipPackageReader zip;
        return zip.Open(filePath) && IsPresentationMediaType(PackageMediaType(zip));
    }
    const std::string text(head.begin(), head.end());
    return text.find(std::string("mimetype=\"") + kPresentationMediaType) != std::string::npos;
}

bool ReadOdpPresentationFromMemory(std::vector<uint8_t> data, PptxPresentation& out, std::string& error,
                                   const std::string& password, bool withPictures) {
    out = PptxPresentation{};
    error.clear();
    if (IsZip(data.data(), data.size())) {
        UCZipPackageReader zip;
        if (!zip.OpenFromMemory(std::move(data))) {
            error = "The file is not a valid OpenDocument presentation (.odp)";
            return false;
        }
        return ReadPackage(zip, out, error, password, withPictures);
    }
    return ReadFlat(reinterpret_cast<const char*>(data.data()), data.size(), out, error, withPictures);
}

bool ReadOdpPresentation(const std::string& filePath, PptxPresentation& out, std::string& error,
                         const std::string& password, bool withPictures) {
    out = PptxPresentation{};
    error.clear();
    std::vector<uint8_t> head;
    if (!ReadBytes(filePath, head, 4)) {
        error = "Cannot open file: " + filePath;
        return false;
    }
    bool ok = false;
    if (IsZip(head.data(), head.size())) {
        UCZipPackageReader zip;
        if (!zip.Open(filePath)) {
            error = "The file is not a valid OpenDocument presentation (.odp)";
        } else {
            ok = ReadPackage(zip, out, error, password, withPictures);
        }
    } else {
        std::ifstream file(PathFromUtf8(filePath), std::ios::binary);
        const std::string xml((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        ok = ReadFlat(xml.c_str(), xml.size(), out, error, withPictures);
    }
    if (!ok) error += ": " + filePath;
    return ok;
}

} // namespace UltraCanvas
