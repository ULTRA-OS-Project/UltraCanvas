// Plugins/Documents/Word/UltraCanvasOdtFormat.cpp
// OpenDocument Text (.odt) reader and writer for UCRichDocument.
// ODT is a ZIP package (mimetype first, stored) containing content.xml,
// styles.xml, meta.xml, META-INF/manifest.xml and Pictures/*. Parsing uses
// tinyxml2 with the conventional ODF namespace prefixes (office:, text:,
// style:, fo:, draw:, table:, xlink:) — the same approach as the existing
// ODS spreadsheet loader.
// Version: 1.6.0 - Pattern fills: draw:hatch read and written, other patterns as their tile
//                  (an inline draw:fill-image, read too);
//                  picture fills: draw:fill-image read and written
// Version: 1.5.0 - Gradient fills (draw:gradient) read and written
// Version: 1.4.0 - Drawn shapes: draw:rect, ellipse, line, path, polygon, polyline, custom-shape
//                  and g read and written; a text frame's height and its words' vertical place
//                  written
// Version: 1.3.0 - fo:letter-spacing: a run's (and a named text style's) letter spacing, read
//                  and written
// Version: 1.2.0
// Last Modified: 2026-11-04
// Author: UltraCanvas Framework

#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"
#include "UltraCanvasMathToLatex.h"
#include "UltraCanvasWordFormatInternal.h"
#include "UltraCanvasZipPackage.h"
#include "UltraCanvasTextUtils.h"

#include "tinyxml2.h"
#include "miniz.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>
#include <cstring>
#include <map>
#include <set>
#include <locale>
#include <iomanip>
#include <sstream>

namespace UltraCanvas {

using WordFormatInternal::EscapeXml;
using WordFormatInternal::MathBlockAsParagraph;
using WordFormatInternal::ParseLengthPt;
using WordFormatInternal::ToLower;

namespace {

// ===== ODT READING =====

constexpr float kUnsetLength = std::numeric_limits<float>::quiet_NaN();

// Tri-state character properties collected from a style (-1 = inherit).
struct OdtTextProps {
    int bold = -1;
    int italic = -1;
    int underline = -1;
    int strike = -1;
    int subscript = -1;
    int superscript = -1;
    int hidden = -1;                                // text:display="none" (hidden text)
    std::string color;
    std::string fontFamily;
    float fontSizePt = 0.0f;
    float letterSpacingPt = kUnsetLength;           // fo:letter-spacing ("normal" = 0); NaN = inherit
    std::string parentStyleName;
    std::string masterPageName;                     // style:master-page-name (paragraph styles)
    RichTextAlign align = RichTextAlign::Default;   // paragraph styles only
    int rightToLeft = -1;                           // style:writing-mode rl-*: 1, lr-*: 0, unset: -1
    bool bottomBorder = false;                      // paragraph styles only
    bool pageBreakBefore = false;                   // paragraph styles only
    int headingLevel = 0;                           // derived from heading style names
    std::string characterStyle;                     // the named text style a span has (model id)
    // Paragraph geometry (paragraph styles only). NaN = not set by this style.
    float marginLeft = kUnsetLength;
    float marginRight = kUnsetLength;
    float textIndent = kUnsetLength;
    float marginTop = kUnsetLength;
    float marginBottom = kUnsetLength;
    float lineSpacing = kUnsetLength;               // proportional line height, 1 = single
    float lineHeightPt = kUnsetLength;              // fixed line height
    bool lineHeightAtLeast = false;
    bool hasFrame = false;                          // borders/background stated (as a group)
    RichBorder frame[4];                            // top, bottom, left, right
    std::string paragraphBackground;
    std::string highlight;                          // text background; "" = inherit
    bool hasTabStops = false;                       // an empty style:tab-stops clears inherited ones
    std::vector<RichTabStop> tabStops;              // positions as written (see tabsRelativeToIndent_)

    void MergeParent(const OdtTextProps& parent) {
        auto inherit = [](float& value, float parentValue) {
            if (std::isnan(value)) value = parentValue;
        };
        inherit(marginLeft, parent.marginLeft);
        inherit(marginRight, parent.marginRight);
        inherit(textIndent, parent.textIndent);
        inherit(marginTop, parent.marginTop);
        inherit(marginBottom, parent.marginBottom);
        inherit(lineSpacing, parent.lineSpacing);
        if (std::isnan(lineHeightPt)) {
            lineHeightPt = parent.lineHeightPt;
            lineHeightAtLeast = parent.lineHeightAtLeast;
        }
        if (!hasFrame && parent.hasFrame) {
            hasFrame = true;
            for (int i = 0; i < 4; ++i) frame[i] = parent.frame[i];
            paragraphBackground = parent.paragraphBackground;
        }
        if (highlight.empty()) highlight = parent.highlight;
        if (!hasTabStops) {
            hasTabStops = parent.hasTabStops;
            tabStops = parent.tabStops;
        }
        if (bold < 0) bold = parent.bold;
        if (italic < 0) italic = parent.italic;
        if (underline < 0) underline = parent.underline;
        if (strike < 0) strike = parent.strike;
        if (subscript < 0) subscript = parent.subscript;
        if (superscript < 0) superscript = parent.superscript;
        if (hidden < 0) hidden = parent.hidden;
        if (color.empty()) color = parent.color;
        if (fontFamily.empty()) fontFamily = parent.fontFamily;
        if (fontSizePt <= 0) fontSizePt = parent.fontSizePt;
        if (std::isnan(letterSpacingPt)) letterSpacingPt = parent.letterSpacingPt;
        if (masterPageName.empty()) masterPageName = parent.masterPageName;
        if (align == RichTextAlign::Default) align = parent.align;
        if (rightToLeft < 0) rightToLeft = parent.rightToLeft;
        bottomBorder = bottomBorder || parent.bottomBorder;
        pageBreakBefore = pageBreakBefore || parent.pageBreakBefore;
        if (headingLevel == 0) headingLevel = parent.headingLevel;
        if (characterStyle.empty()) characterStyle = parent.characterStyle;
    }
};

// ODF style names and the model's style ids: Writer's default paragraph
// style "Standard" is the model's "Normal", and "Heading_20_3" its "Heading3",
// so a document moved between ODT and DOCX keeps one set of names.
std::string OdfNameToStyleId(const std::string& name) {
    if (name == "Standard") return "Normal";
    const std::string prefix = "Heading_20_";
    if (name.size() == prefix.size() + 1 && name.compare(0, prefix.size(), prefix) == 0
        && name.back() >= '1' && name.back() <= '9') {
        return "Heading" + name.substr(prefix.size());
    }
    return name;
}

std::string StyleIdToOdfName(const std::string& id) {
    if (id == "Normal") return "Standard";
    if (id.size() == 8 && id.compare(0, 7, "Heading") == 0 && id.back() >= '1' && id.back() <= '9') {
        return "Heading_20_" + id.substr(7);
    }
    // An NCName: spaces spelt as ODF does, anything else outside it dropped.
    std::string out;
    for (char c : id) {
        if (c == ' ') out += "_20_";
        else if (std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.'
                 || static_cast<unsigned char>(c) >= 0x80) out += c;
    }
    if (out.empty() || std::isdigit(static_cast<unsigned char>(out[0])) || out[0] == '-' || out[0] == '.') out = "S" + out;
    return out;
}

// Matches "Heading_20_3" / "Heading 3" / "heading3" style names (ODF encodes
// a space in style names as "_20_"). Returns 1..6 or 0.
int HeadingLevelFromStyleName(const std::string& styleName) {
    std::string name = WordFormatInternal::ToLower(styleName);
    // Decode the ODF space escape first — its digit would fool the scan below.
    size_t esc;
    while ((esc = name.find("_20_")) != std::string::npos) {
        name.replace(esc, 4, " ");
    }
    size_t pos = name.find("heading");
    if (pos == std::string::npos) return 0;
    size_t digit = name.find_first_of("123456789", pos);
    if (digit == std::string::npos) return 0;
    int level = name[digit] - '0';
    return (level >= 1 && level <= 6) ? level : 0;
}

bool IsMonospaceFamily(const std::string& family) {
    std::string lower = WordFormatInternal::ToLower(family);
    return lower.find("courier") != std::string::npos
        || lower.find("consolas") != std::string::npos
        || lower.find("mono") != std::string::npos;
}

// Stands in for whitespace between two tags so the XML parser keeps it as a
// text node. U+E000 is private use: it never occurs in a document's text.
constexpr const char* kKeptWhitespace = "\xEE\x80\x80";

// Replaces every whitespace-only stretch between '>' and '<' with one
// kKeptWhitespace marker. Between block elements the marker is ignored (only
// elements are read there); inside a paragraph it is the space ODF says it
// is (see AppendXmlText).
void ProtectInterElementWhitespace(std::string& xml) {
    std::string out;
    out.reserve(xml.size());
    size_t i = 0;
    while (i < xml.size()) {
        out.push_back(xml[i]);
        if (xml[i++] != '>') continue;
        size_t j = i;
        while (j < xml.size() && (xml[j] == ' ' || xml[j] == '\t' || xml[j] == '\n' || xml[j] == '\r')) ++j;
        if (j > i && j < xml.size() && xml[j] == '<') {
            out += kKeptWhitespace;
            i = j;
        }
    }
    xml.swap(out);
}

class OdtReader {
public:
    bool Load(const std::string& filePath, UCRichDocument& doc, std::string& error) {
        doc_ = &doc;
        if (!zip_.Open(filePath)) {
            error = "The file is not a valid OpenDocument text file (.odt): " + filePath;
            return false;
        }
        std::string contentXml;
        if (!zip_.ReadEntry("content.xml", contentXml)) {
            error = "The OpenDocument package has no content.xml: " + filePath;
            return false;
        }

        // styles.xml carries the named/automatic styles AND the master pages
        // whose headers/footers hold letterhead content (bank details,
        // Handelsregister lines, ...). Keep the parsed document alive until
        // after the body so the header/footer elements can be walked.
        std::string stylesXml;
        tinyxml2::XMLDocument stylesDoc;
        tinyxml2::XMLElement* stylesRoot = nullptr;
        bool haveStyles = zip_.ReadEntry("styles.xml", stylesXml);
        if (haveStyles) ProtectInterElementWhitespace(stylesXml);   // header/footer text
        if (haveStyles && stylesDoc.Parse(stylesXml.c_str()) == tinyxml2::XML_SUCCESS) {
            stylesRoot = stylesDoc.FirstChildElement("office:document-styles");
            if (stylesRoot) {
                CollectFontFaces(stylesRoot->FirstChildElement("office:font-face-decls"));
                CollectDefaultStyle(stylesRoot->FirstChildElement("office:styles"));
                CollectGradients(stylesRoot->FirstChildElement("office:styles"));
                CollectStyles(stylesRoot->FirstChildElement("office:styles"));
                CollectNamedStyles(stylesRoot->FirstChildElement("office:styles"));
                CollectStyles(stylesRoot->FirstChildElement("office:automatic-styles"));
                CollectListStyles(stylesRoot->FirstChildElement("office:styles"));
                CollectListStyles(stylesRoot->FirstChildElement("office:automatic-styles"));
            }
        }

        // Whitespace-only text between two spans ("<span>bold</span> <span>")
        // is a real space, but tinyxml2 drops whitespace-only text nodes.
        ProtectInterElementWhitespace(contentXml);
        tinyxml2::XMLDocument contentDoc;
        if (contentDoc.Parse(contentXml.c_str()) != tinyxml2::XML_SUCCESS) {
            error = "The document content is not valid XML: " + filePath;
            return false;
        }
        auto* root = contentDoc.FirstChildElement("office:document-content");
        if (!root) {
            error = "The file is not an OpenDocument text document: " + filePath;
            return false;
        }
        CollectFontFaces(root->FirstChildElement("office:font-face-decls"));
        CollectStyles(root->FirstChildElement("office:automatic-styles"));
        CollectListStyles(root->FirstChildElement("office:automatic-styles"));

        auto* body = root->FirstChildElement("office:body");
        auto* text = body ? body->FirstChildElement("office:text") : nullptr;
        if (!text) {
            error = "The OpenDocument file has no text body: " + filePath;
            return false;
        }
        // The linear block model has no page chrome, so the page header is
        // emitted before the body and the page footer after it, each set off
        // with a rule. A letterhead typically defines a dedicated first-page
        // master (its own header/footer) distinct from the plain continuation
        // master; a single page always draws both its header and footer from
        // the one master applied to it, so pick the master actually applied to
        // the document's first page and render only its regions. Rendering a
        // header from an unrelated master (e.g. the continuation master's
        // "Seite N / N" page-number line, which never shows on a one-page
        // letter) would inject chrome the reader never displays.
        LoadSettings();
        tinyxml2::XMLElement* masterPage = ResolveMasterPage(stylesRoot, text);
        LoadPageSetup(stylesRoot, masterPage);
        // The first page draws its header and footer from its master; the
        // pages after it from that master's next-style-name, when it names
        // another one (a letterhead's first page and its continuation pages).
        ParseMasterPage(masterPage, doc_->firstPageFurniture);
        tinyxml2::XMLElement* nextMaster = FollowingMasterPage(stylesRoot, masterPage);
        auto* headerFirst = masterPage ? masterPage->FirstChildElement("style:header-first") : nullptr;
        auto* footerFirst = masterPage ? masterPage->FirstChildElement("style:footer-first") : nullptr;
        if (headerFirst || footerFirst) {
            // ODF 1.3: one master page with its own first-page header and
            // footer ("Same content on first page" off in Writer).
            doc_->pageFurniture = doc_->firstPageFurniture;
            doc_->firstPageFurniture = RichPageFurniture{};
            ParseMasterPageRegion(headerFirst, doc_->firstPageFurniture.header);
            ParseMasterPageRegion(footerFirst, doc_->firstPageFurniture.footer);
            doc_->firstPageDiffers = true;
        } else if (nextMaster && nextMaster != masterPage) {
            ParseMasterPage(nextMaster, doc_->pageFurniture);
            doc_->firstPageDiffers = true;
        } else {
            doc_->pageFurniture = doc_->firstPageFurniture;
            doc_->firstPageFurniture = RichPageFurniture{};
        }
        LoadTrackedChanges(text->FirstChildElement("text:tracked-changes"));
        ParseBlockContainer(text, 0, "");
        if (!pendingPageFrames_.empty()) {
            // Frames after the last paragraph: one of their own carries them.
            RichDocBlock carrier;
            carrier.type = RichBlockType::Paragraph;
            carrier.runs = std::move(pendingPageFrames_);
            pendingPageFrames_.clear();
            doc_->blocks.push_back(std::move(carrier));
        }
        ApplySections();
        if (!doc_->notes.empty()) doc_->UpdateNoteMarks();
        LoadMetadata();
        return true;
    }

private:
    UCZipPackageReader zip_;
    UCRichDocument* doc_ = nullptr;
    // Page breaks (fo:break-before/after="page") only take effect for
    // paragraphs in the main text flow. A paragraph carrying that property
    // inside a positioned frame, text box, table cell or header/footer does
    // not start a new page (ODF/CSS fragmentation applies to in-flow boxes),
    // so break emission is gated on this flag.
    bool inMainFlow_ = true;
    int frameDepth_ = 0;                       // inside a text frame's content
    // Page-anchored frames met between paragraphs, waiting for the next
    // paragraph of the main flow to anchor them.
    std::vector<RichTextRun> pendingPageFrames_;
    bool inTableOfContents_ = false;           // paragraphs are contents entries
    std::map<std::string, RichSectionSetup> sectionStyles_;   // section style -> columns
    struct SectionRange { size_t start; size_t end; RichSectionSetup setup; };
    std::vector<SectionRange> sectionRanges_;

    // Sections in columns become the model's sections: one starting at the
    // section's first block, and a single-column one after it.
    void ApplySections() {
        for (const SectionRange& range : sectionRanges_) {
            RichSectionSetup setup = range.setup;
            setup.newPage = false;
            if (range.start == 0) doc_->firstSection = setup;
            else {
                doc_->blocks[range.start].sectionStart = true;
                doc_->blocks[range.start].section = setup;
            }
            if (range.end < doc_->blocks.size() && !doc_->blocks[range.end].sectionStart) {
                doc_->blocks[range.end].sectionStart = true;
                doc_->blocks[range.end].section = RichSectionSetup{};
            }
        }
    }

    // A paragraph of a table of contents: its level (from a "Contents N"
    // style, else its indent), and the page number after its tab made a page
    // reference to the heading its link points at, so it keeps itself right.
    void MakeContentsEntry(RichDocBlock& block, const std::string& styleName) {
        int level = 0;
        for (const std::string& candidate : {WordFormatInternal::ToLower(styleName),
                                             WordFormatInternal::ToLower(ParentStyleName(styleName))}) {
            if (candidate.find("contents") == std::string::npos && candidate.find("toc") == std::string::npos) continue;
            const size_t digit = candidate.find_last_of("123456789");
            if (digit != std::string::npos) { level = candidate[digit] - '0'; break; }
        }
        if (level <= 0) level = 1 + static_cast<int>(std::lround(std::max(0.0f, block.leftIndentPt) / 12.0f));
        block.tocLevel = std::clamp(level, 1, 9);
        std::string target;
        for (const RichTextRun& run : block.runs) {
            if (run.linkTarget.size() > 1 && run.linkTarget[0] == '#') target = run.linkTarget.substr(1);
            if (run.field == RichTextRun::Field::PageReference) return;
        }
        if (target.empty() || block.runs.empty()) return;
        RichTextRun& last = block.runs.back();
        std::string digits = last.text;
        const size_t tab = digits.find_last_of('\t');
        std::string before;
        if (tab != std::string::npos) {
            before = digits.substr(0, tab + 1);
            digits = digits.substr(tab + 1);
        }
        if (digits.empty() || digits.find_first_not_of("0123456789") != std::string::npos) return;
        RichTextRun number = last;
        number.text = digits;
        number.field = RichTextRun::Field::PageReference;
        number.fieldArgument = target;
        number.linkTarget.clear();
        number.lineBreakBefore = before.empty() && last.lineBreakBefore;
        if (before.empty()) {
            last = number;
        } else {
            last.text = before;
            block.runs.push_back(number);
        }
    }
    std::map<std::string, OdtTextProps> styles_;
    std::set<std::string> namedStyles_;          // ODF names of the office:styles styles
    // list style name -> (level -> ordered?)
    std::map<std::string, std::map<int, bool>> listStyles_;
    // list style name -> (level -> how its label reads)
    struct ListLevelLabel {
        RichNumberFormat format = RichNumberFormat::Decimal;
        std::string numberTemplate;
        std::string bulletText;
    };
    std::map<std::string, std::map<int, ListLevelLabel>> listLabels_;
    // list style name -> (level -> text:start-value of a numbered level)
    std::map<std::string, std::map<int, int>> listStartValues_;
    // table-column style name -> column width in points
    std::map<std::string, float> columnWidths_;
    std::map<std::string, RichTableCell> cellStyles_;          // table-cell style -> borders, fill
    // Graphic styles: how a frame wraps and where it sits. Empty = not stated
    // (then the parent style's, then Writer's defaults).
    struct GraphicPlacement {
        std::string parent, wrap, runThrough, horizontalPos, horizontalRel, verticalPos, verticalRel;
        // A text frame's look: its fill, border (or a shape's stroke) and
        // padding, and its least height.
        std::string fill, fillColor, backgroundColor, border, stroke, strokeColor, strokeWidth, padding, minHeight;
        // A drawn shape's line ends and dash, and where its words sit.
        std::string strokeDash, markerStart, markerEnd, textAreaVertical, paddingLeft;
        std::string fillGradientName, fillHatchName, fillHatchSolid, fillImageName, repeat;
    };
    std::map<std::string, GraphicPlacement> graphicStyles_;
    GraphicPlacement defaultGraphic_;                          // style:default-style family="graphic"
    // A drawn shape's graphic property: its style's, else the default's.
    std::string ShapeAttribute(const std::string& styleName, std::string GraphicPlacement::*field) const {
        const std::string value = GraphicAttribute(styleName, field);
        return value.empty() ? defaultGraphic_.*field : value;
    }
    std::string GraphicAttribute(const std::string& styleName,
                                 std::string GraphicPlacement::*field) const {
        std::string name = styleName;
        for (int depth = 0; depth < 8 && !name.empty(); ++depth) {
            auto it = graphicStyles_.find(name);
            if (it == graphicStyles_.end()) break;
            if (!(it->second.*field).empty()) return it->second.*field;
            name = it->second.parent;
        }
        return "";
    }
    struct TablePlacement {
        float widthPt = 0.0f;
        float widthPercent = 0.0f;
        RichTextAlign align = RichTextAlign::Left;
        float indentPt = 0.0f;
    };
    std::map<std::string, TablePlacement> tablePlacements_;    // table style -> width, position
    std::vector<std::string> columnDefaultCellStyles_;         // of the table being read
    std::map<std::string, std::string> fontFamilies_;          // font-face name -> family
    bool tabsRelativeToIndent_ = true;                         // Writer's default
    // Numbering runs per LIST, not per text:list element: a list that
    // continues another (text:continue-numbering / text:continue-list)
    // shares its counters, which is how "1. <note> 2." keeps counting.
    RichListNumbering numbering_;
    std::string currentListKey_;                               // top-level list being read
    std::map<std::string, std::string> lastListKeyForStyle_;   // list style -> its latest list
    std::map<std::string, std::string> listKeyForXmlId_;       // xml:id -> list
    int listCount_ = 0;

    static const char* Attr(const tinyxml2::XMLElement* e, const char* name) {
        const char* v = e->Attribute(name);
        return v ? v : "";
    }

    // style:font-name refers to a font-face declaration, whose name is only a
    // key ("Liberation Sans1", "StarSymbol1"); the family is its
    // svg:font-family, possibly quoted and with fallbacks after a comma.
    void CollectFontFaces(tinyxml2::XMLElement* decls) {
        if (!decls) return;
        for (auto* face = decls->FirstChildElement("style:font-face"); face;
             face = face->NextSiblingElement("style:font-face")) {
            std::string family = Attr(face, "svg:font-family");
            size_t comma = family.find(',');
            if (comma != std::string::npos) family.resize(comma);
            while (!family.empty() && (family.front() == '\'' || family.front() == '"' || family.front() == ' ')) {
                family.erase(family.begin());
            }
            while (!family.empty() && (family.back() == '\'' || family.back() == '"' || family.back() == ' ')) {
                family.pop_back();
            }
            std::string name = Attr(face, "style:name");
            if (!name.empty() && !family.empty()) fontFamilies_[name] = family;
        }
    }

    // An ODF border: "0.5pt solid #000000", "0.06pt double #c0c0c0", "none".
    static RichBorder ParseBorder(const std::string& value) {
        RichBorder border;
        std::istringstream tokens(value);
        std::string token;
        bool none = false;
        float width = -1.0f;
        while (tokens >> token) {
            const std::string lower = WordFormatInternal::ToLower(token);
            if (lower == "none" || lower == "hidden") none = true;
            else if (!lower.empty() && lower[0] == '#') border.color = token;
            else if (lower == "thin") width = 0.75f;
            else if (lower == "medium") width = 1.5f;
            else if (lower == "thick") width = 3.0f;
            else if (!lower.empty() && (std::isdigit(static_cast<unsigned char>(lower[0])) || lower[0] == '.')) {
                width = ParseLengthPt(token);
            }
            else if (std::isalpha(static_cast<unsigned char>(lower[0]))) {
                border.style = RichBorderStyleFromName(lower);   // solid, double, dotted, ...
            }
        }
        if (!none) border.widthPt = width >= 0.0f ? width : (value.empty() ? 0.0f : 0.75f);
        if (border.widthPt <= 0.0f) border = RichBorder{};
        return border;
    }

    // Borders and background of a table-cell style. Unset sides stay empty.
    static RichTableCell ReadCellFormat(tinyxml2::XMLElement* props) {
        RichTableCell format;
        const std::string all = Attr(props, "fo:border");
        auto side = [&](const char* name) {
            const char* v = props->Attribute(name);
            return ParseBorder(v ? std::string(v) : all);
        };
        format.borderTop = side("fo:border-top");
        format.borderBottom = side("fo:border-bottom");
        format.borderLeft = side("fo:border-left");
        format.borderRight = side("fo:border-right");
        std::string background = Attr(props, "fo:background-color");
        if (!background.empty() && background != "transparent") format.backgroundColor = background;
        const std::string vertical = Attr(props, "style:vertical-align");
        format.verticalAlign = vertical == "middle" ? RichVerticalAlign::Middle
                             : vertical == "bottom" ? RichVerticalAlign::Bottom : RichVerticalAlign::Top;
        // Padding: one value for all sides, or per side.
        const char* allPadding = props->Attribute("fo:padding");
        auto padding = [&](const char* name) {
            const char* v = props->Attribute(name);
            if (!v) v = allPadding;
            return v ? ParseLengthPt(v) : -1.0f;
        };
        format.paddingTopPt = padding("fo:padding-top");
        format.paddingBottomPt = padding("fo:padding-bottom");
        format.paddingLeftPt = padding("fo:padding-left");
        format.paddingRightPt = padding("fo:padding-right");
        return format;
    }

    // Lengths in a paragraph-properties element; "" leaves a field unset.
    static void ReadParagraphGeometry(tinyxml2::XMLElement* pp, OdtTextProps& props) {
        auto length = [&](const char* name, float& out) {
            const char* v = pp->Attribute(name);
            if (v && *v) out = ParseLengthPt(v);
        };
        length("fo:margin-left", props.marginLeft);
        length("fo:margin-right", props.marginRight);
        length("fo:text-indent", props.textIndent);
        length("fo:margin-top", props.marginTop);
        length("fo:margin-bottom", props.marginBottom);
        // Only a proportional line height maps onto the model's multiple; a
        // fixed "0.5cm" or style:line-height-at-least is left to the view.
        std::string lineHeight = Attr(pp, "fo:line-height");
        const std::string atLeast = Attr(pp, "style:line-height-at-least");
        if (!atLeast.empty()) {
            props.lineHeightPt = ParseLengthPt(atLeast);
            props.lineHeightAtLeast = true;
        } else if (!lineHeight.empty() && lineHeight.back() != '%' && lineHeight != "normal") {
            props.lineHeightPt = ParseLengthPt(lineHeight);   // "0.6cm": exactly this
            props.lineHeightAtLeast = false;
        }
        // Frame and fill. Any of them stated replaces the inherited set.
        const std::string all = Attr(pp, "fo:border");
        const char* sides[4] = {"fo:border-top", "fo:border-bottom", "fo:border-left", "fo:border-right"};
        const std::string background = Attr(pp, "fo:background-color");
        bool anyBorder = !all.empty();
        for (const char* side : sides) anyBorder = anyBorder || pp->Attribute(side);
        if (anyBorder || !background.empty()) {
            props.hasFrame = true;
            for (int i = 0; i < 4; ++i) {
                const char* v = pp->Attribute(sides[i]);
                props.frame[i] = ParseBorder(v ? std::string(v) : all);
            }
            props.paragraphBackground = background == "transparent" ? "" : background;
        }
        if (!lineHeight.empty() && lineHeight.back() == '%') {
            float percent = 0.0f;
            if (TryParseFloat(lineHeight.substr(0, lineHeight.size() - 1), percent) && percent > 0) {
                props.lineSpacing = percent / 100.0f;
            }
        } else if (lineHeight == "normal") {
            props.lineSpacing = 1.0f;
        }
        if (auto* stops = pp->FirstChildElement("style:tab-stops")) {
            props.hasTabStops = true;
            props.tabStops.clear();
            for (auto* stop = stops->FirstChildElement("style:tab-stop"); stop;
                 stop = stop->NextSiblingElement("style:tab-stop")) {
                RichTabStop tab;
                tab.positionPt = ParseLengthPt(Attr(stop, "style:position"));
                std::string type = Attr(stop, "style:type");
                tab.kind = type == "center" ? RichTabKind::Center
                         : type == "right" ? RichTabKind::Right
                         : type == "char" ? RichTabKind::Decimal : RichTabKind::Left;
                props.tabStops.push_back(tab);
            }
        }
    }

