// core/UltraCanvasPresentationPptxWriter.cpp
// PowerPoint (.pptx) writer - see UltraCanvasPresentationPptx.h: a
// PptxPresentation written as a package PowerPoint, LibreOffice and the
// reader open, and a word-processing document laid out as a deck.
// Version: 1.2.0 - group shapes: p:grpSp round a groupPath's shapes, nested, chOff / chExt the group's box
// Version: 1.1.0 - the deck's own masters and layouts (names, types, backgrounds, placeholders
//                  with their look and prompt text), each slide on its layout; transitions
//                  (p:transition, p14:dur in mc:AlternateContent for a length no speed names)
// Version: 1.0.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework
#include "UltraCanvasDrawingML.h"
#include "UltraCanvasOfficeEncryption.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasPresentationPptx.h"
#include "UltraCanvasZipPackage.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <locale>
#include <map>
#include <regex>
#include <set>
#include <sstream>

namespace UltraCanvas {

namespace {

constexpr int64_t kDefaultSlideWidth = 12192000;   // 16:9
constexpr int64_t kDefaultSlideHeight = 6858000;
constexpr const char* kNsA = "http://schemas.openxmlformats.org/drawingml/2006/main";
constexpr const char* kNsR = "http://schemas.openxmlformats.org/officeDocument/2006/relationships";
constexpr const char* kNsP = "http://schemas.openxmlformats.org/presentationml/2006/main";
constexpr const char* kRelBase = "http://schemas.openxmlformats.org/officeDocument/2006/relationships/";
constexpr const char* kXmlHead = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n";
// PowerPoint's Medium Style 2 - Accent 1: the look of a table that carries none of its own.
constexpr const char* kDefaultTableStyle = "{5C22544A-7EE6-4342-B048-85BDC9FD1C3A}";

std::string Escape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default:
                // XML 1.0 has no place for the other control characters.
                if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') break;
                out += static_cast<char>(c);
        }
    }
    return out;
}

std::string Num(int64_t value) { return std::to_string(value); }

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return std::tolower(c); });
    return text;
}

// "RRGGBB" (or "#RRGGBB") as a:srgbClr; empty for anything else.
std::string Hex(const std::string& colour) {
    std::string digits = !colour.empty() && colour[0] == '#' ? colour.substr(1) : colour;
    if (digits.size() != 6) return "";
    for (char& c : digits) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) return "";
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return digits;
}

std::string SolidFill(const std::string& colour) {
    const std::string hex = Hex(colour);
    return hex.empty() ? std::string() : "<a:solidFill><a:srgbClr val=\"" + hex + "\"/></a:solidFill>";
}

// The extension a picture part keeps, from its name or its bytes.
std::string PictureExtension(const std::string& target, const std::vector<uint8_t>& bytes) {
    const size_t dot = target.rfind('.');
    std::string ext = dot == std::string::npos ? "" : Lower(target.substr(dot + 1));
    if (ext == "jpg" || ext == "jpeg" || ext == "png" || ext == "gif" || ext == "bmp" || ext == "tif" ||
        ext == "tiff" || ext == "emf" || ext == "wmf" || ext == "svg")
        return ext == "jpg" ? "jpeg" : ext == "tif" ? "tiff" : ext;
    if (bytes.size() >= 4 && bytes[0] == 0x89 && bytes[1] == 'P') return "png";
    if (bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8) return "jpeg";
    if (bytes.size() >= 4 && bytes[0] == 'G' && bytes[1] == 'I' && bytes[2] == 'F') return "gif";
    if (bytes.size() >= 2 && bytes[0] == 'B' && bytes[1] == 'M') return "bmp";
    return "png";
}

std::string PictureContentType(const std::string& ext) {
    if (ext == "jpeg") return "image/jpeg";
    if (ext == "svg") return "image/svg+xml";
    if (ext == "emf") return "image/x-emf";
    if (ext == "wmf") return "image/x-wmf";
    return "image/" + ext;
}

// The main part's content type for the package kind the extension names.
std::string MainContentType(const std::string& extension) {
    const std::string ext = Lower(extension);
    const std::string base = "application/vnd.";
    if (ext == "potx") return base + "openxmlformats-officedocument.presentationml.template.main+xml";
    if (ext == "ppsx") return base + "openxmlformats-officedocument.presentationml.slideshow.main+xml";
    if (ext == "pptm") return base + "ms-powerpoint.presentation.macroEnabled.main+xml";
    if (ext == "potm") return base + "ms-powerpoint.template.macroEnabled.main+xml";
    if (ext == "ppsm") return base + "ms-powerpoint.slideshow.macroEnabled.main+xml";
    return base + "openxmlformats-officedocument.presentationml.presentation.main+xml";
}

// ----- The fixed parts: theme, master, layout, notes master -----

std::string ThemeXml(const PptxPresentation& deck, const std::string& name) {
    const std::map<std::string, std::string>& office = DrawingMLDefaultThemeColors();
    auto colour = [&](const char* key) {
        const auto found = deck.themeColors.find(key);
        std::string hex = found == deck.themeColors.end() ? "" : Hex(found->second);
        if (hex.empty()) hex = office.at(key);
        return "<a:" + std::string(key) + "><a:srgbClr val=\"" + hex + "\"/></a:" + key + ">";
    };
    const std::string major = deck.majorFont.empty() ? "Calibri Light" : deck.majorFont;
    const std::string minor = deck.minorFont.empty() ? "Calibri" : deck.minorFont;
    std::string xml = std::string(kXmlHead) + "<a:theme xmlns:a=\"" + kNsA + "\" name=\"" + Escape(name) + "\">"
        "<a:themeElements><a:clrScheme name=\"" + Escape(name) + "\">";
    for (const char* key : {"dk1", "lt1", "dk2", "lt2", "accent1", "accent2", "accent3", "accent4", "accent5",
                            "accent6", "hlink", "folHlink"})
        xml += colour(key);
    xml += "</a:clrScheme><a:fontScheme name=\"" + Escape(name) + "\">"
        "<a:majorFont><a:latin typeface=\"" + Escape(major) + "\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:majorFont>"
        "<a:minorFont><a:latin typeface=\"" + Escape(minor) + "\"/><a:ea typeface=\"\"/><a:cs typeface=\"\"/></a:minorFont>"
        "</a:fontScheme><a:fmtScheme name=\"" + Escape(name) + "\"><a:fillStyleLst>";
    for (int i = 0; i < 3; ++i) xml += "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>";
    xml += "</a:fillStyleLst><a:lnStyleLst>";
    for (const char* width : {"6350", "12700", "19050"})
        xml += std::string("<a:ln w=\"") + width + "\" cap=\"flat\" cmpd=\"sng\" algn=\"ctr\"><a:solidFill>"
               "<a:schemeClr val=\"phClr\"/></a:solidFill><a:prstDash val=\"solid\"/><a:miter lim=\"800000\"/></a:ln>";
    xml += "</a:lnStyleLst><a:effectStyleLst>";
    for (int i = 0; i < 3; ++i) xml += "<a:effectStyle><a:effectLst/></a:effectStyle>";
    xml += "</a:effectStyleLst><a:bgFillStyleLst>";
    for (int i = 0; i < 3; ++i) xml += "<a:solidFill><a:schemeClr val=\"phClr\"/></a:solidFill>";
    xml += "</a:bgFillStyleLst></a:fmtScheme></a:themeElements><a:objectDefaults/><a:extraClrSchemeLst/></a:theme>";
    return xml;
}

// A list style's nine levels: the sizes, margins and bullets PowerPoint's
// own template gives its body text.
std::string BodyLevels(bool bulleted, const char* font) {
    std::string xml;
    for (int level = 1; level <= 9; ++level) {
        const int size = level == 1 ? 2800 : level == 2 ? 2400 : 2000;
        const int64_t margin = bulleted ? 228600 + int64_t(level - 1) * 457200 : int64_t(level - 1) * 457200;
        xml += "<a:lvl" + std::to_string(level) + "pPr marL=\"" + Num(margin) + "\" indent=\"" +
               (bulleted ? "-228600" : "0") + "\" algn=\"l\" defTabSz=\"914400\" rtl=\"0\" eaLnBrk=\"1\" latinLnBrk=\"0\" "
               "hangingPunct=\"1\">";
        // Body text is spaced as PowerPoint's template spaces it; other text
        // (text boxes, notes) is not.
        if (bulleted)
            xml += std::string("<a:lnSpc><a:spcPct val=\"90000\"/></a:lnSpc><a:spcBef><a:spcPts val=\"") +
                   (level == 1 ? "1000" : "500") + "\"/></a:spcBef>";
        if (bulleted) xml += "<a:buFont typeface=\"Arial\"/><a:buChar char=\"\xE2\x80\xA2\"/>";
        else xml += "<a:buNone/>";
        xml += "<a:defRPr sz=\"" + std::to_string(bulleted ? size : 1800) + "\" kern=\"1200\"><a:solidFill>"
               "<a:schemeClr val=\"tx1\"/></a:solidFill><a:latin typeface=\"" + std::string(font) + "\"/></a:defRPr></a:lvl" +
               std::to_string(level) + "pPr>";
    }
    return xml;
}

std::string PlaceholderSp(int id, const char* name, const char* phXml, int64_t x, int64_t y, int64_t cx, int64_t cy,
                          const char* anchor) {
    return "<p:sp><p:nvSpPr><p:cNvPr id=\"" + std::to_string(id) + "\" name=\"" + name + "\"/><p:cNvSpPr>"
           "<a:spLocks noGrp=\"1\"/></p:cNvSpPr><p:nvPr>" + phXml + "</p:nvPr></p:nvSpPr><p:spPr><a:xfrm><a:off x=\"" +
           Num(x) + "\" y=\"" + Num(y) + "\"/><a:ext cx=\"" + Num(cx) + "\" cy=\"" + Num(cy) + "\"/></a:xfrm>"
           "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></p:spPr><p:txBody><a:bodyPr anchor=\"" + anchor +
           "\"><a:normAutofit/></a:bodyPr><a:lstStyle/><a:p><a:endParaRPr lang=\"en-US\"/></a:p></p:txBody></p:sp>";
}

const char* kGroupHeader =
    "<p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr><a:xfrm>"
    "<a:off x=\"0\" y=\"0\"/><a:ext cx=\"0\" cy=\"0\"/><a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"0\" cy=\"0\"/></a:xfrm></p:grpSpPr>";

std::string Namespaces() {
    return std::string("xmlns:a=\"") + kNsA + "\" xmlns:r=\"" + kNsR + "\" xmlns:p=\"" + kNsP + "\"";
}

// The standard master's title and body placeholders.
std::string StandardMasterShapes(int64_t w, int64_t h) {
    const int64_t mx = w / 13, titleH = h / 6;
    return PlaceholderSp(2, "Title Placeholder 1", "<p:ph type=\"title\"/>", mx, h / 19, w - 2 * mx, titleH, "ctr") +
           PlaceholderSp(3, "Text Placeholder 2", "<p:ph type=\"body\" idx=\"1\"/>", mx, h / 19 + titleH + h / 40,
                         w - 2 * mx, h - (h / 19 + titleH + h / 40) - h / 12, "t");
}

