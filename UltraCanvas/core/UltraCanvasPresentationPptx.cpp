// core/UltraCanvasPresentationPptx.cpp
// PowerPoint (.pptx) reader - see UltraCanvasPresentationPptx.h.
// Version: 1.3.0 - fonts (the theme's too), spacing, indents, bullet characters and numbering schemes,
//                  insets, wrap and vertical text, strike / baseline / caps / highlight, hyperlinks;
//                  rotation, mirroring, custom outlines, gradients, dashes, arrowheads, connectors;
//                  table styles, merged cells, cell fills and borders; picture crops; charts
//                  (UltraCanvasDrawingML) and SmartArt (its drawing part)
// Version: 1.2.0 - mc:AlternateContent read: its first Choice that gives anything, else its
//                  Fallback - around shapes and around runs; an equation (a14:m) gives its text
// Version: 1.1.0 - the layout's and master's places, text styles and backgrounds, the theme's
//                  colours, fills and outlines, a group's shapes placed on the slide, the pictures
// Version: 1.0.0
// Last Modified: 2026-11-20
// Author: UltraCanvas Framework
#include "UltraCanvasPresentationPptx.h"
#include "UltraCanvasDrawingML.h"
#include "UltraCanvasOfficeEncryption.h"
#include "UltraCanvasZipPackage.h"
#include "tinyxml2.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace UltraCanvas {

namespace {

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

// The attribute, whatever prefix the writer gave it ("r:id", "r:embed").
std::string Attr(const tinyxml2::XMLElement* e, const char* localName) {
    for (const tinyxml2::XMLAttribute* a = e ? e->FirstAttribute() : nullptr; a; a = a->Next()) {
        const char* name = a->Name();
        const char* colon = std::strrchr(name, ':');
        if (std::strcmp(colon ? colon + 1 : name, localName) == 0) return a->Value();
    }
    return "";
}

// A relationship attribute (r:id, r:embed): prefixed, whatever the prefix -
// <p:sldId> carries a plain id="256" beside its r:id, which Attr would take.
std::string RelAttr(const tinyxml2::XMLElement* e, const char* localName) {
    for (const tinyxml2::XMLAttribute* a = e ? e->FirstAttribute() : nullptr; a; a = a->Next()) {
        const char* colon = std::strrchr(a->Name(), ':');
        if (colon && std::strcmp(colon + 1, localName) == 0) return a->Value();
    }
    return "";
}

int64_t AttrInt(const tinyxml2::XMLElement* e, const char* localName) {
    const std::string v = Attr(e, localName);
    if (v.empty()) return 0;
    char* end = nullptr;
    const long long n = std::strtoll(v.c_str(), &end, 10);
    return end && *end == '\0' ? static_cast<int64_t>(n) : 0;
}

// A part's path from a relationship target, relative to the part that names
// it ("../media/image1.png" from ppt/slides/slide1.xml is ppt/media/image1.png).
std::string ResolvePart(const std::string& fromPart, const std::string& target) {
    if (!target.empty() && target[0] == '/') return target.substr(1);
    std::vector<std::string> parts;
    const size_t slash = fromPart.rfind('/');
    std::string path = (slash == std::string::npos ? std::string() : fromPart.substr(0, slash + 1)) + target;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t end = std::min(path.find('/', start), path.size());
        const std::string piece = path.substr(start, end - start);
        if (piece == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!piece.empty() && piece != ".") {
            parts.push_back(piece);
        }
        start = end + 1;
    }
    std::string out;
    for (const std::string& p : parts) out += (out.empty() ? "" : "/") + p;
    return out;
}

// ----- Colours -----
// "RRGGBB" <-> 0xRRGGBB, and the DrawingML colour modifiers worked in HSL
// (lumMod / lumOff) or toward white and black (tint / shade).

uint32_t ParseHex(const std::string& hex) {
    return hex.size() == 6 ? static_cast<uint32_t>(std::strtoul(hex.c_str(), nullptr, 16)) : 0;
}

std::string ToHex(uint32_t rgb) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%06X", rgb & 0xFFFFFFu);
    return buffer;
}

void RgbToHsl(double r, double g, double b, double& h, double& s, double& l) {
    const double maxC = std::max({r, g, b}), minC = std::min({r, g, b});
    l = (maxC + minC) / 2.0;
    if (maxC == minC) {
        h = s = 0;
        return;
    }
    const double d = maxC - minC;
    s = l > 0.5 ? d / (2.0 - maxC - minC) : d / (maxC + minC);
    if (maxC == r) h = (g - b) / d + (g < b ? 6 : 0);
    else if (maxC == g) h = (b - r) / d + 2;
    else h = (r - g) / d + 4;
    h /= 6.0;
}

double HueToRgb(double p, double q, double t) {
    if (t < 0) t += 1;
    if (t > 1) t -= 1;
    if (t < 1.0 / 6) return p + (q - p) * 6 * t;
    if (t < 1.0 / 2) return q;
    if (t < 2.0 / 3) return p + (q - p) * (2.0 / 3 - t) * 6;
    return p;
}

std::string ApplyModifiers(const std::string& hex, const tinyxml2::XMLElement* colour) {
    if (hex.empty() || !colour) return hex;
    const uint32_t rgb = ParseHex(hex);
    double r = ((rgb >> 16) & 0xFF) / 255.0, g = ((rgb >> 8) & 0xFF) / 255.0, b = (rgb & 0xFF) / 255.0;
    bool changed = false;
    for (auto* m = colour->FirstChildElement(); m; m = m->NextSiblingElement()) {
        const double v = AttrInt(m, "val") / 100000.0;
        if (Is(m, "lumMod") || Is(m, "lumOff")) {
            double h, s, l;
            RgbToHsl(r, g, b, h, s, l);
            l = Is(m, "lumMod") ? l * v : l + v;
            l = std::clamp(l, 0.0, 1.0);
            if (s == 0) {
                r = g = b = l;
            } else {
                const double q = l < 0.5 ? l * (1 + s) : l + s - l * s, p = 2 * l - q;
                r = HueToRgb(p, q, h + 1.0 / 3);
                g = HueToRgb(p, q, h);
                b = HueToRgb(p, q, h - 1.0 / 3);
            }
            changed = true;
        } else if (Is(m, "tint")) {   // toward white: val is how much of the colour stays
            r = 1 - (1 - r) * v;
            g = 1 - (1 - g) * v;
            b = 1 - (1 - b) * v;
            changed = true;
        } else if (Is(m, "shade")) {  // toward black
            r *= v;
            g *= v;
            b *= v;
            changed = true;
        }
    }
    if (!changed) return hex;
    auto byte = [](double c) { return static_cast<uint32_t>(std::lround(std::clamp(c, 0.0, 1.0) * 255.0)); };
    return ToHex((byte(r) << 16) | (byte(g) << 8) | byte(b));
}

// A level's text style: what a list style (a:lstStyle, p:titleStyle ...) says for one level.
struct LevelStyle {
    float sizePt = 0;
    std::string color;
    int align = -1;   // PptxAlign; -1: not said
    std::string font;             // a:latin typeface ("+mj-lt" kept as it is, resolved late)
    int bold = -1, italic = -1;   // -1: not said
    PptxSpacing spaceBefore, spaceAfter, lineSpacing;
    int64_t marginLeft = -1;
    int64_t indent = 0;
    bool hasIndent = false;
    int bullet = -1;              // -1: not said, 0: buNone, 1: buChar, 2: buAutoNum
    std::string bulletChar, bulletFont, bulletColor, numberScheme;
    float bulletSizePercent = 0;
    int numberStartAt = 0;

    // What this level leaves unsaid, taken from `from`.
    void Inherit(const LevelStyle& from) {
        if (sizePt <= 0) sizePt = from.sizePt;
        if (align < 0) align = from.align;
        if (font.empty()) font = from.font;
        if (bold < 0) bold = from.bold;
        if (italic < 0) italic = from.italic;
        if (!spaceBefore.IsSet()) spaceBefore = from.spaceBefore;
        if (!spaceAfter.IsSet()) spaceAfter = from.spaceAfter;
        if (!lineSpacing.IsSet()) lineSpacing = from.lineSpacing;
        if (marginLeft < 0) marginLeft = from.marginLeft;
        if (!hasIndent && from.hasIndent) {
            indent = from.indent;
            hasIndent = true;
        }
        if (bullet < 0) {
            bullet = from.bullet;
            bulletChar = from.bulletChar;
            numberScheme = from.numberScheme;
            numberStartAt = from.numberStartAt;
        }
        if (bulletFont.empty()) bulletFont = from.bulletFont;
        if (bulletColor.empty()) bulletColor = from.bulletColor;
        if (bulletSizePercent <= 0) bulletSizePercent = from.bulletSizePercent;
    }
};

struct ListStyle {
    LevelStyle levels[9];
};

// What a placeholder on a layout or master gives the slide's placeholder of the same kind.
struct PlaceholderStyle {
    bool hasPlace = false;
    int64_t x = 0, y = 0, width = 0, height = 0;
    int anchor = -1;  // PptxAnchor; -1: not said
    ListStyle list;
};

// A slide layout or slide master: its placeholders and background.
struct Template {
    std::map<std::string, PlaceholderStyle> byType;
    std::map<std::string, PlaceholderStyle> byIdx;
    std::string background;
};

std::string PlaceholderType(const tinyxml2::XMLElement* ph) {
    const std::string type = Attr(ph, "type");
    return type.empty() || type == "obj" ? "body" : type;
}

const PlaceholderStyle* FindPlaceholder(const Template& t, const std::string& type, const std::string& idx) {
    if (!idx.empty()) {
        const auto found = t.byIdx.find(idx);
        if (found != t.byIdx.end()) return &found->second;
    }
    auto found = t.byType.find(type);
    if (found != t.byType.end()) return &found->second;
    // The master has a title and a body: a title slide's centred title and
    // subtitle take theirs.
    const std::string fallback = type == "ctrTitle" ? "title" : type == "subTitle" ? "body" : "";
    if (!fallback.empty() && (found = t.byType.find(fallback)) != t.byType.end()) return &found->second;
    return nullptr;
}