    // Default paragraph properties (style:default-style): the document's
    // default tab distance.
    // draw:gradient definitions, by name: the stops, angle and kind a
    // shape's draw:fill-gradient-name refers to. ODF turns its angle
    // anticlockwise from top-to-bottom; the model's runs clockwise from
    // left-to-right. Axial ones put the start colour on the middle line and
    // the end at both edges; radial and the other round kinds the start at
    // the edge and the end at the centre.
    struct OdfGradient {
        std::vector<RichShape::GradientStop> stops;
        float angleDeg = 0.0f;
        bool radial = false;
        float centerX = 0.5f, centerY = 0.5f;
    };
    std::map<std::string, OdfGradient> gradients_;
    // draw:hatch definitions: lines, single or crossed, as the nearest of
    // Word's patterns, with their colour.
    struct OdfHatch {
        std::string pattern;
        std::string color;
    };
    std::map<std::string, OdfHatch> hatches_;
    // draw:fill-image: its picture's href, or the picture itself inline
    // (office:binary-data, as a pattern's tile is written); loaded once.
    struct OdfFillImage {
        std::string href, base64;
        int mediaIndex = -2;                                   // -2 = not loaded yet
    };
    std::map<std::string, OdfFillImage> fillImages_;
    int FillImageMedia(OdfFillImage& image) {
        if (image.mediaIndex != -2) return image.mediaIndex;
        image.mediaIndex = -1;
        if (!image.href.empty()) {
            image.mediaIndex = LoadPicture(image.href);
        } else if (!image.base64.empty()) {
            std::vector<uint8_t> bytes = Base64Decode(image.base64);
            const bool png = bytes.size() > 4 && bytes[0] == 0x89 && bytes[1] == 'P';
            const bool jpeg = bytes.size() > 3 && bytes[0] == 0xFF && bytes[1] == 0xD8;
            const bool bmp = bytes.size() > 2 && bytes[0] == 'B' && bytes[1] == 'M';
            if (png || jpeg || bmp) {
                const std::string name = "fill" + std::to_string(doc_->media.size() + 1) + (png ? ".png" : jpeg ? ".jpg" : ".bmp");
                image.mediaIndex = doc_->AddMedia(name, UCRichDocument::MimeTypeForImageName(name), std::move(bytes));
            }
        }
        return image.mediaIndex;
    }
    void CollectGradients(tinyxml2::XMLElement* container) {
        if (!container) return;
        for (auto* element = container->FirstChildElement("draw:fill-image"); element;
             element = element->NextSiblingElement("draw:fill-image")) {
            OdfFillImage image;
            image.href = Attr(element, "xlink:href");
            if (auto* data = element->FirstChildElement("office:binary-data")) {
                image.base64 = data->GetText() ? data->GetText() : "";
            }
            fillImages_[Attr(element, "draw:name")] = std::move(image);
        }
        for (auto* element = container->FirstChildElement("draw:hatch"); element;
             element = element->NextSiblingElement("draw:hatch")) {
            // draw:rotation: tenths of a degree, or with a unit; anticlockwise.
            const std::string rotationText = Attr(element, "draw:rotation");
            double rotation = 0.0;
            const size_t ends = WordFormatInternal::ReadNumberAt(rotationText, 0, rotation);
            if (rotationText.substr(ends).find("deg") == std::string::npos) rotation /= 10.0;
            rotation = std::fmod(rotation, 180.0);
            if (rotation < 0.0) rotation += 180.0;
            const int nearest = static_cast<int>(std::lround(rotation / 45.0)) % 4;   // 0, 45, 90, 135
            const std::string style = Attr(element, "draw:style");
            const float distance = ParseLengthPt(Attr(element, "draw:distance"));
            const bool close = distance > 0.0f && distance <= 2.5f;
            OdfHatch hatch;
            if (style == "double" || style == "triple") {
                hatch.pattern = nearest % 2 == 0 ? (close ? "smGrid" : "lgGrid") : "diagCross";
            } else {
                static const char* kSingle[4][2] = {{"horz", "ltHorz"}, {"upDiag", "ltUpDiag"},
                                                    {"vert", "ltVert"}, {"dnDiag", "ltDnDiag"}};
                hatch.pattern = kSingle[nearest][close ? 1 : 0];
            }
            hatch.color = Attr(element, "draw:color");
            if (hatch.color.size() != 7) hatch.color = "#000000";
            hatches_[Attr(element, "draw:name")] = std::move(hatch);
        }
        for (auto* element = container->FirstChildElement("draw:gradient"); element;
             element = element->NextSiblingElement("draw:gradient")) {
            const std::string kind = Attr(element, "draw:style");
            const std::string start = Attr(element, "draw:start-color");
            const std::string end = Attr(element, "draw:end-color");
            if (start.size() != 7 || end.size() != 7) continue;
            OdfGradient gradient;
            // draw:angle: degrees, or with a unit (read as Writer reads it).
            const std::string angleText = Attr(element, "draw:angle");
            double angle = 0.0;
            const size_t ends = WordFormatInternal::ReadNumberAt(angleText, 0, angle);
            const std::string unit = angleText.substr(ends);
            if (unit.find("grad") != std::string::npos) angle *= 0.9;
            else if (unit.find("rad") != std::string::npos) angle *= 180.0 / 3.14159265358979323846;
            double turn = std::fmod(90.0 - angle, 360.0);
            if (turn < 0.0) turn += 360.0;
            gradient.angleDeg = static_cast<float>(turn);
            if (kind == "linear" || kind.empty()) {
                gradient.stops = {{0.0f, start}, {1.0f, end}};
            } else if (kind == "axial") {
                gradient.stops = {{0.0f, end}, {0.5f, start}, {1.0f, end}};   // the start colour on the axis
            } else {
                gradient.radial = true;
                gradient.stops = {{0.0f, end}, {1.0f, start}};
                double centre = 50.0;
                if (WordFormatInternal::ReadNumberAt(Attr(element, "draw:cx"), 0, centre) > 0) {
                    gradient.centerX = static_cast<float>(std::clamp(centre / 100.0, 0.0, 1.0));
                }
                centre = 50.0;
                if (WordFormatInternal::ReadNumberAt(Attr(element, "draw:cy"), 0, centre) > 0) {
                    gradient.centerY = static_cast<float>(std::clamp(centre / 100.0, 0.0, 1.0));
                }
            }
            gradients_[Attr(element, "draw:name")] = std::move(gradient);
        }
    }

    void CollectDefaultStyle(tinyxml2::XMLElement* container) {
        if (!container) return;
        for (auto* style = container->FirstChildElement("style:default-style"); style;
             style = style->NextSiblingElement("style:default-style")) {
            if (std::string(Attr(style, "style:family")) == "graphic") {
                // What a drawn shape states nothing about falls back to.
                if (auto* graphic = style->FirstChildElement("style:graphic-properties")) {
                    GraphicPlacement& fallback = defaultGraphic_;
                    fallback.fill = Attr(graphic, "draw:fill");
                    fallback.fillColor = Attr(graphic, "draw:fill-color");
                    fallback.stroke = Attr(graphic, "draw:stroke");
                    fallback.strokeColor = Attr(graphic, "svg:stroke-color");
                    fallback.strokeWidth = Attr(graphic, "svg:stroke-width");
                }
                continue;
            }
            if (std::string(Attr(style, "style:family")) != "paragraph") continue;
            if (auto* pp = style->FirstChildElement("style:paragraph-properties")) {
                float distance = ParseLengthPt(Attr(pp, "style:tab-stop-distance"));
                if (distance > 0) doc_->defaultTabStopPt = distance;
            }
        }
    }

    // settings.xml: whether tab positions count from the paragraph's indent
    // (Writer's own default) or from the page margin (Word's convention,
    // kept in documents Writer imported from Word).
    void LoadSettings() {
        std::string settingsXml;
        if (!zip_.ReadEntry("settings.xml", settingsXml)) return;
        const std::string key = "config:name=\"TabsRelativeToIndent\"";
        size_t at = settingsXml.find(key);
        if (at == std::string::npos) return;
        size_t close = settingsXml.find('>', at);
        if (close == std::string::npos) return;
        tabsRelativeToIndent_ = settingsXml.compare(close + 1, 4, "true") == 0;
    }

    // Applies a paragraph style's geometry to a block, in the model's terms.
    void ApplyGeometry(RichDocBlock& block, const OdtTextProps& props) const {
        auto value = [](float v) { return std::isnan(v) ? 0.0f : v; };
        block.leftIndentPt = value(props.marginLeft);
        block.rightIndentPt = value(props.marginRight);
        block.firstLineIndentPt = value(props.textIndent);
        // ODF's default margins are zero, so unstated spacing is zero too.
        block.spaceBeforePt = value(props.marginTop);
        block.spaceAfterPt = value(props.marginBottom);
        block.lineSpacing = std::isnan(props.lineSpacing) ? 0.0f : props.lineSpacing;
        if (!std::isnan(props.lineHeightPt) && props.lineHeightPt > 0.0f) {
            block.lineHeightPt = props.lineHeightPt;
            block.lineHeightAtLeast = props.lineHeightAtLeast;
            block.lineSpacing = 0.0f;
        }
        if (props.hasFrame) {
            block.paragraphBorderTop = props.frame[0];
            block.paragraphBorderBottom = props.frame[1];
            block.paragraphBorderLeft = props.frame[2];
            block.paragraphBorderRight = props.frame[3];
            block.paragraphBackground = props.paragraphBackground;
        }
        block.tabStops = props.tabStops;
        if (tabsRelativeToIndent_) {
            for (RichTabStop& tab : block.tabStops) tab.positionPt += block.leftIndentPt;
        }
        std::sort(block.tabStops.begin(), block.tabStops.end(),
                  [](const RichTabStop& a, const RichTabStop& b) { return a.positionPt < b.positionPt; });
    }

    std::string FamilyForFontName(const std::string& fontName) const {
        auto it = fontFamilies_.find(fontName);
        return it != fontFamilies_.end() ? it->second : fontName;
    }

    void CollectStyles(tinyxml2::XMLElement* container) {
        if (!container) return;
        for (auto* style = container->FirstChildElement("style:style"); style;
             style = style->NextSiblingElement("style:style")) {
            if (std::string(Attr(style, "style:family")) == "section") {
                // A section's columns.
                RichSectionSetup setup;
                if (auto* sp = style->FirstChildElement("style:section-properties")) {
                    if (auto* cols = sp->FirstChildElement("style:columns")) {
                        setup.columns = std::clamp(cols->IntAttribute("fo:column-count", 1), 1, 9);
                        const char* gap = Attr(cols, "fo:column-gap");
                        if (gap && *gap) setup.columnGapPt = ParseLengthPt(gap);
                    }
                }
                sectionStyles_[Attr(style, "style:name")] = setup;
                continue;
            }
            OdtTextProps props;
            props.parentStyleName = Attr(style, "style:parent-style-name");
            props.masterPageName = Attr(style, "style:master-page-name");
            if (auto* tp = style->FirstChildElement("style:text-properties")) {
                std::string weight = Attr(tp, "fo:font-weight");
                if (!weight.empty()) props.bold = (weight != "normal") ? 1 : 0;
                std::string fontStyle = Attr(tp, "fo:font-style");
                if (!fontStyle.empty()) props.italic = (fontStyle != "normal") ? 1 : 0;
                std::string underline = Attr(tp, "style:text-underline-style");
                if (!underline.empty()) props.underline = (underline != "none") ? 1 : 0;
                std::string strike = Attr(tp, "style:text-line-through-style");
                if (!strike.empty()) props.strike = (strike != "none") ? 1 : 0;
                std::string position = Attr(tp, "style:text-position");
                if (!position.empty()) {
                    props.superscript = (position.rfind("super", 0) == 0) ? 1 : 0;
                    props.subscript = (position.rfind("sub", 0) == 0) ? 1 : 0;
                }
                std::string display = Attr(tp, "text:display");
                if (!display.empty()) props.hidden = (display == "none") ? 1 : 0;
                props.color = Attr(tp, "fo:color");
                props.fontFamily = FamilyForFontName(Attr(tp, "style:font-name"));
                if (props.fontFamily.empty()) props.fontFamily = Attr(tp, "fo:font-family");
                props.fontSizePt = ParseLengthPt(Attr(tp, "fo:font-size"));
                const std::string tracking = Attr(tp, "fo:letter-spacing");
                if (tracking == "normal") props.letterSpacingPt = 0.0f;
                else if (!tracking.empty()) props.letterSpacingPt = ParseLengthPt(tracking);
                // A character background is a highlight; "transparent" says
                // explicitly that there is none.
                props.highlight = Attr(tp, "fo:background-color");
            }
            if (auto* pp = style->FirstChildElement("style:paragraph-properties")) {
                std::string align = Attr(pp, "fo:text-align");
                if (align == "center") props.align = RichTextAlign::Center;
                else if (align == "end" || align == "right") props.align = RichTextAlign::Right;
                else if (align == "justify") props.align = RichTextAlign::Justify;
                else if (align == "start" || align == "left") props.align = RichTextAlign::Left;
                const std::string mode = Attr(pp, "style:writing-mode");
                if (mode.rfind("rl", 0) == 0) props.rightToLeft = 1;
                else if (mode.rfind("lr", 0) == 0) props.rightToLeft = 0;
                ReadParagraphGeometry(pp, props);
                std::string border = Attr(pp, "fo:border-bottom");
                props.bottomBorder = !border.empty() && border != "none";
                props.pageBreakBefore = std::string(Attr(pp, "fo:break-before")) == "page";
            }
            std::string name = Attr(style, "style:name");
            if (auto* tableProps = style->FirstChildElement("style:table-properties")) {
                // Width (absolute, or relative "50%") and placement. "margins"
                // means the width follows from the margins; the stated width
                // and left margin then give the same box.
                TablePlacement placement;
                placement.widthPt = ParseLengthPt(Attr(tableProps, "style:width"));
                const std::string relative = Attr(tableProps, "style:rel-width");
                if (!relative.empty() && relative.back() == '%') {
                    TryParseFloat(relative.substr(0, relative.size() - 1), placement.widthPercent);
                }
                const std::string align = Attr(tableProps, "table:align");
                placement.align = align == "center" ? RichTextAlign::Center
                                : align == "right" ? RichTextAlign::Right : RichTextAlign::Left;
                placement.indentPt = ParseLengthPt(Attr(tableProps, "fo:margin-left"));
                if (!name.empty()) tablePlacements_[name] = placement;
            }
            if (auto* cellProps = style->FirstChildElement("style:table-cell-properties")) {
                if (!name.empty()) cellStyles_[name] = ReadCellFormat(cellProps);
            }
            if (auto* graphic = style->FirstChildElement("style:graphic-properties")) {
                GraphicPlacement placement;
                placement.parent = Attr(style, "style:parent-style-name");
                placement.wrap = Attr(graphic, "style:wrap");
                placement.runThrough = Attr(graphic, "style:run-through");
                placement.horizontalPos = Attr(graphic, "style:horizontal-pos");
                placement.horizontalRel = Attr(graphic, "style:horizontal-rel");
                placement.verticalPos = Attr(graphic, "style:vertical-pos");
                placement.verticalRel = Attr(graphic, "style:vertical-rel");
                placement.fill = Attr(graphic, "draw:fill");
                placement.fillColor = Attr(graphic, "draw:fill-color");
                placement.backgroundColor = Attr(graphic, "fo:background-color");
                placement.border = Attr(graphic, "fo:border");
                placement.stroke = Attr(graphic, "draw:stroke");
                placement.strokeColor = Attr(graphic, "svg:stroke-color");
                placement.strokeWidth = Attr(graphic, "svg:stroke-width");
                placement.padding = Attr(graphic, "fo:padding");
                placement.minHeight = Attr(graphic, "fo:min-height");
                placement.strokeDash = Attr(graphic, "draw:stroke-dash");
                placement.markerStart = Attr(graphic, "draw:marker-start");
                placement.markerEnd = Attr(graphic, "draw:marker-end");
                placement.textAreaVertical = Attr(graphic, "draw:textarea-vertical-align");
                placement.paddingLeft = Attr(graphic, "fo:padding-left");
                placement.fillGradientName = Attr(graphic, "draw:fill-gradient-name");
                placement.fillHatchName = Attr(graphic, "draw:fill-hatch-name");
                placement.fillHatchSolid = Attr(graphic, "draw:fill-hatch-solid");
                placement.fillImageName = Attr(graphic, "draw:fill-image-name");
                placement.repeat = Attr(graphic, "style:repeat");
                if (!name.empty()) graphicStyles_[name] = placement;
            }
            if (auto* cp = style->FirstChildElement("style:table-column-properties")) {
                // Absolute width wins; a relative "1234*" width is still a
                // valid proportion among the table's columns.
                float width = ParseLengthPt(Attr(cp, "style:column-width"));
                if (width <= 0) width = ParseLengthPt(Attr(cp, "style:rel-column-width"));
                if (width > 0 && !name.empty()) columnWidths_[name] = width;
            }
            props.headingLevel = HeadingLevelFromStyleName(name);
            if (props.headingLevel == 0) {
                props.headingLevel = HeadingLevelFromStyleName(Attr(style, "style:display-name"));
            }
            if (!name.empty()) styles_[name] = props;
        }
    }

    // The document's named styles (office:styles, not automatic ones) as
    // model styles. CollectStyles has read their properties already.
    void CollectNamedStyles(tinyxml2::XMLElement* container) {
        if (!container) return;
        for (auto* style = container->FirstChildElement("style:style"); style;
             style = style->NextSiblingElement("style:style")) {
            const std::string family = Attr(style, "style:family");
            if (family != "paragraph" && family != "text") continue;
            const std::string name = Attr(style, "style:name");
            auto found = styles_.find(name);
            if (name.empty() || found == styles_.end()) continue;
            const OdtTextProps& own = found->second;
            RichStyle named;
            named.id = OdfNameToStyleId(name);
            const std::string display = Attr(style, "style:display-name");
            named.name = display.empty() ? named.id : display;
            if (name == "Standard" && display.empty()) named.name = "Normal";
            named.kind = family == "text" ? RichStyle::Kind::Character : RichStyle::Kind::Paragraph;
            const std::string parent = Attr(style, "style:parent-style-name");
            if (!parent.empty()) named.basedOn = OdfNameToStyleId(parent);
            const std::string next = Attr(style, "style:next-style-name");
            if (!next.empty() && family == "paragraph") named.nextStyle = OdfNameToStyleId(next);
            RichStyleCharacter& c = named.character;
            if (own.bold >= 0) c.bold = own.bold == 1;
            if (own.italic >= 0) c.italic = own.italic == 1;
            if (own.underline >= 0) c.underline = own.underline == 1;
            if (own.strike >= 0) c.strikethrough = own.strike == 1;
            if (!own.color.empty()) c.color = own.color;
            if (!own.fontFamily.empty()) c.fontFamily = own.fontFamily;
            if (own.fontSizePt > 0.0f) c.fontSizePt = own.fontSizePt;
            if (!std::isnan(own.letterSpacingPt)) c.letterSpacingPt = own.letterSpacingPt;
            if (named.kind == RichStyle::Kind::Paragraph) {
                RichStyleParagraph& p = named.paragraph;
                if (own.align != RichTextAlign::Default) p.align = own.align;
                if (!std::isnan(own.marginLeft)) p.leftIndentPt = own.marginLeft;
                if (!std::isnan(own.marginRight)) p.rightIndentPt = own.marginRight;
                if (!std::isnan(own.textIndent)) p.firstLineIndentPt = own.textIndent;
                if (!std::isnan(own.marginTop)) p.spaceBeforePt = own.marginTop;
                if (!std::isnan(own.marginBottom)) p.spaceAfterPt = own.marginBottom;
                if (!std::isnan(own.lineSpacing) && own.lineSpacing > 0.0f) p.lineSpacing = own.lineSpacing;
                const int outline = style->IntAttribute("style:default-outline-level", 0);
                const int level = outline > 0 ? outline : own.headingLevel;
                if (level >= 1 && level <= 6) p.headingLevel = level;
            }
            namedStyles_.insert(name);
            doc_->styles.push_back(std::move(named));
        }
    }

    std::string ParentStyleName(const std::string& styleName) const {
        auto found = styles_.find(styleName);
        return found != styles_.end() ? found->second.parentStyleName : "";
    }

    // The named style a paragraph or span has: its own style when that is a
    // named one, else the named style its automatic style is based on. As a
    // model id; "" for none.
    std::string NamedStyleFor(const std::string& styleName) const {
        if (styleName.empty()) return "";
        if (namedStyles_.count(styleName)) return OdfNameToStyleId(styleName);
        auto found = styles_.find(styleName);
        if (found != styles_.end() && namedStyles_.count(found->second.parentStyleName)) {
            return OdfNameToStyleId(found->second.parentStyleName);
        }
        return "";
    }

    void CollectListStyles(tinyxml2::XMLElement* container) {
        if (!container) return;
        for (auto* ls = container->FirstChildElement("text:list-style"); ls;
             ls = ls->NextSiblingElement("text:list-style")) {
            std::string name = Attr(ls, "style:name");
            if (name.empty()) continue;
            for (auto* level = ls->FirstChildElement(); level;
                 level = level->NextSiblingElement()) {
                std::string tag = level->Name() ? level->Name() : "";
                int levelNum = level->IntAttribute("text:level", 1);
                ListLevelLabel& label = listLabels_[name][levelNum];
                if (tag == "text:list-level-style-number") {
                    listStyles_[name][levelNum] = true;
                    listStartValues_[name][levelNum] =
                        std::max(1, level->IntAttribute("text:start-value", 1));
                    // "a)" / "1.2." / "(iv)": the format, the levels shown and
                    // the text around them, as a Word-style template.
                    label.format = NumberFormatFromOdf(Attr(level, "style:num-format"));
                    const int shown = std::clamp(level->IntAttribute("text:display-levels", 1), 1, levelNum);
                    std::string templ = Attr(level, "style:num-prefix");
                    for (int k = levelNum - shown + 1; k <= levelNum; ++k) {
                        templ += "%" + std::to_string(std::clamp(k, 1, 9));
                        if (k < levelNum) templ += ".";
                    }
                    templ += Attr(level, "style:num-suffix");
                    label.numberTemplate = templ;
                } else if (tag == "text:list-level-style-bullet"
                           || tag == "text:list-level-style-image") {
                    listStyles_[name][levelNum] = false;
                    label.bulletText = Attr(level, "text:bullet-char");
                    // A bullet from a symbol font (Wingdings "§", Symbol "·")
                    // is the character that font draws there.
                    auto* textProps = level->FirstChildElement("style:text-properties");
                    const std::string font = textProps
                            ? FamilyForFontName(Attr(textProps, "style:font-name")) : "";
                    if (!font.empty() && !label.bulletText.empty()) {
                        size_t at = 0;
                        const uint32_t cp = WordFormatInternal::DecodeUtf8(label.bulletText, at);
                        if (uint32_t unicode = WordFormatInternal::SymbolFontCharToUnicode(font, cp)) {
                            label.bulletText.clear();
                            WordFormatInternal::AppendUtf8(label.bulletText, unicode);
                        }
                    }
                }
            }
        }
    }

    static RichNumberFormat NumberFormatFromOdf(const std::string& format) {
        if (format == "a") return RichNumberFormat::LowerLetter;
        if (format == "A") return RichNumberFormat::UpperLetter;
        if (format == "i") return RichNumberFormat::LowerRoman;
        if (format == "I") return RichNumberFormat::UpperRoman;
        if (format.empty()) return RichNumberFormat::NoNumber;
        return RichNumberFormat::Decimal;
    }

    OdtTextProps ResolveStyle(const std::string& name, int depth = 0) const {
        auto it = styles_.find(name);
        if (it == styles_.end() || depth > 16) return OdtTextProps{};
        OdtTextProps props = it->second;
        if (!props.parentStyleName.empty()) {
            props.MergeParent(ResolveStyle(props.parentStyleName, depth + 1));
        }
        return props;
    }

    void ApplyPropsToRun(RichTextRun& run, const OdtTextProps& props) const {
        run.characterStyleId = props.characterStyle;
        if (props.bold == 1) run.bold = true;
        if (props.italic == 1) run.italic = true;
        if (props.underline == 1) run.underline = true;
        if (props.strike == 1) run.strikethrough = true;
        if (props.subscript == 1) run.subscript = true;
        if (props.superscript == 1) run.superscript = true;
        if (!props.color.empty()) run.color = props.color;
        if (!props.fontFamily.empty()) run.fontFamily = props.fontFamily;
        if (props.fontSizePt > 0) run.fontSizePt = props.fontSizePt;
        if (!std::isnan(props.letterSpacingPt)) run.letterSpacingPt = props.letterSpacingPt;
        // A monospace family is the inline-code signal in the model.
        if (IsMonospaceFamily(props.fontFamily)) run.code = true;
        if (!props.highlight.empty() && props.highlight != "transparent") run.highlightColor = props.highlight;
    }

    int LoadPicture(const std::string& href) {
        // Producers write both "Pictures/x.png" and "./Pictures/x.png";
        // external (linked, not embedded) pictures cannot be loaded.
        std::string path = href;
        if (path.rfind("./", 0) == 0) path = path.substr(2);
        if (path.empty() || path.find("://") != std::string::npos) return -1;
        std::vector<uint8_t> bytes;
        if (!zip_.ReadEntry(path, bytes)) return -1;
        std::string name = path;
        size_t slash = name.find_last_of('/');
        if (slash != std::string::npos) name = name.substr(slash + 1);
        return doc_->AddMedia(name, UCRichDocument::MimeTypeForImageName(name),
                              std::move(bytes));
    }

