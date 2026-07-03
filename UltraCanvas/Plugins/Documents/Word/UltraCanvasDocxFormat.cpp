// Plugins/Documents/Word/UltraCanvasDocxFormat.cpp
// Word 2007+ (.docx, OOXML WordprocessingML) reader and writer for
// UCRichDocument. DOCX is an OPC ZIP package: [Content_Types].xml, _rels
// relationship parts, word/document.xml plus styles/numbering/media parts.
// Version: 1.0.0
// Last Modified: 2026-07-03
// Author: UltraCanvas Framework

#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"
#include "UltraCanvasWordFormatInternal.h"
#include "UltraCanvasZipPackage.h"

#include "tinyxml2.h"

#include <map>
#include <sstream>

namespace UltraCanvas {

using WordFormatInternal::EscapeXml;
using WordFormatInternal::ToLower;

namespace {

constexpr float kEmuPerPoint = 12700.0f;

// ===== DOCX READING =====

struct DocxRelationship {
    std::string target;
    bool external = false;
};

class DocxReader {
public:
    bool Load(const std::string& filePath, UCRichDocument& doc, std::string& error) {
        doc_ = &doc;
        if (!zip_.Open(filePath)) {
            error = "The file is not a valid Word document (.docx): " + filePath;
            return false;
        }
        std::string documentXml;
        if (!zip_.ReadEntry("word/document.xml", documentXml)) {
            error = "The package has no word/document.xml — not a Word document: " + filePath;
            return false;
        }
        LoadRelationships();
        LoadStyles();
        LoadNumbering();

        if (docXml_.Parse(documentXml.c_str()) != tinyxml2::XML_SUCCESS) {
            error = "The Word document content is not valid XML: " + filePath;
            return false;
        }
        auto* root = docXml_.FirstChildElement("w:document");
        auto* body = root ? root->FirstChildElement("w:body") : nullptr;
        if (!body) {
            error = "The Word document has no body: " + filePath;
            return false;
        }
        for (auto* elem = body->FirstChildElement(); elem; elem = elem->NextSiblingElement()) {
            std::string tag = elem->Name() ? elem->Name() : "";
            if (tag == "w:p") ParseParagraph(elem);
            else if (tag == "w:tbl") ParseTable(elem);
        }
        LoadMetadata();
        return true;
    }

private:
    UCZipPackageReader zip_;
    UCRichDocument* doc_ = nullptr;
    tinyxml2::XMLDocument docXml_;
    std::map<std::string, DocxRelationship> relationships_;
    std::map<std::string, std::string> styleNames_;        // styleId -> display name
    std::map<std::string, std::string> numIdToAbstract_;
    std::map<std::string, std::map<int, bool>> abstractNumOrdered_;   // abstractId -> ilvl -> ordered
    std::map<std::string, int> mediaByTarget_;              // package path -> media index
    bool pendingPageBreak_ = false;

    static const char* Attr(const tinyxml2::XMLElement* e, const char* name) {
        const char* v = e->Attribute(name);
        return v ? v : "";
    }

    // OOXML boolean toggle: element present with no/true value means on.
    static bool ToggleOn(const tinyxml2::XMLElement* parent, const char* tag) {
        auto* e = parent->FirstChildElement(tag);
        if (!e) return false;
        std::string v = Attr(e, "w:val");
        return v.empty() || (v != "false" && v != "0" && v != "none");
    }

    void LoadRelationships() {
        std::string relsXml;
        if (!zip_.ReadEntry("word/_rels/document.xml.rels", relsXml)) return;
        tinyxml2::XMLDocument rels;
        if (rels.Parse(relsXml.c_str()) != tinyxml2::XML_SUCCESS) return;
        auto* root = rels.FirstChildElement("Relationships");
        if (!root) return;
        for (auto* rel = root->FirstChildElement("Relationship"); rel;
             rel = rel->NextSiblingElement("Relationship")) {
            DocxRelationship entry;
            entry.target = Attr(rel, "Target");
            entry.external = std::string(Attr(rel, "TargetMode")) == "External";
            std::string id = Attr(rel, "Id");
            if (!id.empty()) relationships_[id] = entry;
        }
    }

    // Resolves a relationship target relative to word/ into a package path.
    static std::string ResolvePartPath(const std::string& target) {
        std::string path = target;
        if (!path.empty() && path[0] == '/') return path.substr(1);
        path = "word/" + path;
        // Collapse "word/../x" to "x".
        size_t pos;
        while ((pos = path.find("/../")) != std::string::npos) {
            size_t slash = path.rfind('/', pos == 0 ? 0 : pos - 1);
            if (slash == std::string::npos) {
                path = path.substr(pos + 4);
            } else {
                path = path.substr(0, slash + 1) + path.substr(pos + 4);
            }
        }
        return path;
    }

    void LoadStyles() {
        std::string stylesXml;
        if (!zip_.ReadEntry("word/styles.xml", stylesXml)) return;
        tinyxml2::XMLDocument styles;
        if (styles.Parse(stylesXml.c_str()) != tinyxml2::XML_SUCCESS) return;
        auto* root = styles.FirstChildElement("w:styles");
        if (!root) return;
        for (auto* style = root->FirstChildElement("w:style"); style;
             style = style->NextSiblingElement("w:style")) {
            std::string id = Attr(style, "w:styleId");
            auto* name = style->FirstChildElement("w:name");
            if (!id.empty() && name) styleNames_[id] = Attr(name, "w:val");
        }
    }