class PptxReader {
public:
    PptxReader(UCZipPackageReader& zip, bool withPictures) : zip_(zip), withPictures_(withPictures) {}

    bool Read(PptxPresentation& out, std::string& error) {
        tinyxml2::XMLDocument presentation;
        if (!LoadXml("ppt/presentation.xml", presentation)) {
            error = "The package has no ppt/presentation.xml - not a PowerPoint presentation";
            return false;
        }
        const tinyxml2::XMLElement* root = presentation.RootElement();
        if (const tinyxml2::XMLElement* size = Child(root, "sldSz")) {
            out.slideWidth = AttrInt(size, "cx");
            out.slideHeight = AttrInt(size, "cy");
        }
        // Text outside placeholders (a text box): the deck's default text style.
        ReadListStyle(Child(root, "defaultTextStyle"), defaultTextStyle_);
        const std::map<std::string, std::string> rels = Relationships("ppt/presentation.xml");
        int counted = 0;
        for (auto* id = Child(Child(root, "sldIdLst"), "sldId"); id; id = id->NextSiblingElement()) {
            if (!Is(id, "sldId")) continue;
            const auto target = rels.find(RelAttr(id, "id"));
            if (target != rels.end()) slideNumbers_[target->second] = ++counted;
        }
        int number = 0;
        for (auto* id = Child(Child(root, "sldIdLst"), "sldId"); id; id = id->NextSiblingElement()) {
            if (!Is(id, "sldId")) continue;
            const auto target = rels.find(RelAttr(id, "id"));
            if (target == rels.end()) continue;
            PptxSlide slide;
            slide.number = ++number;
            if (ReadSlide(target->second, slide, out)) out.slides.push_back(std::move(slide));
        }
        out.themeColors = theme_;
        out.majorFont = majorFont_;
        out.minorFont = minorFont_;
        return true;
    }

private:
    struct Master {
        Template places;
        ListStyle titleStyle, bodyStyle, otherStyle;
        std::map<std::string, std::string> colorMap;   // tx1 -> dk1 ...
    };

    UCZipPackageReader& zip_;
    bool withPictures_ = false;
    ListStyle defaultTextStyle_;
    std::map<std::string, std::string> theme_;           // dk1 -> "000000" ...
    std::string majorFont_, minorFont_;                  // the theme's heading and body typefaces
    std::map<std::string, std::string> externalLinks_;   // the slide's hyperlink relationships: id -> target
    std::map<std::string, std::string> slideLinks_;      // ... to another slide: id -> "#slideN"
    std::map<std::string, int> slideNumbers_;            // slide part -> its number
    tinyxml2::XMLDocument tableStyles_;                  // ppt/tableStyles.xml
    bool tableStylesRead_ = false;
    std::map<std::string, Master> masters_;
    std::map<std::string, Template> layouts_;
    const Master* master_ = nullptr;                     // the slide's being read
    const Template* layout_ = nullptr;

    bool LoadXml(const std::string& part, tinyxml2::XMLDocument& doc) {
        std::string xml;
        return zip_.ReadEntry(part, xml) && doc.Parse(xml.c_str(), xml.size()) == tinyxml2::XML_SUCCESS &&
               doc.RootElement();
    }

    // Relationship id -> resolved part, and type suffix -> part for lookups by type.
    std::map<std::string, std::string> Relationships(const std::string& part,
                                                     std::map<std::string, std::string>* byType = nullptr,
                                                     std::map<std::string, std::string>* external = nullptr) {
        std::map<std::string, std::string> out;
        const size_t slash = part.rfind('/');
        const std::string relsPart = part.substr(0, slash + 1) + "_rels/" + part.substr(slash + 1) + ".rels";
        tinyxml2::XMLDocument doc;
        if (!LoadXml(relsPart, doc)) return out;
        for (auto* r = doc.RootElement()->FirstChildElement(); r; r = r->NextSiblingElement()) {
            if (!Is(r, "Relationship")) continue;
            if (Attr(r, "TargetMode") == "External") {
                if (external) (*external)[Attr(r, "Id")] = Attr(r, "Target");
                continue;
            }
            const std::string resolved = ResolvePart(part, Attr(r, "Target"));
            out[Attr(r, "Id")] = resolved;
            if (byType) {
                const std::string type = Attr(r, "Type");
                (*byType)[type.substr(type.rfind('/') + 1)] = resolved;
            }
        }
        return out;
    }

    // ----- Colours -----

    // The colour an element holding one names (a:solidFill, a fillRef, a
    // clrScheme entry ...): its srgbClr, sysClr, prstClr or schemeClr child,
    // with that child's modifiers. Empty when none.
    std::string Colour(const tinyxml2::XMLElement* holder) const {
        for (auto* c = holder ? holder->FirstChildElement() : nullptr; c; c = c->NextSiblingElement()) {
            std::string hex;
            if (Is(c, "srgbClr")) hex = Attr(c, "val");
            else if (Is(c, "sysClr")) hex = Attr(c, "lastClr");
            else if (Is(c, "prstClr")) hex = Attr(c, "val") == "white" ? "FFFFFF" : "000000";
            else if (Is(c, "schemeClr")) hex = SchemeColour(Attr(c, "val"));
            else continue;
            if (hex.size() != 6) return "";
            return ApplyModifiers(hex, c);
        }
        return "";
    }

    std::string SchemeColour(const std::string& name) const {
        std::string key = name;
        if (master_) {
            const auto mapped = master_->colorMap.find(name);
            if (mapped != master_->colorMap.end()) key = mapped->second;
        }
        if (key == "tx1") key = "dk1";
        else if (key == "bg1") key = "lt1";
        else if (key == "tx2") key = "dk2";
        else if (key == "bg2") key = "lt2";
        const auto found = theme_.find(key);
        return found == theme_.end() ? "" : found->second;
    }

    // A shape's fill from its spPr (or its style's fillRef): empty for none.
    std::string Fill(const tinyxml2::XMLElement* spPr, const tinyxml2::XMLElement* style) const {
        if (Child(spPr, "noFill")) return "";
        if (const tinyxml2::XMLElement* solid = Child(spPr, "solidFill")) return Colour(solid);
        if (Child(spPr, "gradFill") || Child(spPr, "blipFill") || Child(spPr, "pattFill")) return "";
        const tinyxml2::XMLElement* ref = Child(style, "fillRef");
        return ref && AttrInt(ref, "idx") > 0 ? Colour(ref) : "";
    }

    void Outline(const tinyxml2::XMLElement* spPr, const tinyxml2::XMLElement* style, PptxShape& shape) const {
        const tinyxml2::XMLElement* ln = Child(spPr, "ln");
        if (ln && Child(ln, "noFill")) return;
        if (const tinyxml2::XMLElement* solid = Child(ln, "solidFill")) {
            shape.line = Colour(solid);
        } else if (const tinyxml2::XMLElement* ref = Child(style, "lnRef"); ref && AttrInt(ref, "idx") > 0) {
            shape.line = Colour(ref);
        }
        if (!shape.line.empty()) {
            const int64_t w = AttrInt(ln, "w");
            shape.lineWidthPt = w > 0 ? w / 12700.0 : 0.75;
        }
    }

    // ----- Styles -----

    void ReadLevel(const tinyxml2::XMLElement* lvl, LevelStyle& out) const {
        if (!lvl) return;
        const std::string algn = Attr(lvl, "algn");
        if (algn == "l") out.align = static_cast<int>(PptxAlign::Left);
        else if (algn == "ctr") out.align = static_cast<int>(PptxAlign::Center);
        else if (algn == "r") out.align = static_cast<int>(PptxAlign::Right);
        else if (algn == "just" || algn == "dist") out.align = static_cast<int>(PptxAlign::Justify);
        if (const tinyxml2::XMLElement* rPr = Child(lvl, "defRPr")) {
            if (const int64_t sz = AttrInt(rPr, "sz"); sz > 0) out.sizePt = sz / 100.0f;
            if (const tinyxml2::XMLElement* solid = Child(rPr, "solidFill")) out.color = Colour(solid);
            if (const tinyxml2::XMLElement* latin = Child(rPr, "latin")) out.font = Attr(latin, "typeface");
            if (const std::string b = Attr(rPr, "b"); !b.empty()) out.bold = b == "1" || b == "true";
            if (const std::string i = Attr(rPr, "i"); !i.empty()) out.italic = i == "1" || i == "true";
        }
        if (const std::string marL = Attr(lvl, "marL"); !marL.empty()) out.marginLeft = AttrInt(lvl, "marL");
        if (const std::string indent = Attr(lvl, "indent"); !indent.empty()) {
            out.indent = AttrInt(lvl, "indent");
            out.hasIndent = true;
        }
        ReadSpacing(Child(lvl, "spcBef"), out.spaceBefore);
        ReadSpacing(Child(lvl, "spcAft"), out.spaceAfter);
        ReadSpacing(Child(lvl, "lnSpc"), out.lineSpacing);
        if (Child(lvl, "buNone")) {
            out.bullet = 0;
        } else if (const tinyxml2::XMLElement* ch = Child(lvl, "buChar")) {
            out.bullet = 1;
            out.bulletChar = Attr(ch, "char");
        } else if (const tinyxml2::XMLElement* num = Child(lvl, "buAutoNum")) {
            out.bullet = 2;
            out.numberScheme = Attr(num, "type");
            const int64_t start = AttrInt(num, "startAt");
            out.numberStartAt = start > 0 ? static_cast<int>(std::min<int64_t>(start, 32767)) : 1;
        } else if (Child(lvl, "buBlip")) {
            out.bullet = 1;   // a picture bullet: drawn as PowerPoint's dot
        }
        if (const tinyxml2::XMLElement* font = Child(lvl, "buFont")) out.bulletFont = Attr(font, "typeface");
        if (const tinyxml2::XMLElement* colour = Child(lvl, "buClr")) out.bulletColor = Colour(colour);
        if (const tinyxml2::XMLElement* size = Child(lvl, "buSzPct")) out.bulletSizePercent = AttrInt(size, "val") / 1000.0f;
    }