    // State for one paragraph while its inline content is being collected.
    struct InlineContext {
        std::vector<RichTextRun> runs;
        std::vector<RichDocBlock> trailingImages;   // images found inside the paragraph
        // draw:text-box contents anchored in the paragraph; their block
        // content is emitted right after the paragraph itself.
        std::vector<tinyxml2::XMLElement*> textBoxes;
        // Text boxes become text-frame runs (RichTextRun::frameIndex) where
        // a paragraph can carry one; elsewhere (a table cell) their text is
        // flattened as before.
        bool framesAsRuns = false;
        bool pendingLineBreak = false;
        bool endsInCollapsibleSpace = false;   // last text ended in XML whitespace
        std::vector<std::string> bookmarks;    // text:bookmark(-start) names met
    };

    // ODF white-space handling (ODF 1.3 part 3, 6.1.2) for character data
    // straight from the XML: tabs, CRs and newlines count as spaces, a run of
    // them is one space, and none opens a paragraph (or a line of a cell).
    // Spaces that must survive are spelled <text:s/>, which does not come
    // through here.
    void AppendXmlText(InlineContext& ctx, const std::string& raw, const OdtTextProps& props,
                       const std::string& linkTarget) {
        std::string text;
        text.reserve(raw.size());
        bool space = ctx.endsInCollapsibleSpace || ctx.runs.empty() || ctx.pendingLineBreak;
        for (size_t i = 0; i < raw.size(); ++i) {
            char c = raw[i];
            if (raw.compare(i, 3, kKeptWhitespace) == 0) {
                i += 2;
                c = ' ';
            }
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                if (!space) text.push_back(' ');
                space = true;
            } else {
                text.push_back(c);
                space = false;
            }
        }
        if (text.empty()) return;
        AppendRun(ctx, text, props, linkTarget);
        ctx.endsInCollapsibleSpace = text.back() == ' ';
    }

    void AppendRun(InlineContext& ctx, const std::string& text, const OdtTextProps& props,
                   const std::string& linkTarget) {
        if (text.empty()) return;
        if (props.hidden == 1) return;   // hidden text must not render
        ctx.endsInCollapsibleSpace = false;
        RichTextRun run;
        run.text = text;
        run.linkTarget = linkTarget;
        ApplyPropsToRun(run, props);
        run.lineBreakBefore = ctx.pendingLineBreak;
        ctx.pendingLineBreak = false;
        run.commentIds = ActiveCommentIds();
        if (activeInsertion_ >= 0) {
            run.change = RichTextRun::Change::Inserted;
            run.revision = activeInsertion_;
        }
        ctx.runs.push_back(std::move(run));
    }

    // Tracked changes: text:tracked-changes lists each changed region -
    // who and when, and for a deletion the deleted text; the body marks
    // insertions with change-start / change-end and deletions with a point.
    struct ChangedRegion {
        bool insertion = false;
        bool deletion = false;
        int revision = -1;
        std::string deletedText;
    };
    std::map<std::string, ChangedRegion> changedRegions_;
    int activeInsertion_ = -1;             // revision of the insertion being read

    void LoadTrackedChanges(tinyxml2::XMLElement* changes) {
        if (!changes) return;
        for (auto* region = changes->FirstChildElement("text:changed-region"); region;
             region = region->NextSiblingElement("text:changed-region")) {
            std::string id = Attr(region, "text:id");
            if (id.empty()) id = Attr(region, "xml:id");
            ChangedRegion out;
            tinyxml2::XMLElement* change = region->FirstChildElement("text:insertion");
            out.insertion = change != nullptr;
            if (!change) {
                change = region->FirstChildElement("text:deletion");
                out.deletion = change != nullptr;
            }
            if (!change) continue;             // format changes: not kept
            std::string author, date;
            if (auto* info = change->FirstChildElement("office:change-info")) {
                if (auto* creator = info->FirstChildElement("dc:creator")) author = ElementText(creator);
                if (auto* when = info->FirstChildElement("dc:date")) date = ElementText(when);
            }
            out.revision = -1;
            for (size_t i = 0; i < doc_->revisions.size(); i++) {
                if (doc_->revisions[i].author == author && doc_->revisions[i].date == date) out.revision = static_cast<int>(i);
            }
            if (out.revision < 0) {
                doc_->revisions.push_back({author, date});
                out.revision = static_cast<int>(doc_->revisions.size()) - 1;
            }
            if (out.deletion) {
                bool first = true;
                for (auto* p = change->FirstChildElement(); p; p = p->NextSiblingElement()) {
                    const std::string tag = p->Name() ? p->Name() : "";
                    if (tag != "text:p" && tag != "text:h") continue;
                    if (!first) out.deletedText += " ";
                    std::string line = ElementText(p);
                    for (size_t at; (at = line.find(kKeptWhitespace)) != std::string::npos;) line.replace(at, 3, " ");
                    out.deletedText += line;
                    first = false;
                }
            }
            changedRegions_[id] = std::move(out);
        }
    }

    // Comments (office:annotation): those whose range the text being read is
    // inside, by office:name; a comment without a name marks only the text
    // right after it.
    std::vector<std::pair<std::string, int>> activeAnnotations_;
    int pointAnnotation_ = -1;

    std::vector<int> ActiveCommentIds() {
        std::vector<int> ids;
        for (const auto& [name, index] : activeAnnotations_) ids.push_back(index);
        if (pointAnnotation_ >= 0) {
            ids.push_back(pointAnnotation_);
            pointAnnotation_ = -1;
        }
        return ids;
    }

    void ReadAnnotation(tinyxml2::XMLElement* annotation) {
        RichComment comment;
        if (auto* creator = annotation->FirstChildElement("dc:creator")) comment.author = ElementText(creator);
        if (auto* date = annotation->FirstChildElement("dc:date")) comment.date = ElementText(date);
        comment.resolved = std::string(Attr(annotation, "loext:resolved")) == "true";
        bool first = true;
        for (auto* p = annotation->FirstChildElement(); p; p = p->NextSiblingElement()) {
            const std::string tag = p->Name() ? p->Name() : "";
            if (tag != "text:p" && tag != "text:h") continue;
            if (!first) comment.text += "\n";
            std::string line = ElementText(p);
            for (size_t at; (at = line.find(kKeptWhitespace)) != std::string::npos;) line.replace(at, 3, " ");
            comment.text += line;
            first = false;
        }
        doc_->comments.push_back(std::move(comment));
        const int index = static_cast<int>(doc_->comments.size()) - 1;
        const std::string name = Attr(annotation, "office:name");
        if (name.empty()) pointAnnotation_ = index;
        else activeAnnotations_.emplace_back(name, index);
    }

    // Embedded formula objects live as sub-documents inside the package
    // (e.g. "Object 1/content.xml" holding MathML). Converted formulas are
    // appended as $latex$ text so the markdown pipeline can render them.
    bool ParseFormulaObject(tinyxml2::XMLElement* frame, const OdtTextProps& props,
                            const std::string& linkTarget, InlineContext& ctx) {
        auto* object = frame->FirstChildElement("draw:object");
        if (!object) return false;
        std::string href = Attr(object, "xlink:href");
        if (href.rfind("./", 0) == 0) href = href.substr(2);
        if (href.empty()) return false;
        std::string contentXml;
        if (!zip_.ReadEntry(href + "/content.xml", contentXml)
            && !zip_.ReadEntry(href, contentXml)) {
            return false;
        }
        tinyxml2::XMLDocument mathDoc;
        if (mathDoc.Parse(contentXml.c_str()) != tinyxml2::XML_SUCCESS) return false;
        auto* root = mathDoc.RootElement();
        if (!root) return false;
        std::string rootName = root->Name() ? root->Name() : "";
        if (rootName != "math" && rootName != "math:math"
            && rootName.rfind(":math") == std::string::npos) {
            return false;
        }
        std::string latex = WordMath::MathMLToLatex(root);
        if (latex.empty()) return false;
        AppendRun(ctx, "$" + latex + "$", props, linkTarget);
        return true;
    }

    bool ParseImageFrame(tinyxml2::XMLElement* frame, InlineContext& ctx) {
        auto* image = frame->FirstChildElement("draw:image");
        if (!image) return false;
        int mediaIndex = LoadPicture(Attr(image, "xlink:href"));
        if (mediaIndex < 0) return false;

        const float widthPt = ParseLengthPt(Attr(frame, "svg:width"));
        const float heightPt = ParseLengthPt(Attr(frame, "svg:height"));
        const std::string altText = Attr(frame, "draw:name");

        // text:anchor-type="as-char" is a picture anchored *in* the text;
        // "paragraph" and "char" anchor a floating one to its paragraph, and
        // "page" one to the page (it travels with the paragraph that follows
        // it, which puts it on that paragraph's page). All of them belong in
        // the run stream. A frame anchored to another frame stays a block of
        // its own.
        const std::string anchorType = Attr(frame, "text:anchor-type");
        if (anchorType == "as-char" || anchorType == "paragraph" || anchorType == "char"
            || (anchorType == "page" && ctx.framesAsRuns)) {
            RichTextRun run;
            run.text = RichTextRun::kObjectReplacement;
            run.mediaIndex = mediaIndex;
            run.imageWidthPt = widthPt;
            run.imageHeightPt = heightPt;
            run.imageAltText = altText;
            if (anchorType != "as-char") ApplyFloatPlacement(frame, run);
            run.lineBreakBefore = ctx.pendingLineBreak;
            ctx.pendingLineBreak = false;
            ctx.runs.push_back(std::move(run));
            return true;
        }

        RichDocBlock block;
        block.type = RichBlockType::Image;
        block.mediaIndex = mediaIndex;
        block.imageWidthPt = widthPt;
        block.imageHeightPt = heightPt;
        block.imageAltText = altText;
        ctx.trailingImages.push_back(std::move(block));
        return true;
    }

    // A draw:frame carries one of: an embedded formula object, an image, or
    // a draw:text-box (positioned letterhead blocks — sender address, contact
    // details, ...). Text-box content keeps its block structure and is
    // emitted after the anchoring paragraph.
    void ParseFrame(tinyxml2::XMLElement* frame, const OdtTextProps& props,
                    const std::string& linkTarget, InlineContext& ctx) {
        if (ParseFormulaObject(frame, props, linkTarget, ctx)) return;
        if (ParseImageFrame(frame, ctx)) return;
        if (auto* textBox = frame->FirstChildElement("draw:text-box")) {
            if (ctx.framesAsRuns && frameDepth_ == 0) ParseTextFrame(frame, textBox, ctx);
            else ctx.textBoxes.push_back(textBox);
        }
    }

    // How a floating picture or text frame wraps and where it sits: its
    // graphic style's wrap, its horizontal and vertical position and what
    // they count from (the page, its text area, or the paragraph), and the
    // frame's svg:x / svg:y.
    void ApplyFloatPlacement(tinyxml2::XMLElement* frame, RichTextRun& run) const {
        using Origin = RichTextRun::FloatOrigin;
        const std::string style = Attr(frame, "draw:style-name");
        const std::string wrap = GraphicAttribute(style, &GraphicPlacement::wrap);
        if (wrap == "none") {
            run.imageWrap = RichTextRun::ImageWrap::TopAndBottom;
        } else if (wrap == "run-through") {
            run.imageWrap = GraphicAttribute(style, &GraphicPlacement::runThrough) == "background"
                ? RichTextRun::ImageWrap::BehindText : RichTextRun::ImageWrap::InFrontOfText;
        } else {
            run.imageWrap = RichTextRun::ImageWrap::Square;   // parallel, left, right, dynamic, biggest
        }
        const bool pageAnchored = std::string(Attr(frame, "text:anchor-type")) == "page";
        auto origin = [&](const std::string& relation) {
            if (relation == "page" || relation == "page-start-margin" || relation == "page-end-margin") return Origin::Page;
            if (relation == "page-content") return Origin::Margin;
            if (relation.empty() && pageAnchored) return Origin::Page;
            return Origin::Paragraph;
        };
        const Origin across = origin(GraphicAttribute(style, &GraphicPlacement::horizontalRel));
        run.floatHorizontalFrom = across == Origin::Page ? Origin::Page : Origin::Margin;
        run.floatVerticalFrom = origin(GraphicAttribute(style, &GraphicPlacement::verticalRel));

        const char* x = frame->Attribute("svg:x");
        const std::string position = GraphicAttribute(style, &GraphicPlacement::horizontalPos);
        if (position == "right" || position == "outside") {
            run.imageFloatAlign = RichTextAlign::Right;
        } else if (position == "center") {
            run.imageFloatAlign = RichTextAlign::Center;
        } else if (position == "from-left" || position == "from-inside" || (position.empty() && x)) {
            run.imageFloatAlign = RichTextAlign::Default;
            const float offset = ParseLengthPt(x ? x : "");
            run.imageOffsetXPt = run.floatHorizontalFrom == Origin::Page ? offset : std::max(0.0f, offset);
        } else {
            run.imageFloatAlign = RichTextAlign::Left;
        }
        const std::string down = GraphicAttribute(style, &GraphicPlacement::verticalPos);
        using VAlign = RichTextRun::FloatVerticalAlign;
        run.floatVerticalAlign = down == "middle" ? VAlign::Middle : down == "bottom" ? VAlign::Bottom : VAlign::Top;
        if (down.empty() || down == "from-top") {
            const float offset = ParseLengthPt(Attr(frame, "svg:y"));
            run.imageOffsetYPt = run.floatVerticalFrom == Origin::Paragraph ? std::max(0.0f, offset) : offset;
        }
    }

    // ===== DRAWN SHAPES =====
    static bool IsShapeTag(const std::string& tag) {
        return tag == "draw:rect" || tag == "draw:ellipse" || tag == "draw:circle" || tag == "draw:line"
            || tag == "draw:path" || tag == "draw:polygon" || tag == "draw:polyline" || tag == "draw:custom-shape"
            || tag == "draw:g" || tag == "draw:connector";
    }