    void LoadNumbering() {
        std::string numberingXml;
        if (!zip_.ReadEntry("word/numbering.xml", numberingXml)) return;
        tinyxml2::XMLDocument numbering;
        if (numbering.Parse(numberingXml.c_str()) != tinyxml2::XML_SUCCESS) return;
        auto* root = numbering.FirstChildElement("w:numbering");
        if (!root) return;
        for (auto* abstractNum = root->FirstChildElement("w:abstractNum"); abstractNum;
             abstractNum = abstractNum->NextSiblingElement("w:abstractNum")) {
            std::string abstractId = Attr(abstractNum, "w:abstractNumId");
            for (auto* lvl = abstractNum->FirstChildElement("w:lvl"); lvl;
                 lvl = lvl->NextSiblingElement("w:lvl")) {
                int ilvl = lvl->IntAttribute("w:ilvl", 0);
                auto* numFmt = lvl->FirstChildElement("w:numFmt");
                std::string fmt = numFmt ? Attr(numFmt, "w:val") : "";
                abstractNumOrdered_[abstractId][ilvl] = (fmt != "bullet" && !fmt.empty());
            }
        }
        for (auto* num = root->FirstChildElement("w:num"); num;
             num = num->NextSiblingElement("w:num")) {
            auto* abstractRef = num->FirstChildElement("w:abstractNumId");
            if (abstractRef) {
                numIdToAbstract_[Attr(num, "w:numId")] = Attr(abstractRef, "w:val");
            }
        }
    }

    // Returns 1..6 when the paragraph style is a heading, else 0.
    int HeadingLevelForStyle(const std::string& styleId) const {
        std::string name;
        auto it = styleNames_.find(styleId);
        if (it != styleNames_.end()) name = ToLower(it->second);
        std::string id = ToLower(styleId);
        for (const std::string& candidate : {name, id}) {
            size_t pos = candidate.find("heading");
            if (pos == std::string::npos) continue;
            size_t digit = candidate.find_first_of("123456789", pos);
            if (digit != std::string::npos) {
                int level = candidate[digit] - '0';
                if (level >= 1 && level <= 6) return level;
            }
        }
        return 0;
    }

    int LoadImageByRelId(const std::string& relId) {
        auto rel = relationships_.find(relId);
        if (rel == relationships_.end() || rel->second.external) return -1;
        std::string path = ResolvePartPath(rel->second.target);
        auto cached = mediaByTarget_.find(path);
        if (cached != mediaByTarget_.end()) return cached->second;
        std::vector<uint8_t> bytes;
        if (!zip_.ReadEntry(path, bytes)) return -1;
        std::string name = path.substr(path.find_last_of('/') + 1);
        int index = doc_->AddMedia(name, UCRichDocument::MimeTypeForImageName(name),
                                   std::move(bytes));
        mediaByTarget_[path] = index;
        return index;
    }

    static tinyxml2::XMLElement* FindDescendant(tinyxml2::XMLElement* root, const char* name) {
        for (auto* node = root->FirstChild(); node; node = node->NextSibling()) {
            auto* elem = node->ToElement();
            if (!elem) continue;
            if (elem->Name() && std::string(elem->Name()) == name) return elem;
            if (auto* found = FindDescendant(elem, name)) return found;
        }
        return nullptr;
    }

    struct InlineContext {
        std::vector<RichTextRun> runs;
        std::vector<RichDocBlock> trailingImages;
        bool pendingLineBreak = false;
    };

    void ParseRunProperties(tinyxml2::XMLElement* rPr, RichTextRun& run) const {
        if (!rPr) return;
        run.bold = ToggleOn(rPr, "w:b");
        run.italic = ToggleOn(rPr, "w:i");
        run.strikethrough = ToggleOn(rPr, "w:strike");
        run.underline = ToggleOn(rPr, "w:u");
        if (auto* vertAlign = rPr->FirstChildElement("w:vertAlign")) {
            std::string v = Attr(vertAlign, "w:val");
            run.superscript = (v == "superscript");
            run.subscript = (v == "subscript");
        }
        if (auto* color = rPr->FirstChildElement("w:color")) {
            std::string v = Attr(color, "w:val");
            if (!v.empty() && v != "auto") run.color = "#" + v;
        }
        if (auto* sz = rPr->FirstChildElement("w:sz")) {
            run.fontSizePt = sz->FloatAttribute("w:val", 0.0f) / 2.0f;   // half-points
        }
        if (auto* fonts = rPr->FirstChildElement("w:rFonts")) {
            run.fontFamily = Attr(fonts, "w:ascii");
            std::string lower = ToLower(run.fontFamily);
            if (lower.find("courier") != std::string::npos
                || lower.find("consolas") != std::string::npos
                || lower.find("mono") != std::string::npos) {
                run.code = true;
            }
        }
    }