    // a:spcBef / a:spcAft / a:lnSpc: a:spcPct (thousandths of a per cent) or a:spcPts (hundredths of a point).
    static void ReadSpacing(const tinyxml2::XMLElement* holder, PptxSpacing& out) {
        if (!holder) return;
        if (const tinyxml2::XMLElement* pct = Child(holder, "spcPct")) {
            const int64_t v = AttrInt(pct, "val");
            if (v > 0) out = PptxSpacing{v / 100000.0f, 0};
        } else if (const tinyxml2::XMLElement* pts = Child(holder, "spcPts")) {
            const int64_t v = AttrInt(pts, "val");
            if (v >= 0) out = PptxSpacing{0, std::max(0.01f, v / 100.0f)};
        }
    }

    // A typeface as the theme names it: "+mj-lt" the heading font, "+mn-lt" the body's.
    std::string Typeface(const std::string& face) const {
        if (face.rfind("+mj", 0) == 0) return majorFont_;
        if (face.rfind("+mn", 0) == 0) return minorFont_;
        return face;
    }

    void ReadListStyle(const tinyxml2::XMLElement* list, ListStyle& out) const {
        if (!list) return;
        for (int i = 0; i < 9; ++i) {
            const std::string name = "lvl" + std::to_string(i + 1) + "pPr";
            ReadLevel(Child(list, name.c_str()), out.levels[i]);
        }
    }

    // A layout's or master's placeholders: place, anchor and list style by type and idx.
    void ReadTemplate(const tinyxml2::XMLElement* root, Template& out) const {
        const tinyxml2::XMLElement* cSld = Child(root, "cSld");
        out.background = Background(Child(cSld, "bg"));
        for (auto* sp = Child(Child(cSld, "spTree"), "sp"); sp; sp = sp->NextSiblingElement()) {
            if (!Is(sp, "sp")) continue;
            const tinyxml2::XMLElement* ph = Child(Child(Child(sp, "nvSpPr"), "nvPr"), "ph");
            if (!ph) continue;
            PlaceholderStyle style;
            if (const tinyxml2::XMLElement* xfrm = Child(Child(sp, "spPr"), "xfrm")) {
                const tinyxml2::XMLElement* off = Child(xfrm, "off");
                const tinyxml2::XMLElement* ext = Child(xfrm, "ext");
                if (off && ext) {
                    style.hasPlace = true;
                    style.x = AttrInt(off, "x");
                    style.y = AttrInt(off, "y");
                    style.width = AttrInt(ext, "cx");
                    style.height = AttrInt(ext, "cy");
                }
            }
            const tinyxml2::XMLElement* body = Child(sp, "txBody");
            style.anchor = Anchor(Child(body, "bodyPr"));
            ReadListStyle(Child(body, "lstStyle"), style.list);
            const std::string type = PlaceholderType(ph);
            out.byType.emplace(type, style);
            const std::string idx = Attr(ph, "idx");
            if (!idx.empty()) out.byIdx.emplace(idx, style);
        }
    }

    static int Anchor(const tinyxml2::XMLElement* bodyPr) {
        const std::string anchor = Attr(bodyPr, "anchor");
        if (anchor == "t") return static_cast<int>(PptxAnchor::Top);
        if (anchor == "ctr") return static_cast<int>(PptxAnchor::Middle);
        if (anchor == "b") return static_cast<int>(PptxAnchor::Bottom);
        return -1;
    }

    std::string Background(const tinyxml2::XMLElement* bg) const {
        if (!bg) return "";
        if (const tinyxml2::XMLElement* pr = Child(bg, "bgPr")) return Colour(Child(pr, "solidFill"));
        return Colour(Child(bg, "bgRef"));
    }

    const Master* LoadMaster(const std::string& part) {
        if (part.empty()) return nullptr;
        if (const auto found = masters_.find(part); found != masters_.end()) return &found->second;
        Master& master = masters_[part];
        tinyxml2::XMLDocument doc;
        if (!LoadXml(part, doc)) return &master;
        const tinyxml2::XMLElement* root = doc.RootElement();
        std::map<std::string, std::string> byType;
        Relationships(part, &byType);
        if (theme_.empty()) {
            const auto theme = byType.find("theme");
            tinyxml2::XMLDocument themeDoc;
            if (theme != byType.end() && LoadXml(theme->second, themeDoc)) {
                const tinyxml2::XMLElement* scheme =
                    Child(Child(themeDoc.RootElement(), "themeElements"), "clrScheme");
                for (auto* c = scheme ? scheme->FirstChildElement() : nullptr; c; c = c->NextSiblingElement()) {
                    const std::string hex = Colour(c);   // its srgbClr or sysClr
                    if (!hex.empty()) theme_[LocalName(c)] = hex;
                }
                const tinyxml2::XMLElement* fonts =
                    Child(Child(themeDoc.RootElement(), "themeElements"), "fontScheme");
                majorFont_ = Attr(Child(Child(fonts, "majorFont"), "latin"), "typeface");
                minorFont_ = Attr(Child(Child(fonts, "minorFont"), "latin"), "typeface");
            }
        }
        if (const tinyxml2::XMLElement* map = Child(root, "clrMap")) {
            for (const tinyxml2::XMLAttribute* a = map->FirstAttribute(); a; a = a->Next())
                master.colorMap[a->Name()] = a->Value();
        }
        const Master* previous = master_;
        master_ = &master;   // the master's own colours resolve through its map
        ReadTemplate(root, master.places);
        const tinyxml2::XMLElement* styles = Child(root, "txStyles");
        ReadListStyle(Child(styles, "titleStyle"), master.titleStyle);
        ReadListStyle(Child(styles, "bodyStyle"), master.bodyStyle);
        ReadListStyle(Child(styles, "otherStyle"), master.otherStyle);
        master_ = previous;
        return &master;
    }

    // ----- Slides -----

    bool ReadSlide(const std::string& part, PptxSlide& slide, PptxPresentation& out) {
        tinyxml2::XMLDocument doc;
        if (!LoadXml(part, doc)) return false;
        const tinyxml2::XMLElement* root = doc.RootElement();
        slide.hidden = Attr(root, "show") == "0";
        std::map<std::string, std::string> byType;
        externalLinks_.clear();
        slideLinks_.clear();
        const std::map<std::string, std::string> rels = Relationships(part, &byType, &externalLinks_);
        for (const auto& [id, target] : rels) {
            const auto other = slideNumbers_.find(target);
            if (other != slideNumbers_.end()) slideLinks_[id] = "#slide" + std::to_string(other->second);
        }

        // Its layout, and the layout's master: what its placeholders inherit.
        master_ = nullptr;
        layout_ = nullptr;
        if (const auto layout = byType.find("slideLayout"); layout != byType.end()) {
            std::map<std::string, std::string> layoutRels;
            Relationships(layout->second, &layoutRels);
            const auto master = layoutRels.find("slideMaster");
            master_ = LoadMaster(master == layoutRels.end() ? "" : master->second);
            if (const auto found = layouts_.find(layout->second); found != layouts_.end()) {
                layout_ = &found->second;
            } else {
                Template& t = layouts_[layout->second];
                tinyxml2::XMLDocument layoutDoc;
                if (LoadXml(layout->second, layoutDoc)) ReadTemplate(layoutDoc.RootElement(), t);
                layout_ = &t;
            }
        }
        slide.background = Background(Child(Child(root, "cSld"), "bg"));
        if (slide.background.empty() && layout_) slide.background = layout_->background;
        if (slide.background.empty() && master_) slide.background = master_->places.background;
        slide.textColor = SchemeColour("tx1");

        ReadTree(Child(Child(root, "cSld"), "spTree"), rels, slide.shapes, Transform{}, out);
        for (const PptxShape& shape : slide.shapes) {
            if (shape.kind == PptxShapeKind::Title && slide.title.empty()) {
                for (const PptxParagraph& p : shape.paragraphs) {
                    const std::string text = p.Text();
                    if (text.empty()) continue;
                    slide.title += (slide.title.empty() ? "" : " ") + text;
                }
            }
        }
        // The speaker notes: the notes slide's body placeholder.
        const auto notes = byType.find("notesSlide");
        tinyxml2::XMLDocument notesDoc;
        if (notes != byType.end() && LoadXml(notes->second, notesDoc)) {
            std::vector<PptxShape> shapes;
            const Master* master = master_;
            const Template* layout = layout_;
            master_ = nullptr;   // the notes page has its own master; its styles do not matter here
            layout_ = nullptr;
            ReadTree(Child(Child(notesDoc.RootElement(), "cSld"), "spTree"), {}, shapes, Transform{}, out);
            master_ = master;
            layout_ = layout;
            for (PptxShape& shape : shapes)
                if (shape.kind == PptxShapeKind::Body)
                    for (PptxParagraph& p : shape.paragraphs) slide.notes.push_back(std::move(p));
        }
        return true;
    }

    // A group's child coordinates onto the slide: x' = offset + (x - childOffset) * scale.
    struct Transform {
        double scaleX = 1, scaleY = 1, dx = 0, dy = 0;
        void Apply(PptxShape& shape) const {
            shape.x = static_cast<int64_t>(std::llround(shape.x * scaleX + dx));
            shape.y = static_cast<int64_t>(std::llround(shape.y * scaleY + dy));
            shape.width = static_cast<int64_t>(std::llround(shape.width * scaleX));
            shape.height = static_cast<int64_t>(std::llround(shape.height * scaleY));
        }
    };