    // An SVG path (svg:d, or an enhanced path's plain numbers) into a
    // shape's outline, scaled by its view box. False when it holds formulas.
    static bool ReadSvgPath(const std::string& text, double left, double top, double width, double height,
                            RichShape& shape) {
        using Op = RichShape::PathPoint::Op;
        if (text.find_first_of("?$") != std::string::npos) return false;
        if (width <= 0.0) width = 1.0;
        if (height <= 0.0) height = 1.0;
        shape.kind = RichShape::Kind::Path;
        shape.path.clear();
        double curX = 0, curY = 0, startX = 0, startY = 0;
        char command = 0;
        std::vector<double> numbers;
        auto nx = [&](double v) { return static_cast<float>((v - left) / width); };
        auto ny = [&](double v) { return static_cast<float>((v - top) / height); };
        auto emit = [&](Op op, double x, double y) {
            RichShape::PathPoint p;
            p.op = op;
            p.x = nx(x);
            p.y = ny(y);
            shape.path.push_back(p);
            curX = x;
            curY = y;
        };
        auto flush = [&]() {
            const bool relative = std::islower(static_cast<unsigned char>(command)) != 0;
            const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(command)));
            size_t i = 0;
            auto take = [&](size_t count) { return i + count <= numbers.size(); };
            while (true) {
                const double bx = relative ? curX : 0.0, by = relative ? curY : 0.0;
                if (c == 'M' && take(2)) {
                    emit(i == 0 ? Op::Move : Op::Line, bx + numbers[i], by + numbers[i + 1]);
                    if (i == 0) { startX = curX; startY = curY; }
                    i += 2;
                } else if (c == 'L' && take(2)) {
                    emit(Op::Line, bx + numbers[i], by + numbers[i + 1]);
                    i += 2;
                } else if (c == 'H' && take(1)) {
                    emit(Op::Line, bx + numbers[i], curY);
                    i += 1;
                } else if (c == 'V' && take(1)) {
                    emit(Op::Line, curX, by + numbers[i]);
                    i += 1;
                } else if (c == 'C' && take(6)) {
                    RichShape::PathPoint p;
                    p.op = Op::Curve;
                    p.c1x = nx(bx + numbers[i]); p.c1y = ny(by + numbers[i + 1]);
                    p.c2x = nx(bx + numbers[i + 2]); p.c2y = ny(by + numbers[i + 3]);
                    curX = bx + numbers[i + 4];
                    curY = by + numbers[i + 5];
                    p.x = nx(curX); p.y = ny(curY);
                    shape.path.push_back(p);
                    i += 6;
                } else if (c == 'Q' && take(4)) {
                    const double qx = bx + numbers[i], qy = by + numbers[i + 1];
                    const double ex = bx + numbers[i + 2], ey = by + numbers[i + 3];
                    RichShape::PathPoint p;
                    p.op = Op::Curve;
                    p.c1x = nx(curX + (qx - curX) * 2.0 / 3.0); p.c1y = ny(curY + (qy - curY) * 2.0 / 3.0);
                    p.c2x = nx(ex + (qx - ex) * 2.0 / 3.0); p.c2y = ny(ey + (qy - ey) * 2.0 / 3.0);
                    p.x = nx(ex); p.y = ny(ey);
                    shape.path.push_back(p);
                    curX = ex;
                    curY = ey;
                    i += 4;
                } else {
                    break;
                }
            }
            numbers.clear();
        };
        for (size_t i = 0; i < text.size();) {
            const char c = text[i];
            if (std::isalpha(static_cast<unsigned char>(c))) {
                flush();
                command = c;
                const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                if (upper == 'Z') {
                    RichShape::PathPoint p;
                    p.op = Op::Close;
                    shape.path.push_back(p);
                    curX = startX;
                    curY = startY;
                }
                // Enhanced-path N (end of a sub-path), F / S (no fill / no
                // stroke) need nothing more.
                ++i;
            } else if (c == '-' || c == '+' || c == '.' || std::isdigit(static_cast<unsigned char>(c))) {
                double value = 0.0;
                const size_t next = WordFormatInternal::ReadNumberAt(text, i, value);
                if (next == i) { ++i; continue; }
                numbers.push_back(value);
                i = next;
            } else {
                ++i;
            }
        }
        flush();
        return !shape.path.empty();
    }

    // draw:custom-shape's geometry: a named type (Writer's own, Word's
    // "ooxml-..." and "mso-spt..."), else its plain enhanced path.
    static void ReadEnhancedGeometry(tinyxml2::XMLElement* geometry, RichShape& shape) {
        if (!geometry) return;
        const std::string type = Attr(geometry, "draw:type");
        static const std::map<std::string, std::string> kNames = {
            {"rectangle", "rect"}, {"round-rectangle", "roundRect"}, {"ellipse", "ellipse"}, {"circle", "ellipse"},
            {"diamond", "diamond"}, {"isosceles-triangle", "triangle"}, {"right-triangle", "rtTriangle"},
            {"parallelogram", "parallelogram"}, {"trapezoid", "trapezoid"}, {"hexagon", "hexagon"},
            {"octagon", "octagon"}, {"pentagon", "pentagon"}, {"star5", "star5"}, {"star4", "star4"},
            {"right-arrow", "rightArrow"}, {"left-arrow", "leftArrow"}, {"up-arrow", "upArrow"},
            {"down-arrow", "downArrow"}, {"cross", "plus"}, {"chevron", "chevron"}, {"pentagon-right", "homePlate"},
            {"flowchart-process", "rect"}, {"flowchart-decision", "diamond"}, {"flowchart-terminator", "flowChartTerminator"},
            {"flowchart-alternate-process", "roundRect"}, {"flowchart-data", "parallelogram"},
            {"flowchart-connector", "ellipse"}, {"flowchart-preparation", "hexagon"}, {"flowchart-extract", "triangle"},
            {"flowchart-merge", "flowChartMerge"}, {"line", "line"}};
        bool known = false;
        auto found = kNames.find(type);
        if (found != kNames.end()) known = shape.ApplyPreset(found->second);
        else if (type.rfind("ooxml-", 0) == 0) known = shape.ApplyPreset(type.substr(6));
        else if (type.rfind("mso-spt", 0) == 0) known = WordFormatInternal::ApplyOfficeArtShapeType(std::atoi(type.c_str() + 7), shape);
        if (!known) {
            double box[4] = {0, 0, 21600, 21600};
            WordFormatInternal::ReadNumbers(Attr(geometry, "svg:viewBox"), box, 4);
            if (!ReadSvgPath(Attr(geometry, "draw:enhanced-path"), box[0], box[1], box[2], box[3], shape)) {
                shape.ApplyPreset("rect");
            }
        }
        const std::string mirrorH = Attr(geometry, "draw:mirror-horizontal");
        const std::string mirrorV = Attr(geometry, "draw:mirror-vertical");
        shape.flipH = shape.flipH != (mirrorH == "true");
        shape.flipV = shape.flipV != (mirrorV == "true");
    }

    // A shape's box: svg:x/y/width/height, or a draw:transform that turns
    // it (anticlockwise, about its corner) and moves it.
    static void ReadShapeBox(tinyxml2::XMLElement* element, RichShape& shape) {
        shape.widthPt = ParseLengthPt(Attr(element, "svg:width"));
        shape.heightPt = ParseLengthPt(Attr(element, "svg:height"));
        shape.xPt = ParseLengthPt(Attr(element, "svg:x"));
        shape.yPt = ParseLengthPt(Attr(element, "svg:y"));
        const std::string transform = Attr(element, "draw:transform");
        if (transform.empty()) return;
        double angle = 0.0, tx = 0.0, ty = 0.0;
        bool moved = false;
        for (size_t at = 0; at < transform.size();) {
            const size_t open = transform.find('(', at);
            if (open == std::string::npos) break;
            const size_t close = transform.find(')', open);
            if (close == std::string::npos) break;
            std::string name = transform.substr(at, open - at);
            name.erase(std::remove(name.begin(), name.end(), ' '), name.end());
            const std::string args = transform.substr(open + 1, close - open - 1);
            if (name == "rotate") {
                double turn = 0.0;
                WordFormatInternal::ReadNumbers(args, &turn, 1);
                angle += turn;
            } else if (name == "translate") {
                std::istringstream values(args);
                std::string first, second;
                values >> first >> second;
                tx += ParseLengthPt(first);
                ty += ParseLengthPt(second);
                moved = true;
            }
            at = close + 1;
        }
        if (!moved) return;
        // Where its centre ends up gives its upright box.
        const double hw = shape.widthPt * 0.5, hh = shape.heightPt * 0.5;
        const double cx = tx + hw * std::cos(angle) + hh * std::sin(angle);
        const double cy = ty - hw * std::sin(angle) + hh * std::cos(angle);
        shape.xPt = static_cast<float>(cx - hw);
        shape.yPt = static_cast<float>(cy - hh);
        constexpr double kDegrees = 180.0 / 3.14159265358979323846;
        shape.rotationDeg = static_cast<float>(-angle * kDegrees);
    }

    // A shape's fill, line and words' place from its graphic style.
    void ReadShapeStyle(tinyxml2::XMLElement* element, RichShape& shape, float* padding,
                        RichShape::TextAnchor* anchor) {
        const std::string style = Attr(element, "draw:style-name");
        auto look = [&](std::string GraphicPlacement::*field) { return ShapeAttribute(style, field); };
        const std::string fill = look(&GraphicPlacement::fill);
        if (fill != "none") {
            std::string colour = look(&GraphicPlacement::fillColor);
            if (colour.empty() && (fill == "solid" || fill.empty())) colour = "#729FCF";   // Writer's own default
            if (fill == "solid" || fill.empty() || fill == "gradient" || fill == "hatch") shape.fillColor = colour;
            // A bitmap fill: its picture, repeated (Writer's default) or stretched.
            auto image = fill == "bitmap" ? fillImages_.find(look(&GraphicPlacement::fillImageName)) : fillImages_.end();
            if (image != fillImages_.end()) {
                shape.fillMediaIndex = FillImageMedia(image->second);
                shape.fillPictureTiled = look(&GraphicPlacement::repeat) != "stretch";
            }
            auto hatch = fill == "hatch" ? hatches_.find(look(&GraphicPlacement::fillHatchName)) : hatches_.end();
            if (hatch != hatches_.end()) {
                // Lines in the hatch's colour, over the fill colour when the
                // hatch is solid.
                shape.fillPattern = hatch->second.pattern;
                shape.patternBackColor = look(&GraphicPlacement::fillHatchSolid) == "true" ? colour : "";
                shape.fillColor = hatch->second.color;
            }
            auto gradient = fill == "gradient" ? gradients_.find(look(&GraphicPlacement::fillGradientName)) : gradients_.end();
            if (gradient != gradients_.end()) {
                shape.gradientStops = gradient->second.stops;
                shape.gradientAngleDeg = gradient->second.angleDeg;
                shape.gradientRadial = gradient->second.radial;
                shape.gradientCenterX = gradient->second.centerX;
                shape.gradientCenterY = gradient->second.centerY;
                shape.fillColor = shape.gradientStops.front().color;
            }
        }
        const std::string stroke = look(&GraphicPlacement::stroke);
        if (stroke != "none") {
            std::string colour = look(&GraphicPlacement::strokeColor);
            shape.lineColor = colour.empty() ? "#3465A4" : colour;
            const std::string width = look(&GraphicPlacement::strokeWidth);
            const float points = ParseLengthPt(width);
            shape.lineWidthPt = width.empty() ? 0.75f : std::max(0.0f, points);
            if (stroke == "dash") {
                std::string dash = ToLower(look(&GraphicPlacement::strokeDash));
                shape.dash = dash.find("dot") != std::string::npos && dash.find("dash") == std::string::npos
                           ? RichShape::Dash::Dotted : RichShape::Dash::Dashed;
            }
            shape.arrowStart = !look(&GraphicPlacement::markerStart).empty();
            shape.arrowEnd = !look(&GraphicPlacement::markerEnd).empty();
        }
        if (anchor) {
            const std::string vertical = look(&GraphicPlacement::textAreaVertical);
            *anchor = vertical == "middle" ? RichShape::TextAnchor::Middle
                    : vertical == "bottom" ? RichShape::TextAnchor::Bottom : RichShape::TextAnchor::Top;
        }
        if (padding) {
            std::string inset = look(&GraphicPlacement::paddingLeft);
            if (inset.empty()) inset = look(&GraphicPlacement::padding);
            if (!inset.empty()) *padding = std::max(0.0f, ParseLengthPt(inset));
        }
    }

    // A shape's paragraphs (text:p children), read out of the main flow.
    std::vector<RichDocBlock> ReadShapeWords(tinyxml2::XMLElement* element) {
        bool any = false;
        for (auto* child = element->FirstChildElement(); child && !any; child = child->NextSiblingElement()) {
            const std::string tag = child->Name() ? child->Name() : "";
            any = tag == "text:p" || tag == "text:h" || tag == "text:list";
        }
        if (!any) return {};
        const size_t start = doc_->blocks.size();
        const bool savedFlow = inMainFlow_;
        inMainFlow_ = false;
        ++frameDepth_;
        ParseBlockContainer(element, 0, "");
        --frameDepth_;
        inMainFlow_ = savedFlow;
        std::vector<RichDocBlock> words(std::make_move_iterator(doc_->blocks.begin() + static_cast<std::ptrdiff_t>(start)),
                                        std::make_move_iterator(doc_->blocks.end()));
        doc_->blocks.resize(start);
        while (!words.empty() && words.back().type == RichBlockType::Paragraph && words.back().runs.empty()) words.pop_back();
        return words;
    }

    // One shape or group, its box in the coordinates it was drawn in (the
    // anchor's). A lone shape's words go to `ownWords`, a member's into a
    // frame of their own.
    bool ReadShapeElement(tinyxml2::XMLElement* element, RichShape& shape, std::vector<RichDocBlock>* ownWords,
                          float* ownPadding, RichShape::TextAnchor* ownAnchor, int depth) {
        if (depth > 16) return false;
        const std::string tag = element->Name() ? element->Name() : "";
        shape.name = Attr(element, "draw:name");
        if (tag == "draw:g") {
            shape.kind = RichShape::Kind::Group;
            float minX = std::numeric_limits<float>::max(), minY = minX, maxX = -minX, maxY = -minX;
            for (auto* child = element->FirstChildElement(); child; child = child->NextSiblingElement()) {
                const std::string childTag = child->Name() ? child->Name() : "";
                RichShape member;
                bool ok = false;
                if (IsShapeTag(childTag)) {
                    ok = ReadShapeElement(child, member, nullptr, nullptr, nullptr, depth + 1);
                } else if (childTag == "draw:frame") {
                    auto* image = child->FirstChildElement("draw:image");
                    member.mediaIndex = image ? LoadPicture(Attr(image, "xlink:href")) : -1;
                    if (member.mediaIndex >= 0) {
                        ReadShapeBox(child, member);
                        member.name = Attr(child, "draw:name");
                        ok = true;
                    }
                }
                if (!ok) continue;
                minX = std::min(minX, member.xPt);
                minY = std::min(minY, member.yPt);
                maxX = std::max(maxX, member.xPt + member.widthPt);
                maxY = std::max(maxY, member.yPt + member.heightPt);
                shape.children.push_back(std::move(member));
            }
            if (shape.children.empty()) return false;
            // The group's box is its members' bounds; theirs count from it.
            shape.xPt = minX;
            shape.yPt = minY;
            shape.widthPt = maxX - minX;
            shape.heightPt = maxY - minY;
            for (RichShape& member : shape.children) {
                member.xPt -= minX;
                member.yPt -= minY;
            }
            return true;
        }
        if (tag == "draw:line" || tag == "draw:connector") {
            const float x1 = ParseLengthPt(Attr(element, "svg:x1")), y1 = ParseLengthPt(Attr(element, "svg:y1"));
            const float x2 = ParseLengthPt(Attr(element, "svg:x2")), y2 = ParseLengthPt(Attr(element, "svg:y2"));
            shape.kind = RichShape::Kind::Line;
            shape.xPt = std::min(x1, x2);
            shape.yPt = std::min(y1, y2);
            shape.widthPt = std::abs(x2 - x1);
            shape.heightPt = std::abs(y2 - y1);
            shape.flipH = x2 < x1;
            shape.flipV = y2 < y1;
            if (shape.flipH && shape.flipV) shape.flipH = shape.flipV = false;
            ReadShapeStyle(element, shape, nullptr, nullptr);
            shape.fillColor.clear();
            return true;
        }
        ReadShapeBox(element, shape);
        if (tag == "draw:rect") {
            const float radius = ParseLengthPt(Attr(element, "draw:corner-radius"));
            const float shorter = std::min(shape.widthPt, shape.heightPt);
            if (radius > 0.0f && shorter > 0.0f) {
                shape.kind = RichShape::Kind::RoundRectangle;
                shape.cornerRadius = std::clamp(radius / shorter, 0.0f, 0.5f);
            }
        } else if (tag == "draw:ellipse" || tag == "draw:circle") {
            shape.kind = RichShape::Kind::Ellipse;
        } else if (tag == "draw:path") {
            double box[4] = {0, 0, 0, 0};
            WordFormatInternal::ReadNumbers(Attr(element, "svg:viewBox"), box, 4);
            if (!ReadSvgPath(Attr(element, "svg:d"), box[0], box[1], box[2], box[3], shape)) return false;
        } else if (tag == "draw:polygon" || tag == "draw:polyline") {
            double box[4] = {0, 0, 0, 0};
            WordFormatInternal::ReadNumbers(Attr(element, "svg:viewBox"), box, 4);
            const double left = box[0], top = box[1], width = box[2], height = box[3];
            std::string points = Attr(element, "draw:points");
            std::replace(points.begin(), points.end(), ',', ' ');
            std::string path = "M " + points + (tag == "draw:polygon" ? " Z" : "");
            if (!ReadSvgPath(path, left, top, width, height, shape)) return false;
        } else if (tag == "draw:custom-shape") {
            ReadEnhancedGeometry(element->FirstChildElement("draw:enhanced-geometry"), shape);
        } else {
            return false;
        }
        float padding = 3.6f;
        RichShape::TextAnchor anchor = RichShape::TextAnchor::Top;
        ReadShapeStyle(element, shape, &padding, &anchor);
        if (!shape.IsClosed()) shape.fillColor.clear();
        std::vector<RichDocBlock> words = ReadShapeWords(element);
        if (ownWords) {
            *ownWords = std::move(words);
            if (ownPadding) *ownPadding = padding;
            if (ownAnchor) *ownAnchor = anchor;
        } else if (!words.empty()) {
            RichTextFrame text;
            text.blocks = std::move(words);
            text.shapeText = true;
            shape.textFrame = static_cast<int>(doc_->frames.size());
            shape.textPaddingPt = padding;
            shape.textAnchor = anchor;
            doc_->frames.push_back(std::move(text));
        }
        return true;
    }

    // A shape or group anchored in the text: a drawing frame, floating where
    // its graphic style and its box say.
    void ParseDrawingShape(tinyxml2::XMLElement* element, InlineContext& ctx) {
        RichTextFrame frame;
        RichShape shape;
        if (!ReadShapeElement(element, shape, &frame.blocks, &frame.paddingPt, &frame.textAnchor, 0)) return;
        frame.name = shape.name;
        RichTextRun run;
        run.text = RichTextRun::kObjectReplacement;
        // The frame is the shape's upright box (a turned shape turns in it),
        // as Word places one.
        const float boxX = shape.xPt, boxY = shape.yPt, boxW = shape.widthPt, boxH = shape.heightPt;
        run.imageWidthPt = boxW;
        run.imageHeightPt = boxH;
        shape.xPt -= boxX;
        shape.yPt -= boxY;
        frame.shapes.push_back(std::move(shape));
        if (std::string(Attr(element, "text:anchor-type")) == "as-char") {
            run.imageWrap = RichTextRun::ImageWrap::TopAndBottom;
        } else {
            ApplyFloatPlacement(element, run);
            // The box, not the element's svg:x / svg:y (a line, a group and
            // a turned shape have none), gives the offsets.
            using Origin = RichTextRun::FloatOrigin;
            const std::string position = GraphicAttribute(Attr(element, "draw:style-name"), &GraphicPlacement::horizontalPos);
            if (run.imageFloatAlign == RichTextAlign::Default || position.empty()) {
                run.imageFloatAlign = RichTextAlign::Default;
                run.imageOffsetXPt = run.floatHorizontalFrom == Origin::Page ? boxX : std::max(0.0f, boxX);
            }
            if (run.floatVerticalAlign == RichTextRun::FloatVerticalAlign::Top) {
                run.imageOffsetYPt = run.floatVerticalFrom == Origin::Paragraph ? std::max(0.0f, boxY) : boxY;
            }
        }
        run.imageAltText = frame.name;
        run.frameIndex = static_cast<int>(doc_->frames.size());
        doc_->frames.push_back(std::move(frame));
        run.lineBreakBefore = ctx.pendingLineBreak;
        ctx.pendingLineBreak = false;
        ctx.runs.push_back(std::move(run));
    }

    // A draw:text-box: its paragraphs become a text frame (UCRichDocument::
    // frames), floating where the frame says, with its fill, border and
    // padding. Read like the body, but out of the main flow; a text box
    // inside it is flattened into its text.
    void ParseTextFrame(tinyxml2::XMLElement* frame, tinyxml2::XMLElement* textBox, InlineContext& ctx) {
        RichTextFrame content;
        content.name = Attr(frame, "draw:name");
        const size_t start = doc_->blocks.size();
        const bool savedFlow = inMainFlow_;
        inMainFlow_ = false;
        ++frameDepth_;
        ParseBlockContainer(textBox, 0, "");
        --frameDepth_;
        inMainFlow_ = savedFlow;
        content.blocks.assign(std::make_move_iterator(doc_->blocks.begin() + static_cast<std::ptrdiff_t>(start)),
                              std::make_move_iterator(doc_->blocks.end()));
        doc_->blocks.resize(start);

        const std::string style = Attr(frame, "draw:style-name");
        auto look = [&](std::string GraphicPlacement::*field) { return GraphicAttribute(style, field); };
        if (look(&GraphicPlacement::fill) == "solid") {
            content.backgroundColor = look(&GraphicPlacement::fillColor);
        } else if (look(&GraphicPlacement::fill).empty()) {
            const std::string background = look(&GraphicPlacement::backgroundColor);
            if (!background.empty() && background != "transparent") content.backgroundColor = background;
        }
        const std::string border = look(&GraphicPlacement::border);
        if (!border.empty()) {
            content.border = ParseBorder(border);
        } else if (look(&GraphicPlacement::stroke) == "solid") {
            content.border.widthPt = std::max(0.5f, ParseLengthPt(look(&GraphicPlacement::strokeWidth)));
            content.border.color = look(&GraphicPlacement::strokeColor);
            if (content.border.color.empty()) content.border.color = "#000000";
        }
        content.paddingPt = std::max(0.0f, ParseLengthPt(look(&GraphicPlacement::padding)));
        const std::string vertical = look(&GraphicPlacement::textAreaVertical);
        content.textAnchor = vertical == "middle" ? RichShape::TextAnchor::Middle
                           : vertical == "bottom" ? RichShape::TextAnchor::Bottom : RichShape::TextAnchor::Top;

        RichTextRun run;
        run.text = RichTextRun::kObjectReplacement;
        run.frameIndex = static_cast<int>(doc_->frames.size());
        run.imageAltText = content.name;
        run.imageWidthPt = ParseLengthPt(Attr(frame, "svg:width"));
        if (run.imageWidthPt <= 0.0f) run.imageWidthPt = ParseLengthPt(Attr(frame, "fo:min-width"));
        // Its least height: the box's own, else the frame's.
        float height = ParseLengthPt(Attr(textBox, "fo:min-height"));
        if (height <= 0.0f) height = ParseLengthPt(Attr(frame, "svg:height"));
        if (height <= 0.0f) height = ParseLengthPt(look(&GraphicPlacement::minHeight));
        run.imageHeightPt = std::max(0.0f, height);
        doc_->frames.push_back(std::move(content));
        if (std::string(Attr(frame, "text:anchor-type")) == "as-char") {
            run.imageWrap = RichTextRun::ImageWrap::TopAndBottom;   // in the line: alone on its own lines
        } else {
            ApplyFloatPlacement(frame, run);
        }
        run.lineBreakBefore = ctx.pendingLineBreak;
        ctx.pendingLineBreak = false;
        ctx.runs.push_back(std::move(run));
    }

    void ParseInlineNodes(tinyxml2::XMLNode* parent, const OdtTextProps& props,
                          const std::string& linkTarget, InlineContext& ctx) {
        for (auto* node = parent->FirstChild(); node; node = node->NextSibling()) {
            if (auto* textNode = node->ToText()) {
                AppendXmlText(ctx, textNode->Value() ? textNode->Value() : "", props, linkTarget);
                continue;
            }
            auto* elem = node->ToElement();
            if (!elem) continue;
            std::string tag = elem->Name() ? elem->Name() : "";
            if (tag == "text:span") {
                const std::string spanStyle = Attr(elem, "text:style-name");
                OdtTextProps spanProps = ResolveStyle(spanStyle);
                spanProps.characterStyle = NamedStyleFor(spanStyle);
                spanProps.MergeParent(props);
                ParseInlineNodes(elem, spanProps, linkTarget, ctx);
            } else if (tag == "text:a") {
                std::string href = Attr(elem, "xlink:href");
                ParseInlineNodes(elem, props, href.empty() ? linkTarget : href, ctx);
            } else if (tag == "text:s") {
                int count = elem->IntAttribute("text:c", 1);
                AppendRun(ctx, std::string(static_cast<size_t>(std::max(1, count)), ' '),
                          props, linkTarget);
            } else if (tag == "text:tab") {
                AppendRun(ctx, "\t", props, linkTarget);
            } else if (tag == "text:page-number" || tag == "text:page-count") {
                // Page fields: a paged view fills in each page's number; the
                // stored text is what text output and a continuous view show
                // (the count's last value, page 1 - never an empty gap).
                const bool count = tag == "text:page-count";
                std::string shown = elem->GetText() ? elem->GetText() : "";
                if (shown.empty() || !count) shown = "1";
                AppendRun(ctx, shown, props, linkTarget);
                if (!ctx.runs.empty()) {
                    ctx.runs.back().field = count ? RichTextRun::Field::PageCount : RichTextRun::Field::PageNumber;
                }
            } else if (tag == "text:line-break") {
                ctx.pendingLineBreak = true;
            } else if (tag == "text:bookmark" || tag == "text:bookmark-start") {
                const std::string name = Attr(elem, "text:name");
                if (!name.empty()) ctx.bookmarks.push_back(name);
            } else if (tag == "text:bookmark-end") {
                // The bookmark is the paragraph's; where it ends is not kept.
            } else if (tag == "text:sequence" || tag == "text:bookmark-ref") {
                // A caption number, or a cross-reference to a bookmark's text
                // or page.
                std::string shown = ElementText(elem);
                if (shown.empty()) shown = "1";
                AppendRun(ctx, shown, props, linkTarget);
                if (!ctx.runs.empty()) {
                    RichTextRun& run = ctx.runs.back();
                    if (tag == "text:sequence") {
                        run.field = RichTextRun::Field::Sequence;
                        run.fieldArgument = Attr(elem, "text:name");
                    } else {
                        run.field = std::string(Attr(elem, "text:reference-format")) == "page"
                                  ? RichTextRun::Field::PageReference : RichTextRun::Field::Reference;
                        run.fieldArgument = Attr(elem, "text:ref-name");
                    }
                }
            } else if (tag == "draw:frame") {
                ParseFrame(elem, props, linkTarget, ctx);
            } else if (IsShapeTag(tag)) {
                if (ctx.framesAsRuns && frameDepth_ == 0) ParseDrawingShape(elem, ctx);
            } else if (tag == "text:note") {
                // A footnote or endnote: its body's paragraphs become the
                // note, the citation a reference run numbered once all are
                // read.
                RichNote note;
                note.kind = std::string(Attr(elem, "text:note-class")) == "endnote"
                          ? RichNote::Kind::Endnote : RichNote::Kind::Footnote;
                if (auto* noteBody = elem->FirstChildElement("text:note-body")) {
                    const size_t start = doc_->blocks.size();
                    const bool savedFlow = inMainFlow_;
                    inMainFlow_ = false;
                    ParseBlockContainer(noteBody, 0, "");
                    inMainFlow_ = savedFlow;
                    note.blocks.assign(std::make_move_iterator(doc_->blocks.begin() + static_cast<std::ptrdiff_t>(start)),
                                       std::make_move_iterator(doc_->blocks.end()));
                    doc_->blocks.resize(start);
                }
                if (note.blocks.empty()) note.blocks.emplace_back();
                doc_->notes.push_back(std::move(note));
                RichTextRun reference;
                ApplyPropsToRun(reference, props);
                reference.noteIndex = static_cast<int>(doc_->notes.size()) - 1;
                reference.superscript = true;
                reference.text = "*";
                reference.lineBreakBefore = ctx.pendingLineBreak;
                ctx.pendingLineBreak = false;
                ctx.endsInCollapsibleSpace = false;
                ctx.runs.push_back(std::move(reference));
            } else if (tag == "text:change-start" || tag == "text:change-end" || tag == "text:change") {
                auto region = changedRegions_.find(Attr(elem, "text:change-id"));
                if (region != changedRegions_.end()) {
                    if (tag == "text:change-start" && region->second.insertion) activeInsertion_ = region->second.revision;
                    else if (tag == "text:change-end" && region->second.insertion) activeInsertion_ = -1;
                    else if (tag == "text:change" && region->second.deletion && !region->second.deletedText.empty()) {
                        // The deleted text, back where it was, marked deleted.
                        const int saved = activeInsertion_;
                        activeInsertion_ = -1;
                        AppendRun(ctx, region->second.deletedText, props, linkTarget);
                        activeInsertion_ = saved;
                        if (!ctx.runs.empty()) {
                            ctx.runs.back().change = RichTextRun::Change::Deleted;
                            ctx.runs.back().revision = region->second.revision;
                        }
                    }
                }
            } else if (tag == "office:annotation") {
                ReadAnnotation(elem);
            } else if (tag == "office:annotation-end") {
                const std::string name = Attr(elem, "office:name");
                activeAnnotations_.erase(std::remove_if(activeAnnotations_.begin(), activeAnnotations_.end(),
                                                        [&](const auto& a) { return a.first == name; }),
                                         activeAnnotations_.end());
            } else if (tag == "text:soft-page-break"
                       || tag == "text:tracked-changes" || tag == "text:sequence-decls") {
                // Non-content markup.
            } else {
                // Unknown inline element (bookmark, field, ...): recurse so any
                // nested text still comes through.
                ParseInlineNodes(elem, props, linkTarget, ctx);
            }
        }
    }

    // Emits a paragraph-family block plus any images found within it.
    // An element's text, nested spans included.
    static std::string ElementText(tinyxml2::XMLElement* elem) {
        std::string text;
        for (auto* node = elem->FirstChild(); node; node = node->NextSibling()) {
            if (auto* textNode = node->ToText()) text += textNode->Value() ? textNode->Value() : "";
            else if (auto* child = node->ToElement()) text += ElementText(child);
        }
        return text;
    }

    void EmitParagraphBlock(tinyxml2::XMLElement* elem, RichDocBlock block) {
        std::string styleName = Attr(elem, "text:style-name");
        OdtTextProps paraProps = ResolveStyle(styleName);
        block.styleId = NamedStyleFor(styleName);
        if (block.styleId == "Normal") block.styleId.clear();
        block.align = paraProps.align;
        block.rightToLeft = paraProps.rightToLeft == 1;
        ApplyGeometry(block, paraProps);

        // Reverse-map well-known paragraph shapes: heading styles used on
        // plain text:p (as LibreOffice's HTML import does), quotation styles
        // and monospace (preformatted) paragraphs.
        if (block.type == RichBlockType::Paragraph) {
            if (paraProps.headingLevel > 0) {
                block.type = RichBlockType::Heading;
                block.headingLevel = paraProps.headingLevel;
            } else if (WordFormatInternal::ToLower(styleName).find("quote") != std::string::npos) {
                block.type = RichBlockType::BlockQuote;
            } else if (IsMonospaceFamily(paraProps.fontFamily)) {
                block.type = RichBlockType::CodeBlock;
            }
        }

        // Character formatting owned by the block style (heading bold, quote
        // italics, code font) must not leak into the runs, or the markdown
        // round-trip doubles it up ("# **Title**").
        OdtTextProps runBase = paraProps;
        if (block.type == RichBlockType::Heading || block.type == RichBlockType::BlockQuote
            || block.type == RichBlockType::CodeBlock) {
            runBase = OdtTextProps{};
        }
        if (block.type == RichBlockType::Paragraph || block.type == RichBlockType::ListItem) {
            block.paragraphFontSizePt = paraProps.fontSizePt;
            block.paragraphFontFamily = paraProps.fontFamily;
        }

        InlineContext ctx;
        ctx.framesAsRuns = true;
        ParseInlineNodes(elem, runBase, "", ctx);
        block.runs = std::move(ctx.runs);
        if (inMainFlow_ && frameDepth_ == 0 && !pendingPageFrames_.empty()) {
            // The page-anchored frames before it travel with this paragraph.
            block.runs.insert(block.runs.begin(), pendingPageFrames_.begin(), pendingPageFrames_.end());
            pendingPageFrames_.clear();
        }
        block.bookmarks = std::move(ctx.bookmarks);
        if (inTableOfContents_ && block.type == RichBlockType::Paragraph) MakeContentsEntry(block, styleName);

        bool pageBreak = inMainFlow_ && paraProps.pageBreakBefore;
        if (pageBreak) {
            RichDocBlock pageBreakBlock;
            pageBreakBlock.type = RichBlockType::PageBreak;
            doc_->blocks.push_back(std::move(pageBreakBlock));
        }

        // An empty bordered paragraph is the horizontal-rule idiom.
        if (block.type == RichBlockType::Paragraph && block.runs.empty()
            && paraProps.bottomBorder) {
            block.type = RichBlockType::HorizontalRule;
        }

        // A picture on a line of its own is a standalone image, not a run:
        // ODT anchors both kinds as-char, so what else the paragraph holds is
        // what tells them apart.
        RichDocBlock promotedImage;
        if (block.type == RichBlockType::Paragraph && ctx.textBoxes.empty()
            && WordFormatInternal::ParagraphIsOneInlineImage(block.runs, promotedImage)) {
            block.type = RichBlockType::Image;
            block.runs.clear();
            block.mediaIndex = promotedImage.mediaIndex;
            block.imageWidthPt = promotedImage.imageWidthPt;
            block.imageHeightPt = promotedImage.imageHeightPt;
            block.imageAltText = promotedImage.imageAltText;
        }

        bool emptyPageBreakCarrier = pageBreak && block.runs.empty()
                                     && block.type == RichBlockType::Paragraph;
        bool onlyFrames = block.runs.empty()
                          && (!ctx.trailingImages.empty() || !ctx.textBoxes.empty());
        if (!onlyFrames && !emptyPageBreakCarrier) {
            doc_->blocks.push_back(std::move(block));
        }
        for (auto& image : ctx.trailingImages) {
            doc_->blocks.push_back(std::move(image));
        }
        // Text-box content is not part of the main flow: suppress page breaks
        // from break-before styles on paragraphs positioned inside the box.
        bool savedFlow = inMainFlow_;
        inMainFlow_ = false;
        for (auto* textBox : ctx.textBoxes) {
            ParseBlockContainer(textBox, 0, "");
        }
        inMainFlow_ = savedFlow;
    }

    void ParseList(tinyxml2::XMLElement* list, int level, std::string listStyleName) {
        std::string ownStyle = Attr(list, "text:style-name");
        if (!ownStyle.empty()) listStyleName = ownStyle;
        bool ordered = false;
        int startValue = 1;
        auto styleIt = listStyles_.find(listStyleName);
        if (styleIt != listStyles_.end()) {
            auto levelIt = styleIt->second.find(level + 1);   // text:level is 1-based
            if (levelIt != styleIt->second.end()) ordered = levelIt->second;
        }
        auto startIt = listStartValues_.find(listStyleName);
        if (startIt != listStartValues_.end()) {
            auto levelIt = startIt->second.find(level + 1);
            if (levelIt != startIt->second.end()) startValue = levelIt->second;
        }

        // Which list this element belongs to. A top-level text:list starts a
        // new one unless it says it continues an earlier list; a nested one
        // is a level of the list around it.
        const std::string savedListKey = currentListKey_;
        if (level == 0 || currentListKey_.empty()) {
            std::string continues = Attr(list, "text:continue-list");
            auto byId = listKeyForXmlId_.find(continues);
            auto byStyle = lastListKeyForStyle_.find(listStyleName);
            if (!continues.empty() && byId != listKeyForXmlId_.end()) {
                currentListKey_ = byId->second;
            } else if (std::string(Attr(list, "text:continue-numbering")) == "true"
                       && byStyle != lastListKeyForStyle_.end()) {
                currentListKey_ = byStyle->second;
            } else {
                currentListKey_ = "odt-list-" + std::to_string(++listCount_);
            }
            lastListKeyForStyle_[listStyleName] = currentListKey_;
            std::string xmlId = Attr(list, "xml:id");
            if (!xmlId.empty()) listKeyForXmlId_[xmlId] = currentListKey_;
        }

        for (auto* item = list->FirstChildElement(); item;
             item = item->NextSiblingElement()) {
            std::string tag = item->Name() ? item->Name() : "";
            if (tag != "text:list-item" && tag != "text:list-header") continue;
            // A list header carries no number; an item may set its own.
            int number = 0;
            if (ordered && tag == "text:list-item") {
                if (item->Attribute("text:start-value")) {
                    numbering_.Restart(currentListKey_, level,
                                       std::max(1, item->IntAttribute("text:start-value", 1)));
                }
                number = numbering_.Next(currentListKey_, level, startValue);
            }
            bool numbered = false;   // only the item's first paragraph shows the number
            for (auto* child = item->FirstChildElement(); child;
                 child = child->NextSiblingElement()) {
                std::string childTag = child->Name() ? child->Name() : "";
                if (childTag == "text:list") {
                    ParseList(child, level + 1, listStyleName);
                } else if (childTag == "text:p" || childTag == "text:h") {
                    RichDocBlock block;
                    block.type = RichBlockType::ListItem;
                    block.orderedList = ordered;
                    block.listLevel = level;
                    ApplyListLabel(block, listStyleName, level + 1);
                    size_t index = doc_->blocks.size();
                    EmitParagraphBlock(child, std::move(block));
                    if (number > 0 && !numbered && index < doc_->blocks.size()) {
                        RichListNumbering::Apply(doc_->blocks, index, number);
                        numbered = true;
                    }
                }
            }
        }
        currentListKey_ = savedListKey;
    }

    // Flattens every block inside a table cell (paragraphs, headings, list
    // items, nested table cells) into ctx.runs, one logical line per block.
    void CollectCellRuns(tinyxml2::XMLElement* container, InlineContext& ctx) {
        for (auto* elem = container->FirstChildElement(); elem;
             elem = elem->NextSiblingElement()) {
            std::string tag = elem->Name() ? elem->Name() : "";
            if (tag == "text:p" || tag == "text:h") {
                if (!ctx.runs.empty()) ctx.pendingLineBreak = true;
                ParseInlineNodes(elem, ResolveStyle(Attr(elem, "text:style-name")), "", ctx);
            } else if (tag == "text:list" || tag == "text:list-item"
                       || tag == "text:list-header" || tag == "table:table"
                       || tag == "table:table-row" || tag == "table:table-cell"
                       || tag == "table:table-header-rows" || tag == "text:section") {
                CollectCellRuns(elem, ctx);   // descend to reach the paragraphs
            }
        }
    }

    void ApplyListLabel(RichDocBlock& block, const std::string& listStyleName, int odfLevel) const {
        auto style = listLabels_.find(listStyleName);
        if (style == listLabels_.end()) return;
        auto level = style->second.find(odfLevel);
        if (level == style->second.end()) return;
        if (block.orderedList) {
            block.numberFormat = level->second.format;
            block.numberTemplate = level->second.numberTemplate;
        } else {
            block.bulletText = level->second.bulletText;
        }
    }

    // First text:p / text:h anywhere inside `container`, depth-first.
    static tinyxml2::XMLElement* FirstParagraphIn(tinyxml2::XMLElement* container) {
        for (auto* elem = container->FirstChildElement(); elem;
             elem = elem->NextSiblingElement()) {
            std::string tag = elem->Name() ? elem->Name() : "";
            if (tag == "text:p" || tag == "text:h") return elem;
            if (auto* found = FirstParagraphIn(elem)) return found;
        }
        return nullptr;
    }

    // Appends one table:table-column's width (repeated as often as the column
    // says). False when its style carries no width.
    bool AppendColumnWidths(tinyxml2::XMLElement* column, std::vector<float>& widths) {
        int repeat = std::clamp(column->IntAttribute("table:number-columns-repeated", 1), 1, 1024);
        // A column may name the cell style its cells use when they name none.
        for (int i = 0; i < repeat; ++i) {
            columnDefaultCellStyles_.push_back(Attr(column, "table:default-cell-style-name"));
        }
        auto it = columnWidths_.find(Attr(column, "table:style-name"));
        float width = it != columnWidths_.end() ? it->second : 0.0f;
        for (int i = 0; i < repeat; ++i) widths.push_back(width);
        return width > 0;
    }

    void ParseTable(tinyxml2::XMLElement* table) {
        RichDocBlock block;
        block.type = RichBlockType::Table;

        auto parseRow = [&](tinyxml2::XMLElement* rowElem, bool header) {
            RichTableRow row;
            row.header = header;
            size_t gridColumn = 0;
            for (auto* cellElem = rowElem->FirstChildElement(); cellElem;
                 cellElem = cellElem->NextSiblingElement()) {
                std::string tag = cellElem->Name() ? cellElem->Name() : "";
                const size_t column = gridColumn;
                gridColumn += static_cast<size_t>(
                    std::clamp(cellElem->IntAttribute("table:number-columns-repeated", 1), 1, 1024));
                if (tag == "table:covered-table-cell") continue;
                if (tag != "table:table-cell") continue;
                RichTableCell cell;
                {
                    std::string cellStyle = Attr(cellElem, "table:style-name");
                    if (cellStyle.empty() && column < columnDefaultCellStyles_.size()) {
                        cellStyle = columnDefaultCellStyles_[column];
                    }
                    auto format = cellStyles_.find(cellStyle);
                    if (format != cellStyles_.end()) cell.CopyCellFormat(format->second);
                }
                cell.columnSpan = cellElem->IntAttribute("table:number-columns-spanned", 1);
                cell.rowSpan = cellElem->IntAttribute("table:number-rows-spanned", 1);
                // Alignment is a paragraph property in ODF; the cell takes its
                // first paragraph's (a right-aligned amount, a centred date).
                if (auto* firstParagraph = FirstParagraphIn(cellElem)) {
                    const OdtTextProps paragraph = ResolveStyle(Attr(firstParagraph, "text:style-name"));
                    cell.align = paragraph.align;
                    // A cell holds text, not paragraphs: a frame on its
                    // paragraph (a letterhead's sender line with a rule
                    // under it) becomes the cell's where the cell has none.
                    if (paragraph.hasFrame) {
                        RichBorder* sides[4] = {&cell.borderTop, &cell.borderBottom, &cell.borderLeft, &cell.borderRight};
                        for (int i = 0; i < 4; ++i) {
                            if (!sides[i]->IsVisible() && paragraph.frame[i].IsVisible()) *sides[i] = paragraph.frame[i];
                        }
                        if (cell.backgroundColor.empty()) cell.backgroundColor = paragraph.paragraphBackground;
                    }
                }
                InlineContext ctx;
                // A cell holds block content (paragraphs, headings, lists,
                // even nested tables). The flat cell model keeps only runs, so
                // every block's text is flattened into the cell separated by
                // line breaks rather than dropping non-paragraph blocks.
                CollectCellRuns(cellElem, ctx);
                // A cell has no room for block structure: text boxes anchored
                // in it flatten into line-broken cell text so nothing is lost.
                // Index loop: nested frames may append more text boxes.
                for (size_t tb = 0; tb < ctx.textBoxes.size(); ++tb) {
                    for (auto* p = ctx.textBoxes[tb]->FirstChildElement("text:p"); p;
                         p = p->NextSiblingElement("text:p")) {
                        if (!ctx.runs.empty()) ctx.pendingLineBreak = true;
                        ParseInlineNodes(p, ResolveStyle(Attr(p, "text:style-name")), "", ctx);
                    }
                }
                cell.runs = std::move(ctx.runs);
                row.cells.push_back(std::move(cell));
            }
            block.tableRows.push_back(std::move(row));
        };

        // The document says how its cells are framed; a cell with no border
        // style really has no lines (layout tables).
        block.tableBordersFromDocument = true;
        auto placement = tablePlacements_.find(Attr(table, "table:style-name"));
        if (placement != tablePlacements_.end()) {
            block.tableWidthPt = placement->second.widthPt;
            block.tableWidthPercent = placement->second.widthPercent;
            block.tableAlign = placement->second.align;
            block.tableIndentPt = placement->second.indentPt;
        }
        const std::vector<std::string> savedDefaults = std::move(columnDefaultCellStyles_);
        columnDefaultCellStyles_.clear();

        // Column proportions from the column styles; any column without a
        // known width drops the list (equal columns) rather than guessing.
        bool widthsKnown = true;
        for (auto* column = table->FirstChildElement(); column;
             column = column->NextSiblingElement()) {
            std::string tag = column->Name() ? column->Name() : "";
            if (tag == "table:table-columns" || tag == "table:table-header-columns") {
                for (auto* inner = column->FirstChildElement("table:table-column"); inner;
                     inner = inner->NextSiblingElement("table:table-column")) {
                    widthsKnown = AppendColumnWidths(inner, block.tableColumnWidths) && widthsKnown;
                }
            } else if (tag == "table:table-column") {
                widthsKnown = AppendColumnWidths(column, block.tableColumnWidths) && widthsKnown;
            }
        }
        if (!widthsKnown) block.tableColumnWidths.clear();

        if (auto* headerRows = table->FirstChildElement("table:table-header-rows")) {
            for (auto* rowElem = headerRows->FirstChildElement("table:table-row"); rowElem;
                 rowElem = rowElem->NextSiblingElement("table:table-row")) {
                parseRow(rowElem, true);
            }
        }
        for (auto* rowElem = table->FirstChildElement("table:table-row"); rowElem;
             rowElem = rowElem->NextSiblingElement("table:table-row")) {
            parseRow(rowElem, false);
        }
        // Skip layout-only tables whose cells carry no text (letterheads use
        // an empty table in the page header purely for positioning); emitting
        // them would litter the linear flow with stray cell separators.
        bool hasText = false;
        for (const auto& row : block.tableRows) {
            for (const auto& cell : row.cells) {
                if (UCRichDocument::ConcatenateRunText(cell.runs)
                        .find_first_not_of(" \t\r\n") != std::string::npos) {
                    hasText = true;
                    break;
                }
            }
            if (hasText) break;
        }
        columnDefaultCellStyles_ = savedDefaults;
        if (!block.tableRows.empty() && hasText) doc_->blocks.push_back(std::move(block));
    }

    void ParseBlockContainer(tinyxml2::XMLElement* container, int listLevel,
                             const std::string& listStyleName) {
        for (auto* elem = container->FirstChildElement(); elem;
             elem = elem->NextSiblingElement()) {
            std::string tag = elem->Name() ? elem->Name() : "";
            if (tag == "text:h") {
                RichDocBlock block;
                block.type = RichBlockType::Heading;
                block.headingLevel = std::clamp(elem->IntAttribute("text:outline-level", 1), 1, 6);
                EmitParagraphBlock(elem, std::move(block));
            } else if (tag == "text:p") {
                RichDocBlock block;
                block.type = RichBlockType::Paragraph;
                EmitParagraphBlock(elem, std::move(block));
            } else if (tag == "text:list") {
                ParseList(elem, listLevel, listStyleName);
            } else if (tag == "table:table") {
                ParseTable(elem);
            } else if (tag == "text:section") {
                // Sections switched off in the source document (template
                // machinery like optional payment blocks) must not render.
                if (std::string(Attr(elem, "text:display")) != "none") {
                    // A section in columns starts a section of the model's,
                    // and the text after it goes back to one column.
                    auto columns = sectionStyles_.find(Attr(elem, "text:style-name"));
                    const size_t start = doc_->blocks.size();
                    ParseBlockContainer(elem, listLevel, listStyleName);
                    if (inMainFlow_ && columns != sectionStyles_.end() && columns->second.columns > 1
                        && doc_->blocks.size() > start) {
                        sectionRanges_.push_back({start, doc_->blocks.size(), columns->second});
                    }
                }
            } else if (tag == "draw:frame" || IsShapeTag(tag)) {
                // Page-anchored frame or shape sitting directly in the text
                // flow: it waits for the next paragraph.
                InlineContext ctx;
                ctx.framesAsRuns = inMainFlow_ && frameDepth_ == 0;
                if (tag == "draw:frame") ParseFrame(elem, OdtTextProps{}, "", ctx);
                else if (ctx.framesAsRuns) ParseDrawingShape(elem, ctx);
                if (ctx.framesAsRuns && !ctx.runs.empty()) {
                    pendingPageFrames_.insert(pendingPageFrames_.end(), ctx.runs.begin(), ctx.runs.end());
                    ctx.runs.clear();
                }
                if (!ctx.runs.empty()) {
                    RichDocBlock promoted;
                    if (WordFormatInternal::ParagraphIsOneInlineImage(ctx.runs, promoted)) {
                        doc_->blocks.push_back(std::move(promoted));
                    } else {
                        RichDocBlock block;
                        block.type = RichBlockType::Paragraph;
                        block.runs = std::move(ctx.runs);
                        doc_->blocks.push_back(std::move(block));
                    }
                }
                for (auto& image : ctx.trailingImages) {
                    doc_->blocks.push_back(std::move(image));
                }
                bool savedFlow = inMainFlow_;
                inMainFlow_ = false;   // text-box content is not main flow
                for (size_t tb = 0; tb < ctx.textBoxes.size(); ++tb) {
                    ParseBlockContainer(ctx.textBoxes[tb], listLevel, listStyleName);
                }
                inMainFlow_ = savedFlow;
            } else if (tag == "style:region-left" || tag == "style:region-center"
                       || tag == "style:region-right") {
                // Header/footer column regions.
                ParseBlockContainer(elem, listLevel, listStyleName);
            } else if (tag == "draw:g") {
                // Grouped drawing shapes (e.g. a logo frame grouped with a
                // caption): recurse so the nested frames/text boxes render.
                ParseBlockContainer(elem, listLevel, listStyleName);
            } else if (tag == "text:table-of-content" || tag == "text:illustration-index"
                       || tag == "text:table-index" || tag == "text:object-index"
                       || tag == "text:user-index" || tag == "text:alphabetical-index"
                       || tag == "text:bibliography") {
                // An index is stored twice: how to build it (the *-source
                // element) and the text it was last built into (index-body).
                // The body is what the document shows.
                if (auto* indexBody = elem->FirstChildElement("text:index-body")) {
                    const bool saved = inTableOfContents_;
                    inTableOfContents_ = tag == "text:table-of-content";
                    ParseBlockContainer(indexBody, listLevel, listStyleName);
                    inTableOfContents_ = saved;
                }
            } else if (tag == "text:index-title") {
                ParseBlockContainer(elem, listLevel, listStyleName);
            } else if (tag == "text:change-start" || tag == "text:change-end") {
                // Whole inserted paragraphs.
                auto region = changedRegions_.find(Attr(elem, "text:change-id"));
                if (region != changedRegions_.end() && region->second.insertion) {
                    activeInsertion_ = tag == "text:change-start" ? region->second.revision : -1;
                }
            }
            // Everything else (sequence declarations, forms) is skipped.
        }
    }

    // True if a master-page region (or any descendant) carries visible text or
    // an image/table — used to skip empty first-page headers when choosing
    // which master page to render.
    bool NodeHasVisibleContent(tinyxml2::XMLNode* node) const {
        for (auto* child = node->FirstChild(); child; child = child->NextSibling()) {
            if (auto* textNode = child->ToText()) {
                std::string v = textNode->Value() ? textNode->Value() : "";
                for (size_t at; (at = v.find(kKeptWhitespace)) != std::string::npos;) v.erase(at, 3);
                if (v.find_first_not_of(" \t\r\n") != std::string::npos) return true;
                continue;
            }
            auto* elem = child->ToElement();
            if (!elem) continue;
            std::string tag = elem->Name() ? elem->Name() : "";
            if (tag == "draw:frame" || tag == "draw:image" || tag == "draw:g"
                || tag == "table:table") {
                return true;
            }
            if (NodeHasVisibleContent(elem)) return true;
        }
        return false;
    }

    bool MasterPageHasContent(tinyxml2::XMLElement* page) const {
        for (const char* tag : {"style:header", "style:footer"}) {
            auto* region = page->FirstChildElement(tag);
            if (!region) continue;
            if (std::string(Attr(region, "style:display")) == "false") continue;
            if (NodeHasVisibleContent(region)) return true;
        }
        return false;
    }

    // Name of the master page the document's first page draws from. The initial
    // page master is pinned by the first flow block's style via
    // style:master-page-name (ODF §16.2); otherwise the document defaults to the
    // "Standard" master. The pin lives on the paragraph *or table* style — real
    // letterheads carry it on the leading layout table (table:style-name), not a
    // paragraph — so both are consulted. A later pin is a page break onto a new
    // page and must not be mistaken for the first page's master, so only the
    // first flow block is inspected. Non-flow leading nodes (forms, field
    // declarations, page-anchored frames/shapes) carry no page style and are
    // skipped.
    std::string AppliedMasterPageName(tinyxml2::XMLElement* body) const {
        for (auto* elem = body->FirstChildElement(); elem;
             elem = elem->NextSiblingElement()) {
            std::string tag = elem->Name() ? elem->Name() : "";
            const char* styleAttr = nullptr;
            if (tag == "text:p" || tag == "text:h" || tag == "text:list") {
                styleAttr = "text:style-name";
            } else if (tag == "table:table") {
                styleAttr = "table:style-name";
            } else if (tag == "text:section") {
                // A section is flow content; resolve within it, then stop.
                return AppliedMasterPageName(elem);
            } else {
                continue;   // non-flow: forms, decls, page-anchored drawings
            }
            return ResolveStyle(Attr(elem, styleAttr)).masterPageName;
        }
        return "";
    }

    // The master page the pages after `master` use (style:next-style-name).
    tinyxml2::XMLElement* FollowingMasterPage(tinyxml2::XMLElement* stylesRoot,
                                              tinyxml2::XMLElement* master) const {
        if (!stylesRoot || !master) return nullptr;
        const std::string next = Attr(master, "style:next-style-name");
        if (next.empty()) return master;
        auto* masters = stylesRoot->FirstChildElement("office:master-styles");
        for (auto* page = masters ? masters->FirstChildElement("style:master-page") : nullptr; page;
             page = page->NextSiblingElement("style:master-page")) {
            if (next == Attr(page, "style:name")) return page;
        }
        return master;
    }

    // Chooses which master page supplies the header/footer for the linear
    // rendering: the one pinned by the body's first paragraph, else "Standard"
    // if it carries content, else the first master page that has any content,
    // else the first defined. This keeps letterhead chrome (logo, contacts,
    // bank footer) that lives only on a first-page master.
    tinyxml2::XMLElement* ResolveMasterPage(tinyxml2::XMLElement* stylesRoot,
                                            tinyxml2::XMLElement* body) const {
        if (!stylesRoot) return nullptr;
        auto* masters = stylesRoot->FirstChildElement("office:master-styles");
        if (!masters) return nullptr;
        std::string wanted = AppliedMasterPageName(body);
        tinyxml2::XMLElement* byName = nullptr;
        tinyxml2::XMLElement* standard = nullptr;
        tinyxml2::XMLElement* firstWithContent = nullptr;
        tinyxml2::XMLElement* first = nullptr;
        for (auto* page = masters->FirstChildElement("style:master-page"); page;
             page = page->NextSiblingElement("style:master-page")) {
            if (!first) first = page;
            std::string name = Attr(page, "style:name");
            if (!wanted.empty() && name == wanted) byName = page;
            if (name == "Standard") standard = page;
            if (!firstWithContent && MasterPageHasContent(page)) firstWithContent = page;
        }
        if (byName) return byName;
        if (standard && MasterPageHasContent(standard)) return standard;
        if (firstWithContent) return firstWithContent;
        return standard ? standard : first;
    }

    // Reads a master page's header or footer region into `out`. A region that
    // is switched off, or holds nothing visible, leaves `out` empty.
    void ParseMasterPageRegion(tinyxml2::XMLElement* region, std::vector<RichDocBlock>& out) {
        out.clear();
        if (!region || std::string(Attr(region, "style:display")) == "false") return;
        // The block parsers append to the document's body; collect this
        // region's blocks there and move them out.
        const size_t start = doc_->blocks.size();
        bool savedFlow = inMainFlow_;
        inMainFlow_ = false;   // header/footer paragraphs never break pages
        ParseBlockContainer(region, 0, "");
        inMainFlow_ = savedFlow;
        out.assign(std::make_move_iterator(doc_->blocks.begin() + static_cast<std::ptrdiff_t>(start)),
                   std::make_move_iterator(doc_->blocks.end()));
        doc_->blocks.resize(start);

        bool hasContent = false;
        for (const RichDocBlock& b : out) {
            if (b.type != RichBlockType::Paragraph) { hasContent = true; break; }
            const std::string text = UCRichDocument::ConcatenateRunText(b.runs);
            if (text.find_first_not_of(" \t\n") != std::string::npos) { hasContent = true; break; }
        }
        if (!hasContent) out.clear();
    }

    // A master page's header and footer.
    void ParseMasterPage(tinyxml2::XMLElement* master, RichPageFurniture& out) {
        if (!master) return;
        ParseMasterPageRegion(master->FirstChildElement("style:header"), out.header);
        ParseMasterPageRegion(master->FirstChildElement("style:footer"), out.footer);
    }

    // The page the first master page lays the document on: size, margins, and
    // where the body starts once a header / footer takes its room (ODF puts
    // them inside the page margins and moves the body away from them).
    void LoadPageSetup(tinyxml2::XMLElement* stylesRoot, tinyxml2::XMLElement* master) {
        if (!stylesRoot || !master) return;
        const std::string layoutName = Attr(master, "style:page-layout-name");
        auto* automatic = stylesRoot->FirstChildElement("office:automatic-styles");
        tinyxml2::XMLElement* layout = nullptr;
        for (auto* l = automatic ? automatic->FirstChildElement("style:page-layout") : nullptr; l;
             l = l->NextSiblingElement("style:page-layout")) {
            if (layoutName == Attr(l, "style:name")) { layout = l; break; }
        }
        auto* props = layout ? layout->FirstChildElement("style:page-layout-properties") : nullptr;
        if (!props) return;
        RichPageSetup& page = doc_->page;
        page.widthPt = ParseLengthPt(Attr(props, "fo:page-width"));
        page.heightPt = ParseLengthPt(Attr(props, "fo:page-height"));
        const float margin = ParseLengthPt(Attr(props, "fo:margin"));
        auto side = [&](const char* name) {
            const char* v = props->Attribute(name);
            return v ? ParseLengthPt(v) : margin;
        };
        page.marginTopPt = page.headerTopPt = side("fo:margin-top");
        page.marginBottomPt = page.footerBottomPt = side("fo:margin-bottom");
        page.marginLeftPt = side("fo:margin-left");
        page.marginRightPt = side("fo:margin-right");
        // Header / footer room, when the master page has one: its minimum
        // height, which (as Writer reads it) includes the spacing to the
        // body. A header taller than that pushes the body down further;
        // the view measures it.
        auto room = [&](const char* styleTag, const char* spacingAttr) {
            auto* hfStyle = layout->FirstChildElement(styleTag);
            auto* hf = hfStyle ? hfStyle->FirstChildElement("style:header-footer-properties") : nullptr;
            if (!hf) return 0.0f;
            return std::max(ParseLengthPt(Attr(hf, "fo:min-height")), ParseLengthPt(Attr(hf, spacingAttr)));
        };
        if (master->FirstChildElement("style:header")) page.marginTopPt += room("style:header-style", "fo:margin-bottom");
        if (master->FirstChildElement("style:footer")) page.marginBottomPt += room("style:footer-style", "fo:margin-top");
    }

    void LoadMetadata() {
        std::string metaXml;
        if (!zip_.ReadEntry("meta.xml", metaXml)) return;
        tinyxml2::XMLDocument metaDoc;
        if (metaDoc.Parse(metaXml.c_str()) != tinyxml2::XML_SUCCESS) return;
        auto* root = metaDoc.FirstChildElement("office:document-meta");
        auto* meta = root ? root->FirstChildElement("office:meta") : nullptr;
        if (!meta) return;
        auto readText = [&](const char* tag) -> std::string {
            auto* e = meta->FirstChildElement(tag);
            return (e && e->GetText()) ? e->GetText() : "";
        };
        doc_->metadata.title = readText("dc:title");
        doc_->metadata.author = readText("dc:creator");
        if (doc_->metadata.author.empty()) doc_->metadata.author = readText("meta:initial-creator");
        doc_->metadata.description = readText("dc:description");
        doc_->metadata.createdDate = readText("meta:creation-date");
        doc_->metadata.modifiedDate = readText("dc:date");
    }
};

