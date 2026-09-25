// Plugins/Documents/Word/UltraCanvasDocxFormat.cpp
// Word 2007+ (.docx, OOXML WordprocessingML) reader and writer for
// UCRichDocument. DOCX is an OPC ZIP package: [Content_Types].xml, _rels
// relationship parts, word/document.xml plus styles/numbering/media parts.
// Version: 1.0.0
// Last Modified: 2026-07-03
// Author: UltraCanvas Framework

#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"
#include "UltraCanvasMathToLatex.h"
#include "UltraCanvasWordFormatInternal.h"
#include "UltraCanvasZipPackage.h"

#include "tinyxml2.h"

#include <cmath>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>

namespace UltraCanvas {

using WordFormatInternal::EscapeXml;
using WordFormatInternal::MathBlockAsParagraph;
using WordFormatInternal::ToLower;

namespace {

constexpr float kEmuPerPoint = 12700.0f;

// ===== DOCX READING =====

struct DocxRelationship {
    std::string target;
    bool external = false;
};

// A run of nothing but spaces - <w:t xml:space="preserve"> </w:t>, the space
// between two differently formatted words - is text, but tinyxml2 drops
// whitespace-only text. Each such space is swapped for a private-use marker
// before parsing and turned back into a space when the run is read. Without
// xml:space="preserve" Word ignores the whitespace too, so that is left alone.
constexpr const char* kKeptSpaceMarker = "\xEE\x80\x81";   // U+E001

void ProtectWhitespaceRuns(std::string& xml) {
    std::string out;
    out.reserve(xml.size());
    size_t pos = 0;
    while (true) {
        size_t open = xml.find("<w:t", pos);
        if (open == std::string::npos) break;
        const char after = open + 4 < xml.size() ? xml[open + 4] : '\0';
        size_t close = xml.find('>', open);
        if ((after != '>' && after != ' ') || close == std::string::npos || xml[close - 1] == '/') {
            out.append(xml, pos, open + 4 - pos);
            pos = open + 4;
            continue;
        }
        size_t text = close + 1;
        size_t end = text;
        while (end < xml.size() && (xml[end] == ' ' || xml[end] == '\t' || xml[end] == '\n' || xml[end] == '\r')) ++end;
        out.append(xml, pos, text - pos);
        const bool preserve =
            xml.substr(open, close - open).find("xml:space=\"preserve\"") != std::string::npos;
        if (end > text && preserve && xml.compare(end, 6, "</w:t>") == 0) {
            for (size_t i = text; i < end; ++i) out += kKeptSpaceMarker;
        } else {
            out.append(xml, text, end - text);
        }
        pos = end;
    }
    out.append(xml, pos, std::string::npos);
    xml.swap(out);
}

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
        LoadSettings();
        LoadNumbering();

        ProtectWhitespaceRuns(documentXml);
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
    // ===== PARAGRAPH GEOMETRY =====
    // Word measures in twips (1/20 pt); tab positions count from the page's
    // text margin, which is exactly the model's convention.
    struct Geometry {
        float left = kUnset, right = kUnset, firstLine = kUnset;
        float before = kUnset, after = kUnset, lineSpacing = kUnset;
        // Tab changes in order: a stop to add, or (clear) one to remove.
        struct TabChange { RichTabStop stop; bool clear = false; };
        std::vector<TabChange> tabs;
    };
    struct StyleGeometry {
        std::string basedOn;
        Geometry geometry;
    };
    static constexpr float kUnset = -1.0e9f;