    static Transform GroupTransform(const tinyxml2::XMLElement* grpSpPr, const Transform& outer) {
        const tinyxml2::XMLElement* xfrm = Child(grpSpPr, "xfrm");
        const tinyxml2::XMLElement* off = Child(xfrm, "off");
        const tinyxml2::XMLElement* ext = Child(xfrm, "ext");
        const tinyxml2::XMLElement* chOff = Child(xfrm, "chOff");
        const tinyxml2::XMLElement* chExt = Child(xfrm, "chExt");
        if (!off || !ext || !chOff || !chExt) return outer;
        const double cw = static_cast<double>(AttrInt(chExt, "cx")), ch = static_cast<double>(AttrInt(chExt, "cy"));
        Transform t;
        t.scaleX = cw > 0 ? AttrInt(ext, "cx") / cw : 1;
        t.scaleY = ch > 0 ? AttrInt(ext, "cy") / ch : 1;
        t.dx = AttrInt(off, "x") - AttrInt(chOff, "x") * t.scaleX;
        t.dy = AttrInt(off, "y") - AttrInt(chOff, "y") * t.scaleY;
        // Then the outer group's.
        Transform combined;
        combined.scaleX = t.scaleX * outer.scaleX;
        combined.scaleY = t.scaleY * outer.scaleY;
        combined.dx = t.dx * outer.scaleX + outer.dx;
        combined.dy = t.dy * outer.scaleY + outer.dy;
        return combined;
    }

    void ReadTree(const tinyxml2::XMLElement* tree, const std::map<std::string, std::string>& rels,
                  std::vector<PptxShape>& out, const Transform& transform, PptxPresentation& deck, int depth = 0) {
        if (depth > 16) return;
        for (auto* e = tree ? tree->FirstChildElement() : nullptr; e; e = e->NextSiblingElement()) {
            if (Is(e, "sp")) {
                PptxShape shape;
                if (ReadShape(e, shape)) {
                    if (!shape.placeInherited) transform.Apply(shape);
                    out.push_back(std::move(shape));
                }
            } else if (Is(e, "cxnSp")) {
                PptxShape shape;
                if (ReadConnector(e, shape)) {
                    transform.Apply(shape);
                    out.push_back(std::move(shape));
                }
            } else if (Is(e, "grpSp")) {
                ReadTree(e, rels, out, GroupTransform(Child(e, "grpSpPr"), transform), deck, depth + 1);
            } else if (Is(e, "AlternateContent")) {
                // Markup Compatibility (ECMA-376 Part 3): PowerPoint wraps
                // what an older reader may not know - an equation, a newer
                // chart, a 3D model, ink - in a Choice and gives a Fallback
                // beside it. Read the first Choice that gives a shape, else
                // the Fallback; never both, which would read it twice.
                bool chosen = false;
                for (auto* branch = e->FirstChildElement(); branch && !chosen; branch = branch->NextSiblingElement()) {
                    if (!Is(branch, "Choice")) continue;
                    std::vector<PptxShape> shapes;
                    ReadTree(branch, rels, shapes, transform, deck, depth + 1);
                    if (shapes.empty()) continue;
                    for (PptxShape& shape : shapes) out.push_back(std::move(shape));
                    chosen = true;
                }
                if (!chosen) ReadTree(Child(e, "Fallback"), rels, out, transform, deck, depth + 1);
            } else if (Is(e, "graphicFrame")) {
                ReadGraphicFrame(e, rels, out, transform, deck, depth);
            } else if (Is(e, "pic")) {
                PptxShape shape;
                ReadPicture(e, rels, deck, shape);
                if (!shape.placeInherited) transform.Apply(shape);
                out.push_back(std::move(shape));
            }
        }
    }

    // A table, a chart, SmartArt or an embedded object's picture.
    void ReadGraphicFrame(const tinyxml2::XMLElement* e, const std::map<std::string, std::string>& rels,
                          std::vector<PptxShape>& out, const Transform& transform, PptxPresentation& deck, int depth) {
        const tinyxml2::XMLElement* data = Child(Child(e, "graphic"), "graphicData");
        const std::string uri = Attr(data, "uri");
        PptxShape frame;
        frame.name = Attr(Child(Child(e, "nvGraphicFramePr"), "cNvPr"), "name");
        ReadPlace(Child(e, "xfrm"), frame);
        if (const tinyxml2::XMLElement* table = Child(data, "tbl")) {
            frame.kind = PptxShapeKind::Table;
            ReadTable(table, frame);
            transform.Apply(frame);
            out.push_back(std::move(frame));
            return;
        }
        if (uri.find("/chart") != std::string::npos) {
            // A chart part (c:chartSpace), read as .docx and .xlsx read theirs.
            const auto part = rels.find(RelAttr(Child(data, "chart"), "id"));
            std::string xml;
            if (part != rels.end() && zip_.ReadEntry(part->second, xml) &&
                ReadDrawingMLChart(xml, theme_.empty() ? DrawingMLDefaultThemeColors() : theme_, frame.chart)) {
                frame.kind = PptxShapeKind::Chart;
                transform.Apply(frame);
                out.push_back(std::move(frame));
            }
            return;
        }
        if (uri.find("/diagram") != std::string::npos) {
            // SmartArt: PowerPoint saves the diagram as drawn beside its data
            // (a dsp:drawing part the data model's extension names), which
            // reads as ordinary shapes placed in the frame.
            const auto model = rels.find(RelAttr(Child(data, "relIds"), "dm"));
            tinyxml2::XMLDocument modelDoc;
            if (model == rels.end() || !LoadXml(model->second, modelDoc)) return;
            const tinyxml2::XMLElement* ext = FindElement(modelDoc.RootElement(), "dataModelExt");
            const auto drawing = rels.find(Attr(ext, "relId"));
            tinyxml2::XMLDocument drawingDoc;
            if (drawing == rels.end() || !LoadXml(drawing->second, drawingDoc)) return;
            Transform inFrame;
            inFrame.dx = static_cast<double>(frame.x) * transform.scaleX + transform.dx;
            inFrame.dy = static_cast<double>(frame.y) * transform.scaleY + transform.dy;
            inFrame.scaleX = transform.scaleX;
            inFrame.scaleY = transform.scaleY;
            const std::map<std::string, std::string> drawingRels = Relationships(drawing->second);
            ReadTree(Child(drawingDoc.RootElement(), "spTree"), drawingRels, out, inFrame, deck, depth + 1);
            return;
        }
        // An embedded object (or anything else) that carries the picture it shows.
        if (const tinyxml2::XMLElement* pic = FindElement(data, "pic")) {
            PptxShape shape;
            ReadPicture(pic, rels, deck, shape);
            shape.x = frame.x;
            shape.y = frame.y;
            shape.width = frame.width;
            shape.height = frame.height;
            if (shape.name.empty()) shape.name = frame.name;
            transform.Apply(shape);
            out.push_back(std::move(shape));
        }
    }

    // The first element named `local` under `root`, depth first.
    static const tinyxml2::XMLElement* FindElement(const tinyxml2::XMLElement* root, const char* local) {
        for (auto* c = root ? root->FirstChildElement() : nullptr; c; c = c->NextSiblingElement()) {
            if (Is(c, local)) return c;
            if (auto* found = FindElement(c, local)) return found;
        }
        return nullptr;
    }

    void ReadPicture(const tinyxml2::XMLElement* e, const std::map<std::string, std::string>& rels,
                     PptxPresentation& deck, PptxShape& shape) {
        shape.kind = PptxShapeKind::Picture;
        const tinyxml2::XMLElement* nv = Child(e, "nvPicPr");
        const tinyxml2::XMLElement* props = Child(nv, "cNvPr");
        shape.name = Attr(props, "name");
        shape.altText = Attr(props, "descr");
        const tinyxml2::XMLElement* spPr = Child(e, "spPr");
        ReadXfrm(Child(spPr, "xfrm"), shape);
        if (const tinyxml2::XMLElement* ph = Child(Child(nv, "nvPr"), "ph"); ph && shape.width == 0)
            InheritPlace(PlaceholderType(ph), Attr(ph, "idx"), shape);
        if (const tinyxml2::XMLElement* geometry = Child(spPr, "prstGeom")) shape.geometry = Attr(geometry, "prst");
        Outline(spPr, Child(e, "style"), shape);
        const tinyxml2::XMLElement* blipFill = Child(e, "blipFill");
        if (const tinyxml2::XMLElement* crop = Child(blipFill, "srcRect")) {
            auto share = [&](const char* side) { return std::clamp(AttrInt(crop, side) / 100000.0, -1.0, 1.0); };
            shape.cropLeft = share("l");
            shape.cropTop = share("t");
            shape.cropRight = share("r");
            shape.cropBottom = share("b");
        }
        const auto target = rels.find(RelAttr(Child(blipFill, "blip"), "embed"));
        if (target != rels.end()) {
            shape.pictureTarget = target->second;
            if (withPictures_ && !deck.pictures.count(target->second)) {
                std::vector<uint8_t> bytes;
                if (zip_.ReadEntry(target->second, bytes)) deck.pictures[target->second] = std::move(bytes);
            }
        }
    }

    // A line or connector: its place, outline shape, line and arrowheads.
    bool ReadConnector(const tinyxml2::XMLElement* e, PptxShape& shape) const {
        shape.kind = PptxShapeKind::Connector;
        shape.name = Attr(Child(Child(e, "nvCxnSpPr"), "cNvPr"), "name");
        const tinyxml2::XMLElement* spPr = Child(e, "spPr");
        ReadXfrm(Child(spPr, "xfrm"), shape);
        if (const tinyxml2::XMLElement* geometry = Child(spPr, "prstGeom")) shape.geometry = Attr(geometry, "prst");
        if (shape.geometry.empty()) shape.geometry = "line";
        ReadCustomGeometry(Child(spPr, "custGeom"), shape);
        Outline(spPr, Child(e, "style"), shape);
        ReadLineStyle(Child(spPr, "ln"), shape);
        return !shape.line.empty();
    }

    // Offset and size, then turn and mirroring.
    static void ReadXfrm(const tinyxml2::XMLElement* xfrm, PptxShape& shape) {
        ReadPlace(xfrm, shape);
        if (!xfrm) return;
        shape.rotationDeg = AttrInt(xfrm, "rot") / 60000.0;
        shape.flipH = Attr(xfrm, "flipH") == "1" || Attr(xfrm, "flipH") == "true";
        shape.flipV = Attr(xfrm, "flipV") == "1" || Attr(xfrm, "flipV") == "true";
    }