    void ParseDrawing(tinyxml2::XMLElement* drawing, InlineContext& ctx) {
        auto* blip = FindDescendant(drawing, "a:blip");
        if (!blip) return;
        std::string relId = Attr(blip, "r:embed");
        int mediaIndex = LoadImageByRelId(relId);
        if (mediaIndex < 0) return;
        RichDocBlock block;
        block.type = RichBlockType::Image;
        block.mediaIndex = mediaIndex;
        if (auto* extent = FindDescendant(drawing, "wp:extent")) {
            block.imageWidthPt = extent->FloatAttribute("cx", 0.0f) / kEmuPerPoint;
            block.imageHeightPt = extent->FloatAttribute("cy", 0.0f) / kEmuPerPoint;
        }
        if (auto* docPr = FindDescendant(drawing, "wp:docPr")) {
            block.imageAltText = Attr(docPr, "descr");
            if (block.imageAltText.empty()) block.imageAltText = Attr(docPr, "name");
        }
        ctx.trailingImages.push_back(std::move(block));
    }

    void ParseRun(tinyxml2::XMLElement* runElem, const std::string& linkTarget,
                  InlineContext& ctx) {
        RichTextRun props;
        props.linkTarget = linkTarget;
        ParseRunProperties(runElem->FirstChildElement("w:rPr"), props);

        for (auto* child = runElem->FirstChildElement(); child;
             child = child->NextSiblingElement()) {
            std::string tag = child->Name() ? child->Name() : "";
            if (tag == "w:t") {
                RichTextRun run = props;
                run.text = child->GetText() ? child->GetText() : "";
                run.lineBreakBefore = ctx.pendingLineBreak;
                ctx.pendingLineBreak = false;
                if (!run.text.empty()) ctx.runs.push_back(std::move(run));
            } else if (tag == "w:br") {
                if (std::string(Attr(child, "w:type")) == "page") pendingPageBreak_ = true;
                else ctx.pendingLineBreak = true;
            } else if (tag == "w:cr") {
                ctx.pendingLineBreak = true;
            } else if (tag == "w:tab") {
                RichTextRun run = props;
                run.text = "\t";
                run.lineBreakBefore = ctx.pendingLineBreak;
                ctx.pendingLineBreak = false;
                ctx.runs.push_back(std::move(run));
            } else if (tag == "w:noBreakHyphen") {
                RichTextRun run = props;
                run.text = "-";
                ctx.runs.push_back(std::move(run));
            } else if (tag == "w:drawing" || tag == "w:pict") {
                ParseDrawing(child, ctx);
            }
        }
    }

    void ParseInlineContainer(tinyxml2::XMLElement* container, const std::string& linkTarget,
                              InlineContext& ctx) {
        for (auto* child = container->FirstChildElement(); child;
             child = child->NextSiblingElement()) {
            std::string tag = child->Name() ? child->Name() : "";
            if (tag == "w:r") {
                ParseRun(child, linkTarget, ctx);
            } else if (tag == "w:hyperlink") {
                std::string target = linkTarget;
                std::string relId = Attr(child, "r:id");
                auto rel = relationships_.find(relId);
                if (rel != relationships_.end() && rel->second.external) {
                    target = rel->second.target;
                } else {
                    std::string anchor = Attr(child, "w:anchor");
                    if (!anchor.empty()) target = "#" + anchor;
                }
                ParseInlineContainer(child, target, ctx);
            } else if (tag == "mc:AlternateContent") {
                // Pick exactly one representation, or content would duplicate.
                auto* choice = child->FirstChildElement("mc:Choice");
                auto* fallback = child->FirstChildElement("mc:Fallback");
                if (choice) ParseInlineContainer(choice, linkTarget, ctx);
                else if (fallback) ParseInlineContainer(fallback, linkTarget, ctx);
            } else if (tag == "w:ins" || tag == "w:smartTag" || tag == "w:sdt"
                       || tag == "w:sdtContent") {
                // Accepted tracked insertions and content-control wrappers.
                ParseInlineContainer(child, linkTarget, ctx);
            }
            // w:del (tracked deletions), bookmarks, proofing marks: skipped.
        }
    }