// A slide master: its name, background ("RRGGBB"; empty: the theme's light
// colour), shapes, layouts (p:sldLayoutId elements) and PowerPoint's text styles.
std::string MasterXmlWith(const std::string& name, const std::string& background, const std::string& shapes,
                          const std::string& layoutIds) {
    const std::string fill = SolidFill(background);
    std::string xml = std::string(kXmlHead) + "<p:sldMaster " + Namespaces() + "><p:cSld" +
        (name.empty() ? std::string() : " name=\"" + Escape(name) + "\"") + ">" +
        (fill.empty() ? std::string("<p:bg><p:bgRef idx=\"1001\"><a:schemeClr val=\"bg1\"/></p:bgRef></p:bg>")
                      : "<p:bg><p:bgPr>" + fill + "<a:effectLst/></p:bgPr></p:bg>") +
        "<p:spTree>" + kGroupHeader + shapes +
        "</p:spTree></p:cSld><p:clrMap bg1=\"lt1\" tx1=\"dk1\" bg2=\"lt2\" tx2=\"dk2\" accent1=\"accent1\" "
        "accent2=\"accent2\" accent3=\"accent3\" accent4=\"accent4\" accent5=\"accent5\" accent6=\"accent6\" "
        "hlink=\"hlink\" folHlink=\"folHlink\"/><p:sldLayoutIdLst>" + layoutIds + "</p:sldLayoutIdLst><p:txStyles><p:titleStyle><a:lvl1pPr algn=\"l\" defTabSz=\"914400\" rtl=\"0\" eaLnBrk=\"1\" "
        "latinLnBrk=\"0\" hangingPunct=\"1\"><a:lnSpc><a:spcPct val=\"90000\"/></a:lnSpc><a:spcBef><a:spcPct val=\"0\"/>"
        "</a:spcBef><a:buNone/><a:defRPr sz=\"4400\" kern=\"1200\"><a:solidFill><a:schemeClr val=\"tx1\"/></a:solidFill>"
        "<a:latin typeface=\"+mj-lt\"/></a:defRPr></a:lvl1pPr></p:titleStyle><p:bodyStyle>" +
        BodyLevels(true, "+mn-lt") + "</p:bodyStyle><p:otherStyle>" + BodyLevels(false, "+mn-lt") +
        "</p:otherStyle></p:txStyles></p:sldMaster>";
    return xml;
}

std::string MasterXml(int64_t w, int64_t h) {
    return MasterXmlWith("", "", StandardMasterShapes(w, h), "<p:sldLayoutId id=\"2147483649\" r:id=\"rId1\"/>");
}

// A deck's own layout: its type (when PowerPoint knows it), name, background
// and placeholders (`shapes`).
std::string DeckLayoutXml(const PptxLayout& layout, const std::string& shapes) {
    static const std::set<std::string> kTypes = {
        "title", "tx", "twoColTx", "tbl", "txAndChart", "chartAndTx", "dgm", "chart", "txAndClipArt", "clipArtAndTx",
        "titleOnly", "blank", "txAndObj", "objAndTx", "objOnly", "obj", "txAndMedia", "mediaAndTx", "objOverTx",
        "txOverObj", "txAndTwoObj", "twoObjAndTx", "twoObjOverTx", "fourObj", "vertTx", "clipArtAndVertTx",
        "vertTitleAndTx", "vertTitleAndTxOverChart", "twoObj", "objAndTwoObj", "twoObjAndObj", "cust", "secHead",
        "twoTxTwoObj", "objTx", "picTx"};
    const std::string fill = SolidFill(layout.background);
    return std::string(kXmlHead) + "<p:sldLayout " + Namespaces() +
           (kTypes.count(layout.type) ? " type=\"" + layout.type + "\"" : std::string()) + " preserve=\"1\"><p:cSld" +
           (layout.name.empty() ? std::string() : " name=\"" + Escape(layout.name) + "\"") + ">" +
           (fill.empty() ? std::string() : "<p:bg><p:bgPr>" + fill + "<a:effectLst/></p:bgPr></p:bg>") + "<p:spTree>" +
           kGroupHeader + shapes + "</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>";
}

std::string LayoutXml(int64_t w, int64_t h) {
    const int64_t mx = w / 13, titleH = h / 6;
    return std::string(kXmlHead) + "<p:sldLayout " + Namespaces() + " type=\"obj\" preserve=\"1\"><p:cSld name=\"Title and "
        "Content\"><p:spTree>" + kGroupHeader +
        PlaceholderSp(2, "Title 1", "<p:ph type=\"title\"/>", mx, h / 19, w - 2 * mx, titleH, "ctr") +
        PlaceholderSp(3, "Content Placeholder 2", "<p:ph idx=\"1\"/>", mx, h / 19 + titleH + h / 40, w - 2 * mx,
                      h - (h / 19 + titleH + h / 40) - h / 12, "t") +
        "</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>";
}

std::string NotesMasterXml() {
    return std::string(kXmlHead) + "<p:notesMaster " + Namespaces() + "><p:cSld><p:bg><p:bgRef idx=\"1001\">"
        "<a:schemeClr val=\"bg1\"/></p:bgRef></p:bg><p:spTree>" + kGroupHeader +
        PlaceholderSp(2, "Slide Image Placeholder 1", "<p:ph type=\"sldImg\" idx=\"2\"/>", 685800, 1143000, 5486400,
                      3086100, "t") +
        PlaceholderSp(3, "Notes Placeholder 2", "<p:ph type=\"body\" sz=\"quarter\" idx=\"3\"/>", 685800, 4400550,
                      5486400, 3600450, "t") +
        "</p:spTree></p:cSld><p:clrMap bg1=\"lt1\" tx1=\"dk1\" bg2=\"lt2\" tx2=\"dk2\" accent1=\"accent1\" "
        "accent2=\"accent2\" accent3=\"accent3\" accent4=\"accent4\" accent5=\"accent5\" accent6=\"accent6\" "
        "hlink=\"hlink\" folHlink=\"folHlink\"/><p:notesStyle>" + BodyLevels(false, "+mn-lt") +
        "</p:notesStyle></p:notesMaster>";
}

std::string Relationships(const std::vector<std::pair<std::string, std::string>>& rels) {
    // Each entry: id, then the rest of the element's attributes.
    std::string xml = std::string(kXmlHead) +
        "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">";
    for (const auto& [id, rest] : rels) xml += "<Relationship Id=\"" + id + "\" " + rest + "/>";
    return xml + "</Relationships>";
}

std::string Rel(const std::string& type, const std::string& target, bool external = false) {
    return "Type=\"" + std::string(kRelBase) + type + "\" Target=\"" + Escape(target) + "\"" +
           (external ? " TargetMode=\"External\"" : "");
}

// ----- Masters' and layouts' placeholders -----

// A placeholder a layout offers a slide: its kind, and the p:ph a slide's
// shape of that kind names to take its place and look.
struct PlaceholderSlot {
    PptxShapeKind kind;
    std::string ph;
};

// What a master's or layout's placeholder is called in p:ph: its type
// ("title", "body", "dt" ...), from the model's type, else its kind; empty
// for a shape that is no placeholder.
std::string DesignPlaceholderType(const PptxShape& shape) {
    static const std::set<std::string> kTypes = {"title", "body", "ctrTitle", "subTitle", "dt", "sldNum", "ftr",
                                                 "obj", "chart", "tbl", "clipArt", "dgm", "media", "pic"};
    if (!shape.placeholder.empty()) return kTypes.count(shape.placeholder) ? shape.placeholder : "body";
    switch (shape.kind) {
        case PptxShapeKind::Title: return "title";
        case PptxShapeKind::Subtitle: return "subTitle";
        case PptxShapeKind::Body: return "body";
        default: return "";
    }
}

// The p:ph of a master's (`onMaster`) or layout's placeholder: the title
// without an index, the date, footer and number at PowerPoint's 10-12, a
// layout's content as an untyped (obj) placeholder numbered from 1.
std::string DesignPh(const std::string& type, bool onMaster, int& nextIdx) {
    if (type == "title" || type == "ctrTitle") return "<p:ph type=\"" + type + "\"/>";
    if (type == "dt") return "<p:ph type=\"dt\" sz=\"half\" idx=\"10\"/>";
    if (type == "ftr") return "<p:ph type=\"ftr\" sz=\"quarter\" idx=\"11\"/>";
    if (type == "sldNum") return "<p:ph type=\"sldNum\" sz=\"quarter\" idx=\"12\"/>";
    const std::string idx = std::to_string(nextIdx++);
    if (type == "body" || type == "obj")
        return onMaster ? "<p:ph type=\"body\" idx=\"" + idx + "\"/>" : "<p:ph idx=\"" + idx + "\"/>";
    return "<p:ph type=\"" + type + "\" idx=\"" + idx + "\"/>";
}

// The slots a layout's placeholders give its slides, in its order (not the
// date, footer and number, which a slide does not carry).
std::vector<PlaceholderSlot> LayoutSlots(const PptxLayout& layout) {
    std::vector<PlaceholderSlot> slots;
    int nextIdx = 1;
    for (const PptxShape& shape : layout.placeholders) {
        const std::string type = DesignPlaceholderType(shape);
        if (type.empty()) continue;
        const std::string ph = DesignPh(type, false, nextIdx);
        if (type == "dt" || type == "ftr" || type == "sldNum") continue;
        const PptxShapeKind kind = type == "title" || type == "ctrTitle" ? PptxShapeKind::Title
                                 : type == "subTitle"                    ? PptxShapeKind::Subtitle
                                                                         : PptxShapeKind::Body;
        slots.push_back({kind, ph});
    }
    return slots;
}

// ----- Transitions -----

// The slide's p:transition: the effect's element, its speed, and when the
// show moves on; a length no speed names goes in p14:dur, inside
// mc:AlternateContent with the speed alone as the Fallback, as PowerPoint
// 2010 and later write it. Empty for a slide that comes on plainly and
// waits for a click.
std::string TransitionXml(const PptxTransition& transition) {
    const bool timed = transition.advanceAfterSec >= 0;
    if (transition.kind == PptxTransitionKind::NoTransition && transition.advanceOnClick && !timed) return "";
    std::string effect;
    switch (transition.kind) {
        case PptxTransitionKind::Fade: effect = "<p:fade/>"; break;
        case PptxTransitionKind::Push: effect = "<p:push/>"; break;
        case PptxTransitionKind::Wipe: effect = "<p:wipe/>"; break;
        case PptxTransitionKind::Split: effect = "<p:split orient=\"vert\" dir=\"out\"/>"; break;
        case PptxTransitionKind::Cover: effect = "<p:cover/>"; break;
        case PptxTransitionKind::Zoom: effect = "<p:zoom/>"; break;
        case PptxTransitionKind::NoTransition: break;
    }
    const double seconds = transition.durationSec > 0 ? transition.durationSec : 0.5;
    const char* speed = seconds <= 0.5 ? "fast" : seconds <= 0.75 ? "med" : "slow";
    std::string timing;
    if (!transition.advanceOnClick) timing += " advClick=\"0\"";
    if (timed) timing += " advTm=\"" + Num(std::llround(transition.advanceAfterSec * 1000.0)) + "\"";
    const std::string plain =
        std::string("<p:transition spd=\"") + speed + "\"" + timing + (effect.empty() ? "/>" : ">" + effect + "</p:transition>");
    const int64_t ms = std::llround(seconds * 1000.0);
    if (effect.empty() || ms == 500 || ms == 750 || ms == 1000) return plain;
    return "<mc:AlternateContent xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\">"
           "<mc:Choice xmlns:p14=\"http://schemas.microsoft.com/office/powerpoint/2010/main\" Requires=\"p14\">"
           "<p:transition spd=\"" + std::string(speed) + "\" p14:dur=\"" + Num(ms) + "\"" + timing + ">" + effect +
           "</p:transition></mc:Choice><mc:Fallback>" + plain + "</mc:Fallback></mc:AlternateContent>";
}