    // a:custGeom's paths as one outline, each point 0..1 across and down the box.
    static void ReadCustomGeometry(const tinyxml2::XMLElement* custGeom, PptxShape& shape) {
        using Op = RichShape::PathPoint::Op;
        const tinyxml2::XMLElement* list = Child(custGeom, "pathLst");
        for (auto* path = Child(list, "path"); path; path = path->NextSiblingElement()) {
            if (!Is(path, "path")) continue;
            double w = static_cast<double>(AttrInt(path, "w")), h = static_cast<double>(AttrInt(path, "h"));
            if (w <= 0) w = shape.width > 0 ? static_cast<double>(shape.width) : 1.0;
            if (h <= 0) h = shape.height > 0 ? static_cast<double>(shape.height) : 1.0;
            double curX = 0, curY = 0, startX = 0, startY = 0;
            auto pointOf = [&](const tinyxml2::XMLElement* pt, double& x, double& y) {
                x = AttrInt(pt, "x") / w;
                y = AttrInt(pt, "y") / h;
            };
            auto add = [&](Op op, double x, double y) {
                RichShape::PathPoint point;
                point.op = op;
                point.x = static_cast<float>(x);
                point.y = static_cast<float>(y);
                shape.customPath.push_back(point);
            };
            for (auto* cmd = path->FirstChildElement(); cmd && shape.customPath.size() < 20000; cmd = cmd->NextSiblingElement()) {
                if (Is(cmd, "moveTo") || Is(cmd, "lnTo")) {
                    double x, y;
                    pointOf(Child(cmd, "pt"), x, y);
                    add(Is(cmd, "moveTo") ? Op::Move : Op::Line, x, y);
                    if (Is(cmd, "moveTo")) {
                        startX = x;
                        startY = y;
                    }
                    curX = x;
                    curY = y;
                } else if (Is(cmd, "cubicBezTo") || Is(cmd, "quadBezTo")) {
                    double xs[3] = {0, 0, 0}, ys[3] = {0, 0, 0};
                    int n = 0;
                    for (auto* pt = Child(cmd, "pt"); pt && n < 3; pt = pt->NextSiblingElement())
                        if (Is(pt, "pt")) pointOf(pt, xs[n], ys[n]), ++n;
                    RichShape::PathPoint point;
                    point.op = Op::Curve;
                    if (Is(cmd, "quadBezTo") && n >= 2) {   // the quadratic as a cubic
                        point.c1x = static_cast<float>(curX + 2.0 / 3.0 * (xs[0] - curX));
                        point.c1y = static_cast<float>(curY + 2.0 / 3.0 * (ys[0] - curY));
                        point.c2x = static_cast<float>(xs[1] + 2.0 / 3.0 * (xs[0] - xs[1]));
                        point.c2y = static_cast<float>(ys[1] + 2.0 / 3.0 * (ys[0] - ys[1]));
                        point.x = static_cast<float>(xs[1]);
                        point.y = static_cast<float>(ys[1]);
                    } else if (n >= 3) {
                        point.c1x = static_cast<float>(xs[0]);
                        point.c1y = static_cast<float>(ys[0]);
                        point.c2x = static_cast<float>(xs[1]);
                        point.c2y = static_cast<float>(ys[1]);
                        point.x = static_cast<float>(xs[2]);
                        point.y = static_cast<float>(ys[2]);
                    } else {
                        continue;
                    }
                    shape.customPath.push_back(point);
                    curX = point.x;
                    curY = point.y;
                } else if (Is(cmd, "arcTo")) {
                    // An arc of the ellipse wR x hR from stAng through swAng
                    // (60000ths of a degree), from the current point: as short lines.
                    const double wr = AttrInt(cmd, "wR") / w, hr = AttrInt(cmd, "hR") / h;
                    const double kRad = 3.14159265358979323846 / 180.0;
                    const double start = AttrInt(cmd, "stAng") / 60000.0 * kRad;
                    const double sweep = AttrInt(cmd, "swAng") / 60000.0 * kRad;
                    const double cx = curX - wr * std::cos(start), cy = curY - hr * std::sin(start);
                    const int steps = std::max(2, static_cast<int>(std::ceil(std::abs(sweep) / (kRad * 10))));
                    for (int k = 1; k <= steps; ++k) {
                        const double a = start + sweep * k / steps;
                        add(Op::Line, cx + wr * std::cos(a), cy + hr * std::sin(a));
                    }
                    curX = shape.customPath.back().x;
                    curY = shape.customPath.back().y;
                } else if (Is(cmd, "close")) {
                    add(Op::Close, startX, startY);
                    curX = startX;
                    curY = startY;
                }
            }
        }
    }

    // a:ln's dash and arrowheads.
    static void ReadLineStyle(const tinyxml2::XMLElement* ln, PptxShape& shape) {
        if (!ln) return;
        const std::string dash = Attr(Child(ln, "prstDash"), "val");
        if (dash == "dot" || dash == "sysDot") shape.dash = RichShape::Dash::Dotted;
        else if (!dash.empty() && dash != "solid") shape.dash = RichShape::Dash::Dashed;
        auto arrow = [&](const char* end) {
            const std::string type = Attr(Child(ln, end), "type");
            return !type.empty() && type != "none";
        };
        shape.arrowStart = arrow("headEnd");
        shape.arrowEnd = arrow("tailEnd");
    }

    // a:gradFill's stops and direction; false when it has fewer than two stops.
    bool ReadGradient(const tinyxml2::XMLElement* grad, PptxShape& shape) const {
        if (!grad) return false;
        std::vector<RichShape::GradientStop> stops;
        for (auto* gs = Child(Child(grad, "gsLst"), "gs"); gs; gs = gs->NextSiblingElement()) {
            if (!Is(gs, "gs")) continue;
            const std::string colour = Colour(gs);
            if (colour.empty()) continue;
            RichShape::GradientStop stop;
            stop.position = static_cast<float>(std::clamp(AttrInt(gs, "pos") / 100000.0, 0.0, 1.0));
            stop.color = colour;
            stops.push_back(stop);
        }
        if (stops.size() < 2) return false;
        std::stable_sort(stops.begin(), stops.end(),
                         [](const RichShape::GradientStop& a, const RichShape::GradientStop& b) { return a.position < b.position; });
        shape.gradientStops = std::move(stops);
        shape.fill = shape.gradientStops.front().color;
        if (const tinyxml2::XMLElement* lin = Child(grad, "lin")) shape.gradientAngleDeg = AttrInt(lin, "ang") / 60000.0;
        else if (Child(grad, "path")) shape.gradientRadial = true;
        else shape.gradientAngleDeg = 90;
        return true;
    }

    // ----- Tables -----

    // One part of a table style (wholeTbl, band1H, firstRow ...).
    struct TablePart {
        bool present = false;
        std::string fill, text;
        int bold = -1;
        PptxCellStyle::Border left, right, top, bottom, insideH, insideV;
        bool hasLeft = false, hasRight = false, hasTop = false, hasBottom = false, hasInsideH = false, hasInsideV = false;
    };
    struct TableStyle {
        TablePart whole, band1H, band2H, band1V, band2V, firstRow, lastRow, firstCol, lastCol;
    };

    PptxCellStyle::Border LineOf(const tinyxml2::XMLElement* holder) const {
        PptxCellStyle::Border border;
        const tinyxml2::XMLElement* ln = Child(holder, "ln");
        if (ln) {
            if (Child(ln, "noFill")) return border;
            border.color = Colour(Child(ln, "solidFill"));
            const int64_t width = AttrInt(ln, "w");
            border.widthPt = width > 0 ? width / 12700.0 : 0.75;
        } else if (const tinyxml2::XMLElement* ref = Child(holder, "lnRef")) {
            border.color = Colour(ref);
            border.widthPt = 1.0;
        }
        if (border.color.empty()) border.widthPt = 0;
        return border;
    }

    void ReadTablePart(const tinyxml2::XMLElement* e, TablePart& part) const {
        if (!e) return;
        part.present = true;
        if (const tinyxml2::XMLElement* text = Child(e, "tcTxStyle")) {
            part.text = Colour(text);
            if (part.text.empty()) part.text = Colour(Child(text, "fontRef"));
            const std::string b = Attr(text, "b");
            if (!b.empty()) part.bold = b == "on" ? 1 : 0;
        }
        const tinyxml2::XMLElement* cell = Child(e, "tcStyle");
        if (const tinyxml2::XMLElement* fill = Child(cell, "fill")) part.fill = Colour(Child(fill, "solidFill"));
        else if (const tinyxml2::XMLElement* ref = Child(cell, "fillRef")) part.fill = Colour(ref);
        const tinyxml2::XMLElement* borders = Child(cell, "tcBdr");
        auto side = [&](const char* name, PptxCellStyle::Border& to, bool& has) {
            if (const tinyxml2::XMLElement* b = Child(borders, name)) {
                to = LineOf(b);
                has = true;
            }
        };
        side("left", part.left, part.hasLeft);
        side("right", part.right, part.hasRight);
        side("top", part.top, part.hasTop);
        side("bottom", part.bottom, part.hasBottom);
        side("insideH", part.insideH, part.hasInsideH);
        side("insideV", part.insideV, part.hasInsideV);
    }

    // PowerPoint's Medium Style 2 in `accent`: the table look a deck gets by default.
    TableStyle MediumStyle2(const std::string& accent) const {
        TableStyle style;
        const std::string base = SchemeColour(accent).empty() ? "4472C4" : SchemeColour(accent);
        const std::string white = SchemeColour("lt1").empty() ? "FFFFFF" : SchemeColour("lt1");
        auto tint = [&](double keep) {   // toward white: `keep` of the colour stays
            const uint32_t rgb = ParseHex(base);
            auto c = [&](int shift) {
                const double v = ((rgb >> shift) & 0xFF) / 255.0;
                return static_cast<uint32_t>(std::lround((1 - (1 - v) * keep) * 255.0));
            };
            return ToHex((c(16) << 16) | (c(8) << 8) | c(0));
        };
        PptxCellStyle::Border line{white, 1.0};
        style.whole.present = true;
        style.whole.fill = tint(0.2);
        style.whole.text = SchemeColour("dk1");
        style.whole.left = style.whole.right = style.whole.top = style.whole.bottom = line;
        style.whole.insideH = style.whole.insideV = line;
        style.whole.hasLeft = style.whole.hasRight = style.whole.hasTop = style.whole.hasBottom = true;
        style.whole.hasInsideH = style.whole.hasInsideV = true;
        style.band1H.present = style.band1V.present = true;
        style.band1H.fill = style.band1V.fill = tint(0.4);
        for (TablePart* edge : {&style.firstRow, &style.lastRow, &style.firstCol, &style.lastCol}) {
            edge->present = true;
            edge->fill = base;
            edge->text = white;
            edge->bold = 1;
        }
        return style;
    }