    void ParseParagraph(tinyxml2::XMLElement* p) {
        RichDocBlock block;
        block.type = RichBlockType::Paragraph;
        bool hasBottomBorder = false;

        if (auto* pPr = p->FirstChildElement("w:pPr")) {
            if (auto* pBdr = pPr->FirstChildElement("w:pBdr")) {
                hasBottomBorder = pBdr->FirstChildElement("w:bottom") != nullptr;
            }
            if (auto* pStyle = pPr->FirstChildElement("w:pStyle")) {
                std::string styleId = Attr(pStyle, "w:val");
                int level = HeadingLevelForStyle(styleId);
                if (level > 0) {
                    block.type = RichBlockType::Heading;
                    block.headingLevel = level;
                } else {
                    std::string name = ToLower(styleNames_.count(styleId)
                                               ? styleNames_[styleId] : styleId);
                    if (name.find("quote") != std::string::npos) {
                        block.type = RichBlockType::BlockQuote;
                    } else if (name.find("code") != std::string::npos
                               || name.find("preformatted") != std::string::npos
                               || name.find("htmlpre") != std::string::npos) {
                        block.type = RichBlockType::CodeBlock;
                    }
                }
            }
            if (auto* jc = pPr->FirstChildElement("w:jc")) {
                std::string v = Attr(jc, "w:val");
                if (v == "center") block.align = RichTextAlign::Center;
                else if (v == "right" || v == "end") block.align = RichTextAlign::Right;
                else if (v == "both" || v == "distribute") block.align = RichTextAlign::Justify;
            }
            if (auto* numPr = pPr->FirstChildElement("w:numPr")) {
                auto* ilvl = numPr->FirstChildElement("w:ilvl");
                auto* numId = numPr->FirstChildElement("w:numId");
                if (numId && std::string(Attr(numId, "w:val")) != "0") {
                    block.type = RichBlockType::ListItem;
                    block.listLevel = ilvl ? std::atoi(Attr(ilvl, "w:val")) : 0;
                    block.orderedList = false;
                    auto abstractIt = numIdToAbstract_.find(Attr(numId, "w:val"));
                    if (abstractIt != numIdToAbstract_.end()) {
                        auto levels = abstractNumOrdered_.find(abstractIt->second);
                        if (levels != abstractNumOrdered_.end()) {
                            auto lvlIt = levels->second.find(block.listLevel);
                            if (lvlIt != levels->second.end()) block.orderedList = lvlIt->second;
                        }
                    }
                }
            }
        }

        InlineContext ctx;
        ParseInlineContainer(p, "", ctx);
        block.runs = std::move(ctx.runs);

        // An empty paragraph carrying only a bottom border is the
        // horizontal-rule idiom.
        if (block.type == RichBlockType::Paragraph && block.runs.empty() && hasBottomBorder) {
            block.type = RichBlockType::HorizontalRule;
        }

        bool emptyParagraph = block.runs.empty() && ctx.trailingImages.empty()
                              && block.type == RichBlockType::Paragraph;
        bool imageOnly = block.runs.empty() && !ctx.trailingImages.empty();
        if (!imageOnly && !emptyParagraph) {
            doc_->blocks.push_back(std::move(block));
        } else if (emptyParagraph && !pendingPageBreak_) {
            doc_->blocks.push_back(std::move(block));   // keep intentional blank lines
        }
        for (auto& image : ctx.trailingImages) {
            doc_->blocks.push_back(std::move(image));
        }
        if (pendingPageBreak_) {
            RichDocBlock pageBreak;
            pageBreak.type = RichBlockType::PageBreak;
            doc_->blocks.push_back(std::move(pageBreak));
            pendingPageBreak_ = false;
        }
    }

    void ParseTable(tinyxml2::XMLElement* tbl) {
        RichDocBlock block;
        block.type = RichBlockType::Table;
        for (auto* tr = tbl->FirstChildElement("w:tr"); tr;
             tr = tr->NextSiblingElement("w:tr")) {
            RichTableRow row;
            if (auto* trPr = tr->FirstChildElement("w:trPr")) {
                row.header = trPr->FirstChildElement("w:tblHeader") != nullptr;
            }
            for (auto* tc = tr->FirstChildElement("w:tc"); tc;
                 tc = tc->NextSiblingElement("w:tc")) {
                RichTableCell cell;
                if (auto* tcPr = tc->FirstChildElement("w:tcPr")) {
                    if (auto* gridSpan = tcPr->FirstChildElement("w:gridSpan")) {
                        cell.columnSpan = std::max(1, std::atoi(Attr(gridSpan, "w:val")));
                    }
                }
                InlineContext ctx;
                bool firstParagraph = true;
                for (auto* p = tc->FirstChildElement("w:p"); p;
                     p = p->NextSiblingElement("w:p")) {
                    if (!firstParagraph) ctx.pendingLineBreak = true;
                    firstParagraph = false;
                    ParseInlineContainer(p, "", ctx);
                }
                cell.runs = std::move(ctx.runs);
                row.cells.push_back(std::move(cell));
            }
            block.tableRows.push_back(std::move(row));
        }
        if (!block.tableRows.empty()) doc_->blocks.push_back(std::move(block));
    }