// ----- A slide's parts -----

class SlideWriter {
public:
    SlideWriter(const PptxPresentation& deck, int number, std::map<std::string, std::string>& media,
                int& mediaCount, int& chartCount, std::vector<std::pair<std::string, std::string>>& extraParts)
        : deck_(deck), number_(number), media_(media), mediaCount_(mediaCount), chartCount_(chartCount),
          extraParts_(extraParts) {}

    // The layout the slide is on (its part, from the slide's folder) and the
    // placeholders it offers; the standard Title and Content when not set.
    void SetLayout(const std::string& target, const std::vector<PlaceholderSlot>& slots) {
        layoutTarget_ = target;
        slots_ = slots;
    }

    std::string Write(const PptxSlide& slide) {
        rels_.push_back({"rId1", Rel("slideLayout", layoutTarget_)});
        std::string xml = std::string(kXmlHead) + "<p:sld " + Namespaces() + (slide.hidden ? " show=\"0\"" : "") +
                          "><p:cSld>";
        if (const std::string fill = SolidFill(slide.background); !fill.empty())
            xml += "<p:bg><p:bgPr>" + fill + "<a:effectLst/></p:bgPr></p:bg>";
        xml += "<p:spTree>";
        xml += kGroupHeader;
        bodyIndex_ = 0;
        xml += WriteShapes(slide);
        xml += "</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr>" + TransitionXml(slide.transition) +
               "</p:sld>";
        return xml;
    }

    // A master's or layout's shape tree: its placeholders, each with the
    // p:ph PowerPoint matches a slide's by, a list style holding the look of
    // its paragraphs, and its prompt text.
    std::string WriteDesign(const std::vector<PptxShape>& placeholders, bool onMaster) {
        std::string xml;
        int nextIdx = 1;
        for (const PptxShape& shape : placeholders) {
            const std::string type = DesignPlaceholderType(shape);
            if (type.empty() || shape.kind == PptxShapeKind::Picture || shape.kind == PptxShapeKind::Table ||
                shape.kind == PptxShapeKind::Chart || shape.kind == PptxShapeKind::Connector) {
                xml += WriteShape(shape);
                continue;
            }
            fieldType_ = type == "sldNum" ? "slidenum" : type == "dt" ? "datetime1" : "";
            xml += WriteSp(shape, DesignPh(type, onMaster, nextIdx), ListStyleOf(shape.paragraphs));
            fieldType_.clear();
        }
        return xml;
    }

    void AddRel(const std::string& id, const std::string& rest) { rels_.push_back({id, rest}); }
    std::string RelsXml() const { return Relationships(rels_); }

private:
    const PptxPresentation& deck_;
    int number_;
    std::map<std::string, std::string>& media_;   // picture target -> ppt/media/imageN.ext
    int& mediaCount_;
    int& chartCount_;
    std::vector<std::pair<std::string, std::string>>& extraParts_;   // part name -> XML (charts)
    std::vector<std::pair<std::string, std::string>> rels_;
    std::map<std::string, std::string> linkIds_;
    int nextShapeId_ = 2;
    int bodyIndex_ = 0;
    std::string layoutTarget_ = "../slideLayouts/slideLayout1.xml";
    std::vector<PlaceholderSlot> slots_ = {{PptxShapeKind::Title, "<p:ph type=\"title\"/>"},
                                           {PptxShapeKind::Body, "<p:ph idx=\"1\"/>"}};
    std::string fieldType_;   // a master's or layout's number or date: its text is this field

    // The `n`th (0-based) slot of `kind` the slide's layout offers; null when it has none.
    const PlaceholderSlot* Slot(PptxShapeKind kind, int n) const {
        for (const PlaceholderSlot& slot : slots_)
            if (slot.kind == kind && n-- == 0) return &slot;
        return nullptr;
    }

    // The look of a placeholder's paragraphs as its list style, a level per
    // level used (the first paragraph at it), so that what is typed into it
    // later looks the same.
    std::string ListStyleOf(const std::vector<PptxParagraph>& paragraphs) {
        std::string levels[9];
        for (const PptxParagraph& p : paragraphs) {
            const int level = std::clamp(p.level, 0, 8);
            if (!levels[level].empty()) continue;
            const std::string tag = "a:lvl" + std::to_string(level + 1) + "pPr";
            std::string xml = "<" + tag;
            if (p.marginLeft >= 0) xml += " marL=\"" + Num(p.marginLeft) + "\"";
            if (p.indent != 0 || p.marginLeft >= 0) xml += " indent=\"" + Num(p.indent) + "\"";
            static const char* const kAlign[] = {"l", "ctr", "r", "just"};
            xml += " algn=\"" + std::string(kAlign[static_cast<int>(p.align)]) + "\">";
            xml += Spacing("lnSpc", p.lineSpacing) + Spacing("spcBef", p.spaceBefore) + Spacing("spcAft", p.spaceAfter);
            xml += BulletXml(p);
            if (!p.runs.empty()) {
                PptxRun look = p.runs.front();
                look.hyperlink.clear();
                std::string rPr = RunProperties(look, "a:defRPr");
                // A list style's run properties carry no language or dirty flag.
                for (const char* drop : {" lang=\"en-US\"", " dirty=\"0\""}) {
                    const size_t at = rPr.find(drop);
                    if (at != std::string::npos) rPr.erase(at, std::string(drop).size());
                }
                xml += rPr;
            }
            levels[level] = xml + "</" + tag + ">";
        }
        std::string out;
        for (const std::string& level : levels) out += level;
        return out.empty() ? std::string("<a:lstStyle/>") : "<a:lstStyle>" + out + "</a:lstStyle>";
    }

    // A paragraph's bullet or number, or that it has none.
    static std::string BulletXml(const PptxParagraph& p) {
        if (!p.bullet) return "<a:buNone/>";
        std::string xml;
        if (const std::string hex = Hex(p.bulletColor); !hex.empty())
            xml += "<a:buClr><a:srgbClr val=\"" + hex + "\"/></a:buClr>";
        if (p.bulletSizePercent > 0) xml += "<a:buSzPct val=\"" + Num(std::llround(p.bulletSizePercent * 1000.0)) + "\"/>";
        if (p.numbered) {
            xml += "<a:buAutoNum type=\"" + Escape(p.numberScheme.empty() ? "arabicPeriod" : p.numberScheme) + "\"";
            if (p.numberStartAt > 1) xml += " startAt=\"" + Num(p.numberStartAt) + "\"";
            return xml + "/>";
        }
        xml += "<a:buFont typeface=\"" + Escape(p.bulletFont.empty() ? "Arial" : p.bulletFont) + "\"/>";
        return xml + "<a:buChar char=\"" + Escape(p.bulletChar.empty() ? "\xE2\x80\xA2" : p.bulletChar) + "\"/>";
    }

    std::string NewRelId() { return "rId" + std::to_string(rels_.size() + 1); }

    std::string NonVisual(const PptxShape& shape, const char* kind, const std::string& extra, const std::string& nvPr,
                          const std::string& descr = std::string()) {
        const int id = nextShapeId_++;
        const std::string name = shape.name.empty() ? std::string(kind) + " " + std::to_string(id - 1) : shape.name;
        return "<p:cNvPr id=\"" + std::to_string(id) + "\" name=\"" + Escape(name) + "\"" +
               (descr.empty() ? "" : " descr=\"" + Escape(descr) + "\"") + "/>" + extra + "<p:nvPr>" + nvPr + "</p:nvPr>";
    }

    static std::string Xfrm(const PptxShape& shape, const char* tag = "a:xfrm") {
        std::string xml = std::string("<") + tag;
        const int64_t rot = static_cast<int64_t>(std::llround(std::fmod(shape.rotationDeg, 360.0) * 60000.0));
        if (rot != 0) xml += " rot=\"" + Num(rot < 0 ? rot + 21600000 : rot) + "\"";
        if (shape.flipH) xml += " flipH=\"1\"";
        if (shape.flipV) xml += " flipV=\"1\"";
        xml += "><a:off x=\"" + Num(shape.x) + "\" y=\"" + Num(shape.y) + "\"/><a:ext cx=\"" +
               Num(std::max<int64_t>(0, shape.width)) + "\" cy=\"" + Num(std::max<int64_t>(0, shape.height)) + "\"/></" +
               tag + ">";
        return xml;
    }

    static std::string Geometry(const PptxShape& shape, const char* fallback) {
        if (shape.customPath.empty()) {
            const std::string prst = shape.geometry.empty() ? fallback : shape.geometry;
            return "<a:prstGeom prst=\"" + Escape(prst) + "\"><a:avLst/></a:prstGeom>";
        }
        // The path in the box's own EMU, as PowerPoint writes a freeform.
        const int64_t w = std::max<int64_t>(1, shape.width), h = std::max<int64_t>(1, shape.height);
        auto pt = [&](float x, float y) {
            return "<a:pt x=\"" + Num(std::llround(x * static_cast<double>(w))) + "\" y=\"" +
                   Num(std::llround(y * static_cast<double>(h))) + "\"/>";
        };
        std::string xml = "<a:custGeom><a:avLst/><a:gdLst/><a:ahLst/><a:cxnLst/><a:rect l=\"l\" t=\"t\" r=\"r\" b=\"b\"/>"
                          "<a:pathLst><a:path w=\"" + Num(w) + "\" h=\"" + Num(h) + "\">";
        bool started = false;
        for (const RichShape::PathPoint& p : shape.customPath) {
            using Op = RichShape::PathPoint::Op;
            if (!started && p.op != Op::Move) xml += "<a:moveTo>" + pt(p.x, p.y) + "</a:moveTo>";
            started = true;
            switch (p.op) {
                case Op::Move: xml += "<a:moveTo>" + pt(p.x, p.y) + "</a:moveTo>"; break;
                case Op::Line: xml += "<a:lnTo>" + pt(p.x, p.y) + "</a:lnTo>"; break;
                case Op::Curve:
                    xml += "<a:cubicBezTo>" + pt(p.c1x, p.c1y) + pt(p.c2x, p.c2y) + pt(p.x, p.y) + "</a:cubicBezTo>";
                    break;
                case Op::Close: xml += "<a:close/>"; break;
            }
        }
        return xml + "</a:path></a:pathLst></a:custGeom>";
    }