    TableStyle ResolveTableStyle(const std::string& id) {
        if (!tableStylesRead_) {
            tableStylesRead_ = true;
            if (!LoadXml("ppt/tableStyles.xml", tableStyles_)) tableStyles_.Clear();
        }
        const tinyxml2::XMLElement* list = tableStyles_.RootElement();
        const std::string wanted = id.empty() ? Attr(list, "def") : id;
        for (auto* st = Child(list, "tblStyle"); st; st = st->NextSiblingElement()) {
            if (!Is(st, "tblStyle") || Attr(st, "styleId") != wanted) continue;
            TableStyle style;
            ReadTablePart(Child(st, "wholeTbl"), style.whole);
            ReadTablePart(Child(st, "band1H"), style.band1H);
            ReadTablePart(Child(st, "band2H"), style.band2H);
            ReadTablePart(Child(st, "band1V"), style.band1V);
            ReadTablePart(Child(st, "band2V"), style.band2V);
            ReadTablePart(Child(st, "firstRow"), style.firstRow);
            ReadTablePart(Child(st, "lastRow"), style.lastRow);
            ReadTablePart(Child(st, "firstCol"), style.firstCol);
            ReadTablePart(Child(st, "lastCol"), style.lastCol);
            return style;
        }
        // Not in the package: PowerPoint's built-in Medium Style 2, by its accent.
        static const std::map<std::string, std::string> accents = {
            {"{5C22544A-7EE6-4342-B048-85BDC9FD1C3A}", "accent1"}, {"{21E4AEA4-8DFA-4A89-87EB-49C32662AFE8}", "accent2"},
            {"{F5AB1C69-6EDB-4FF4-983F-18BD219EF322}", "accent3"}, {"{00A15C55-8517-42AA-B614-E9B94910E393}", "accent4"},
            {"{7DF18680-E054-41AD-8BC1-D1AEF772440D}", "accent5"}, {"{93296810-A885-4BE3-A3E7-6D5BEEA58F35}", "accent6"},
            {"{073A0DAA-6AF3-43AB-8588-CEC1D06C72B9}", "dk1"}};
        const auto accent = accents.find(wanted);
        return MediumStyle2(accent == accents.end() ? "accent1" : accent->second);
    }

    void ReadTable(const tinyxml2::XMLElement* table, PptxShape& shape) {
        const tinyxml2::XMLElement* tblPr = Child(table, "tblPr");
        auto flag = [&](const char* name) { return Attr(tblPr, name) == "1" || Attr(tblPr, name) == "true"; };
        const bool firstRow = flag("firstRow"), lastRow = flag("lastRow"), firstCol = flag("firstCol"),
                   lastCol = flag("lastCol"), bandRow = flag("bandRow"), bandCol = flag("bandCol");
        const tinyxml2::XMLElement* styleId = Child(tblPr, "tableStyleId");
        const bool styled = styleId || tblPr;
        const TableStyle style = ResolveTableStyle(styleId && styleId->GetText() ? styleId->GetText() : "");

        for (auto* col = Child(Child(table, "tblGrid"), "gridCol"); col; col = col->NextSiblingElement())
            if (Is(col, "gridCol")) shape.columnWidths.push_back(AttrInt(col, "w"));
        std::vector<const tinyxml2::XMLElement*> rows;
        for (auto* tr = Child(table, "tr"); tr; tr = tr->NextSiblingElement())
            if (Is(tr, "tr")) rows.push_back(tr);
        const size_t rowCount = rows.size();
        for (size_t r = 0; r < rowCount; ++r) {
            const tinyxml2::XMLElement* tr = rows[r];
            shape.rowHeights.push_back(AttrInt(tr, "h"));
            std::vector<std::vector<PptxParagraph>> row;
            std::vector<PptxCellStyle> looks;
            size_t columnCount = shape.columnWidths.size();
            size_t c = 0;
            for (auto* tc = Child(tr, "tc"); tc; tc = tc->NextSiblingElement()) {
                if (!Is(tc, "tc")) continue;
                PptxCellStyle look;
                // The style's parts, the narrower over the wider: the whole
                // table, its bands, its edge columns, its edge rows.
                const bool isFirstRow = firstRow && r == 0, isLastRow = lastRow && r + 1 == rowCount;
                const bool isFirstCol = firstCol && c == 0, isLastCol = lastCol && columnCount > 0 && c + 1 == columnCount;
                const size_t bandIndexRow = r - (firstRow ? 1 : 0), bandIndexCol = c - (firstCol ? 1 : 0);
                std::vector<const TablePart*> parts;
                if (styled) {
                    parts.push_back(&style.whole);
                    if (bandCol && !isFirstCol && !isLastCol) parts.push_back(bandIndexCol % 2 == 0 ? &style.band1V : &style.band2V);
                    if (bandRow && !isFirstRow && !isLastRow) parts.push_back(bandIndexRow % 2 == 0 ? &style.band1H : &style.band2H);
                    if (isFirstCol) parts.push_back(&style.firstCol);
                    if (isLastCol) parts.push_back(&style.lastCol);
                    if (isFirstRow) parts.push_back(&style.firstRow);
                    if (isLastRow) parts.push_back(&style.lastRow);
                }
                for (const TablePart* part : parts) {
                    if (!part->present) continue;
                    if (!part->fill.empty()) look.fill = part->fill;
                    if (!part->text.empty()) look.textColor = part->text;
                    if (part->bold >= 0) look.bold = part->bold == 1;
                    if (part->hasLeft || part->hasInsideV) look.left = c == 0 ? (part->hasLeft ? part->left : look.left)
                                                                               : (part->hasInsideV ? part->insideV : look.left);
                    if (part->hasRight || part->hasInsideV) look.right = columnCount > 0 && c + 1 >= columnCount
                                                                             ? (part->hasRight ? part->right : look.right)
                                                                             : (part->hasInsideV ? part->insideV : look.right);
                    if (part->hasTop || part->hasInsideH) look.top = r == 0 ? (part->hasTop ? part->top : look.top)
                                                                          : (part->hasInsideH ? part->insideH : look.top);
                    if (part->hasBottom || part->hasInsideH) look.bottom = r + 1 == rowCount
                                                                               ? (part->hasBottom ? part->bottom : look.bottom)
                                                                               : (part->hasInsideH ? part->insideH : look.bottom);
                }
                // Then the cell's own.
                look.gridSpan = static_cast<int>(std::clamp<int64_t>(AttrInt(tc, "gridSpan"), 1, 1000));
                look.rowSpan = static_cast<int>(std::clamp<int64_t>(AttrInt(tc, "rowSpan"), 1, 1000));
                look.mergedAway = Attr(tc, "hMerge") == "1" || Attr(tc, "hMerge") == "true" ||
                                  Attr(tc, "vMerge") == "1" || Attr(tc, "vMerge") == "true";
                if (const tinyxml2::XMLElement* tcPr = Child(tc, "tcPr")) {
                    if (Child(tcPr, "noFill")) look.fill.clear();
                    if (const tinyxml2::XMLElement* solid = Child(tcPr, "solidFill")) look.fill = Colour(solid);
                    auto edge = [&](const char* name, PptxCellStyle::Border& to) {
                        if (const tinyxml2::XMLElement* ln = Child(tcPr, name)) {
                            PptxCellStyle::Border b;
                            if (Child(ln, "noFill")) {
                                to = b;
                                return;
                            }
                            b.color = Colour(Child(ln, "solidFill"));
                            const int64_t width = AttrInt(ln, "w");
                            b.widthPt = b.color.empty() ? 0 : (width > 0 ? width / 12700.0 : 0.75);
                            if (!b.color.empty()) to = b;
                        }
                    };
                    edge("lnL", look.left);
                    edge("lnR", look.right);
                    edge("lnT", look.top);
                    edge("lnB", look.bottom);
                    auto inset = [&](const char* name, int64_t& to) {
                        if (!Attr(tcPr, name).empty()) to = std::max<int64_t>(0, AttrInt(tcPr, name));
                    };
                    inset("marL", look.insetLeft);
                    inset("marR", look.insetRight);
                    inset("marT", look.insetTop);
                    inset("marB", look.insetBottom);
                    const int anchor = Anchor(tcPr);
                    if (anchor >= 0) look.anchor = static_cast<PptxAnchor>(anchor);
                }
                std::vector<PptxParagraph> cell;
                ReadText(Child(tc, "txBody"), false, cell, nullptr, look.textColor);
                // What the table style says for the text, where the run says nothing.
                for (PptxParagraph& p : cell)
                    for (PptxRun& run : p.runs) {
                        if (look.bold) run.bold = true;
                        if (run.color.empty()) run.color = look.textColor;
                    }
                row.push_back(std::move(cell));
                looks.push_back(look);
                ++c;
                columnCount = std::max(columnCount, c);
            }
            shape.rows.push_back(std::move(row));
            shape.cellStyles.push_back(std::move(looks));
        }
    }

    static void ReadPlace(const tinyxml2::XMLElement* xfrm, PptxShape& shape) {
        if (const tinyxml2::XMLElement* off = Child(xfrm, "off")) {
            shape.x = AttrInt(off, "x");
            shape.y = AttrInt(off, "y");
        }
        if (const tinyxml2::XMLElement* ext = Child(xfrm, "ext")) {
            shape.width = AttrInt(ext, "cx");
            shape.height = AttrInt(ext, "cy");
        }
    }

