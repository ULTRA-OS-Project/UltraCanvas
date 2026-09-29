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
#include "UltraCanvasTextUtils.h"

#include "tinyxml2.h"

#include <algorithm>
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
        // A picture placed from the page's edge needs the page's margin, which
        // the section at the end of the body states.
        if (auto* sectPr = body->FirstChildElement("w:sectPr")) {
            if (auto* margins = sectPr->FirstChildElement("w:pgMar")) {
                pageMarginLeftPt_ = static_cast<float>(margins->IntAttribute("w:left", 1440)) / 20.0f;
            }
        }
        for (auto* elem = body->FirstChildElement(); elem; elem = elem->NextSiblingElement()) {
            std::string tag = elem->Name() ? elem->Name() : "";
            if (tag == "w:p") ParseParagraph(elem);
            else if (tag == "w:tbl") ParseTable(elem);
            else if (tag == "w:bookmarkStart" && std::string(Attr(elem, "w:name")) != "_GoBack") {
                pendingBookmarks_.push_back(Attr(elem, "w:name"));
            } else if (tag == "w:sdt") {
                // A content control round the table of contents: its paragraphs.
                if (auto* content = elem->FirstChildElement("w:sdtContent")) {
                    for (auto* inner = content->FirstChildElement(); inner; inner = inner->NextSiblingElement()) {
                        std::string innerTag = inner->Name() ? inner->Name() : "";
                        if (innerTag == "w:p") ParseParagraph(inner);
                        else if (innerTag == "w:tbl") ParseTable(inner);
                    }
                }
            }
        }
        pendingBookmarks_.clear();
        LoadSection(body->FirstChildElement("w:sectPr"));
        LoadNotes(RichNote::Kind::Footnote);
        LoadNotes(RichNote::Kind::Endnote);
        LoadComments();
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
        float lineHeight = kUnset;          // exact / at-least, points
        bool lineHeightAtLeast = false;
        bool hasFrame = false;              // w:pBdr or w:shd stated
        RichBorder frame[4];                // top, bottom, left, right
        std::string background;
        // Tab changes in order: a stop to add, or (clear) one to remove.
        struct TabChange { RichTabStop stop; bool clear = false; };
        std::vector<TabChange> tabs;
        // The style's character size and font (w:rPr beside its w:pPr).
        float fontSize = kUnset;
        std::string fontFamily;
    };
    struct StyleGeometry {
        std::string basedOn;
        Geometry geometry;
    };
    static constexpr float kUnset = -1.0e9f;
    float pageMarginLeftPt_ = 72.0f;        // w:pgMar w:left, for page-relative pictures

    // ===== TABLE BORDERS =====
    // A table's six border positions; a side not stated stays unset so a
    // table's own borders can override its style's side by side.
    struct BorderSpec { bool set = false; RichBorder border; };
    struct TableBorders { BorderSpec top, left, bottom, right, insideH, insideV; };
    struct TableStyle { std::string basedOn; TableBorders borders; };

    // w:top/w:bottom/w:left(w:start)/w:right(w:end)/w:insideH/w:insideV:
    // w:val "nil"/"none" = no line, w:sz in eighths of a point.
    static BorderSpec ReadBorder(tinyxml2::XMLElement* e) {
        BorderSpec spec;
        if (!e) return spec;
        spec.set = true;
        const std::string val = Attr(e, "w:val");
        if (val == "nil" || val == "none" || val.empty()) return spec;
        spec.border.widthPt = std::max(0.25f, static_cast<float>(e->IntAttribute("w:sz", 4)) / 8.0f);
        spec.border.style = RichBorderStyleFromName(val);
        const std::string color = Attr(e, "w:color");
        if (color.size() == 6 && color != "auto") spec.border.color = "#" + color;
        return spec;
    }

    static void ReadTableBorders(tinyxml2::XMLElement* borders, TableBorders& out) {
        auto take = [&](BorderSpec& target, const char* a, const char* b) {
            BorderSpec spec = ReadBorder(borders->FirstChildElement(a));
            if (!spec.set && b) spec = ReadBorder(borders->FirstChildElement(b));
            if (spec.set) target = spec;
        };
        take(out.top, "w:top", nullptr);
        take(out.bottom, "w:bottom", nullptr);
        take(out.left, "w:left", "w:start");
        take(out.right, "w:right", "w:end");
        take(out.insideH, "w:insideH", nullptr);
        take(out.insideV, "w:insideV", nullptr);
    }

    static void OverlayBorders(TableBorders& base, const TableBorders& over) {
        for (auto [target, source] : {std::pair{&base.top, &over.top}, std::pair{&base.left, &over.left},
                                      std::pair{&base.bottom, &over.bottom}, std::pair{&base.right, &over.right},
                                      std::pair{&base.insideH, &over.insideH},
                                      std::pair{&base.insideV, &over.insideV}}) {
            if (source->set) *target = *source;
        }
    }

    TableBorders TableStyleBorders(const std::string& styleId, int depth = 0) const {
        auto it = tableStyles_.find(styleId);
        if (it == tableStyles_.end() || depth > 16) return TableBorders{};
        TableBorders borders = it->second.basedOn.empty() ? TableBorders{}
                                                           : TableStyleBorders(it->second.basedOn, depth + 1);
        OverlayBorders(borders, it->second.borders);
        return borders;
    }

    UCZipPackageReader zip_;
    UCRichDocument* doc_ = nullptr;
    tinyxml2::XMLDocument docXml_;
    std::map<std::string, DocxRelationship> relationships_;
    std::map<std::string, std::string> styleNames_;        // styleId -> display name
    std::map<std::string, StyleGeometry> styleGeometry_;   // paragraph styleId -> geometry
    Geometry defaultGeometry_;                             // w:docDefaults/w:pPrDefault
    std::string defaultParagraphStyle_;                    // applies when a paragraph names none
    std::map<std::string, TableStyle> tableStyles_;        // table styleId -> borders
    std::string defaultTableStyle_;
    std::map<std::string, std::string> numIdToAbstract_;
    std::map<std::string, std::map<int, bool>> abstractNumOrdered_;   // abstractId -> ilvl -> ordered
    std::map<std::string, std::map<int, int>> abstractNumStart_;      // abstractId -> ilvl -> w:start
    struct LevelLabel {
        RichNumberFormat format = RichNumberFormat::Decimal;
        std::string numberTemplate;
        std::string bulletText;
    };
    std::map<std::string, std::map<int, LevelLabel>> abstractLabels_;   // abstractId -> ilvl -> label
    // numId -> ilvl -> w:startOverride, applied when that numId is first used
    std::map<std::string, std::map<int, int>> numStartOverride_;
    std::set<std::string> numIdsSeen_;
    // Word counts per abstract list: every paragraph of that list advances
    // one counter per level, whatever paragraphs sit between them.
    RichListNumbering numbering_;
    std::map<std::string, int> mediaByTarget_;              // package path -> media index
    bool pendingPageBreak_ = false;
    // Footnote and endnote references met in the body: "f<id>" / "e<id>" ->
    // index into doc_->notes. Their text is read from the notes parts after
    // the body.
    std::map<std::string, int> noteIndexById_;
    // Bookmarks started between paragraphs, for the next one.
    std::vector<std::string> pendingBookmarks_;
    // Comments: w:id -> index into doc_->comments, and those whose range the
    // text being read is inside (ranges may cross paragraphs).
    std::map<std::string, int> commentIndexById_;
    std::vector<int> activeComments_;

    int CommentFor(const std::string& id) {
        auto found = commentIndexById_.find(id);
        if (found != commentIndexById_.end()) return found->second;
        doc_->comments.emplace_back();
        const int index = static_cast<int>(doc_->comments.size()) - 1;
        commentIndexById_[id] = index;
        return index;
    }

    // word/comments.xml: author, initials, date and text of each comment the
    // body marks.
    void LoadComments() {
        if (commentIndexById_.empty()) return;
        std::string partName = "word/comments.xml";
        for (const auto& [id, rel] : relationships_) {
            const std::string file = "comments.xml";
            if (!rel.external && rel.target.size() >= file.size()
                && rel.target.compare(rel.target.size() - file.size(), file.size(), file) == 0) {
                partName = ResolvePartPath(rel.target);
            }
        }
        std::string xml;
        if (!zip_.ReadEntry(partName, xml)) return;
        ProtectWhitespaceRuns(xml);
        tinyxml2::XMLDocument part;
        if (part.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS || !part.RootElement()) return;
        for (auto* c = part.RootElement()->FirstChildElement("w:comment"); c; c = c->NextSiblingElement("w:comment")) {
            auto found = commentIndexById_.find(Attr(c, "w:id"));
            if (found == commentIndexById_.end()) continue;
            RichComment& comment = doc_->comments[static_cast<size_t>(found->second)];
            comment.author = Attr(c, "w:author");
            comment.initials = Attr(c, "w:initials");
            comment.date = Attr(c, "w:date");
            std::string text;
            for (auto* p = c->FirstChildElement("w:p"); p; p = p->NextSiblingElement("w:p")) {
                if (p != c->FirstChildElement("w:p")) text += "\n";
                text += ElementText(p);
            }
            for (size_t at; (at = text.find(kKeptSpaceMarker)) != std::string::npos;) text.replace(at, 3, " ");
            comment.text = text;
        }
    }

    // The w:t text under an element, w:tab as a tab.
    static std::string ElementText(tinyxml2::XMLElement* e) {
        std::string text;
        for (auto* child = e->FirstChildElement(); child; child = child->NextSiblingElement()) {
            const std::string tag = child->Name() ? child->Name() : "";
            if (tag == "w:t") text += child->GetText() ? child->GetText() : "";
            else if (tag == "w:tab") text += "\t";
            else text += ElementText(child);
        }
        return text;
    }

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
            auto* rPrDefault = defaults->FirstChildElement("w:rPrDefault");
            ReadStyleFont(rPrDefault ? rPrDefault->FirstChildElement("w:rPr") : nullptr, defaultGeometry_);
        }
        for (auto* style = root->FirstChildElement("w:style"); style;
             style = style->NextSiblingElement("w:style")) {
            std::string id = Attr(style, "w:styleId");
            auto* name = style->FirstChildElement("w:name");
            if (!id.empty() && name) styleNames_[id] = Attr(name, "w:val");
            if (!id.empty() && std::string(Attr(style, "w:type")) == "table") {
                TableStyle tableStyle;
                if (auto* basedOn = style->FirstChildElement("w:basedOn")) tableStyle.basedOn = Attr(basedOn, "w:val");
                auto* tblPr = style->FirstChildElement("w:tblPr");
                if (auto* borders = tblPr ? tblPr->FirstChildElement("w:tblBorders") : nullptr) {
                    ReadTableBorders(borders, tableStyle.borders);
                }
                tableStyles_[id] = tableStyle;
                if (std::string(Attr(style, "w:default")) == "1") defaultTableStyle_ = id;
                continue;
            }
            // The document's named paragraph and character styles.
            const std::string type = Attr(style, "w:type");
            if (!id.empty() && (type == "paragraph" || type == "character")) {
                RichStyle named;
                named.id = id;
                named.name = name ? Attr(name, "w:val") : id;
                named.kind = type == "character" ? RichStyle::Kind::Character : RichStyle::Kind::Paragraph;
                if (auto* basedOn = style->FirstChildElement("w:basedOn")) named.basedOn = Attr(basedOn, "w:val");
                if (auto* next = style->FirstChildElement("w:next")) named.nextStyle = Attr(next, "w:val");
                named.character = ReadStyleCharacter(style->FirstChildElement("w:rPr"));
                if (named.kind == RichStyle::Kind::Paragraph) {
                    named.paragraph = ReadStyleParagraph(style->FirstChildElement("w:pPr"));
                    // Word's heading styles by name, whether or not they state
                    // an outline level.
                    const int level = HeadingLevelForStyle(id);
                    if (level > 0) named.paragraph.headingLevel = level;
                }
                doc_->styles.push_back(std::move(named));
            }
            if (id.empty() || std::string(Attr(style, "w:type")) != "paragraph") continue;
            StyleGeometry entry;
            if (auto* basedOn = style->FirstChildElement("w:basedOn")) entry.basedOn = Attr(basedOn, "w:val");
            if (auto* pPr = style->FirstChildElement("w:pPr")) ReadGeometry(pPr, entry.geometry);
            ReadStyleFont(style->FirstChildElement("w:rPr"), entry.geometry);
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
                g.lineHeight = 0.0f;
            } else if (spacing->Attribute("w:line") && (rule == "exact" || rule == "atLeast")) {
                g.lineHeight = static_cast<float>(spacing->IntAttribute("w:line", 240)) / 20.0f;
                g.lineHeightAtLeast = rule == "atLeast";
                g.lineSpacing = 0.0f;
            }
        }
        auto* pBdr = pPr->FirstChildElement("w:pBdr");
        auto* shd = pPr->FirstChildElement("w:shd");
        if (pBdr || shd) {
            g.hasFrame = true;
            if (pBdr) {
                const char* names[4] = {"w:top", "w:bottom", "w:left", "w:right"};
                const char* alternates[4] = {nullptr, nullptr, "w:start", "w:end"};
                for (int i = 0; i < 4; ++i) {
                    BorderSpec spec = ReadBorder(pBdr->FirstChildElement(names[i]));
                    if (!spec.set && alternates[i]) spec = ReadBorder(pBdr->FirstChildElement(alternates[i]));
                    g.frame[i] = spec.border;
                }
            }
            if (shd) {
                const std::string fill = Attr(shd, "w:fill");
                if (fill.size() == 6 && fill != "auto") g.background = "#" + fill;
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
        if (over.lineHeight != kUnset) {
            base.lineHeight = over.lineHeight;
            base.lineHeightAtLeast = over.lineHeightAtLeast;
        }
        if (over.hasFrame) {
            base.hasFrame = true;
            for (int i = 0; i < 4; ++i) base.frame[i] = over.frame[i];
            base.background = over.background;
        }
        base.tabs.insert(base.tabs.end(), over.tabs.begin(), over.tabs.end());
        take(base.fontSize, over.fontSize);
        if (!over.fontFamily.empty()) base.fontFamily = over.fontFamily;
    }

    static void ReadStyleFont(tinyxml2::XMLElement* rPr, Geometry& g) {
        if (!rPr) return;
        if (auto* sz = rPr->FirstChildElement("w:sz")) g.fontSize = sz->FloatAttribute("w:val", 0.0f) / 2.0f;
        if (auto* fonts = rPr->FirstChildElement("w:rFonts")) {
            const std::string family = Attr(fonts, "w:ascii");
            if (!family.empty()) g.fontFamily = family;
        }
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
        if (g.lineHeight != kUnset && g.lineHeight > 0.0f) {
            block.lineHeightPt = g.lineHeight;
            block.lineHeightAtLeast = g.lineHeightAtLeast;
        }
        if (g.hasFrame) {
            block.paragraphBorderTop = g.frame[0];
            block.paragraphBorderBottom = g.frame[1];
            block.paragraphBorderLeft = g.frame[2];
            block.paragraphBorderRight = g.frame[3];
            block.paragraphBackground = g.background;
        }
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
        // The style's size and font, which runs without their own take.
        block.paragraphFontSizePt = g.fontSize != kUnset ? g.fontSize : 0.0f;
        block.paragraphFontFamily = g.fontFamily;
    }

    // An empty paragraph's line is as tall as its paragraph mark's font
    // (w:pPr/w:rPr), which is what Word measures it with.
    static void ApplyParagraphMark(RichDocBlock& block, tinyxml2::XMLElement* pPr) {
        if (!block.runs.empty() || !pPr) return;
        Geometry mark;
        ReadStyleFont(pPr->FirstChildElement("w:rPr"), mark);
        if (mark.fontSize != kUnset && mark.fontSize > 0.0f) block.paragraphFontSizePt = mark.fontSize;
        if (!mark.fontFamily.empty()) block.paragraphFontFamily = mark.fontFamily;
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
                LevelLabel& label = abstractLabels_[abstractId][ilvl];
                auto* lvlText = lvl->FirstChildElement("w:lvlText");
                const std::string text = lvlText ? Attr(lvlText, "w:val") : "";
                if (fmt == "bullet") {
                    // The bullet, mapped when its font is a symbol font.
                    auto* rPr = lvl->FirstChildElement("w:rPr");
                    auto* fonts = rPr ? rPr->FirstChildElement("w:rFonts") : nullptr;
                    const std::string font = fonts ? Attr(fonts, "w:ascii") : "";
                    label.bulletText = text;
                    if (!font.empty() && !text.empty()) {
                        size_t at = 0;
                        const uint32_t cp = WordFormatInternal::DecodeUtf8(text, at);
                        if (uint32_t unicode = WordFormatInternal::SymbolFontCharToUnicode(font, cp)) {
                            label.bulletText.clear();
                            WordFormatInternal::AppendUtf8(label.bulletText, unicode);
                        }
                    }
                } else {
                    label.format = fmt == "lowerLetter" ? RichNumberFormat::LowerLetter
                                 : fmt == "upperLetter" ? RichNumberFormat::UpperLetter
                                 : fmt == "lowerRoman" ? RichNumberFormat::LowerRoman
                                 : fmt == "upperRoman" ? RichNumberFormat::UpperRoman
                                 : fmt == "decimalZero" ? RichNumberFormat::DecimalZero
                                 : fmt == "none" ? RichNumberFormat::NoNumber : RichNumberFormat::Decimal;
                    label.numberTemplate = text;   // Word's own "%1.%2." notation
                }
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
    // The named paragraph style a paragraph has ("" = the default one).
    std::string ParagraphStyleId(tinyxml2::XMLElement* p) const {
        auto* pPr = p ? p->FirstChildElement("w:pPr") : nullptr;
        auto* pStyle = pPr ? pPr->FirstChildElement("w:pStyle") : nullptr;
        const std::string id = pStyle ? Attr(pStyle, "w:val") : "";
        if (id.empty() || id == defaultParagraphStyle_ || !doc_->FindStyle(id)) return "";
        return id;
    }

    // What a paragraph's style (and the default style under it) gives its text.
    RichStyleCharacter ParagraphStyleCharacter(tinyxml2::XMLElement* p) const {
        auto* pPr = p ? p->FirstChildElement("w:pPr") : nullptr;
        auto* pStyle = pPr ? pPr->FirstChildElement("w:pStyle") : nullptr;
        const std::string id = pStyle ? Attr(pStyle, "w:val") : defaultParagraphStyle_;
        RichStyleCharacter character = doc_->ResolveStyle(id).character;
        // Sizes and fonts reach the text through the paragraph's own font
        // (paragraphFontSizePt / paragraphFontFamily), as before.
        character.fontSizePt.reset();
        character.fontFamily.reset();
        return character;
    }

    // A w:style's character properties.
    static RichStyleCharacter ReadStyleCharacter(tinyxml2::XMLElement* rPr) {
        RichStyleCharacter character;
        if (!rPr) return character;
        if (rPr->FirstChildElement("w:b")) character.bold = ToggleOn(rPr, "w:b");
        if (rPr->FirstChildElement("w:i")) character.italic = ToggleOn(rPr, "w:i");
        if (rPr->FirstChildElement("w:u")) character.underline = ToggleOn(rPr, "w:u");
        if (rPr->FirstChildElement("w:strike")) character.strikethrough = ToggleOn(rPr, "w:strike");
        if (auto* sz = rPr->FirstChildElement("w:sz")) character.fontSizePt = sz->FloatAttribute("w:val", 0.0f) / 2.0f;
        if (auto* fonts = rPr->FirstChildElement("w:rFonts")) {
            const std::string family = Attr(fonts, "w:ascii");
            if (!family.empty()) character.fontFamily = family;
        }
        if (auto* color = rPr->FirstChildElement("w:color")) {
            const std::string v = Attr(color, "w:val");
            if (v.size() == 6 && v != "auto") character.color = "#" + v;
        }
        return character;
    }

    // A w:style's paragraph properties.
    static RichStyleParagraph ReadStyleParagraph(tinyxml2::XMLElement* pPr) {
        RichStyleParagraph paragraph;
        if (!pPr) return paragraph;
        if (auto* jc = pPr->FirstChildElement("w:jc")) {
            const std::string v = Attr(jc, "w:val");
            paragraph.align = v == "center" ? RichTextAlign::Center
                            : (v == "right" || v == "end") ? RichTextAlign::Right
                            : (v == "both" || v == "distribute") ? RichTextAlign::Justify : RichTextAlign::Left;
        }
        if (auto* ind = pPr->FirstChildElement("w:ind")) {
            for (const char* name : {"w:left", "w:start"}) {
                if (ind->Attribute(name)) paragraph.leftIndentPt = static_cast<float>(ind->IntAttribute(name, 0)) / 20.0f;
            }
            for (const char* name : {"w:right", "w:end"}) {
                if (ind->Attribute(name)) paragraph.rightIndentPt = static_cast<float>(ind->IntAttribute(name, 0)) / 20.0f;
            }
            if (ind->Attribute("w:firstLine")) paragraph.firstLineIndentPt = static_cast<float>(ind->IntAttribute("w:firstLine", 0)) / 20.0f;
            if (ind->Attribute("w:hanging")) paragraph.firstLineIndentPt = -static_cast<float>(ind->IntAttribute("w:hanging", 0)) / 20.0f;
        }
        if (auto* spacing = pPr->FirstChildElement("w:spacing")) {
            if (spacing->Attribute("w:before")) paragraph.spaceBeforePt = static_cast<float>(spacing->IntAttribute("w:before", 0)) / 20.0f;
            if (spacing->Attribute("w:after")) paragraph.spaceAfterPt = static_cast<float>(spacing->IntAttribute("w:after", 0)) / 20.0f;
            const std::string rule = Attr(spacing, "w:lineRule");
            if (spacing->Attribute("w:line") && (rule.empty() || rule == "auto")) {
                paragraph.lineSpacing = static_cast<float>(spacing->IntAttribute("w:line", 240)) / 240.0f;
            }
        }
        if (auto* outline = pPr->FirstChildElement("w:outlineLvl")) {
            const int level = outline->IntAttribute("w:val", 9) + 1;
            if (level >= 1 && level <= 6) paragraph.headingLevel = level;
        }
        return paragraph;
    }

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
        // Complex field (w:fldChar begin / w:instrText / separate / end): the
        // result runs of a PAGE or NUMPAGES field are marked as that field.
        std::string fieldInstruction;
        bool inFieldResult = false;
        RichTextRun::Field field = RichTextRun::Field::Plain;
        std::string fieldArgument;           // SEQ's label, REF's / PAGEREF's bookmark
        // What the paragraph's style gives its text, under each run's own.
        RichStyleCharacter paragraphCharacter;
    };

    // PAGE -> page number, NUMPAGES / SECTIONPAGES -> page count, SEQ label
    // -> caption number, REF / PAGEREF bookmark -> cross-reference. The
    // label or bookmark goes to `argument`.
    static RichTextRun::Field FieldForInstruction(const std::string& instruction,
                                                  std::string* argument = nullptr) {
        std::vector<std::string> words;
        std::string word;
        for (char c : instruction + " ") {
            if (c == ' ' || c == '\t') {
                if (!word.empty()) words.push_back(word);
                word.clear();
                continue;
            }
            word.push_back(c);
        }
        if (words.empty()) return RichTextRun::Field::Plain;
        std::string name = words[0];
        for (char& c : name) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (argument) argument->clear();
        if (name == "PAGE") return RichTextRun::Field::PageNumber;
        if (name == "NUMPAGES" || name == "SECTIONPAGES") return RichTextRun::Field::PageCount;
        const bool sequence = name == "SEQ", reference = name == "REF", pageReference = name == "PAGEREF";
        if ((sequence || reference || pageReference) && words.size() > 1 && words[1][0] != '\\') {
            if (argument) *argument = words[1];
            return sequence ? RichTextRun::Field::Sequence
                 : reference ? RichTextRun::Field::Reference : RichTextRun::Field::PageReference;
        }
        return RichTextRun::Field::Plain;
    }

    static std::string HighlightColor(const std::string& name) {
        static const std::pair<const char*, const char*> colors[] = {
            {"yellow", "#FFFF00"}, {"green", "#00FF00"}, {"cyan", "#00FFFF"}, {"magenta", "#FF00FF"},
            {"blue", "#0000FF"}, {"red", "#FF0000"}, {"darkBlue", "#000080"}, {"darkCyan", "#008080"},
            {"darkGreen", "#008000"}, {"darkMagenta", "#800080"}, {"darkRed", "#800000"},
            {"darkYellow", "#808000"}, {"darkGray", "#808080"}, {"lightGray", "#C0C0C0"},
            {"black", "#000000"}, {"white", "#FFFFFF"}};
        for (const auto& [key, hex] : colors) {
            if (name == key) return hex;
        }
        return "";
    }

    void ParseRunProperties(tinyxml2::XMLElement* rPr, RichTextRun& run) const {
        if (!rPr) return;
        // A character style first; the run's own properties then override it.
        if (auto* rStyle = rPr->FirstChildElement("w:rStyle")) {
            const std::string id = Attr(rStyle, "w:val");
            if (doc_->FindStyle(id)) {
                run.characterStyleId = id;
                doc_->ResolveStyle(id).character.ApplyTo(run);
            }
        }
        // Only what is stated: the paragraph's style may have given the rest.
        if (rPr->FirstChildElement("w:b")) run.bold = ToggleOn(rPr, "w:b");
        if (rPr->FirstChildElement("w:i")) run.italic = ToggleOn(rPr, "w:i");
        if (rPr->FirstChildElement("w:strike")) run.strikethrough = ToggleOn(rPr, "w:strike");
        if (rPr->FirstChildElement("w:u")) run.underline = ToggleOn(rPr, "w:u");
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
        // A highlighter colour (one of Word's named ones), or character
        // shading, which may be any colour.
        if (auto* highlight = rPr->FirstChildElement("w:highlight")) {
            run.highlightColor = HighlightColor(Attr(highlight, "w:val"));
        }
        if (auto* shd = rPr->FirstChildElement("w:shd"); shd && run.highlightColor.empty()) {
            const std::string fill = Attr(shd, "w:fill");
            if (fill.size() == 6 && fill != "auto") run.highlightColor = "#" + fill;
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

    // An EMU count (integer, may be negative); 0 when absent.
    static long long ParseEmu(const char* text) {
        if (!text) return 0;
        return std::strtoll(text, nullptr, 10);     // integers only: no locale decimal point involved
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

        // <wp:anchor>: a floating picture. It stays in the run stream where it
        // is anchored, carrying how text wraps round it and where it sits.
        auto* anchor = drawing->FirstChildElement("wp:anchor");
        RichTextRun run;
        run.text = RichTextRun::kObjectReplacement;
        run.mediaIndex = mediaIndex;
        run.imageWidthPt = widthPt;
        run.imageHeightPt = heightPt;
        run.imageAltText = altText;
        run.imageWrap = RichTextRun::ImageWrap::Square;
        if (anchor) {
            if (anchor->FirstChildElement("wp:wrapTopAndBottom")) {
                run.imageWrap = RichTextRun::ImageWrap::TopAndBottom;
            } else if (anchor->FirstChildElement("wp:wrapNone")) {
                run.imageWrap = Attr(anchor, "behindDoc") == std::string("1")
                    ? RichTextRun::ImageWrap::BehindText : RichTextRun::ImageWrap::InFrontOfText;
            }
            if (auto* positionH = anchor->FirstChildElement("wp:positionH")) {
                const std::string from = Attr(positionH, "relativeFrom");
                if (auto* align = positionH->FirstChildElement("wp:align")) {
                    const std::string side = align->GetText() ? align->GetText() : "";
                    run.imageFloatAlign = side == "right" || side == "outside" ? RichTextAlign::Right
                                        : side == "center" ? RichTextAlign::Center : RichTextAlign::Left;
                } else if (auto* offset = positionH->FirstChildElement("wp:posOffset")) {
                    float x = static_cast<float>(ParseEmu(offset->GetText())) / kEmuPerPoint;
                    // From the page's edge: the text column starts a margin in.
                    if (from == "page") x -= pageMarginLeftPt_;
                    run.imageFloatAlign = RichTextAlign::Default;
                    run.imageOffsetXPt = std::max(0.0f, x);
                }
            }
            if (auto* positionV = anchor->FirstChildElement("wp:positionV")) {
                const std::string from = Attr(positionV, "relativeFrom");
                auto* offset = positionV->FirstChildElement("wp:posOffset");
                // Only an offset from the paragraph (or its line) has a meaning
                // once pages are laid out afresh; others sit at the paragraph.
                if (offset && (from == "paragraph" || from == "line")) {
                    run.imageOffsetYPt = static_cast<float>(ParseEmu(offset->GetText())) / kEmuPerPoint;
                }
            }
        }
        run.lineBreakBefore = ctx.pendingLineBreak;
        ctx.pendingLineBreak = false;
        ctx.runs.push_back(std::move(run));
    }

    void ParseRun(tinyxml2::XMLElement* runElem, const std::string& linkTarget,
                  InlineContext& ctx) {
        RichTextRun props;
        props.linkTarget = linkTarget;
        ctx.paragraphCharacter.ApplyTo(props);
        ParseRunProperties(runElem->FirstChildElement("w:rPr"), props);

        for (auto* child = runElem->FirstChildElement(); child;
             child = child->NextSiblingElement()) {
            std::string tag = child->Name() ? child->Name() : "";
            if (tag == "w:fldChar") {
                const std::string type = Attr(child, "w:fldCharType");
                if (type == "begin") {
                    ctx.fieldInstruction.clear();
                    ctx.inFieldResult = false;
                    ctx.field = RichTextRun::Field::Plain;
                } else if (type == "separate") {
                    ctx.field = FieldForInstruction(ctx.fieldInstruction, &ctx.fieldArgument);
                    ctx.inFieldResult = true;
                } else if (type == "end") {
                    ctx.inFieldResult = false;
                    ctx.field = RichTextRun::Field::Plain;
                }
            } else if (tag == "w:instrText") {
                if (child->GetText()) ctx.fieldInstruction += child->GetText();
            } else if (tag == "w:t") {
                RichTextRun run = props;
                if (ctx.inFieldResult || ctx.field != RichTextRun::Field::Plain) {
                    run.field = ctx.field;
                    if (run.field != RichTextRun::Field::Plain) run.fieldArgument = ctx.fieldArgument;
                }
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
            } else if (tag == "w:footnoteReference" || tag == "w:endnoteReference") {
                const bool endnote = tag == "w:endnoteReference";
                RichTextRun run = props;
                run.noteIndex = NoteFor(endnote ? RichNote::Kind::Endnote : RichNote::Kind::Footnote,
                                        Attr(child, "w:id"));
                run.superscript = true;
                run.text = "*";                       // numbered once all are read
                run.lineBreakBefore = ctx.pendingLineBreak;
                ctx.pendingLineBreak = false;
                ctx.runs.push_back(std::move(run));
            }
            // w:footnoteRef / w:endnoteRef, the mark inside a note's own
            // text, is drawn by the view: nothing to keep.
        }
    }

    void ParseInlineContainer(tinyxml2::XMLElement* container, const std::string& linkTarget,
                              InlineContext& ctx) {
        for (auto* child = container->FirstChildElement(); child;
             child = child->NextSiblingElement()) {
            std::string tag = child->Name() ? child->Name() : "";
            if (tag == "w:r") {
                const size_t before = ctx.runs.size();
                ParseRun(child, linkTarget, ctx);
                for (size_t r = before; r < ctx.runs.size(); r++) ctx.runs[r].commentIds = activeComments_;
            } else if (tag == "w:commentRangeStart") {
                const int index = CommentFor(Attr(child, "w:id"));
                if (std::find(activeComments_.begin(), activeComments_.end(), index) == activeComments_.end()) {
                    activeComments_.push_back(index);
                }
            } else if (tag == "w:commentRangeEnd") {
                const int index = CommentFor(Attr(child, "w:id"));
                activeComments_.erase(std::remove(activeComments_.begin(), activeComments_.end(), index),
                                      activeComments_.end());
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
            } else if (tag == "w:fldSimple") {
                // <w:fldSimple w:instr="PAGE"><w:r><w:t>3</w:t></w:r></w:fldSimple>
                const RichTextRun::Field saved = ctx.field;
                const std::string savedArgument = ctx.fieldArgument;
                ctx.field = FieldForInstruction(Attr(child, "w:instr"), &ctx.fieldArgument);
                ParseInlineContainer(child, linkTarget, ctx);
                ctx.field = saved;
                ctx.fieldArgument = savedArgument;
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
                    if (abstractIt != numIdToAbstract_.end()) {
                        auto labels = abstractLabels_.find(abstractIt->second);
                        if (labels != abstractLabels_.end()) {
                            auto label = labels->second.find(block.listLevel);
                            if (label != labels->second.end()) {
                                block.numberFormat = label->second.format;
                                block.numberTemplate = label->second.numberTemplate;
                                block.bulletText = label->second.bulletText;
                            }
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
        // Formatting a heading, quote or code block has by its kind (heading
        // bold, quote italics) stays the block's, not its runs' - as the ODT
        // reader does, so Markdown does not double it up.
        if (block.type != RichBlockType::Heading && block.type != RichBlockType::BlockQuote
            && block.type != RichBlockType::CodeBlock) {
            ctx.paragraphCharacter = ParagraphStyleCharacter(p);
        }
        block.styleId = ParagraphStyleId(p);
        // A table of contents entry: Word's "TOC 1".."TOC 9" styles (which
        // a file may use without defining).
        std::string rawStyle;
        if (auto* pPr = p->FirstChildElement("w:pPr")) {
            if (auto* pStyle = pPr->FirstChildElement("w:pStyle")) rawStyle = Attr(pStyle, "w:val");
        }
        if (!rawStyle.empty()) {
            const std::string styleName = ToLower(styleNames_.count(rawStyle) ? styleNames_[rawStyle] : rawStyle);
            for (const std::string& candidate : {styleName, ToLower(rawStyle)}) {
                if (candidate.rfind("toc", 0) == 0) {
                    const size_t digit = candidate.find_first_of("123456789");
                    if (digit != std::string::npos && candidate.find_first_not_of(" 0123456789", 3) == std::string::npos) {
                        block.tocLevel = candidate[digit] - '0';
                        break;
                    }
                }
            }
        }
        // Bookmarks started in the paragraph (or just before it) mark it.
        block.bookmarks = std::move(pendingBookmarks_);
        pendingBookmarks_.clear();
        for (auto* child = p->FirstChildElement("w:bookmarkStart"); child;
             child = child->NextSiblingElement("w:bookmarkStart")) {
            const std::string name = Attr(child, "w:name");
            if (!name.empty() && name != "_GoBack") block.bookmarks.push_back(name);
        }
        ParseInlineContainer(p, "", ctx);
        // A field's result may come in several runs ("Figure " "3"); the
        // model's field is one run, whose text an update replaces whole.
        for (size_t r = 1; r < ctx.runs.size();) {
            RichTextRun& previous = ctx.runs[r - 1];
            const RichTextRun& run = ctx.runs[r];
            if (run.field != RichTextRun::Field::Plain && run.field == previous.field
                && run.fieldArgument == previous.fieldArgument && !run.lineBreakBefore) {
                previous.text += run.text;
                ctx.runs.erase(ctx.runs.begin() + static_cast<std::ptrdiff_t>(r));
            } else {
                r++;
            }
        }
        block.runs = std::move(ctx.runs);
        ApplyParagraphMark(block, p->FirstChildElement("w:pPr"));

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
        std::map<std::pair<size_t, size_t>, TableBorders> cellBorders;   // (row, cell) -> w:tcBorders
        std::map<std::pair<size_t, size_t>, RichDocBlock> paragraphFrames; // (row, cell) -> first paragraph
        // Word's default cell margins (0.19 cm at the sides), then the table's.
        float tableMargins[4] = {0.0f, 0.0f, 5.4f, 5.4f};
        if (auto* tblPr = tbl->FirstChildElement("w:tblPr")) {
            ReadCellMargins(tblPr->FirstChildElement("w:tblCellMar"), tableMargins);
        }

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
                {
                    // Word's cell margins: the table's, then the cell's own.
                    float margins[4] = {tableMargins[0], tableMargins[1], tableMargins[2], tableMargins[3]};
                    auto* tcPr = tc->FirstChildElement("w:tcPr");
                    ReadCellMargins(tcPr ? tcPr->FirstChildElement("w:tcMar") : nullptr, margins);
                    cell.paddingTopPt = margins[0];
                    cell.paddingBottomPt = margins[1];
                    cell.paddingLeftPt = margins[2];
                    cell.paddingRightPt = margins[3];
                    if (auto* vAlign = tcPr ? tcPr->FirstChildElement("w:vAlign") : nullptr) {
                        const std::string v = Attr(vAlign, "w:val");
                        cell.verticalAlign = v == "center" ? RichVerticalAlign::Middle
                                           : v == "bottom" ? RichVerticalAlign::Bottom : RichVerticalAlign::Top;
                    }
                }
                if (auto* tcPr = tc->FirstChildElement("w:tcPr")) {
                    if (auto* borders = tcPr->FirstChildElement("w:tcBorders")) {
                        TableBorders own;
                        ReadTableBorders(borders, own);
                        cellBorders[{rowIndex, row.cells.size()}] = own;
                    }
                    if (auto* shd = tcPr->FirstChildElement("w:shd")) {
                        const std::string fill = Attr(shd, "w:fill");
                        if (fill.size() == 6 && fill != "auto") cell.backgroundColor = "#" + fill;
                    }
                }
                InlineContext ctx;
                bool firstParagraph = true;
                for (auto* p = tc->FirstChildElement("w:p"); p;
                     p = p->NextSiblingElement("w:p")) {
                    if (!firstParagraph) ctx.pendingLineBreak = true;
                    ctx.paragraphCharacter = ParagraphStyleCharacter(p);
                    // Alignment is a paragraph property; the cell takes its
                    // first paragraph's, and its frame where the cell has none.
                    if (firstParagraph) {
                        cell.align = ParagraphAlignment(p);
                        RichDocBlock paragraph;
                        ApplyGeometry(paragraph, p->FirstChildElement("w:pPr"));
                        if (paragraph.HasParagraphFrame()) paragraphFrames[{rowIndex, row.cells.size()}] = paragraph;
                    }
                    firstParagraph = false;
                    const size_t runsBefore = ctx.runs.size();
                    ParseInlineContainer(p, "", ctx);
                    // A cell keeps runs, not paragraphs: text without a size
                    // or font of its own takes its paragraph style's here.
                    RichDocBlock styled;
                    ApplyGeometry(styled, p->FirstChildElement("w:pPr"));
                    for (size_t r = runsBefore; r < ctx.runs.size(); ++r) {
                        RichTextRun& run = ctx.runs[r];
                        if (run.fontSizePt <= 0.0f) run.fontSizePt = styled.paragraphFontSizePt;
                        if (run.fontFamily.empty()) run.fontFamily = styled.paragraphFontFamily;
                    }
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
        ResolveCellBorders(tbl, block, cellBorders);
        ReadTablePlacement(tbl, block);
        // A cell holds text, not paragraphs: its paragraph's frame fills in
        // the sides the cell leaves open.
        for (const auto& [position, paragraph] : paragraphFrames) {
            if (position.first >= block.tableRows.size()
                || position.second >= block.tableRows[position.first].cells.size()) continue;
            RichTableCell& cell = block.tableRows[position.first].cells[position.second];
            const RichBorder* from[4] = {&paragraph.paragraphBorderTop, &paragraph.paragraphBorderBottom,
                                         &paragraph.paragraphBorderLeft, &paragraph.paragraphBorderRight};
            RichBorder* to[4] = {&cell.borderTop, &cell.borderBottom, &cell.borderLeft, &cell.borderRight};
            for (int i = 0; i < 4; ++i) {
                if (!to[i]->IsVisible() && from[i]->IsVisible()) *to[i] = *from[i];
            }
            if (cell.backgroundColor.empty()) cell.backgroundColor = paragraph.paragraphBackground;
        }
        if (!block.tableRows.empty()) doc_->blocks.push_back(std::move(block));
    }

    // Each cell's four sides: its own w:tcBorders where stated, else the
    // table's borders (w:tblBorders over the table style's) - the outer ones
    // on the table's edge, insideH/insideV between cells.
    // Cell margins: w:top/w:bottom/w:left(w:start)/w:right(w:end), twips.
    static void ReadCellMargins(tinyxml2::XMLElement* margins, float out[4]) {
        if (!margins) return;
        const char* names[4] = {"w:top", "w:bottom", "w:left", "w:right"};
        const char* alternates[4] = {nullptr, nullptr, "w:start", "w:end"};
        for (int i = 0; i < 4; ++i) {
            auto* e = margins->FirstChildElement(names[i]);
            if (!e && alternates[i]) e = margins->FirstChildElement(alternates[i]);
            if (e && (std::string(Attr(e, "w:type")) == "dxa" || !e->Attribute("w:type"))) {
                out[i] = static_cast<float>(e->IntAttribute("w:w", 0)) / 20.0f;
            }
        }
    }

    // Width (w:tblW), alignment (w:jc) and indent (w:tblInd) of a table. An
    // "auto" width is the sum of its grid columns, which is what Word draws.
    void ReadTablePlacement(tinyxml2::XMLElement* tbl, RichDocBlock& table) const {
        auto* tblPr = tbl->FirstChildElement("w:tblPr");
        if (auto* width = tblPr ? tblPr->FirstChildElement("w:tblW") : nullptr) {
            const std::string type = Attr(width, "w:type");
            const std::string value = Attr(width, "w:w");
            if (type == "dxa") {
                table.tableWidthPt = static_cast<float>(width->IntAttribute("w:w", 0)) / 20.0f;
            } else if (type == "pct") {
                float percent = 0.0f;
                if (!value.empty() && value.back() == '%') TryParseFloat(value.substr(0, value.size() - 1), percent);
                else percent = static_cast<float>(width->IntAttribute("w:w", 0)) / 50.0f;   // fiftieths
                table.tableWidthPercent = percent;
            }
        }
        if (table.tableWidthPt <= 0.0f && table.tableWidthPercent <= 0.0f) {
            float twips = 0.0f;
            for (float w : table.tableColumnWidths) twips += w;
            table.tableWidthPt = twips / 20.0f;
        }
        if (auto* jc = tblPr ? tblPr->FirstChildElement("w:jc") : nullptr) {
            const std::string v = Attr(jc, "w:val");
            table.tableAlign = v == "center" ? RichTextAlign::Center
                             : (v == "right" || v == "end") ? RichTextAlign::Right : RichTextAlign::Left;
        }
        if (auto* indent = tblPr ? tblPr->FirstChildElement("w:tblInd") : nullptr) {
            table.tableIndentPt = static_cast<float>(indent->IntAttribute("w:w", 0)) / 20.0f;
        }
    }

    void ResolveCellBorders(tinyxml2::XMLElement* tbl, RichDocBlock& table,
                            const std::map<std::pair<size_t, size_t>, TableBorders>& cellBorders) const {
        table.tableBordersFromDocument = true;
        auto* tblPr = tbl->FirstChildElement("w:tblPr");
        std::string styleId = defaultTableStyle_;
        if (auto* tblStyle = tblPr ? tblPr->FirstChildElement("w:tblStyle") : nullptr) {
            styleId = Attr(tblStyle, "w:val");
        }
        TableBorders borders = TableStyleBorders(styleId);
        if (auto* own = tblPr ? tblPr->FirstChildElement("w:tblBorders") : nullptr) {
            TableBorders direct;
            ReadTableBorders(own, direct);
            OverlayBorders(borders, direct);
        }
        const RichTableGrid grid = BuildTableGrid(table);
        for (size_t r = 0; r < table.tableRows.size(); ++r) {
            for (size_t c = 0; c < table.tableRows[r].cells.size(); ++c) {
                RichTableCell& cell = table.tableRows[r].cells[c];
                int top = 0, left = 0;
                if (!grid.OriginOf(static_cast<int>(r), static_cast<int>(c), top, left)) continue;
                const int bottom = top + std::max(1, cell.rowSpan) - 1;
                const int right = left + std::max(1, cell.columnSpan) - 1;
                auto pick = [](const BorderSpec& own, const BorderSpec& table) {
                    return own.set ? own.border : table.border;
                };
                auto it = cellBorders.find({r, c});
                const TableBorders none;
                const TableBorders& own = it != cellBorders.end() ? it->second : none;
                cell.borderTop = pick(own.top, top == 0 ? borders.top : borders.insideH);
                cell.borderBottom = pick(own.bottom, bottom >= grid.rowCount - 1 ? borders.bottom : borders.insideH);
                cell.borderLeft = pick(own.left, left == 0 ? borders.left : borders.insideV);
                cell.borderRight = pick(own.right, right >= grid.columnCount - 1 ? borders.right : borders.insideV);
            }
        }
    }

    // The body's section: page size and margins, and the header and footer
    // parts it references (w:titlePg gives the first page its own).
    void LoadSection(tinyxml2::XMLElement* sectPr) {
        if (!sectPr) return;
        auto twips = [](tinyxml2::XMLElement* e, const char* name) {
            return e ? static_cast<float>(e->IntAttribute(name, 0)) / 20.0f : 0.0f;
        };
        RichPageSetup& page = doc_->page;
        auto* size = sectPr->FirstChildElement("w:pgSz");
        page.widthPt = twips(size, "w:w");
        page.heightPt = twips(size, "w:h");
        auto* margins = sectPr->FirstChildElement("w:pgMar");
        page.marginTopPt = twips(margins, "w:top");
        page.marginBottomPt = twips(margins, "w:bottom");
        page.marginLeftPt = twips(margins, "w:left");
        page.marginRightPt = twips(margins, "w:right");
        page.headerTopPt = twips(margins, "w:header");
        page.footerBottomPt = twips(margins, "w:footer");
        // A negative top/bottom margin means "exactly, whatever the header".
        page.marginTopPt = std::abs(page.marginTopPt);
        page.marginBottomPt = std::abs(page.marginBottomPt);

        doc_->firstPageDiffers = sectPr->FirstChildElement("w:titlePg") != nullptr;
        for (const char* kind : {"w:headerReference", "w:footerReference"}) {
            const bool header = std::string(kind) == "w:headerReference";
            for (auto* ref = sectPr->FirstChildElement(kind); ref; ref = ref->NextSiblingElement(kind)) {
                const std::string type = Attr(ref, "w:type");
                RichPageFurniture* target = type == "first" ? &doc_->firstPageFurniture
                                          : (type == "default" || type.empty()) ? &doc_->pageFurniture : nullptr;
                if (!target) continue;   // even pages: not modelled
                auto rel = relationships_.find(Attr(ref, "r:id"));
                if (rel == relationships_.end()) continue;
                LoadHeaderFooterPart("word/" + rel->second.target, header ? target->header : target->footer);
            }
        }
        if (!doc_->firstPageDiffers) doc_->firstPageFurniture = RichPageFurniture{};
    }

    // A header or footer part: its paragraphs and tables, read like the body.
    void LoadHeaderFooterPart(const std::string& partName, std::vector<RichDocBlock>& out) {
        std::string xml;
        if (!zip_.ReadEntry(partName, xml)) return;
        ProtectWhitespaceRuns(xml);
        tinyxml2::XMLDocument part;
        if (part.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS) return;
        auto* root = part.RootElement();
        if (!root) return;
        const size_t start = doc_->blocks.size();
        for (auto* elem = root->FirstChildElement(); elem; elem = elem->NextSiblingElement()) {
            std::string tag = elem->Name() ? elem->Name() : "";
            if (tag == "w:p") ParseParagraph(elem);
            else if (tag == "w:tbl") ParseTable(elem);
        }
        out.assign(std::make_move_iterator(doc_->blocks.begin() + static_cast<std::ptrdiff_t>(start)),
                   std::make_move_iterator(doc_->blocks.end()));
        doc_->blocks.resize(start);
        // An empty paragraph Word keeps in every header is not content.
        while (!out.empty() && out.back().type == RichBlockType::Paragraph && out.back().runs.empty()) out.pop_back();
    }

    int NoteFor(RichNote::Kind kind, const std::string& id) {
        const std::string key = (kind == RichNote::Kind::Endnote ? "e" : "f") + id;
        auto found = noteIndexById_.find(key);
        if (found != noteIndexById_.end()) return found->second;
        RichNote note;
        note.kind = kind;
        doc_->notes.push_back(std::move(note));
        const int index = static_cast<int>(doc_->notes.size()) - 1;
        noteIndexById_[key] = index;
        return index;
    }

    // word/footnotes.xml or word/endnotes.xml: each referenced note's
    // paragraphs, read like the body's.
    void LoadNotes(RichNote::Kind kind) {
        const bool endnote = kind == RichNote::Kind::Endnote;
        const char* prefix = endnote ? "e" : "f";
        bool any = false;
        for (const auto& [key, index] : noteIndexById_) any = any || key[0] == prefix[0];
        if (!any) return;
        std::string partName = endnote ? "word/endnotes.xml" : "word/footnotes.xml";
        for (const auto& [id, rel] : relationships_) {
            const std::string file = endnote ? "endnotes.xml" : "footnotes.xml";
            if (!rel.external && rel.target.size() >= file.size()
                && rel.target.compare(rel.target.size() - file.size(), file.size(), file) == 0) {
                partName = ResolvePartPath(rel.target);
            }
        }
        std::string xml;
        if (!zip_.ReadEntry(partName, xml)) return;
        ProtectWhitespaceRuns(xml);
        tinyxml2::XMLDocument part;
        if (part.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS) return;
        auto* root = part.RootElement();
        if (!root) return;
        const char* noteTag = endnote ? "w:endnote" : "w:footnote";
        for (auto* note = root->FirstChildElement(noteTag); note; note = note->NextSiblingElement(noteTag)) {
            auto found = noteIndexById_.find(prefix + std::string(Attr(note, "w:id")));
            if (found == noteIndexById_.end()) continue;     // separators, unreferenced notes
            const size_t start = doc_->blocks.size();
            for (auto* elem = note->FirstChildElement(); elem; elem = elem->NextSiblingElement()) {
                std::string tag = elem->Name() ? elem->Name() : "";
                if (tag == "w:p") ParseParagraph(elem);
                else if (tag == "w:tbl") ParseTable(elem);
            }
            std::vector<RichDocBlock> blocks(std::make_move_iterator(doc_->blocks.begin() + static_cast<std::ptrdiff_t>(start)),
                                             std::make_move_iterator(doc_->blocks.end()));
            doc_->blocks.resize(start);
            // The space Word puts after the note's own mark.
            if (!blocks.empty() && !blocks.front().runs.empty()) {
                std::string& text = blocks.front().runs.front().text;
                const size_t lead = text.find_first_not_of(' ');
                text.erase(0, lead == std::string::npos ? text.size() : lead);
                if (text.empty()) blocks.front().runs.erase(blocks.front().runs.begin());
            }
            if (blocks.empty()) blocks.emplace_back();
            if (found->second < static_cast<int>(doc_->notes.size())) {
                doc_->notes[static_cast<size_t>(found->second)].blocks = std::move(blocks);
            }
        }
        doc_->UpdateNoteMarks();
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
        for (const FurniturePart& part : furnitureParts_) {
            if (!zip_.AddEntry("word/" + part.name, part.xml)
                || !zip_.AddEntry("word/_rels/" + part.name + ".rels", BuildPartRelsXml())) {
                error = "Failed to write a header or footer: " + zip_.GetLastError();
                return false;
            }
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
    int drawingId_ = 0;
    const std::vector<RichDocBlock>* blocks_ = nullptr;   // the blocks being written (body or furniture)
    std::vector<std::string> hyperlinks_;   // index -> URL; rel id = rIdLink{index+1}
    bool usesLists_ = false;
    std::vector<std::string> listDefinitions_;   // w:abstractNum per list run
    int currentListNumId_ = 1;

    std::string MediaPartName(size_t mediaIndex) const {
        const RichDocMedia& m = doc_->media[mediaIndex];
        std::string ext = UCRichDocument::FileExtensionForMimeType(m.mimeType);
        return "word/media/image" + std::to_string(mediaIndex + 1) + "." + ext;
    }

    int bookmarkId_ = 0;

    int HyperlinkRelIndex(const std::string& url) {
        for (size_t i = 0; i < hyperlinks_.size(); ++i) {
            if (hyperlinks_[i] == url) return static_cast<int>(i);
        }
        hyperlinks_.push_back(url);
        return static_cast<int>(hyperlinks_.size()) - 1;
    }

    // What the styles of the paragraph being written give its text: a run
    // that is *not* bold where they are must say so, or Word shows it bold.
    RichStyleCharacter paragraphBase_;

    RichStyleCharacter ParagraphBase(const RichDocBlock& block) const {
        std::string id = block.styleId;
        if (id.empty() || !doc_->FindStyle(id)) {
            id = block.type == RichBlockType::Heading ? "Heading" + std::to_string(std::clamp(block.headingLevel, 1, 6))
               : block.type == RichBlockType::BlockQuote ? "Quote" : "Normal";
        }
        if (doc_->FindStyle(id)) {
            RichStyleCharacter base = doc_->ResolveStyle(id).character;
            // A heading's bold and a quote's italics belong to the block's
            // kind (its runs are read without them): never switched off here.
            if (block.type == RichBlockType::Heading) base.bold.reset();
            if (block.type == RichBlockType::BlockQuote) base.italic.reset();
            return base;
        }
        return RichStyleCharacter{};              // the writer's own styles: nothing to undo
    }

    void WriteRunProperties(std::ostringstream& xml, const RichTextRun& run, bool asHyperlink) const {
        RichStyleCharacter base = paragraphBase_;
        if (!run.characterStyleId.empty() && doc_->FindStyle(run.characterStyleId)) {
            base.Overlay(doc_->ResolveStyle(run.characterStyleId).character);
        }
        const bool boldOff = !run.bold && base.bold.value_or(false);
        const bool italicOff = !run.italic && base.italic.value_or(false);
        const bool strikeOff = !run.strikethrough && base.strikethrough.value_or(false);
        const bool underlineOff = !run.underline && !asHyperlink && base.underline.value_or(false);
        bool hasProps = run.bold || run.italic || run.underline || run.strikethrough
                        || run.code || run.subscript || run.superscript
                        || !run.color.empty() || !run.fontFamily.empty()
                        || run.fontSizePt > 0 || !run.highlightColor.empty() || asHyperlink
                        || !run.characterStyleId.empty() || boldOff || italicOff || strikeOff || underlineOff;
        if (!hasProps) return;
        xml << "<w:rPr>";
        // The character style first (CT_RPr's order); the run's own
        // properties after it are what it looks like in any case.
        if (!run.characterStyleId.empty()) xml << "<w:rStyle w:val=\"" << EscapeXml(run.characterStyleId) << "\"/>";
        if (run.code) {
            xml << "<w:rFonts w:ascii=\"Courier New\" w:hAnsi=\"Courier New\"/>";
        } else if (!run.fontFamily.empty()) {
            xml << "<w:rFonts w:ascii=\"" << EscapeXml(run.fontFamily)
                << "\" w:hAnsi=\"" << EscapeXml(run.fontFamily) << "\"/>";
        }
        // In the order CT_RPr requires: b, i, strike, color, sz, u, shd,
        // vertAlign (Word rejects a run whose properties are out of order).
        if (run.bold) xml << "<w:b/>";
        else if (boldOff) xml << "<w:b w:val=\"0\"/>";
        if (run.italic) xml << "<w:i/>";
        else if (italicOff) xml << "<w:i w:val=\"0\"/>";
        if (run.strikethrough) xml << "<w:strike/>";
        else if (strikeOff) xml << "<w:strike w:val=\"0\"/>";
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
        if (run.underline || asHyperlink) xml << "<w:u w:val=\"single\"/>";
        else if (underlineOff) xml << "<w:u w:val=\"none\"/>";
        // Any highlight colour, as character shading (w:highlight knows only
        // sixteen named colours).
        if (run.highlightColor.size() == 7) {
            xml << "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\""
                << EscapeXml(run.highlightColor.substr(1)) << "\"/>";
        }
        if (run.superscript) xml << "<w:vertAlign w:val=\"superscript\"/>";
        if (run.subscript) xml << "<w:vertAlign w:val=\"subscript\"/>";
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

    static std::string FieldInstruction(const RichTextRun& run) {
        switch (run.field) {
            case RichTextRun::Field::PageNumber: return " PAGE ";
            case RichTextRun::Field::PageCount: return " NUMPAGES ";
            case RichTextRun::Field::Sequence: return " SEQ " + run.fieldArgument + " \\* ARABIC ";
            case RichTextRun::Field::Reference: return " REF " + run.fieldArgument + " \\h ";
            case RichTextRun::Field::PageReference: return " PAGEREF " + run.fieldArgument + " \\h ";
            default: return "";
        }
    }

    // Comment ranges open where the text being written is: a range starts at
    // the first run under the comment and ends before the first after it
    // that is not, which may be paragraphs later.
    std::vector<int> openComments_;
    std::set<int> writtenComments_;

    static void WriteCommentEnd(std::ostringstream& xml, int id) {
        xml << "<w:commentRangeEnd w:id=\"" << id << "\"/><w:r>"
            << "<w:commentReference w:id=\"" << id << "\"/></w:r>";
    }

    void UpdateOpenComments(std::ostringstream& xml, const std::vector<int>& wanted) {
        for (size_t i = 0; i < openComments_.size();) {
            const int id = openComments_[i];
            if (std::find(wanted.begin(), wanted.end(), id) == wanted.end()) {
                WriteCommentEnd(xml, id);
                openComments_.erase(openComments_.begin() + static_cast<std::ptrdiff_t>(i));
            } else {
                i++;
            }
        }
        for (int id : wanted) {
            if (id < 0 || id >= static_cast<int>(doc_->comments.size())) continue;
            if (std::find(openComments_.begin(), openComments_.end(), id) != openComments_.end()) continue;
            if (writtenComments_.count(id)) continue;       // one range per comment
            xml << "<w:commentRangeStart w:id=\"" << id << "\"/>";
            openComments_.push_back(id);
            writtenComments_.insert(id);
        }
    }

    std::string BuildCommentsXml() const {
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:comments " << kPartNamespaces << ">";
        for (int id : writtenComments_) {
            const RichComment& comment = doc_->comments[static_cast<size_t>(id)];
            xml << "<w:comment w:id=\"" << id << "\" w:author=\"" << EscapeXml(comment.author) << "\"";
            if (!comment.date.empty()) xml << " w:date=\"" << EscapeXml(comment.date) << "\"";
            if (!comment.initials.empty()) xml << " w:initials=\"" << EscapeXml(comment.initials) << "\"";
            xml << ">";
            size_t start = 0;
            do {
                const size_t end = comment.text.find('\n', start);
                const std::string line = comment.text.substr(start, end == std::string::npos ? std::string::npos : end - start);
                xml << "<w:p><w:r><w:t xml:space=\"preserve\">" << EscapeXml(line) << "</w:t></w:r></w:p>";
                start = end == std::string::npos ? std::string::npos : end + 1;
            } while (start != std::string::npos);
            xml << "</w:comment>";
        }
        xml << "</w:comments>\n";
        return xml.str();
    }

    void WriteRuns(std::ostringstream& xml, const std::vector<RichTextRun>& runs) {
        for (const auto& run : runs) {
            UpdateOpenComments(xml, run.commentIds);
            bool isLink = !run.linkTarget.empty();
            if (isLink && run.linkTarget[0] == '#') {
                // A link inside the document, to a bookmark.
                xml << "<w:hyperlink w:anchor=\"" << EscapeXml(run.linkTarget.substr(1)) << "\">";
            } else if (isLink) {
                xml << "<w:hyperlink r:id=\"rIdLink"
                    << (HyperlinkRelIndex(run.linkTarget) + 1) << "\">";
            }
            if (run.field != RichTextRun::Field::Plain) {
                // A page number or count: a simple field showing its last value.
                xml << "<w:fldSimple w:instr=\"" << EscapeXml(FieldInstruction(run)) << "\"><w:r>";
                WriteRunProperties(xml, run, false);
                WriteRunText(xml, run.text);
                xml << "</w:r></w:fldSimple>";
                if (isLink) xml << "</w:hyperlink>";
                continue;
            }
            if (run.IsNoteReference()) {
                auto id = noteIds_.find(run.noteIndex);
                if (id != noteIds_.end()) {
                    const bool endnote = doc_->notes[static_cast<size_t>(run.noteIndex)].kind == RichNote::Kind::Endnote;
                    xml << "<w:r>";
                    WriteRunProperties(xml, run, false);
                    if (run.lineBreakBefore) xml << "<w:br/>";
                    xml << (endnote ? "<w:endnoteReference w:id=\"" : "<w:footnoteReference w:id=\"")
                        << id->second << "\"/></w:r>";
                }
                if (isLink) xml << "</w:hyperlink>";
                continue;
            }
            xml << "<w:r>";
            if (run.IsInlineImage()) {
                if (run.lineBreakBefore) xml << "<w:br/>";
                WriteDrawing(xml, run.mediaIndex, run.imageWidthPt, run.imageHeightPt,
                             run.imageAltText, ++inlineDrawingId_,
                             run.IsFloatingImage() ? &run : nullptr);
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
        if (block.HasParagraphFrame()) {
            auto side = [&](const char* name, const RichBorder& border) {
                if (!border.IsVisible()) return;
                const long eighths = std::max(2L, std::lround(border.widthPt * 8.0f));
                const std::string color = border.color.size() == 7 ? border.color.substr(1) : "auto";
                pPr << "<w:" << name << " w:val=\"" << RichBorderStyleWordName(border.style) << "\" w:sz=\"" << std::to_string(eighths)
                    << "\" w:space=\"4\" w:color=\"" << EscapeXml(color) << "\"/>";
            };
            if (block.paragraphBorderTop.IsVisible() || block.paragraphBorderBottom.IsVisible()
                || block.paragraphBorderLeft.IsVisible() || block.paragraphBorderRight.IsVisible()) {
                pPr << "<w:pBdr>";
                side("top", block.paragraphBorderTop);
                side("left", block.paragraphBorderLeft);
                side("bottom", block.paragraphBorderBottom);
                side("right", block.paragraphBorderRight);
                pPr << "</w:pBdr>";
            }
            if (block.paragraphBackground.size() == 7) {
                pPr << "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\""
                    << EscapeXml(block.paragraphBackground.substr(1)) << "\"/>";
            }
        }
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
        if (block.spaceBeforePt >= 0.0f || block.spaceAfterPt >= 0.0f || block.lineSpacing > 0.0f
            || block.lineHeightPt > 0.0f) {
            pPr << "<w:spacing";
            if (block.spaceBeforePt >= 0.0f) pPr << " w:before=\"" << Twips(block.spaceBeforePt) << "\"";
            if (block.spaceAfterPt >= 0.0f) pPr << " w:after=\"" << Twips(block.spaceAfterPt) << "\"";
            if (block.lineHeightPt > 0.0f) {
                pPr << " w:line=\"" << Twips(block.lineHeightPt) << "\" w:lineRule=\""
                    << (block.lineHeightAtLeast ? "atLeast" : "exact") << "\"";
            } else if (block.lineSpacing > 0.0f) {
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
        paragraphBase_ = ParagraphBase(block);
        xml << "<w:p>";
        std::ostringstream pPr;
        // The paragraph's own named style, when it has one the file carries.
        const bool named = !block.styleId.empty() && doc_->FindStyle(block.styleId);
        if (named && block.type != RichBlockType::ListItem && block.type != RichBlockType::HorizontalRule) {
            pPr << "<w:pStyle w:val=\"" << EscapeXml(block.styleId) << "\"/>";
        } else if (block.tocLevel > 0 && block.type == RichBlockType::Paragraph) {
            pPr << "<w:pStyle w:val=\"TOC" << std::clamp(block.tocLevel, 1, 9) << "\"/>";
        } else if (block.type == RichBlockType::Heading) {
            pPr << "<w:pStyle w:val=\"Heading" << std::clamp(block.headingLevel, 1, 6)
                << "\"/>";
        } else if (block.type == RichBlockType::BlockQuote) {
            pPr << "<w:pStyle w:val=\"Quote\"/>";
        } else if (block.type == RichBlockType::CodeBlock) {
            pPr << "<w:pStyle w:val=\"CodeBlock\"/>";
        } else if (block.type == RichBlockType::ListItem) {
            usesLists_ = true;
            pPr << "<w:pStyle w:val=\"ListParagraph\"/><w:numPr><w:ilvl w:val=\""
                << std::clamp(block.listLevel, 0, 8) << "\"/><w:numId w:val=\""
                << currentListNumId_ << "\"/></w:numPr>";
        } else if (block.type == RichBlockType::HorizontalRule) {
            pPr << "<w:pBdr><w:bottom w:val=\"single\" w:sz=\"6\" w:space=\"1\" "
                   "w:color=\"808080\"/></w:pBdr>";
        }
        WriteGeometry(pPr, block);
        if (const char* jc = JcValue(block.align)) {
            pPr << "<w:jc w:val=\"" << jc << "\"/>";
        }
        // The paragraph mark carries the paragraph's font, which is what
        // an empty paragraph is measured with.
        if (block.paragraphFontSizePt > 0.0f || !block.paragraphFontFamily.empty()) {
            pPr << "<w:rPr>";
            if (!block.paragraphFontFamily.empty()) {
                pPr << "<w:rFonts w:ascii=\"" << EscapeXml(block.paragraphFontFamily) << "\" w:hAnsi=\""
                    << EscapeXml(block.paragraphFontFamily) << "\"/>";
            }
            if (block.paragraphFontSizePt > 0.0f) {
                pPr << "<w:sz w:val=\"" << std::lround(block.paragraphFontSizePt * 2.0f) << "\"/>";
            }
            pPr << "</w:rPr>";
        }
        std::string pPrStr = pPr.str();
        if (!pPrStr.empty()) xml << "<w:pPr>" << pPrStr << "</w:pPr>";
        for (const std::string& name : block.bookmarks) {
            const int id = ++bookmarkId_;
            xml << "<w:bookmarkStart w:id=\"" << id << "\" w:name=\"" << EscapeXml(name)
                << "\"/><w:bookmarkEnd w:id=\"" << id << "\"/>";
        }

        if (block.type == RichBlockType::CodeBlock) {
            // Collapse per-line runs into one monospace run with breaks.
            RichTextRun run;
            run.text = UCRichDocument::ConcatenateRunText(block.runs);
            run.code = true;
            WriteRuns(xml, {run});
        } else if (block.paragraphFontSizePt > 0.0f || !block.paragraphFontFamily.empty()) {
            // Word's runs take their size from the style, not the paragraph,
            // so text without its own size or font carries the paragraph's.
            std::vector<RichTextRun> runs = block.runs;
            for (RichTextRun& run : runs) {
                if (run.fontSizePt <= 0.0f) run.fontSizePt = block.paragraphFontSizePt;
                if (run.fontFamily.empty() && !run.code) run.fontFamily = block.paragraphFontFamily;
            }
            WriteRuns(xml, runs);
        } else {
            WriteRuns(xml, block.runs);
        }
        xml << "</w:p>\n";
    }

    // The <w:drawing> element alone. A picture is the same markup whether it
    // is a paragraph of its own or sits inside a line; only the wrapping differs.
    void WriteDrawing(std::ostringstream& xml, int mediaIndex, float widthPtIn,
                      float heightPtIn, const std::string& altText, int drawingId,
                      const RichTextRun* floating = nullptr) {
        if (mediaIndex < 0 || mediaIndex >= static_cast<int>(doc_->media.size())) return;
        RichDocBlock shim;
        shim.mediaIndex = mediaIndex;
        shim.imageWidthPt = widthPtIn;
        shim.imageHeightPt = heightPtIn;
        shim.imageAltText = altText;
        WriteDrawingElement(xml, shim, drawingId, floating);
    }

    void WriteImage(std::ostringstream& xml, const RichDocBlock& block, int drawingId) {
        if (block.mediaIndex < 0 || block.mediaIndex >= static_cast<int>(doc_->media.size())) {
            return;
        }
        xml << "<w:p><w:r>";
        WriteDrawingElement(xml, block, drawingId);
        xml << "</w:r></w:p>\n";
    }

    void WriteDrawingElement(std::ostringstream& xml, const RichDocBlock& block, int drawingId,
                             const RichTextRun* floating = nullptr) {
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

        xml << "<w:drawing>";
        if (floating) {
            // A floating picture: where it sits and how text wraps round it,
            // in the element order CT_Anchor requires.
            using Wrap = RichTextRun::ImageWrap;
            const Wrap wrap = floating->imageWrap;
            const long long gap = static_cast<long long>(9.0f * kEmuPerPoint);      // Word's 0.32 cm
            xml << "<wp:anchor distT=\"0\" distB=\"0\" distL=\"" << gap << "\" distR=\"" << gap
                << "\" simplePos=\"0\" relativeHeight=\"" << (251658240 + drawingId) << "\" behindDoc=\""
                << (wrap == Wrap::BehindText ? 1 : 0)
                << "\" locked=\"0\" layoutInCell=\"1\" allowOverlap=\"1\">"
                << "<wp:simplePos x=\"0\" y=\"0\"/><wp:positionH relativeFrom=\"column\">";
            switch (floating->imageFloatAlign) {
                case RichTextAlign::Right:  xml << "<wp:align>right</wp:align>"; break;
                case RichTextAlign::Center: xml << "<wp:align>center</wp:align>"; break;
                case RichTextAlign::Default:
                    xml << "<wp:posOffset>" << static_cast<long long>(floating->imageOffsetXPt * kEmuPerPoint)
                        << "</wp:posOffset>";
                    break;
                default: xml << "<wp:align>left</wp:align>"; break;
            }
            xml << "</wp:positionH><wp:positionV relativeFrom=\"paragraph\"><wp:posOffset>"
                << static_cast<long long>(floating->imageOffsetYPt * kEmuPerPoint)
                << "</wp:posOffset></wp:positionV>"
                << "<wp:extent cx=\"" << cx << "\" cy=\"" << cy << "\"/>"
                << "<wp:effectExtent l=\"0\" t=\"0\" r=\"0\" b=\"0\"/>";
            switch (wrap) {
                case Wrap::TopAndBottom: xml << "<wp:wrapTopAndBottom/>"; break;
                case Wrap::BehindText:
                case Wrap::InFrontOfText: xml << "<wp:wrapNone/>"; break;
                default: xml << "<wp:wrapSquare wrapText=\"bothSides\"/>"; break;
            }
        } else {
            xml << "<wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\">"
                << "<wp:extent cx=\"" << cx << "\" cy=\"" << cy << "\"/>";
        }
        xml << "<wp:docPr id=\"" << drawingId << "\" name=\"" << EscapeXml(name) << "\"/>"
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
            << "</pic:pic></a:graphicData></a:graphic>"
            << (floating ? "</wp:anchor>" : "</wp:inline>") << "</w:drawing>";
    }

    // w:tcBorders then w:shd, in the order CT_TcPr requires (after vMerge).
    static void WriteCellFrame(std::ostringstream& xml, const RichTableCell& cell) {
        xml << "<w:tcBorders>";
        auto side = [&](const char* name, const RichBorder& border) {
            if (!border.IsVisible()) {
                xml << "<w:" << name << " w:val=\"nil\"/>";
                return;
            }
            const long eighths = std::max(2L, std::lround(border.widthPt * 8.0f));
            const std::string color = border.color.size() == 7 ? border.color.substr(1) : "auto";
            xml << "<w:" << name << " w:val=\"" << RichBorderStyleWordName(border.style) << "\" w:sz=\"" << std::to_string(eighths)
                << "\" w:space=\"0\" w:color=\"" << EscapeXml(color) << "\"/>";
        };
        side("top", cell.borderTop);
        side("left", cell.borderLeft);
        side("bottom", cell.borderBottom);
        side("right", cell.borderRight);
        xml << "</w:tcBorders>";
        if (cell.backgroundColor.size() == 7) {
            xml << "<w:shd w:val=\"clear\" w:color=\"auto\" w:fill=\""
                << EscapeXml(cell.backgroundColor.substr(1)) << "\"/>";
        }
        // Then w:tcMar and w:vAlign, still in CT_TcPr order.
        const float paddings[4] = {cell.paddingTopPt, cell.paddingLeftPt, cell.paddingBottomPt, cell.paddingRightPt};
        const char* names[4] = {"top", "left", "bottom", "right"};
        bool anyPadding = false;
        for (float p : paddings) anyPadding = anyPadding || p >= 0.0f;
        if (anyPadding) {
            xml << "<w:tcMar>";
            for (int i = 0; i < 4; ++i) {
                if (paddings[i] >= 0.0f) {
                    xml << "<w:" << names[i] << " w:w=\"" << Twips(paddings[i]) << "\" w:type=\"dxa\"/>";
                }
            }
            xml << "</w:tcMar>";
        }
        if (cell.verticalAlign != RichVerticalAlign::Top) {
            xml << "<w:vAlign w:val=\"" << (cell.verticalAlign == RichVerticalAlign::Middle ? "center" : "bottom") << "\"/>";
        }
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
        // A document's own frames go on the cells (w:tcBorders); the table
        // then has no borders of its own. Otherwise the usual thin grid.
        const char* tableLine = block.tableBordersFromDocument
                ? "w:val=\"nil\"/>" : "w:val=\"single\" w:sz=\"4\" w:color=\"auto\"/>";
        // CT_TblPr order: tblStyle, tblW, jc, tblInd, tblBorders.
        xml << "<w:tbl><w:tblPr><w:tblStyle w:val=\"TableGrid\"/>";
        if (block.tableWidthPt > 0.0f) {
            xml << "<w:tblW w:w=\"" << Twips(block.tableWidthPt) << "\" w:type=\"dxa\"/>";
        } else if (block.tableWidthPercent > 0.0f) {
            xml << "<w:tblW w:w=\"" << std::to_string(std::lround(block.tableWidthPercent * 50.0f)) << "\" w:type=\"pct\"/>";
        } else {
            xml << "<w:tblW w:w=\"0\" w:type=\"auto\"/>";
        }
        if (block.tableAlign == RichTextAlign::Center) xml << "<w:jc w:val=\"center\"/>";
        else if (block.tableAlign == RichTextAlign::Right) xml << "<w:jc w:val=\"right\"/>";
        if (block.tableIndentPt > 0.0f) xml << "<w:tblInd w:w=\"" << Twips(block.tableIndentPt) << "\" w:type=\"dxa\"/>";
        xml << "<w:tblBorders>";
        for (const char* side : {"top", "left", "bottom", "right", "insideH", "insideV"}) {
            xml << "<w:" << side << " " << tableLine;
        }
        xml << "</w:tblBorders></w:tblPr><w:tblGrid>";
        // Known proportions become twips across the table's width (9000 twips,
        // 6.25in, when it has none of its own).
        const std::vector<float>& widths = block.tableColumnWidths;
        float totalWidth = 0.0f;
        for (float w : widths) totalWidth += std::max(0.0f, w);
        const bool widthsKnown = widths.size() == columnCount && totalWidth > 0.0f;
        for (size_t c = 0; c < columnCount; ++c) {
            if (widthsKnown) {
                const float tableTwips = block.tableWidthPt > 0.0f ? block.tableWidthPt * 20.0f : 9000.0f;
                const long twips = std::max(1L, std::lround(tableTwips * widths[c] / totalWidth));
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
                if (block.tableBordersFromDocument) WriteCellFrame(xml, cell);
                xml << "</w:tcPr><w:p>";
                if (const char* jc = JustificationFor(cell.align)) {
                    xml << "<w:pPr><w:jc w:val=\"" << jc << "\"/></w:pPr>";
                }
                std::vector<RichTextRun> runs = cell.runs;
                if (row.header) {
                    for (auto& run : runs) run.bold = true;
                }
                paragraphBase_ = ParagraphBase(RichDocBlock{});     // a cell's paragraph is Normal
                WriteRuns(xml, runs);
                xml << "</w:p></w:tc>";
            }
            xml << "</w:tr>\n";
        }
        xml << "</w:tbl>\n"
            // Word requires a paragraph between a table and the section end.
            << "<w:p/>\n";
    }

    // The blocks as WordprocessingML paragraphs and tables.
    std::string WriteBlocks(const std::vector<RichDocBlock>& blocks) {
        const std::vector<RichDocBlock>* savedBlocks = blocks_;
        blocks_ = &blocks;
        std::ostringstream body;
        for (size_t index = 0; index < blocks.size(); ++index) {
            const RichDocBlock& block = blocks[index];
            // A run of list items is one Word list - until an item needs a
            // level definition of its own (see StartsNewList).
            if (block.type == RichBlockType::ListItem && WordFormatInternal::StartsNewList(blocks, index)) {
                currentListNumId_ = AddListDefinition(index);
            }
            switch (block.type) {
                case RichBlockType::Table:
                    WriteTable(body, block);
                    break;
                case RichBlockType::Image:
                    WriteImage(body, block, ++drawingId_);
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
        blocks_ = savedBlocks;
        return body.str();
    }

    static constexpr const char* kPartNamespaces =
        "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
        "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
        "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\"";

    // The headers and footers as their own parts, referenced from sectPr.
    struct FurniturePart { std::string name; std::string kind; std::string type; std::string xml; };
    std::vector<FurniturePart> furnitureParts_;

    // Footnote and endnote ids by note, from 1 in the order the references
    // appear (0 and -1 are Word's separator notes). Only notes something
    // refers to are written.
    std::map<int, int> noteIds_;
    void NumberNotes() {
        int footnotes = 0, endnotes = 0;
        for (const UCRichDocument::NoteReference& reference : doc_->NoteReferences()) {
            if (noteIds_.count(reference.noteIndex)) continue;
            const bool endnote = doc_->notes[static_cast<size_t>(reference.noteIndex)].kind == RichNote::Kind::Endnote;
            noteIds_[reference.noteIndex] = endnote ? ++endnotes : ++footnotes;
        }
    }

    // footnotes.xml / endnotes.xml, as parts beside the headers.
    void AddNotesPart(RichNote::Kind kind) {
        const bool endnote = kind == RichNote::Kind::Endnote;
        const std::string element = endnote ? "endnote" : "footnote";
        std::vector<std::pair<int, int>> ordered;     // id, note
        for (const auto& [note, id] : noteIds_) {
            if (doc_->notes[static_cast<size_t>(note)].kind == kind) ordered.emplace_back(id, note);
        }
        if (ordered.empty()) return;
        std::sort(ordered.begin(), ordered.end());
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<w:" << element << "s "
            << kPartNamespaces << ">"
            << "<w:" << element << " w:type=\"separator\" w:id=\"-1\"><w:p><w:r><w:separator/></w:r></w:p></w:"
            << element << ">"
            << "<w:" << element << " w:type=\"continuationSeparator\" w:id=\"0\"><w:p><w:r>"
               "<w:continuationSeparator/></w:r></w:p></w:" << element << ">";
        for (const auto& [id, note] : ordered) {
            std::vector<RichDocBlock> blocks = doc_->notes[static_cast<size_t>(note)].blocks;
            if (blocks.empty()) blocks.emplace_back();
            std::string body = WriteBlocks(blocks);
            // The note's own mark opens its first paragraph, after its
            // properties.
            const std::string mark = "<w:r><w:rPr><w:vertAlign w:val=\"superscript\"/></w:rPr><w:" + element
                                   + "Ref/></w:r><w:r><w:t xml:space=\"preserve\"> </w:t></w:r>";
            size_t at = std::string::npos;
            for (size_t p = body.find("<w:p"); p != std::string::npos; p = body.find("<w:p", p + 1)) {
                const char next = p + 4 < body.size() ? body[p + 4] : '\0';
                if (next == '>' || next == ' ') { at = p; break; }
            }
            if (at != std::string::npos) {
                size_t insert = body.find('>', at) + 1;
                if (body.compare(insert, 7, "<w:pPr>") == 0) insert = body.find("</w:pPr>", insert) + 8;
                body.insert(insert, mark);
            }
            xml << "<w:" << element << " w:id=\"" << id << "\">" << body << "</w:" << element << ">";
        }
        xml << "</w:" << element << "s>\n";
        FurniturePart part;
        part.kind = element + "s";
        part.name = element + "s.xml";
        part.xml = xml.str();
        furnitureParts_.push_back(std::move(part));
    }

    std::string SectionPropertiesXml() {
        const UCRichDocument& doc = *doc_;
        std::ostringstream refs;
        auto part = [&](const std::vector<RichDocBlock>& blocks, const char* kind, const char* type) {
            if (blocks.empty()) return;
            FurniturePart p;
            p.kind = kind;
            p.type = type;
            p.name = std::string(kind) + std::to_string(furnitureParts_.size() + 1) + ".xml";
            const char* root = std::string(kind) == "header" ? "w:hdr" : "w:ftr";
            p.xml = std::string("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<")
                  + root + " " + kPartNamespaces + ">" + WriteBlocks(blocks) + "</" + root + ">\n";
            refs << "<w:" << kind << "Reference w:type=\"" << type << "\" r:id=\"rIdPart"
                 << (furnitureParts_.size() + 1) << "\"/>";
            furnitureParts_.push_back(std::move(p));
        };
        part(doc.pageFurniture.header, "header", "default");
        if (doc.firstPageDiffers) part(doc.firstPageFurniture.header, "header", "first");
        part(doc.pageFurniture.footer, "footer", "default");
        if (doc.firstPageDiffers) part(doc.firstPageFurniture.footer, "footer", "first");

        // Word's defaults (A4, 2 cm margins) where the document states no page.
        const RichPageSetup& page = doc.page;
        auto twips = [](float pt) { return std::lround(pt * 20.0f); };
        const bool hasPage = page.HasPage();
        std::ostringstream xml;
        xml << "<w:sectPr>" << refs.str();
        xml << "<w:pgSz w:w=\"" << (hasPage ? twips(page.widthPt) : 11906)
            << "\" w:h=\"" << (hasPage ? twips(page.heightPt) : 16838) << "\""
            << (hasPage && page.widthPt > page.heightPt ? " w:orient=\"landscape\"" : "") << "/>";
        const float headerTop = page.headerTopPt > 0.0f ? page.headerTopPt : 35.45f;
        const float footerBottom = page.footerBottomPt > 0.0f ? page.footerBottomPt : 35.45f;
        xml << "<w:pgMar w:top=\"" << (hasPage ? twips(page.marginTopPt) : 1134)
            << "\" w:right=\"" << (hasPage ? twips(page.marginRightPt) : 1134)
            << "\" w:bottom=\"" << (hasPage ? twips(page.marginBottomPt) : 1134)
            << "\" w:left=\"" << (hasPage ? twips(page.marginLeftPt) : 1134)
            << "\" w:header=\"" << twips(headerTop) << "\" w:footer=\"" << twips(footerBottom)
            << "\" w:gutter=\"0\"/>";
        if (doc.firstPageDiffers) xml << "<w:titlePg/>";
        xml << "</w:sectPr>";
        return xml.str();
    }

    std::string BuildDocumentXml() {
        NumberNotes();
        std::string body = WriteBlocks(doc_->blocks);
        if (!openComments_.empty()) {
            // A comment running to the end of the document ends here.
            std::ostringstream close;
            close << "<w:p>";
            for (int id : openComments_) WriteCommentEnd(close, id);
            close << "</w:p>\n";
            openComments_.clear();
            body += close.str();
        }
        const std::string sectPr = SectionPropertiesXml();
        if (!writtenComments_.empty()) {
            FurniturePart part;
            part.kind = "comments";
            part.name = "comments.xml";
            part.xml = BuildCommentsXml();
            furnitureParts_.push_back(std::move(part));
        }
        AddNotesPart(RichNote::Kind::Footnote);
        AddNotesPart(RichNote::Kind::Endnote);
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            << "<w:document "
            << "xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\" "
            << "xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" "
            << "xmlns:wp=\"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing\">"
            << "<w:body>\n" << body << sectPr
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
        for (const FurniturePart& part : furnitureParts_) {
            xml << "<Override PartName=\"/word/" << part.name << "\" "
                   "ContentType=\"application/vnd.openxmlformats-officedocument."
                   "wordprocessingml." << part.kind << "+xml\"/>\n";
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
        for (size_t i = 0; i < furnitureParts_.size(); ++i) {
            xml << "<Relationship Id=\"rIdPart" << (i + 1)
                << "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/"
                << furnitureParts_[i].kind << "\" Target=\"" << furnitureParts_[i].name << "\"/>\n";
        }
        xml << MediaAndLinkRelationships() << "</Relationships>\n";
        return xml.str();
    }

    // A header or footer part refers to pictures and links by the same ids
    // the body does, so its relationships repeat the body's.
    std::string BuildPartRelsXml() const {
        return "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
               "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">\n"
             + MediaAndLinkRelationships() + "</Relationships>\n";
    }

    std::string MediaAndLinkRelationships() const {
        std::ostringstream xml;
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
        return xml.str();
    }

    // One named style as Word writes it: name, what it is based on and what
    // follows it, then its paragraph and character properties, each in the
    // element order the schema requires.
    static void WriteNamedStyle(std::ostringstream& xml, const RichStyle& style, bool isDefault) {
        const bool paragraph = style.kind == RichStyle::Kind::Paragraph;
        xml << "<w:style w:type=\"" << (paragraph ? "paragraph" : "character") << "\""
            << (isDefault ? " w:default=\"1\"" : "") << " w:styleId=\"" << EscapeXml(style.id) << "\">"
            << "<w:name w:val=\"" << EscapeXml(style.name.empty() ? style.id : style.name) << "\"/>";
        if (!style.basedOn.empty()) xml << "<w:basedOn w:val=\"" << EscapeXml(style.basedOn) << "\"/>";
        if (paragraph && !style.nextStyle.empty()) xml << "<w:next w:val=\"" << EscapeXml(style.nextStyle) << "\"/>";
        if (paragraph && !style.paragraph.IsEmpty()) {
            const RichStyleParagraph& p = style.paragraph;
            xml << "<w:pPr>";
            if (p.headingLevel && *p.headingLevel > 0) xml << "<w:keepNext/>";
            if (p.spaceBeforePt || p.spaceAfterPt || p.lineSpacing) {
                xml << "<w:spacing";
                if (p.spaceBeforePt) xml << " w:before=\"" << Twips(*p.spaceBeforePt) << "\"";
                if (p.spaceAfterPt) xml << " w:after=\"" << Twips(*p.spaceAfterPt) << "\"";
                if (p.lineSpacing) xml << " w:line=\"" << std::lround(*p.lineSpacing * 240.0f) << "\" w:lineRule=\"auto\"";
                xml << "/>";
            }
            if (p.leftIndentPt || p.rightIndentPt || p.firstLineIndentPt) {
                xml << "<w:ind";
                if (p.leftIndentPt) xml << " w:left=\"" << Twips(*p.leftIndentPt) << "\"";
                if (p.rightIndentPt) xml << " w:right=\"" << Twips(*p.rightIndentPt) << "\"";
                if (p.firstLineIndentPt && *p.firstLineIndentPt >= 0.0f) xml << " w:firstLine=\"" << Twips(*p.firstLineIndentPt) << "\"";
                if (p.firstLineIndentPt && *p.firstLineIndentPt < 0.0f) xml << " w:hanging=\"" << Twips(-*p.firstLineIndentPt) << "\"";
                xml << "/>";
            }
            if (p.align && *p.align != RichTextAlign::Default) {
                const char* jc = *p.align == RichTextAlign::Center ? "center" : *p.align == RichTextAlign::Right ? "right"
                               : *p.align == RichTextAlign::Justify ? "both" : "left";
                xml << "<w:jc w:val=\"" << jc << "\"/>";
            }
            if (p.headingLevel && *p.headingLevel > 0) xml << "<w:outlineLvl w:val=\"" << (*p.headingLevel - 1) << "\"/>";
            xml << "</w:pPr>";
        }
        if (!style.character.IsEmpty()) {
            const RichStyleCharacter& c = style.character;
            xml << "<w:rPr>";
            const std::string family = c.code && *c.code ? std::string("Courier New") : c.fontFamily.value_or("");
            if (!family.empty()) xml << "<w:rFonts w:ascii=\"" << EscapeXml(family) << "\" w:hAnsi=\"" << EscapeXml(family) << "\"/>";
            if (c.bold) xml << (*c.bold ? "<w:b/>" : "<w:b w:val=\"0\"/>");
            if (c.italic) xml << (*c.italic ? "<w:i/>" : "<w:i w:val=\"0\"/>");
            if (c.strikethrough) xml << (*c.strikethrough ? "<w:strike/>" : "<w:strike w:val=\"0\"/>");
            if (c.color && c.color->size() == 7) xml << "<w:color w:val=\"" << EscapeXml(c.color->substr(1)) << "\"/>";
            if (c.fontSizePt && *c.fontSizePt > 0.0f) {
                const long half = std::lround(*c.fontSizePt * 2.0f);
                xml << "<w:sz w:val=\"" << half << "\"/><w:szCs w:val=\"" << half << "\"/>";
            }
            if (c.underline) xml << (*c.underline ? "<w:u w:val=\"single\"/>" : "<w:u w:val=\"none\"/>");
            xml << "</w:rPr>";
        }
        xml << "</w:style>\n";
    }

    std::string BuildStylesXml() const {
        std::ostringstream xml;
        // The styles the writer names itself, unless the document has its own
        // of that name - then the document's is written instead.
        auto documentHas = [&](const std::string& id) { return doc_->FindStyle(id) != nullptr; };
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            << "<w:styles xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">\n"
            << "<w:docDefaults><w:rPrDefault><w:rPr>"
               "<w:rFonts w:ascii=\"Calibri\" w:hAnsi=\"Calibri\"/>"
               "<w:sz w:val=\"22\"/><w:szCs w:val=\"22\"/>"
               "</w:rPr></w:rPrDefault></w:docDefaults>\n";
        if (!documentHas("Normal")) {
            xml << "<w:style w:type=\"paragraph\" w:default=\"1\" w:styleId=\"Normal\">"
                   "<w:name w:val=\"Normal\"/></w:style>\n";
        }
        static const int headingSizesHalfPt[6] = {36, 32, 28, 26, 24, 22};
        for (int level = 1; level <= 6; ++level) {
            if (documentHas("Heading" + std::to_string(level))) continue;
            xml << "<w:style w:type=\"paragraph\" w:styleId=\"Heading" << level << "\">"
                << "<w:name w:val=\"heading " << level << "\"/>"
                << "<w:basedOn w:val=\"Normal\"/>"
                << "<w:pPr><w:keepNext/><w:spacing w:before=\"240\" w:after=\"120\"/>"
                << "<w:outlineLvl w:val=\"" << (level - 1) << "\"/></w:pPr>"
                << "<w:rPr><w:b/><w:sz w:val=\"" << headingSizesHalfPt[level - 1]
                << "\"/><w:szCs w:val=\"" << headingSizesHalfPt[level - 1]
                << "\"/></w:rPr></w:style>\n";
        }
        if (!documentHas("Quote")) {
            xml << "<w:style w:type=\"paragraph\" w:styleId=\"Quote\">"
                   "<w:name w:val=\"Quote\"/><w:basedOn w:val=\"Normal\"/>"
                   "<w:pPr><w:ind w:left=\"567\" w:right=\"567\"/></w:pPr>"
                   "<w:rPr><w:i/></w:rPr></w:style>\n";
        }
        if (!documentHas("CodeBlock")) {
            xml << "<w:style w:type=\"paragraph\" w:styleId=\"CodeBlock\">"
                   "<w:name w:val=\"Code Block\"/><w:basedOn w:val=\"Normal\"/>"
                   "<w:pPr><w:ind w:left=\"284\"/></w:pPr>"
                   "<w:rPr><w:rFonts w:ascii=\"Courier New\" w:hAnsi=\"Courier New\"/>"
                   "<w:sz w:val=\"20\"/></w:rPr></w:style>\n";
        }
        if (!documentHas("ListParagraph")) {
            xml << "<w:style w:type=\"paragraph\" w:styleId=\"ListParagraph\">"
                   "<w:name w:val=\"List Paragraph\"/><w:basedOn w:val=\"Normal\"/>"
                   "<w:pPr><w:ind w:left=\"720\"/><w:contextualSpacing/></w:pPr></w:style>\n";
        }
        for (const RichStyle& style : doc_->styles) WriteNamedStyle(xml, style, style.id == "Normal");
        xml << "<w:style w:type=\"table\" w:styleId=\"TableGrid\">"
               "<w:name w:val=\"Table Grid\"/></w:style>\n"
            << "</w:styles>\n";
        return xml.str();
    }

    // One Word list per run of list items: its own abstract numbering with
    // each level's label taken from the first item at that level, so separate
    // lists count separately and "a)" / "1.2." / custom bullets survive.
    static const char* WordNumFmt(RichNumberFormat format) {
        switch (format) {
            case RichNumberFormat::LowerLetter: return "lowerLetter";
            case RichNumberFormat::UpperLetter: return "upperLetter";
            case RichNumberFormat::LowerRoman: return "lowerRoman";
            case RichNumberFormat::UpperRoman: return "upperRoman";
            case RichNumberFormat::DecimalZero: return "decimalZero";
            case RichNumberFormat::NoNumber: return "none";
            default: return "decimal";
        }
    }

    int AddListDefinition(size_t begin) {
        const RichDocBlock* byLevel[9] = {};
        for (size_t j = begin; j < (*blocks_).size() && (*blocks_)[j].type == RichBlockType::ListItem; ++j) {
            if (j > begin && WordFormatInternal::StartsNewList((*blocks_), j)) break;
            const int l = std::clamp((*blocks_)[j].listLevel, 0, 8);
            if (!byLevel[l]) byLevel[l] = &(*blocks_)[j];
        }
        const int id = static_cast<int>(listDefinitions_.size());
        std::ostringstream xml;
        xml << "<w:abstractNum w:abstractNumId=\"" << id << "\">\n";
        for (int level = 0; level < 9; ++level) {
            const RichDocBlock* item = byLevel[level];
            const bool ordered = item && item->orderedList;
            const int start = ordered && item->listStartNumber > 0 ? item->listStartNumber : 1;
            xml << "<w:lvl w:ilvl=\"" << level << "\"><w:start w:val=\"" << start << "\"/>";
            if (ordered) {
                const std::string text = item->numberTemplate.empty()
                        ? "%" + std::to_string(level + 1) + "." : item->numberTemplate;
                xml << "<w:numFmt w:val=\"" << WordNumFmt(item->numberFormat) << "\"/>"
                    << "<w:lvlText w:val=\"" << EscapeXml(text) << "\"/>";
            } else {
                const std::string bullet = item && !item->bulletText.empty() ? item->bulletText : "\xE2\x80\xA2";
                xml << "<w:numFmt w:val=\"bullet\"/><w:lvlText w:val=\"" << EscapeXml(bullet) << "\"/>";
            }
            xml << "<w:lvlJc w:val=\"left\"/><w:pPr><w:ind w:left=\""
                << 720 * (level + 1) << "\" w:hanging=\"360\"/></w:pPr></w:lvl>\n";
        }
        xml << "</w:abstractNum>\n";
        listDefinitions_.push_back(xml.str());
        return id + 1;   // numId: 1-based
    }

    std::string BuildNumberingXml() const {
        std::ostringstream xml;
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
            << "<w:numbering xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">\n";
        for (const std::string& definition : listDefinitions_) xml << definition;
        for (size_t i = 0; i < listDefinitions_.size(); ++i) {
            xml << "<w:num w:numId=\"" << (i + 1) << "\"><w:abstractNumId w:val=\"" << i << "\"/></w:num>\n";
        }
        xml << "</w:numbering>\n";
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
    if (!reader.Load(filePath, outDocument, outError)) return false;
    outDocument.ReadCheckboxPrefixes();
    return true;
}

bool UCWordDocumentIO::SaveDocx(const std::string& filePath, const UCRichDocument& document,
                                std::string& outError) {
    DocxWriter writer;
    // The format has no check list: its items go out as a ballot box
    // opening their text, which is what reads back in.
    for (const RichDocBlock& block : document.blocks) {
        if (block.checkbox) return writer.Save(filePath, document.WithCheckboxesAsPrefixes(), outError);
    }
    return writer.Save(filePath, document, outError);
}

} // namespace UltraCanvas