    static std::string Fill(const PptxShape& shape) {
        if (shape.gradientStops.size() >= 2) {
            std::string xml = "<a:gradFill rotWithShape=\"1\"><a:gsLst>";
            for (const RichShape::GradientStop& stop : shape.gradientStops) {
                const std::string hex = Hex(stop.color);
                if (hex.empty()) continue;
                xml += "<a:gs pos=\"" + Num(std::llround(std::clamp(stop.position, 0.0f, 1.0f) * 100000.0)) +
                       "\"><a:srgbClr val=\"" + hex + "\"/></a:gs>";
            }
            xml += "</a:gsLst>";
            if (shape.gradientRadial)
                xml += "<a:path path=\"circle\"><a:fillToRect l=\"50000\" t=\"50000\" r=\"50000\" b=\"50000\"/></a:path>";
            else
                xml += "<a:lin ang=\"" + Num(std::llround(std::fmod(shape.gradientAngleDeg + 360.0, 360.0) * 60000.0)) +
                       "\" scaled=\"0\"/>";
            return xml + "</a:gradFill>";
        }
        const std::string solid = SolidFill(shape.fill);
        return solid.empty() ? "<a:noFill/>" : solid;
    }

    static std::string Line(const PptxShape& shape) {
        const std::string fill = SolidFill(shape.line);
        if (fill.empty()) return "<a:ln><a:noFill/></a:ln>";
        const double width = shape.lineWidthPt > 0 ? shape.lineWidthPt : 0.75;
        std::string xml = "<a:ln w=\"" + Num(std::llround(width * 12700.0)) + "\">" + fill;
        if (shape.dash == RichShape::Dash::Dashed) xml += "<a:prstDash val=\"dash\"/>";
        else if (shape.dash == RichShape::Dash::Dotted) xml += "<a:prstDash val=\"sysDot\"/>";
        if (shape.arrowStart) xml += "<a:headEnd type=\"triangle\"/>";
        if (shape.arrowEnd) xml += "<a:tailEnd type=\"triangle\"/>";
        return xml + "</a:ln>";
    }

    // ----- Text -----

    std::string LinkFor(const std::string& target) {
        if (target.empty()) return "";
        auto found = linkIds_.find(target);
        if (found == linkIds_.end()) {
            const std::string id = NewRelId();
            if (target[0] == '#') {
                // "#slide3": a jump to another slide of the deck.
                int other = 0;
                if (std::sscanf(target.c_str(), "#slide%d", &other) != 1 || other < 1 ||
                    other > static_cast<int>(deck_.slides.size()))
                    return "";
                rels_.push_back({id, Rel("slide", "slide" + std::to_string(other) + ".xml")});
            } else {
                rels_.push_back({id, Rel("hyperlink", target, true)});
            }
            found = linkIds_.emplace(target, id).first;
        }
        if (target[0] == '#')
            return "<a:hlinkClick r:id=\"" + found->second + "\" action=\"ppaction://hlinksldjump\"/>";
        return "<a:hlinkClick r:id=\"" + found->second + "\"/>";
    }

    std::string RunProperties(const PptxRun& run, const char* tag = "a:rPr") {
        std::string xml = std::string("<") + tag + " lang=\"en-US\"";
        if (run.sizePt > 0) xml += " sz=\"" + Num(std::llround(run.sizePt * 100.0)) + "\"";
        if (run.bold) xml += " b=\"1\"";
        if (run.italic) xml += " i=\"1\"";
        if (run.underline) xml += " u=\"sng\"";
        if (run.strike) xml += " strike=\"sngStrike\"";
        if (run.baseline != 0) xml += " baseline=\"" + Num(static_cast<int64_t>(run.baseline) * 1000) + "\"";
        if (run.allCaps) xml += " cap=\"all\"";
        else if (run.smallCaps) xml += " cap=\"small\"";
        xml += " dirty=\"0\">";
        xml += SolidFill(run.color);
        if (const std::string hex = Hex(run.highlight); !hex.empty())
            xml += "<a:highlight><a:srgbClr val=\"" + hex + "\"/></a:highlight>";
        if (!run.font.empty()) xml += "<a:latin typeface=\"" + Escape(run.font) + "\"/>";
        xml += LinkFor(run.hyperlink);
        return xml + "</" + tag + ">";
    }

    static std::string Spacing(const char* tag, const PptxSpacing& spacing) {
        if (spacing.percent > 0)
            return std::string("<a:") + tag + "><a:spcPct val=\"" + Num(std::llround(spacing.percent * 100000.0)) +
                   "\"/></a:" + tag + ">";
        if (spacing.points > 0)
            return std::string("<a:") + tag + "><a:spcPts val=\"" + Num(std::llround(spacing.points * 100.0)) +
                   "\"/></a:" + tag + ">";
        return "";
    }

    std::string Paragraphs(const std::vector<PptxParagraph>& paragraphs) {
        std::string xml;
        for (const PptxParagraph& p : paragraphs) {
            xml += "<a:p><a:pPr";
            if (p.marginLeft >= 0) xml += " marL=\"" + Num(p.marginLeft) + "\"";
            if (p.indent != 0 || p.marginLeft >= 0) xml += " indent=\"" + Num(p.indent) + "\"";
            if (p.level > 0) xml += " lvl=\"" + Num(std::clamp(p.level, 0, 8)) + "\"";
            static const char* const kAlign[] = {"l", "ctr", "r", "just"};
            xml += " algn=\"" + std::string(kAlign[static_cast<int>(p.align)]) + "\">";
            xml += Spacing("lnSpc", p.lineSpacing) + Spacing("spcBef", p.spaceBefore) + Spacing("spcAft", p.spaceAfter);
            xml += BulletXml(p) + "</a:pPr>";
            const PptxRun* last = nullptr;
            bool field = false;
            for (const PptxRun& run : p.runs) {
                last = &run;
                if (run.text == "\n") {
                    xml += "<a:br>" + RunProperties(run) + "</a:br>";
                    continue;
                }
                if (run.text.empty()) continue;
                if (!fieldType_.empty()) {
                    // A master's slide number or date: the field, once, showing its text.
                    if (field) continue;
                    field = true;
                    xml += "<a:fld id=\"{" + std::string(fieldType_ == "slidenum" ? "B6F15528-21DE-4FAA-801E-634DDDAF4B2B"
                                                                              : "1D8BD707-D9CF-40AE-B4C6-C98DA3205C09") +
                           "}\" type=\"" + fieldType_ + "\">" + RunProperties(run) + "<a:t>" + Escape(run.text) +
                           "</a:t></a:fld>";
                    continue;
                }
                xml += "<a:r>" + RunProperties(run) + "<a:t>" + Escape(run.text) + "</a:t></a:r>";
            }
            PptxRun end = last ? *last : PptxRun{};
            end.hyperlink.clear();
            xml += RunProperties(end, "a:endParaRPr") + "</a:p>";
        }
        if (paragraphs.empty()) xml += "<a:p><a:endParaRPr lang=\"en-US\" dirty=\"0\"/></a:p>";
        return xml;
    }

    std::string TextBody(const PptxShape& shape, const char* tag, const std::string& listStyle = "<a:lstStyle/>") {
        std::string xml = std::string("<") + tag + "><a:bodyPr";
        auto inset = [&](const char* name, int64_t value) {
            if (value >= 0) xml += std::string(" ") + name + "=\"" + Num(value) + "\"";
        };
        inset("lIns", shape.insetLeft);
        inset("tIns", shape.insetTop);
        inset("rIns", shape.insetRight);
        inset("bIns", shape.insetBottom);
        xml += std::string(" wrap=\"") + (shape.wrap ? "square" : "none") + "\"";
        if (shape.textRotation == 90) xml += " vert=\"vert\"";
        else if (shape.textRotation == 270) xml += " vert=\"vert270\"";
        static const char* const kAnchor[] = {"t", "ctr", "b"};
        xml += " rtlCol=\"0\" anchor=\"" + std::string(kAnchor[static_cast<int>(shape.anchor)]) + "\">";
        // Shrunk to fit only where the deck says so: an autofit without a
        // scale lets a viewer shrink the text as far as it likes.
        if (shape.fontScale > 0 && shape.fontScale < 0.999f)
            xml += "<a:normAutofit fontScale=\"" + Num(std::llround(shape.fontScale * 100000.0)) + "\"/>";
        xml += "</a:bodyPr>" + listStyle + Paragraphs(shape.paragraphs) + "</" + tag + ">";
        return xml;
    }

    // ----- Shapes -----

    // The slide's shapes in order, each group's (PptxShape::groupPath) inside
    // a p:grpSp of its own: its box the shapes', its child space the same,
    // so the shapes keep their places on the slide.
    std::string WriteShapes(const PptxSlide& slide) {
        std::string xml;
        const std::vector<PptxShape>& shapes = slide.shapes;
        std::vector<int> open;   // the groups open, outermost first
        for (size_t i = 0; i < shapes.size(); ++i) {
            const std::vector<int>& path = shapes[i].groupPath;
            size_t common = 0;
            while (common < open.size() && common < path.size() && open[common] == path[common]) ++common;
            for (; open.size() > common; open.pop_back()) xml += "</p:grpSp>";
            while (open.size() < path.size()) {
                const size_t depth = open.size();
                int64_t left = INT64_MAX, top = INT64_MAX, right = INT64_MIN, bottom = INT64_MIN;
                for (size_t k = i; k < shapes.size(); ++k) {
                    const std::vector<int>& other = shapes[k].groupPath;
                    if (other.size() <= depth || !std::equal(path.begin(), path.begin() + static_cast<long>(depth) + 1, other.begin()))
                        break;
                    const PptxShape& member = shapes[k];
                    if (member.width <= 0 && member.height <= 0 && member.placeInherited) continue;
                    left = std::min(left, member.x);
                    top = std::min(top, member.y);
                    right = std::max(right, member.x + member.width);
                    bottom = std::max(bottom, member.y + member.height);
                }
                if (left > right) left = top = right = bottom = 0;
                const int id = path[depth];
                const auto named = slide.groupNames.find(id);
                const std::string name = named != slide.groupNames.end() && !named->second.empty()
                                             ? named->second : "Group " + std::to_string(id);
                const std::string off = "x=\"" + Num(left) + "\" y=\"" + Num(top) + "\"";
                const std::string ext = "cx=\"" + Num(right - left) + "\" cy=\"" + Num(bottom - top) + "\"";
                xml += "<p:grpSp><p:nvGrpSpPr><p:cNvPr id=\"" + std::to_string(nextShapeId_++) + "\" name=\"" +
                       Escape(name) + "\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr><a:xfrm><a:off " + off +
                       "/><a:ext " + ext + "/><a:chOff " + off + "/><a:chExt " + ext + "/></a:xfrm></p:grpSpPr>";
                open.push_back(id);
            }
            xml += WriteShape(shapes[i]);
        }
        for (; !open.empty(); open.pop_back()) xml += "</p:grpSp>";
        return xml;
    }