    // A placeholder that names no place takes its layout's, then its master's.
    void InheritPlace(const std::string& type, const std::string& idx, PptxShape& shape) const {
        for (const Template* t : {layout_, master_ ? &master_->places : nullptr}) {
            if (!t) continue;
            const PlaceholderStyle* ph = FindPlaceholder(*t, type, idx);
            if (!ph || !ph->hasPlace) continue;
            shape.x = ph->x;
            shape.y = ph->y;
            shape.width = ph->width;
            shape.height = ph->height;
            shape.placeInherited = true;
            return;
        }
    }

    // A shape with text, or one with a fill or outline of its own; false for
    // slide furniture (date, footer, slide number) and for a shape with
    // neither.
    bool ReadShape(const tinyxml2::XMLElement* sp, PptxShape& shape) const {
        const tinyxml2::XMLElement* nv = Child(sp, "nvSpPr");
        shape.name = Attr(Child(nv, "cNvPr"), "name");
        const tinyxml2::XMLElement* ph = Child(Child(nv, "nvPr"), "ph");
        std::string idx;
        if (ph) {
            const std::string type = Attr(ph, "type");
            if (type == "dt" || type == "ftr" || type == "sldNum" || type == "hdr" || type == "sldImg")
                return false;
            shape.kind = type == "title" || type == "ctrTitle" ? PptxShapeKind::Title
                       : type == "subTitle"                    ? PptxShapeKind::Subtitle
                       : PptxShapeKind::Body;   // body, obj or no type: content
            shape.placeholder = PlaceholderType(ph);
            idx = Attr(ph, "idx");
        }
        const tinyxml2::XMLElement* spPr = Child(sp, "spPr");
        const tinyxml2::XMLElement* style = Child(sp, "style");
        ReadXfrm(Child(spPr, "xfrm"), shape);
        if (ph && shape.width == 0 && shape.height == 0) InheritPlace(shape.placeholder, idx, shape);
        if (const tinyxml2::XMLElement* geometry = Child(spPr, "prstGeom")) shape.geometry = Attr(geometry, "prst");
        ReadCustomGeometry(Child(spPr, "custGeom"), shape);
        if (!ReadGradient(Child(spPr, "gradFill"), shape)) shape.fill = Fill(spPr, style);
        Outline(spPr, style, shape);
        ReadLineStyle(Child(spPr, "ln"), shape);

        const tinyxml2::XMLElement* body = Child(sp, "txBody");
        const tinyxml2::XMLElement* bodyPr = Child(body, "bodyPr");
        auto inset = [&](const char* name, int64_t& to) {
            if (!Attr(bodyPr, name).empty()) to = std::max<int64_t>(0, AttrInt(bodyPr, name));
        };
        inset("lIns", shape.insetLeft);
        inset("rIns", shape.insetRight);
        inset("tIns", shape.insetTop);
        inset("bIns", shape.insetBottom);
        shape.wrap = Attr(bodyPr, "wrap") != "none";
        const std::string vert = Attr(bodyPr, "vert");
        shape.textRotation = vert == "vert" || vert == "eaVert" || vert == "wordArtVertRtl" ? 90 : vert == "vert270" ? 270 : 0;
        int anchor = Anchor(bodyPr);
        const PlaceholderStyle* fromLayout = ph && layout_ ? FindPlaceholder(*layout_, shape.placeholder, idx) : nullptr;
        const PlaceholderStyle* fromMaster =
            ph && master_ ? FindPlaceholder(master_->places, shape.placeholder, idx) : nullptr;
        if (anchor < 0 && fromLayout) anchor = fromLayout->anchor;
        if (anchor < 0 && fromMaster) anchor = fromMaster->anchor;
        if (anchor >= 0) shape.anchor = static_cast<PptxAnchor>(anchor);
        if (const tinyxml2::XMLElement* fit = Child(bodyPr, "normAutofit")) {
            if (const int64_t scale = AttrInt(fit, "fontScale"); scale > 0) shape.fontScale = scale / 100000.0f;
        }
        // The style chain its text resolves through, nearest first.
        ListStyle own;
        ReadListStyle(Child(body, "lstStyle"), own);
        std::vector<const ListStyle*> chain = {&own};
        if (fromLayout) chain.push_back(&fromLayout->list);
        if (fromMaster) chain.push_back(&fromMaster->list);
        if (master_) {
            if (shape.kind == PptxShapeKind::Title) chain.push_back(&master_->titleStyle);
            else if (ph) chain.push_back(&master_->bodyStyle);
        }
        if (!ph) chain.push_back(&defaultTextStyle_);
        const tinyxml2::XMLElement* fontRef = Child(style, "fontRef");
        ReadText(body, shape.kind == PptxShapeKind::Body, shape.paragraphs, &chain, fontRef ? Colour(fontRef) : "");
        if (!shape.fill.empty() || !shape.line.empty()) return true;
        for (const PptxParagraph& p : shape.paragraphs)
            if (!p.Text().empty()) return true;
        return false;
    }

    // The paragraphs of a txBody; `chain` (nearest first) resolves what the
    // runs and paragraphs leave unsaid, `shapeColour` comes before the
    // master's text styles (a style's fontRef).
    void ReadText(const tinyxml2::XMLElement* body, bool bulletedByDefault, std::vector<PptxParagraph>& out,
                  const std::vector<const ListStyle*>* chain, const std::string& shapeColour) const {
        for (auto* p = Child(body, "p"); p; p = p->NextSiblingElement()) {
            if (!Is(p, "p")) continue;
            PptxParagraph para;
            const tinyxml2::XMLElement* pPr = Child(p, "pPr");
            para.level = static_cast<int>(std::clamp<int64_t>(AttrInt(pPr, "lvl"), 0, 8));

            // What the paragraph's level inherits: its own pPr, then the chain.
            LevelStyle inherited;
            ReadLevel(pPr, inherited);
            if (chain && !chain->empty()) {
                for (const ListStyle* list : *chain) {
                    const LevelStyle& level = list->levels[para.level];
                    inherited.Inherit(level);
                    // The colour: the lists before the last, then the shape's style, then the last
                    // (the master's text styles or the deck's default).
                    if (inherited.color.empty() && list != chain->back()) inherited.color = level.color;
                }
                if (inherited.color.empty()) inherited.color = shapeColour;
                if (inherited.color.empty()) inherited.color = chain->back()->levels[para.level].color;
            }
            if (inherited.align >= 0) para.align = static_cast<PptxAlign>(inherited.align);
            inherited.font = Typeface(inherited.font.empty() ? std::string("+mn-lt") : inherited.font);

            para.bullet = inherited.bullet < 0 ? bulletedByDefault : inherited.bullet > 0;
            para.numbered = inherited.bullet == 2;
            if (para.bullet) {
                para.bulletChar = inherited.bullet == 1 ? inherited.bulletChar : "";
                para.bulletFont = Typeface(inherited.bulletFont);
                para.bulletColor = inherited.bulletColor;
                para.bulletSizePercent = inherited.bulletSizePercent;
            }
            if (para.numbered) {
                para.numberScheme = inherited.numberScheme.empty() ? "arabicPeriod" : inherited.numberScheme;
                para.numberStartAt = inherited.numberStartAt > 0 ? inherited.numberStartAt : 1;
            }
            para.spaceBefore = inherited.spaceBefore;
            para.spaceAfter = inherited.spaceAfter;
            para.lineSpacing = inherited.lineSpacing;
            para.marginLeft = inherited.marginLeft;
            para.indent = inherited.hasIndent ? inherited.indent : 0;

            ReadRuns(p, inherited, para.runs);
            out.push_back(std::move(para));
        }
    }

    // The text of an Office Math element (m:oMathPara, m:oMath): its m:t
    // runs in order - the equation as it reads, not as it is set.
    static void MathText(const tinyxml2::XMLElement* e, std::string& out) {
        for (auto* c = e->FirstChildElement(); c; c = c->NextSiblingElement()) {
            if (Is(c, "t")) {
                if (c->GetText()) out += c->GetText();
            } else {
                MathText(c, out);
            }
        }
    }

    // A paragraph's runs; `inherited` gives a run the size and colour it
    // leaves unsaid.
    void ReadRuns(const tinyxml2::XMLElement* parent, const LevelStyle& inherited, std::vector<PptxRun>& runs) const {
        for (auto* r = parent->FirstChildElement(); r; r = r->NextSiblingElement()) {
            if (Is(r, "br")) {
                PptxRun lineBreak{"\n", false, false, false, inherited.sizePt, inherited.color};
                lineBreak.font = inherited.font;
                runs.push_back(std::move(lineBreak));
            } else if (Is(r, "r") || Is(r, "fld")) {
                PptxRun run;
                const tinyxml2::XMLElement* t = Child(r, "t");
                run.text = t && t->GetText() ? t->GetText() : "";
                const tinyxml2::XMLElement* rPr = Child(r, "rPr");
                const std::string b = Attr(rPr, "b"), i = Attr(rPr, "i");
                run.bold = b.empty() ? inherited.bold == 1 : (b == "1" || b == "true");
                run.italic = i.empty() ? inherited.italic == 1 : (i == "1" || i == "true");
                const std::string u = Attr(rPr, "u");
                run.underline = !u.empty() && u != "none";
                const int64_t sz = AttrInt(rPr, "sz");
                run.sizePt = sz > 0 ? sz / 100.0f : inherited.sizePt;
                const std::string colour = Colour(Child(rPr, "solidFill"));
                run.color = colour.empty() ? inherited.color : colour;
                const std::string face = Attr(Child(rPr, "latin"), "typeface");
                run.font = face.empty() ? inherited.font : Typeface(face);
                const std::string strike = Attr(rPr, "strike");
                run.strike = !strike.empty() && strike != "noStrike";
                run.baseline = static_cast<int>(std::clamp<int64_t>(AttrInt(rPr, "baseline") / 1000, -100, 100));
                const std::string cap = Attr(rPr, "cap");
                run.allCaps = cap == "all";
                run.smallCaps = cap == "small";
                run.highlight = Colour(Child(rPr, "highlight"));
                const std::string link = RelAttr(Child(rPr, "hlinkClick"), "id");
                if (!link.empty()) {
                    if (const auto e = externalLinks_.find(link); e != externalLinks_.end()) run.hyperlink = e->second;
                    else if (const auto s = slideLinks_.find(link); s != slideLinks_.end()) run.hyperlink = s->second;
                }
                if (!run.text.empty()) runs.push_back(std::move(run));
            } else if (Is(r, "m")) {
                // An equation (a14:m), as PowerPoint 2010 and later store it.
                PptxRun run;
                MathText(r, run.text);
                run.sizePt = inherited.sizePt;
                run.color = inherited.color;
                run.font = "Cambria Math";
                if (!run.text.empty()) runs.push_back(std::move(run));
            } else if (Is(r, "AlternateContent")) {
                // Inside a paragraph too: the first Choice that gives text,
                // else the Fallback (an older reader's runs for an equation).
                bool chosen = false;
                for (auto* branch = r->FirstChildElement(); branch && !chosen; branch = branch->NextSiblingElement()) {
                    if (!Is(branch, "Choice")) continue;
                    std::vector<PptxRun> choice;
                    ReadRuns(branch, inherited, choice);
                    if (choice.empty()) continue;
                    runs.insert(runs.end(), choice.begin(), choice.end());
                    chosen = true;
                }
                if (!chosen)
                    if (const tinyxml2::XMLElement* fallback = Child(r, "Fallback")) ReadRuns(fallback, inherited, runs);
            }
        }
    }
};