// ===== ODT WRITING =====

class OdtWriter {
public:
    bool Save(const std::string& filePath, const UCRichDocument& doc, std::string& error) {
        doc_ = &doc;
        if (!zip_.Open(filePath)) {
            error = "Cannot create file: " + filePath;
            return false;
        }
        // ODF requires the mimetype entry first and uncompressed.
        static const char* kMimeType = "application/vnd.oasis.opendocument.text";
        // Body and page furniture first: writing them collects the automatic
        // styles that content.xml and styles.xml each declare.
        std::string body = WriteBlocks(doc.blocks);
        if (!openComments_.empty()) {
            // A comment running to the end of the document ends here.
            std::ostringstream close;
            close << "<text:p>";
            for (int id : openComments_) close << "<office:annotation-end office:name=\"__Annotation__" << id << "\"/>";
            close << "</text:p>\n";
            openComments_.clear();
            body += close.str();
        }
        const std::string masterStyles = BuildMasterStyles();
        if (!zip_.AddEntry("mimetype", std::string(kMimeType), false)
            || !zip_.AddEntry("content.xml", BuildContentXml(body))
            || !zip_.AddEntry("styles.xml", BuildStylesXml(masterStyles))
            || !zip_.AddEntry("meta.xml", BuildMetaXml())
            || !zip_.AddEntry("META-INF/manifest.xml", BuildManifestXml())) {
            error = "Failed to write document package: " + zip_.GetLastError();
            return false;
        }
        for (size_t i = 0; i < doc.media.size(); ++i) {
            if (!zip_.AddEntry(PictureHref(i), doc.media[i].data.data(),
                               doc.media[i].data.size())) {
                error = "Failed to embed image: " + zip_.GetLastError();
                return false;
            }
        }
        if (!zip_.Finalize()) {
            error = "Failed to finalize document: " + zip_.GetLastError();
            return false;
        }
        return true;
    }

private:
    UCZipPackageWriter zip_;
    const UCRichDocument* doc_ = nullptr;
    int noteCounter_ = 0;                 // text:note ids, ftn1 / edn2 ...
    int tocCount_ = 0;
    const std::vector<RichDocBlock>* blocks_ = nullptr;   // the blocks being written (body or furniture)
    int tableCount_ = 0;
    std::string pageLayout_;                // style:page-layout for the master page
    std::vector<RichTextRun> textStyles_;   // formatting tuples for T1..Tn
    std::string columnStyles_;              // automatic table-column styles
    std::string geometryStyles_;            // automatic paragraph styles with geometry
    std::string cellStyles_;                // automatic table-cell styles (frames, fills)
    std::string graphicStylesXml_;          // automatic graphic styles (floating pictures)
    std::map<std::string, std::string> floatingFrameStyles_;   // wrap|through|position -> name
    std::string listStyles_;                // automatic list styles with the document's labels
    int customListStyleCount_ = 0;
    std::map<std::string, std::string> cellStyleNames_;       // properties -> style name
    std::map<std::string, std::string> geometryStyleNames_;   // properties -> style name

    std::string PictureHref(size_t mediaIndex) const {
        const RichDocMedia& m = doc_->media[mediaIndex];
        std::string ext = UCRichDocument::FileExtensionForMimeType(m.mimeType);
        return "Pictures/image" + std::to_string(mediaIndex + 1) + "." + ext;
    }

    // Returns "" for unformatted runs, else the T-style name for the tuple.
    std::string TextStyleNameFor(const RichTextRun& run) {
        RichTextRun key = run;
        key.text.clear();
        key.linkTarget.clear();
        key.lineBreakBefore = false;
        RichTextRun plain;
        if (key.HasSameFormatting(plain)) return "";
        for (size_t i = 0; i < textStyles_.size(); ++i) {
            if (textStyles_[i].HasSameFormatting(key)) {
                return "T" + std::to_string(i + 1);
            }
        }
        textStyles_.push_back(key);
        return "T" + std::to_string(textStyles_.size());
    }

    // Escapes text and converts tabs/newlines/multi-spaces to ODF markup.
    static std::string OdtText(const std::string& text) {
        std::string out;
        out.reserve(text.size());
        size_t spaceRun = 0;
        auto flushSpaces = [&]() {
            if (spaceRun == 0) return;
            out += ' ';
            if (spaceRun > 1) {
                out += "<text:s text:c=\"" + std::to_string(spaceRun - 1) + "\"/>";
            }
            spaceRun = 0;
        };
        for (char c : text) {
            if (c == ' ') { ++spaceRun; continue; }
            flushSpaces();
            if (c == '\t') out += "<text:tab/>";
            else if (c == '\n') out += "<text:line-break/>";
            else if (c == '&') out += "&amp;";
            else if (c == '<') out += "&lt;";
            else if (c == '>') out += "&gt;";
            else out.push_back(c);
        }
        flushSpaces();
        return out;
    }