    UCZipPackageReader zip_;
    UCRichDocument* doc_ = nullptr;
    tinyxml2::XMLDocument docXml_;
    std::map<std::string, DocxRelationship> relationships_;
    std::map<std::string, std::string> styleNames_;        // styleId -> display name
    std::map<std::string, StyleGeometry> styleGeometry_;   // paragraph styleId -> geometry
    Geometry defaultGeometry_;                             // w:docDefaults/w:pPrDefault
    std::string defaultParagraphStyle_;                    // applies when a paragraph names none
    std::map<std::string, std::string> numIdToAbstract_;
    std::map<std::string, std::map<int, bool>> abstractNumOrdered_;   // abstractId -> ilvl -> ordered
    std::map<std::string, std::map<int, int>> abstractNumStart_;      // abstractId -> ilvl -> w:start
    // numId -> ilvl -> w:startOverride, applied when that numId is first used
    std::map<std::string, std::map<int, int>> numStartOverride_;
    std::set<std::string> numIdsSeen_;
    // Word counts per abstract list: every paragraph of that list advances
    // one counter per level, whatever paragraphs sit between them.
    RichListNumbering numbering_;
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
        if (auto* defaults = root->FirstChildElement("w:docDefaults")) {
            auto* pPrDefault = defaults->FirstChildElement("w:pPrDefault");
            if (auto* pPr = pPrDefault ? pPrDefault->FirstChildElement("w:pPr") : nullptr) {
                ReadGeometry(pPr, defaultGeometry_);
            }
        }
        for (auto* style = root->FirstChildElement("w:style"); style;
             style = style->NextSiblingElement("w:style")) {
            std::string id = Attr(style, "w:styleId");
            auto* name = style->FirstChildElement("w:name");
            if (!id.empty() && name) styleNames_[id] = Attr(name, "w:val");
            if (id.empty() || std::string(Attr(style, "w:type")) != "paragraph") continue;
            StyleGeometry entry;
            if (auto* basedOn = style->FirstChildElement("w:basedOn")) entry.basedOn = Attr(basedOn, "w:val");
            if (auto* pPr = style->FirstChildElement("w:pPr")) ReadGeometry(pPr, entry.geometry);
            styleGeometry_[id] = entry;
            if (std::string(Attr(style, "w:default")) == "1" || std::string(Attr(style, "w:default")) == "true") {
                defaultParagraphStyle_ = id;
            }
        }
    }

    // word/settings.xml: the distance between default tab stops.
    void LoadSettings() {
        std::string settingsXml;
        if (!zip_.ReadEntry("word/settings.xml", settingsXml)) return;
        tinyxml2::XMLDocument settings;
        if (settings.Parse(settingsXml.c_str()) != tinyxml2::XML_SUCCESS) return;
        auto* root = settings.FirstChildElement("w:settings");
        auto* tab = root ? root->FirstChildElement("w:defaultTabStop") : nullptr;
        if (tab) {
            const int twips = tab->IntAttribute("w:val", 0);
            if (twips > 0) doc_->defaultTabStopPt = static_cast<float>(twips) / 20.0f;
        }
    }


    static void ReadGeometry(tinyxml2::XMLElement* pPr, Geometry& g) {
        auto twips = [](tinyxml2::XMLElement* e, const char* name, float& out) {
            if (e && e->Attribute(name)) out = static_cast<float>(e->IntAttribute(name, 0)) / 20.0f;
        };
        if (auto* ind = pPr->FirstChildElement("w:ind")) {
            twips(ind, "w:start", g.left);
            twips(ind, "w:left", g.left);
            twips(ind, "w:end", g.right);
            twips(ind, "w:right", g.right);
            twips(ind, "w:firstLine", g.firstLine);
            if (ind->Attribute("w:hanging")) {
                g.firstLine = -static_cast<float>(ind->IntAttribute("w:hanging", 0)) / 20.0f;
            }
        }
        if (auto* spacing = pPr->FirstChildElement("w:spacing")) {
            twips(spacing, "w:before", g.before);
            twips(spacing, "w:after", g.after);
            // Only "auto" spacing is proportional (240 = single); exact and
            // at-least heights are left to the view.
            const std::string rule = Attr(spacing, "w:lineRule");
            if (spacing->Attribute("w:line") && (rule.empty() || rule == "auto")) {
                g.lineSpacing = static_cast<float>(spacing->IntAttribute("w:line", 240)) / 240.0f;
            }
        }
        if (auto* tabs = pPr->FirstChildElement("w:tabs")) {
            for (auto* tab = tabs->FirstChildElement("w:tab"); tab;
                 tab = tab->NextSiblingElement("w:tab")) {
                Geometry::TabChange change;
                change.stop.positionPt = static_cast<float>(tab->IntAttribute("w:pos", 0)) / 20.0f;
                const std::string val = Attr(tab, "w:val");
                change.clear = val == "clear";
                change.stop.kind = (val == "center") ? RichTabKind::Center
                                 : (val == "right" || val == "end") ? RichTabKind::Right
                                 : (val == "decimal") ? RichTabKind::Decimal : RichTabKind::Left;
                if (val == "bar") continue;   // a vertical line, not a stop
                g.tabs.push_back(change);
            }
        }
    }