std::vector<RichTextRun> RichRuns(const PptxParagraph& p) {
    std::vector<RichTextRun> out;
    bool breakNext = false;
    for (const PptxRun& run : p.runs) {
        if (run.text == "\n") {
            breakNext = true;
            continue;
        }
        RichTextRun r;
        r.text = run.text;
        r.bold = run.bold;
        r.italic = run.italic;
        r.underline = run.underline;
        r.strikethrough = run.strike;
        r.superscript = run.baseline > 0;
        r.subscript = run.baseline < 0;
        if (!run.hyperlink.empty() && run.hyperlink[0] != '#') r.linkTarget = run.hyperlink;
        r.lineBreakBefore = breakNext;
        breakNext = false;
        out.push_back(std::move(r));
    }
    return out;
}

RichDocBlock TextBlock(RichBlockType type, const std::string& text, bool italic = false) {
    RichDocBlock block;
    block.type = type;
    RichTextRun run;
    run.text = text;
    run.italic = italic;
    block.runs.push_back(std::move(run));
    return block;
}

} // namespace

std::string PptxNumberLabel(const std::string& scheme, int number) {
    number = std::max(1, number);
    std::string digits;
    auto roman = [](int n, bool upper) {
        static const std::pair<int, const char*> table[] = {{1000, "m"}, {900, "cm"}, {500, "d"}, {400, "cd"}, {100, "c"},
                                                            {90, "xc"}, {50, "l"}, {40, "xl"}, {10, "x"}, {9, "ix"},
                                                            {5, "v"}, {4, "iv"}, {1, "i"}};
        std::string out;
        n = std::min(n, 3999);
        for (const auto& [value, letters] : table)
            while (n >= value) {
                out += letters;
                n -= value;
            }
        if (upper) std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return std::toupper(c); });
        return out;
    };
    auto alpha = [](int n, bool upper) {   // a..z, then aa, bb ... as PowerPoint counts
        const char letter = static_cast<char>((upper ? 'A' : 'a') + (n - 1) % 26);
        return std::string(static_cast<size_t>((n - 1) / 26 + 1), letter);
    };
    if (scheme.rfind("alphaLc", 0) == 0) digits = alpha(number, false);
    else if (scheme.rfind("alphaUc", 0) == 0) digits = alpha(number, true);
    else if (scheme.rfind("romanLc", 0) == 0) digits = roman(number, false);
    else if (scheme.rfind("romanUc", 0) == 0) digits = roman(number, true);
    else digits = std::to_string(number);
    auto endsWith = [&](const char* tail) {
        const size_t n = std::strlen(tail);
        return scheme.size() >= n && scheme.compare(scheme.size() - n, n, tail) == 0;
    };
    if (endsWith("ParenBoth")) return "(" + digits + ")";
    if (endsWith("ParenR")) return digits + ")";
    if (endsWith("Plain")) return digits;
    if (endsWith("Minus")) return digits + " -";
    return digits + ".";
}

std::string PptxParagraph::Text() const {
    std::string out;
    for (const PptxRun& run : runs) out += run.text;
    return out;
}

bool IsPptxExtension(const std::string& extensionOrPath) {
    const size_t dot = extensionOrPath.rfind('.');
    std::string ext = dot == std::string::npos ? extensionOrPath : extensionOrPath.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext == "pptx" || ext == "pptm" || ext == "ppsx" || ext == "ppsm" || ext == "potx" || ext == "potm";
}

bool IsPptxPackage(const std::string& filePath) {
    UCZipPackageReader zip;
    return zip.Open(filePath) && zip.HasEntry("ppt/presentation.xml");
}

bool ReadPptxPresentationFromPackage(std::vector<uint8_t> package, PptxPresentation& out,
                                     std::string& error, bool withPictures) {
    out = PptxPresentation{};
    error.clear();
    UCZipPackageReader zip;
    if (!zip.OpenFromMemory(std::move(package))) {
        error = "The file is not a valid PowerPoint presentation (.pptx)";
        return false;
    }
    PptxReader reader(zip, withPictures);
    return reader.Read(out, error);
}

bool ReadPptxPresentation(const std::string& filePath, PptxPresentation& out, std::string& error,
                          const std::string& password, bool withPictures) {
    out = PptxPresentation{};
    error.clear();
    // A password-protected deck is a compound file holding the package
    // encrypted (UltraCanvasOfficeEncryption.h), not a ZIP.
    std::vector<uint8_t> package;
    switch (DecryptOfficePackageFile(filePath, password, package, error)) {
        case OfficeDecryptStatus::NotEncrypted: {
            UCZipPackageReader zip;
            if (!zip.Open(filePath)) {
                error = "The file is not a valid PowerPoint presentation (.pptx): " + filePath;
                return false;
            }
            PptxReader reader(zip, withPictures);
            if (!reader.Read(out, error)) {
                error += ": " + filePath;
                return false;
            }
            return true;
        }
        case OfficeDecryptStatus::Decrypted:
            if (!ReadPptxPresentationFromPackage(std::move(package), out, error, withPictures)) {
                error += ": " + filePath;
                return false;
            }
            return true;
        default:
            error += ": " + filePath;
            return false;
    }
}

UCRichDocument PptxPresentationToRichDocument(const PptxPresentation& presentation) {
    UCRichDocument doc;
    for (const PptxSlide& slide : presentation.slides) {
        if (!doc.blocks.empty()) {
            RichDocBlock rule;
            rule.type = RichBlockType::HorizontalRule;
            doc.blocks.push_back(std::move(rule));
        }
        RichDocBlock heading = TextBlock(RichBlockType::Heading,
            std::to_string(slide.number) + ". " + (slide.title.empty() ? "Slide " + std::to_string(slide.number)
                                                                        : slide.title) +
            (slide.hidden ? " (hidden)" : ""));
        heading.headingLevel = 1;
        doc.blocks.push_back(std::move(heading));

        for (const PptxShape& shape : slide.shapes) {
            switch (shape.kind) {
                case PptxShapeKind::Title:
                    break;   // the heading
                case PptxShapeKind::Picture:
                    doc.blocks.push_back(TextBlock(RichBlockType::Paragraph,
                        "[Picture" + (shape.altText.empty() ? std::string() : ": " + shape.altText) + "]",
                        true));
                    break;
                case PptxShapeKind::Chart:
                    doc.blocks.push_back(TextBlock(RichBlockType::Paragraph,
                        "[Chart" + (shape.chart.title.empty() ? std::string() : ": " + shape.chart.title) + "]",
                        true));
                    break;
                case PptxShapeKind::Connector:
                    break;   // a line says nothing in words
                case PptxShapeKind::Table: {
                    RichDocBlock table;
                    table.type = RichBlockType::Table;
                    for (const auto& row : shape.rows) {
                        RichTableRow out;
                        for (const auto& cell : row) {
                            RichTableCell c;
                            for (const PptxParagraph& p : cell) {
                                std::vector<RichTextRun> runs = RichRuns(p);
                                if (!c.runs.empty() && !runs.empty()) runs.front().lineBreakBefore = true;
                                c.runs.insert(c.runs.end(), runs.begin(), runs.end());
                            }
                            out.cells.push_back(std::move(c));
                        }
                        table.tableRows.push_back(std::move(out));
                    }
                    if (!table.tableRows.empty()) table.tableRows.front().header = true;
                    doc.blocks.push_back(std::move(table));
                    break;
                }
                default:
                    for (const PptxParagraph& p : shape.paragraphs) {
                        std::vector<RichTextRun> runs = RichRuns(p);
                        if (runs.empty()) continue;
                        RichDocBlock block;
                        block.type = p.bullet ? RichBlockType::ListItem : RichBlockType::Paragraph;
                        block.orderedList = p.numbered;
                        block.listLevel = p.level;
                        if (shape.kind == PptxShapeKind::Subtitle)
                            for (RichTextRun& r : runs) r.italic = true;
                        block.runs = std::move(runs);
                        doc.blocks.push_back(std::move(block));
                    }
                    break;
            }
        }

        bool first = true;
        for (const PptxParagraph& p : slide.notes) {
            std::vector<RichTextRun> runs = RichRuns(p);
            if (runs.empty()) continue;
            RichDocBlock quote;
            quote.type = RichBlockType::BlockQuote;
            if (first) {
                RichTextRun label;
                label.text = "Notes: ";
                label.bold = true;
                quote.runs.push_back(std::move(label));
                first = false;
            }
            quote.runs.insert(quote.runs.end(), runs.begin(), runs.end());
            doc.blocks.push_back(std::move(quote));
        }
    }
    return doc;
}

} // namespace UltraCanvas