    void LoadMetadata() {
        std::string coreXml;
        if (!zip_.ReadEntry("docProps/core.xml", coreXml)) return;
        tinyxml2::XMLDocument core;
        if (core.Parse(coreXml.c_str()) != tinyxml2::XML_SUCCESS) return;
        auto* root = core.FirstChildElement("cp:coreProperties");
        if (!root) return;
        auto readText = [&](const char* tag) -> std::string {
            auto* e = root->FirstChildElement(tag);
            return (e && e->GetText()) ? e->GetText() : "";
        };
        doc_->metadata.title = readText("dc:title");
        doc_->metadata.author = readText("dc:creator");
        doc_->metadata.description = readText("dc:description");
        doc_->metadata.createdDate = readText("dcterms:created");
        doc_->metadata.modifiedDate = readText("dcterms:modified");
    }
};

// ===== DOCX WRITING =====

class DocxWriter {
public:
    bool Save(const std::string& filePath, const UCRichDocument& doc, std::string& error) {
        doc_ = &doc;
        if (!zip_.Open(filePath)) {
            error = "Cannot create file: " + filePath;
            return false;
        }
        std::string documentXml = BuildDocumentXml();   // fills hyperlinks_ / usesLists_
        if (!zip_.AddEntry("[Content_Types].xml", BuildContentTypesXml())
            || !zip_.AddEntry("_rels/.rels", BuildRootRelsXml())
            || !zip_.AddEntry("word/document.xml", documentXml)
            || !zip_.AddEntry("word/_rels/document.xml.rels", BuildDocumentRelsXml())
            || !zip_.AddEntry("word/styles.xml", BuildStylesXml())
            || !zip_.AddEntry("docProps/core.xml", BuildCorePropsXml())
            || !zip_.AddEntry("docProps/app.xml", BuildAppPropsXml())) {
            error = "Failed to write document package: " + zip_.GetLastError();
            return false;
        }
        if (usesLists_ && !zip_.AddEntry("word/numbering.xml", BuildNumberingXml())) {
            error = "Failed to write list definitions: " + zip_.GetLastError();
            return false;
        }
        for (size_t i = 0; i < doc.media.size(); ++i) {
            if (!zip_.AddEntry(MediaPartName(i), doc.media[i].data.data(),
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
    std::vector<std::string> hyperlinks_;   // index -> URL; rel id = rIdLink{index+1}
    bool usesLists_ = false;

    std::string MediaPartName(size_t mediaIndex) const {
        const RichDocMedia& m = doc_->media[mediaIndex];
        std::string ext = UCRichDocument::FileExtensionForMimeType(m.mimeType);
        return "word/media/image" + std::to_string(mediaIndex + 1) + "." + ext;
    }

    int HyperlinkRelIndex(const std::string& url) {
        for (size_t i = 0; i < hyperlinks_.size(); ++i) {
            if (hyperlinks_[i] == url) return static_cast<int>(i);
        }
        hyperlinks_.push_back(url);
        return static_cast<int>(hyperlinks_.size()) - 1;
    }

    static void WriteRunProperties(std::ostringstream& xml, const RichTextRun& run,
                                   bool asHyperlink) {
        bool hasProps = run.bold || run.italic || run.underline || run.strikethrough
                        || run.code || run.subscript || run.superscript
                        || !run.color.empty() || !run.fontFamily.empty()
                        || run.fontSizePt > 0 || asHyperlink;
        if (!hasProps) return;
        xml << "<w:rPr>";
        if (run.code) {
            xml << "<w:rFonts w:ascii=\"Courier New\" w:hAnsi=\"Courier New\"/>";
        } else if (!run.fontFamily.empty()) {
            xml << "<w:rFonts w:ascii=\"" << EscapeXml(run.fontFamily)
                << "\" w:hAnsi=\"" << EscapeXml(run.fontFamily) << "\"/>";
        }
        if (run.bold) xml << "<w:b/>";
        if (run.italic) xml << "<w:i/>";
        if (run.strikethrough) xml << "<w:strike/>";
        if (run.underline || asHyperlink) xml << "<w:u w:val=\"single\"/>";
        if (run.superscript) xml << "<w:vertAlign w:val=\"superscript\"/>";
        if (run.subscript) xml << "<w:vertAlign w:val=\"subscript\"/>";
        if (!run.color.empty()) {
            std::string hex = run.color;
            if (!hex.empty() && hex[0] == '#') hex = hex.substr(1);
            xml << "<w:color w:val=\"" << EscapeXml(hex) << "\"/>";
        } else if (asHyperlink) {
            xml << "<w:color w:val=\"0563C1\"/>";
        }
        if (run.fontSizePt > 0) {
            xml << "<w:sz w:val=\"" << static_cast<int>(run.fontSizePt * 2 + 0.5f) << "\"/>"
                << "<w:szCs w:val=\"" << static_cast<int>(run.fontSizePt * 2 + 0.5f) << "\"/>";
        }
        xml << "</w:rPr>";
    }

    // Splits run text on tabs/newlines into w:t / w:tab / w:br sequence.
    static void WriteRunText(std::ostringstream& xml, const std::string& text) {
        std::string pending;
        auto flush = [&]() {
            if (pending.empty()) return;
            xml << "<w:t xml:space=\"preserve\">" << EscapeXml(pending) << "</w:t>";
            pending.clear();
        };
        for (char c : text) {
            if (c == '\t') { flush(); xml << "<w:tab/>"; }
            else if (c == '\n') { flush(); xml << "<w:br/>"; }
            else pending.push_back(c);
        }
        flush();
    }

    void WriteRuns(std::ostringstream& xml, const std::vector<RichTextRun>& runs) {
        for (const auto& run : runs) {
            bool isLink = !run.linkTarget.empty();
            if (isLink) {
                xml << "<w:hyperlink r:id=\"rIdLink"
                    << (HyperlinkRelIndex(run.linkTarget) + 1) << "\">";
            }
            xml << "<w:r>";
            WriteRunProperties(xml, run, isLink);
            if (run.lineBreakBefore) xml << "<w:br/>";
            WriteRunText(xml, run.text);
            xml << "</w:r>";
            if (isLink) xml << "</w:hyperlink>";
        }
    }

    static const char* JcValue(RichTextAlign align) {
        switch (align) {
            case RichTextAlign::Center: return "center";
            case RichTextAlign::Right: return "right";
            case RichTextAlign::Justify: return "both";
            default: return nullptr;
        }
    }

    void WriteParagraph(std::ostringstream& xml, const RichDocBlock& block) {
        xml << "<w:p>";
        std::ostringstream pPr;
        if (block.type == RichBlockType::Heading) {
            pPr << "<w:pStyle w:val=\"Heading" << std::clamp(block.headingLevel, 1, 6)
                << "\"/>";
        } else if (block.type == RichBlockType::BlockQuote) {
            pPr << "<w:pStyle w:val=\"Quote\"/>";
        } else if (block.type == RichBlockType::CodeBlock) {
            pPr << "<w:pStyle w:val=\"CodeBlock\"/>";
        } else if (block.type == RichBlockType::ListItem) {
            usesLists_ = true;
            pPr << "<w:pStyle w:val=\"ListParagraph\"/><w:numPr><w:ilvl w:val=\""
                << std::max(0, block.listLevel) << "\"/><w:numId w:val=\""
                << (block.orderedList ? 2 : 1) << "\"/></w:numPr>";
        } else if (block.type == RichBlockType::HorizontalRule) {
            pPr << "<w:pBdr><w:bottom w:val=\"single\" w:sz=\"6\" w:space=\"1\" "
                   "w:color=\"808080\"/></w:pBdr>";
        }
        if (const char* jc = JcValue(block.align)) {
            pPr << "<w:jc w:val=\"" << jc << "\"/>";
        }
        std::string pPrStr = pPr.str();
        if (!pPrStr.empty()) xml << "<w:pPr>" << pPrStr << "</w:pPr>";

        if (block.type == RichBlockType::CodeBlock) {
            // Collapse per-line runs into one monospace run with breaks.
            RichTextRun run;
            run.text = UCRichDocument::ConcatenateRunText(block.runs);
            run.code = true;
            WriteRuns(xml, {run});
        } else {
            WriteRuns(xml, block.runs);
        }
        xml << "</w:p>\n";
    }

    void WriteImage(std::ostringstream& xml, const RichDocBlock& block, int drawingId) {
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
                widthPt = 288.0f;
                heightPt = 216.0f;
            }
        }
        long long cx = static_cast<long long>(widthPt * kEmuPerPoint);
        long long cy = static_cast<long long>(heightPt * kEmuPerPoint);
        std::string name = block.imageAltText.empty()
            ? "Image " + std::to_string(drawingId) : block.imageAltText;

        xml << "<w:p><w:r><w:drawing>"
            << "<wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">"
            << "<wp:extent cx=\"" << cx << "\" cy=\"" << cy << "\"/>"
            << "<wp:docPr id=\"" << drawingId << "\" name=\"" << EscapeXml(name) << "\"/>"
            << "<a:graphic xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">"
            << "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
            << "<pic:pic xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\">"
            << "<pic:nvPicPr><pic:cNvPr id=\"" << drawingId << "\" name=\""
            << EscapeXml(name) << "\"/><pic:cNvPicPr/></pic:nvPicPr>"
            << "<pic:blipFill><a:blip r:embed=\"rIdImg" << (block.mediaIndex + 1)
            << "\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>"
            << "<pic:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\"" << cx
            << "\" cy=\"" << cy << "\"/></a:xfrm>"
            << "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom></pic:spPr>"
            << "</pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing></w:r></w:p>\n";
    }

    void WriteTable(std::ostringstream& xml, const RichDocBlock& block) {
        size_t columnCount = 0;
        for (const auto& row : block.tableRows) {
            size_t width = 0;
            for (const auto& cell : row.cells) width += std::max(1, cell.columnSpan);
            columnCount = std::max(columnCount, width);
        }
        xml << "<w:tbl><w:tblPr><w:tblStyle w:val=\"TableGrid\"/>"
               "<w:tblW w:w=\"0\" w:type=\"auto\"/>"
               "<w:tblBorders>"
               "<w:top w:val=\"single\" w:sz=\"4\" w:color=\"auto\"/>"
               "<w:left w:val=\"single\" w:sz=\"4\" w:color=\"auto\"/>"
               "<w:bottom w:val=\"single\" w:sz=\"4\" w:color=\"auto\"/>"
               "<w:right w:val=\"single\" w:sz=\"4\" w:color=\"auto\"/>"
               "<w:insideH w:val=\"single\" w:sz=\"4\" w:color=\"auto\"/>"
               "<w:insideV w:val=\"single\" w:sz=\"4\" w:color=\"auto\"/>"
               "</w:tblBorders></w:tblPr><w:tblGrid>";
        for (size_t c = 0; c < columnCount; ++c) xml << "<w:gridCol/>";
        xml << "</w:tblGrid>\n";
        for (const auto& row : block.tableRows) {
            xml << "<w:tr>";
            if (row.header) xml << "<w:trPr><w:tblHeader/></w:trPr>";
            for (const auto& cell : row.cells) {
                xml << "<w:tc><w:tcPr>";
                if (cell.columnSpan > 1) {
                    xml << "<w:gridSpan w:val=\"" << cell.columnSpan << "\"/>";
                }
                xml << "</w:tcPr><w:p>";
                std::vector<RichTextRun> runs = cell.runs;
                if (row.header) {
                    for (auto& run : runs) run.bold = true;
                }
                WriteRuns(xml, runs);
                xml << "</w:p></w:tc>";
            }
            xml << "</w:tr>\n";
        }
        xml << "</w:tbl>\n"
            // Word requires a paragraph between a table and the section end.
            << "<w:p/>\n";
    }

    std::string BuildDocumentXml() {
        std::ostringstream body;
        int drawingId = 0;
        for (const auto& block : doc_->blocks) {
            switch (block.type) {
                case RichBlockType::Table:
                    WriteTable(body, block);
                    break;
                case RichBlockType::Image:
                    WriteImage(body, block, ++drawingId);
                    break;
                case RichBlockType::PageBreak:
                    body << "<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>\n";
                    break;
                default:
                    WriteParagraph(body, block);
                    break;
            }
        }
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            << "<w:document "
            << "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
            << "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
            << "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\">"
            << "<w:body>\n" << body.str()
            << "<w:sectPr><w:pgSz w:w=\"11906\" w:h=\"16838\"/>"
               "<w:pgMar w:top=\"1134\" w:right=\"1134\" w:bottom=\"1134\" w:left=\"1134\" "
               "w:header=\"709\" w:footer=\"709\" w:gutter=\"0\"/></w:sectPr>"
            << "</w:body></w:document>\n";
        return xml.str();
    }

    std::string BuildContentTypesXml() const {
        // One Default per distinct media extension keeps the package minimal.
        std::map<std::string, std::string> extToMime;
        for (const auto& m : doc_->media) {
            extToMime[UCRichDocument::FileExtensionForMimeType(m.mimeType)] = m.mimeType;
        }
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            << "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">\n"
            << "<Default Extension=\"rels\" "
               "ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>\n"
            << "<Default Extension=\"xml\" ContentType=\"application/xml\"/>\n";
        for (const auto& [ext, mime] : extToMime) {
            xml << "<Default Extension=\"" << ext << "\" ContentType=\"" << mime << "\"/>\n";
        }
        xml << "<Override PartName=\"/word/document.xml\" "
               "ContentType=\"application/vnd.openxmlformats-officedocument."
               "wordprocessingml.document.main+xml\"/>\n"
            << "<Override PartName=\"/word/styles.xml\" "
               "ContentType=\"application/vnd.openxmlformats-officedocument."
               "wordprocessingml.styles+xml\"/>\n";
        if (usesLists_) {
            xml << "<Override PartName=\"/word/numbering.xml\" "
                   "ContentType=\"application/vnd.openxmlformats-officedocument."
                   "wordprocessingml.numbering+xml\"/>\n";
        }
        xml << "<Override PartName=\"/docProps/core.xml\" "
               "ContentType=\"application/vnd.openxmlformats-package."
               "core-properties+xml\"/>\n"
            << "<Override PartName=\"/docProps/app.xml\" "
               "ContentType=\"application/vnd.openxmlformats-officedocument."
               "extended-properties+xml\"/>\n"
            << "</Types>\n";
        return xml.str();
    }

    static std::string BuildRootRelsXml() {
        return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
               "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
               "<Relationship Id=\"rId1\" "
               "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument\" "
               "Target=\"word/document.xml\"/>\n"
               "<Relationship Id=\"rId2\" "
               "Type=\"http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties\" "
               "Target=\"docProps/core.xml\"/>\n"
               "<Relationship Id=\"rId3\" "
               "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/extended-properties\" "
               "Target=\"docProps/app.xml\"/>\n"
               "</Relationships>\n";
    }

    std::string BuildDocumentRelsXml() const {
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            << "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
            << "<Relationship Id=\"rIdStyles\" "
               "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles\" "
               "Target=\"styles.xml\"/>\n";
        if (usesLists_) {
            xml << "<Relationship Id=\"rIdNumbering\" "
                   "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/numbering\" "
                   "Target=\"numbering.xml\"/>\n";
        }
        for (size_t i = 0; i < doc_->media.size(); ++i) {
            std::string target = MediaPartName(i).substr(5);   // strip "word/"
            xml << "<Relationship Id=\"rIdImg" << (i + 1)
                << "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/image\" "
                << "Target=\"" << target << "\"/>\n";
        }
        for (size_t i = 0; i < hyperlinks_.size(); ++i) {
            xml << "<Relationship Id=\"rIdLink" << (i + 1)
                << "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/hyperlink\" "
                << "Target=\"" << EscapeXml(hyperlinks_[i])
                << "\" TargetMode=\"External\"/>\n";
        }
        xml << "</Relationships>\n";
        return xml.str();
    }

    static std::string BuildStylesXml() {
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            << "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">\n"
            << "<w:docDefaults><w:rPrDefault><w:rPr>"
               "<w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\"/>"
               "<w:sz w:val=\"22\"/><w:szCs w:val=\"22\"/>"
               "</w:rPr></w:rPrDefault></w:docDefaults>\n"
            << "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\">"
               "<w:name w:val=\"Normal\"/></w:style>\n";
        static const int headingSizesHalfPt[6] = {36, 32, 28, 26, 24, 22};
        for (int level = 1; level <= 6; ++level) {
            xml << "<w:style w:type=\"paragraph\" w:styleId=\"Heading" << level << "\">"
                << "<w:name w:val=\"heading " << level << "\"/>"
                << "<w:basedOn w:val=\"Normal\"/>"
                << "<w:pPr><w:keepNext/><w:spacing w:before=\"240\" w:after=\"120\"/>"
                << "<w:outlineLvl w:val=\"" << (level - 1) << "\"/></w:pPr>"
                << "<w:rPr><w:b/><w:sz w:val=\"" << headingSizesHalfPt[level - 1]
                << "\"/><w:szCs w:val=\"" << headingSizesHalfPt[level - 1]
                << "\"/></w:rPr></w:style>\n";
        }
        xml << "<w:style w:type=\"paragraph\" w:styleId=\"Quote\">"
               "<w:name w:val=\"Quote\"/><w:basedOn w:val=\"Normal\"/>"
               "<w:pPr><w:ind w:left=\"567\" w:right=\"567\"/></w:pPr>"
               "<w:rPr><w:i/></w:rPr></w:style>\n"
            << "<w:style w:type=\"paragraph\" w:styleId=\"CodeBlock\">"
               "<w:name w:val=\"Code Block\"/><w:basedOn w:val=\"Normal\"/>"
               "<w:pPr><w:ind w:left=\"284\"/></w:pPr>"
               "<w:rPr><w:rFonts w:ascii=\"Courier New\" w:hAnsi=\"Courier New\"/>"
               "<w:sz w:val=\"20\"/></w:rPr></w:style>\n"
            << "<w:style w:type=\"paragraph\" w:styleId=\"ListParagraph\">"
               "<w:name w:val=\"List Paragraph\"/><w:basedOn w:val=\"Normal\"/>"
               "<w:pPr><w:ind w:left=\"720\"/><w:contextualSpacing/></w:pPr></w:style>\n"
            << "<w:style w:type=\"table\" w:styleId=\"TableGrid\">"
               "<w:name w:val=\"Table Grid\"/></w:style>\n"
            << "</w:styles>\n";
        return xml.str();
    }

    static std::string BuildNumberingXml() {
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            << "<w:numbering xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">\n"
            << "<w:abstractNum w:abstractNumId=\"0\">\n";
        for (int level = 0; level < 9; ++level) {
            xml << "<w:lvl w:ilvl=\"" << level << "\"><w:start w:val=\"1\"/>"
                << "<w:numFmt w:val=\"bullet\"/><w:lvlText w:val=\"\xE2\x80\xA2\"/>"
                << "<w:lvlJc w:val=\"left\"/><w:pPr><w:ind w:left=\""
                << 720 * (level + 1) << "\" w:hanging=\"360\"/></w:pPr>"
                << "<w:rPr><w:rFonts w:ascii=\"Symbol\" w:hAnsi=\"Symbol\" w:hint=\"default\"/>"
                << "</w:rPr></w:lvl>\n";
        }
        xml << "</w:abstractNum>\n<w:abstractNum w:abstractNumId=\"1\">\n";
        for (int level = 0; level < 9; ++level) {
            xml << "<w:lvl w:ilvl=\"" << level << "\"><w:start w:val=\"1\"/>"
                << "<w:numFmt w:val=\"decimal\"/><w:lvlText w:val=\"%" << (level + 1)
                << ".\"/><w:lvlJc w:val=\"left\"/><w:pPr><w:ind w:left=\""
                << 720 * (level + 1) << "\" w:hanging=\"360\"/></w:pPr></w:lvl>\n";
        }
        xml << "</w:abstractNum>\n"
            << "<w:num w:numId=\"1\"><w:abstractNumId w:val=\"0\"/></w:num>\n"
            << "<w:num w:numId=\"2\"><w:abstractNumId w:val=\"1\"/></w:num>\n"
            << "</w:numbering>\n";
        return xml.str();
    }

    std::string BuildCorePropsXml() const {
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            << "<cp:coreProperties "
            << "xmlns:cp=\"http://schemas.openxmlformats.org/package/2006/metadata/core-properties\" "
            << "xmlns:dc=\"http://purl.org/dc/elements/1.1/\">\n";
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
        xml << "</cp:coreProperties>\n";
        return xml.str();
    }

    static std::string BuildAppPropsXml() {
        return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
               "<Properties xmlns=\"http://schemas.openxmlformats.org/officeDocument/2006/"
               "extended-properties\">\n"
               "<Application>UltraCanvas</Application>\n"
               "</Properties>\n";
    }
};

} // namespace

bool UCWordDocumentIO::LoadDocx(const std::string& filePath, UCRichDocument& outDocument,
                                std::string& outError) {
    outDocument = UCRichDocument{};
    DocxReader reader;
    return reader.Load(filePath, outDocument, outError);
}

bool UCWordDocumentIO::SaveDocx(const std::string& filePath, const UCRichDocument& document,
                                std::string& outError) {
    DocxWriter writer;
    return writer.Save(filePath, document, outError);
}

} // namespace UltraCanvas
