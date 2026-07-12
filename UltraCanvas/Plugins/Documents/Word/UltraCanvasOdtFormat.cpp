// Plugins/Documents/Word/UltraCanvasOdtFormat.cpp
// OpenDocument Text (.odt) reader and writer for UCRichDocument.
// ODT is a ZIP package (mimetype first, stored) containing content.xml,
// styles.xml, meta.xml, META-INF/manifest.xml and Pictures/*. Parsing uses
// tinyxml2 with the conventional ODF namespace prefixes (office:, text:,
// style:, fo:, draw:, table:, xlink:) — the same approach as the existing
// ODS spreadsheet loader.
// Version: 1.2.0
// Last Modified: 2026-07-12
// Author: UltraCanvas Framework

#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"
#include "UltraCanvasMathToLatex.h"
#include "UltraCanvasWordFormatInternal.h"
#include "UltraCanvasZipPackage.h"

#include "tinyxml2.h"

#include <cstring>
#include <map>
#include <sstream>

namespace UltraCanvas {

using WordFormatInternal::EscapeXml;
using WordFormatInternal::ParseLengthPt;

namespace {

// ===== ODT READING =====

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
    std::string parentStyleName;
    std::string masterPageName;                     // style:master-page-name (paragraph styles)
    RichTextAlign align = RichTextAlign::Default;   // paragraph styles only
    bool bottomBorder = false;                      // paragraph styles only
    bool pageBreakBefore = false;                   // paragraph styles only
    int headingLevel = 0;                           // derived from heading style names

    void MergeParent(const OdtTextProps& parent) {
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
        if (masterPageName.empty()) masterPageName = parent.masterPageName;
        if (align == RichTextAlign::Default) align = parent.align;
        bottomBorder = bottomBorder || parent.bottomBorder;
        pageBreakBefore = pageBreakBefore || parent.pageBreakBefore;
        if (headingLevel == 0) headingLevel = parent.headingLevel;
    }
};

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
        if (zip_.ReadEntry("styles.xml", stylesXml)
            && stylesDoc.Parse(stylesXml.c_str()) == tinyxml2::XML_SUCCESS) {
            stylesRoot = stylesDoc.FirstChildElement("office:document-styles");
            if (stylesRoot) {
                CollectStyles(stylesRoot->FirstChildElement("office:styles"));
                CollectStyles(stylesRoot->FirstChildElement("office:automatic-styles"));
                CollectListStyles(stylesRoot->FirstChildElement("office:styles"));
                CollectListStyles(stylesRoot->FirstChildElement("office:automatic-styles"));
            }
        }

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
        // master (logo, full contact block, bank-details footer) distinct from
        // the plain continuation master; pick the one actually applied to the
        // body rather than whichever master happens to be written first.
        tinyxml2::XMLElement* masterPage = ResolveMasterPage(stylesRoot, text);
        ParseMasterPageRegion(masterPage, "style:header", false);
        ParseBlockContainer(text, 0, "");
        ParseMasterPageRegion(masterPage, "style:footer", true);
        LoadMetadata();
        return true;
    }