    std::string WriteShape(const PptxShape& shape) {
        switch (shape.kind) {
            case PptxShapeKind::Picture: return WritePicture(shape);
            case PptxShapeKind::Table: return WriteTable(shape);
            case PptxShapeKind::Chart: return WriteChart(shape);
            case PptxShapeKind::Connector: return WriteConnector(shape);
            default: return WriteSp(shape);
        }
    }

    // A shape of its own (sp); `designPh` and `listStyle` for a master's or
    // layout's placeholder.
    std::string WriteSp(const PptxShape& shape, const std::string& designPh = std::string(),
                        const std::string& listStyle = "<a:lstStyle/>") {
        std::string ph = designPh;
        const char* kind = "TextBox";
        bool textBox = false;
        const PlaceholderSlot* slot = nullptr;
        switch (designPh.empty() ? shape.kind : PptxShapeKind::Text) {
            case PptxShapeKind::Title:
                slot = Slot(PptxShapeKind::Title, 0);
                ph = slot ? slot->ph : "<p:ph type=\"title\"/>";
                kind = "Title";
                break;
            case PptxShapeKind::Subtitle:
                slot = Slot(PptxShapeKind::Subtitle, 0);
                ph = slot ? slot->ph : "<p:ph type=\"subTitle\" idx=\"13\"/>";
                kind = "Subtitle";
                break;
            case PptxShapeKind::Body:
                // Each body takes the layout's next content placeholder; one
                // past them is a placeholder of its own.
                slot = Slot(PptxShapeKind::Body, bodyIndex_);
                ++bodyIndex_;
                ph = slot ? slot->ph : "<p:ph idx=\"" + std::to_string(99 + bodyIndex_) + "\"/>";
                kind = "Content Placeholder";
                break;
            default:
                if (!designPh.empty()) {
                    kind = "Placeholder";
                    break;
                }
                bool hasText = false;
                for (const PptxParagraph& p : shape.paragraphs) hasText = hasText || !p.Text().empty();
                textBox = hasText && shape.fill.empty() && shape.line.empty() && shape.customPath.empty() &&
                          (shape.geometry.empty() || shape.geometry == "rect");
                kind = textBox ? "TextBox" : "Shape";
                break;
        }
        const std::string cNvSpPr = ph.empty() ? (textBox ? "<p:cNvSpPr txBox=\"1\"/>" : "<p:cNvSpPr/>")
                                               : "<p:cNvSpPr><a:spLocks noGrp=\"1\"/></p:cNvSpPr>";
        std::string xml = "<p:sp><p:nvSpPr>" + NonVisual(shape, kind, cNvSpPr, ph) + "</p:nvSpPr><p:spPr>";
        // Its place, wherever it came from (this deck's layout is not the one
        // it was read with); a placeholder that never had one keeps its layout's.
        if (ph.empty() || shape.width > 0 || shape.height > 0) xml += Xfrm(shape);
        xml += Geometry(shape, "rect") + Fill(shape) + Line(shape) + "</p:spPr>";
        xml += TextBody(shape, "p:txBody", listStyle) + "</p:sp>";
        return xml;
    }

    std::string WriteConnector(const PptxShape& shape) {
        std::string xml = "<p:cxnSp><p:nvCxnSpPr>" + NonVisual(shape, "Connector", "<p:cNvCxnSpPr/>", "") +
                          "</p:nvCxnSpPr><p:spPr>" + Xfrm(shape) + Geometry(shape, "line");
        PptxShape lined = shape;
        if (Hex(lined.line).empty()) lined.line = "000000";
        xml += Line(lined) + "</p:spPr></p:cxnSp>";
        return xml;
    }

    std::string WritePicture(const PptxShape& shape) {
        const auto bytes = deck_.pictures.find(shape.pictureTarget);
        if (bytes == deck_.pictures.end() || bytes->second.empty()) {
            // Its bytes were not kept (a deck read without pictures): a
            // grey box with its description, where it stood.
            PptxShape box = shape;
            box.kind = PptxShapeKind::Text;
            box.fill = "ECEEF1";
            box.line = "C8CCD2";
            box.anchor = PptxAnchor::Middle;
            box.paragraphs.clear();
            PptxParagraph p;
            p.align = PptxAlign::Center;
            PptxRun run;
            run.text = shape.altText.empty() ? std::string("Picture") : shape.altText;
            run.italic = true;
            run.color = "6B7280";
            p.runs.push_back(run);
            box.paragraphs.push_back(p);
            return WriteSp(box);
        }
        std::string& part = media_[shape.pictureTarget];
        if (part.empty())
            part = "ppt/media/image" + std::to_string(++mediaCount_) + "." + PictureExtension(shape.pictureTarget, bytes->second);
        const std::string id = NewRelId();
        rels_.push_back({id, Rel("image", "../media/" + part.substr(part.rfind('/') + 1))});
        std::string xml = "<p:pic><p:nvPicPr>" +
                          NonVisual(shape, "Picture", "<p:cNvPicPr><a:picLocks noChangeAspect=\"1\"/></p:cNvPicPr>", "",
                                    shape.altText) +
                          "</p:nvPicPr><p:blipFill><a:blip r:embed=\"" + id + "\"/>";
        if (shape.cropLeft != 0 || shape.cropTop != 0 || shape.cropRight != 0 || shape.cropBottom != 0) {
            auto share = [](double v) { return Num(std::llround(v * 100000.0)); };
            xml += "<a:srcRect l=\"" + share(shape.cropLeft) + "\" t=\"" + share(shape.cropTop) + "\" r=\"" +
                   share(shape.cropRight) + "\" b=\"" + share(shape.cropBottom) + "\"/>";
        }
        xml += "<a:stretch><a:fillRect/></a:stretch></p:blipFill><p:spPr>" + Xfrm(shape) + Geometry(shape, "rect");
        if (!Hex(shape.line).empty()) xml += Line(shape);
        return xml + "</p:spPr></p:pic>";
    }

    std::string GraphicFrame(const PptxShape& shape, const char* kind, const std::string& data) {
        return "<p:graphicFrame><p:nvGraphicFramePr>" +
               NonVisual(shape, kind, "<p:cNvGraphicFramePr><a:graphicFrameLocks noGrp=\"1\"/></p:cNvGraphicFramePr>", "") +
               "</p:nvGraphicFramePr>" + Xfrm(shape, "p:xfrm") + "<a:graphic>" + data + "</a:graphic></p:graphicFrame>";
    }

    std::string WriteChart(const PptxShape& shape) {
        const std::string part = "ppt/charts/chart" + std::to_string(++chartCount_) + ".xml";
        extraParts_.push_back({part, WriteDrawingMLChart(shape.chart)});
        const std::string id = NewRelId();
        rels_.push_back({id, Rel("chart", "../charts/" + part.substr(part.rfind('/') + 1))});
        return GraphicFrame(shape, "Chart",
                            "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/chart\"><c:chart "
                            "xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\" r:id=\"" + id +
                            "\"/></a:graphicData>");
    }

    static std::string Border(const char* tag, const PptxCellStyle::Border& border) {
        const std::string fill = SolidFill(border.color);
        if (fill.empty() || border.widthPt <= 0) return std::string("<a:") + tag + "><a:noFill/></a:" + tag + ">";
        return std::string("<a:") + tag + " w=\"" + Num(std::llround(border.widthPt * 12700.0)) + "\">" + fill +
               "</a:" + tag + ">";
    }

    std::string WriteTable(const PptxShape& shape) {
        size_t columns = shape.columnWidths.size();
        for (const auto& row : shape.rows) columns = std::max(columns, row.size());
        if (columns == 0 || shape.rows.empty()) return "";
        std::vector<int64_t> widths = shape.columnWidths;
        if (widths.size() != columns) {
            widths.assign(columns, std::max<int64_t>(1, shape.width) / static_cast<int64_t>(columns));
        }
        const bool styled = shape.cellStyles.size() == shape.rows.size();
        // Which cells a merge covers: hMerge to the right of its origin, vMerge below it.
        std::vector<std::vector<int>> covered(shape.rows.size(), std::vector<int>(columns, 0));   // 1 h, 2 v, 3 both
        if (styled) {
            for (size_t r = 0; r < shape.rows.size(); ++r)
                for (size_t c = 0; c < columns && c < shape.cellStyles[r].size(); ++c) {
                    const PptxCellStyle& look = shape.cellStyles[r][c];
                    if (look.mergedAway || covered[r][c]) continue;
                    for (int dr = 0; dr < look.rowSpan && r + dr < shape.rows.size(); ++dr)
                        for (int dc = 0; dc < look.gridSpan && c + dc < columns; ++dc)
                            if (dr || dc) covered[r + dr][c + dc] = (dc ? 1 : 0) | (dr ? 2 : 0);
                }
        }
        std::string xml = "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/table\"><a:tbl>";
        // Its cells carry their own look; one read without (from another
        // reader) takes PowerPoint's default table style.
        xml += styled ? "<a:tblPr/>" : std::string("<a:tblPr firstRow=\"1\" bandRow=\"1\"><a:tableStyleId>") +
                                           kDefaultTableStyle + "</a:tableStyleId></a:tblPr>";
        xml += "<a:tblGrid>";
        for (int64_t w : widths) xml += "<a:gridCol w=\"" + Num(std::max<int64_t>(1, w)) + "\"/>";
        xml += "</a:tblGrid>";
        for (size_t r = 0; r < shape.rows.size(); ++r) {
            const int64_t h = r < shape.rowHeights.size() && shape.rowHeights[r] > 0 ? shape.rowHeights[r] : 370840;
            xml += "<a:tr h=\"" + Num(h) + "\">";
            for (size_t c = 0; c < columns; ++c) {
                const PptxCellStyle* look = styled && c < shape.cellStyles[r].size() ? &shape.cellStyles[r][c] : nullptr;
                xml += "<a:tc";
                if (look && !covered[r][c]) {
                    if (look->gridSpan > 1) xml += " gridSpan=\"" + Num(look->gridSpan) + "\"";
                    if (look->rowSpan > 1) xml += " rowSpan=\"" + Num(look->rowSpan) + "\"";
                }
                if (covered[r][c] & 1) xml += " hMerge=\"1\"";
                if (covered[r][c] & 2) xml += " vMerge=\"1\"";
                xml += "><a:txBody><a:bodyPr/><a:lstStyle/>";
                static const std::vector<PptxParagraph> kNone;
                const std::vector<PptxParagraph>& text = c < shape.rows[r].size() ? shape.rows[r][c] : kNone;
                xml += Paragraphs(covered[r][c] ? kNone : text);
                xml += "</a:txBody><a:tcPr";
                if (look) {
                    static const char* const kAnchor[] = {"t", "ctr", "b"};
                    xml += " marL=\"" + Num(look->insetLeft) + "\" marR=\"" + Num(look->insetRight) + "\" marT=\"" +
                           Num(look->insetTop) + "\" marB=\"" + Num(look->insetBottom) + "\" anchor=\"" +
                           kAnchor[static_cast<int>(look->anchor)] + "\">";
                    xml += Border("lnL", look->left) + Border("lnR", look->right) + Border("lnT", look->top) +
                           Border("lnB", look->bottom);
                    const std::string fill = SolidFill(look->fill);
                    xml += fill.empty() ? "<a:noFill/>" : fill;
                    xml += "</a:tcPr>";
                } else {
                    xml += "/>";
                }
                xml += "</a:tc>";
            }
            xml += "</a:tr>";
        }
        xml += "</a:tbl></a:graphicData>";
        PptxShape frame = shape;
        if (frame.width <= 0) {
            frame.width = 0;
            for (int64_t w : widths) frame.width += w;
        }
        return GraphicFrame(frame, "Table", xml);
    }
};

