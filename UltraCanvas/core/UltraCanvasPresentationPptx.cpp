// core/UltraCanvasPresentationPptx.cpp
// PowerPoint (.pptx) reader - see UltraCanvasPresentationPptx.h.
// Version: 1.2.0 - mc:AlternateContent read: its first Choice that gives anything, else its
//                  Fallback - around shapes and around runs; an equation (a14:m) gives its text
// Version: 1.1.0 - the layout's and master's places, text styles and backgrounds, the theme's
//                  colours, fills and outlines, a group's shapes placed on the slide, the pictures
// Version: 1.0.0
// Last Modified: 2026-11-18
// Author: UltraCanvas Framework
#include "UltraCanvasPresentationPptx.h"
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
                                                     std::map<std::string, std::string>* byType = nullptr) {
        std::map<std::string, std::string> out;
        const size_t slash = part.rfind('/');
        const std::string relsPart = part.substr(0, slash + 1) + "_rels/" + part.substr(slash + 1) + ".rels";
        tinyxml2::XMLDocument doc;
        if (!LoadXml(relsPart, doc)) return out;
        for (auto* r = doc.RootElement()->FirstChildElement(); r; r = r->NextSiblingElement()) {
            if (!Is(r, "Relationship") || Attr(r, "TargetMode") == "External") continue;
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
        }
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
        const std::map<std::string, std::string> rels = Relationships(part, &byType);

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
                  std::vector<PptxShape>& out, const Transform& transform, PptxPresentation& deck) {
        for (auto* e = tree ? tree->FirstChildElement() : nullptr; e; e = e->NextSiblingElement()) {
            if (Is(e, "sp")) {
                PptxShape shape;
                if (ReadShape(e, shape)) {
                    if (!shape.placeInherited) transform.Apply(shape);
                    out.push_back(std::move(shape));
                }
            } else if (Is(e, "grpSp")) {
                ReadTree(e, rels, out, GroupTransform(Child(e, "grpSpPr"), transform), deck);
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
                    ReadTree(branch, rels, shapes, transform, deck);
                    if (shapes.empty()) continue;
                    for (PptxShape& shape : shapes) out.push_back(std::move(shape));
                    chosen = true;
                }
                if (!chosen) ReadTree(Child(e, "Fallback"), rels, out, transform, deck);
            } else if (Is(e, "graphicFrame")) {
                const tinyxml2::XMLElement* table = Child(Child(Child(e, "graphic"), "graphicData"), "tbl");
                if (!table) continue;
                PptxShape shape;
                shape.kind = PptxShapeKind::Table;
                shape.name = Attr(Child(Child(e, "nvGraphicFramePr"), "cNvPr"), "name");
                ReadPlace(Child(e, "xfrm"), shape);
                transform.Apply(shape);
                for (auto* col = Child(Child(table, "tblGrid"), "gridCol"); col; col = col->NextSiblingElement())
                    if (Is(col, "gridCol")) shape.columnWidths.push_back(AttrInt(col, "w"));
                for (auto* tr = Child(table, "tr"); tr; tr = tr->NextSiblingElement()) {
                    if (!Is(tr, "tr")) continue;
                    shape.rowHeights.push_back(AttrInt(tr, "h"));
                    std::vector<std::vector<PptxParagraph>> row;
                    for (auto* tc = Child(tr, "tc"); tc; tc = tc->NextSiblingElement()) {
                        if (!Is(tc, "tc")) continue;
                        std::vector<PptxParagraph> cell;
                        ReadText(Child(tc, "txBody"), false, cell, nullptr, "");
                        row.push_back(std::move(cell));
                    }
                    shape.rows.push_back(std::move(row));
                }
                out.push_back(std::move(shape));
            } else if (Is(e, "pic")) {
                PptxShape shape;
                shape.kind = PptxShapeKind::Picture;
                const tinyxml2::XMLElement* nv = Child(e, "nvPicPr");
                const tinyxml2::XMLElement* props = Child(nv, "cNvPr");
                shape.name = Attr(props, "name");
                shape.altText = Attr(props, "descr");
                ReadPlace(Child(Child(e, "spPr"), "xfrm"), shape);
                if (const tinyxml2::XMLElement* ph = Child(Child(nv, "nvPr"), "ph"); ph && shape.width == 0)
                    InheritPlace(PlaceholderType(ph), Attr(ph, "idx"), shape);
                if (!shape.placeInherited) transform.Apply(shape);
                const auto target = rels.find(RelAttr(Child(Child(e, "blipFill"), "blip"), "embed"));
                if (target != rels.end()) {
                    shape.pictureTarget = target->second;
                    if (withPictures_ && !deck.pictures.count(target->second)) {
                        std::vector<uint8_t> bytes;
                        if (zip_.ReadEntry(target->second, bytes)) deck.pictures[target->second] = std::move(bytes);
                    }
                }
                out.push_back(std::move(shape));
            }
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
        ReadPlace(Child(spPr, "xfrm"), shape);
        if (ph && shape.width == 0 && shape.height == 0) InheritPlace(shape.placeholder, idx, shape);
        if (const tinyxml2::XMLElement* geometry = Child(spPr, "prstGeom")) shape.geometry = Attr(geometry, "prst");
        shape.fill = Fill(spPr, style);
        Outline(spPr, style, shape);

        const tinyxml2::XMLElement* body = Child(sp, "txBody");
        const tinyxml2::XMLElement* bodyPr = Child(body, "bodyPr");
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
            para.bullet = bulletedByDefault;
            if (Child(pPr, "buNone")) para.bullet = false;
            if (Child(pPr, "buChar")) para.bullet = true;
            if (Child(pPr, "buAutoNum")) para.bullet = para.numbered = true;

            // What the paragraph's level inherits.
            LevelStyle inherited;
            ReadLevel(pPr, inherited);   // its own algn
            if (chain && !chain->empty()) {
                for (const ListStyle* list : *chain) {
                    const LevelStyle& level = list->levels[para.level];
                    if (inherited.align < 0) inherited.align = level.align;
                    if (inherited.sizePt <= 0) inherited.sizePt = level.sizePt;
                    // The colour: the lists before the last, then the shape's style, then the last
                    // (the master's text styles or the deck's default).
                    if (inherited.color.empty() && list != chain->back()) inherited.color = level.color;
                }
                if (inherited.color.empty()) inherited.color = shapeColour;
                if (inherited.color.empty()) inherited.color = chain->back()->levels[para.level].color;
            }
            if (inherited.align >= 0) para.align = static_cast<PptxAlign>(inherited.align);

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
                runs.push_back({"\n", false, false, false, inherited.sizePt, inherited.color});
            } else if (Is(r, "r") || Is(r, "fld")) {
                PptxRun run;
                const tinyxml2::XMLElement* t = Child(r, "t");
                run.text = t && t->GetText() ? t->GetText() : "";
                const tinyxml2::XMLElement* rPr = Child(r, "rPr");
                run.bold = Attr(rPr, "b") == "1" || Attr(rPr, "b") == "true";
                run.italic = Attr(rPr, "i") == "1" || Attr(rPr, "i") == "true";
                const std::string u = Attr(rPr, "u");
                run.underline = !u.empty() && u != "none";
                const int64_t sz = AttrInt(rPr, "sz");
                run.sizePt = sz > 0 ? sz / 100.0f : inherited.sizePt;
                const std::string colour = Colour(Child(rPr, "solidFill"));
                run.color = colour.empty() ? inherited.color : colour;
                if (!run.text.empty()) runs.push_back(std::move(run));
            } else if (Is(r, "m")) {
                // An equation (a14:m), as PowerPoint 2010 and later store it.
                PptxRun run;
                MathText(r, run.text);
                run.sizePt = inherited.sizePt;
                run.color = inherited.color;
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