private:
    UCZipPackageReader zip_;
    UCRichDocument* doc_ = nullptr;
    std::map<std::string, OdtTextProps> styles_;
    // list style name -> (level -> ordered?)
    std::map<std::string, std::map<int, bool>> listStyles_;

    static const char* Attr(const tinyxml2::XMLElement* e, const char* name) {
        const char* v = e->Attribute(name);
        return v ? v : "";
    }

    void CollectStyles(tinyxml2::XMLElement* container) {
        if (!container) return;
        for (auto* style = container->FirstChildElement("style:style"); style;
             style = style->NextSiblingElement("style:style")) {
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
                props.fontFamily = Attr(tp, "style:font-name");
                if (props.fontFamily.empty()) props.fontFamily = Attr(tp, "fo:font-family");
                props.fontSizePt = ParseLengthPt(Attr(tp, "fo:font-size"));
            }
            if (auto* pp = style->FirstChildElement("style:paragraph-properties")) {
                std::string align = Attr(pp, "fo:text-align");
                if (align == "center") props.align = RichTextAlign::Center;
                else if (align == "end" || align == "right") props.align = RichTextAlign::Right;
                else if (align == "justify") props.align = RichTextAlign::Justify;
                else if (align == "start" || align == "left") props.align = RichTextAlign::Left;
                std::string border = Attr(pp, "fo:border-bottom");
                props.bottomBorder = !border.empty() && border != "none";
                props.pageBreakBefore = std::string(Attr(pp, "fo:break-before")) == "page";
            }
            std::string name = Attr(style, "style:name");
            props.headingLevel = HeadingLevelFromStyleName(name);
            if (props.headingLevel == 0) {
                props.headingLevel = HeadingLevelFromStyleName(Attr(style, "style:display-name"));
            }
            if (!name.empty()) styles_[name] = props;
        }
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
                if (tag == "text:list-level-style-number") {
                    listStyles_[name][levelNum] = true;
                } else if (tag == "text:list-level-style-bullet"
                           || tag == "text:list-level-style-image") {
                    listStyles_[name][levelNum] = false;
                }
            }
        }
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
        if (props.bold == 1) run.bold = true;
        if (props.italic == 1) run.italic = true;
        if (props.underline == 1) run.underline = true;
        if (props.strike == 1) run.strikethrough = true;
        if (props.subscript == 1) run.subscript = true;
        if (props.superscript == 1) run.superscript = true;
        if (!props.color.empty()) run.color = props.color;
        if (!props.fontFamily.empty()) run.fontFamily = props.fontFamily;
        if (props.fontSizePt > 0) run.fontSizePt = props.fontSizePt;
        // A monospace family is the inline-code signal in the model.
        if (IsMonospaceFamily(props.fontFamily)) run.code = true;
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
        bool pendingLineBreak = false;
    };

    void AppendRun(InlineContext& ctx, const std::string& text, const OdtTextProps& props,
                   const std::string& linkTarget) {
        if (text.empty()) return;
        if (props.hidden == 1) return;   // hidden text must not render
        RichTextRun run;
        run.text = text;
        run.linkTarget = linkTarget;
        ApplyPropsToRun(run, props);
        run.lineBreakBefore = ctx.pendingLineBreak;
        ctx.pendingLineBreak = false;
        ctx.runs.push_back(std::move(run));
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
        RichDocBlock block;
        block.type = RichBlockType::Image;
        block.mediaIndex = mediaIndex;
        block.imageWidthPt = ParseLengthPt(Attr(frame, "svg:width"));
        block.imageHeightPt = ParseLengthPt(Attr(frame, "svg:height"));
        block.imageAltText = Attr(frame, "draw:name");
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
            ctx.textBoxes.push_back(textBox);
        }
    }

    void ParseInlineNodes(tinyxml2::XMLNode* parent, const OdtTextProps& props,
                          const std::string& linkTarget, InlineContext& ctx) {
        for (auto* node = parent->FirstChild(); node; node = node->NextSibling()) {
            if (auto* textNode = node->ToText()) {
                AppendRun(ctx, textNode->Value() ? textNode->Value() : "", props, linkTarget);
                continue;
            }
            auto* elem = node->ToElement();
            if (!elem) continue;
            std::string tag = elem->Name() ? elem->Name() : "";
            if (tag == "text:span") {
                OdtTextProps spanProps = ResolveStyle(Attr(elem, "text:style-name"));
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
            } else if (tag == "text:line-break") {
                ctx.pendingLineBreak = true;
            } else if (tag == "draw:frame") {
                ParseFrame(elem, props, linkTarget, ctx);
            } else if (tag == "text:note") {
                // Keep footnote content inline in parentheses so it is not lost.
                if (auto* noteBody = elem->FirstChildElement("text:note-body")) {
                    InlineContext noteCtx;
                    for (auto* p = noteBody->FirstChildElement("text:p"); p;
                         p = p->NextSiblingElement("text:p")) {
                        ParseInlineNodes(p, props, linkTarget, noteCtx);
                    }
                    std::string noteText = UCRichDocument::ConcatenateRunText(noteCtx.runs);
                    if (!noteText.empty()) {
                        AppendRun(ctx, " (" + noteText + ")", props, linkTarget);
                    }
                }
            } else if (tag == "text:soft-page-break" || tag == "office:annotation"
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
    void EmitParagraphBlock(tinyxml2::XMLElement* elem, RichDocBlock block) {
        std::string styleName = Attr(elem, "text:style-name");
        OdtTextProps paraProps = ResolveStyle(styleName);
        block.align = paraProps.align;

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

        InlineContext ctx;
        ParseInlineNodes(elem, runBase, "", ctx);
        block.runs = std::move(ctx.runs);

        if (paraProps.pageBreakBefore) {
            RichDocBlock pageBreak;
            pageBreak.type = RichBlockType::PageBreak;
            doc_->blocks.push_back(std::move(pageBreak));
        }

        // An empty bordered paragraph is the horizontal-rule idiom.
        if (block.type == RichBlockType::Paragraph && block.runs.empty()
            && paraProps.bottomBorder) {
            block.type = RichBlockType::HorizontalRule;
        }

        bool emptyPageBreakCarrier = paraProps.pageBreakBefore && block.runs.empty()
                                     && block.type == RichBlockType::Paragraph;
        bool onlyFrames = block.runs.empty()
                          && (!ctx.trailingImages.empty() || !ctx.textBoxes.empty());
        if (!onlyFrames && !emptyPageBreakCarrier) {
            doc_->blocks.push_back(std::move(block));
        }
        for (auto& image : ctx.trailingImages) {
            doc_->blocks.push_back(std::move(image));
        }
        for (auto* textBox : ctx.textBoxes) {
            ParseBlockContainer(textBox, 0, "");
        }
    }

    void ParseList(tinyxml2::XMLElement* list, int level, std::string listStyleName) {
        std::string ownStyle = Attr(list, "text:style-name");
        if (!ownStyle.empty()) listStyleName = ownStyle;
        bool ordered = false;
        auto styleIt = listStyles_.find(listStyleName);
        if (styleIt != listStyles_.end()) {
            auto levelIt = styleIt->second.find(level + 1);   // text:level is 1-based
            if (levelIt != styleIt->second.end()) ordered = levelIt->second;
        }
        for (auto* item = list->FirstChildElement(); item;
             item = item->NextSiblingElement()) {
            std::string tag = item->Name() ? item->Name() : "";
            if (tag != "text:list-item" && tag != "text:list-header") continue;
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
                    EmitParagraphBlock(child, std::move(block));
                }
            }
        }
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

    void ParseTable(tinyxml2::XMLElement* table) {
        RichDocBlock block;
        block.type = RichBlockType::Table;

        auto parseRow = [&](tinyxml2::XMLElement* rowElem, bool header) {
            RichTableRow row;
            row.header = header;
            for (auto* cellElem = rowElem->FirstChildElement(); cellElem;
                 cellElem = cellElem->NextSiblingElement()) {
                std::string tag = cellElem->Name() ? cellElem->Name() : "";
                if (tag == "table:covered-table-cell") continue;
                if (tag != "table:table-cell") continue;
                RichTableCell cell;
                cell.columnSpan = cellElem->IntAttribute("table:number-columns-spanned", 1);
                cell.rowSpan = cellElem->IntAttribute("table:number-rows-spanned", 1);
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
        if (!block.tableRows.empty()) doc_->blocks.push_back(std::move(block));
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
                    ParseBlockContainer(elem, listLevel, listStyleName);
                }
            } else if (tag == "draw:frame") {
                // Page-anchored frame sitting directly in the text flow.
                InlineContext ctx;
                ParseFrame(elem, OdtTextProps{}, "", ctx);
                if (!ctx.runs.empty()) {
                    RichDocBlock block;
                    block.type = RichBlockType::Paragraph;
                    block.runs = std::move(ctx.runs);
                    doc_->blocks.push_back(std::move(block));
                }
                for (auto& image : ctx.trailingImages) {
                    doc_->blocks.push_back(std::move(image));
                }
                for (size_t tb = 0; tb < ctx.textBoxes.size(); ++tb) {
                    ParseBlockContainer(ctx.textBoxes[tb], listLevel, listStyleName);
                }
            } else if (tag == "style:region-left" || tag == "style:region-center"
                       || tag == "style:region-right") {
                // Header/footer column regions.
                ParseBlockContainer(elem, listLevel, listStyleName);
            } else if (tag == "draw:g") {
                // Grouped drawing shapes (e.g. a logo frame grouped with a
                // caption): recurse so the nested frames/text boxes render.
                ParseBlockContainer(elem, listLevel, listStyleName);
            }
            // Everything else (TOC, sequence declarations, forms) is skipped.
        }
    }

    // True if a master-page region (or any descendant) carries visible text or
    // an image/table — used to skip empty first-page headers when choosing
    // which master page to render.
    bool NodeHasVisibleContent(tinyxml2::XMLNode* node) const {
        for (auto* child = node->FirstChild(); child; child = child->NextSibling()) {
            if (auto* textNode = child->ToText()) {
                std::string v = textNode->Value() ? textNode->Value() : "";
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

    // Name of the master page the body actually starts on: the first
    // paragraph/heading's paragraph style may pin it via style:master-page-name
    // (ODF §16.2); otherwise the document defaults to the "Standard" master.
    std::string AppliedMasterPageName(tinyxml2::XMLElement* body) const {
        for (auto* elem = body->FirstChildElement(); elem;
             elem = elem->NextSiblingElement()) {
            std::string tag = elem->Name() ? elem->Name() : "";
            if (tag == "text:p" || tag == "text:h") {
                std::string name = ResolveStyle(Attr(elem, "text:style-name")).masterPageName;
                if (!name.empty()) return name;
                break;
            }
            if (tag == "text:section") {
                // Descend into a leading section to reach the first paragraph.
                std::string name = AppliedMasterPageName(elem);
                if (!name.empty()) return name;
            }
        }
        return "";
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

    // Emits the header or footer of the applied master page (styles.xml) as
    // ordinary blocks, set off from the body with a horizontal rule. Regions
    // that are empty or explicitly not displayed contribute nothing.
    void ParseMasterPageRegion(tinyxml2::XMLElement* page, const char* regionTag,
                               bool afterBody) {
        if (!page) return;
        auto* region = page->FirstChildElement(regionTag);
        if (!region || std::string(Attr(region, "style:display")) == "false") return;

        size_t start = doc_->blocks.size();
        if (afterBody) {
            RichDocBlock rule;
            rule.type = RichBlockType::HorizontalRule;
            doc_->blocks.push_back(std::move(rule));
        }
        size_t contentStart = doc_->blocks.size();
        ParseBlockContainer(region, 0, "");

        bool hasContent = false;
        for (size_t i = contentStart; i < doc_->blocks.size() && !hasContent; ++i) {
            const RichDocBlock& b = doc_->blocks[i];
            if (b.type != RichBlockType::Paragraph) {
                hasContent = true;
            } else {
                std::string text = UCRichDocument::ConcatenateRunText(b.runs);
                hasContent = text.find_first_not_of(" \t\n") != std::string::npos;
            }
        }
        if (!hasContent) {
            doc_->blocks.resize(start);
            return;
        }
        if (!afterBody) {
            RichDocBlock rule;
            rule.type = RichBlockType::HorizontalRule;
            doc_->blocks.push_back(std::move(rule));
        }
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
        if (!zip_.AddEntry("mimetype", std::string(kMimeType), false)
            || !zip_.AddEntry("content.xml", BuildContentXml())
            || !zip_.AddEntry("styles.xml", BuildStylesXml())
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
    std::vector<RichTextRun> textStyles_;   // formatting tuples for T1..Tn

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

    void WriteRuns(std::ostringstream& xml, const std::vector<RichTextRun>& runs) {
        for (const auto& run : runs) {
            if (run.lineBreakBefore) xml << "<text:line-break/>";
            std::string styleName = TextStyleNameFor(run);
            std::string body = OdtText(run.text);
            if (!styleName.empty()) {
                body = "<text:span text:style-name=\"" + styleName + "\">" + body + "</text:span>";
            }
            if (!run.linkTarget.empty()) {
                body = "<text:a xlink:type=\"simple\" xlink:href=\""
                     + EscapeXml(run.linkTarget) + "\">" + body + "</text:a>";
            }
            xml << body;
        }
    }

    const char* ParagraphStyleFor(const RichDocBlock& block) {
        switch (block.type) {
            case RichBlockType::BlockQuote: return "PQuote";
            case RichBlockType::CodeBlock: return "PCode";
            default: break;
        }
        switch (block.align) {
            case RichTextAlign::Center: return "PCenter";
            case RichTextAlign::Right: return "PRight";
            case RichTextAlign::Justify: return "PJustify";
            default: return "Standard";
        }
    }

    void WriteParagraph(std::ostringstream& xml, const RichDocBlock& block) {
        xml << "<text:p text:style-name=\"" << ParagraphStyleFor(block) << "\">";
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
            columnCount = std::max(columnCount, row.cells.size());
        }
        xml << "<table:table table:name=\"Table" << tableNumber << "\">\n"
            << "<table:table-column table:number-columns-repeated=\"" << columnCount << "\"/>\n";
        for (const auto& row : block.tableRows) {
            if (row.header) xml << "<table:table-header-rows>";
            xml << "<table:table-row>";
            for (const auto& cell : row.cells) {
                xml << "<table:table-cell office:value-type=\"string\"";
                if (cell.columnSpan > 1) {
                    xml << " table:number-columns-spanned=\"" << cell.columnSpan << "\"";
                }
                xml << "><text:p text:style-name=\"Standard\">";
                WriteRuns(xml, cell.runs);
                xml << "</text:p></table:table-cell>";
                for (int s = 1; s < cell.columnSpan; ++s) {
                    xml << "<table:covered-table-cell/>";
                }
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
    size_t WriteListRun(std::ostringstream& xml, size_t begin, size_t end, int level) {
        bool ordered = doc_->blocks[begin].orderedList;
        for (size_t j = begin; j < end; ++j) {
            if (doc_->blocks[j].listLevel <= level) {
                ordered = doc_->blocks[j].orderedList;
                break;
            }
        }
        xml << "<text:list text:style-name=\"" << (ordered ? "LNum" : "LBullet") << "\">\n";
        size_t i = begin;
        bool itemOpen = false;
        while (i < end) {
            const RichDocBlock& item = doc_->blocks[i];
            if (item.listLevel < level) break;
            if (item.listLevel == level) {
                if (item.orderedList != ordered) break;
                if (itemOpen) xml << "</text:list-item>\n";
                xml << "<text:list-item><text:p text:style-name=\"Standard\">";
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
                i = WriteListRun(xml, i, end, level + 1);
            }
        }
        if (itemOpen) xml << "</text:list-item>\n";
        xml << "</text:list>\n";
        return i;
    }

    std::string BuildContentXml() {
        std::ostringstream body;
        int tableNumber = 0;
        size_t i = 0;
        while (i < doc_->blocks.size()) {
            const RichDocBlock& block = doc_->blocks[i];
            switch (block.type) {
                case RichBlockType::Heading:
                    body << "<text:h text:style-name=\"Heading_20_"
                         << std::clamp(block.headingLevel, 1, 6)
                         << "\" text:outline-level=\"" << std::clamp(block.headingLevel, 1, 6)
                         << "\">";
                    WriteRuns(body, block.runs);
                    body << "</text:h>\n";
                    ++i;
                    break;
                case RichBlockType::ListItem: {
                    size_t end = i;
                    while (end < doc_->blocks.size()
                           && doc_->blocks[end].type == RichBlockType::ListItem) {
                        ++end;
                    }
                    size_t pos = i;
                    while (pos < end) {
                        pos = WriteListRun(body, pos, end, doc_->blocks[pos].listLevel);
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
                    WriteTable(body, block, ++tableNumber);
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
                case RichBlockType::Paragraph:
                case RichBlockType::BlockQuote:
                default:
                    WriteParagraph(body, block);
                    ++i;
                    break;
            }
        }

        // The body is generated first so textStyles_ is complete for the
        // automatic-styles section.
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
            << "office:version=\"1.2\">\n"
            << "<office:automatic-styles>\n";

        for (size_t s = 0; s < textStyles_.size(); ++s) {
            const RichTextRun& t = textStyles_[s];
            xml << "<style:style style:name=\"T" << (s + 1)
                << "\" style:family=\"text\"><style:text-properties";
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
            if (t.fontSizePt > 0) xml << " fo:font-size=\"" << t.fontSizePt << "pt\"";
            xml << "/></style:style>\n";
        }

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
        xml << "</text:list-style>\n"
            << "</office:automatic-styles>\n"
            << "<office:body>\n<office:text>\n"
            << body.str()
            << "</office:text>\n</office:body>\n</office:document-content>\n";
        return xml.str();
    }

    std::string BuildStylesXml() const {
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            << "<office:document-styles "
            << "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
            << "xmlns:style=\"urn:oasis:names:tc:opendocument:xmlns:style:1.0\" "
            << "xmlns:text=\"urn:oasis:names:tc:opendocument:xmlns:text:1.0\" "
            << "xmlns:fo=\"urn:oasis:names:tc:opendocument:xmlns:xsl-fo-compatible:1.0\" "
            << "office:version=\"1.2\">\n<office:styles>\n"
            << "<style:style style:name=\"Standard\" style:family=\"paragraph\"/>\n";
        static const float headingSizesPt[6] = {18.0f, 16.0f, 14.0f, 12.0f, 11.0f, 10.5f};
        for (int level = 1; level <= 6; ++level) {
            xml << "<style:style style:name=\"Heading_20_" << level
                << "\" style:display-name=\"Heading " << level
                << "\" style:family=\"paragraph\" style:parent-style-name=\"Standard\" "
                << "style:default-outline-level=\"" << level << "\">"
                << "<style:paragraph-properties fo:margin-top=\"0.3cm\" "
                   "fo:margin-bottom=\"0.15cm\" fo:keep-with-next=\"always\"/>"
                << "<style:text-properties fo:font-weight=\"bold\" fo:font-size=\""
                << headingSizesPt[level - 1] << "pt\"/></style:style>\n";
        }
        xml << "</office:styles>\n</office:document-styles>\n";
        return xml.str();
    }

    std::string BuildMetaXml() const {
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            << "<office:document-meta "
            << "xmlns:office=\"urn:oasis:names:tc:opendocument:xmlns:office:1.0\" "
            << "xmlns:meta=\"urn:oasis:names:tc:opendocument:xmlns:meta:1.0\" "
            << "xmlns:dc=\"http://purl.org/dc/elements/1.1/\" office:version=\"1.2\">\n"
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
            << "manifest:version=\"1.2\">\n"
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
    return reader.Load(filePath, outDocument, outError);
}

bool UCWordDocumentIO::SaveOdt(const std::string& filePath, const UCRichDocument& document,
                               std::string& outError) {
    OdtWriter writer;
    return writer.Save(filePath, document, outError);
}

} // namespace UltraCanvas