std::string NotesXml(const PptxSlide& slide) {
    std::string paragraphs;
    for (const PptxParagraph& p : slide.notes) {
        paragraphs += "<a:p>";
        for (const PptxRun& run : p.runs) {
            if (run.text == "\n") {
                paragraphs += "<a:br><a:rPr lang=\"en-US\" dirty=\"0\"/></a:br>";
            } else if (!run.text.empty()) {
                paragraphs += std::string("<a:r><a:rPr lang=\"en-US\"") + (run.bold ? " b=\"1\"" : "") +
                              (run.italic ? " i=\"1\"" : "") + " dirty=\"0\"/><a:t>" + Escape(run.text) + "</a:t></a:r>";
            }
        }
        paragraphs += "</a:p>";
    }
    return std::string(kXmlHead) + "<p:notes " + Namespaces() + "><p:cSld><p:spTree>" + kGroupHeader +
        "<p:sp><p:nvSpPr><p:cNvPr id=\"2\" name=\"Slide Image Placeholder 1\"/><p:cNvSpPr><a:spLocks noGrp=\"1\" "
        "noRot=\"1\" noChangeAspect=\"1\"/></p:cNvSpPr><p:nvPr><p:ph type=\"sldImg\"/></p:nvPr></p:nvSpPr><p:spPr/></p:sp>"
        "<p:sp><p:nvSpPr><p:cNvPr id=\"3\" name=\"Notes Placeholder 2\"/><p:cNvSpPr><a:spLocks noGrp=\"1\"/>"
        "</p:cNvSpPr><p:nvPr><p:ph type=\"body\" idx=\"1\"/></p:nvPr></p:nvSpPr><p:spPr/><p:txBody><a:bodyPr/>"
        "<a:lstStyle/>" + paragraphs + "</p:txBody></p:sp></p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/>"
        "</p:clrMapOvr></p:notes>";
}

bool HasNotes(const PptxSlide& slide) {
    for (const PptxParagraph& p : slide.notes)
        if (!p.Text().empty()) return true;
    return false;
}

} // namespace


// ----- A document laid out as a deck -----

namespace {

PptxRun RunFrom(const RichTextRun& r) {
    PptxRun run;
    run.text = r.text;
    run.bold = r.bold;
    run.italic = r.italic;
    run.underline = r.underline;
    run.strike = r.strikethrough;
    run.baseline = r.superscript ? 30 : r.subscript ? -25 : 0;
    run.font = r.code && r.fontFamily.empty() ? std::string("Courier New") : r.fontFamily;
    run.sizePt = r.fontSizePt;
    run.color = Hex(r.color);
    run.highlight = Hex(r.highlightColor);
    run.hyperlink = r.linkTarget;
    return run;
}

// A block's runs as a paragraph's: a line break before a run becomes "\n".
std::vector<PptxRun> RunsFrom(const std::vector<RichTextRun>& runs) {
    std::vector<PptxRun> out;
    for (const RichTextRun& r : runs) {
        if (r.lineBreakBefore && !out.empty()) {
            PptxRun br;
            br.text = "\n";
            br.sizePt = r.fontSizePt;
            out.push_back(br);
        }
        if (r.mediaIndex >= 0 || r.frameIndex >= 0) continue;   // pictures and frames in the line: not text
        if (!r.text.empty()) out.push_back(RunFrom(r));
    }
    return out;
}

PptxAlign AlignFrom(RichTextAlign align) {
    switch (align) {
        case RichTextAlign::Center: return PptxAlign::Center;
        case RichTextAlign::Right: return PptxAlign::Right;
        case RichTextAlign::Justify: return PptxAlign::Justify;
        default: return PptxAlign::Left;
    }
}

std::string SchemeFrom(RichNumberFormat format) {
    switch (format) {
        case RichNumberFormat::LowerLetter: return "alphaLcPeriod";
        case RichNumberFormat::UpperLetter: return "alphaUcPeriod";
        case RichNumberFormat::LowerRoman: return "romanLcPeriod";
        case RichNumberFormat::UpperRoman: return "romanUcPeriod";
        default: return "arabicPeriod";
    }
}

std::string PlainText(const std::vector<RichTextRun>& runs) {
    std::string out;
    for (const RichTextRun& r : runs) out += (r.lineBreakBefore && !out.empty() ? " " : "") + r.text;
    return out;
}

// What a slide gathers before it is laid out.
struct Gathered {
    std::string title;
    std::vector<PptxRun> titleRuns;
    bool hidden = false;
    std::vector<PptxParagraph> body;
    std::vector<const RichDocBlock*> objects;   // tables and pictures, in order
    std::vector<PptxParagraph> notes;
    bool titled = false;   // its title came from a heading, not the document's title
    bool HasContent() const { return titled || !body.empty() || !objects.empty() || !notes.empty(); }
    bool Empty() const { return title.empty() && body.empty() && objects.empty() && notes.empty(); }
};

} // namespace

PptxPresentation PptxPresentationFromRichDocument(const UCRichDocument& document) {
    PptxPresentation deck;
    deck.slideWidth = kDefaultSlideWidth;
    deck.slideHeight = kDefaultSlideHeight;
    const int64_t w = deck.slideWidth, h = deck.slideHeight;
    const int64_t mx = w / 13, titleY = h / 19, titleH = h / 6;
    const int64_t contentY = titleY + titleH + h / 40, contentH = h - contentY - h / 12, contentW = w - 2 * mx;

    // A deck's own outline (PptxPresentationToRichDocument) numbers its
    // headings "1. Title", "2. Title (hidden)": read back, they are titles.
    static const std::regex outlineHeading(R"(^(\d+)\. (.*?)( \(hidden\))?$)");
    bool outline = false;
    {
        int expected = 1, headings = 0;
        outline = true;
        for (const RichDocBlock& block : document.blocks) {
            if (block.type != RichBlockType::Heading || block.headingLevel != 1) continue;
            std::smatch m;
            const std::string text = PlainText(block.runs);
            if (!std::regex_match(text, m, outlineHeading) || std::stoi(m[1].str()) != expected++) {
                outline = false;
                break;
            }
            ++headings;
        }
        outline = outline && headings > 0;
    }

    std::vector<Gathered> slides(1);
    if (!document.metadata.title.empty()) slides.back().title = document.metadata.title;
    auto next = [&]() {
        if (slides.back().HasContent()) slides.emplace_back();
    };
    for (const RichDocBlock& block : document.blocks) {
        Gathered& slide = slides.back();
        switch (block.type) {
            case RichBlockType::Heading:
                if (block.headingLevel == 1) {
                    // A heading starts a slide - the one the document's own
                    // title opened, or one after a rule, while still empty.
                    if (slides.back().HasContent()) slides.emplace_back();
                    Gathered& titled = slides.back();
                    titled.titled = true;
                    titled.title = PlainText(block.runs);
                    titled.titleRuns = RunsFrom(block.runs);
                    std::smatch m;
                    if (outline && std::regex_match(titled.title, m, outlineHeading)) {
                        titled.title = m[2].str();
                        titled.hidden = m[3].matched;
                        titled.titleRuns.clear();
                        PptxRun run;
                        run.text = titled.title;
                        titled.titleRuns.push_back(run);
                    }
                } else {
                    PptxParagraph p;
                    p.runs = RunsFrom(block.runs);
                    for (PptxRun& run : p.runs) run.bold = true;
                    if (!p.runs.empty()) slide.body.push_back(std::move(p));
                }
                break;
            case RichBlockType::Paragraph:
            case RichBlockType::MathBlock: {
                PptxParagraph p;
                p.runs = RunsFrom(block.runs);
                p.align = AlignFrom(block.align);
                if (!p.runs.empty()) slide.body.push_back(std::move(p));
                break;
            }
            case RichBlockType::ListItem: {
                PptxParagraph p;
                p.runs = RunsFrom(block.runs);
                p.level = std::clamp(block.listLevel, 0, 8);
                p.bullet = true;
                p.numbered = block.orderedList;
                // The bullet or number hangs in its own room before the text.
                const int64_t hang = p.numbered ? 457200 : 285750;
                p.marginLeft = hang + int64_t(p.level) * 457200;
                p.indent = -hang;
                if (p.numbered) {
                    p.numberScheme = SchemeFrom(block.numberFormat);
                    p.numberStartAt = std::max(1, block.listStartNumber);
                } else if (!block.bulletText.empty()) {
                    p.bulletChar = block.bulletText;
                }
                slide.body.push_back(std::move(p));
                break;
            }
            case RichBlockType::CodeBlock: {
                PptxParagraph p;
                p.runs = RunsFrom(block.runs);
                for (PptxRun& run : p.runs)
                    if (run.text != "\n") run.font = "Courier New";
                if (!p.runs.empty()) slide.body.push_back(std::move(p));
                break;
            }
            case RichBlockType::BlockQuote: {
                // The outline's speaker notes open with a bold "Notes: ".
                std::vector<PptxRun> runs = RunsFrom(block.runs);
                if (!runs.empty() && runs.front().bold && runs.front().text == "Notes: ") {
                    runs.erase(runs.begin());
                    PptxParagraph p;
                    p.runs = std::move(runs);
                    slide.notes.push_back(std::move(p));
                } else if (!runs.empty()) {
                    PptxParagraph p;
                    p.runs = std::move(runs);
                    for (PptxRun& run : p.runs) run.italic = true;
                    slide.body.push_back(std::move(p));
                }
                break;
            }
            case RichBlockType::Table:
                if (!block.tableRows.empty()) slide.objects.push_back(&block);
                break;
            case RichBlockType::Image:
                if (block.mediaIndex >= 0 && block.mediaIndex < static_cast<int>(document.media.size()))
                    slide.objects.push_back(&block);
                break;
            case RichBlockType::HorizontalRule:
            case RichBlockType::PageBreak:
                next();
                break;
        }
    }
    if (!slides.empty() && slides.back().Empty()) slides.pop_back();

    int number = 0;
    for (const Gathered& g : slides) {
        PptxSlide slide;
        slide.number = ++number;
        slide.hidden = g.hidden;
        slide.title = g.title;
        slide.notes = g.notes;
        if (!g.title.empty()) {
            PptxShape title;
            title.kind = PptxShapeKind::Title;
            title.placeholder = "title";
            title.name = "Title 1";
            title.x = mx;
            title.y = titleY;
            title.width = contentW;
            title.height = titleH;
            title.anchor = PptxAnchor::Middle;
            PptxParagraph p;
            p.runs = g.titleRuns.empty() ? std::vector<PptxRun>{PptxRun{g.title}} : g.titleRuns;
            title.paragraphs.push_back(std::move(p));
            slide.shapes.push_back(std::move(title));
        }
        // The text over the upper part of the content area, the tables and
        // pictures one under the other below it (or over all of it).
        const int64_t textH = g.objects.empty() ? contentH : g.body.empty() ? 0 : contentH * 2 / 5;
        if (!g.body.empty()) {
            PptxShape body;
            body.kind = PptxShapeKind::Body;
            body.placeholder = "body";
            body.name = "Content Placeholder 2";
            body.x = mx;
            body.y = contentY;
            body.width = contentW;
            body.height = textH;
            body.paragraphs = g.body;
            // Text that would overflow its box is set smaller, as
            // PowerPoint's autofit sets it: lines of about 60 characters at
            // the body's 28 pt (24 below the first level), down to 40 %.
            double needPt = 0;
            for (const PptxParagraph& p : body.paragraphs) {
                const double size = p.level == 0 ? 28.0 : 24.0;
                const double chars = static_cast<double>(p.Text().size()) * (size / 28.0);
                needPt += size * 1.2 * std::max(1.0, std::ceil(chars / 60.0)) + size * 0.3;
            }
            const double roomPt = static_cast<double>(textH) / 12700.0 - 7.2;
            if (needPt > roomPt && roomPt > 0)
                body.fontScale = static_cast<float>(std::max(0.4, std::floor(roomPt / needPt * 40.0) / 40.0));
            slide.shapes.push_back(std::move(body));
        }
        if (!g.objects.empty()) {
            const int64_t gap = 91440;
            const int64_t areaY = contentY + textH + (textH > 0 ? gap : 0);
            const int64_t areaH = contentY + contentH - areaY;
            const int64_t each = (areaH - gap * static_cast<int64_t>(g.objects.size() - 1)) /
                                 static_cast<int64_t>(g.objects.size());
            int64_t y = areaY;
            int index = 0;
            for (const RichDocBlock* block : g.objects) {
                PptxShape shape;
                if (block->type == RichBlockType::Table) {
                    shape.kind = PptxShapeKind::Table;
                    shape.name = "Table " + std::to_string(++index);
                    size_t columns = 0;
                    for (const RichTableRow& row : block->tableRows) {
                        size_t span = 0;
                        for (const RichTableCell& cell : row.cells) span += static_cast<size_t>(std::max(1, cell.columnSpan));
                        columns = std::max(columns, span);
                    }
                    if (columns == 0) continue;
                    shape.x = mx;
                    shape.y = y;
                    shape.width = contentW;
                    const int64_t rowH = std::min<int64_t>(370840, each / static_cast<int64_t>(block->tableRows.size()));
                    shape.height = rowH * static_cast<int64_t>(block->tableRows.size());
                    shape.columnWidths.assign(columns, contentW / static_cast<int64_t>(columns));
                    for (const RichTableRow& row : block->tableRows) {
                        std::vector<std::vector<PptxParagraph>> cells;
                        for (const RichTableCell& cell : row.cells) {
                            PptxParagraph p;
                            p.runs = RunsFrom(cell.runs);
                            p.align = AlignFrom(cell.align);
                            if (row.header)
                                for (PptxRun& run : p.runs) run.bold = true;
                            cells.push_back({p});
                            for (int extra = 1; extra < cell.columnSpan; ++extra) cells.push_back({});
                        }
                        cells.resize(columns);
                        shape.rows.push_back(std::move(cells));
                        shape.rowHeights.push_back(rowH);
                    }
                    // No cell looks of its own: PowerPoint's default table style.
                } else {
                    const RichDocMedia& media = document.media[static_cast<size_t>(block->mediaIndex)];
                    shape.kind = PptxShapeKind::Picture;
                    shape.name = "Picture " + std::to_string(++index);
                    shape.altText = block->imageAltText;
                    shape.pictureTarget = "media/" + (media.name.empty() ? "image" + std::to_string(index) : media.name);
                    deck.pictures[shape.pictureTarget] = media.data;
                    // As large as fits, at its shape (4:3 when the document gives none).
                    const double aspect = block->imageWidthPt > 0 && block->imageHeightPt > 0
                                              ? block->imageWidthPt / static_cast<double>(block->imageHeightPt)
                                              : 4.0 / 3.0;
                    int64_t pw = contentW, ph = static_cast<int64_t>(std::llround(pw / aspect));
                    if (ph > each) {
                        ph = each;
                        pw = static_cast<int64_t>(std::llround(ph * aspect));
                    }
                    shape.x = mx + (contentW - pw) / 2;
                    shape.y = y;
                    shape.width = pw;
                    shape.height = ph;
                }
                slide.shapes.push_back(std::move(shape));
                y += each + gap;
            }
        }
        deck.slides.push_back(std::move(slide));
    }
    return deck;
}