    static void Overlay(Geometry& base, const Geometry& over) {
        auto take = [](float& value, float overValue) { if (overValue != kUnset) value = overValue; };
        take(base.left, over.left);
        take(base.right, over.right);
        take(base.firstLine, over.firstLine);
        take(base.before, over.before);
        take(base.after, over.after);
        take(base.lineSpacing, over.lineSpacing);
        base.tabs.insert(base.tabs.end(), over.tabs.begin(), over.tabs.end());
    }

    Geometry StyleChainGeometry(const std::string& styleId, int depth = 0) const {
        auto it = styleGeometry_.find(styleId);
        if (it == styleGeometry_.end() || depth > 16) return defaultGeometry_;
        Geometry g = it->second.basedOn.empty() ? defaultGeometry_
                                                : StyleChainGeometry(it->second.basedOn, depth + 1);
        Overlay(g, it->second.geometry);
        return g;
    }

    void ApplyGeometry(RichDocBlock& block, tinyxml2::XMLElement* pPr) const {
        std::string styleId = defaultParagraphStyle_;
        auto* pStyle = pPr ? pPr->FirstChildElement("w:pStyle") : nullptr;
        if (pStyle) styleId = Attr(pStyle, "w:val");
        Geometry g = StyleChainGeometry(styleId);
        if (pPr) {
            Geometry direct;
            ReadGeometry(pPr, direct);
            Overlay(g, direct);
        }
        auto value = [](float v) { return v == kUnset ? 0.0f : v; };
        block.leftIndentPt = value(g.left);
        block.rightIndentPt = value(g.right);
        block.firstLineIndentPt = value(g.firstLine);
        // Word's built-in default spacing is zero; unstated means zero.
        block.spaceBeforePt = value(g.before);
        block.spaceAfterPt = value(g.after);
        block.lineSpacing = g.lineSpacing == kUnset ? 0.0f : g.lineSpacing;
        block.tabStops.clear();
        for (const auto& change : g.tabs) {
            auto& stops = block.tabStops;
            stops.erase(std::remove_if(stops.begin(), stops.end(),
                                       [&](const RichTabStop& t) {
                                           return std::abs(t.positionPt - change.stop.positionPt) < 0.5f;
                                       }),
                        stops.end());
            if (!change.clear) stops.push_back(change.stop);
        }
        std::sort(block.tabStops.begin(), block.tabStops.end(),
                  [](const RichTabStop& a, const RichTabStop& b) { return a.positionPt < b.positionPt; });
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
                if (auto* start = lvl->FirstChildElement("w:start")) {
                    abstractNumStart_[abstractId][ilvl] = std::max(1, start->IntAttribute("w:val", 1));
                }
            }
        }
        for (auto* num = root->FirstChildElement("w:num"); num;
             num = num->NextSiblingElement("w:num")) {
            auto* abstractRef = num->FirstChildElement("w:abstractNumId");
            if (abstractRef) {
                numIdToAbstract_[Attr(num, "w:numId")] = Attr(abstractRef, "w:val");
            }
            for (auto* lvlOverride = num->FirstChildElement("w:lvlOverride"); lvlOverride;
                 lvlOverride = lvlOverride->NextSiblingElement("w:lvlOverride")) {
                if (auto* start = lvlOverride->FirstChildElement("w:startOverride")) {
                    numStartOverride_[Attr(num, "w:numId")][lvlOverride->IntAttribute("w:ilvl", 0)] =
                        std::max(1, start->IntAttribute("w:val", 1));
                }
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

        float widthPt = 0.0f, heightPt = 0.0f;
        if (auto* extent = FindDescendant(drawing, "wp:extent")) {
            widthPt = extent->FloatAttribute("cx", 0.0f) / kEmuPerPoint;
            heightPt = extent->FloatAttribute("cy", 0.0f) / kEmuPerPoint;
        }
        std::string altText;
        if (auto* docPr = FindDescendant(drawing, "wp:docPr")) {
            altText = Attr(docPr, "descr");
            if (altText.empty()) altText = Attr(docPr, "name");
        }

        // <wp:inline> is a picture sitting in the line of text; <wp:anchor> is
        // one floating with text flowed around it. Only the first belongs in
        // the run stream - treating both as a trailing paragraph is what used
        // to pull a logo out of the middle of a sentence.
        if (drawing->FirstChildElement("wp:inline") != nullptr) {
            RichTextRun run;
            run.text = RichTextRun::kObjectReplacement;
            run.mediaIndex = mediaIndex;
            run.imageWidthPt = widthPt;
            run.imageHeightPt = heightPt;
            run.imageAltText = altText;
            run.lineBreakBefore = ctx.pendingLineBreak;
            ctx.pendingLineBreak = false;
            ctx.runs.push_back(std::move(run));
            return;
        }

        RichDocBlock block;
        block.type = RichBlockType::Image;
        block.mediaIndex = mediaIndex;
        block.imageWidthPt = widthPt;
        block.imageHeightPt = heightPt;
        block.imageAltText = altText;
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
                for (size_t at; (at = run.text.find(kKeptSpaceMarker)) != std::string::npos;) {
                    run.text.replace(at, 3, " ");
                }
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
            } else if (tag == "w:sym") {
                // Insert > Symbol: a character code in a named (usually
                // symbol) font; the import's symbol-font pass maps it.
                RichTextRun run = props;
                run.fontFamily = Attr(child, "w:font");
                const unsigned long code = std::strtoul(Attr(child, "w:char"), nullptr, 16);
                if (code > 0 && code < 0x110000) {
                    WordFormatInternal::AppendUtf8(run.text, static_cast<uint32_t>(code));
                    run.lineBreakBefore = ctx.pendingLineBreak;
                    ctx.pendingLineBreak = false;
                    ctx.runs.push_back(std::move(run));
                }
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
            } else if (tag == "m:oMath") {
                // Embedded equations become $latex$ text for the markdown
                // pipeline (which renders inline math).
                std::string latex = WordMath::OmmlToLatex(child);
                if (!latex.empty()) {
                    RichTextRun run;
                    run.text = "$" + latex + "$";
                    run.linkTarget = linkTarget;
                    run.lineBreakBefore = ctx.pendingLineBreak;
                    ctx.pendingLineBreak = false;
                    ctx.runs.push_back(std::move(run));
                }
            } else if (tag == "m:oMathPara") {
                ParseInlineContainer(child, linkTarget, ctx);
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
        int listNumber = 0;   // the number an ordered list item carries
        RichDocBlock block;
        block.type = RichBlockType::Paragraph;
        bool hasBottomBorder = false;

        ApplyGeometry(block, p->FirstChildElement("w:pPr"));
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
                    const std::string numIdValue = Attr(numId, "w:val");
                    auto abstractIt = numIdToAbstract_.find(numIdValue);
                    if (abstractIt != numIdToAbstract_.end()) {
                        auto levels = abstractNumOrdered_.find(abstractIt->second);
                        if (levels != abstractNumOrdered_.end()) {
                            auto lvlIt = levels->second.find(block.listLevel);
                            if (lvlIt != levels->second.end()) block.orderedList = lvlIt->second;
                        }
                    }
                    if (block.orderedList) {
                        const std::string listKey = abstractIt != numIdToAbstract_.end()
                                ? abstractIt->second : "num-" + numIdValue;
                        // A numId with a start override restarts the list the
                        // first time it is used ("restart numbering" in Word).
                        if (numIdsSeen_.insert(numIdValue).second) {
                            auto overrides = numStartOverride_.find(numIdValue);
                            if (overrides != numStartOverride_.end()) {
                                for (const auto& [level, start] : overrides->second) {
                                    numbering_.Restart(listKey, level, start);
                                }
                            }
                        }
                        int startAt = 1;
                        auto starts = abstractNumStart_.find(listKey);
                        if (starts != abstractNumStart_.end()) {
                            auto it = starts->second.find(block.listLevel);
                            if (it != starts->second.end()) startAt = it->second;
                        }
                        listNumber = numbering_.Next(listKey, block.listLevel, startAt);
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

        // A picture on a line of its own is a standalone image, not a run.
        RichDocBlock promoted;
        if (block.type == RichBlockType::Paragraph
            && WordFormatInternal::ParagraphIsOneInlineImage(block.runs, promoted)) {
            doc_->blocks.push_back(std::move(promoted));
            for (auto& image : ctx.trailingImages) doc_->blocks.push_back(std::move(image));
            return;
        }

        bool emptyParagraph = block.runs.empty() && ctx.trailingImages.empty()
                              && block.type == RichBlockType::Paragraph;
        bool imageOnly = block.runs.empty() && !ctx.trailingImages.empty();
        if (!imageOnly && !emptyParagraph) {
            doc_->blocks.push_back(std::move(block));
            if (listNumber > 0) {
                RichListNumbering::Apply(doc_->blocks, doc_->blocks.size() - 1, listNumber);
            }
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

    static RichTextAlign ParagraphAlignment(tinyxml2::XMLElement* p) {
        auto* pPr = p->FirstChildElement("w:pPr");
        auto* jc = pPr ? pPr->FirstChildElement("w:jc") : nullptr;
        if (!jc) return RichTextAlign::Default;
        std::string v = Attr(jc, "w:val");
        if (v == "center") return RichTextAlign::Center;
        if (v == "right" || v == "end") return RichTextAlign::Right;
        if (v == "both" || v == "distribute") return RichTextAlign::Justify;
        if (v == "left" || v == "start") return RichTextAlign::Left;
        return RichTextAlign::Default;
    }

    void ParseTable(tinyxml2::XMLElement* tbl) {
        RichDocBlock block;
        block.type = RichBlockType::Table;

        // A vertically merged cell appears as <w:vMerge w:val="restart"/> once
        // and then a plain <w:vMerge/> in every row it covers. The model holds
        // such a cell once, in its starting row, with rowSpan counting the
        // rows — so the continuations are not cells of their own, they just
        // grow the span of the cell that opened the merge. openMerge remembers
        // where that cell lives, keyed by the grid column it occupies.
        struct OpenMerge { size_t rowIndex; size_t cellIndex; };
        std::map<size_t, OpenMerge> openMerge;

        // Column proportions (twips) from the table grid.
        if (auto* grid = tbl->FirstChildElement("w:tblGrid")) {
            for (auto* col = grid->FirstChildElement("w:gridCol"); col;
                 col = col->NextSiblingElement("w:gridCol")) {
                block.tableColumnWidths.push_back(static_cast<float>(col->IntAttribute("w:w", 0)));
            }
            for (float w : block.tableColumnWidths) {
                if (w <= 0) { block.tableColumnWidths.clear(); break; }
            }
        }

        for (auto* tr = tbl->FirstChildElement("w:tr"); tr;
             tr = tr->NextSiblingElement("w:tr")) {
            RichTableRow row;
            if (auto* trPr = tr->FirstChildElement("w:trPr")) {
                row.header = trPr->FirstChildElement("w:tblHeader") != nullptr;
            }

            const size_t rowIndex = block.tableRows.size();
            size_t gridColumn = 0;
            for (auto* tc = tr->FirstChildElement("w:tc"); tc;
                 tc = tc->NextSiblingElement("w:tc")) {
                int columnSpan = 1;
                bool mergeRestart = false;
                bool mergeContinue = false;
                if (auto* tcPr = tc->FirstChildElement("w:tcPr")) {
                    if (auto* gridSpan = tcPr->FirstChildElement("w:gridSpan")) {
                        columnSpan = std::max(1, std::atoi(Attr(gridSpan, "w:val")));
                    }
                    if (auto* vMerge = tcPr->FirstChildElement("w:vMerge")) {
                        const std::string value = Attr(vMerge, "w:val");
                        // No value, or "continue", means this cell continues the
                        // merge above it; only "restart" opens a new one.
                        mergeRestart = (value == "restart");
                        mergeContinue = !mergeRestart;
                    }
                }

                if (mergeContinue) {
                    auto it = openMerge.find(gridColumn);
                    if (it != openMerge.end()
                        && it->second.rowIndex < block.tableRows.size()
                        && it->second.cellIndex
                               < block.tableRows[it->second.rowIndex].cells.size()) {
                        block.tableRows[it->second.rowIndex]
                            .cells[it->second.cellIndex].rowSpan++;
                    }
                    gridColumn += static_cast<size_t>(columnSpan);
                    continue;   // no cell of its own
                }

                RichTableCell cell;
                cell.columnSpan = columnSpan;
                InlineContext ctx;
                bool firstParagraph = true;
                for (auto* p = tc->FirstChildElement("w:p"); p;
                     p = p->NextSiblingElement("w:p")) {
                    if (!firstParagraph) ctx.pendingLineBreak = true;
                    // Alignment is a paragraph property; the cell takes its
                    // first paragraph's.
                    if (firstParagraph) cell.align = ParagraphAlignment(p);
                    firstParagraph = false;
                    ParseInlineContainer(p, "", ctx);
                }
                cell.runs = std::move(ctx.runs);

                if (mergeRestart) {
                    openMerge[gridColumn] = OpenMerge{rowIndex, row.cells.size()};
                } else {
                    openMerge.erase(gridColumn);
                }
                gridColumn += static_cast<size_t>(columnSpan);
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
        if (HasSettings() && !zip_.AddEntry("word/settings.xml", BuildSettingsXml())) {
            error = "Failed to write document settings: " + zip_.GetLastError();
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
    // Drawing ids only have to be unique within the document. Inline pictures
    // count from a high base so they cannot collide with the block images,
    // which number from 1.
    int inlineDrawingId_ = 100000;
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
            if (run.IsInlineImage()) {
                if (run.lineBreakBefore) xml << "<w:br/>";
                WriteDrawing(xml, run.mediaIndex, run.imageWidthPt, run.imageHeightPt,
                             run.imageAltText, ++inlineDrawingId_);
            } else {
                WriteRunProperties(xml, run, isLink);
                if (run.lineBreakBefore) xml << "<w:br/>";
                WriteRunText(xml, run.text);
            }
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

    static std::string Twips(float points) {
        return std::to_string(std::lround(points * 20.0f));
    }

    // w:tabs, w:spacing, w:ind - in the order CT_PPrBase requires them
    // (after w:pBdr, before w:jc).
    static void WriteGeometry(std::ostringstream& pPr, const RichDocBlock& block) {
        if (!block.tabStops.empty()) {
            pPr << "<w:tabs>";
            for (const RichTabStop& stop : block.tabStops) {
                const char* kind = stop.kind == RichTabKind::Center ? "center"
                                 : stop.kind == RichTabKind::Right ? "right"
                                 : stop.kind == RichTabKind::Decimal ? "decimal" : "left";
                pPr << "<w:tab w:val=\"" << kind << "\" w:pos=\"" << Twips(stop.positionPt) << "\"/>";
            }
            pPr << "</w:tabs>";
        }
        if (block.spaceBeforePt >= 0.0f || block.spaceAfterPt >= 0.0f || block.lineSpacing > 0.0f) {
            pPr << "<w:spacing";
            if (block.spaceBeforePt >= 0.0f) pPr << " w:before=\"" << Twips(block.spaceBeforePt) << "\"";
            if (block.spaceAfterPt >= 0.0f) pPr << " w:after=\"" << Twips(block.spaceAfterPt) << "\"";
            if (block.lineSpacing > 0.0f) {
                pPr << " w:line=\"" << std::to_string(std::lround(block.lineSpacing * 240.0f))
                    << "\" w:lineRule=\"auto\"";
            }
            pPr << "/>";
        }
        // List items take their indent from the numbering definition.
        if (block.type != RichBlockType::ListItem
            && (block.leftIndentPt != 0.0f || block.rightIndentPt != 0.0f || block.firstLineIndentPt != 0.0f)) {
            pPr << "<w:ind w:left=\"" << Twips(block.leftIndentPt) << "\" w:right=\""
                << Twips(block.rightIndentPt) << "\"";
            if (block.firstLineIndentPt > 0.0f) pPr << " w:firstLine=\"" << Twips(block.firstLineIndentPt) << "\"";
            if (block.firstLineIndentPt < 0.0f) pPr << " w:hanging=\"" << Twips(-block.firstLineIndentPt) << "\"";
            pPr << "/>";
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
        WriteGeometry(pPr, block);
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

    // The <w:drawing> element alone. A picture is the same markup whether it
    // is a paragraph of its own or sits inside a line; only the wrapping differs.
    void WriteDrawing(std::ostringstream& xml, int mediaIndex, float widthPtIn,
                      float heightPtIn, const std::string& altText, int drawingId) {
        if (mediaIndex < 0 || mediaIndex >= static_cast<int>(doc_->media.size())) return;
        RichDocBlock shim;
        shim.mediaIndex = mediaIndex;
        shim.imageWidthPt = widthPtIn;
        shim.imageHeightPt = heightPtIn;
        shim.imageAltText = altText;
        WriteDrawingElement(xml, shim, drawingId);
    }

    void WriteImage(std::ostringstream& xml, const RichDocBlock& block, int drawingId) {
        if (block.mediaIndex < 0 || block.mediaIndex >= static_cast<int>(doc_->media.size())) {
            return;
        }
        xml << "<w:p><w:r>";
        WriteDrawingElement(xml, block, drawingId);
        xml << "</w:r></w:p>\n";
    }

    void WriteDrawingElement(std::ostringstream& xml, const RichDocBlock& block, int drawingId) {
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

        xml << "<w:drawing>"
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
            << "</pic:pic></a:graphicData></a:graphic></wp:inline></w:drawing>";
    }

    static const char* JustificationFor(RichTextAlign align) {
        switch (align) {
            case RichTextAlign::Center: return "center";
            case RichTextAlign::Right: return "right";
            case RichTextAlign::Justify: return "both";
            case RichTextAlign::Left: return "left";
            default: return nullptr;
        }
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
        // Known proportions become twips across a 9000-twip (6.25in) table.
        const std::vector<float>& widths = block.tableColumnWidths;
        float totalWidth = 0.0f;
        for (float w : widths) totalWidth += std::max(0.0f, w);
        const bool widthsKnown = widths.size() == columnCount && totalWidth > 0.0f;
        for (size_t c = 0; c < columnCount; ++c) {
            if (widthsKnown) {
                const long twips = std::max(1L, std::lround(9000.0f * widths[c] / totalWidth));
                xml << "<w:gridCol w:w=\"" << std::to_string(twips) << "\"/>";
            } else {
                xml << "<w:gridCol/>";
            }
        }
        xml << "</w:tblGrid>\n";
        // Walk the grid, not the cell list: a row-spanning cell is written once
        // with <w:vMerge w:val="restart"/>, and every grid position it covers
        // below needs a real <w:tc> carrying a plain <w:vMerge/>. Word requires
        // those continuation cells to exist, unlike ODT's covered-cell marker.
        std::vector<int> rowSpanRemaining(columnCount, 0);
        for (const auto& row : block.tableRows) {
            xml << "<w:tr>";
            if (row.header) xml << "<w:trPr><w:tblHeader/></w:trPr>";

            size_t cellIndex = 0;
            for (size_t col = 0; col < columnCount; ) {
                if (rowSpanRemaining[col] > 0) {
                    xml << "<w:tc><w:tcPr><w:vMerge/></w:tcPr><w:p/></w:tc>";
                    rowSpanRemaining[col]--;
                    col++;
                    continue;
                }
                if (cellIndex >= row.cells.size()) break;   // a short row
                const RichTableCell& cell = row.cells[cellIndex++];
                const int columnSpan = std::max(1, cell.columnSpan);
                const int rowSpan = std::max(1, cell.rowSpan);
                if (rowSpan > 1) {
                    for (size_t c = col; c < col + static_cast<size_t>(columnSpan)
                                         && c < columnCount; c++) {
                        rowSpanRemaining[c] = rowSpan - 1;
                    }
                }
                col += static_cast<size_t>(columnSpan);

                xml << "<w:tc><w:tcPr>";
                if (columnSpan > 1) {
                    xml << "<w:gridSpan w:val=\"" << columnSpan << "\"/>";
                }
                if (rowSpan > 1) {
                    xml << "<w:vMerge w:val=\"restart\"/>";
                }
                xml << "</w:tcPr><w:p>";
                if (const char* jc = JustificationFor(cell.align)) {
                    xml << "<w:pPr><w:jc w:val=\"" << jc << "\"/></w:pPr>";
                }
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
                case RichBlockType::MathBlock:
                    // No OMML writer: the formula source travels as a centred
                    // "$$...$$" paragraph, which the Markdown pipeline typesets
                    // again after a re-import.
                    WriteParagraph(body, MathBlockAsParagraph(block));
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
        if (HasSettings()) {
            xml << "<Override PartName=\"/word/settings.xml\" "
                   "ContentType=\"application/vnd.openxmlformats-officedocument."
                   "wordprocessingml.settings+xml\"/>\n";
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

    // word/settings.xml carries only the default tab interval, so it is
    // written only when the document states one.
    bool HasSettings() const { return doc_->defaultTabStopPt > 0.0f; }

    std::string BuildSettingsXml() const {
        return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
               "<w:settings xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">"
               "<w:defaultTabStop w:val=\"" + Twips(doc_->defaultTabStopPt) + "\"/></w:settings>\n";
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
        if (HasSettings()) {
            xml << "<Relationship Id=\"rIdSettings\" "
                   "Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/settings\" "
                   "Target=\"settings.xml\"/>\n";
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