    // A picture inside a line: the same draw:frame a block image uses, anchored
    // as-char so it stays in the text rather than becoming its own paragraph.
    void WriteInlineImage(std::ostringstream& xml, const RichTextRun& run) {
        if (run.mediaIndex < 0 || run.mediaIndex >= static_cast<int>(doc_->media.size())) {
            // No such picture: keep the alt text rather than emitting nothing.
            xml << OdtText(run.imageAltText);
            return;
        }
        float widthPt = run.imageWidthPt;
        float heightPt = run.imageHeightPt;
        if (widthPt <= 0.0f || heightPt <= 0.0f) {
            int w = 0, h = 0;
            if (UCRichDocument::SniffImagePixelSize(doc_->media[run.mediaIndex].data, w, h)) {
                widthPt = static_cast<float>(w) * 72.0f / 96.0f;
                heightPt = static_cast<float>(h) * 72.0f / 96.0f;
            } else {
                widthPt = 72.0f;
                heightPt = 72.0f;
            }
        }
        xml << "<draw:frame draw:name=\""
            << EscapeXml(run.imageAltText.empty() ? std::string("Image") : run.imageAltText) << "\"";
        if (run.IsFloatingImage()) {
            xml << " draw:style-name=\"" << FloatingFrameStyle(run) << "\" text:anchor-type=\"paragraph\"";
            if (run.imageFloatAlign == RichTextAlign::Default) xml << " svg:x=\"" << Pt(run.imageOffsetXPt) << "\"";
            if (run.floatVerticalAlign == RichTextRun::FloatVerticalAlign::Top) {
                xml << " svg:y=\"" << Pt(run.imageOffsetYPt) << "\"";
            }
        } else {
            xml << " text:anchor-type=\"as-char\"";
        }
        xml << " svg:width=\"" << Pt(widthPt) << "\" svg:height=\"" << Pt(heightPt) << "\">"
            << "<draw:image xlink:href=\"" << PictureHref(run.mediaIndex)
            << "\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\"/>"
            << "</draw:frame>";
    }

    // ===== DRAWN SHAPES =====
    // A shape's fill, line (dash, arrowheads) and where its words sit, as
    // graphic-properties attributes.
    std::string ShapeLookAttributes(const RichShape& shape, float padding, RichShape::TextAnchor anchor) {
        std::string look;
        if (shape.HasPictureFill() && shape.IsClosed() && shape.fillMediaIndex < static_cast<int>(doc_->media.size())) {
            look += " draw:fill=\"bitmap\" draw:fill-image-name=\"" + FillImageName(shape.fillMediaIndex)
                  + "\" style:repeat=\"" + (shape.fillPictureTiled ? "repeat" : "stretch") + "\"";
        } else if (shape.fillColor.empty() || !shape.IsClosed()) look += " draw:fill=\"none\"";
        else if (shape.HasGradient()) look += " draw:fill=\"gradient\" draw:fill-gradient-name=\"" + GradientName(shape)
                                           + "\" draw:fill-color=\"" + EscapeXml(shape.fillColor) + "\"";
        else if (shape.HasPattern()) look += PatternFill(shape);
        else look += " draw:fill=\"solid\" draw:fill-color=\"" + EscapeXml(shape.fillColor) + "\"";
        if (shape.lineColor.empty()) {
            look += " draw:stroke=\"none\"";
        } else {
            if (shape.dash == RichShape::Dash::Solid) look += " draw:stroke=\"solid\"";
            else look += std::string(" draw:stroke=\"dash\" draw:stroke-dash=\"")
                       + (shape.dash == RichShape::Dash::Dotted ? "UC_Dot" : "UC_Dash") + "\"";
            look += " svg:stroke-color=\"" + EscapeXml(shape.lineColor) + "\" svg:stroke-width=\""
                  + Pt(std::max(0.0f, shape.lineWidthPt)) + "\"";
            const std::string arrow = Pt(std::max(6.0f, shape.lineWidthPt * 3.5f));
            if (shape.arrowStart) look += " draw:marker-start=\"UC_Arrow\" draw:marker-start-width=\"" + arrow + "\"";
            if (shape.arrowEnd) look += " draw:marker-end=\"UC_Arrow\" draw:marker-end-width=\"" + arrow + "\"";
        }
        const char* vertical = anchor == RichShape::TextAnchor::Middle ? "middle"
                             : anchor == RichShape::TextAnchor::Bottom ? "bottom" : "top";
        const std::string inset = Pt(std::max(0.0f, padding));
        look += std::string(" draw:textarea-vertical-align=\"") + vertical + "\" fo:padding-left=\"" + inset
              + "\" fo:padding-right=\"" + inset + "\" fo:padding-top=\"" + inset + "\" fo:padding-bottom=\"" + inset
              + "\" draw:auto-grow-height=\"false\"";
        return look;
    }

    // A draw:gradient for a shape's gradient, defined once in office:styles.
    // ODF holds two colours: the first and last stops (a three-stop one that
    // comes back to its first colour is axial). Its angle turns anticlockwise
    // from top-to-bottom, in degrees.
    std::map<std::string, std::string> gradientNames_;   // key -> name
    std::string gradientDefinitions_;
    std::string GradientName(const RichShape& shape) {
        const std::string first = shape.gradientStops.front().color, last = shape.gradientStops.back().color;
        const bool axial = !shape.gradientRadial && shape.gradientStops.size() == 3
                        && ToLower(first) == ToLower(last);
        std::string style = shape.gradientRadial ? "radial" : axial ? "axial" : "linear";
        std::string start = axial ? shape.gradientStops[1].color : first, end = axial ? first : last;
        if (shape.gradientRadial) std::swap(start, end);           // ODF: the edge's colour first
        double angle = 90.0 - static_cast<double>(shape.gradientAngleDeg);
        angle = std::fmod(angle, 360.0);
        if (angle < 0.0) angle += 360.0;
        const long degrees = std::lround(angle) % 360;
        const long cx = std::lround(std::clamp(shape.gradientCenterX, 0.0f, 1.0f) * 100.0f);
        const long cy = std::lround(std::clamp(shape.gradientCenterY, 0.0f, 1.0f) * 100.0f);
        const std::string centre = shape.gradientRadial
            ? " draw:cx=\"" + std::to_string(cx) + "%\" draw:cy=\"" + std::to_string(cy) + "%\"" : std::string();
        const std::string key = style + "|" + start + "|" + end + "|" + std::to_string(degrees) + centre;
        auto found = gradientNames_.find(key);
        if (found != gradientNames_.end()) return found->second;
        const std::string name = "UC_Gradient_" + std::to_string(gradientNames_.size() + 1);
        gradientNames_[key] = name;
        gradientDefinitions_ += "<draw:gradient draw:name=\"" + name + "\" draw:style=\"" + style + "\"" + centre
                              + " draw:start-color=\"" + EscapeXml(start) + "\" draw:end-color=\"" + EscapeXml(end)
                              + "\" draw:start-intensity=\"100%\" draw:end-intensity=\"100%\" draw:angle=\""
                              + std::to_string(degrees) + "deg\" draw:border=\"0%\"/>\n";
        return name;
    }

    // A picture fill's draw:fill-image, defined once per picture in office:styles.
    std::map<int, std::string> fillImageNames_;
    std::string FillImageName(int mediaIndex) {
        auto found = fillImageNames_.find(mediaIndex);
        if (found != fillImageNames_.end()) return found->second;
        const std::string name = "UC_Fill_" + std::to_string(fillImageNames_.size() + 1);
        fillImageNames_[mediaIndex] = name;
        gradientDefinitions_ += "<draw:fill-image draw:name=\"" + name + "\" xlink:href=\""
                              + PictureHref(static_cast<size_t>(mediaIndex))
                              + "\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\"/>\n";
        return name;
    }

    // A pattern fill: ODF hatches its lines (draw:hatch, defined once in
    // office:styles); a pattern of dots or squares, which no hatch draws,
    // becomes the colour it looks like from afar.
    std::map<std::string, std::string> hatchNames_;
    std::string PatternFill(const RichShape& shape) {
        const std::string p = RichShape::NearestPresetPattern(shape.fillPattern);
        auto has = [&](const char* part) { return p.find(part) != std::string::npos; };
        std::string style = "single";
        int rotation = -1;                                    // degrees, anticlockwise
        if (has("Horz") || p == "horz") rotation = 0;
        else if (has("Vert") || p == "vert") rotation = 90;
        else if (has("UpDiag") || p == "upDiag") rotation = 45;
        else if (has("DnDiag") || p == "dnDiag") rotation = 135;
        else if (p == "cross" || p == "smGrid" || p == "lgGrid" || p == "dotGrid") { style = "double"; rotation = 0; }
        else if (p == "diagCross") { style = "double"; rotation = 45; }
        const std::string back = shape.patternBackColor;
        if (rotation < 0) {
            // Dots, checks, bricks: no hatch draws them, so the 8 x 8 tile
            // itself, a picture held in the style, repeated (a 96 DPI PNG
            // with its background left clear when it has none).
            const std::string tile = PatternTilePng(shape);
            if (!tile.empty()) {
                const std::string key = "tile|" + shape.fillPattern + "|" + shape.fillColor + "|" + back;
                auto found = hatchNames_.find(key);
                std::string name;
                if (found != hatchNames_.end()) {
                    name = found->second;
                } else {
                    name = "UC_Pattern_" + std::to_string(hatchNames_.size() + 1);
                    hatchNames_[key] = name;
                    gradientDefinitions_ += "<draw:fill-image draw:name=\"" + name + "\"><office:binary-data>" + tile
                                          + "</office:binary-data></draw:fill-image>\n";
                }
                return " draw:fill=\"bitmap\" draw:fill-image-name=\"" + name + "\" style:repeat=\"repeat\"";
            }
            // The tile's share of foreground, over its background.
            int on = 0;
            for (int i = 0; i < 64; ++i) on += RichShape::PatternBit(shape.fillPattern, i % 8, i / 8) ? 1 : 0;
            const float share = static_cast<float>(on) / 64.0f;
            auto channel = [](const std::string& hex, size_t at) {
                return hex.size() == 7 ? std::strtol(hex.substr(at, 2).c_str(), nullptr, 16) : 255L;
            };
            char mixed[8];
            std::snprintf(mixed, sizeof mixed, "#%02X%02X%02X",
                          static_cast<int>(std::lround(channel(shape.fillColor, 1) * share + channel(back, 1) * (1.0f - share))),
                          static_cast<int>(std::lround(channel(shape.fillColor, 3) * share + channel(back, 3) * (1.0f - share))),
                          static_cast<int>(std::lround(channel(shape.fillColor, 5) * share + channel(back, 5) * (1.0f - share))));
            return std::string(" draw:fill=\"solid\" draw:fill-color=\"") + mixed + "\"";
        }
        // Light and narrow patterns draw their lines closer together.
        const float distance = has("nar") ? 1.5f : (has("lt") || has("dk") || p == "smGrid" || p == "dotGrid") ? 3.0f : 6.0f;
        const std::string key = style + "|" + std::to_string(rotation) + "|" + shape.fillColor + "|" + Pt(distance);
        auto found = hatchNames_.find(key);
        std::string name;
        if (found != hatchNames_.end()) {
            name = found->second;
        } else {
            name = "UC_Hatch_" + std::to_string(hatchNames_.size() + 1);
            hatchNames_[key] = name;
            gradientDefinitions_ += "<draw:hatch draw:name=\"" + name + "\" draw:style=\"" + style + "\" draw:color=\""
                                  + EscapeXml(shape.fillColor) + "\" draw:distance=\"" + Pt(distance)
                                  + "\" draw:rotation=\"" + std::to_string(rotation * 10) + "\"/>\n";
        }
        std::string out = " draw:fill=\"hatch\" draw:fill-hatch-name=\"" + name + "\"";
        if (!back.empty()) out += " draw:fill-hatch-solid=\"true\" draw:fill-color=\"" + EscapeXml(back) + "\"";
        else out += " draw:fill-hatch-solid=\"false\"";
        return out;
    }

    // A pattern's 8 x 8 tile as a PNG, in base64: the foreground's pixels,
    // the background's (clear when there is none).
    static std::string PatternTilePng(const RichShape& shape) {
        auto channel = [](const std::string& hex, size_t at) {
            return static_cast<uint8_t>(hex.size() == 7 ? std::strtol(hex.substr(at, 2).c_str(), nullptr, 16) : 0L);
        };
        std::vector<uint8_t> pixels(8 * 8 * 4);
        for (int i = 0; i < 64; ++i) {
            const bool fore = RichShape::PatternBit(shape.fillPattern, i % 8, i / 8);
            const std::string& colour = fore ? shape.fillColor : shape.patternBackColor;
            uint8_t* px = &pixels[static_cast<size_t>(i) * 4];
            px[0] = channel(colour, 1);
            px[1] = channel(colour, 3);
            px[2] = channel(colour, 5);
            px[3] = (!fore && shape.patternBackColor.empty()) ? 0 : 255;
        }
        size_t length = 0;
        void* png = tdefl_write_image_to_png_file_in_memory(pixels.data(), 8, 8, 4, &length);
        if (!png) return "";
        std::vector<uint8_t> bytes(static_cast<uint8_t*>(png), static_cast<uint8_t*>(png) + length);
        mz_free(png);
        return Base64Encode(bytes, false);
    }

    // A group member's style: its look alone.
    std::string ShapeStyle(const std::string& look) {
        auto found = shapeStyles_.find(look);
        if (found != shapeStyles_.end()) return found->second;
        const std::string name = "gr" + std::to_string(shapeStyles_.size() + 1);
        shapeStyles_[look] = name;
        graphicStylesXml_ += "<style:style style:name=\"" + name + "\" style:family=\"graphic\">"
                             "<style:graphic-properties" + look + "/></style:style>\n";
        return name;
    }
    std::map<std::string, std::string> shapeStyles_;

    // A group's turn and mirroring passed down to a member: its box's
    // centre moved round the group's, its own turn and flips changed to match.
    static RichShape PassDown(const RichShape& member, const RichShape& group) {
        RichShape out = member;
        const float gw = group.widthPt, gh = group.heightPt;
        float cx = member.xPt + member.widthPt * 0.5f, cy = member.yPt + member.heightPt * 0.5f;
        if (group.flipH) {
            cx = gw - cx;
            out.flipH = !out.flipH;
            out.rotationDeg = -out.rotationDeg;
        }
        if (group.flipV) {
            cy = gh - cy;
            out.flipV = !out.flipV;
            out.rotationDeg = -out.rotationDeg;
        }
        if (group.rotationDeg != 0.0f) {
            constexpr double kRad = 3.14159265358979323846 / 180.0;
            const double angle = static_cast<double>(group.rotationDeg) * kRad;   // clockwise on the page
            const double dx = cx - gw * 0.5f, dy = cy - gh * 0.5f;
            cx = static_cast<float>(gw * 0.5 + dx * std::cos(angle) - dy * std::sin(angle));
            cy = static_cast<float>(gh * 0.5 + dx * std::sin(angle) + dy * std::cos(angle));
            out.rotationDeg += group.rotationDeg;
        }
        out.xPt = cx - member.widthPt * 0.5f;
        out.yPt = cy - member.heightPt * 0.5f;
        return out;
    }

    // Where a shape's box goes: svg:x / svg:y, or - turned - a draw:transform
    // turning it about its centre (ODF turns anticlockwise, about the box's
    // corner, then moves it).
    static std::string ShapeBoxAttributes(const RichShape& shape, float x, float y) {
        std::string out = " svg:width=\"" + Pt(std::max(0.0f, shape.widthPt)) + "\" svg:height=\""
                        + Pt(std::max(0.0f, shape.heightPt)) + "\"";
        if (std::fmod(static_cast<double>(shape.rotationDeg), 360.0) == 0.0) {
            return out + " svg:x=\"" + Pt(x) + "\" svg:y=\"" + Pt(y) + "\"";
        }
        constexpr double kRad = 3.14159265358979323846 / 180.0;
        const double angle = -static_cast<double>(shape.rotationDeg) * kRad;      // anticlockwise
        const double hw = shape.widthPt * 0.5, hh = shape.heightPt * 0.5;
        // The centre, turned about the corner as ODF turns it.
        const double turnedX = hw * std::cos(angle) + hh * std::sin(angle);
        const double turnedY = -hw * std::sin(angle) + hh * std::cos(angle);
        const double tx = x + hw - turnedX, ty = y + hh - turnedY;
        std::ostringstream transform;
        transform.imbue(std::locale::classic());
        transform << std::fixed << std::setprecision(6) << " draw:transform=\"rotate (" << angle << ") translate ("
                  << std::setprecision(2) << tx << "pt " << ty << "pt)\"";
        return out + transform.str();
    }

    // One shape (or group) as ODF draws it, at (originX, originY) plus its
    // own box; `style` places a top-level one, else its look alone does.
    void WriteShapeElement(std::ostringstream& xml, const RichShape& shape, float originX, float originY,
                           const std::string& style, const std::string& anchor, const std::vector<RichDocBlock>* words,
                           float padding, RichShape::TextAnchor textAnchor, int depth) {
        using Kind = RichShape::Kind;
        using Op = RichShape::PathPoint::Op;
        if (depth > 16) return;
        const float x = originX + shape.xPt, y = originY + shape.yPt;
        const std::string styleName = style.empty() ? ShapeStyle(ShapeLookAttributes(shape, padding, textAnchor)) : style;
        const std::string name = shape.name.empty() ? "" : " draw:name=\"" + EscapeXml(shape.name) + "\"";
        if (shape.kind == Kind::Group) {
            xml << "<draw:g" << name << (style.empty() ? "" : " draw:style-name=\"" + style + "\"") << anchor << ">";
            for (const RichShape& member : shape.children) {
                WriteShapeMember(xml, PassDown(member, shape), x, y, depth + 1);
            }
            xml << "</draw:g>";
            return;
        }
        if (shape.mediaIndex >= 0 && shape.mediaIndex < static_cast<int>(doc_->media.size())) {
            xml << "<draw:frame draw:style-name=\"" << styleName << "\"" << name << anchor
                << ShapeBoxAttributes(shape, x, y) << "><draw:image xlink:href=\"" << PictureHref(static_cast<size_t>(shape.mediaIndex))
                << "\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\"/></draw:frame>";
            return;
        }
        auto wordsXml = [&]() {
            if (words && !words->empty() && shape.kind != Kind::Line) xml << WriteBlocks(*words);
        };
        if (shape.kind == Kind::Line) {
            // Its two ends, mirrored and turned as the shape is.
            double x1 = 0, y1 = 0, x2 = shape.widthPt, y2 = shape.heightPt;
            if (shape.flipH) std::swap(x1, x2);
            if (shape.flipV) std::swap(y1, y2);
            const double cx = shape.widthPt * 0.5, cy = shape.heightPt * 0.5;
            if (shape.rotationDeg != 0.0f) {
                constexpr double kRad = 3.14159265358979323846 / 180.0;
                const double angle = static_cast<double>(shape.rotationDeg) * kRad;
                auto turn = [&](double& px, double& py) {
                    const double dx = px - cx, dy = py - cy;
                    px = cx + dx * std::cos(angle) - dy * std::sin(angle);
                    py = cy + dx * std::sin(angle) + dy * std::cos(angle);
                };
                turn(x1, y1);
                turn(x2, y2);
            }
            xml << "<draw:line draw:style-name=\"" << styleName << "\"" << name << anchor
                << " svg:x1=\"" << Pt(static_cast<float>(x + x1)) << "\" svg:y1=\"" << Pt(static_cast<float>(y + y1))
                << "\" svg:x2=\"" << Pt(static_cast<float>(x + x2)) << "\" svg:y2=\"" << Pt(static_cast<float>(y + y2))
                << "\"/>";
            return;
        }
        if (shape.kind == Kind::Path) {
            // Mirroring is drawn into the points; the view box is 0..10000.
            std::ostringstream d;
            d.imbue(std::locale::classic());
            auto px = [&](float v) { return std::lround((shape.flipH ? 1.0f - v : v) * 10000.0f); };
            auto py = [&](float v) { return std::lround((shape.flipV ? 1.0f - v : v) * 10000.0f); };
            for (const RichShape::PathPoint& point : shape.path) {
                switch (point.op) {
                    case Op::Move: d << "M" << px(point.x) << " " << py(point.y) << " "; break;
                    case Op::Line: d << "L" << px(point.x) << " " << py(point.y) << " "; break;
                    case Op::Curve:
                        d << "C" << px(point.c1x) << " " << py(point.c1y) << " " << px(point.c2x) << " "
                          << py(point.c2y) << " " << px(point.x) << " " << py(point.y) << " ";
                        break;
                    case Op::Close: d << "Z "; break;
                }
            }
            // ODF fills only a closed outline; Word fills an open one too.
            if (!shape.fillColor.empty() && shape.IsOpen()) d << "Z";
            xml << "<draw:path draw:style-name=\"" << styleName << "\"" << name << anchor
                << ShapeBoxAttributes(shape, x, y) << " svg:viewBox=\"0 0 10000 10000\" svg:d=\""
                << d.str() << "\">";
            wordsXml();
            xml << "</draw:path>";
            return;
        }
        const char* tag = shape.kind == Kind::Ellipse ? "draw:ellipse" : "draw:rect";
        xml << "<" << tag << " draw:style-name=\"" << styleName << "\"" << name << anchor << ShapeBoxAttributes(shape, x, y);
        if (shape.kind == Kind::RoundRectangle) {
            xml << " draw:corner-radius=\""
                << Pt(std::clamp(shape.cornerRadius, 0.0f, 0.5f) * std::min(shape.widthPt, shape.heightPt)) << "\"";
        }
        xml << ">";
        wordsXml();
        xml << "</" << tag << ">";
    }

    void WriteShapeMember(std::ostringstream& xml, const RichShape& shape, float originX, float originY, int depth) {
        const RichTextFrame* words = shape.textFrame >= 0 && shape.textFrame < static_cast<int>(doc_->frames.size())
            ? &doc_->frames[static_cast<size_t>(shape.textFrame)] : nullptr;
        WriteShapeElement(xml, shape, originX, originY, "", "", words ? &words->blocks : nullptr,
                          shape.textPaddingPt, shape.textAnchor, depth);
    }

    // A shape's box (and its members') stretched by (sx, sy).
    static void ScaleShape(RichShape& shape, float sx, float sy, int depth) {
        shape.xPt *= sx;
        shape.yPt *= sy;
        shape.widthPt *= sx;
        shape.heightPt *= sy;
        if (depth > 16) return;
        for (RichShape& member : shape.children) ScaleShape(member, sx, sy, depth + 1);
    }

    // A drawing, anchored to its paragraph and placed as its run says: a lone
    // shape (with the frame's words), else a group of its shapes.
    void WriteDrawing(std::ostringstream& xml, const RichTextRun& run, const RichTextFrame& frame) {
        const float x = run.imageFloatAlign == RichTextAlign::Default ? run.imageOffsetXPt : 0.0f;
        const float y = run.floatVerticalAlign == RichTextRun::FloatVerticalAlign::Top ? run.imageOffsetYPt : 0.0f;
        const std::string anchor = " text:anchor-type=\"paragraph\"";
        if (frame.shapes.size() == 1 && frame.shapes.front().kind != RichShape::Kind::Group) {
            RichShape shape = frame.shapes.front();
            shape.xPt = shape.yPt = 0.0f;
            shape.widthPt = run.imageWidthPt;
            shape.heightPt = run.imageHeightPt;
            if (shape.name.empty()) shape.name = frame.name;
            const std::string style = FloatingFrameStyle(run, nullptr,
                                                         ShapeLookAttributes(shape, frame.paddingPt, frame.textAnchor));
            WriteShapeElement(xml, shape, x, y, style, anchor, &frame.blocks, frame.paddingPt, frame.textAnchor, 0);
            return;
        }
        // One group is the drawing's own; several shapes are grouped. A
        // resized drawing's shapes are scaled to its size (ODF draws them
        // where they are).
        RichShape group;
        if (frame.shapes.size() == 1) {
            group = frame.shapes.front();
            group.xPt = group.yPt = 0.0f;
        } else {
            group.kind = RichShape::Kind::Group;
            group.widthPt = frame.drawingWidthPt > 0.0f ? frame.drawingWidthPt : run.imageWidthPt;
            group.heightPt = frame.drawingHeightPt > 0.0f ? frame.drawingHeightPt : run.imageHeightPt;
            group.children = frame.shapes;
        }
        const float scaleX = group.widthPt > 0.0f && run.imageWidthPt > 0.0f ? run.imageWidthPt / group.widthPt : 1.0f;
        const float scaleY = group.heightPt > 0.0f && run.imageHeightPt > 0.0f ? run.imageHeightPt / group.heightPt : 1.0f;
        if (scaleX != 1.0f || scaleY != 1.0f) ScaleShape(group, scaleX, scaleY, 0);
        if (group.name.empty()) group.name = frame.name;
        WriteShapeElement(xml, group, x, y, FloatingFrameStyle(run), anchor, nullptr, 0.0f,
                          RichShape::TextAnchor::Top, 0);
    }

    // A text frame: a paragraph-anchored draw:frame holding a draw:text-box
    // with the frame's paragraphs, placed and styled as the run says.
    void WriteTextFrame(std::ostringstream& xml, const RichTextRun& run) {
        const RichTextFrame* frame = doc_->FrameFor(run);
        if (!frame) return;
        const float widthPt = run.imageWidthPt > 0.0f ? run.imageWidthPt : 144.0f;
        xml << "<draw:frame draw:style-name=\"" << FloatingFrameStyle(run, frame) << "\"";
        if (!frame->name.empty()) xml << " draw:name=\"" << EscapeXml(frame->name) << "\"";
        xml << " text:anchor-type=\"paragraph\"";
        if (run.imageFloatAlign == RichTextAlign::Default) xml << " svg:x=\"" << Pt(run.imageOffsetXPt) << "\"";
        if (run.floatVerticalAlign == RichTextRun::FloatVerticalAlign::Top) xml << " svg:y=\"" << Pt(run.imageOffsetYPt) << "\"";
        xml << " svg:width=\"" << Pt(widthPt) << "\"";
        // Its height too: Writer sizes the frame by it (and lets the box grow).
        if (run.imageHeightPt > 0.0f) xml << " svg:height=\"" << Pt(run.imageHeightPt) << "\"";
        xml << "><draw:text-box";
        if (run.imageHeightPt > 0.0f) xml << " fo:min-height=\"" << Pt(run.imageHeightPt) << "\"";
        xml << ">";
        std::vector<RichDocBlock> blocks = frame->blocks;
        if (blocks.empty()) blocks.emplace_back();
        xml << WriteBlocks(blocks) << "</draw:text-box></draw:frame>";
    }