bool WritePptxPresentationToPackage(const PptxPresentation& deck, std::vector<uint8_t>& out, std::string& error,
                                    const std::string& extension) {
    out.clear();
    error.clear();
    const int64_t w = deck.slideWidth > 0 ? deck.slideWidth : kDefaultSlideWidth;
    const int64_t h = deck.slideHeight > 0 ? deck.slideHeight : kDefaultSlideHeight;
    PptxPresentation sized = deck;
    sized.slideWidth = w;
    sized.slideHeight = h;

    UCZipPackageWriter zip;
    if (!zip.OpenInMemory()) {
        error = "The presentation could not be written: no memory for the package";
        return false;
    }
    std::vector<std::pair<std::string, std::string>> parts;   // name -> XML, in the order written
    std::map<std::string, std::string> media;                  // picture target -> part
    int mediaCount = 0, chartCount = 0;
    std::vector<std::pair<std::string, std::string>> charts;
    bool anyNotes = false;
    for (const PptxSlide& slide : deck.slides) anyNotes = anyNotes || HasNotes(slide);

    // The design: the deck's own masters and layouts, or (a deck made in
    // memory) the standard master with its Title and Content layout. Every
    // master gets a layout (a blank one when the deck gives it none) and a
    // theme of its own, and its layouts are numbered master by master.
    const bool ownDesign = !deck.layouts.empty();
    std::vector<PptxLayout> layouts = deck.layouts;
    const size_t masterCount = ownDesign ? std::max<size_t>(1, deck.masters.size()) : 1;
    for (PptxLayout& layout : layouts)
        layout.masterIndex = std::clamp(layout.masterIndex, 0, static_cast<int>(masterCount) - 1);
    for (size_t m = 0; ownDesign && m < masterCount; ++m) {
        const bool used = std::any_of(layouts.begin(), layouts.end(),
                                      [&](const PptxLayout& l) { return l.masterIndex == static_cast<int>(m); });
        if (used) continue;
        PptxLayout blank;
        blank.name = "Blank";
        blank.type = "blank";
        blank.masterIndex = static_cast<int>(m);
        layouts.push_back(blank);
    }
    std::vector<std::vector<size_t>> masterLayouts(masterCount);   // each master's layouts, by index into `layouts`
    std::vector<int> layoutNumber(std::max<size_t>(1, layouts.size()), 1);   // each layout's part number
    int numbered = 0;
    for (size_t i = 0; i < layouts.size(); ++i) masterLayouts[static_cast<size_t>(layouts[i].masterIndex)].push_back(i);
    for (std::vector<size_t>& list : masterLayouts)
        for (size_t i : list) layoutNumber[i] = ++numbered;
    const size_t layoutCount = ownDesign ? layouts.size() : 1;
    auto layoutPart = [&](size_t i) { return "slideLayout" + std::to_string(layoutNumber[i]) + ".xml"; };

    std::vector<std::pair<std::string, std::string>> designParts;
    uint32_t nextDesignId = 2147483648u;   // sldMasterId and sldLayoutId share one series from 2^31
    std::vector<uint32_t> masterIds(masterCount);
    for (size_t m = 0; m < masterCount; ++m) {
        const std::string n = std::to_string(m + 1);
        masterIds[m] = nextDesignId++;
        if (!ownDesign) {
            nextDesignId++;
            designParts.push_back({"ppt/slideMasters/slideMaster1.xml", MasterXml(w, h)});
            designParts.push_back({"ppt/slideMasters/_rels/slideMaster1.xml.rels",
                                   Relationships({{"rId1", Rel("slideLayout", "../slideLayouts/slideLayout1.xml")},
                                                  {"rId2", Rel("theme", "../theme/theme1.xml")}})});
            designParts.push_back({"ppt/slideLayouts/slideLayout1.xml", LayoutXml(w, h)});
            designParts.push_back({"ppt/slideLayouts/_rels/slideLayout1.xml.rels",
                                   Relationships({{"rId1", Rel("slideMaster", "../slideMasters/slideMaster1.xml")}})});
            break;
        }
        SlideWriter writer(sized, 0, media, mediaCount, chartCount, charts);
        std::string layoutIds;
        for (size_t k = 0; k < masterLayouts[m].size(); ++k) {
            const std::string rid = "rId" + std::to_string(k + 1);
            writer.AddRel(rid, Rel("slideLayout", "../slideLayouts/" + layoutPart(masterLayouts[m][k])));
            layoutIds += "<p:sldLayoutId id=\"" + std::to_string(nextDesignId++) + "\" r:id=\"" + rid + "\"/>";
        }
        writer.AddRel("rId" + std::to_string(masterLayouts[m].size() + 1), Rel("theme", "../theme/theme" + n + ".xml"));
        const PptxMaster* master = m < deck.masters.size() ? &deck.masters[m] : nullptr;
        const std::string shapes = master ? writer.WriteDesign(master->placeholders, true) : StandardMasterShapes(w, h);
        designParts.push_back({"ppt/slideMasters/slideMaster" + n + ".xml",
                               MasterXmlWith(master ? master->name : std::string(), master ? master->background : std::string(),
                                             shapes, layoutIds)});
        designParts.push_back({"ppt/slideMasters/_rels/slideMaster" + n + ".xml.rels", writer.RelsXml()});
        for (size_t i : masterLayouts[m]) {
            SlideWriter layoutWriter(sized, 0, media, mediaCount, chartCount, charts);
            layoutWriter.AddRel("rId1", Rel("slideMaster", "../slideMasters/slideMaster" + n + ".xml"));
            const std::string layoutShapes = layoutWriter.WriteDesign(layouts[i].placeholders, false);
            designParts.push_back({"ppt/slideLayouts/" + layoutPart(i), DeckLayoutXml(layouts[i], layoutShapes)});
            designParts.push_back({"ppt/slideLayouts/_rels/" + layoutPart(i) + ".rels", layoutWriter.RelsXml()});
        }
    }

    // The presentation and its relationships.
    std::vector<std::pair<std::string, std::string>> presRels;
    std::string masterIdList;
    for (size_t m = 0; m < masterCount; ++m) {
        const std::string id = "rId" + std::to_string(presRels.size() + 1);
        presRels.push_back({id, Rel("slideMaster", "slideMasters/slideMaster" + std::to_string(m + 1) + ".xml")});
        masterIdList += "<p:sldMasterId id=\"" + std::to_string(masterIds[m]) + "\" r:id=\"" + id + "\"/>";
    }
    auto nextRel = [&]() { return "rId" + std::to_string(presRels.size() + 1); };
    presRels.push_back({nextRel(), Rel("theme", "theme/theme1.xml")});
    presRels.push_back({nextRel(), Rel("presProps", "presProps.xml")});
    presRels.push_back({nextRel(), Rel("viewProps", "viewProps.xml")});
    presRels.push_back({nextRel(), Rel("tableStyles", "tableStyles.xml")});
    std::string notesMasterRel;
    if (anyNotes) {
        notesMasterRel = nextRel();
        presRels.push_back({notesMasterRel, Rel("notesMaster", "notesMasters/notesMaster1.xml")});
    }
    std::string sldIds;
    for (size_t i = 0; i < deck.slides.size(); ++i) {
        const std::string id = nextRel();
        presRels.push_back({id, Rel("slide", "slides/slide" + std::to_string(i + 1) + ".xml")});
        sldIds += "<p:sldId id=\"" + std::to_string(256 + i) + "\" r:id=\"" + id + "\"/>";
    }
    std::string presentation = std::string(kXmlHead) + "<p:presentation " + Namespaces() + " saveSubsetFonts=\"1\">"
        "<p:sldMasterIdLst>" + masterIdList + "</p:sldMasterIdLst>";
    if (anyNotes) presentation += "<p:notesMasterIdLst><p:notesMasterId r:id=\"" + notesMasterRel + "\"/></p:notesMasterIdLst>";
    if (!sldIds.empty()) presentation += "<p:sldIdLst>" + sldIds + "</p:sldIdLst>";
    presentation += "<p:sldSz cx=\"" + Num(w) + "\" cy=\"" + Num(h) + "\"/><p:notesSz cx=\"6858000\" cy=\"9144000\"/>"
        "<p:defaultTextStyle>" + BodyLevels(false, "+mn-lt") + "</p:defaultTextStyle></p:presentation>";

    // The slides, each on its layout (the first when it names none), their
    // notes, pictures and charts.
    std::vector<std::vector<PlaceholderSlot>> slots;
    for (size_t i = 0; ownDesign && i < layouts.size(); ++i) slots.push_back(LayoutSlots(layouts[i]));
    std::vector<std::pair<std::string, std::string>> slideParts;
    for (size_t i = 0; i < deck.slides.size(); ++i) {
        const PptxSlide& slide = deck.slides[i];
        const std::string n = std::to_string(i + 1);
        SlideWriter writer(sized, static_cast<int>(i + 1), media, mediaCount, chartCount, charts);
        if (ownDesign) {
            const size_t layout = slide.layoutIndex >= 0 && slide.layoutIndex < static_cast<int>(deck.layouts.size())
                                      ? static_cast<size_t>(slide.layoutIndex)
                                      : 0;
            writer.SetLayout("../slideLayouts/" + layoutPart(layout), slots[layout]);
        }
        const std::string xml = writer.Write(slide);
        if (HasNotes(slide)) {
            writer.AddRel("rId" + std::to_string(1000), Rel("notesSlide", "../notesSlides/notesSlide" + n + ".xml"));
            slideParts.push_back({"ppt/notesSlides/notesSlide" + n + ".xml", NotesXml(slide)});
            slideParts.push_back({"ppt/notesSlides/_rels/notesSlide" + n + ".xml.rels",
                                  Relationships({{"rId1", Rel("notesMaster", "../notesMasters/notesMaster1.xml")},
                                                 {"rId2", Rel("slide", "../slides/slide" + n + ".xml")}})});
        }
        slideParts.push_back({"ppt/slides/slide" + n + ".xml", xml});
        slideParts.push_back({"ppt/slides/_rels/slide" + n + ".xml.rels", writer.RelsXml()});
    }

    // [Content_Types].xml
    std::set<std::string> pictureTypes;
    for (const auto& [target, part] : media) pictureTypes.insert(part.substr(part.rfind('.') + 1));
    std::string types = std::string(kXmlHead) +
        "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
        "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
        "<Default Extension=\"xml\" ContentType=\"application/xml\"/>";
    for (const std::string& ext : pictureTypes)
        types += "<Default Extension=\"" + ext + "\" ContentType=\"" + PictureContentType(ext) + "\"/>";
    const std::string ppt = "application/vnd.openxmlformats-officedocument.presentationml.";
    const std::string themeType = "application/vnd.openxmlformats-officedocument.theme+xml";
    auto override = [&](const std::string& part, const std::string& type) {
        types += "<Override PartName=\"/" + part + "\" ContentType=\"" + type + "\"/>";
    };
    override("ppt/presentation.xml", MainContentType(extension));
    for (size_t m = 0; m < masterCount; ++m) {
        override("ppt/slideMasters/slideMaster" + std::to_string(m + 1) + ".xml", ppt + "slideMaster+xml");
        override("ppt/theme/theme" + std::to_string(m + 1) + ".xml", themeType);
    }
    for (size_t i = 0; i < layoutCount; ++i)
        override("ppt/slideLayouts/slideLayout" + std::to_string(i + 1) + ".xml", ppt + "slideLayout+xml");
    override("ppt/presProps.xml", ppt + "presProps+xml");
    override("ppt/viewProps.xml", ppt + "viewProps+xml");
    override("ppt/tableStyles.xml", ppt + "tableStyles+xml");
    const std::string notesTheme = "theme" + std::to_string(masterCount + 1) + ".xml";
    if (anyNotes) {
        override("ppt/notesMasters/notesMaster1.xml", ppt + "notesMaster+xml");
        override("ppt/theme/" + notesTheme, themeType);
    }
    for (size_t i = 0; i < deck.slides.size(); ++i) {
        override("ppt/slides/slide" + std::to_string(i + 1) + ".xml", ppt + "slide+xml");
        if (HasNotes(deck.slides[i]))
            override("ppt/notesSlides/notesSlide" + std::to_string(i + 1) + ".xml", ppt + "notesSlide+xml");
    }
    for (const auto& [part, xml] : charts)
        override(part, "application/vnd.openxmlformats-officedocument.drawingml.chart+xml");
    override("docProps/core.xml", "application/vnd.openxmlformats-package.core-properties+xml");
    override("docProps/app.xml", "application/vnd.openxmlformats-officedocument.extended-properties+xml");
    types += "</Types>";

    const std::string title = deck.slides.empty() ? std::string() : deck.slides.front().title;
    const std::string core = std::string(kXmlHead) +
        "<cp:coreProperties xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" xmlns:dcterms=\"http://purl.org/dc/terms/\" "
        "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\"><dc:title>" + Escape(title) +
        "</dc:title></cp:coreProperties>";
    const std::string app = std::string(kXmlHead) +
        "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/extended-properties\">"
        "<Application>UltraCanvas</Application><Slides>" + std::to_string(deck.slides.size()) + "</Slides>"
        "<Notes>" + std::to_string(std::count_if(deck.slides.begin(), deck.slides.end(), HasNotes)) +
        "</Notes></Properties>";

    parts.push_back({"[Content_Types].xml", types});
    parts.push_back({"_rels/.rels",
                     Relationships({{"rId1", Rel("officeDocument", "ppt/presentation.xml")},
                                    {"rId2", "Type=\"http://schemas.openxmlformats.org/package/2006/relationships/"
                                             "metadata/core-properties\" Target=\"docProps/core.xml\""},
                                    {"rId3", Rel("extended-properties", "docProps/app.xml")}})});
    parts.push_back({"docProps/core.xml", core});
    parts.push_back({"docProps/app.xml", app});
    parts.push_back({"ppt/presentation.xml", presentation});
    parts.push_back({"ppt/_rels/presentation.xml.rels", Relationships(presRels)});
    parts.push_back({"ppt/presProps.xml", std::string(kXmlHead) + "<p:presentationPr " + Namespaces() + "/>"});
    parts.push_back({"ppt/viewProps.xml", std::string(kXmlHead) + "<p:viewPr " + Namespaces() + "/>"});
    parts.push_back({"ppt/tableStyles.xml", std::string(kXmlHead) + "<a:tblStyleLst xmlns:a=\"" + kNsA + "\" def=\"" +
                                                kDefaultTableStyle + "\"/>"});
    for (size_t m = 0; m < masterCount; ++m)
        parts.push_back({"ppt/theme/theme" + std::to_string(m + 1) + ".xml", ThemeXml(sized, "Office Theme")});
    parts.insert(parts.end(), designParts.begin(), designParts.end());
    if (anyNotes) {
        parts.push_back({"ppt/notesMasters/notesMaster1.xml", NotesMasterXml()});
        parts.push_back({"ppt/notesMasters/_rels/notesMaster1.xml.rels",
                         Relationships({{"rId1", Rel("theme", "../theme/" + notesTheme)}})});
        parts.push_back({"ppt/theme/" + notesTheme, ThemeXml(sized, "Notes Theme")});
    }
    parts.insert(parts.end(), slideParts.begin(), slideParts.end());
    parts.insert(parts.end(), charts.begin(), charts.end());

    for (const auto& [name, xml] : parts) {
        if (!zip.AddEntry(name, xml)) {
            error = "The presentation could not be written: " + name;
            return false;
        }
    }
    for (const auto& [target, part] : media) {
        const auto bytes = deck.pictures.find(target);
        const std::string data(bytes->second.begin(), bytes->second.end());
        // Pictures are compressed already.
        if (!zip.AddEntry(part, data, false)) {
            error = "The presentation could not be written: " + part;
            return false;
        }
    }
    if (!zip.Finalize()) {
        error = "The presentation could not be written: the package did not close";
        return false;
    }
    out = zip.TakeBytes();
    return !out.empty();
}

bool WritePptxPresentation(const PptxPresentation& deck, const std::string& filePath, std::string& error,
                           const std::string& password) {
    const size_t dot = filePath.rfind('.');
    const std::string extension = dot == std::string::npos ? std::string("pptx") : filePath.substr(dot + 1);
    std::vector<uint8_t> package;
    if (!WritePptxPresentationToPackage(deck, package, error, IsPptxExtension(extension) ? extension : "pptx")) {
        error += ": " + filePath;
        return false;
    }
    if (!password.empty()) {
        // Saved with a password: the package encrypted into a compound file,
        // as PowerPoint's "Encrypt with Password" writes it.
        if (!EncryptOfficePackageToFile(package, password, filePath, error)) {
            error += ": " + filePath;
            return false;
        }
        return true;
    }
    std::ofstream file(PathFromUtf8(filePath), std::ios::binary | std::ios::trunc);
    if (!file) {
        error = "The presentation could not be saved (the file could not be created): " + filePath;
        return false;
    }
    file.write(reinterpret_cast<const char*>(package.data()), static_cast<std::streamsize>(package.size()));
    if (!file) {
        error = "The presentation could not be saved (the disk refused the write): " + filePath;
        return false;
    }
    return true;
}

} // namespace UltraCanvas