    // An automatic graphic style saying how a floating picture or text frame
    // wraps, where it sits and what that counts from, and a frame's fill,
    // border and padding; one per distinct combination.
    std::string FloatingFrameStyle(const RichTextRun& run, const RichTextFrame* frame = nullptr,
                                   const std::string& shapeLook = "") {
        using Wrap = RichTextRun::ImageWrap;
        using Origin = RichTextRun::FloatOrigin;
        using VAlign = RichTextRun::FloatVerticalAlign;
        std::string wrap = "parallel", runThrough = "foreground";
        if (run.imageWrap == Wrap::TopAndBottom) wrap = "none";
        else if (run.imageWrap == Wrap::BehindText) { wrap = "run-through"; runThrough = "background"; }
        else if (run.imageWrap == Wrap::InFrontOfText) wrap = "run-through";
        const std::string position = run.imageFloatAlign == RichTextAlign::Right ? "right"
                                   : run.imageFloatAlign == RichTextAlign::Center ? "center"
                                   : run.imageFloatAlign == RichTextAlign::Default ? "from-left" : "left";
        const std::string verticalPosition = run.floatVerticalAlign == VAlign::Middle ? "middle"
                                           : run.floatVerticalAlign == VAlign::Bottom ? "bottom" : "from-top";
        auto relation = [](Origin origin, bool across, Origin other) -> std::string {
            if (origin == Origin::Page) return "page";
            if (origin == Origin::Margin && !(across && other == Origin::Paragraph)) return "page-content";
            return "paragraph";
        };
        const std::string horizontalRel = relation(run.floatHorizontalFrom, true, run.floatVerticalFrom);
        const std::string verticalRel = relation(run.floatVerticalFrom, false, run.floatHorizontalFrom);
        std::string look;
        if (frame) {
            look += frame->backgroundColor.empty()
                  ? std::string(" draw:fill=\"none\"")
                  : " draw:fill=\"solid\" draw:fill-color=\"" + EscapeXml(frame->backgroundColor) + "\"";
            look += " fo:border=\"" + BorderValue(frame->border) + "\"";
            look += " fo:padding=\"" + Pt(std::max(0.0f, frame->paddingPt)) + "\"";
            if (frame->textAnchor != RichShape::TextAnchor::Top) {
                look += std::string(" draw:textarea-vertical-align=\"")
                      + (frame->textAnchor == RichShape::TextAnchor::Middle ? "middle" : "bottom") + "\"";
            }
        }
        look += shapeLook;
        const std::string key = wrap + "|" + runThrough + "|" + position + "|" + verticalPosition + "|"
                              + horizontalRel + "|" + verticalRel + "|" + look;
        auto found = floatingFrameStyles_.find(key);
        if (found != floatingFrameStyles_.end()) return found->second;
        const std::string name = "fr" + std::to_string(floatingFrameStyles_.size() + 1);
        floatingFrameStyles_[key] = name;
        graphicStylesXml_ += "<style:style style:name=\"" + name + "\" style:family=\"graphic\">"
                             "<style:graphic-properties style:wrap=\"" + wrap + "\" style:run-through=\""
                             + runThrough + "\" style:horizontal-pos=\"" + position
                             + "\" style:horizontal-rel=\"" + horizontalRel + "\" style:vertical-pos=\""
                             + verticalPosition + "\" style:vertical-rel=\"" + verticalRel + "\""
                               " fo:margin-left=\"0.32cm\" fo:margin-right=\"0.32cm\"" + look
                             + "/></style:style>\n";
        return name;
    }

    // A note is written where it is referenced: its citation and its body.
    void WriteNote(std::ostringstream& xml, const RichTextRun& run) {
        if (run.noteIndex >= static_cast<int>(doc_->notes.size())) return;
        const RichNote& note = doc_->notes[static_cast<size_t>(run.noteIndex)];
        const bool endnote = note.kind == RichNote::Kind::Endnote;
        std::vector<RichDocBlock> blocks = note.blocks;
        if (blocks.empty()) blocks.emplace_back();
        xml << "<text:note text:id=\"" << (endnote ? "edn" : "ftn") << (++noteCounter_)
            << "\" text:note-class=\"" << (endnote ? "endnote" : "footnote") << "\">"
            << "<text:note-citation>" << OdtText(run.text) << "</text:note-citation><text:note-body>";
        // The note's text is its own: comments open around the reference
        // stay open past it.
        std::vector<int> open;
        open.swap(openComments_);
        xml << WriteBlocks(blocks);
        openComments_.swap(open);
        xml << "</text:note-body></text:note>";
    }

    // Comments: an office:annotation where the text under it starts and an
    // office:annotation-end where it stops, which may be paragraphs later.
    std::vector<int> openComments_;
    std::set<int> writtenComments_;

    void UpdateOpenComments(std::ostringstream& xml, const std::vector<int>& wanted) {
        for (size_t i = 0; i < openComments_.size();) {
            const int id = openComments_[i];
            if (std::find(wanted.begin(), wanted.end(), id) == wanted.end()) {
                xml << "<office:annotation-end office:name=\"__Annotation__" << id << "\"/>";
                openComments_.erase(openComments_.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                i++;
            }
        }
        for (int id : wanted) {
            if (id < 0 || id >= static_cast<int>(doc_->comments.size())) continue;
            if (std::find(openComments_.begin(), openComments_.end(), id) != openComments_.end()) continue;
            if (writtenComments_.count(id)) continue;
            const RichComment& comment = doc_->comments[static_cast<size_t>(id)];
            xml << "<office:annotation office:name=\"__Annotation__" << id << "\""
                << (comment.resolved ? " loext:resolved=\"true\"" : "") << ">";
            if (!comment.author.empty()) xml << "<dc:creator>" << EscapeXml(comment.author) << "</dc:creator>";
            if (!comment.date.empty()) xml << "<dc:date>" << EscapeXml(comment.date) << "</dc:date>";
            size_t start = 0;
            do {
                const size_t end = comment.text.find('\n', start);
                const std::string line = comment.text.substr(start, end == std::string::npos ? std::string::npos : end - start);
                xml << "<text:p>" << OdtText(line) << "</text:p>";
                start = end == std::string::npos ? std::string::npos : end + 1;
            } while (start != std::string::npos);
            xml << "</office:annotation>";
            openComments_.push_back(id);
            writtenComments_.insert(id);
        }
    }

    // Tracked changes: one changed region per changed run, listed at the top
    // of the text.
    std::ostringstream changedRegions_;
    int changeCount_ = 0;

    std::string ChangeInfo(const RichTextRun& run) const {
        const RichRevision* revision = run.revision >= 0 && run.revision < static_cast<int>(doc_->revisions.size())
                                     ? &doc_->revisions[static_cast<size_t>(run.revision)] : nullptr;
        std::string info = "<office:change-info><dc:creator>" + EscapeXml(revision ? revision->author : "")
                         + "</dc:creator><dc:date>"
                         + EscapeXml(revision && !revision->date.empty() ? revision->date : "1970-01-01T00:00:00")
                         + "</dc:date></office:change-info>";
        return info;
    }

    void WriteRuns(std::ostringstream& xml, const std::vector<RichTextRun>& runs) {
        for (const auto& run : runs) {
            UpdateOpenComments(xml, run.commentIds);
            if (run.change == RichTextRun::Change::Deleted) {
                // Deleted text lives in its region; the text has a point.
                const std::string id = "ct" + std::to_string(++changeCount_);
                changedRegions_ << "<text:changed-region text:id=\"" << id << "\"><text:deletion>" << ChangeInfo(run)
                                << "<text:p>" << OdtText(run.text) << "</text:p></text:deletion></text:changed-region>";
                if (run.lineBreakBefore) xml << "<text:line-break/>";
                xml << "<text:change text:change-id=\"" << id << "\"/>";
                continue;
            }
            std::string changeId;
            if (run.change == RichTextRun::Change::Inserted) {
                changeId = "ct" + std::to_string(++changeCount_);
                changedRegions_ << "<text:changed-region text:id=\"" << changeId << "\"><text:insertion>"
                                << ChangeInfo(run) << "</text:insertion></text:changed-region>";
                xml << "<text:change-start text:change-id=\"" << changeId << "\"/>";
            }
            WriteRun(xml, run);
            if (!changeId.empty()) xml << "<text:change-end text:change-id=\"" << changeId << "\"/>";
        }
    }

    void WriteRun(std::ostringstream& xml, const RichTextRun& run) {
        if (run.lineBreakBefore) xml << "<text:line-break/>";
        if (run.IsInlineImage()) {
            WriteInlineImage(xml, run);
            return;
        }
        if (run.IsTextFrame()) {
            const RichTextFrame* frame = doc_->FrameFor(run);
            if (frame && frame->IsDrawing()) WriteDrawing(xml, run, *frame);
            else WriteTextFrame(xml, run);
            return;
        }
        if (run.IsNoteReference()) {
            WriteNote(xml, run);
            return;
        }
        std::string styleName = TextStyleNameFor(run);
        std::string body = OdtText(run.text);
        if (run.field == RichTextRun::Field::PageNumber) {
            body = "<text:page-number text:select-page=\"current\">" + body + "</text:page-number>";
        } else if (run.field == RichTextRun::Field::PageCount) {
            body = "<text:page-count>" + body + "</text:page-count>";
        } else if (run.field == RichTextRun::Field::Sequence) {
            const std::string name = EscapeXml(run.fieldArgument);
            body = "<text:sequence text:name=\"" + name + "\" text:formula=\"ooow:" + name
                 + "+1\" style:num-format=\"1\">" + body + "</text:sequence>";
        } else if (run.field == RichTextRun::Field::Reference
                   || run.field == RichTextRun::Field::PageReference) {
            body = std::string("<text:bookmark-ref text:reference-format=\"")
                 + (run.field == RichTextRun::Field::PageReference ? "page" : "text")
                 + "\" text:ref-name=\"" + EscapeXml(run.fieldArgument) + "\">" + body + "</text:bookmark-ref>";
        }
        if (!styleName.empty()) {
            body = "<text:span text:style-name=\"" + styleName + "\">" + body + "</text:span>";
        }
        if (!run.linkTarget.empty()) {
            body = "<text:a xlink:type=\"simple\" xlink:href=\""
                 + EscapeXml(run.linkTarget) + "\">" + body + "</text:a>";
        }
        xml << body;
    }

    std::string ParagraphStyleFor(const RichDocBlock& block) {
        // The paragraph's own named style; an automatic one based on it when
        // the paragraph also has formatting of its own.
        if (!block.styleId.empty() && doc_->FindStyle(block.styleId)
            && (block.type == RichBlockType::Paragraph || block.type == RichBlockType::Heading)) {
            const std::string parent = StyleIdToOdfName(block.styleId);
            if (block.HasParagraphGeometry() || block.align != RichTextAlign::Default || block.rightToLeft
                || block.paragraphFontSizePt > 0.0f || !block.paragraphFontFamily.empty()) {
                return GeometryStyleFor(block, parent);
            }
            return parent;
        }
        switch (block.type) {
            case RichBlockType::BlockQuote: return "PQuote";
            case RichBlockType::CodeBlock: return "PCode";
            default: break;
        }
        if (block.HasParagraphGeometry() || block.paragraphFontSizePt > 0.0f || !block.paragraphFontFamily.empty()
            || block.rightToLeft) {
            return GeometryStyleFor(block);
        }
        return AlignedStyle(block.align);
    }

    // Lengths go out dot-decimal whatever the process locale.
    static std::string Pt(float value) {
        std::ostringstream out;
        out.imbue(std::locale::classic());
        out << std::fixed << std::setprecision(2) << value << "pt";
        return out.str();
    }

    // One automatic paragraph style per distinct geometry (with alignment,
    // since an automatic style cannot inherit from another automatic one).
    std::string GeometryStyleFor(const RichDocBlock& block, const std::string& parent = "Standard") {
        std::ostringstream props;
        if (const char* align = AlignValue(block.align)) props << " fo:text-align=\"" << align << "\"";
        if (block.rightToLeft) props << " style:writing-mode=\"rl-tb\"";
        if (block.leftIndentPt != 0.0f) props << " fo:margin-left=\"" << Pt(block.leftIndentPt) << "\"";
        if (block.rightIndentPt != 0.0f) props << " fo:margin-right=\"" << Pt(block.rightIndentPt) << "\"";
        if (block.firstLineIndentPt != 0.0f) props << " fo:text-indent=\"" << Pt(block.firstLineIndentPt) << "\"";
        if (block.spaceBeforePt >= 0.0f) props << " fo:margin-top=\"" << Pt(block.spaceBeforePt) << "\"";
        if (block.spaceAfterPt >= 0.0f) props << " fo:margin-bottom=\"" << Pt(block.spaceAfterPt) << "\"";
        if (block.lineHeightPt > 0.0f) {
            props << (block.lineHeightAtLeast ? " style:line-height-at-least=\"" : " fo:line-height=\"")
                  << Pt(block.lineHeightPt) << "\"";
        } else if (block.lineSpacing > 0.0f) {
            props << " fo:line-height=\"" << std::to_string(std::lround(block.lineSpacing * 100.0f)) << "%\"";
        }
        if (block.HasParagraphFrame()) {
            props << " fo:border-top=\"" << BorderValue(block.paragraphBorderTop) << "\""
                  << " fo:border-bottom=\"" << BorderValue(block.paragraphBorderBottom) << "\""
                  << " fo:border-left=\"" << BorderValue(block.paragraphBorderLeft) << "\""
                  << " fo:border-right=\"" << BorderValue(block.paragraphBorderRight) << "\""
                  << " fo:padding=\"0.05cm\"";
            if (!block.paragraphBackground.empty()) {
                props << " fo:background-color=\"" << EscapeXml(block.paragraphBackground) << "\"";
            }
        }
        std::ostringstream tabs;
        if (!block.tabStops.empty()) {
            // Written relative to the indent, Writer's default reading.
            tabs << "<style:tab-stops>";
            for (const RichTabStop& stop : block.tabStops) {
                tabs << "<style:tab-stop style:position=\"" << Pt(stop.positionPt - block.leftIndentPt) << "\"";
                switch (stop.kind) {
                    case RichTabKind::Center: tabs << " style:type=\"center\""; break;
                    case RichTabKind::Right: tabs << " style:type=\"right\""; break;
                    case RichTabKind::Decimal: tabs << " style:type=\"char\" style:char=\".\""; break;
                    default: break;
                }
                tabs << "/>";
            }
            tabs << "</style:tab-stops>";
        }
        std::ostringstream font;
        if (block.paragraphFontSizePt > 0.0f) font << " fo:font-size=\"" << Pt(block.paragraphFontSizePt) << "\"";
        if (!block.paragraphFontFamily.empty()) font << " fo:font-family=\"" << EscapeXml(block.paragraphFontFamily) << "\"";
        const std::string textProps = font.str().empty() ? "" : "<style:text-properties" + font.str() + "/>";
        const std::string key = parent + "|" + props.str() + tabs.str() + textProps;
        auto it = geometryStyleNames_.find(key);
        if (it != geometryStyleNames_.end()) return it->second;
        const std::string name = "PG" + std::to_string(geometryStyleNames_.size() + 1);
        geometryStyleNames_[key] = name;
        geometryStyles_ += "<style:style style:name=\"" + name + "\" style:family=\"paragraph\" "
                           "style:parent-style-name=\"" + EscapeXml(parent) + "\"><style:paragraph-properties"
                         + props.str() + (tabs.str().empty() ? "/>" : ">" + tabs.str() + "</style:paragraph-properties>")
                         + textProps + "</style:style>\n";
        return name;
    }

    static std::string BorderValue(const RichBorder& border) {
        if (!border.IsVisible()) return "none";
        return Pt(border.widthPt) + " " + RichBorderStyleOdfName(border.style) + " "
             + (border.color.empty() ? std::string("#000000") : border.color);
    }

    // The automatic table-cell style for a cell's frame and fill, shared by
    // every cell that looks the same. A table without document borders gets
    // the thin grid the view draws for it.
    std::string CellStyleFor(const RichTableCell& cell, bool fromDocument) {
        std::ostringstream props;
        if (!fromDocument) {
            props << " fo:border=\"0.50pt solid #000000\"";
        } else {
            props << " fo:border-top=\"" << BorderValue(cell.borderTop) << "\""
                  << " fo:border-bottom=\"" << BorderValue(cell.borderBottom) << "\""
                  << " fo:border-left=\"" << BorderValue(cell.borderLeft) << "\""
                  << " fo:border-right=\"" << BorderValue(cell.borderRight) << "\"";
            if (!cell.backgroundColor.empty()) {
                props << " fo:background-color=\"" << EscapeXml(cell.backgroundColor) << "\"";
            }
            if (cell.verticalAlign != RichVerticalAlign::Top) {
                props << " style:vertical-align=\""
                      << (cell.verticalAlign == RichVerticalAlign::Middle ? "middle" : "bottom") << "\"";
            }
        }
        // The cell's own padding where it has one, else a small default.
        const float paddings[4] = {cell.paddingTopPt, cell.paddingBottomPt, cell.paddingLeftPt, cell.paddingRightPt};
        const char* paddingNames[4] = {"fo:padding-top", "fo:padding-bottom", "fo:padding-left", "fo:padding-right"};
        for (int i = 0; i < 4; ++i) {
            props << " " << paddingNames[i] << "=\"" << (paddings[i] >= 0.0f ? Pt(paddings[i]) : std::string("0.05cm")) << "\"";
        }
        const std::string key = props.str();
        auto it = cellStyleNames_.find(key);
        if (it != cellStyleNames_.end()) return it->second;
        const std::string name = "Cell" + std::to_string(cellStyleNames_.size() + 1);
        cellStyleNames_[key] = name;
        cellStyles_ += "<style:style style:name=\"" + name + "\" style:family=\"table-cell\">"
                       "<style:table-cell-properties" + key + "/></style:style>\n";
        return name;
    }

    static const char* AlignValue(RichTextAlign align) {
        switch (align) {
            case RichTextAlign::Center: return "center";
            case RichTextAlign::Right: return "end";
            case RichTextAlign::Justify: return "justify";
            case RichTextAlign::Left: return "start";
            default: return nullptr;
        }
    }

    static const char* AlignedStyle(RichTextAlign align) {
        switch (align) {
            case RichTextAlign::Center: return "PCenter";
            case RichTextAlign::Right: return "PRight";
            case RichTextAlign::Justify: return "PJustify";
            default: return "Standard";
        }
    }

    static void WriteBookmarks(std::ostringstream& xml, const RichDocBlock& block) {
        for (const std::string& name : block.bookmarks) xml << "<text:bookmark text:name=\"" << EscapeXml(name) << "\"/>";
    }

    void WriteParagraph(std::ostringstream& xml, const RichDocBlock& block) {
        xml << "<text:p text:style-name=\"" << ParagraphStyleFor(block) << "\">";
        WriteBookmarks(xml, block);
        WriteRuns(xml, block.runs);
        xml << "</text:p>\n";
    }

    void WriteImage(std::ostringstream& xml, const RichDocBlock& block) {
        if (block.mediaIndex < 0 || block.mediaIndex >= static_cast<int>(doc_->media.size())) {
            return;
        }
        float widthPt = block.imageWidthPt;
        float heightPt = block.imageHeightPt;
        if (widthPt <= 0 || heightPt <= 0) {
            int w = 0, h = 0;
            if (UCRichDocument::SniffImagePixelSize(doc_->media[block.mediaIndex].data, w, h)) {
                widthPt = static_cast<float>(w) * 72.0f / 96.0f;
                heightPt = static_cast<float>(h) * 72.0f / 96.0f;
            } else {
                widthPt = 288.0f;   // 4in x 3in placeholder for unsniffable formats
                heightPt = 216.0f;
            }
        }
        xml << "<text:p text:style-name=\"Standard\">"
            << "<draw:frame draw:name=\"" << EscapeXml(block.imageAltText.empty()
                    ? ("Image" + std::to_string(block.mediaIndex + 1)) : block.imageAltText)
            << "\" text:anchor-type=\"as-char\" svg:width=\"" << widthPt
            << "pt\" svg:height=\"" << heightPt << "pt\">"
            << "<draw:image xlink:href=\"" << PictureHref(block.mediaIndex)
            << "\" xlink:type=\"simple\" xlink:show=\"embed\" xlink:actuate=\"onLoad\"/>"
            << "</draw:frame></text:p>\n";
    }

    void WriteTable(std::ostringstream& xml, const RichDocBlock& block, int tableNumber) {
        size_t columnCount = 0;
        for (const auto& row : block.tableRows) {
            size_t width = 0;
            for (const auto& cell : row.cells) width += std::max(1, cell.columnSpan);
            columnCount = std::max(columnCount, width);
        }
        if (columnCount == 0) return;
        xml << "<table:table table:name=\"Table" << tableNumber << "\"";
        // Width and placement, when the table has its own.
        if (block.tableWidthPt > 0.0f || block.tableWidthPercent > 0.0f) {
            const std::string name = "Table" + std::to_string(tableNumber);
            std::ostringstream props;
            if (block.tableWidthPt > 0.0f) props << " style:width=\"" << Pt(block.tableWidthPt) << "\"";
            else props << " style:rel-width=\"" << std::to_string(std::lround(block.tableWidthPercent)) << "%\"";
            const char* align = block.tableAlign == RichTextAlign::Center ? "center"
                              : block.tableAlign == RichTextAlign::Right ? "right" : "left";
            props << " table:align=\"" << align << "\"";
            if (block.tableIndentPt > 0.0f && std::string(align) == "left") {
                props << " fo:margin-left=\"" << Pt(block.tableIndentPt) << "\"";
            }
            columnStyles_ += "<style:style style:name=\"" + name + "\" style:family=\"table\">"
                             "<style:table-properties" + props.str() + "/></style:style>\n";
            xml << " table:style-name=\"" << name << "\"";
        }
        xml << ">\n";
        // Known proportions become relative column widths ("1234*"), each in
        // an automatic column style; otherwise the columns share equally.
        const std::vector<float>& widths = block.tableColumnWidths;
        float totalWidth = 0.0f;
        for (float w : widths) totalWidth += std::max(0.0f, w);
        if (widths.size() == columnCount && totalWidth > 0.0f) {
            for (size_t c = 0; c < columnCount; ++c) {
                const std::string name = "Table" + std::to_string(tableNumber) + ".C" + std::to_string(c + 1);
                const long relative = std::max(1L, std::lround(10000.0f * widths[c] / totalWidth));
                columnStyles_ += "<style:style style:name=\"" + name + "\" style:family=\"table-column\">"
                                 "<style:table-column-properties style:rel-column-width=\""
                               + std::to_string(relative) + "*\"/></style:style>\n";
                xml << "<table:table-column table:style-name=\"" << name << "\"/>\n";
            }
        } else {
            xml << "<table:table-column table:number-columns-repeated=\"" << columnCount << "\"/>\n";
        }
        // Walk the grid rather than the cell list: a cell spanning rows covers
        // grid columns in the rows below it, and those columns carry a
        // <table:covered-table-cell/> instead of a cell of their own. Tracking
        // that is the only way a row span survives the write — the reader has
        // always recovered them, so dropping them here lost the merge silently.
        std::vector<int> rowSpanRemaining(columnCount, 0);
        for (const auto& row : block.tableRows) {
            if (row.header) xml << "<table:table-header-rows>";
            xml << "<table:table-row>";

            size_t cellIndex = 0;
            for (size_t col = 0; col < columnCount; ) {
                if (rowSpanRemaining[col] > 0) {
                    xml << "<table:covered-table-cell/>";
                    rowSpanRemaining[col]--;
                    col++;
                    continue;
                }
                if (cellIndex >= row.cells.size()) break;   // a short row
                const RichTableCell& cell = row.cells[cellIndex++];
                const int columnSpan = std::max(1, cell.columnSpan);
                const int rowSpan = std::max(1, cell.rowSpan);

                xml << "<table:table-cell table:style-name=\""
                    << CellStyleFor(cell, block.tableBordersFromDocument) << "\" office:value-type=\"string\"";
                if (columnSpan > 1) {
                    xml << " table:number-columns-spanned=\"" << columnSpan << "\"";
                }
                if (rowSpan > 1) {
                    xml << " table:number-rows-spanned=\"" << rowSpan << "\"";
                }
                xml << "><text:p text:style-name=\"" << AlignedStyle(cell.align) << "\">";
                WriteRuns(xml, cell.runs);
                xml << "</text:p></table:table-cell>";
                for (int s = 1; s < columnSpan; ++s) {
                    xml << "<table:covered-table-cell/>";
                }
                if (rowSpan > 1) {
                    for (size_t c = col; c < col + static_cast<size_t>(columnSpan)
                                         && c < columnCount; c++) {
                        rowSpanRemaining[c] = rowSpan - 1;
                    }
                }
                col += static_cast<size_t>(columnSpan);
            }

            xml << "</table:table-row>";
            if (row.header) xml << "</table:table-header-rows>";
            xml << "\n";
        }
        xml << "</table:table>\n";
    }

    // Emits consecutive ListItem blocks from [begin,end) as one text:list
    // (with nested sub-lists). Stops at a shallower item or when the
    // ordered/unordered kind flips at this level — the caller then starts a
    // new list. Returns the index of the first unconsumed block.
    // An ODF number level from a Word-style template: "(%1)" -> prefix "(",
    // suffix ")"; "%1.%2." -> display-levels 2, suffix ".". A template that
    // is not prefix + consecutive levels ending at this one + suffix keeps
    // only its own number.
    static void TemplateToOdf(const std::string& templ, int ownLevel, std::string& prefix,
                              std::string& suffix, int& displayLevels) {
        prefix.clear();
        suffix = ".";
        displayLevels = 1;
        if (templ.empty()) return;
        const size_t first = templ.find('%');
        const size_t own = templ.rfind("%" + std::to_string(ownLevel));
        if (first == std::string::npos || own == std::string::npos || own < first) return;
        const int firstLevel = templ[first + 1] - '0';
        if (firstLevel < 1 || firstLevel > ownLevel) return;
        prefix = templ.substr(0, first);
        suffix = templ.substr(own + 2);
        displayLevels = ownLevel - firstLevel + 1;
    }

    static const char* OdfNumFormat(RichNumberFormat format) {
        switch (format) {
            case RichNumberFormat::LowerLetter: return "a";
            case RichNumberFormat::UpperLetter: return "A";
            case RichNumberFormat::LowerRoman: return "i";
            case RichNumberFormat::UpperRoman: return "I";
            case RichNumberFormat::NoNumber: return "";
            default: return "1";
        }
    }

    // A list style for the list starting at `begin` when its items carry the
    // document's own labels or bullets; "" when the plain LNum/LBullet do.
    // Each level takes the label of the first item at that level.
    std::string CustomListStyleFor(size_t begin, size_t end, int level) {
        const RichDocBlock* byLevel[10] = {};
        bool custom = false;
        for (size_t j = begin; j < end; ++j) {
            const RichDocBlock& item = (*blocks_)[j];
            if (item.type != RichBlockType::ListItem || item.listLevel < level) break;
            if (j > begin && WordFormatInternal::StartsNewList((*blocks_), j)) break;
            const int l = std::clamp(item.listLevel, 0, 9);
            if (!byLevel[l]) byLevel[l] = &item;
            custom = custom || !item.numberTemplate.empty() || !item.bulletText.empty()
                     || (item.orderedList && item.numberFormat != RichNumberFormat::Decimal);
        }
        if (!custom) return "";
        const std::string name = "LDoc" + std::to_string(++customListStyleCount_);
        std::ostringstream xml;
        xml << "<text:list-style style:name=\"" << name << "\">\n";
        for (int l = 0; l < 10; ++l) {
            const RichDocBlock* item = byLevel[l];
            const std::string props = "<style:list-level-properties text:space-before=\""
                                    + Pt(18.0f * static_cast<float>(l)) + "\" text:min-label-width=\"18.00pt\"/>";
            if (item && item->orderedList) {
                std::string prefix, suffix;
                int shown = 1;
                TemplateToOdf(item->numberTemplate, l + 1, prefix, suffix, shown);
                xml << "<text:list-level-style-number text:level=\"" << (l + 1) << "\" style:num-format=\""
                    << OdfNumFormat(item->numberFormat) << "\"";
                if (!prefix.empty()) xml << " style:num-prefix=\"" << EscapeXml(prefix) << "\"";
                if (!suffix.empty()) xml << " style:num-suffix=\"" << EscapeXml(suffix) << "\"";
                if (shown > 1) xml << " text:display-levels=\"" << shown << "\"";
                xml << ">" << props << "</text:list-level-style-number>\n";
            } else {
                const std::string bullet = item && !item->bulletText.empty() ? item->bulletText : "\xE2\x80\xA2";
                xml << "<text:list-level-style-bullet text:level=\"" << (l + 1) << "\" text:bullet-char=\""
                    << EscapeXml(bullet) << "\">" << props << "</text:list-level-style-bullet>\n";
            }
        }
        xml << "</text:list-style>\n";
        listStyles_ += xml.str();
        return name;
    }

    size_t WriteListRun(std::ostringstream& xml, size_t begin, size_t end, int level,
                        std::string customStyle = "", bool topLevel = true) {
        bool ordered = (*blocks_)[begin].orderedList;
        for (size_t j = begin; j < end; ++j) {
            if ((*blocks_)[j].listLevel <= level) {
                ordered = (*blocks_)[j].orderedList;
                break;
            }
        }
        if (topLevel) customStyle = CustomListStyleFor(begin, end, level);
        xml << "<text:list text:style-name=\""
            << (!customStyle.empty() ? customStyle : (ordered ? "LNum" : "LBullet")) << "\">\n";
        size_t i = begin;
        bool itemOpen = false;
        while (i < end) {
            const RichDocBlock& item = (*blocks_)[i];
            if (item.listLevel < level) break;
            if (item.listLevel == level) {
                if (item.orderedList != ordered) break;
                // An item that needs its own level definition starts a new list.
                if (i != begin && WordFormatInternal::StartsNewList((*blocks_), i)) break;
                if (itemOpen) xml << "</text:list-item>\n";
                xml << "<text:list-item";
                // A number the item carries (a list starting at N, or running
                // on past an interruption) is the item's start value.
                if (item.orderedList && item.listStartNumber > 0) {
                    xml << " text:start-value=\"" << std::to_string(item.listStartNumber) << "\"";
                }
                xml << "><text:p text:style-name=\"" << ParagraphStyleFor(item) << "\">";
                WriteRuns(xml, item.runs);
                xml << "</text:p>";
                itemOpen = true;
                ++i;
            } else {
                // Deeper item: nest a sub-list inside the current list item.
                if (!itemOpen) {
                    xml << "<text:list-item>";
                    itemOpen = true;
                }
                i = WriteListRun(xml, i, end, level + 1, customStyle, false);
            }
        }
        if (itemOpen) xml << "</text:list-item>\n";
        xml << "</text:list>\n";
        return i;
    }

    // The blocks as ODF text elements.
    // A section in columns, as a text:section with a section style. Only
    // the body has sections.
    std::string sectionStylesXml_;
    int sectionCount_ = 0;
    bool sectionOpen_ = false;

    void UpdateSection(std::ostringstream& body, const std::vector<RichDocBlock>& blocks, size_t i) {
        if (&blocks != &doc_->blocks) return;
        if (i > 0 && !blocks[i].sectionStart) return;
        if (sectionOpen_) {
            body << "</text:section>\n";
            sectionOpen_ = false;
        }
        const RichSectionSetup& setup = i == 0 ? (blocks[0].sectionStart ? blocks[0].section : doc_->firstSection)
                                               : blocks[i].section;
        if (i > 0 && setup.newPage) body << "<text:p text:style-name=\"PPageBreak\"/>\n";
        if (setup.columns <= 1) return;
        const std::string name = "Sect" + std::to_string(++sectionCount_);
        sectionStylesXml_ += "<style:style style:name=\"" + name + "\" style:family=\"section\">"
                             "<style:section-properties text:dont-balance-text-columns=\"true\">"
                             "<style:columns fo:column-count=\"" + std::to_string(setup.columns)
                           + "\" fo:column-gap=\"" + Pt(setup.columnGapPt) + "\"/></style:section-properties></style:style>\n";
        body << "<text:section text:style-name=\"" << name << "\" text:name=\"Section" << sectionCount_ << "\">\n";
        sectionOpen_ = true;
    }

    std::string WriteBlocks(const std::vector<RichDocBlock>& blocks) {
        const std::vector<RichDocBlock>* savedBlocks = blocks_;
        blocks_ = &blocks;
        std::ostringstream body;
        size_t i = 0;
        while (i < blocks.size()) {
            UpdateSection(body, blocks, i);
            const RichDocBlock& block = blocks[i];
            if (block.tocLevel > 0 && block.type == RichBlockType::Paragraph) {
                // A table of contents: its entries as the index's text.
                size_t end = i;
                int deepest = 1;
                while (end < blocks.size() && blocks[end].tocLevel > 0 && blocks[end].type == RichBlockType::Paragraph) {
                    deepest = std::max(deepest, blocks[end].tocLevel);
                    ++end;
                }
                body << "<text:table-of-content text:name=\"Table of Contents" << ++tocCount_
                     << "\"><text:table-of-content-source text:outline-level=\"" << deepest
                     << "\"/><text:index-body>\n";
                for (; i < end; ++i) WriteParagraph(body, blocks[i]);
                body << "</text:index-body></text:table-of-content>\n";
                continue;
            }
            // A list or a table of contents is written as one: a section
            // starting inside it starts after it.
            switch (block.type) {
                case RichBlockType::Heading:
                    if (!block.styleId.empty() && doc_->FindStyle(block.styleId)) {
                        body << "<text:h text:style-name=\"" << EscapeXml(ParagraphStyleFor(block)) << "\"";
                    } else {
                        body << "<text:h text:style-name=\"Heading_20_" << std::clamp(block.headingLevel, 1, 6) << "\"";
                    }
                    body << " text:outline-level=\"" << std::clamp(block.headingLevel, 1, 6) << "\">";
                    WriteBookmarks(body, block);
                    WriteRuns(body, block.runs);
                    body << "</text:h>\n";
                    ++i;
                    break;
                case RichBlockType::ListItem: {
                    size_t end = i;
                    while (end < blocks.size()
                           && blocks[end].type == RichBlockType::ListItem) {
                        ++end;
                    }
                    size_t pos = i;
                    while (pos < end) {
                        pos = WriteListRun(body, pos, end, blocks[pos].listLevel);
                    }
                    i = end;
                    break;
                }
                case RichBlockType::CodeBlock: {
                    RichDocBlock codeBlock = block;
                    // Collapse the per-line runs into text with line breaks; the
                    // PCode paragraph style carries the monospace font.
                    RichTextRun run;
                    run.text = UCRichDocument::ConcatenateRunText(block.runs);
                    codeBlock.runs = {run};
                    WriteParagraph(body, codeBlock);
                    ++i;
                    break;
                }
                case RichBlockType::Table:
                    WriteTable(body, block, ++tableCount_);
                    ++i;
                    break;
                case RichBlockType::Image:
                    WriteImage(body, block);
                    ++i;
                    break;
                case RichBlockType::HorizontalRule:
                    body << "<text:p text:style-name=\"PRule\"/>\n";
                    ++i;
                    break;
                case RichBlockType::PageBreak:
                    body << "<text:p text:style-name=\"PPageBreak\"/>\n";
                    ++i;
                    break;
                case RichBlockType::MathBlock:
                    // No MathML writer: the formula source travels as a centred
                    // "$$...$$" paragraph (typeset again after a re-import).
                    WriteParagraph(body, MathBlockAsParagraph(block));
                    ++i;
                    break;
                case RichBlockType::Paragraph:
                case RichBlockType::BlockQuote:
                default:
                    WriteParagraph(body, block);
                    ++i;
                    break;
            }
        }
        if (&blocks == &doc_->blocks && sectionOpen_) {
            body << "</text:section>\n";
            sectionOpen_ = false;
        }
        blocks_ = savedBlocks;
        return body.str();
    }

    std::string BuildContentXml(const std::string& body) const {
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            << "<office:document-content "
            << "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
            << "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
            << "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" "
            << "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
            << "xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" "
            << "xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" "
            << "xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" "
            << "xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
            << "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" "
            << "xmlns:loext=\"urn:org:documentfoundation:names:experimental:office:xmlns:loext:1.0\" "
            << "office:version=\"1.3\">\n"
            << "<office:automatic-styles>\n"
            << AutomaticStylesXml()
            << "</office:automatic-styles>\n"
            << "<office:body>\n<office:text>\n"
            << (!changedRegions_.str().empty() ? "<text:tracked-changes>" + changedRegions_.str() + "</text:tracked-changes>\n"
                                            : std::string())
            << body
            << "</office:text>\n</office:body>\n</office:document-content>\n";
        return xml.str();
    }

    // The automatic styles the written blocks use. content.xml and
    // styles.xml each declare them, since the body's and the headers' and
    // footers' styles resolve in their own file.
    std::string AutomaticStylesXml() const {
        std::ostringstream xml;
        for (size_t s = 0; s < textStyles_.size(); ++s) {
            const RichTextRun& t = textStyles_[s];
            xml << "<style:style style:name=\"T" << (s + 1) << "\" style:family=\"text\"";
            // A run with a character style: its automatic style is based on it.
            if (!t.characterStyleId.empty() && doc_->FindStyle(t.characterStyleId)) {
                xml << " style:parent-style-name=\"" << EscapeXml(StyleIdToOdfName(t.characterStyleId)) << "\"";
            }
            xml << "><style:text-properties";
            if (t.bold) xml << " fo:font-weight=\"bold\"";
            if (t.italic) xml << " fo:font-style=\"italic\"";
            if (t.underline) xml << " style:text-underline-style=\"solid\"";
            if (t.strikethrough) xml << " style:text-line-through-style=\"solid\"";
            if (t.subscript) xml << " style:text-position=\"sub 58%\"";
            if (t.superscript) xml << " style:text-position=\"super 58%\"";
            if (t.code) xml << " style:font-name=\"Courier New\" fo:font-family=\"'Courier New'\"";
            else if (!t.fontFamily.empty())
                xml << " fo:font-family=\"" << EscapeXml(t.fontFamily) << "\"";
            if (!t.color.empty()) xml << " fo:color=\"" << EscapeXml(t.color) << "\"";
            if (!t.highlightColor.empty()) xml << " fo:background-color=\"" << EscapeXml(t.highlightColor) << "\"";
            if (t.fontSizePt > 0) xml << " fo:font-size=\"" << t.fontSizePt << "pt\"";
            if (t.letterSpacingPt)
                xml << " fo:letter-spacing=\"" << FormatFloatClassic(*t.letterSpacingPt) << "pt\"";
            xml << "/></style:style>\n";
        }

        xml << columnStyles_ << cellStyles_ << geometryStyles_ << graphicStylesXml_ << sectionStylesXml_;
        xml << "<style:style style:name=\"PCenter\" style:family=\"paragraph\" "
               "style:parent-style-name=\"Standard\">"
               "<style:paragraph-properties fo:text-align=\"center\"/></style:style>\n"
            << "<style:style style:name=\"PRight\" style:family=\"paragraph\" "
               "style:parent-style-name=\"Standard\">"
               "<style:paragraph-properties fo:text-align=\"end\"/></style:style>\n"
            << "<style:style style:name=\"PJustify\" style:family=\"paragraph\" "
               "style:parent-style-name=\"Standard\">"
               "<style:paragraph-properties fo:text-align=\"justify\"/></style:style>\n"
            << "<style:style style:name=\"PQuote\" style:family=\"paragraph\" "
               "style:parent-style-name=\"Standard\">"
               "<style:paragraph-properties fo:margin-left=\"1cm\" fo:margin-right=\"1cm\"/>"
               "<style:text-properties fo:font-style=\"italic\"/></style:style>\n"
            << "<style:style style:name=\"PCode\" style:family=\"paragraph\" "
               "style:parent-style-name=\"Standard\">"
               "<style:paragraph-properties fo:margin-left=\"0.5cm\"/>"
               "<style:text-properties style:font-name=\"Courier New\" "
               "fo:font-family=\"'Courier New'\"/></style:style>\n"
            << "<style:style style:name=\"PRule\" style:family=\"paragraph\" "
               "style:parent-style-name=\"Standard\">"
               "<style:paragraph-properties fo:border-bottom=\"0.5pt solid #808080\" "
               "fo:margin-top=\"0.2cm\" fo:margin-bottom=\"0.2cm\"/></style:style>\n"
            << "<style:style style:name=\"PPageBreak\" style:family=\"paragraph\" "
               "style:parent-style-name=\"Standard\">"
               "<style:paragraph-properties fo:break-before=\"page\"/></style:style>\n"
            << "<text:list-style style:name=\"LBullet\">\n";
        auto levelProperties = [](int level) {
            std::ostringstream props;
            props << "<style:list-level-properties text:space-before=\""
                  << (0.64 * (level - 1)) << "cm\" text:min-label-width=\"0.64cm\"/>";
            return props.str();
        };
        for (int level = 1; level <= 10; ++level) {
            xml << "<text:list-level-style-bullet text:level=\"" << level
                << "\" text:bullet-char=\"\xE2\x80\xA2\">" << levelProperties(level)
                << "</text:list-level-style-bullet>\n";
        }
        xml << "</text:list-style>\n<text:list-style style:name=\"LNum\">\n";
        for (int level = 1; level <= 10; ++level) {
            xml << "<text:list-level-style-number text:level=\"" << level
                << "\" style:num-format=\"1\" style:num-suffix=\".\">" << levelProperties(level)
                << "</text:list-level-style-number>\n";
        }
        xml << "</text:list-style>\n" << listStyles_;
        return xml.str();
    }

    // The page layout and the master page with its headers and footers. The
    // model's top margin is where the body starts; ODF's is where the
    // header starts, the header's height and spacing following it.
    std::string BuildMasterStyles() {
        const UCRichDocument& doc = *doc_;
        const RichPageFurniture& later = doc.pageFurniture;
        const RichPageFurniture& first = doc.firstPageDiffers ? doc.firstPageFurniture : doc.pageFurniture;
        const bool hasHeader = !later.header.empty() || !first.header.empty();
        const bool hasFooter = !later.footer.empty() || !first.footer.empty();
        const RichPageSetup& page = doc.page;
        if (!page.HasPage() && !hasHeader && !hasFooter) return "";

        std::ostringstream props;
        std::ostringstream headerStyle;
        std::ostringstream footerStyle;
        if (page.HasPage()) {
            props << " fo:page-width=\"" << Pt(page.widthPt) << "\" fo:page-height=\"" << Pt(page.heightPt) << "\""
                  << " style:print-orientation=\"" << (page.widthPt > page.heightPt ? "landscape" : "portrait") << "\""
                  << " fo:margin-left=\"" << Pt(page.marginLeftPt) << "\" fo:margin-right=\"" << Pt(page.marginRightPt) << "\"";
        }
        // A header's room is the space between where it starts and where the
        // body does: its minimum height, spacing to the body included.
        auto room = [](float margin, float edge, std::ostringstream& out, const char* tag, const char* spacingAttr) {
            const float start = (edge > 0.0f && edge < margin) ? edge : margin * 0.5f;
            const float total = std::max(0.0f, margin - start);
            const float spacing = std::min(7.0f, total * 0.5f);
            out << "<style:" << tag << "><style:header-footer-properties fo:min-height=\"" << Pt(total)
                << "\" " << spacingAttr << "=\"" << Pt(spacing) << "\" style:dynamic-spacing=\"false\"/></style:" << tag << ">";
            return start;
        };
        float top = page.marginTopPt, bottom = page.marginBottomPt;
        if (hasHeader) top = room(page.marginTopPt, page.headerTopPt, headerStyle, "header-style", "fo:margin-bottom");
        if (hasFooter) bottom = room(page.marginBottomPt, page.footerBottomPt, footerStyle, "footer-style", "fo:margin-top");
        if (page.HasPage()) props << " fo:margin-top=\"" << Pt(top) << "\" fo:margin-bottom=\"" << Pt(bottom) << "\"";
        pageLayout_ = "<style:page-layout style:name=\"PL1\"><style:page-layout-properties" + props.str() + "/>"
                    + headerStyle.str() + footerStyle.str() + "</style:page-layout>\n";

        std::ostringstream xml;
        xml << "<office:master-styles>\n<style:master-page style:name=\"Standard\" style:page-layout-name=\"PL1\">";
        if (hasHeader) {
            xml << "<style:header>" << WriteBlocks(later.header) << "</style:header>";
            if (doc.firstPageDiffers) xml << "<style:header-first>" << WriteBlocks(first.header) << "</style:header-first>";
        }
        if (hasFooter) {
            xml << "<style:footer>" << WriteBlocks(later.footer) << "</style:footer>";
            if (doc.firstPageDiffers) xml << "<style:footer-first>" << WriteBlocks(first.footer) << "</style:footer-first>";
        }
        xml << "</style:master-page>\n</office:master-styles>\n";
        return xml.str();
    }

    std::string BuildStylesXml(const std::string& masterStyles) const {
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            << "<office:document-styles "
            << "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
            << "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
            << "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
            << "xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" "
            << "xmlns:table=\"urn:oasis:names:tc:opendocument:xmlns:table:1.0\" "
            << "xmlns:draw=\"urn:oasis:names:tc:opendocument:xmlns:drawing:1.0\" "
            << "xmlns:svg=\"urn:oasis:names:tc:opendocument:xmlns:svg-compatible:1.0\" "
            << "xmlns:xlink=\"http://www.w3.org/1999/xlink\" "
            << "office:version=\"1.3\">\n<office:styles>\n";
        if (doc_->defaultTabStopPt > 0.0f) {
            xml << "<style:default-style style:family=\"paragraph\"><style:paragraph-properties "
                   "style:tab-stop-distance=\"" << Pt(doc_->defaultTabStopPt) << "\"/></style:default-style>\n";
        }
        // Paragraphs that state no spacing (a document built from Markdown)
        // keep a small gap below, as the view gives them.
        if (!doc_->FindStyle("Normal")) {
            xml << "<style:style style:name=\"Standard\" style:family=\"paragraph\">"
                   "<style:paragraph-properties fo:margin-bottom=\"6pt\"/></style:style>\n";
        }
        static const float headingSizesPt[6] = {18.0f, 16.0f, 14.0f, 12.0f, 11.0f, 10.5f};
        for (int level = 1; level <= 6; ++level) {
            if (doc_->FindStyle("Heading" + std::to_string(level))) continue;
            xml << "<style:style style:name=\"Heading_20_" << level
                << "\" style:display-name=\"Heading " << level
                << "\" style:family=\"paragraph\" style:parent-style-name=\"Standard\" "
                << "style:default-outline-level=\"" << level << "\">"
                << "<style:paragraph-properties fo:margin-top=\"0.3cm\" "
                   "fo:margin-bottom=\"0.15cm\" fo:keep-with-next=\"always\"/>"
                << "<style:text-properties fo:font-weight=\"bold\" fo:font-size=\""
                << headingSizesPt[level - 1] << "pt\"/></style:style>\n";
        }
        for (const RichStyle& style : doc_->styles) WriteNamedStyle(xml, style);
        // What drawn shapes' lines refer to: an arrowhead, a dash and a dot.
        if (std::any_of(doc_->frames.begin(), doc_->frames.end(), [](const RichTextFrame& f) { return f.IsDrawing(); })) {
            xml << "<draw:marker draw:name=\"UC_Arrow\" svg:viewBox=\"0 0 20 30\" svg:d=\"M10 0l-10 30h20z\"/>\n"
                   "<draw:stroke-dash draw:name=\"UC_Dash\" draw:style=\"rect\" draw:dots1=\"1\" draw:dots1-length=\"400%\""
                   " draw:distance=\"300%\"/>\n"
                   "<draw:stroke-dash draw:name=\"UC_Dot\" draw:style=\"rect\" draw:dots1=\"1\" draw:dots1-length=\"100%\""
                   " draw:distance=\"200%\"/>\n";
            xml << gradientDefinitions_;
        }
        xml << "</office:styles>\n";
        if (!masterStyles.empty()) {
            xml << "<office:automatic-styles>\n" << AutomaticStylesXml() << pageLayout_
                << "</office:automatic-styles>\n" << masterStyles;
        }
        xml << "</office:document-styles>\n";
        return xml.str();
    }

    // A named style in office:styles, with the ODF name of its id.
    void WriteNamedStyle(std::ostringstream& xml, const RichStyle& style) const {
        const bool paragraph = style.kind == RichStyle::Kind::Paragraph;
        xml << "<style:style style:name=\"" << EscapeXml(StyleIdToOdfName(style.id)) << "\"";
        if (!style.name.empty() && style.name != style.id) xml << " style:display-name=\"" << EscapeXml(style.name) << "\"";
        xml << " style:family=\"" << (paragraph ? "paragraph" : "text") << "\"";
        if (!style.basedOn.empty()) xml << " style:parent-style-name=\"" << EscapeXml(StyleIdToOdfName(style.basedOn)) << "\"";
        if (paragraph && !style.nextStyle.empty()) {
            xml << " style:next-style-name=\"" << EscapeXml(StyleIdToOdfName(style.nextStyle)) << "\"";
        }
        if (paragraph && style.paragraph.headingLevel && *style.paragraph.headingLevel > 0) {
            xml << " style:default-outline-level=\"" << *style.paragraph.headingLevel << "\"";
        }
        xml << ">";
        if (paragraph && !style.paragraph.IsEmpty()) {
            const RichStyleParagraph& p = style.paragraph;
            xml << "<style:paragraph-properties";
            if (p.align) {
                if (const char* align = AlignValue(*p.align)) xml << " fo:text-align=\"" << align << "\"";
            }
            if (p.leftIndentPt) xml << " fo:margin-left=\"" << Pt(*p.leftIndentPt) << "\"";
            if (p.rightIndentPt) xml << " fo:margin-right=\"" << Pt(*p.rightIndentPt) << "\"";
            if (p.firstLineIndentPt) xml << " fo:text-indent=\"" << Pt(*p.firstLineIndentPt) << "\"";
            if (p.spaceBeforePt) xml << " fo:margin-top=\"" << Pt(*p.spaceBeforePt) << "\"";
            if (p.spaceAfterPt) xml << " fo:margin-bottom=\"" << Pt(*p.spaceAfterPt) << "\"";
            if (p.lineSpacing) xml << " fo:line-height=\"" << std::lround(*p.lineSpacing * 100.0f) << "%\"";
            xml << "/>";
        }
        if (!style.character.IsEmpty()) {
            const RichStyleCharacter& c = style.character;
            xml << "<style:text-properties";
            if (c.bold) xml << " fo:font-weight=\"" << (*c.bold ? "bold" : "normal") << "\"";
            if (c.italic) xml << " fo:font-style=\"" << (*c.italic ? "italic" : "normal") << "\"";
            if (c.underline) xml << " style:text-underline-style=\"" << (*c.underline ? "solid" : "none") << "\"";
            if (c.strikethrough) xml << " style:text-line-through-style=\"" << (*c.strikethrough ? "solid" : "none") << "\"";
            if (c.code && *c.code) xml << " style:font-name=\"Courier New\" fo:font-family=\"'Courier New'\"";
            else if (c.fontFamily) xml << " fo:font-family=\"" << EscapeXml(*c.fontFamily) << "\"";
            if (c.color) xml << " fo:color=\"" << EscapeXml(*c.color) << "\"";
            if (c.fontSizePt && *c.fontSizePt > 0.0f) xml << " fo:font-size=\"" << Pt(*c.fontSizePt) << "\"";
            if (c.highlightColor) xml << " fo:background-color=\"" << EscapeXml(*c.highlightColor) << "\"";
            if (c.letterSpacingPt) xml << " fo:letter-spacing=\"" << Pt(*c.letterSpacingPt) << "\"";
            xml << "/>";
        }
        xml << "</style:style>\n";
    }

    std::string BuildMetaXml() const {
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            << "<office:document-meta "
            << "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
            << "xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\" "
            << "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" office:version=\"1.3\">\n"
            << "<office:meta>\n"
            << "<meta:generator>UltraCanvas</meta:generator>\n";
        if (!doc_->metadata.title.empty()) {
            xml << "<dc:title>" << EscapeXml(doc_->metadata.title) << "</dc:title>\n";
        }
        if (!doc_->metadata.author.empty()) {
            xml << "<dc:creator>" << EscapeXml(doc_->metadata.author) << "</dc:creator>\n";
        }
        if (!doc_->metadata.description.empty()) {
            xml << "<dc:description>" << EscapeXml(doc_->metadata.description)
                << "</dc:description>\n";
        }
        xml << "</office:meta>\n</office:document-meta>\n";
        return xml.str();
    }

    std::string BuildManifestXml() const {
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            << "<manifest:manifest "
            << "xmlns:manifest=\"urn:oasis:names:tc:opendocument:xmlns:manifest:1.0\" "
            << "manifest:version=\"1.3\">\n"
            << "<manifest:file-entry manifest:full-path=\"/\" "
               "manifest:media-type=\"application/vnd.oasis.opendocument.text\"/>\n"
            << "<manifest:file-entry manifest:full-path=\"content.xml\" "
               "manifest:media-type=\"text/xml\"/>\n"
            << "<manifest:file-entry manifest:full-path=\"styles.xml\" "
               "manifest:media-type=\"text/xml\"/>\n"
            << "<manifest:file-entry manifest:full-path=\"meta.xml\" "
               "manifest:media-type=\"text/xml\"/>\n";
        for (size_t i = 0; i < doc_->media.size(); ++i) {
            xml << "<manifest:file-entry manifest:full-path=\"" << PictureHref(i)
                << "\" manifest:media-type=\"" << EscapeXml(doc_->media[i].mimeType)
                << "\"/>\n";
        }
        xml << "</manifest:manifest>\n";
        return xml.str();
    }
};

} // namespace

bool UCWordDocumentIO::LoadOdt(const std::string& filePath, UCRichDocument& outDocument,
                               std::string& outError) {
    outDocument = UCRichDocument{};
    OdtReader reader;
    if (!reader.Load(filePath, outDocument, outError)) return false;
    outDocument.ReadCheckboxPrefixes();
    return true;
}

bool UCWordDocumentIO::SaveOdt(const std::string& filePath, const UCRichDocument& document,
                               std::string& outError) {
    OdtWriter writer;
    // The format has no check list: its items go out as a ballot box
    // opening their text, which is what reads back in.
    for (const RichDocBlock& block : document.blocks) {
        if (block.checkbox) return writer.Save(filePath, document.WithCheckboxesAsPrefixes(), outError);
    }
    return writer.Save(filePath, document, outError);
}

} // namespace UltraCanvas
