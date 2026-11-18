// Plugins/Documents/Word/UltraCanvasDocxFormat.cpp
// Word 2007+ (.docx, OOXML WordprocessingML) reader and writer for
// UCRichDocument. DOCX is an OPC ZIP package: [Content_Types].xml, _rels
// relationship parts, word/document.xml plus styles/numbering/media parts.
// Version: 1.15.0 - a chart's data is read into RichTextFrame::chart and written back as a chart part (word/charts)
// Version: 1.14.0 - charts: a scatter chart's points over a value axis of their own
// Version: 1.13.1 - charts: pies, bars lying down, stacked and per-cent columns
// Version: 1.13.0 - charts (c:chartSpace) drawn as shapes; nil paragraph borders; numbering levels without
//                   number or bullet; a font named with its alternatives
// Version: 1.12.1 - a cell paragraph's own line multiple read and written
// Version: 1.12.0 - w:tblLayout (autofit or fixed) read and written
// Version: 1.11.0 - tables nested in cells, column breaks, a cell's line height and multiple, negative w:tblInd,
//                   a cell's lines written as paragraphs, shapes and turned pictures in the line
// Version: 1.10.0 - theme fonts (asciiTheme / hAnsiTheme), capitals (w:caps) from a run or its paragraph's
//                   style, a turned picture (a:xfrm rot) read and written, round2SameRect's outline
// Version: 1.9.0 - paragraph spacing in cells read and written; w:kern as the document's kerning
// Version: 1.8.0 - a section's own header, footer and page margins; row heights (w:trHeight) read and written, a cell's empty paragraphs kept as empty
//                  lines, negative float offsets
// Version: 1.7.0 - The main document part is the one the package's relationships name
//                  (word/document2.xml, say), with its own folder and
//                  relationships; parts that bind the OOXML namespaces to other prefixes
//                  (ns0:p, a default namespace) read as the w:/r:/a: ones they are
// Version: 1.6.0 - Pattern fills (a:pattFill) and picture fills (a:blipFill) read and written
// Version: 1.5.0 - Gradient fills read and written (a:gradFill, VML v:fill); VML pictures
//                  (v:imagedata) as pictures in the line or floating
// Version: 1.4.0 - Drawn shapes: wps shapes (preset and custom outlines, theme colours), wpg
//                  groups with pictures and text, VML shapes and groups; a header's own
//                  relationships, so its pictures load
// Version: 1.3.0 - w:spacing in w:rPr: a run's (and a character style's) letter spacing, read
//                  and written
// Version: 1.2.0
// Last Modified: 2026-11-15
// Author: UltraCanvas Framework

#include "Plugins/Documents/Word/UltraCanvasWordDocumentIO.h"
#include "UltraCanvasMathToLatex.h"
#include "UltraCanvasWordFormatInternal.h"
#include "UltraCanvasZipPackage.h"
#include "UltraCanvasOfficeEncryption.h"
#include "UltraCanvasTextUtils.h"

#include "tinyxml2.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <sstream>

namespace UltraCanvas {

using WordFormatInternal::EscapeXml;
using WordFormatInternal::MathBlockAsParagraph;
using WordFormatInternal::ToLower;
using WordFormatInternal::ApplyOfficeArtShapeType;

namespace {

constexpr float kEmuPerPoint = 12700.0f;

// A run's character spacing, w:spacing in w:rPr (not the paragraph's
// w:spacing in w:pPr): twentieths of a point (ST_SignedTwipsMeasure), or a
// length with its unit ("1.5pt"). Nothing when the run states none.
std::optional<float> CharacterSpacingPt(tinyxml2::XMLElement* rPr) {
    auto* spacing = rPr ? rPr->FirstChildElement("w:spacing") : nullptr;
    const char* value = spacing ? spacing->Attribute("w:val") : nullptr;
    if (!value || !*value) return std::nullopt;
    const std::string v(value);
    if (v.find_first_of("abcdefghijklmnopqrstuvwxyz") != std::string::npos)
        return WordFormatInternal::ParseLengthPt(v);
    char* end = nullptr;
    const long twips = std::strtol(value, &end, 10);   // an integer: no locale decimal point involved
    if (end == value) return std::nullopt;
    return static_cast<float>(twips) / 20.0f;
}

// w:spacing for a letter spacing in points, in twentieths of a point.
std::string CharacterSpacingXml(float points) {
    return "<w:spacing w:val=\"" + std::to_string(std::lround(points * 20.0f)) + "\"/>";
}

// ===== DOCX READING =====

struct DocxRelationship {
    std::string target;
    std::string type;
    bool external = false;
};

// The prefix the reader expects for each OOXML namespace it reads by name,
// transitional and strict (ISO 29500 strict) URIs alike.
const std::map<std::string, std::string>& CanonicalOoxmlPrefixes() {
    static const std::map<std::string, std::string> prefixes = {
        {"http://schemas.openxmlformats.org/wordprocessingml/2006/main", "w"},
        {"http://purl.oclc.org/ooxml/wordprocessingml/main", "w"},
        {"http://schemas.openxmlformats.org/officeDocument/2006/relationships", "r"},
        {"http://purl.oclc.org/ooxml/officeDocument/relationships", "r"},
        {"http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing", "wp"},
        {"http://purl.oclc.org/ooxml/drawingml/wordprocessingDrawing", "wp"},
        {"http://schemas.openxmlformats.org/drawingml/2006/main", "a"},
        {"http://purl.oclc.org/ooxml/drawingml/main", "a"},
        {"http://schemas.openxmlformats.org/drawingml/2006/picture", "pic"},
        {"http://purl.oclc.org/ooxml/drawingml/picture", "pic"},
        {"http://schemas.openxmlformats.org/officeDocument/2006/math", "m"},
        {"http://purl.oclc.org/ooxml/officeDocument/math", "m"},
        {"http://schemas.openxmlformats.org/markup-compatibility/2006", "mc"},
        {"http://schemas.microsoft.com/office/word/2010/wordprocessingShape", "wps"},
        {"http://schemas.microsoft.com/office/word/2010/wordprocessingGroup", "wpg"},
        {"http://schemas.microsoft.com/office/word/2010/wordml", "w14"},
        {"urn:schemas-microsoft-com:vml", "v"},
        {"urn:schemas-microsoft-com:office:office", "o"},
    };
    return prefixes;
}

// The reader matches element and attribute names as written (w:p, r:id).
// Word writes those prefixes, but XML lets a producer pick its own - ns0:p
// from a Python XML library, or the WordprocessingML namespace as the
// default one - and such a part read as having no body. Renames every
// prefix bound to a known namespace to the expected one, in place.
void NormalizeOoxmlPrefixes(tinyxml2::XMLElement* element,
                            std::map<std::string, std::string> renames) {
    // This element's declarations, over its parent's.
    for (const auto* a = element->FirstAttribute(); a; a = a->Next()) {
        const std::string name = a->Name();
        std::string prefix;
        if (name == "xmlns") prefix = "";
        else if (name.compare(0, 6, "xmlns:") == 0) prefix = name.substr(6);
        else continue;
        const auto known = CanonicalOoxmlPrefixes().find(a->Value());
        if (known != CanonicalOoxmlPrefixes().end() && known->second != prefix)
            renames[prefix] = known->second;
        else
            renames.erase(prefix);
    }
    if (renames.empty()) {
        for (auto* child = element->FirstChildElement(); child; child = child->NextSiblingElement())
            NormalizeOoxmlPrefixes(child, renames);
        return;
    }
    auto rename = [&renames](const std::string& name, bool attribute) -> std::string {
        const size_t colon = name.find(':');
        // An attribute without a prefix is in no namespace, whatever the default one.
        if (colon == std::string::npos && attribute) return name;
        const std::string prefix = colon == std::string::npos ? "" : name.substr(0, colon);
        if (prefix == "xmlns") return name;
        const auto to = renames.find(prefix);
        if (to == renames.end()) return name;
        return to->second + ":" + (colon == std::string::npos ? name : name.substr(colon + 1));
    };
    const std::string elementName = element->Name() ? element->Name() : "";
    const std::string renamedElement = rename(elementName, false);
    if (renamedElement != elementName) element->SetName(renamedElement.c_str());
    std::vector<std::pair<std::string, std::string>> renamedAttributes;
    std::vector<std::string> oldNames;
    for (const auto* a = element->FirstAttribute(); a; a = a->Next()) {
        const std::string name = a->Name();
        const std::string renamed = rename(name, true);
        if (renamed != name) {
            oldNames.push_back(name);
            renamedAttributes.emplace_back(renamed, a->Value());
        }
    }
    for (const auto& name : oldNames) element->DeleteAttribute(name.c_str());
    for (const auto& [name, value] : renamedAttributes) element->SetAttribute(name.c_str(), value.c_str());
    for (auto* child = element->FirstChildElement(); child; child = child->NextSiblingElement())
        NormalizeOoxmlPrefixes(child, renames);
}

void NormalizeOoxmlPrefixes(tinyxml2::XMLDocument& document) {
    if (auto* root = document.RootElement()) NormalizeOoxmlPrefixes(root, {});
}

// Whether a package part is a WordprocessingML main document part: by its
// content type in [Content_Types].xml, or - a package that names none - by
// the namespace its root is in.
bool IsWordMainDocumentPart(const UCZipPackageReader& zip, const std::string& part) {
    std::string typesXml;
    if (zip.ReadEntry("[Content_Types].xml", typesXml)) {
        tinyxml2::XMLDocument types;
        if (types.Parse(typesXml.c_str()) == tinyxml2::XML_SUCCESS && types.RootElement()) {
            for (auto* e = types.RootElement()->FirstChildElement(); e; e = e->NextSiblingElement()) {
                const char* partName = e->Attribute("PartName");
                const char* contentType = e->Attribute("ContentType");
                if (!partName || !contentType) continue;
                // Part names compare without regard to case (OPC).
                if (ToLower(partName) != ToLower("/" + part)) continue;
                return std::string(contentType).find("wordprocessingml.document") != std::string::npos
                    || std::string(contentType).find("wordprocessingml.template") != std::string::npos;
            }
        }
    }
    std::string xml;
    if (!zip.ReadEntry(part, xml)) return false;
    xml.resize(std::min<size_t>(xml.size(), 4096));
    return xml.find("wordprocessingml/2006/main") != std::string::npos
        || xml.find("ooxml/wordprocessingml/main") != std::string::npos;
}

// A run of nothing but spaces - <w:t xml:space="preserve"> </w:t>, the space
// between two differently formatted words - is text, but tinyxml2 drops
// whitespace-only text. Each such space is swapped for a private-use marker
// before parsing and turned back into a space when the run is read. Without
// xml:space="preserve" Word ignores the whitespace too, so that is left alone.
constexpr const char* kKeptSpaceMarker = "\xEE\x80\x81";   // U+E001

// The prefix (with its colon) the part's root binds to the WordprocessingML
// namespace: "w:" as Word writes it, "ns0:" from some producers, "" when it
// is the default namespace. This runs on the text, before the part is parsed
// and NormalizeOoxmlPrefixes renames them.
std::string WordprocessingPrefix(const std::string& xml) {
    const size_t rootEnd = xml.find('>', xml.find('<', xml.find("?>") == std::string::npos ? 0 : xml.find("?>") + 2));
    const std::string root = xml.substr(0, rootEnd);
    for (const char* uri : {"http://schemas.openxmlformats.org/wordprocessingml/2006/main",
                            "http://purl.oclc.org/ooxml/wordprocessingml/main"}) {
        for (size_t at = root.find(uri); at != std::string::npos; at = root.find(uri, at + 1)) {
            // ... xmlns:PREFIX="uri" or xmlns="uri"
            const size_t equals = root.rfind('=', at);
            const size_t xmlns = root.rfind("xmlns", equals);
            if (equals == std::string::npos || xmlns == std::string::npos) continue;
            if (root[xmlns + 5] == '=') return "";
            if (root[xmlns + 5] == ':') return root.substr(xmlns + 6, equals - xmlns - 6) + ":";
        }
    }
    return "w:";
}

void ProtectWhitespaceRuns(std::string& xml) {
    const std::string prefix = WordprocessingPrefix(xml);
    const std::string openTag = "<" + prefix + "t";
    const std::string closeTag = "</" + prefix + "t>";
    std::string out;
    out.reserve(xml.size());
    size_t pos = 0;
    while (true) {
        size_t open = xml.find(openTag, pos);
        if (open == std::string::npos) break;
        const size_t nameEnd = open + openTag.size();
        const char after = nameEnd < xml.size() ? xml[nameEnd] : '\0';
        size_t close = xml.find('>', open);
        if ((after != '>' && after != ' ') || close == std::string::npos || xml[close - 1] == '/') {
            out.append(xml, pos, nameEnd - pos);
            pos = nameEnd;
            continue;
        }
        size_t text = close + 1;
        size_t end = text;
        while (end < xml.size() && (xml[end] == ' ' || xml[end] == '\t' || xml[end] == '\n' || xml[end] == '\r')) ++end;
        out.append(xml, pos, text - pos);
        const bool preserve =
            xml.substr(open, close - open).find("xml:space=\"preserve\"") != std::string::npos;
        if (end > text && preserve && xml.compare(end, closeTag.size(), closeTag) == 0) {
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
    bool Load(const std::string& filePath, const std::string& password, UCRichDocument& doc,
              std::string& error) {
        doc_ = &doc;
        doc_->breakAfterSlash = false;             // Word keeps "a/b" whole
        // A password-protected document is a compound file holding the
        // package encrypted (UltraCanvasOfficeEncryption.h), not a ZIP.
        std::vector<uint8_t> package;
        switch (DecryptOfficePackageFile(filePath, password, package, error)) {
            case OfficeDecryptStatus::NotEncrypted:
                if (!zip_.Open(filePath)) {
                    error = "The file is not a valid Word document (.docx): " + filePath;
                    return false;
                }
                break;
            case OfficeDecryptStatus::Decrypted:
                if (!zip_.OpenFromMemory(std::move(package))) {
                    error = "The decrypted document is not a valid Word document (.docx): " + filePath;
                    return false;
                }
                break;
            default:
                error += ": " + filePath;
                return false;
        }
        // Usually word/document.xml, but not always (word/document2.xml):
        // what counts is the part the package's relationships call the
        // office document.
        mainPart_ = WordFormatInternal::FindDocxMainPart(zip_);
        std::string documentXml;
        if (mainPart_.empty() || !zip_.ReadEntry(mainPart_, documentXml)) {
            error = "The package has no main document part — not a Word document: " + filePath;
            return false;
        }
        partFolder_ = mainPart_.substr(0, mainPart_.find_last_of('/') + 1);
        LoadRelationships();
        LoadTheme();
        LoadStyles();
        LoadSettings();
        LoadNumbering();

        ProtectWhitespaceRuns(documentXml);
        if (docXml_.Parse(documentXml.c_str()) != tinyxml2::XML_SUCCESS) {
            error = "The Word document content is not valid XML: " + filePath;
            return false;
        }
        NormalizeOoxmlPrefixes(docXml_);
        auto* root = docXml_.FirstChildElement("w:document");
        auto* body = root ? root->FirstChildElement("w:body") : nullptr;
        if (!body) {
            error = "The Word document has no body: " + filePath;
            return false;
        }
        parsingBody_ = true;
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
        parsingBody_ = false;
        ApplySections(body->FirstChildElement("w:sectPr"));
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
        int caps = -1;                      // w:caps in the style: 1 on, 0 off, -1 not stated
    };
    struct StyleGeometry {
        std::string basedOn;
        Geometry geometry;
    };
    static constexpr float kUnset = -1.0e9f;

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
    // The main document part (word/document.xml as a rule) and its folder,
    // which its relationship targets and the other parts are relative to.
    std::string mainPart_ = "word/document.xml";
    std::string partFolder_ = "word/";
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
    // Sections: each paragraph w:sectPr ends one, after the block index given.
    struct SectionEnd { int lastBlock; RichSectionSetup setup; tinyxml2::XMLElement* sectPr = nullptr; };
    std::vector<SectionEnd> sectionEnds_;
    // The room the header and footer in force take when they are only empty
    // lines (a section without references keeps the one before it).
    float emptyHeaderNow_ = 0.0f, emptyFooterNow_ = 0.0f;
    bool parsingBody_ = false;

    static RichSectionSetup ReadSectionSetup(tinyxml2::XMLElement* sectPr) {
        RichSectionSetup setup;
        if (!sectPr) return setup;
        if (auto* cols = sectPr->FirstChildElement("w:cols")) {
            setup.columns = std::clamp(cols->IntAttribute("w:num", 1), 1, 9);
            setup.columnGapPt = static_cast<float>(cols->IntAttribute("w:space", 720)) / 20.0f;
        }
        const std::string type = sectPr->FirstChildElement("w:type") ? Attr(sectPr->FirstChildElement("w:type"), "w:val") : "";
        setup.newPage = type != "continuous";
        return setup;
    }

    // The sections, now that the body is read: the first one's setup is the
    // document's, each later one starts at the block after the one before
    // ended.
    void ApplySections(tinyxml2::XMLElement* bodySectPr) {
        std::vector<SectionEnd> sections = sectionEnds_;
        sections.push_back({static_cast<int>(doc_->blocks.size()) - 1, ReadSectionSetup(bodySectPr), bodySectPr});
        doc_->firstSection = sections.front().setup;
        // The page, and the header and footer every page has unless its
        // section says otherwise, are the first section's.
        RichPageFurniture pagesNow, firstNow;      // what a section inherits: the one before it
        LoadSection(sections.front().sectPr, &pagesNow, &firstNow);
        bool ownFurniture = false;                // some section so far named a header or footer of its own
        for (size_t k = 1; k < sections.size(); k++) {
            // A section without a reference of a kind keeps the header or
            // footer of the section before it.
            tinyxml2::XMLElement* sectPr = sections[k].sectPr;
            RichSectionSetup& setup = sections[k].setup;
            bool titlePage = false;
            if (sectPr) {
                titlePage = sectPr->FirstChildElement("w:titlePg") != nullptr;
                ownFurniture = ReadFurnitureReferences(sectPr, pagesNow, firstNow, &emptyHeaderNow_, &emptyFooterNow_)
                               || ownFurniture;
                if (auto* margins = sectPr->FirstChildElement("w:pgMar")) {
                    auto value = [&](const char* name, float fallback) {
                        return margins->Attribute(name) ? std::abs(static_cast<float>(margins->IntAttribute(name, 0)) / 20.0f)
                                                        : fallback;
                    };
                    const float headerTop = value("w:header", doc_->page.headerTopPt);
                    const float footerBottom = value("w:footer", doc_->page.footerBottomPt);
                    float top = value("w:top", doc_->page.marginTopPt);
                    float bottom = value("w:bottom", doc_->page.marginBottomPt);
                    // A header of empty lines takes its room here too.
                    if (emptyHeaderNow_ > 0.0f) top = std::max(top, headerTop + emptyHeaderNow_);
                    if (emptyFooterNow_ > 0.0f) bottom = std::max(bottom, footerBottom + emptyFooterNow_);
                    auto differs = [](float a, float b) { return std::abs(a - b) > 0.05f; };
                    if (differs(top, doc_->page.marginTopPt)) setup.marginTopPt = top;
                    if (differs(bottom, doc_->page.marginBottomPt)) setup.marginBottomPt = bottom;
                    if (differs(headerTop, doc_->page.headerTopPt)) setup.headerTopPt = headerTop;
                    if (differs(footerBottom, doc_->page.footerBottomPt)) setup.footerBottomPt = footerBottom;
                }
            }
            if (ownFurniture || titlePage) {
                RichSectionFurniture own;
                own.pages = pagesNow;
                own.firstPageDiffers = titlePage;
                if (titlePage) own.firstPage = firstNow;
                setup.furniture = static_cast<int>(doc_->sectionFurniture.size());
                doc_->sectionFurniture.push_back(std::move(own));
            }
            const int start = sections[k - 1].lastBlock + 1;
            if (start <= 0 || start >= static_cast<int>(doc_->blocks.size())) continue;
            RichDocBlock& block = doc_->blocks[static_cast<size_t>(start)];
            block.sectionStart = true;
            block.section = setup;
        }
    }
    // Comments: w:id -> index into doc_->comments, and those whose range the
    // text being read is inside (ranges may cross paragraphs).
    std::map<std::string, int> commentIndexById_;
    std::vector<int> activeComments_;

    // The revision entry for an author and date, shared by every change
    // they made then.
    int RevisionFor(const std::string& author, const std::string& date) {
        for (size_t i = 0; i < doc_->revisions.size(); i++) {
            if (doc_->revisions[i].author == author && doc_->revisions[i].date == date) return static_cast<int>(i);
        }
        doc_->revisions.push_back({author, date});
        return static_cast<int>(doc_->revisions.size()) - 1;
    }

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
        std::string partName = partFolder_ + "comments.xml";
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
        NormalizeOoxmlPrefixes(part);
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
        relationships_ = ReadRelationships(partFolder_ + "_rels/" + mainPart_.substr(partFolder_.size()) + ".rels");
    }

    // The part a relationship of the main part's names by its type
    // (".../styles"), or the conventional name in the main part's folder.
    std::string PartByRelationship(const std::string& typeSuffix, const std::string& fallbackName) const {
        for (const auto& [id, rel] : relationships_) {
            if (!rel.external && rel.type.size() >= typeSuffix.size()
                && rel.type.compare(rel.type.size() - typeSuffix.size(), typeSuffix.size(), typeSuffix) == 0) {
                return ResolvePartPath(rel.target);
            }
        }
        return partFolder_ + fallbackName;
    }

    // A part's relationships (word/_rels/<part>.rels).
    std::map<std::string, DocxRelationship> ReadRelationships(const std::string& relsPath) {
        std::map<std::string, DocxRelationship> out;
        std::string relsXml;
        if (!zip_.ReadEntry(relsPath, relsXml)) return out;
        tinyxml2::XMLDocument rels;
        if (rels.Parse(relsXml.c_str()) != tinyxml2::XML_SUCCESS) return out;
        auto* root = rels.FirstChildElement("Relationships");
        if (!root) return out;
        for (auto* rel = root->FirstChildElement("Relationship"); rel;
             rel = rel->NextSiblingElement("Relationship")) {
            DocxRelationship entry;
            entry.target = Attr(rel, "Target");
            entry.type = Attr(rel, "Type");
            entry.external = std::string(Attr(rel, "TargetMode")) == "External";
            std::string id = Attr(rel, "Id");
            if (!id.empty()) out[id] = entry;
        }
        return out;
    }

    // Resolves a relationship target relative to the main part's folder
    // (word/) into a package path.
    std::string ResolvePartPath(const std::string& target) const {
        std::string path = target;
        if (!path.empty() && path[0] == '/') return path.substr(1);
        path = partFolder_ + path;
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
        if (!zip_.ReadEntry(PartByRelationship("/styles", "styles.xml"), stylesXml)) return;
        tinyxml2::XMLDocument styles;
        if (styles.Parse(stylesXml.c_str()) != tinyxml2::XML_SUCCESS) return;
        NormalizeOoxmlPrefixes(styles);
        auto* root = styles.FirstChildElement("w:styles");
        if (!root) return;
        bool kerningStated = false;
        if (auto* defaults = root->FirstChildElement("w:docDefaults")) {
            auto* pPrDefault = defaults->FirstChildElement("w:pPrDefault");
            if (auto* pPr = pPrDefault ? pPrDefault->FirstChildElement("w:pPr") : nullptr) {
                ReadGeometry(pPr, defaultGeometry_);
            }
            auto* rPrDefault = defaults->FirstChildElement("w:rPrDefault");
            ReadStyleFont(rPrDefault ? rPrDefault->FirstChildElement("w:rPr") : nullptr, defaultGeometry_);
            kerningStated = kerningStated || KerningOn(rPrDefault ? rPrDefault->FirstChildElement("w:rPr") : nullptr);
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
                kerningStated = kerningStated || KerningOn(style->FirstChildElement("w:rPr"));
            }
        }
        // Word kerns pairs of letters only where w:kern turns it on - in the
        // document defaults or the default paragraph style, for the text as
        // a whole.
        doc_->letterKerning = kerningStated;
    }

    // Whether run properties turn pair kerning on (w:kern, the size in half
    // points from which fonts are kerned; 0 = off).
    static bool KerningOn(tinyxml2::XMLElement* rPr) {
        auto* kern = rPr ? rPr->FirstChildElement("w:kern") : nullptr;
        return kern && kern->IntAttribute("w:val", 0) > 0;
    }

    // word/settings.xml: the distance between default tab stops.
    void LoadSettings() {
        std::string settingsXml;
        if (!zip_.ReadEntry(PartByRelationship("/settings", "settings.xml"), settingsXml)) return;
        tinyxml2::XMLDocument settings;
        if (settings.Parse(settingsXml.c_str()) != tinyxml2::XML_SUCCESS) return;
        NormalizeOoxmlPrefixes(settings);
        auto* root = settings.FirstChildElement("w:settings");
        auto* tab = root ? root->FirstChildElement("w:defaultTabStop") : nullptr;
        if (tab) {
            const int twips = tab->IntAttribute("w:val", 0);
            if (twips > 0) doc_->defaultTabStopPt = static_cast<float>(twips) / 20.0f;
        }
        // "Don't add leading between lines of text": lines without the
        // font's line gap.
        auto* compat = root ? root->FirstChildElement("w:compat") : nullptr;
        if (compat && compat->FirstChildElement("w:noLeading")) doc_->fontLineGap = false;
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
        if (over.caps >= 0) base.caps = over.caps;
    }

    void ReadStyleFont(tinyxml2::XMLElement* rPr, Geometry& g) const {
        if (!rPr) return;
        if (auto* sz = rPr->FirstChildElement("w:sz")) g.fontSize = sz->FloatAttribute("w:val", 0.0f) / 2.0f;
        if (auto* fonts = rPr->FirstChildElement("w:rFonts")) {
            const std::string family = FontFamilyOf(fonts);
            if (!family.empty()) g.fontFamily = family;
        }
        if (rPr->FirstChildElement("w:caps")) g.caps = ToggleOn(rPr, "w:caps") ? 1 : 0;
    }

    // The family a w:rFonts names for Latin text: by name, or the theme's
    // heading ("major") or body ("minor") font - Word's own templates name
    // no family at all, only "minorHAnsi".
    std::string themeMajorFont_, themeMinorFont_;
    std::string FontFamilyOf(tinyxml2::XMLElement* fonts) const {
        if (!fonts) return "";
        // Writer names a font with its alternatives ("Liberation Sans;Arial"):
        // the first is the font.
        auto first = [](std::string name) {
            const size_t more = name.find(';');
            if (more != std::string::npos) name.erase(more);
            while (!name.empty() && name.back() == ' ') name.pop_back();
            return name;
        };
        std::string family = first(Attr(fonts, "w:ascii"));
        if (!family.empty()) return family;
        std::string theme = Attr(fonts, "w:asciiTheme");
        if (theme.empty()) theme = Attr(fonts, "w:hAnsiTheme");
        if (theme.empty()) return first(Attr(fonts, "w:hAnsi"));
        return theme.rfind("major", 0) == 0 ? themeMajorFont_ : themeMinorFont_;
    }

    // Capitals for every letter of a text shown in capitals (w:caps): the
    // model has no such display, so the text itself is capitals. Latin,
    // Greek and Cyrillic letters; others are left as they are.
    static std::string ToCapitals(const std::string& text) {
        std::string out;
        out.reserve(text.size());
        for (size_t i = 0; i < text.size();) {
            const unsigned char lead = static_cast<unsigned char>(text[i]);
            size_t length = lead < 0x80 ? 1 : (lead >> 5) == 0x6 ? 2 : (lead >> 4) == 0xE ? 3 : (lead >> 3) == 0x1E ? 4 : 1;
            if (i + length > text.size()) length = 1;
            if (length == 1) {
                out.push_back(lead >= 'a' && lead <= 'z' ? static_cast<char>(lead - 32) : static_cast<char>(lead));
            } else if (length == 2) {
                unsigned code = ((lead & 0x1Fu) << 6) | (static_cast<unsigned char>(text[i + 1]) & 0x3Fu);
                if ((code >= 0xE0 && code <= 0xFE && code != 0xF7) || (code >= 0x3B1 && code <= 0x3C9 && code != 0x3C2)
                    || (code >= 0x430 && code <= 0x44F)) code -= 0x20;
                else if (code >= 0x450 && code <= 0x45F) code -= 0x50;
                else if (code == 0xFF) code = 0x178;
                else if (((code >= 0x100 && code <= 0x137) || (code >= 0x14A && code <= 0x177)) && (code & 1)) code -= 1;
                else if (((code >= 0x139 && code <= 0x148) || (code >= 0x179 && code <= 0x17E)) && !(code & 1)) code -= 1;
                out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                out.push_back(static_cast<char>(0x80 | (code & 0x3F)));
            } else {
                out.append(text, i, length);
            }
            i += length;
        }
        return out;
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

    // A paragraph a point high whose mark is in a 1 pt font: the writer's
    // trailer after a table, or its carrier of a section break.
    static bool IsTrailerParagraph(tinyxml2::XMLElement* pPr) {
        auto* spacing = pPr ? pPr->FirstChildElement("w:spacing") : nullptr;
        auto* mark = pPr ? pPr->FirstChildElement("w:rPr") : nullptr;
        auto* size = mark ? mark->FirstChildElement("w:sz") : nullptr;
        return spacing && size && std::string(Attr(spacing, "w:lineRule")) == "exact"
            && spacing->IntAttribute("w:line", 240) <= 40 && size->IntAttribute("w:val", 24) <= 4;
    }

    // An empty paragraph's line is as tall as its paragraph mark's font
    // (w:pPr/w:rPr), which is what Word measures it with.
    void ApplyParagraphMark(RichDocBlock& block, tinyxml2::XMLElement* pPr) const {
        if (!pPr) return;
        // A paragraph that only anchors pictures or frames has no words
        // either: its line is the mark's too.
        for (const RichTextRun& run : block.runs) {
            if (!run.IsInlineImage() && !run.IsTextFrame()) return;
        }
        Geometry mark;
        ReadStyleFont(pPr->FirstChildElement("w:rPr"), mark);
        if (mark.fontSize != kUnset && mark.fontSize > 0.0f) block.paragraphFontSizePt = mark.fontSize;
        if (!mark.fontFamily.empty()) block.paragraphFontFamily = mark.fontFamily;
    }

    void LoadNumbering() {
        std::string numberingXml;
        if (!zip_.ReadEntry(PartByRelationship("/numbering", "numbering.xml"), numberingXml)) return;
        tinyxml2::XMLDocument numbering;
        if (numbering.Parse(numberingXml.c_str()) != tinyxml2::XML_SUCCESS) return;
        NormalizeOoxmlPrefixes(numbering);
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
    bool ParagraphStyleCaps(tinyxml2::XMLElement* p) const {
        auto* pPr = p ? p->FirstChildElement("w:pPr") : nullptr;
        auto* pStyle = pPr ? pPr->FirstChildElement("w:pStyle") : nullptr;
        return StyleChainGeometry(pStyle ? Attr(pStyle, "w:val") : defaultParagraphStyle_).caps == 1;
    }

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
    RichStyleCharacter ReadStyleCharacter(tinyxml2::XMLElement* rPr) const {
        RichStyleCharacter character;
        if (!rPr) return character;
        if (rPr->FirstChildElement("w:b")) character.bold = ToggleOn(rPr, "w:b");
        if (rPr->FirstChildElement("w:i")) character.italic = ToggleOn(rPr, "w:i");
        if (rPr->FirstChildElement("w:u")) character.underline = ToggleOn(rPr, "w:u");
        if (rPr->FirstChildElement("w:strike")) character.strikethrough = ToggleOn(rPr, "w:strike");
        if (auto* sz = rPr->FirstChildElement("w:sz")) character.fontSizePt = sz->FloatAttribute("w:val", 0.0f) / 2.0f;
        if (auto* fonts = rPr->FirstChildElement("w:rFonts")) {
            const std::string family = FontFamilyOf(fonts);
            if (!family.empty()) character.fontFamily = family;
        }
        if (auto* color = rPr->FirstChildElement("w:color")) {
            const std::string v = Attr(color, "w:val");
            if (v.size() == 6 && v != "auto") character.color = "#" + v;
        }
        character.letterSpacingPt = CharacterSpacingPt(rPr);
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
        bool columnBreak = false;              // a column break (w:br w:type="column") was read
        RichTextRun pendingBreakFormat;        // the format of the run whose break is pending
        bool paragraphCaps = false;            // the paragraph's style sets its text in capitals (w:caps)
        // Complex field (w:fldChar begin / w:instrText / separate / end): the
        // result runs of a PAGE or NUMPAGES field are marked as that field.
        std::string fieldInstruction;
        bool inFieldResult = false;
        RichTextRun::Field field = RichTextRun::Field::Plain;
        std::string fieldArgument;           // SEQ's label, REF's / PAGEREF's bookmark
        RichTextRun::Change change = RichTextRun::Change::Unchanged;   // inside w:ins / w:del
        int revision = -1;
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
        if (const auto spacing = CharacterSpacingPt(rPr)) run.letterSpacingPt = spacing;
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
            run.fontFamily = FontFamilyOf(fonts);
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

    // A wp:anchor's wrap and position: how text wraps round a floating
    // picture or text box, and where it sits - aligned or offset, from the
    // page's edges, its text area (margin) or the paragraph.
    static void ApplyAnchorPlacement(tinyxml2::XMLElement* anchor, RichTextRun& run) {
        using Origin = RichTextRun::FloatOrigin;
        run.imageWrap = RichTextRun::ImageWrap::Square;
        if (!anchor) return;
        if (anchor->FirstChildElement("wp:wrapTopAndBottom")) {
            run.imageWrap = RichTextRun::ImageWrap::TopAndBottom;
        } else if (anchor->FirstChildElement("wp:wrapNone")) {
            run.imageWrap = Attr(anchor, "behindDoc") == std::string("1")
                ? RichTextRun::ImageWrap::BehindText : RichTextRun::ImageWrap::InFrontOfText;
        }
        auto offsetOf = [](tinyxml2::XMLElement* position, float& out) {
            auto* offset = position->FirstChildElement("wp:posOffset");
            if (!offset) return false;
            out = static_cast<float>(ParseEmu(offset->GetText())) / kEmuPerPoint;
            return true;
        };
        if (auto* positionH = anchor->FirstChildElement("wp:positionH")) {
            const std::string from = Attr(positionH, "relativeFrom");
            const bool page = from == "page" || from == "leftMargin" || from == "rightMargin"
                           || from == "insideMargin" || from == "outsideMargin";
            run.floatHorizontalFrom = page ? Origin::Page : Origin::Margin;
            float x = 0.0f;
            if (auto* align = positionH->FirstChildElement("wp:align")) {
                const std::string side = align->GetText() ? align->GetText() : "";
                run.imageFloatAlign = side == "right" || side == "outside" ? RichTextAlign::Right
                                    : side == "center" ? RichTextAlign::Center : RichTextAlign::Left;
            } else if (offsetOf(positionH, x)) {
                run.imageFloatAlign = RichTextAlign::Default;
                run.imageOffsetXPt = x;                 // left of its area too: a banner starts in the margin
            }
        }
        if (auto* positionV = anchor->FirstChildElement("wp:positionV")) {
            const std::string from = Attr(positionV, "relativeFrom");
            run.floatVerticalFrom = from == "margin" ? Origin::Margin
                                  : (from == "paragraph" || from == "line") ? Origin::Paragraph : Origin::Page;
            using VAlign = RichTextRun::FloatVerticalAlign;
            float y = 0.0f;
            if (auto* align = positionV->FirstChildElement("wp:align")) {
                const std::string side = align->GetText() ? align->GetText() : "";
                run.floatVerticalAlign = side == "center" ? VAlign::Middle
                                       : (side == "bottom" || side == "outside") ? VAlign::Bottom : VAlign::Top;
            } else if (offsetOf(positionV, y)) {
                run.imageOffsetYPt = y;                 // above its paragraph too
            }
        }
    }

    // A text box: Word's wps:wsp with a wps:txbx, or a VML v:textbox
    // (w:pict). Its paragraphs become a text frame floating where the shape
    // is anchored, with the shape's fill, outline and inner margin.
    // A text box's paragraphs and tables (w:txbxContent).
    std::vector<RichDocBlock> ParseTextBoxContent(tinyxml2::XMLElement* content) {
        const size_t start = doc_->blocks.size();
        for (auto* elem = content->FirstChildElement(); elem; elem = elem->NextSiblingElement()) {
            const std::string tag = elem->Name() ? elem->Name() : "";
            if (tag == "w:p") ParseParagraph(elem);
            else if (tag == "w:tbl") ParseTable(elem);
        }
        std::vector<RichDocBlock> blocks(std::make_move_iterator(doc_->blocks.begin() + static_cast<std::ptrdiff_t>(start)),
                                         std::make_move_iterator(doc_->blocks.end()));
        doc_->blocks.resize(start);
        return blocks;
    }

    bool ParseTextBox(tinyxml2::XMLElement* drawing, InlineContext& ctx) {
        auto* content = FindDescendant(drawing, "w:txbxContent");
        if (!content) return false;
        RichTextFrame frame;
        frame.blocks = ParseTextBoxContent(content);

        RichTextRun run;
        run.text = RichTextRun::kObjectReplacement;
        if (std::string(drawing->Name() ? drawing->Name() : "") == "w:pict") {
            if (!ReadVmlTextBox(drawing, frame, run)) return false;
        } else {
            if (auto* docPr = FindDescendant(drawing, "wp:docPr")) frame.name = Attr(docPr, "name");
            if (auto* extent = FindDescendant(drawing, "wp:extent")) {
                run.imageWidthPt = extent->FloatAttribute("cx", 0.0f) / kEmuPerPoint;
                run.imageHeightPt = extent->FloatAttribute("cy", 0.0f) / kEmuPerPoint;
            }
            if (auto* shape = FindDescendant(drawing, "wps:spPr")) {
                // srgbClr, or a theme colour (Word's white box is bg1).
                auto colourOf = [&](tinyxml2::XMLElement* fill) -> std::string { return DrawingColor(fill); };
                frame.backgroundColor = colourOf(shape->FirstChildElement("a:solidFill"));
                if (auto* line = shape->FirstChildElement("a:ln"); line && !line->FirstChildElement("a:noFill")) {
                    const std::string colour = colourOf(line->FirstChildElement("a:solidFill"));
                    if (!colour.empty()) {
                        frame.border.color = colour;
                        frame.border.widthPt = std::max(0.5f, line->FloatAttribute("w", 9525.0f) / kEmuPerPoint);
                    }
                }
            }
            // The inner margin (Word's default is 0.1 inch).
            if (auto* body = FindDescendant(drawing, "wps:bodyPr")) {
                frame.paddingPt = body->FloatAttribute("lIns", 91440.0f) / kEmuPerPoint;
                frame.textAnchor = TextAnchorOf(body);
            }
            if (drawing->FirstChildElement("wp:inline")) {
                run.imageWrap = RichTextRun::ImageWrap::TopAndBottom;   // in the line: on lines of its own
            } else {
                ApplyAnchorPlacement(drawing->FirstChildElement("wp:anchor"), run);
            }
        }
        run.imageAltText = frame.name;
        run.frameIndex = static_cast<int>(doc_->frames.size());
        doc_->frames.push_back(std::move(frame));
        run.lineBreakBefore = ctx.pendingLineBreak;
        ctx.pendingLineBreak = false;
        ctx.runs.push_back(std::move(run));
        return true;
    }

    // A CSS length ("368.5pt", " 2in") in points; a bare number is pixels.
    static float CssLengthPt(std::string value) {
        value.erase(std::remove(value.begin(), value.end(), ' '), value.end());
        if (!value.empty() && (std::isdigit(static_cast<unsigned char>(value.back())) || value.back() == '.')) value += "px";
        return WordFormatInternal::ParseLengthPt(value);
    }

    // A VML text box's size, place and look, from its v:shape's CSS style
    // ("position:absolute;margin-left:368.5pt;...;mso-position-horizontal-
    // relative:page") and attributes.
    static bool ReadVmlTextBox(tinyxml2::XMLElement* pict, RichTextFrame& frame, RichTextRun& run) {
        tinyxml2::XMLElement* shape = FindDescendant(pict, "v:shape");
        if (!shape) shape = FindDescendant(pict, "v:rect");
        if (!shape) return false;
        frame.name = Attr(shape, "ID");
        if (frame.name.empty()) frame.name = Attr(shape, "id");
        std::map<std::string, std::string> css;
        std::istringstream declarations(Attr(shape, "style"));
        std::string declaration;
        while (std::getline(declarations, declaration, ';')) {
            const size_t colon = declaration.find(':');
            if (colon == std::string::npos) continue;
            css[ToLower(declaration.substr(0, colon))] = declaration.substr(colon + 1);
        }
        auto length = [&](const char* name) {
            auto found = css.find(name);
            return found == css.end() ? 0.0f : CssLengthPt(found->second);
        };
        using Origin = RichTextRun::FloatOrigin;
        run.imageWidthPt = length("width");
        run.imageHeightPt = length("height");
        const std::string across = css["mso-position-horizontal-relative"];
        const std::string down = css["mso-position-vertical-relative"];
        run.floatHorizontalFrom = across == "page" ? Origin::Page : Origin::Margin;
        run.floatVerticalFrom = down == "page" ? Origin::Page : down == "margin" ? Origin::Margin : Origin::Paragraph;
        const std::string alignAcross = css["mso-position-horizontal"];
        if (alignAcross == "right" || alignAcross == "outside") run.imageFloatAlign = RichTextAlign::Right;
        else if (alignAcross == "center") run.imageFloatAlign = RichTextAlign::Center;
        else if (alignAcross == "left" || alignAcross == "inside") run.imageFloatAlign = RichTextAlign::Left;
        else {
            run.imageFloatAlign = RichTextAlign::Default;
            const float x = length("margin-left");
            run.imageOffsetXPt = x;
        }
        const float y = length("margin-top");
        run.imageOffsetYPt = y;
        // Wrapping: w10:wrap's type; none means over or under the text.
        run.imageWrap = RichTextRun::ImageWrap::Square;
        if (auto* wrap = FindDescendant(pict, "w10:wrap")) {
            const std::string type = Attr(wrap, "type");
            if (type == "topAndBottom") run.imageWrap = RichTextRun::ImageWrap::TopAndBottom;
            else if (type == "none") run.imageWrap = RichTextRun::ImageWrap::InFrontOfText;
        }
        if (std::string(Attr(shape, "filled")) != "f" && std::string(Attr(shape, "filled")) != "false") {
            const std::string fill = Attr(shape, "fillcolor");
            if (fill.size() == 7 && fill[0] == '#') frame.backgroundColor = fill;
        }
        if (std::string(Attr(shape, "stroked")) != "f" && std::string(Attr(shape, "stroked")) != "false") {
            std::string colour = Attr(shape, "strokecolor");
            if (colour.size() != 7 || colour[0] != '#') colour = "#000000";
            frame.border.color = colour;
            const float weight = CssLengthPt(Attr(shape, "strokeweight"));
            frame.border.widthPt = weight > 0.0f ? weight : 0.75f;
        }
        frame.paddingPt = 3.6f;                       // VML's default inset, 0.05 inch
        return true;
    }

    // ===== DRAWN SHAPES =====
    // The theme's colour scheme (word/theme/theme1.xml): "accent1" -> "4472C4".
    std::map<std::string, std::string> themeColors_;

    void LoadTheme() {
        // Office's own scheme, for a package without a theme.
        themeColors_ = {{"dk1", "000000"}, {"lt1", "FFFFFF"}, {"dk2", "44546A"}, {"lt2", "E7E6E6"},
                        {"accent1", "4472C4"}, {"accent2", "ED7D31"}, {"accent3", "A5A5A5"},
                        {"accent4", "FFC000"}, {"accent5", "5B9BD5"}, {"accent6", "70AD47"},
                        {"hlink", "0563C1"}, {"folHlink", "954F72"}};
        std::string path = partFolder_ + "theme/theme1.xml";
        for (const auto& [id, rel] : relationships_) {
            if (rel.type.size() >= 6 && rel.type.compare(rel.type.size() - 6, 6, "/theme") == 0) {
                path = ResolvePartPath(rel.target);
            }
        }
        std::string xml;
        if (!zip_.ReadEntry(path, xml)) return;
        tinyxml2::XMLDocument theme;
        if (theme.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS || !theme.RootElement()) return;
        NormalizeOoxmlPrefixes(theme);
        // The heading and body fonts styles name as "majorHAnsi" / "minorHAnsi".
        for (auto [tag, target] : {std::pair{"a:majorFont", &themeMajorFont_}, std::pair{"a:minorFont", &themeMinorFont_}}) {
            auto* font = FindDescendant(theme.RootElement(), tag);
            auto* latin = font ? font->FirstChildElement("a:latin") : nullptr;
            if (latin) *target = Attr(latin, "typeface");
        }
        auto* scheme = FindDescendant(theme.RootElement(), "a:clrScheme");
        for (auto* entry = scheme ? scheme->FirstChildElement() : nullptr; entry; entry = entry->NextSiblingElement()) {
            std::string name = entry->Name() ? entry->Name() : "";
            if (name.rfind("a:", 0) == 0) name = name.substr(2);
            std::string value;
            if (auto* rgb = entry->FirstChildElement("a:srgbClr")) value = Attr(rgb, "val");
            else if (auto* sys = entry->FirstChildElement("a:sysClr")) value = Attr(sys, "lastClr");
            if (value.size() == 6) themeColors_[name] = value;
        }
    }

    // A DrawingML colour (the element holding a:srgbClr, a:schemeClr, ...)
    // as "#RRGGBB", its tint, shade and luminance changes applied; `placeholder`
    // is the colour a style reference passes in (a:schemeClr val="phClr").
    std::string DrawingColor(tinyxml2::XMLElement* holder, const std::string& placeholder = "") const {
        if (!holder) return "";
        tinyxml2::XMLElement* colour = nullptr;
        std::string hex;
        for (auto* child = holder->FirstChildElement(); child && hex.empty(); child = child->NextSiblingElement()) {
            const std::string tag = child->Name() ? child->Name() : "";
            colour = child;
            if (tag == "a:srgbClr") hex = Attr(child, "val");
            else if (tag == "a:sysClr") hex = Attr(child, "lastClr");
            else if (tag == "a:scrgbClr") {
                auto channel = [&](const char* name) {
                    const float linear = std::clamp(child->FloatAttribute(name, 0.0f) / 100000.0f, 0.0f, 1.0f);
                    const float value = linear <= 0.0031308f ? linear * 12.92f
                                                             : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
                    return static_cast<int>(std::lround(value * 255.0f));
                };
                char buffer[8];
                std::snprintf(buffer, sizeof buffer, "%02X%02X%02X", channel("r"), channel("g"), channel("b"));
                hex = buffer;
            } else if (tag == "a:schemeClr") {
                std::string name = Attr(child, "val");
                if (name == "phClr") {
                    hex = placeholder.size() == 7 ? placeholder.substr(1) : "";
                    continue;
                }
                if (name == "bg1") name = "lt1";
                else if (name == "tx1") name = "dk1";
                else if (name == "bg2") name = "lt2";
                else if (name == "tx2") name = "dk2";
                auto found = themeColors_.find(name);
                if (found != themeColors_.end()) hex = found->second;
            } else if (tag == "a:prstClr") {
                static const std::map<std::string, std::string> named = {
                    {"black", "000000"}, {"white", "FFFFFF"}, {"red", "FF0000"}, {"green", "008000"},
                    {"blue", "0000FF"}, {"yellow", "FFFF00"}, {"gray", "808080"}, {"orange", "FFA500"}};
                auto found = named.find(Attr(child, "val"));
                if (found != named.end()) hex = found->second;
            }
        }
        if (hex.size() != 6 || !colour) return "";
        float rgb[3];
        for (int i = 0; i < 3; ++i) {
            rgb[i] = static_cast<float>(std::strtol(hex.substr(static_cast<size_t>(i) * 2, 2).c_str(), nullptr, 16)) / 255.0f;
        }
        for (auto* change = colour->FirstChildElement(); change; change = change->NextSiblingElement()) {
            const std::string tag = change->Name() ? change->Name() : "";
            const float amount = change->FloatAttribute("val", 100000.0f) / 100000.0f;
            if (tag == "a:shade") {
                for (float& c : rgb) c *= amount;
            } else if (tag == "a:tint") {
                for (float& c : rgb) c = c + (1.0f - c) * (1.0f - amount);
            } else if (tag == "a:lumMod" || tag == "a:lumOff") {
                // In HSL: lightness times lumMod, plus lumOff.
                const float hi = std::max({rgb[0], rgb[1], rgb[2]}), lo = std::min({rgb[0], rgb[1], rgb[2]});
                float l = (hi + lo) * 0.5f;
                float sat = 0.0f, hue = 0.0f;
                if (hi > lo) {
                    const float d = hi - lo;
                    sat = l > 0.5f ? d / (2.0f - hi - lo) : d / (hi + lo);
                    if (hi == rgb[0]) hue = (rgb[1] - rgb[2]) / d + (rgb[1] < rgb[2] ? 6.0f : 0.0f);
                    else if (hi == rgb[1]) hue = (rgb[2] - rgb[0]) / d + 2.0f;
                    else hue = (rgb[0] - rgb[1]) / d + 4.0f;
                    hue /= 6.0f;
                }
                l = std::clamp(tag == "a:lumMod" ? l * amount : l + amount, 0.0f, 1.0f);
                auto toRgb = [](float p, float q, float t) {
                    if (t < 0.0f) t += 1.0f;
                    if (t > 1.0f) t -= 1.0f;
                    if (t < 1.0f / 6.0f) return p + (q - p) * 6.0f * t;
                    if (t < 0.5f) return q;
                    if (t < 2.0f / 3.0f) return p + (q - p) * (2.0f / 3.0f - t) * 6.0f;
                    return p;
                };
                if (sat <= 0.0f) {
                    rgb[0] = rgb[1] = rgb[2] = l;
                } else {
                    const float q = l < 0.5f ? l * (1.0f + sat) : l + sat - l * sat;
                    const float p2 = 2.0f * l - q;
                    rgb[0] = toRgb(p2, q, hue + 1.0f / 3.0f);
                    rgb[1] = toRgb(p2, q, hue);
                    rgb[2] = toRgb(p2, q, hue - 1.0f / 3.0f);
                }
            }
        }
        char out[8];
        std::snprintf(out, sizeof out, "#%02X%02X%02X", static_cast<int>(std::lround(std::clamp(rgb[0], 0.0f, 1.0f) * 255.0f)),
                      static_cast<int>(std::lround(std::clamp(rgb[1], 0.0f, 1.0f) * 255.0f)),
                      static_cast<int>(std::lround(std::clamp(rgb[2], 0.0f, 1.0f) * 255.0f)));
        return out;
    }

    // A wps:wsp that is a plain text box: a rectangle with words, drawn as a
    // text frame with its own fill and border.
    static bool IsPlainTextBox(tinyxml2::XMLElement* wsp) {
        if (!wsp->FirstChildElement("wps:txbx")) return false;
        auto* spPr = wsp->FirstChildElement("wps:spPr");
        if (!spPr) return true;
        if (spPr->FirstChildElement("a:custGeom")) return false;
        auto* xfrm = spPr->FirstChildElement("a:xfrm");
        if (xfrm && (xfrm->IntAttribute("rot", 0) != 0)) return false;
        auto* preset = spPr->FirstChildElement("a:prstGeom");
        const std::string name = preset ? Attr(preset, "prst") : "rect";
        if (name != "rect") return false;
        // A fill or line from the theme (wps:style) needs the shape's colours.
        for (const char* tag : {"a:gradFill", "a:pattFill", "a:blipFill"}) {
            if (spPr->FirstChildElement(tag)) return false;
        }
        // A dashed outline is a shape's, not a frame border's.
        auto* line = spPr->FirstChildElement("a:ln");
        if (line) {
            auto* dash = line->FirstChildElement("a:prstDash");
            if (dash && std::string(Attr(dash, "val")) != "solid") return false;
        }
        // A shape coloured by its theme style (Word's "add text" to a shape)
        // states no fill or line of its own.
        if (auto* style = wsp->FirstChildElement("wps:style")) {
            auto* fillRef = style->FirstChildElement("a:fillRef");
            auto* lineRef = style->FirstChildElement("a:lnRef");
            const bool ownFill = spPr->FirstChildElement("a:noFill") || spPr->FirstChildElement("a:solidFill");
            const bool ownLine = line && (line->FirstChildElement("a:noFill") || line->FirstChildElement("a:solidFill"));
            if ((fillRef && fillRef->IntAttribute("idx", 0) > 0 && !ownFill)
                || (lineRef && lineRef->IntAttribute("idx", 0) > 0 && !ownLine)) return false;
        }
        return true;
    }

    struct ChildSpace { double offX = 0, offY = 0, extX = 1, extY = 1; double scaleX = 1, scaleY = 1; };

    // A shape's box (a:xfrm) in points within its parent: a top-level
    // shape's from (0, 0); a group member's mapped from the group's child
    // space (chOff / chExt) onto the group's box.
    static void ReadTransform(tinyxml2::XMLElement* xfrm, const ChildSpace& space, RichShape& shape) {
        if (!xfrm) return;
        shape.rotationDeg = static_cast<float>(xfrm->IntAttribute("rot", 0)) / 60000.0f;
        shape.flipH = std::string(Attr(xfrm, "flipH")) == "1" || std::string(Attr(xfrm, "flipH")) == "true";
        shape.flipV = std::string(Attr(xfrm, "flipV")) == "1" || std::string(Attr(xfrm, "flipV")) == "true";
        auto* off = xfrm->FirstChildElement("a:off");
        auto* ext = xfrm->FirstChildElement("a:ext");
        const double x = off ? static_cast<double>(ParseEmu(Attr(off, "x"))) : space.offX;
        const double y = off ? static_cast<double>(ParseEmu(Attr(off, "y"))) : space.offY;
        const double cx = ext ? static_cast<double>(ParseEmu(Attr(ext, "cx"))) : 0.0;
        const double cy = ext ? static_cast<double>(ParseEmu(Attr(ext, "cy"))) : 0.0;
        shape.xPt = static_cast<float>((x - space.offX) * space.scaleX / kEmuPerPoint);
        shape.yPt = static_cast<float>((y - space.offY) * space.scaleY / kEmuPerPoint);
        shape.widthPt = static_cast<float>(cx * space.scaleX / kEmuPerPoint);
        shape.heightPt = static_cast<float>(cy * space.scaleY / kEmuPerPoint);
    }

    // a:custGeom's outline, each point scaled into 0..1 across its path's
    // own width and height; arcs become curves.
    // Returns false when every path says it takes no fill.
    static bool ReadCustomGeometry(tinyxml2::XMLElement* geometry, RichShape& shape) {
        using Op = RichShape::PathPoint::Op;
        shape.kind = RichShape::Kind::Path;
        shape.path.clear();
        auto* list = geometry->FirstChildElement("a:pathLst");
        bool filled = false;
        for (auto* path = list ? list->FirstChildElement("a:path") : nullptr; path; path = path->NextSiblingElement("a:path")) {
            filled = filled || std::string(Attr(path, "fill")) != "none";
            double pw = static_cast<double>(ParseEmu(Attr(path, "w")));
            double ph = static_cast<double>(ParseEmu(Attr(path, "h")));
            if (pw <= 0.0) pw = std::max(1.0, static_cast<double>(shape.widthPt) * kEmuPerPoint);
            if (ph <= 0.0) ph = std::max(1.0, static_cast<double>(shape.heightPt) * kEmuPerPoint);
            double curX = 0.0, curY = 0.0;
            auto point = [&](tinyxml2::XMLElement* pt, double& px, double& py) {
                px = pt ? static_cast<double>(ParseEmu(Attr(pt, "x"))) : 0.0;
                py = pt ? static_cast<double>(ParseEmu(Attr(pt, "y"))) : 0.0;
            };
            auto add = [&](Op op, double x, double y) {
                RichShape::PathPoint p;
                p.op = op;
                p.x = static_cast<float>(x / pw);
                p.y = static_cast<float>(y / ph);
                shape.path.push_back(p);
                curX = x;
                curY = y;
            };
            auto curve = [&](double c1x, double c1y, double c2x, double c2y, double x, double y) {
                RichShape::PathPoint p;
                p.op = Op::Curve;
                p.c1x = static_cast<float>(c1x / pw); p.c1y = static_cast<float>(c1y / ph);
                p.c2x = static_cast<float>(c2x / pw); p.c2y = static_cast<float>(c2y / ph);
                p.x = static_cast<float>(x / pw); p.y = static_cast<float>(y / ph);
                shape.path.push_back(p);
                curX = x;
                curY = y;
            };
            for (auto* step = path->FirstChildElement(); step; step = step->NextSiblingElement()) {
                const std::string tag = step->Name() ? step->Name() : "";
                double x = 0, y = 0;
                if (tag == "a:moveTo" || tag == "a:lnTo") {
                    point(step->FirstChildElement("a:pt"), x, y);
                    add(tag == "a:moveTo" ? Op::Move : Op::Line, x, y);
                } else if (tag == "a:cubicBezTo") {
                    double c[6] = {};
                    int i = 0;
                    for (auto* pt = step->FirstChildElement("a:pt"); pt && i < 6; pt = pt->NextSiblingElement("a:pt"), i += 2) {
                        point(pt, c[i], c[i + 1]);
                    }
                    if (i == 6) curve(c[0], c[1], c[2], c[3], c[4], c[5]);
                } else if (tag == "a:quadBezTo") {
                    double c[4] = {};
                    int i = 0;
                    for (auto* pt = step->FirstChildElement("a:pt"); pt && i < 4; pt = pt->NextSiblingElement("a:pt"), i += 2) {
                        point(pt, c[i], c[i + 1]);
                    }
                    if (i == 4) {
                        curve(curX + (c[0] - curX) * 2.0 / 3.0, curY + (c[1] - curY) * 2.0 / 3.0,
                              c[2] + (c[0] - c[2]) * 2.0 / 3.0, c[3] + (c[1] - c[3]) * 2.0 / 3.0, c[2], c[3]);
                    }
                } else if (tag == "a:arcTo") {
                    // An elliptical arc from the current point: angles in
                    // 60000ths of a degree, as quarter-turn (or shorter) curves.
                    const double wR = static_cast<double>(ParseEmu(Attr(step, "wR")));
                    const double hR = static_cast<double>(ParseEmu(Attr(step, "hR")));
                    constexpr double kRad = 3.14159265358979323846 / 180.0 / 60000.0;
                    double start = static_cast<double>(ParseEmu(Attr(step, "stAng"))) * kRad;
                    const double sweep = static_cast<double>(ParseEmu(Attr(step, "swAng"))) * kRad;
                    const double cx = curX - wR * std::cos(start), cy = curY - hR * std::sin(start);
                    const int pieces = std::max(1, static_cast<int>(std::ceil(std::abs(sweep) / (3.14159265358979323846 / 2.0))));
                    const double piece = sweep / pieces;
                    const double k = 4.0 / 3.0 * std::tan(piece / 4.0);
                    for (int i = 0; i < pieces; ++i) {
                        const double a0 = start, a1 = start + piece;
                        const double x0 = cx + wR * std::cos(a0), y0 = cy + hR * std::sin(a0);
                        const double x1 = cx + wR * std::cos(a1), y1 = cy + hR * std::sin(a1);
                        curve(x0 - k * wR * std::sin(a0), y0 + k * hR * std::cos(a0),
                              x1 + k * wR * std::sin(a1), y1 - k * hR * std::cos(a1), x1, y1);
                        start = a1;
                    }
                } else if (tag == "a:close") {
                    RichShape::PathPoint p;
                    p.op = Op::Close;
                    shape.path.push_back(p);
                }
            }
        }
        return filled;
    }

    // Word's "rectangle: top corners rounded" (round2SameRect): its two top
    // corners rounded by adj1, its two bottom ones by adj2 - each a share of
    // the shorter side, so the outline depends on the box (an arch when adj1
    // is a half and the box is upright).
    static void ApplyRound2SameRect(tinyxml2::XMLElement* preset, RichShape& shape) {
        double adjust1 = 16667.0, adjust2 = 0.0;
        if (auto* list = preset->FirstChildElement("a:avLst")) {
            for (auto* gd = list->FirstChildElement("a:gd"); gd; gd = gd->NextSiblingElement("a:gd")) {
                const std::string name = Attr(gd, "name");
                const std::string formula = Attr(gd, "fmla");
                if (formula.rfind("val ", 0) != 0) continue;
                if (name == "adj1") WordFormatInternal::ReadNumberAt(formula, 4, adjust1);
                else if (name == "adj2") WordFormatInternal::ReadNumberAt(formula, 4, adjust2);
            }
        }
        const float w = shape.widthPt, h = shape.heightPt;
        if (w <= 0.0f || h <= 0.0f) return;
        const float shorter = std::min(w, h);
        const float top = shorter * std::clamp(static_cast<float>(adjust1 / 100000.0), 0.0f, 0.5f);
        const float bottom = shorter * std::clamp(static_cast<float>(adjust2 / 100000.0), 0.0f, 0.5f);
        const float k = 0.5523f;                    // a quarter circle as one curve
        using Op = RichShape::PathPoint::Op;
        shape.kind = RichShape::Kind::Path;
        shape.path.clear();
        auto point = [&](Op op, float x, float y) {
            RichShape::PathPoint p;
            p.op = op;
            p.x = x / w;
            p.y = y / h;
            shape.path.push_back(p);
        };
        auto curve = [&](float c1x, float c1y, float c2x, float c2y, float x, float y) {
            RichShape::PathPoint p;
            p.op = Op::Curve;
            p.c1x = c1x / w; p.c1y = c1y / h;
            p.c2x = c2x / w; p.c2y = c2y / h;
            p.x = x / w;     p.y = y / h;
            shape.path.push_back(p);
        };
        point(Op::Move, top, 0.0f);
        point(Op::Line, w - top, 0.0f);
        if (top > 0.0f) curve(w - top + top * k, 0.0f, w, top - top * k, w, top);
        point(Op::Line, w, h - bottom);
        if (bottom > 0.0f) curve(w, h - bottom + bottom * k, w - bottom + bottom * k, h, w - bottom, h);
        point(Op::Line, bottom, h);
        if (bottom > 0.0f) curve(bottom - bottom * k, h, 0.0f, h - bottom + bottom * k, 0.0f, h - bottom);
        point(Op::Line, 0.0f, top);
        if (top > 0.0f) curve(0.0f, top - top * k, top - top * k, 0.0f, top, 0.0f);
        RichShape::PathPoint close;
        close.op = Op::Close;
        shape.path.push_back(close);
    }

    // A shape's geometry, fill and outline (wps:spPr, pic:spPr), falling back
    // to its theme style's (wps:style) where it states none.
    void ReadShapeLook(tinyxml2::XMLElement* spPr, tinyxml2::XMLElement* styleRefs, RichShape& shape) {
        std::string styleFill, styleLine;
        float styleLineWidth = 0.0f;
        if (styleRefs) {
            if (auto* ref = styleRefs->FirstChildElement("a:fillRef"); ref && ref->IntAttribute("idx", 0) > 0) {
                styleFill = DrawingColor(ref);
            }
            if (auto* ref = styleRefs->FirstChildElement("a:lnRef"); ref && ref->IntAttribute("idx", 0) > 0) {
                styleLine = DrawingColor(ref);
                const int idx = ref->IntAttribute("idx", 1);
                styleLineWidth = idx >= 3 ? 1.5f : idx == 2 ? 1.0f : 0.5f;   // Office's line styles
            }
        }
        if (auto* preset = spPr ? spPr->FirstChildElement("a:prstGeom") : nullptr) {
            const std::string name = Attr(preset, "prst");
            shape.ApplyPreset(name);
            if (name == "round2SameRect") ApplyRound2SameRect(preset, shape);
            if (shape.kind == RichShape::Kind::RoundRectangle) {
                if (auto* list = preset->FirstChildElement("a:avLst")) {
                    for (auto* gd = list->FirstChildElement("a:gd"); gd; gd = gd->NextSiblingElement("a:gd")) {
                        const std::string formula = Attr(gd, "fmla");
                        if (std::string(Attr(gd, "name")) == "adj" && formula.rfind("val ", 0) == 0) {
                            double adjust = 16667.0;
                            WordFormatInternal::ReadNumberAt(formula, 4, adjust);
                            shape.cornerRadius = std::clamp(static_cast<float>(adjust / 100000.0), 0.0f, 0.5f);
                        }
                    }
                }
            }
        }
        bool takesFill = true;
        if (auto* custom = spPr && !spPr->FirstChildElement("a:prstGeom") ? spPr->FirstChildElement("a:custGeom") : nullptr) {
            takesFill = ReadCustomGeometry(custom, shape);
        }
        bool fillStated = false;
        if (spPr) {
            if (spPr->FirstChildElement("a:noFill")) {
                fillStated = true;
            } else if (auto* solid = spPr->FirstChildElement("a:solidFill")) {
                shape.fillColor = DrawingColor(solid);
                fillStated = true;
            } else if (auto* gradient = spPr->FirstChildElement("a:gradFill")) {
                ReadGradient(gradient, shape);
                fillStated = true;
            } else if (auto* picture = spPr->FirstChildElement("a:blipFill")) {
                // A picture fill: tiled, or stretched over the shape.
                auto* blip = picture->FirstChildElement("a:blip");
                shape.fillMediaIndex = blip ? LoadImageByRelId(Attr(blip, "r:embed")) : -1;
                shape.fillPictureTiled = picture->FirstChildElement("a:tile") != nullptr;
                fillStated = shape.fillMediaIndex >= 0;
            } else if (auto* pattern = spPr->FirstChildElement("a:pattFill")) {
                // A preset pattern: its foreground on its background.
                shape.fillColor = DrawingColor(pattern->FirstChildElement("a:fgClr"));
                if (shape.fillColor.empty()) shape.fillColor = "#000000";
                auto* back = pattern->FirstChildElement("a:bgClr");
                shape.patternBackColor = back ? DrawingColor(back) : "#FFFFFF";
                // A background made all but invisible is none.
                auto* backColour = back ? back->FirstChildElement() : nullptr;
                auto* alpha = backColour ? backColour->FirstChildElement("a:alpha") : nullptr;
                if (alpha && alpha->IntAttribute("val", 100000) < 50000) shape.patternBackColor.clear();
                shape.fillPattern = Attr(pattern, "prst");
                if (shape.fillPattern.empty()) shape.fillPattern = "pct5";   // DrawingML's default
                fillStated = true;
            }
        }
        if (!fillStated) shape.fillColor = styleFill;
        shape.lineColor = styleLine;
        if (styleLineWidth > 0.0f) shape.lineWidthPt = styleLineWidth;
        // An outer shadow: how far (dist) and which way (dir, 60000ths of a
        // degree clockwise from the right), its blur and its colour's alpha.
        auto* effects = spPr ? spPr->FirstChildElement("a:effectLst") : nullptr;
        if (auto* outer = effects ? effects->FirstChildElement("a:outerShdw") : nullptr) {
            const double distance = static_cast<double>(ParseEmu(Attr(outer, "dist"))) / kEmuPerPoint;
            const double direction = outer->DoubleAttribute("dir", 0.0) / 60000.0 * 3.14159265358979323846 / 180.0;
            shape.shadowOffsetXPt = static_cast<float>(distance * std::cos(direction));
            shape.shadowOffsetYPt = static_cast<float>(distance * std::sin(direction));
            shape.shadowBlurPt = static_cast<float>(ParseEmu(Attr(outer, "blurRad"))) / kEmuPerPoint;
            const std::string colour = DrawingColor(outer);
            shape.shadowColor = colour.empty() ? "#000000" : colour;
            shape.shadowOpacity = 1.0f;
            for (auto* kind = outer->FirstChildElement(); kind; kind = kind->NextSiblingElement()) {
                if (auto* alpha = kind->FirstChildElement("a:alpha")) {
                    shape.shadowOpacity = std::clamp(static_cast<float>(alpha->IntAttribute("val", 100000)) / 100000.0f, 0.0f, 1.0f);
                }
            }
        }
        if (auto* line = spPr ? spPr->FirstChildElement("a:ln") : nullptr) {
            if (line->Attribute("w")) shape.lineWidthPt = static_cast<float>(ParseEmu(Attr(line, "w"))) / kEmuPerPoint;
            if (line->FirstChildElement("a:noFill")) shape.lineColor.clear();
            else if (auto* solid = line->FirstChildElement("a:solidFill")) shape.lineColor = DrawingColor(solid);
            else if (auto* gradient = line->FirstChildElement("a:gradFill")) {
                auto* list = gradient->FirstChildElement("a:gsLst");
                shape.lineColor = DrawingColor(list ? list->FirstChildElement("a:gs") : nullptr);
            }
            if (auto* dash = line->FirstChildElement("a:prstDash")) {
                const std::string kind = Attr(dash, "val");
                if (kind == "dot" || kind == "sysDot") shape.dash = RichShape::Dash::Dotted;
                else if (!kind.empty() && kind != "solid") shape.dash = RichShape::Dash::Dashed;
            } else if (auto* custom = line->FirstChildElement("a:custDash")) {
                // Its first dash's length, in thousandths of a percent of the
                // line's width: one width or less is a dot.
                auto* first = custom->FirstChildElement("a:ds");
                shape.dash = first && first->IntAttribute("d", 300000) <= 100000 ? RichShape::Dash::Dotted
                                                                                  : RichShape::Dash::Dashed;
            }
            auto arrow = [](tinyxml2::XMLElement* end) {
                const std::string type = end ? Attr(end, "type") : "";
                return !type.empty() && type != "none";
            };
            shape.arrowStart = arrow(line->FirstChildElement("a:headEnd"));
            shape.arrowEnd = arrow(line->FirstChildElement("a:tailEnd"));
        }
        if (!shape.IsClosed() || !takesFill) shape.fillColor.clear();
    }

    // a:gradFill: its stops (a:gs, positions in thousandths of a percent),
    // and a:lin's angle (60000ths of a degree, clockwise from left-to-right)
    // or a:path's round kinds, from the centre out.
    void ReadGradient(tinyxml2::XMLElement* gradient, RichShape& shape) const {
        shape.gradientStops.clear();
        auto* list = gradient->FirstChildElement("a:gsLst");
        for (auto* stop = list ? list->FirstChildElement("a:gs") : nullptr; stop; stop = stop->NextSiblingElement("a:gs")) {
            const std::string colour = DrawingColor(stop);
            if (colour.empty()) continue;
            shape.gradientStops.push_back({std::clamp(stop->FloatAttribute("pos", 0.0f) / 100000.0f, 0.0f, 1.0f), colour});
        }
        std::stable_sort(shape.gradientStops.begin(), shape.gradientStops.end(),
                         [](const RichShape::GradientStop& a, const RichShape::GradientStop& b) { return a.position < b.position; });
        if (auto* linear = gradient->FirstChildElement("a:lin")) {
            shape.gradientAngleDeg = static_cast<float>(ParseEmu(Attr(linear, "ang"))) / 60000.0f;
        } else if (auto* path = gradient->FirstChildElement("a:path")) {
            shape.gradientRadial = true;
            // Its centre: the middle of a:fillToRect (insets from each side).
            if (auto* focus = path->FirstChildElement("a:fillToRect")) {
                const float l = focus->FloatAttribute("l", 50000.0f), t = focus->FloatAttribute("t", 50000.0f);
                const float r = focus->FloatAttribute("r", 50000.0f), b = focus->FloatAttribute("b", 50000.0f);
                shape.gradientCenterX = std::clamp((l + 100000.0f - r) * 0.5f / 100000.0f, 0.0f, 1.0f);
                shape.gradientCenterY = std::clamp((t + 100000.0f - b) * 0.5f / 100000.0f, 0.0f, 1.0f);
            }
        } else {
            shape.gradientAngleDeg = 90.0f;      // Word's default: top to bottom
        }
        shape.fillColor = shape.gradientStops.empty() ? "" : shape.gradientStops.front().color;
        if (shape.gradientStops.size() < 2) shape.gradientStops.clear();
    }

    static RichShape::TextAnchor TextAnchorOf(tinyxml2::XMLElement* bodyPr) {
        const std::string anchor = bodyPr ? Attr(bodyPr, "anchor") : "";
        if (anchor == "ctr") return RichShape::TextAnchor::Middle;
        if (anchor == "b") return RichShape::TextAnchor::Bottom;
        return RichShape::TextAnchor::Top;
    }

    // The words of a shape whose theme style colours them (wps:style's
    // fontRef - Word's white text on a blue shape) take that colour where
    // they state none.
    void ColourShapeText(std::vector<RichDocBlock>& blocks, tinyxml2::XMLElement* styleRefs) const {
        auto* fontRef = styleRefs ? styleRefs->FirstChildElement("a:fontRef") : nullptr;
        const std::string colour = DrawingColor(fontRef);
        if (colour.empty()) return;
        for (RichDocBlock& block : blocks) {
            for (RichTextRun& run : block.runs) {
                if (run.color.empty()) run.color = colour;
            }
        }
    }

    // One member of a drawing (wps:wsp, wpg:grpSp, pic:pic, or the top-level
    // wpg:wgp itself), in `space`. Its words go into a frame of their own
    // unless `ownBlocks` takes them (a lone shape's text is its frame's).
    bool ReadDrawingShape(tinyxml2::XMLElement* element, const ChildSpace& space, RichShape& shape,
                          std::vector<RichDocBlock>* ownBlocks, float* ownPadding, RichShape::TextAnchor* ownAnchor,
                          int depth) {
        if (depth > 16) return false;
        const std::string tag = element->Name() ? element->Name() : "";
        if (tag == "wps:wsp") {
            auto* spPr = element->FirstChildElement("wps:spPr");
            ReadTransform(spPr ? spPr->FirstChildElement("a:xfrm") : nullptr, space, shape);
            if (auto* cNvPr = element->FirstChildElement("wps:cNvPr")) shape.name = Attr(cNvPr, "name");
            auto* styleRefs = element->FirstChildElement("wps:style");
            ReadShapeLook(spPr, styleRefs, shape);
            if (auto* content = FindDescendant(element, "w:txbxContent")) {
                std::vector<RichDocBlock> blocks = ParseTextBoxContent(content);
                ColourShapeText(blocks, styleRefs);
                auto* bodyPr = element->FirstChildElement("wps:bodyPr");
                const float padding = (bodyPr ? bodyPr->FloatAttribute("lIns", 91440.0f) : 91440.0f) / kEmuPerPoint;
                if (ownBlocks) {
                    *ownBlocks = std::move(blocks);
                    if (ownPadding) *ownPadding = padding;
                    if (ownAnchor) *ownAnchor = TextAnchorOf(bodyPr);
                } else if (!blocks.empty()) {
                    RichTextFrame words;
                    words.blocks = std::move(blocks);
                    words.shapeText = true;
                    shape.textFrame = static_cast<int>(doc_->frames.size());
                    shape.textPaddingPt = padding;
                    shape.textAnchor = TextAnchorOf(bodyPr);
                    doc_->frames.push_back(std::move(words));
                }
            }
            return true;
        }
        if (tag == "pic:pic") {
            auto* spPr = element->FirstChildElement("pic:spPr");
            ReadTransform(spPr ? spPr->FirstChildElement("a:xfrm") : nullptr, space, shape);
            auto* blip = FindDescendant(element, "a:blip");
            shape.mediaIndex = blip ? LoadImageByRelId(Attr(blip, "r:embed")) : -1;
            if (shape.mediaIndex < 0) return false;
            ReadShapeLook(spPr, nullptr, shape);
            shape.fillColor.clear();
            return true;
        }
        if (tag == "wpg:wgp" || tag == "wpg:grpSp") {
            shape.kind = RichShape::Kind::Group;
            auto* groupPr = element->FirstChildElement("wpg:grpSpPr");
            auto* xfrm = groupPr ? groupPr->FirstChildElement("a:xfrm") : nullptr;
            if (tag == "wpg:grpSp") ReadTransform(xfrm, space, shape);
            // Its members' child space, mapped onto its box.
            ChildSpace inner;
            if (xfrm) {
                auto* chOff = xfrm->FirstChildElement("a:chOff");
                auto* chExt = xfrm->FirstChildElement("a:chExt");
                auto* ext = xfrm->FirstChildElement("a:ext");
                if (chOff) {
                    inner.offX = static_cast<double>(ParseEmu(Attr(chOff, "x")));
                    inner.offY = static_cast<double>(ParseEmu(Attr(chOff, "y")));
                }
                const double chX = chExt ? static_cast<double>(ParseEmu(Attr(chExt, "cx"))) : 0.0;
                const double chY = chExt ? static_cast<double>(ParseEmu(Attr(chExt, "cy"))) : 0.0;
                const double boxX = static_cast<double>(shape.widthPt) * kEmuPerPoint;
                const double boxY = static_cast<double>(shape.heightPt) * kEmuPerPoint;
                const double extX = ext ? static_cast<double>(ParseEmu(Attr(ext, "cx"))) : boxX;
                const double extY = ext ? static_cast<double>(ParseEmu(Attr(ext, "cy"))) : boxY;
                inner.scaleX = chX > 0.0 ? (boxX > 0.0 ? boxX : extX) / chX : 1.0;
                inner.scaleY = chY > 0.0 ? (boxY > 0.0 ? boxY : extY) / chY : 1.0;
            }
            for (auto* member = element->FirstChildElement(); member; member = member->NextSiblingElement()) {
                const std::string memberTag = member->Name() ? member->Name() : "";
                if (memberTag != "wps:wsp" && memberTag != "wpg:grpSp" && memberTag != "pic:pic") continue;
                RichShape child;
                if (ReadDrawingShape(member, inner, child, nullptr, nullptr, nullptr, depth + 1)) {
                    shape.children.push_back(std::move(child));
                }
            }
            return !shape.children.empty();
        }
        return false;
    }

    // A DrawingML shape or group (wps:wsp, wpg:wgp) in a w:drawing: a frame
    // holding the drawing, floating where it is anchored.
    void ParseDrawingShapes(tinyxml2::XMLElement* drawing, tinyxml2::XMLElement* graphicData, InlineContext& ctx) {
        RichTextRun run;
        run.text = RichTextRun::kObjectReplacement;
        if (auto* extent = FindDescendant(drawing, "wp:extent")) {
            run.imageWidthPt = static_cast<float>(ParseEmu(Attr(extent, "cx"))) / kEmuPerPoint;
            run.imageHeightPt = static_cast<float>(ParseEmu(Attr(extent, "cy"))) / kEmuPerPoint;
        }
        RichTextFrame frame;
        if (auto* docPr = FindDescendant(drawing, "wp:docPr")) frame.name = Attr(docPr, "name");
        auto* element = graphicData->FirstChildElement("wps:wsp");
        if (!element) element = graphicData->FirstChildElement("wpg:wgp");
        if (!element) return;
        RichShape shape;
        shape.widthPt = run.imageWidthPt;
        shape.heightPt = run.imageHeightPt;
        ChildSpace top;
        const bool lone = std::string(element->Name()) == "wps:wsp";
        if (lone) {
            frame.paddingPt = 7.2f;
            if (!ReadDrawingShape(element, top, shape, &frame.blocks, &frame.paddingPt, &frame.textAnchor, 0)) return;
            // The anchor places it: the box is the frame's.
            shape.xPt = shape.yPt = 0.0f;
            shape.widthPt = run.imageWidthPt;
            shape.heightPt = run.imageHeightPt;
        } else {
            if (!ReadDrawingShape(element, top, shape, nullptr, nullptr, nullptr, 0)) return;
            shape.xPt = shape.yPt = 0.0f;
        }
        frame.shapes.push_back(std::move(shape));
        if (drawing->FirstChildElement("wp:inline")) {
            run.imageWrap = RichTextRun::ImageWrap::Inline;         // a drawing standing in the line
        } else {
            ApplyAnchorPlacement(drawing->FirstChildElement("wp:anchor"), run);
        }
        run.imageAltText = frame.name;
        run.frameIndex = static_cast<int>(doc_->frames.size());
        doc_->frames.push_back(std::move(frame));
        run.lineBreakBefore = ctx.pendingLineBreak;
        ctx.pendingLineBreak = false;
        ctx.runs.push_back(std::move(run));
    }

    // ===== VML SHAPES =====
    // Word 2007's drawings, and every DOCX's fallback: v:line, v:rect,
    // v:roundrect, v:oval, v:shape (a path or a preset), v:group.
    static std::map<std::string, std::string> VmlStyle(const tinyxml2::XMLElement* shape) {
        std::map<std::string, std::string> css;
        std::istringstream declarations(Attr(shape, "style"));
        std::string declaration;
        while (std::getline(declarations, declaration, ';')) {
            const size_t colon = declaration.find(':');
            if (colon == std::string::npos) continue;
            std::string name = ToLower(declaration.substr(0, colon));
            name.erase(std::remove(name.begin(), name.end(), ' '), name.end());
            css[name] = declaration.substr(colon + 1);
        }
        return css;
    }

    static std::string VmlColour(std::string value) {
        value.erase(std::remove(value.begin(), value.end(), ' '), value.end());
        const size_t space = value.find('[');
        if (space != std::string::npos) value = value.substr(0, space);
        if (value.size() == 7 && value[0] == '#') return value;
        if (value.size() == 4 && value[0] == '#') {
            return std::string("#") + value[1] + value[1] + value[2] + value[2] + value[3] + value[3];
        }
        static const std::map<std::string, std::string> named = {
            {"black", "#000000"}, {"white", "#FFFFFF"}, {"red", "#FF0000"}, {"green", "#008000"},
            {"blue", "#0000FF"}, {"yellow", "#FFFF00"}, {"gray", "#808080"}, {"silver", "#C0C0C0"},
            {"navy", "#000080"}, {"maroon", "#800000"}, {"teal", "#008080"}, {"purple", "#800080"}};
        auto found = named.find(ToLower(value));
        return found == named.end() ? "" : found->second;
    }

    static bool VmlOff(const char* value) {
        const std::string v = value ? value : "";
        return v == "f" || v == "false" || v == "0";
    }

    // v:fill type="gradient" (linear) or "gradientRadial": the shape's
    // colour and color2, placed by focus and turned by angle as Word 97's
    // shades are (WordFormatInternal::ApplyOfficeShade).
    static void ReadVmlGradient(tinyxml2::XMLElement* fill, RichShape& shape) {
        const std::string type = Attr(fill, "type");
        if ((type != "gradient" && type != "gradientRadial") || shape.fillColor.empty()) return;
        std::string back = VmlColour(Attr(fill, "color2"));
        if (back.empty()) back = "#FFFFFF";
        double angle = 0.0, focus = 0.0;
        WordFormatInternal::ReadNumberAt(Attr(fill, "angle"), 0, angle);
        const std::string focusText = Attr(fill, "focus");
        WordFormatInternal::ReadNumberAt(focusText, 0, focus);
        if (!focusText.empty() && focusText.find('%') == std::string::npos) focus *= 100.0;   // a fraction
        WordFormatInternal::ApplyOfficeShade(shape, shape.fillColor, back, focus, angle, type == "gradientRadial");
        // A round one's centre (focusposition, fractions of the box).
        double centre[2] = {0.0, 0.0};                       // VML's default: the top-left corner
        if (shape.gradientRadial) {
            WordFormatInternal::ReadNumbers(Attr(fill, "focusposition"), centre, 2);
            shape.gradientCenterX = static_cast<float>(std::clamp(centre[0], 0.0, 1.0));
            shape.gradientCenterY = static_cast<float>(std::clamp(centre[1], 0.0, 1.0));
        }
    }

    // A VML shape's look: fill, stroke (v:stroke's dash style and arrows).
    static void ReadVmlLook(tinyxml2::XMLElement* element, RichShape& shape) {
        if (!VmlOff(Attr(element, "filled"))) {
            std::string fill = VmlColour(Attr(element, "fillcolor"));
            if (fill.empty() && !element->Attribute("fillcolor")) fill = "#FFFFFF";   // VML's default fill
            shape.fillColor = fill;
        }
        if (auto* fill = element->FirstChildElement("v:fill")) {
            if (VmlOff(Attr(fill, "on"))) shape.fillColor.clear();
            else if (!VmlColour(Attr(fill, "color")).empty() && !shape.fillColor.empty()) shape.fillColor = VmlColour(Attr(fill, "color"));
            ReadVmlGradient(fill, shape);
        }
        if (!VmlOff(Attr(element, "stroked"))) {
            std::string colour = VmlColour(Attr(element, "strokecolor"));
            shape.lineColor = colour.empty() ? "#000000" : colour;
            const float weight = CssLengthPt(Attr(element, "strokeweight"));
            shape.lineWidthPt = weight > 0.0f ? weight : 0.75f;
        }
        if (auto* stroke = element->FirstChildElement("v:stroke")) {
            if (!VmlColour(Attr(stroke, "color")).empty() && !shape.lineColor.empty()) shape.lineColor = VmlColour(Attr(stroke, "color"));
            const std::string weight = Attr(stroke, "weight");
            if (!weight.empty() && !shape.lineColor.empty()) {
                // A bare number is EMU here; else a CSS length.
                const bool emu = weight.find_first_not_of("0123456789") == std::string::npos;
                double emuWidth = 0.0;
                if (emu) WordFormatInternal::ReadNumberAt(weight, 0, emuWidth);
                const float points = emu ? static_cast<float>(emuWidth) / kEmuPerPoint : CssLengthPt(weight);
                if (points > 0.0f) shape.lineWidthPt = points;
            }
            if (VmlOff(Attr(stroke, "on"))) shape.lineColor.clear();
            const std::string dash = Attr(stroke, "dashstyle");
            if (dash.find("dot") != std::string::npos && dash.find("dash") == std::string::npos) shape.dash = RichShape::Dash::Dotted;
            else if (!dash.empty() && dash != "solid") shape.dash = RichShape::Dash::Dashed;
            const std::string start = Attr(stroke, "startarrow"), end = Attr(stroke, "endarrow");
            shape.arrowStart = !start.empty() && start != "none";
            shape.arrowEnd = !end.empty() && end != "none";
        }
        if (!shape.IsClosed()) shape.fillColor.clear();
    }

    // A VML path ("m 0,0 l 100,0 x e"): m, l, c, x (close), e (end), with
    // their relative forms t, r, v; scaled by the coordinate size.
    static void ReadVmlPath(const std::string& text, double sizeX, double sizeY, RichShape& shape) {
        using Op = RichShape::PathPoint::Op;
        shape.kind = RichShape::Kind::Path;
        shape.path.clear();
        std::vector<double> numbers;
        char command = 0;
        double curX = 0, curY = 0;
        auto flush = [&]() {
            auto emit = [&](Op op, double x, double y) {
                RichShape::PathPoint p;
                p.op = op;
                p.x = static_cast<float>(x / sizeX);
                p.y = static_cast<float>(y / sizeY);
                shape.path.push_back(p);
                curX = x;
                curY = y;
            };
            const bool relative = command == 't' || command == 'r' || command == 'v';
            const double baseX = relative ? curX : 0.0, baseY = relative ? curY : 0.0;
            if (command == 'm' || command == 't') {
                if (numbers.size() >= 2) emit(Op::Move, baseX + numbers[0], baseY + numbers[1]);
            } else if (command == 'l' || command == 'r') {
                for (size_t i = 0; i + 1 < numbers.size(); i += 2) {
                    const double bx = command == 'r' ? curX : 0.0, by = command == 'r' ? curY : 0.0;
                    emit(Op::Line, bx + numbers[i], by + numbers[i + 1]);
                }
            } else if (command == 'c' || command == 'v') {
                for (size_t i = 0; i + 5 < numbers.size(); i += 6) {
                    const double bx = command == 'v' ? curX : 0.0, by = command == 'v' ? curY : 0.0;
                    RichShape::PathPoint p;
                    p.op = Op::Curve;
                    p.c1x = static_cast<float>((bx + numbers[i]) / sizeX);
                    p.c1y = static_cast<float>((by + numbers[i + 1]) / sizeY);
                    p.c2x = static_cast<float>((bx + numbers[i + 2]) / sizeX);
                    p.c2y = static_cast<float>((by + numbers[i + 3]) / sizeY);
                    curX = bx + numbers[i + 4];
                    curY = by + numbers[i + 5];
                    p.x = static_cast<float>(curX / sizeX);
                    p.y = static_cast<float>(curY / sizeY);
                    shape.path.push_back(p);
                }
            }
            numbers.clear();
        };
        size_t i = 0;
        while (i < text.size()) {
            const char c = text[i];
            if (std::isalpha(static_cast<unsigned char>(c))) {
                flush();
                command = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (command == 'x') {
                    RichShape::PathPoint p;
                    p.op = Op::Close;
                    shape.path.push_back(p);
                }
                ++i;
            } else if (c == '-' || c == '+' || std::isdigit(static_cast<unsigned char>(c)) || c == '.') {
                double value = 0.0;
                const size_t next = WordFormatInternal::ReadNumberAt(text, i, value);
                if (next == i) { ++i; continue; }
                numbers.push_back(value);
                i = next;
            } else {
                if (c == ',' && i + 1 < text.size() && text[i + 1] == ',') numbers.push_back(0.0);   // ",," is a zero
                ++i;
            }
        }
        flush();
    }

    // One VML shape (or group) in `space`: the parent's coordinate origin
    // and size (coordorigin / coordsize) mapped onto its box in points.
    bool ReadVmlShape(tinyxml2::XMLElement* element, const ChildSpace& space, RichShape& shape, int depth) {
        if (depth > 16) return false;
        const std::string tag = element->Name() ? element->Name() : "";
        auto css = VmlStyle(element);
        auto number = [&](const char* name) {
            auto found = css.find(name);
            double value = 0.0;
            if (found != css.end()) WordFormatInternal::ReadNumbers(found->second, &value, 1);
            return value;
        };
        if (depth == 0) {
            shape.widthPt = CssLengthPt(css["width"]);
            shape.heightPt = CssLengthPt(css["height"]);
        } else {
            // A member: its left/top/width/height in the group's coordinates.
            shape.xPt = static_cast<float>((number("left") - space.offX) * space.scaleX);
            shape.yPt = static_cast<float>((number("top") - space.offY) * space.scaleY);
            shape.widthPt = static_cast<float>(number("width") * space.scaleX);
            shape.heightPt = static_cast<float>(number("height") * space.scaleY);
        }
        shape.rotationDeg = static_cast<float>(number("rotation"));
        const std::string flip = css["flip"];
        shape.flipH = flip.find('x') != std::string::npos;
        shape.flipV = flip.find('y') != std::string::npos;
        shape.name = Attr(element, "id");
        if (tag == "v:group") {
            shape.kind = RichShape::Kind::Group;
            ChildSpace inner;
            double origin[2] = {0, 0}, size[2] = {1000, 1000};
            WordFormatInternal::ReadNumbers(Attr(element, "coordorigin"), origin, 2);
            if (WordFormatInternal::ReadNumbers(Attr(element, "coordsize"), size, 2) < 2) size[0] = size[1] = 1000;
            const double ox = origin[0], oy = origin[1], sx = size[0], sy = size[1];
            inner.offX = ox;
            inner.offY = oy;
            inner.scaleX = sx > 0 ? shape.widthPt / sx : 1.0;
            inner.scaleY = sy > 0 ? shape.heightPt / sy : 1.0;
            for (auto* member = element->FirstChildElement(); member; member = member->NextSiblingElement()) {
                RichShape child;
                if (ReadVmlShape(member, inner, child, depth + 1)) shape.children.push_back(std::move(child));
            }
            return !shape.children.empty();
        }
        if (tag == "v:line") {
            // from / to, in the parent's coordinates (points at the top level).
            double x1 = 0, y1 = 0, x2 = 0, y2 = 0;
            const std::string fromText = Attr(element, "from"), toText = Attr(element, "to");
            auto parse = [&](const std::string& value, double& x, double& y) {
                const size_t comma = value.find(',');
                if (comma == std::string::npos) return;
                if (depth == 0) {
                    x = CssLengthPt(value.substr(0, comma));
                    y = CssLengthPt(value.substr(comma + 1));
                } else {
                    double pair[2] = {0, 0};
                    WordFormatInternal::ReadNumbers(value, pair, 2);
                    x = pair[0];
                    y = pair[1];
                }
            };
            parse(fromText, x1, y1);
            parse(toText, x2, y2);
            const double scaleX = depth == 0 ? 1.0 : space.scaleX, scaleY = depth == 0 ? 1.0 : space.scaleY;
            const double offX = depth == 0 ? 0.0 : space.offX, offY = depth == 0 ? 0.0 : space.offY;
            shape.kind = RichShape::Kind::Line;
            shape.xPt = static_cast<float>((std::min(x1, x2) - offX) * scaleX);
            shape.yPt = static_cast<float>((std::min(y1, y2) - offY) * scaleY);
            shape.widthPt = static_cast<float>(std::abs(x2 - x1) * scaleX);
            shape.heightPt = static_cast<float>(std::abs(y2 - y1) * scaleY);
            shape.flipH = x2 < x1;
            shape.flipV = y2 < y1;
            if (shape.flipH && shape.flipV) shape.flipH = shape.flipV = false;   // the same line, drawn back
        } else if (tag == "v:rect") {
            shape.kind = RichShape::Kind::Rectangle;
        } else if (tag == "v:roundrect") {
            shape.kind = RichShape::Kind::RoundRectangle;
            const std::string arc = Attr(element, "arcsize");
            if (!arc.empty()) {
                double value = 0.0;
                WordFormatInternal::ReadNumberAt(arc, 0, value);
                if (arc.back() == 'f') value /= 65536.0;
                else if (arc.back() == '%') value /= 100.0;
                shape.cornerRadius = static_cast<float>(std::clamp(value, 0.0, 0.5));
            }
        } else if (tag == "v:oval") {
            shape.kind = RichShape::Kind::Ellipse;
        } else if (tag == "v:shape" || tag == "v:polyline") {
            std::string path = Attr(element, "path");
            double size[2] = {21600, 21600};
            if (WordFormatInternal::ReadNumbers(Attr(element, "coordsize"), size, 2) < 2) size[0] = size[1] = 21600;
            const double sx = size[0], sy = size[1];
            if (tag == "v:polyline") {
                // points="x1,y1 x2,y2 ..." in points.
                std::string all = Attr(element, "points");
                std::replace(all.begin(), all.end(), ' ', ',');
                std::istringstream values(all);
                std::vector<double> numbers;
                for (std::string token; std::getline(values, token, ',');) {
                    if (!token.empty()) numbers.push_back(CssLengthPt(token));
                }
                double minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
                for (size_t i = 0; i + 1 < numbers.size(); i += 2) {
                    minX = std::min(minX, numbers[i]); maxX = std::max(maxX, numbers[i]);
                    minY = std::min(minY, numbers[i + 1]); maxY = std::max(maxY, numbers[i + 1]);
                }
                if (numbers.size() < 4) return false;
                shape.kind = RichShape::Kind::Path;
                for (size_t i = 0; i + 1 < numbers.size(); i += 2) {
                    RichShape::PathPoint p;
                    p.op = i == 0 ? RichShape::PathPoint::Op::Move : RichShape::PathPoint::Op::Line;
                    p.x = maxX > minX ? static_cast<float>((numbers[i] - minX) / (maxX - minX)) : 0.0f;
                    p.y = maxY > minY ? static_cast<float>((numbers[i + 1] - minY) / (maxY - minY)) : 0.0f;
                    shape.path.push_back(p);
                }
                if (depth == 0) {
                    shape.widthPt = static_cast<float>(maxX - minX);
                    shape.heightPt = static_cast<float>(maxY - minY);
                }
            } else if (!path.empty()) {
                ReadVmlPath(path, sx > 0 ? sx : 21600, sy > 0 ? sy : 21600, shape);
            } else {
                // A preset by its shape type (o:spt, or the type's "#_x0000_t5").
                int type = element->IntAttribute("o:spt", 0);
                const std::string typeRef = Attr(element, "type");
                const size_t t = typeRef.find("_t");
                if (type == 0 && t != std::string::npos) type = std::atoi(typeRef.c_str() + t + 2);
                if (!ApplyOfficeArtShapeType(type, shape)) return false;
            }
        } else {
            return false;
        }
        ReadVmlLook(element, shape);
        if (auto* box = element->FirstChildElement("v:textbox")) {
            if (auto* content = box->FirstChildElement("w:txbxContent")) {
                std::vector<RichDocBlock> blocks = ParseTextBoxContent(content);
                if (!blocks.empty()) {
                    RichTextFrame words;
                    words.blocks = std::move(blocks);
                    words.shapeText = true;
                    shape.textFrame = static_cast<int>(doc_->frames.size());
                    doc_->frames.push_back(std::move(words));
                }
            }
        }
        return true;
    }

    // Where a VML shape sits: floating (position:absolute) from the page,
    // its margin or its paragraph, wrapped as w10:wrap says; else in the
    // line, on lines of its own.
    static void ReadVmlPlacement(tinyxml2::XMLElement* element, std::map<std::string, std::string>& css, RichTextRun& run) {
        using Origin = RichTextRun::FloatOrigin;
        if (css["position"].find("absolute") == std::string::npos) {
            run.imageWrap = RichTextRun::ImageWrap::TopAndBottom;
            return;
        }
        auto length = [&](const char* name) {
            auto found = css.find(name);
            return found == css.end() ? 0.0f : CssLengthPt(found->second);
        };
        const std::string across = css["mso-position-horizontal-relative"];
        const std::string down = css["mso-position-vertical-relative"];
        run.floatHorizontalFrom = across == "page" ? Origin::Page : Origin::Margin;
        run.floatVerticalFrom = down == "page" ? Origin::Page : down == "margin" ? Origin::Margin : Origin::Paragraph;
        const std::string alignAcross = css["mso-position-horizontal"];
        if (alignAcross == "right" || alignAcross == "outside") run.imageFloatAlign = RichTextAlign::Right;
        else if (alignAcross == "center") run.imageFloatAlign = RichTextAlign::Center;
        else if (alignAcross == "left" || alignAcross == "inside") run.imageFloatAlign = RichTextAlign::Left;
        else {
            run.imageFloatAlign = RichTextAlign::Default;
            const float x = length("margin-left") + length("left");
            run.imageOffsetXPt = x;
        }
        const float y = length("margin-top") + length("top");
        run.imageOffsetYPt = y;
        run.imageWrap = RichTextRun::ImageWrap::InFrontOfText;
        if (auto* wrap = FindDescendant(element, "w10:wrap")) {
            const std::string type = Attr(wrap, "type");
            if (type == "topAndBottom") run.imageWrap = RichTextRun::ImageWrap::TopAndBottom;
            else if (type == "square" || type == "tight" || type == "through") run.imageWrap = RichTextRun::ImageWrap::Square;
        }
        const std::string z = css["z-index"];
        if (!z.empty() && z[0] == '-') run.imageWrap = RichTextRun::ImageWrap::BehindText;
    }

    // A VML picture (v:shape with v:imagedata, as Word 2003 and older
    // documents keep a logo): a picture in the line, or floating where its
    // CSS position says.
    void ParseVmlPicture(tinyxml2::XMLElement* element, InlineContext& ctx) {
        auto* data = FindDescendant(element, "v:imagedata");
        std::string relId = Attr(data, "r:id");
        if (relId.empty()) relId = Attr(data, "r:pict");
        const int mediaIndex = LoadImageByRelId(relId);
        if (mediaIndex < 0) return;
        auto css = VmlStyle(element);
        RichTextRun run;
        run.text = RichTextRun::kObjectReplacement;
        run.mediaIndex = mediaIndex;
        run.imageWidthPt = CssLengthPt(css["width"]);
        run.imageHeightPt = CssLengthPt(css["height"]);
        run.imageAltText = Attr(element, "alt");
        if (run.imageAltText.empty()) run.imageAltText = Attr(data, "o:title");
        if (css["position"].find("absolute") != std::string::npos) ReadVmlPlacement(element, css, run);
        run.lineBreakBefore = ctx.pendingLineBreak;
        ctx.pendingLineBreak = false;
        ctx.runs.push_back(std::move(run));
    }

    // w:pict without a text box: its VML shapes, as a drawing floating where
    // the first of them says (its CSS position), or in the line.
    void ParseVmlDrawing(tinyxml2::XMLElement* pict, InlineContext& ctx) {
        for (auto* element = pict->FirstChildElement(); element; element = element->NextSiblingElement()) {
            const std::string tag = element->Name() ? element->Name() : "";
            if (tag == "v:shapetype" || tag == "w10:wrap" || tag == "o:lock") continue;
            if ((tag == "v:shape" || tag == "v:rect") && FindDescendant(element, "v:imagedata")) {
                ParseVmlPicture(element, ctx);                                       // a VML picture
                continue;
            }
            RichShape shape;
            ChildSpace top;
            if (!ReadVmlShape(element, top, shape, 0)) continue;
            RichTextRun run;
            run.text = RichTextRun::kObjectReplacement;
            auto css = VmlStyle(element);
            if (tag == "v:line") {
                run.imageWidthPt = std::max(shape.widthPt, 0.0f);
                run.imageHeightPt = std::max(shape.heightPt, 0.0f);
                // A line's place is its from point: the CSS margin is unused.
                css["margin-left"] = std::to_string(shape.xPt) + "pt";
                css["margin-top"] = std::to_string(shape.yPt) + "pt";
            } else {
                run.imageWidthPt = shape.widthPt;
                run.imageHeightPt = shape.heightPt;
            }
            shape.xPt = shape.yPt = 0.0f;
            ReadVmlPlacement(element, css, run);
            RichTextFrame frame;
            frame.name = shape.name;
            frame.shapes.push_back(std::move(shape));
            run.imageAltText = frame.name;
            run.frameIndex = static_cast<int>(doc_->frames.size());
            doc_->frames.push_back(std::move(frame));
            run.lineBreakBefore = ctx.pendingLineBreak;
            ctx.pendingLineBreak = false;
            ctx.runs.push_back(std::move(run));
        }
    }

    // ===== CHARTS =====
    // A chart (DrawingML c:chartSpace in its own part): drawn from the
    // values the file keeps with it, as a drawing of shapes - columns or
    // lines for its series, a value axis with its grid, the categories under
    // it and a legend. Columns for a column chart (side by side, stacked or
    // stacked to 100%), bars lying down for a bar chart, lines for a line or
    // area chart, slices for a pie or doughnut; any other kind is drawn as
    // columns of its series.
    static void CollectChartPoints(tinyxml2::XMLElement* from, std::vector<std::string>& out) {
        for (auto* child = from ? from->FirstChildElement() : nullptr; child; child = child->NextSiblingElement()) {
            if (std::string(child->Name() ? child->Name() : "") == "c:pt") {
                const size_t index = static_cast<size_t>(std::max(0, child->IntAttribute("idx", static_cast<int>(out.size()))));
                if (index < 4096) {
                    if (out.size() <= index) out.resize(index + 1);
                    auto* value = child->FirstChildElement("c:v");
                    out[index] = value && value->GetText() ? value->GetText() : "";
                }
            } else {
                CollectChartPoints(child, out);
            }
        }
    }

    bool ParseChart(tinyxml2::XMLElement* drawing, tinyxml2::XMLElement* graphicData, InlineContext& ctx) {
        auto* reference = FindDescendant(graphicData, "c:chart");
        if (!reference) return false;
        auto relation = relationships_.find(Attr(reference, "r:id"));
        if (relation == relationships_.end()) return false;
        std::string xml;
        if (!zip_.ReadEntry(ResolvePartPath(relation->second.target), xml)) return false;
        tinyxml2::XMLDocument chart;
        if (chart.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS || !chart.RootElement()) return false;
        auto* plot = FindDescendant(chart.RootElement(), "c:plotArea");
        if (!plot) return false;

        struct Series {
            std::string name, color;
            std::vector<double> values;
            std::vector<double> given;          // the values as the file has them (`values` become shares in a per-cent chart)
            bool stated = false;                // the file states its colour
            std::vector<double> across;         // a scatter chart's x of each value
            bool joined = true, marked = true;  // ...and whether its points are joined by a line, and marked
        };
        std::vector<Series> series;
        std::vector<std::string> categories;
        bool lines = false, pie = false, horizontal = false, stacked = false, percent = false, scatter = false;
        bool area = false, doughnut = false, lineMarkers = false;
        std::vector<std::string> pointColors;       // a pie's slices, where the file colours them
        static const char* const kPalette[] = {"#4472C4", "#ED7D31", "#A5A5A5", "#FFC000", "#5B9BD5", "#70AD47"};
        for (auto* kind = plot->FirstChildElement(); kind; kind = kind->NextSiblingElement()) {
            const std::string tag = kind->Name() ? kind->Name() : "";
            if (tag.size() < 7 || tag.compare(tag.size() - 5, 5, "Chart") != 0) continue;
            if (series.empty()) {
                lines = tag == "c:lineChart" || tag == "c:areaChart" || tag == "c:scatterChart";
                scatter = tag == "c:scatterChart";
                area = tag == "c:areaChart" || tag == "c:area3DChart";
                doughnut = tag == "c:doughnutChart";
                if (auto* marker = kind->FirstChildElement("c:marker")) lineMarkers = std::string(Attr(marker, "val")) != "0";
                pie = tag == "c:pieChart" || tag == "c:pie3DChart" || tag == "c:doughnutChart" || tag == "c:ofPieChart";
                if (auto* direction = kind->FirstChildElement("c:barDir")) horizontal = std::string(Attr(direction, "val")) == "bar";
                if (auto* grouping = kind->FirstChildElement("c:grouping")) {
                    const std::string how = Attr(grouping, "val");
                    percent = how == "percentStacked";
                    stacked = (!lines || area) && (how == "stacked" || percent);
                }
            }
            for (auto* ser = kind->FirstChildElement("c:ser"); ser; ser = ser->NextSiblingElement("c:ser")) {
                Series one;
                auto* title = ser->FirstChildElement("c:tx");       // (a series need not have a name)
                if (auto* name = title ? FindDescendant(title, "c:v") : nullptr) one.name = name->GetText() ? name->GetText() : "";
                if (auto* look = ser->FirstChildElement("c:spPr")) {
                    auto* fill = look->FirstChildElement("a:solidFill");
                    if (!fill) {
                        auto* line = look->FirstChildElement("a:ln");
                        fill = line ? line->FirstChildElement("a:solidFill") : nullptr;
                    }
                    if (fill) one.color = DrawingColor(fill);
                }
                one.stated = !one.color.empty();
                if (one.color.empty()) one.color = kPalette[series.size() % 6];
                if (pie && series.empty()) {
                    for (auto* point = ser->FirstChildElement("c:dPt"); point; point = point->NextSiblingElement("c:dPt")) {
                        auto* index = point->FirstChildElement("c:idx");
                        auto* look = point->FirstChildElement("c:spPr");
                        auto* fill = look ? look->FirstChildElement("a:solidFill") : nullptr;
                        const size_t at = static_cast<size_t>(std::max(0, index ? index->IntAttribute("val", 0) : 0));
                        if (!fill || at >= 4096) continue;
                        if (pointColors.size() <= at) pointColors.resize(at + 1);
                        pointColors[at] = DrawingColor(fill);
                    }
                }
                std::vector<std::string> points;
                auto* values = ser->FirstChildElement("c:val");
                if (!values) values = ser->FirstChildElement("c:yVal");
                CollectChartPoints(values, points);
                for (const std::string& point : points) {
                    double value = 0.0;
                    if (!point.empty()) WordFormatInternal::ReadNumberAt(point, 0, value);
                    one.values.push_back(value);
                }
                if (tag == "c:scatterChart") {
                    // Each point's own x (its number in the row where the
                    // file gives none or words), and how the points show.
                    std::vector<std::string> xs;
                    CollectChartPoints(ser->FirstChildElement("c:xVal"), xs);
                    for (size_t i = 0; i < one.values.size(); i++) {
                        double x = static_cast<double>(i + 1);
                        if (i < xs.size() && !xs[i].empty()) {
                            size_t end = WordFormatInternal::ReadNumberAt(xs[i], 0, x);
                            if (end == 0) x = static_cast<double>(i + 1);
                        }
                        one.across.push_back(x);
                    }
                    auto* look = ser->FirstChildElement("c:spPr");
                    auto* line = look ? look->FirstChildElement("a:ln") : nullptr;
                    one.joined = !(line && line->FirstChildElement("a:noFill"));
                    if (auto* style = kind->FirstChildElement("c:scatterStyle")) {
                        if (std::string(Attr(style, "val")) == "marker") one.joined = false;
                    }
                    if (auto* marker = ser->FirstChildElement("c:marker")) {
                        auto* symbol = marker->FirstChildElement("c:symbol");
                        if (symbol && std::string(Attr(symbol, "val")) == "none") one.marked = false;
                        auto* markerLook = marker->FirstChildElement("c:spPr");
                        auto* fill = markerLook ? markerLook->FirstChildElement("a:solidFill") : nullptr;
                        if (fill && !(look && (look->FirstChildElement("a:solidFill") || (line && line->FirstChildElement("a:solidFill"))))) {
                            one.color = DrawingColor(fill);
                            one.stated = true;
                        }
                    }
                    if (!one.joined && !one.marked) one.marked = true;      // something to see
                }
                if (categories.empty()) CollectChartPoints(ser->FirstChildElement("c:cat"), categories);
                if (tag != "c:scatterChart") {
                    if (auto* marker = ser->FirstChildElement("c:marker")) {
                        auto* symbol = marker->FirstChildElement("c:symbol");
                        if (symbol && std::string(Attr(symbol, "val")) == "none") one.marked = false;
                    }
                }
                one.given = one.values;
                if (!one.values.empty()) series.push_back(std::move(one));
            }
        }
        if (series.empty()) return false;
        size_t count = categories.size();
        double highest = 0.0;
        for (const Series& one : series) {
            count = std::max(count, one.values.size());
            for (double value : one.values) highest = std::max(highest, value);
        }
        if (count == 0) return false;
        if (stacked) {
            // Stacked: a category's values one on the other - as shares of
            // their sum where the chart says per cent.
            highest = 0.0;
            for (size_t c = 0; c < count; c++) {
                double sum = 0.0;
                for (const Series& one : series) sum += c < one.values.size() ? std::max(0.0, one.values[c]) : 0.0;
                if (percent && sum > 0.0) {
                    for (Series& one : series) {
                        if (c < one.values.size()) one.values[c] = std::max(0.0, one.values[c]) * 100.0 / sum;
                    }
                    sum = 100.0;
                }
                highest = std::max(highest, sum);
            }
        }
        // The value axis: a round step, five or so of them to the top.
        // (The reader stays free of the UI stack, so the step is worked out
        // here, by the rule of the chart engine's NiceNumber.)
        auto roundStep = [](double rough) {
            if (!(rough > 0.0) || !std::isfinite(rough)) return 1.0;
            const double magnitude = std::pow(10.0, std::floor(std::log10(rough)));
            const double share = rough / magnitude;
            return (share <= 1.0 ? 1.0 : share <= 2.0 ? 2.0 : share <= 5.0 ? 5.0 : 10.0) * magnitude;
        };
        const double step = roundStep(highest / 5.0);
        const int steps = std::max(1, static_cast<int>(std::ceil(highest / step - 1e-9)));
        const double top = step * steps;

        RichTextRun run;
        run.text = RichTextRun::kObjectReplacement;
        if (auto* extent = FindDescendant(drawing, "wp:extent")) {
            run.imageWidthPt = static_cast<float>(ParseEmu(Attr(extent, "cx"))) / kEmuPerPoint;
            run.imageHeightPt = static_cast<float>(ParseEmu(Attr(extent, "cy"))) / kEmuPerPoint;
        }
        if (run.imageWidthPt < 40.0f || run.imageHeightPt < 40.0f) return false;
        const float width = run.imageWidthPt, height = run.imageHeightPt;
        const bool legend = FindDescendant(chart.RootElement(), "c:legend") != nullptr;
        const float legendWidth = legend ? std::min(90.0f, width * 0.25f) : 0.0f;
        // (Beside horizontal bars the categories take the room at the left.)
        const float left = horizontal ? std::min(70.0f, width * 0.25f) : 26.0f;
        const float plotTop = 8.0f, right = width - legendWidth - 6.0f, bottom = height - 18.0f;
        const float plotWidth = std::max(10.0f, right - left), plotHeight = std::max(10.0f, bottom - plotTop);

        RichShape group;
        group.kind = RichShape::Kind::Group;
        group.widthPt = width;
        group.heightPt = height;
        {
            // The chart's area: nothing to see, but it keeps the drawing as
            // large as the chart where a group is as large as its members
            // (ODF's draw:g).
            RichShape area;
            area.widthPt = width;
            area.heightPt = height;
            group.children.push_back(std::move(area));
        }
        auto words = [&](float x, float y, float w, float h, const std::string& text, RichTextAlign align) {
            RichTextFrame label;
            label.shapeText = true;
            RichDocBlock paragraph;
            paragraph.align = align;
            paragraph.spaceBeforePt = 0.0f;
            paragraph.spaceAfterPt = 0.0f;
            RichTextRun letters;
            letters.text = text;
            letters.fontSizePt = 7.0f;
            paragraph.runs.push_back(std::move(letters));
            label.blocks.push_back(std::move(paragraph));
            RichShape box;
            box.xPt = x;
            box.yPt = y;
            box.widthPt = w;
            box.heightPt = h;
            box.textPaddingPt = 0.0f;
            box.textFrame = static_cast<int>(doc_->frames.size());
            doc_->frames.push_back(std::move(label));
            group.children.push_back(std::move(box));
        };
        auto rule = [&](float x, float y, float w, float h, const char* color) {
            RichShape line;
            line.kind = RichShape::Kind::Line;
            line.xPt = x;
            line.yPt = y;
            line.widthPt = w;
            line.heightPt = h;
            line.lineColor = color;
            line.lineWidthPt = 0.5f;
            group.children.push_back(std::move(line));
        };
        auto filled = [&](float x, float y, float w, float h, const std::string& color) {
            if (w <= 0.0f || h <= 0.0f) return;
            RichShape box;
            box.xPt = x;
            box.yPt = y;
            box.widthPt = w;
            box.heightPt = h;
            box.fillColor = color;
            group.children.push_back(std::move(box));
        };
        // What the legend names: the series - or, for a pie, its slices.
        std::vector<std::pair<std::string, std::string>> keys;      // name, colour
        if (pie) {
            // A pie: the first series' values as slices, clockwise from
            // twelve o'clock, each a path of arcs in the pie's square.
            const Series& one = series.front();
            double total = 0.0;
            for (double value : one.values) total += std::max(0.0, value);
            if (total <= 0.0) return false;
            const float side = std::max(10.0f, std::min(width - legendWidth - 12.0f, height - 12.0f));
            const float pieLeft = (width - legendWidth - side) * 0.5f, pieTop = (height - side) * 0.5f;
            constexpr double kTurn = 6.283185307179586;
            double angle = -kTurn / 4.0;
            for (size_t i = 0; i < one.values.size(); i++) {
                const std::string color = i < pointColors.size() && !pointColors[i].empty() ? pointColors[i] : kPalette[i % 6];
                keys.emplace_back(i < categories.size() ? categories[i] : one.name, color);
                const double sweep = std::max(0.0, one.values[i]) / total * kTurn;
                if (sweep <= 0.0) continue;
                RichShape slice;
                slice.kind = RichShape::Kind::Path;
                slice.xPt = pieLeft;
                slice.yPt = pieTop;
                slice.widthPt = side;
                slice.heightPt = side;
                slice.fillColor = color;
                slice.lineColor = "#FFFFFF";
                slice.lineWidthPt = 0.75f;
                auto at = [](double a) { return std::pair<float, float>(static_cast<float>(0.5 + 0.5 * std::cos(a)),
                                                                        static_cast<float>(0.5 + 0.5 * std::sin(a))); };
                RichShape::PathPoint point;
                point.op = RichShape::PathPoint::Op::Move;
                point.x = point.y = 0.5f;
                slice.path.push_back(point);
                point.op = RichShape::PathPoint::Op::Line;
                std::tie(point.x, point.y) = at(angle);
                slice.path.push_back(point);
                const int pieces = std::max(1, static_cast<int>(std::ceil(sweep / (kTurn / 4.0) - 1e-9)));
                const double piece = sweep / pieces, reach = 4.0 / 3.0 * std::tan(piece / 4.0) * 0.5;
                for (int k = 0; k < pieces; k++) {
                    const double from = angle + piece * k, to = from + piece;
                    RichShape::PathPoint arc;
                    arc.op = RichShape::PathPoint::Op::Curve;
                    arc.c1x = static_cast<float>(0.5 + 0.5 * std::cos(from) - reach * std::sin(from));
                    arc.c1y = static_cast<float>(0.5 + 0.5 * std::sin(from) + reach * std::cos(from));
                    arc.c2x = static_cast<float>(0.5 + 0.5 * std::cos(to) + reach * std::sin(to));
                    arc.c2y = static_cast<float>(0.5 + 0.5 * std::sin(to) - reach * std::cos(to));
                    std::tie(arc.x, arc.y) = at(to);
                    slice.path.push_back(arc);
                }
                RichShape::PathPoint close;
                close.op = RichShape::PathPoint::Op::Close;
                slice.path.push_back(close);
                group.children.push_back(std::move(slice));
                angle += sweep;
            }
        } else if (scatter) {
            // Points over two value axes: each runs from a round number
            // under its lowest value (nought, unless the values lie far
            // from it) to one over its highest, or as the file states.
            for (const Series& one : series) keys.emplace_back(one.name, one.color);
            struct Axis { double low = 0.0, high = 1.0, step = 1.0; };
            auto axisOver = [&](bool along) {
                double low = 0.0, high = 0.0;
                bool any = false;
                for (const Series& one : series) {
                    for (double v : along ? one.across : one.values) {
                        low = any ? std::min(low, v) : v;
                        high = any ? std::max(high, v) : v;
                        any = true;
                    }
                }
                if (low > 0.0 && low < high * 5.0 / 6.0) low = 0.0;
                if (high < 0.0 && high > low * 5.0 / 6.0) high = 0.0;
                if (high <= low) { high = low + 1.0; if (low > 0.0) low = std::max(0.0, low - 1.0); }
                // What the file states for this axis: the one at the foot
                // or top is the points' x, the one at a side their value.
                bool statedLow = false, statedHigh = false;
                for (auto* ax = plot->FirstChildElement("c:valAx"); ax; ax = ax->NextSiblingElement("c:valAx")) {
                    auto* side = ax->FirstChildElement("c:axPos");
                    const std::string pos = side ? Attr(side, "val") : "";
                    if ((pos == "b" || pos == "t") != along) continue;
                    auto* scaling = ax->FirstChildElement("c:scaling");
                    if (!scaling) continue;
                    if (auto* least = scaling->FirstChildElement("c:min")) { low = least->DoubleAttribute("val", low); statedLow = true; }
                    if (auto* most = scaling->FirstChildElement("c:max")) { high = most->DoubleAttribute("val", high); statedHigh = true; }
                    if (high <= low) high = low + 1.0;
                    break;
                }
                Axis axis;
                axis.step = roundStep((high - low) / 5.0);
                axis.low = statedLow ? low : std::floor(low / axis.step + 1e-9) * axis.step;
                axis.high = statedHigh ? high : std::ceil(high / axis.step - 1e-9) * axis.step;
                return axis;
            };
            const Axis xAxis = axisOver(true), yAxis = axisOver(false);
            auto label = [](double v, double step) {
                if (std::abs(v) < step * 1e-9) v = 0.0;
                std::ostringstream text;
                text.imbue(std::locale::classic());
                text << v;
                return text.str();
            };
            auto share = [](const Axis& axis, double v) {
                return static_cast<float>(std::clamp((v - axis.low) / (axis.high - axis.low), 0.0, 1.0));
            };
            // The grid: a rule and its value at each step of both axes, the
            // axes themselves where the other's nought is (else at its low end).
            const float noughtX = left + plotWidth * share(xAxis, 0.0), noughtY = bottom - plotHeight * share(yAxis, 0.0);
            for (double v = std::ceil(yAxis.low / yAxis.step - 1e-9) * yAxis.step; v <= yAxis.high + yAxis.step * 1e-6; v += yAxis.step) {
                const float y = bottom - plotHeight * share(yAxis, v);
                rule(left, y, plotWidth, 0.0f, "#D0D0D0");
                words(0.0f, y - 5.0f, left - 4.0f, 10.0f, label(v, yAxis.step), RichTextAlign::Right);
            }
            for (double v = std::ceil(xAxis.low / xAxis.step - 1e-9) * xAxis.step; v <= xAxis.high + xAxis.step * 1e-6; v += xAxis.step) {
                const float x = left + plotWidth * share(xAxis, v);
                rule(x, plotTop, 0.0f, plotHeight, "#D0D0D0");
                words(x - 20.0f, bottom + 3.0f, 40.0f, 12.0f, label(v, xAxis.step), RichTextAlign::Center);
            }
            rule(left, noughtY, plotWidth, 0.0f, "#808080");
            rule(noughtX, plotTop, 0.0f, plotHeight, "#808080");
            for (const Series& one : series) {
                if (one.joined && one.values.size() > 1) {
                    RichShape path;
                    path.kind = RichShape::Kind::Path;
                    path.xPt = left;
                    path.yPt = plotTop;
                    path.widthPt = plotWidth;
                    path.heightPt = plotHeight;
                    path.lineColor = one.color;
                    path.lineWidthPt = 1.5f;
                    for (size_t c = 0; c < one.values.size(); c++) {
                        RichShape::PathPoint point;
                        point.op = c == 0 ? RichShape::PathPoint::Op::Move : RichShape::PathPoint::Op::Line;
                        point.x = share(xAxis, one.across[c]);
                        point.y = 1.0f - share(yAxis, one.values[c]);
                        path.path.push_back(point);
                    }
                    group.children.push_back(std::move(path));
                }
                if (!one.marked) continue;
                for (size_t c = 0; c < one.values.size(); c++) {
                    constexpr float kMark = 5.0f;
                    RichShape mark;
                    mark.kind = RichShape::Kind::Ellipse;
                    mark.xPt = left + plotWidth * share(xAxis, one.across[c]) - kMark * 0.5f;
                    mark.yPt = bottom - plotHeight * share(yAxis, one.values[c]) - kMark * 0.5f;
                    mark.widthPt = kMark;
                    mark.heightPt = kMark;
                    mark.fillColor = one.color;
                    group.children.push_back(std::move(mark));
                }
            }
        } else {
            for (const Series& one : series) keys.emplace_back(one.name, one.color);
            auto value = [&](int i) {
                std::ostringstream text;
                text.imbue(std::locale::classic());
                text << step * i;
                return text.str() + (percent ? "%" : "");
            };
            const float slot = (horizontal ? plotHeight : plotWidth) / static_cast<float>(count);
            if (horizontal) {
                // Bars lying down: the values along the foot, the
                // categories up the left, the first at the bottom.
                for (int i = 0; i <= steps; i++) {
                    const float x = left + plotWidth * static_cast<float>(i) / static_cast<float>(steps);
                    rule(x, plotTop, 0.0f, plotHeight, i == 0 ? "#808080" : "#D0D0D0");
                    words(x - 20.0f, bottom + 3.0f, 40.0f, 12.0f, value(i), RichTextAlign::Center);
                }
                rule(left, bottom, plotWidth, 0.0f, "#808080");
                for (size_t c = 0; c < count && c < categories.size(); c++) {
                    words(0.0f, bottom - slot * (static_cast<float>(c) + 0.5f) - 5.0f, left - 4.0f, 10.0f, categories[c],
                          RichTextAlign::Right);
                }
            } else {
                // The grid and its values, from the foot up.
                for (int i = 0; i <= steps; i++) {
                    const float y = bottom - plotHeight * static_cast<float>(i) / static_cast<float>(steps);
                    rule(left, y, plotWidth, 0.0f, i == 0 ? "#808080" : "#D0D0D0");
                    words(0.0f, y - 5.0f, left - 4.0f, 10.0f, value(i), RichTextAlign::Right);
                }
                rule(left, plotTop, 0.0f, plotHeight, "#808080");
                for (size_t c = 0; c < count && c < categories.size(); c++) {
                    words(left + slot * static_cast<float>(c), bottom + 3.0f, slot, 12.0f, categories[c], RichTextAlign::Center);
                }
            }
            if (lines) {
                for (const Series& one : series) {
                    RichShape path;
                    path.kind = RichShape::Kind::Path;
                    path.xPt = left;
                    path.yPt = plotTop;
                    path.widthPt = plotWidth;
                    path.heightPt = plotHeight;
                    path.lineColor = one.color;
                    path.lineWidthPt = 1.5f;
                    for (size_t c = 0; c < one.values.size(); c++) {
                        RichShape::PathPoint point;
                        point.op = c == 0 ? RichShape::PathPoint::Op::Move : RichShape::PathPoint::Op::Line;
                        point.x = (static_cast<float>(c) + 0.5f) / static_cast<float>(count);
                        point.y = 1.0f - static_cast<float>(std::clamp(one.values[c] / top, 0.0, 1.0));
                        path.path.push_back(point);
                    }
                    group.children.push_back(std::move(path));
                }
            } else {
                // Columns (or bars): side by side in their category's slot,
                // or one on the other.
                const float extent = horizontal ? plotWidth : plotHeight;
                const float thick = slot * 0.7f / (stacked ? 1.0f : static_cast<float>(series.size()));
                for (size_t c = 0; c < count; c++) {
                    float base = 0.0f;
                    for (size_t s = 0; s < series.size(); s++) {
                        if (c >= series[s].values.size()) continue;
                        const float length = extent * static_cast<float>(std::clamp(series[s].values[c] / top, 0.0, 1.0));
                        const float across = slot * static_cast<float>(c) + slot * 0.15f
                                           + (stacked ? 0.0f : thick * static_cast<float>(s));
                        if (horizontal) filled(left + base, bottom - across - thick, length, thick, series[s].color);
                        else filled(left + across, bottom - base - length, thick, length, series[s].color);
                        if (stacked) base += length;
                    }
                }
            }
        }
        if (legend) {
            const float x = right + 10.0f;
            float y = plotTop + std::max(0.0f, (plotHeight - 11.0f * static_cast<float>(keys.size())) * 0.5f);
            for (const auto& [name, color] : keys) {
                filled(x, y + 2.0f, 6.0f, 6.0f, color);
                words(x + 9.0f, y, std::max(10.0f, width - x - 9.0f), 10.0f, name, RichTextAlign::Left);
                y += 11.0f;
            }
        }

        RichTextFrame frame;
        if (auto* docPr = FindDescendant(drawing, "wp:docPr")) frame.name = Attr(docPr, "name");
        if (frame.name.empty()) frame.name = "Chart";
        frame.shapes.push_back(std::move(group));
        // The chart's data, for the view's chart engine (the shapes above
        // are the same chart for whoever has none).
        frame.drawingWidthPt = width;
        frame.drawingHeightPt = height;
        RichChart& data = frame.chart;
        data.kind = pie ? (doughnut ? RichChart::Kind::Doughnut : RichChart::Kind::Pie)
                  : scatter ? RichChart::Kind::Scatter
                  : area ? RichChart::Kind::Area
                  : lines ? RichChart::Kind::Line
                  : horizontal ? RichChart::Kind::Bar : RichChart::Kind::Column;
        data.stacked = stacked;
        data.percent = percent;
        data.legend = legend;
        data.categories = categories;
        data.pointColors = pointColors;
        if (auto* title = FindDescendant(chart.RootElement(), "c:title")) {
            std::vector<tinyxml2::XMLElement*> open{title};
            while (!open.empty()) {              // its a:t texts, in document order
                auto* at = open.back();
                open.pop_back();
                if (at->Name() && std::string(at->Name()) == "a:t" && at->GetText()) data.title += at->GetText();
                std::vector<tinyxml2::XMLElement*> children;
                for (auto* child = at->FirstChildElement(); child; child = child->NextSiblingElement()) children.push_back(child);
                open.insert(open.end(), children.rbegin(), children.rend());
            }
        }
        for (const Series& one : series) {
            RichChartSeries kept;
            kept.name = one.name;
            kept.color = one.stated ? one.color : std::string();
            kept.values = percent ? one.given : one.values;
            kept.xValues = one.across;
            kept.joined = scatter ? one.joined : true;
            kept.marked = scatter ? one.marked : (lines && !area && lineMarkers && one.marked);
            data.series.push_back(std::move(kept));
        }
        if (drawing->FirstChildElement("wp:inline")) run.imageWrap = RichTextRun::ImageWrap::Inline;
        else ApplyAnchorPlacement(drawing->FirstChildElement("wp:anchor"), run);
        run.imageAltText = frame.name;
        run.frameIndex = static_cast<int>(doc_->frames.size());
        doc_->frames.push_back(std::move(frame));
        run.lineBreakBefore = ctx.pendingLineBreak;
        ctx.pendingLineBreak = false;
        ctx.runs.push_back(std::move(run));
        return true;
    }

    void ParseDrawing(tinyxml2::XMLElement* drawing, InlineContext& ctx) {
        if (std::string(drawing->Name() ? drawing->Name() : "") == "w:pict") {
            // VML: a text box, else drawn shapes.
            if (!ParseTextBox(drawing, ctx)) ParseVmlDrawing(drawing, ctx);
            return;
        }
        auto* graphicData = FindDescendant(drawing, "a:graphicData");
        const std::string uri = graphicData ? Attr(graphicData, "uri") : "";
        if (uri.find("wordprocessingGroup") != std::string::npos
            || uri.find("wordprocessingShape") != std::string::npos) {
            // A plain rectangle of text keeps the text box's own look; any
            // other shape, or a group, is a drawing.
            auto* wsp = graphicData->FirstChildElement("wps:wsp");
            if (wsp && IsPlainTextBox(wsp)) ParseTextBox(drawing, ctx);
            else ParseDrawingShapes(drawing, graphicData, ctx);
            return;
        }
        if (uri.find("/chart") != std::string::npos && ParseChart(drawing, graphicData, ctx)) return;
        auto* blip = FindDescendant(drawing, "a:blip");
        if (!blip) {
            ParseTextBox(drawing, ctx);
            return;
        }
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
            // Turned in the line too.
            if (auto* spPr = FindDescendant(drawing, "pic:spPr")) {
                if (auto* xfrm = spPr->FirstChildElement("a:xfrm")) {
                    run.imageRotationDeg = static_cast<float>(xfrm->IntAttribute("rot", 0)) / 60000.0f;
                }
            }
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
        ApplyAnchorPlacement(anchor, run);
        // Turned (a:xfrm rot, 60000ths of a degree): a banner on its side.
        if (auto* spPr = FindDescendant(drawing, "pic:spPr")) {
            if (auto* xfrm = spPr->FirstChildElement("a:xfrm")) {
                run.imageRotationDeg = static_cast<float>(xfrm->IntAttribute("rot", 0)) / 60000.0f;
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
        // In capitals: by the run itself, else by its paragraph's style.
        auto* ownProps = runElem->FirstChildElement("w:rPr");
        const bool capitals = ownProps && ownProps->FirstChildElement("w:caps") ? ToggleOn(ownProps, "w:caps")
                                                                                : ctx.paragraphCaps;

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
            } else if (tag == "w:t" || tag == "w:delText") {
                RichTextRun run = props;
                if (ctx.inFieldResult || ctx.field != RichTextRun::Field::Plain) {
                    run.field = ctx.field;
                    if (run.field != RichTextRun::Field::Plain) run.fieldArgument = ctx.fieldArgument;
                }
                run.text = child->GetText() ? child->GetText() : "";
                for (size_t at; (at = run.text.find(kKeptSpaceMarker)) != std::string::npos;) {
                    run.text.replace(at, 3, " ");
                }
                if (capitals) run.text = ToCapitals(run.text);
                run.lineBreakBefore = ctx.pendingLineBreak;
                ctx.pendingLineBreak = false;
                if (!run.text.empty()) ctx.runs.push_back(std::move(run));
            } else if (tag == "w:br") {
                if (std::string(Attr(child, "w:type")) == "page") {
                    pendingPageBreak_ = true;
                } else if (std::string(Attr(child, "w:type")) == "column") {
                    ctx.columnBreak = true;        // its paragraph starts the next column
                } else {
                    // A second break in a row is an empty line - in the font
                    // of the break that began it, not of the one that ends
                    // it (a small line before a large name stays small).
                    if (ctx.pendingLineBreak) {
                        RichTextRun empty = ctx.pendingBreakFormat;
                        empty.text.clear();
                        empty.lineBreakBefore = true;
                        ctx.runs.push_back(std::move(empty));
                    }
                    ctx.pendingLineBreak = true;
                    ctx.pendingBreakFormat = props;
                }
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
                const size_t before = ctx.runs.size();
                ParseDrawing(child, ctx);
                // A picture in the line stands in a line of its run's font.
                for (size_t r = before; r < ctx.runs.size(); ++r) {
                    if (!ctx.runs[r].IsInlineImage()) continue;
                    ctx.runs[r].fontSizePt = props.fontSizePt;
                    ctx.runs[r].fontFamily = props.fontFamily;
                }
            } else if (tag == "mc:AlternateContent") {
                // A shape in a run: Word's DrawingML (wps) when it is the
                // choice, else its VML fallback - never both.
                auto* choice = child->FirstChildElement("mc:Choice");
                auto* fallback = child->FirstChildElement("mc:Fallback");
                auto* use = choice ? choice : fallback;
                for (auto* inner = use ? use->FirstChildElement() : nullptr; inner; inner = inner->NextSiblingElement()) {
                    const std::string innerTag = inner->Name() ? inner->Name() : "";
                    if (innerTag == "w:drawing" || innerTag == "w:pict") ParseDrawing(inner, ctx);
                }
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
                for (size_t r = before; r < ctx.runs.size(); r++) {
                    ctx.runs[r].commentIds = activeComments_;
                    ctx.runs[r].change = ctx.change;
                    ctx.runs[r].revision = ctx.revision;
                }
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
            } else if (tag == "w:ins" || tag == "w:del" || tag == "w:moveTo" || tag == "w:moveFrom") {
                // Tracked changes: the text inside is marked inserted or
                // deleted (a move is a deletion here and an insertion there).
                const RichTextRun::Change savedChange = ctx.change;
                const int savedRevision = ctx.revision;
                ctx.change = (tag == "w:ins" || tag == "w:moveTo") ? RichTextRun::Change::Inserted
                                                                    : RichTextRun::Change::Deleted;
                ctx.revision = RevisionFor(Attr(child, "w:author"), Attr(child, "w:date"));
                ParseInlineContainer(child, linkTarget, ctx);
                ctx.change = savedChange;
                ctx.revision = savedRevision;
            } else if (tag == "w:smartTag" || tag == "w:sdt" || tag == "w:sdtContent") {
                // Content-control wrappers.
                ParseInlineContainer(child, linkTarget, ctx);
            }
            // Bookmark ends, proofing marks: nothing to keep.
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
                // A line that is drawn: "nil" and "none" are none (Writer
                // writes them on every paragraph).
                hasBottomBorder = ReadBorder(pBdr->FirstChildElement("w:bottom")).border.IsVisible();
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
                // A level without a number or a bullet (w:numFmt "none" and
                // an empty w:lvlText - the outline numbering Writer gives
                // every heading) makes no list item of its paragraph.
                bool silentLevel = false;
                if (numId) {
                    auto abstractOf = numIdToAbstract_.find(Attr(numId, "w:val"));
                    if (abstractOf != numIdToAbstract_.end()) {
                        auto labels = abstractLabels_.find(abstractOf->second);
                        if (labels != abstractLabels_.end()) {
                            auto label = labels->second.find(ilvl ? std::atoi(Attr(ilvl, "w:val")) : 0);
                            silentLevel = label != labels->second.end()
                                && label->second.format == RichNumberFormat::NoNumber
                                && label->second.numberTemplate.empty() && label->second.bulletText.empty();
                        }
                    }
                }
                if (numId && std::string(Attr(numId, "w:val")) != "0" && !silentLevel) {
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
            ctx.paragraphCaps = ParagraphStyleCaps(p);
        }
        block.styleId = ParagraphStyleId(p);
        if (auto* pPr = p->FirstChildElement("w:pPr"); pPr && pPr->FirstChildElement("w:bidi")) {
            block.rightToLeft = ToggleOn(pPr, "w:bidi");
        }
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
        // A section ends with this paragraph.
        tinyxml2::XMLElement* endsSection = nullptr;
        if (auto* pPr = p->FirstChildElement("w:pPr")) endsSection = pPr->FirstChildElement("w:sectPr");
        struct SectionEndGuard {
            DocxReader* reader;
            tinyxml2::XMLElement* sectPr;
            ~SectionEndGuard() {
                if (sectPr && reader->parsingBody_) {
                    reader->sectionEnds_.push_back({static_cast<int>(reader->doc_->blocks.size()) - 1,
                                                    ReadSectionSetup(sectPr), sectPr});
                }
            }
        } sectionEndGuard{this, endsSection};
        // Bookmarks started in the paragraph (or just before it) mark it.
        block.bookmarks = std::move(pendingBookmarks_);
        pendingBookmarks_.clear();
        for (auto* child = p->FirstChildElement("w:bookmarkStart"); child;
             child = child->NextSiblingElement("w:bookmarkStart")) {
            const std::string name = Attr(child, "w:name");
            if (!name.empty() && name != "_GoBack") block.bookmarks.push_back(name);
        }
        ParseInlineContainer(p, "", ctx);
        block.columnBreakBefore = ctx.columnBreak;
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

        // The trailer this writer puts after a table (Word wants a
        // paragraph there) and the paragraph it makes to carry a section
        // break are no paragraphs of the document: a point high, in a 1 pt
        // font. The table's room below is the trailer's space after.
        if (block.runs.empty() && ctx.trailingImages.empty() && IsTrailerParagraph(p->FirstChildElement("w:pPr"))) {
            auto* pPr = p->FirstChildElement("w:pPr");
            const bool afterTable = !doc_->blocks.empty() && doc_->blocks.back().type == RichBlockType::Table;
            if (afterTable) {
                auto* spacing = pPr->FirstChildElement("w:spacing");
                if (spacing && spacing->Attribute("w:after")) {
                    doc_->blocks.back().spaceAfterPt = static_cast<float>(spacing->IntAttribute("w:after", 0)) / 20.0f;
                }
            }
            if (afterTable || pPr->FirstChildElement("w:sectPr")) return;
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
        // Columns give way to their content unless the layout is fixed.
        block.tableAutoFit = true;
        if (auto* tblPr = tbl->FirstChildElement("w:tblPr")) {
            if (auto* tableLayout = tblPr->FirstChildElement("w:tblLayout")) {
                block.tableAutoFit = std::string(Attr(tableLayout, "w:type")) != "fixed";
            }
        }
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
                // The row's height: exactly so, or at least (the default rule).
                if (auto* height = trPr->FirstChildElement("w:trHeight")) {
                    row.heightPt = std::max(0.0f, static_cast<float>(height->IntAttribute("w:val", 0)) / 20.0f);
                    row.heightExact = row.heightPt > 0.0f && std::string(Attr(height, "w:hRule")) == "exact";
                }
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
                // Each paragraph is a line of the cell, an empty one too (a
                // letter spaces its address, date and text with them): one
                // that left no run behind becomes an empty line of its own,
                // in its paragraph's font.
                RichTextRun emptyLine;
                emptyLine.lineBreakBefore = true;
                int paragraphCount = 0;
                float spaceAboveFirst = 0.0f, spaceAfterLast = 0.0f;
                // The empty paragraph a cell must end with after a table in
                // it is no line: Word gives the cell's end mark no height there.
                bool afterNestedTable = false, emptyAfterTable = false;
                for (auto* p = tc->FirstChildElement(); p; p = p->NextSiblingElement()) {
                    const std::string childTag = p->Name() ? p->Name() : "";
                    if (childTag == "w:tbl") {
                        // A table in the cell: a frame of its own standing
                        // in the cell's line, as wide as the table says.
                        RichTextFrame nested;
                        nested.nestedTable = true;
                        const size_t start = doc_->blocks.size();
                        ParseTable(p);
                        nested.blocks.assign(std::make_move_iterator(doc_->blocks.begin() + static_cast<std::ptrdiff_t>(start)),
                                             std::make_move_iterator(doc_->blocks.end()));
                        doc_->blocks.resize(start);
                        if (nested.blocks.empty()) continue;
                        if (!firstParagraph) {
                            if (ctx.pendingLineBreak) ctx.runs.push_back(emptyLine);
                            if (spaceAfterLast > 0.5f) {
                                RichTextRun spacer;
                                spacer.lineBreakBefore = true;
                                spacer.emptyLineHeightPt = spaceAfterLast;
                                ctx.runs.push_back(std::move(spacer));
                            }
                            ctx.pendingLineBreak = true;
                        }
                        firstParagraph = false;
                        spaceAfterLast = 0.0f;
                        paragraphCount++;
                        afterNestedTable = true;
                        emptyAfterTable = false;
                        RichTextRun run;
                        run.text = RichTextRun::kObjectReplacement;
                        run.imageWrap = RichTextRun::ImageWrap::TopAndBottom;
                        run.imageWidthPt = std::max(0.0f, nested.blocks.front().tableWidthPt);
                        run.frameIndex = static_cast<int>(doc_->frames.size());
                        doc_->frames.push_back(std::move(nested));
                        run.lineBreakBefore = ctx.pendingLineBreak;
                        ctx.pendingLineBreak = false;
                        ctx.runs.push_back(std::move(run));
                        continue;
                    }
                    if (childTag != "w:p") continue;
                    // The trailer this writer puts after a table is no line.
                    if (afterNestedTable && IsTrailerParagraph(p->FirstChildElement("w:pPr"))) {
                        afterNestedTable = false;
                        continue;
                    }
                    // The room between two paragraphs - the one's space after
                    // and the other's before - is a line of that height
                    // between them; above the first and below the last it
                    // is the cell's.
                    RichDocBlock spacing;
                    ApplyGeometry(spacing, p->FirstChildElement("w:pPr"));
                    const float before = std::max(0.0f, spacing.spaceBeforePt);
                    if (!firstParagraph) {
                        if (ctx.pendingLineBreak) {
                            ctx.runs.push_back(emptyLine);
                        } else if (ctx.runs.empty()) {
                            // An empty first paragraph: the cell's first
                            // line, in its mark's font (a 2 pt one is a
                            // hair of room above the text).
                            RichTextRun first = emptyLine;
                            first.lineBreakBefore = false;
                            ctx.runs.push_back(std::move(first));
                        }
                        if (spaceAfterLast + before > 0.5f) {
                            RichTextRun spacer;
                            spacer.lineBreakBefore = true;
                            spacer.emptyLineHeightPt = spaceAfterLast + before;
                            ctx.runs.push_back(std::move(spacer));
                        }
                        ctx.pendingLineBreak = true;
                    } else {
                        spaceAboveFirst = before;
                    }
                    spaceAfterLast = std::max(0.0f, spacing.spaceAfterPt);
                    paragraphCount++;
                    ctx.paragraphCharacter = ParagraphStyleCharacter(p);
            ctx.paragraphCaps = ParagraphStyleCaps(p);
                    // Alignment is a paragraph property; the cell takes its
                    // first paragraph's, and its frame where the cell has none.
                    if (firstParagraph) {
                        cell.align = ParagraphAlignment(p);
                        // Lines of a set height (not a trailer's hair of one).
                        if (spacing.lineHeightPt >= 4.0f) {
                            cell.lineHeightPt = spacing.lineHeightPt;
                            cell.lineHeightAtLeast = spacing.lineHeightAtLeast;
                        } else if (spacing.lineHeightPt <= 0.0f && spacing.lineSpacing > 0.0f
                                   && std::abs(spacing.lineSpacing - 1.0f) > 0.01f) {
                            cell.lineSpacing = spacing.lineSpacing;
                        }
                        RichDocBlock paragraph;
                        ApplyGeometry(paragraph, p->FirstChildElement("w:pPr"));
                        if (paragraph.HasParagraphFrame()) paragraphFrames[{rowIndex, row.cells.size()}] = paragraph;
                    }
                    firstParagraph = false;
                    const size_t runsBefore = ctx.runs.size();
                    ParseInlineContainer(p, "", ctx);
                    emptyAfterTable = afterNestedTable && ctx.runs.size() == runsBefore;
                    // A paragraph set tighter or wider than the cell's first
                    // says so on its runs.
                    if (spacing.lineHeightPt <= 0.0f) {
                        const float own = spacing.lineSpacing > 0.0f ? spacing.lineSpacing : 1.0f;
                        const float cells = cell.lineSpacing > 0.0f ? cell.lineSpacing : 1.0f;
                        // (Or not of the height in points the cell's first sets.)
                        if (std::abs(own - cells) > 0.01f || cell.lineHeightPt > 0.0f) {
                            for (size_t r = runsBefore; r < ctx.runs.size(); ++r) {
                                if (!ctx.runs[r].IsInlineImage() && !ctx.runs[r].IsTextFrame()) ctx.runs[r].lineSpacing = own;
                            }
                        }
                    }
                    afterNestedTable = false;
                    // A cell keeps runs, not paragraphs: text without a size
                    // or font of its own takes its paragraph style's here.
                    RichDocBlock styled;
                    ApplyGeometry(styled, p->FirstChildElement("w:pPr"));
                    for (size_t r = runsBefore; r < ctx.runs.size(); ++r) {
                        RichTextRun& run = ctx.runs[r];
                        if (run.fontSizePt <= 0.0f) run.fontSizePt = styled.paragraphFontSizePt;
                        if (run.fontFamily.empty()) run.fontFamily = styled.paragraphFontFamily;
                    }
                    emptyLine.fontSizePt = styled.paragraphFontSizePt;
                    // An empty paragraph that does not share the cell's line
                    // height in points is a line of its own font.
                    emptyLine.lineSpacing = (cell.lineHeightPt > 0.0f && spacing.lineHeightPt <= 0.0f)
                        ? (spacing.lineSpacing > 0.0f ? spacing.lineSpacing : 1.0f) : 0.0f;
                    emptyLine.fontFamily = styled.paragraphFontFamily;
                    // An empty paragraph's line is its mark's font - and so
                    // is the empty line a break at a paragraph's end leaves.
                    if (ctx.runs.size() == runsBefore || ctx.pendingLineBreak) {
                        auto* pPr = p->FirstChildElement("w:pPr");
                        Geometry mark;
                        ReadStyleFont(pPr ? pPr->FirstChildElement("w:rPr") : nullptr, mark);
                        if (mark.fontSize != kUnset && mark.fontSize > 0.0f) emptyLine.fontSizePt = mark.fontSize;
                        if (!mark.fontFamily.empty()) emptyLine.fontFamily = mark.fontFamily;
                    }
                }
                if (ctx.pendingLineBreak) {
                    if (!emptyAfterTable) ctx.runs.push_back(emptyLine);
                    ctx.pendingLineBreak = false;
                }
                // An empty cell is as tall as a line of its paragraph's
                // font: an empty run says which.
                if (ctx.runs.empty() && paragraphCount > 0 && (emptyLine.fontSizePt > 0.0f || !emptyLine.fontFamily.empty())) {
                    RichTextRun font = emptyLine;
                    font.lineBreakBefore = false;
                    ctx.runs.push_back(std::move(font));
                }
                cell.runs = std::move(ctx.runs);
                if (spaceAboveFirst > 0.0f) cell.paddingTopPt = std::max(0.0f, cell.paddingTopPt) + spaceAboveFirst;
                if (spaceAfterLast > 0.0f) cell.paddingBottomPt = std::max(0.0f, cell.paddingBottomPt) + spaceAfterLast;

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
    // A sectPr's header and footer references, read into the default pages'
    // and the first page's furniture. True when it names any.
    // `emptyHeader` / `emptyFooter`: the height of a default header or footer
    // that is only empty paragraphs (kept as no header at all), which Word
    // still makes room for.
    float emptyFurnitureHeightPt_ = 0.0f;         // set by LoadHeaderFooterPart
    bool ReadFurnitureReferences(tinyxml2::XMLElement* sectPr, RichPageFurniture& pagesOut, RichPageFurniture& firstOut,
                                 float* emptyHeader = nullptr, float* emptyFooter = nullptr) {
        bool any = false;
        for (const char* kind : {"w:headerReference", "w:footerReference"}) {
            const bool header = std::string(kind) == "w:headerReference";
            for (auto* ref = sectPr->FirstChildElement(kind); ref; ref = ref->NextSiblingElement(kind)) {
                const std::string type = Attr(ref, "w:type");
                RichPageFurniture* target = type == "first" ? &firstOut
                                          : (type == "default" || type.empty()) ? &pagesOut : nullptr;
                if (!target) continue;   // even pages: not modelled
                auto rel = relationships_.find(Attr(ref, "r:id"));
                if (rel == relationships_.end()) continue;
                LoadHeaderFooterPart(ResolvePartPath(rel->second.target), header ? target->header : target->footer);
                if (target == &pagesOut) {
                    if (header && emptyHeader) *emptyHeader = emptyFurnitureHeightPt_;
                    if (!header && emptyFooter) *emptyFooter = emptyFurnitureHeightPt_;
                }
                any = true;
            }
        }
        return any;
    }

    // The document's page and its header and footer, from its first section.
    // `pagesOut` / `firstOut` get what that section's references name, for
    // the sections after it to inherit.
    void LoadSection(tinyxml2::XMLElement* sectPr, RichPageFurniture* pagesOut = nullptr,
                     RichPageFurniture* firstOut = nullptr) {
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
        ReadFurnitureReferences(sectPr, doc_->pageFurniture, doc_->firstPageFurniture,
                                &emptyHeaderNow_, &emptyFooterNow_);
        // The text starts below a header of empty lines, and ends above such a footer.
        if (emptyHeaderNow_ > 0.0f) page.marginTopPt = std::max(page.marginTopPt, page.headerTopPt + emptyHeaderNow_);
        if (emptyFooterNow_ > 0.0f) page.marginBottomPt = std::max(page.marginBottomPt, page.footerBottomPt + emptyFooterNow_);
        if (pagesOut) *pagesOut = doc_->pageFurniture;
        if (firstOut) *firstOut = doc_->firstPageFurniture;
        if (!doc_->firstPageDiffers) doc_->firstPageFurniture = RichPageFurniture{};
    }

    // A header or footer part: its paragraphs and tables, read like the body.
    void LoadHeaderFooterPart(const std::string& partName, std::vector<RichDocBlock>& out) {
        std::string xml;
        if (!zip_.ReadEntry(partName, xml)) return;
        ProtectWhitespaceRuns(xml);
        tinyxml2::XMLDocument part;
        if (part.Parse(xml.c_str()) != tinyxml2::XML_SUCCESS) return;
        NormalizeOoxmlPrefixes(part);
        auto* root = part.RootElement();
        if (!root) return;
        // Its pictures are named by its own relationships (over the
        // document's, for a package that leaves them out).
        const size_t slash = partName.find_last_of('/');
        std::map<std::string, DocxRelationship> partRels = relationships_;
        for (auto& [id, rel] : ReadRelationships(partName.substr(0, slash + 1) + "_rels/"
                                                 + partName.substr(slash + 1) + ".rels")) {
            partRels[id] = rel;
        }
        std::swap(relationships_, partRels);
        const size_t start = doc_->blocks.size();
        for (auto* elem = root->FirstChildElement(); elem; elem = elem->NextSiblingElement()) {
            std::string tag = elem->Name() ? elem->Name() : "";
            if (tag == "w:p") ParseParagraph(elem);
            else if (tag == "w:tbl") ParseTable(elem);
        }
        std::swap(relationships_, partRels);
        out.assign(std::make_move_iterator(doc_->blocks.begin() + static_cast<std::ptrdiff_t>(start)),
                   std::make_move_iterator(doc_->blocks.end()));
        doc_->blocks.resize(start);
        // An empty paragraph Word keeps in every header is not content - but
        // a header of nothing else still takes its lines' room on the page.
        float emptyLines = 0.0f;
        while (!out.empty() && out.back().type == RichBlockType::Paragraph && out.back().runs.empty()) {
            emptyLines += (out.back().paragraphFontSizePt > 0.0f ? out.back().paragraphFontSizePt : 11.0f) * 1.3f;
            out.pop_back();
        }
        emptyFurnitureHeightPt_ = out.empty() ? emptyLines : 0.0f;
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
        std::string partName = partFolder_ + (endnote ? "endnotes.xml" : "footnotes.xml");
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
        NormalizeOoxmlPrefixes(part);
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
    bool Save(const std::string& filePath, const UCRichDocument& doc, const std::string& password,
              std::string& error) {
        doc_ = &doc;
        // With a password the package is built in memory and encrypted into
        // the file (UltraCanvasOfficeEncryption.h); nothing unencrypted is
        // written to disk.
        const bool encrypt = !password.empty();
        if (encrypt ? !zip_.OpenInMemory() : !zip_.Open(filePath)) {
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
        for (size_t i = 0; i < chartParts_.size(); ++i) {
            if (!zip_.AddEntry("word/charts/chart" + std::to_string(i + 1) + ".xml", chartParts_[i])) {
                error = "Failed to write a chart: " + zip_.GetLastError();
                return false;
            }
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
        return !encrypt || EncryptOfficePackageToFile(zip_.TakeBytes(), password, filePath, error);
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
                        || run.letterSpacingPt.has_value()
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
        // In the order CT_RPr requires: b, i, strike, color, spacing, sz, u,
        // shd, vertAlign (Word rejects a run whose properties are out of order).
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
        if (run.letterSpacingPt) xml << CharacterSpacingXml(*run.letterSpacingPt);
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
    // `deleted`: the text of a tracked deletion, which Word keeps in w:delText.
    static void WriteRunText(std::ostringstream& xml, const std::string& text, bool deleted = false) {
        std::string pending;
        auto flush = [&]() {
            if (pending.empty()) return;
            xml << (deleted ? "<w:delText xml:space=\"preserve\">" : "<w:t xml:space=\"preserve\">")
                << EscapeXml(pending) << (deleted ? "</w:delText>" : "</w:t>");
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
            // A tracked change wraps the run: w:ins, or w:del with its text in
            // w:delText.
            std::string closeChange;
            if (run.change != RichTextRun::Change::Unchanged) {
                const bool inserted = run.change == RichTextRun::Change::Inserted;
                const RichRevision* revision = run.revision >= 0 && run.revision < static_cast<int>(doc_->revisions.size())
                                             ? &doc_->revisions[static_cast<size_t>(run.revision)] : nullptr;
                xml << (inserted ? "<w:ins" : "<w:del") << " w:id=\"" << ++revisionId_ << "\" w:author=\""
                    << EscapeXml(revision ? revision->author : "") << "\"";
                if (revision && !revision->date.empty()) xml << " w:date=\"" << EscapeXml(revision->date) << "\"";
                xml << ">";
                closeChange = inserted ? "</w:ins>" : "</w:del>";
            }
            if (run.field != RichTextRun::Field::Plain && closeChange.empty()) {
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
                xml << closeChange;
                if (isLink) xml << "</w:hyperlink>";
                continue;
            }
            xml << "<w:r>";
            if (run.IsTextFrame()) {
                if (run.lineBreakBefore) xml << "<w:br/>";
                WriteTextBox(xml, run);
            } else if (run.IsInlineImage()) {
                // The font its line is measured with (a small icon in 4 pt
                // text keeps a low line).
                if (run.fontSizePt > 0.0f) {
                    xml << "<w:rPr><w:sz w:val=\"" << std::lround(run.fontSizePt * 2.0f) << "\"/></w:rPr>";
                }
                if (run.lineBreakBefore) xml << "<w:br/>";
                pictureRotationDeg_ = run.imageRotationDeg;
                WriteDrawing(xml, run.mediaIndex, run.imageWidthPt, run.imageHeightPt,
                             run.imageAltText, ++inlineDrawingId_,
                             run.IsFloatingImage() ? &run : nullptr);
            } else {
                WriteRunProperties(xml, run, isLink);
                if (run.lineBreakBefore) xml << "<w:br/>";
                WriteRunText(xml, run.text, run.IsDeleted());
            }
            xml << "</w:r>" << closeChange;
            if (isLink) xml << "</w:hyperlink>";
        }
    }
    int revisionId_ = 1000;

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
        if (block.rightToLeft) pPr << "<w:bidi/>";
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
        pPr << pendingSectPr_;                // the section this paragraph ends
        pendingSectPr_.clear();
        std::string pPrStr = pPr.str();
        if (!pPrStr.empty()) xml << "<w:pPr>" << pPrStr << "</w:pPr>";
        if (block.columnBreakBefore) xml << "<w:r><w:br w:type=\"column\"/></w:r>";
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

    // A wp:anchor's opening: where a floating picture or text box sits, what
    // that counts from (page, margin, column / paragraph), how text wraps
    // round it - in the element order CT_Anchor requires, up to wp:docPr.
    static void WriteAnchorPlacement(std::ostringstream& xml, const RichTextRun& floating, int drawingId,
                                     long long cx, long long cy) {
        using Wrap = RichTextRun::ImageWrap;
        using Origin = RichTextRun::FloatOrigin;
        using VAlign = RichTextRun::FloatVerticalAlign;
        const Wrap wrap = floating.imageWrap;
        if (wrap == Wrap::Inline) {
            // Standing in the line: no place of its own.
            xml << "<wp:inline distT=\"0\" distB=\"0\" distL=\"0\" distR=\"0\"><wp:extent cx=\"" << cx
                << "\" cy=\"" << cy << "\"/><wp:effectExtent l=\"0\" t=\"0\" r=\"0\" b=\"0\"/>";
            return;
        }
        const long long gap = static_cast<long long>(9.0f * kEmuPerPoint);      // Word's 0.32 cm
        const char* across = floating.floatHorizontalFrom == Origin::Page ? "page"
                           : floating.floatVerticalFrom == Origin::Paragraph ? "column" : "margin";
        const char* down = floating.floatVerticalFrom == Origin::Page ? "page"
                         : floating.floatVerticalFrom == Origin::Margin ? "margin" : "paragraph";
        xml << "<wp:anchor distT=\"0\" distB=\"0\" distL=\"" << gap << "\" distR=\"" << gap
            << "\" simplePos=\"0\" relativeHeight=\"" << (251658240 + drawingId) << "\" behindDoc=\""
            << (wrap == Wrap::BehindText ? 1 : 0)
            << "\" locked=\"0\" layoutInCell=\"1\" allowOverlap=\"1\">"
            << "<wp:simplePos x=\"0\" y=\"0\"/><wp:positionH relativeFrom=\"" << across << "\">";
        switch (floating.imageFloatAlign) {
            case RichTextAlign::Right:  xml << "<wp:align>right</wp:align>"; break;
            case RichTextAlign::Center: xml << "<wp:align>center</wp:align>"; break;
            case RichTextAlign::Default:
                xml << "<wp:posOffset>" << static_cast<long long>(floating.imageOffsetXPt * kEmuPerPoint)
                    << "</wp:posOffset>";
                break;
            default: xml << "<wp:align>left</wp:align>"; break;
        }
        xml << "</wp:positionH><wp:positionV relativeFrom=\"" << down << "\">";
        switch (floating.floatVerticalAlign) {
            case VAlign::Middle: xml << "<wp:align>center</wp:align>"; break;
            case VAlign::Bottom: xml << "<wp:align>bottom</wp:align>"; break;
            default:
                xml << "<wp:posOffset>" << static_cast<long long>(floating.imageOffsetYPt * kEmuPerPoint)
                    << "</wp:posOffset>";
                break;
        }
        xml << "</wp:positionV>"
            << "<wp:extent cx=\"" << cx << "\" cy=\"" << cy << "\"/>"
            << "<wp:effectExtent l=\"0\" t=\"0\" r=\"0\" b=\"0\"/>";
        switch (wrap) {
            case Wrap::TopAndBottom: xml << "<wp:wrapTopAndBottom/>"; break;
            case Wrap::BehindText:
            case Wrap::InFrontOfText: xml << "<wp:wrapNone/>"; break;
            default: xml << "<wp:wrapSquare wrapText=\"bothSides\"/>"; break;
        }
    }

    // A text frame as Word's text box: a floating wps shape whose txbxContent
    // holds the frame's paragraphs, with its fill, outline and inner margin.
    // Wrapped in mc:AlternateContent, as Word itself writes it.
    void WriteTextBox(std::ostringstream& xml, const RichTextRun& run) {
        const RichTextFrame* frame = doc_->FrameFor(run);
        if (!frame) return;
        if (frame->IsDrawing()) {
            WriteDrawingFrame(xml, run, *frame);
            return;
        }
        const int drawingId = ++inlineDrawingId_;
        const long long cx = static_cast<long long>((run.imageWidthPt > 0.0f ? run.imageWidthPt : 144.0f) * kEmuPerPoint);
        const long long cy = static_cast<long long>(std::max(14.0f, run.imageHeightPt) * kEmuPerPoint);
        const std::string name = frame->name.empty() ? "Text Box " + std::to_string(drawingId) : frame->name;
        auto rgb = [](const std::string& hex) { return hex.size() == 7 ? hex.substr(1) : std::string("000000"); };
        const long long inset = static_cast<long long>(std::max(0.0f, frame->paddingPt) * kEmuPerPoint);
        std::vector<RichDocBlock> blocks = frame->blocks;
        if (blocks.empty()) blocks.emplace_back();
        xml << "<mc:AlternateContent xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\""
               " xmlns:wps=\"http://schemas.microsoft.com/office/word/2010/wordprocessingShape\">"
               "<mc:Choice Requires=\"wps\"><w:drawing>";
        WriteAnchorPlacement(xml, run, drawingId, cx, cy);
        xml << "<wp:docPr id=\"" << drawingId << "\" name=\"" << EscapeXml(name) << "\"/>"
            << "<a:graphic xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">"
            << "<a:graphicData uri=\"http://schemas.microsoft.com/office/word/2010/wordprocessingShape\">"
            << "<wps:wsp><wps:cNvSpPr txBox=\"1\"/><wps:spPr><a:xfrm><a:off x=\"0\" y=\"0\"/><a:ext cx=\""
            << cx << "\" cy=\"" << cy << "\"/></a:xfrm><a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>";
        if (frame->backgroundColor.empty()) xml << "<a:noFill/>";
        else xml << "<a:solidFill><a:srgbClr val=\"" << EscapeXml(rgb(frame->backgroundColor)) << "\"/></a:solidFill>";
        if (frame->border.IsVisible()) {
            xml << "<a:ln w=\"" << static_cast<long long>(frame->border.widthPt * kEmuPerPoint)
                << "\"><a:solidFill><a:srgbClr val=\"" << EscapeXml(rgb(frame->border.color))
                << "\"/></a:solidFill></a:ln>";
        } else {
            xml << "<a:ln><a:noFill/></a:ln>";
        }
        xml << "</wps:spPr><wps:txbx><w:txbxContent>" << WriteBlocks(blocks) << "</w:txbxContent></wps:txbx>"
            << "<wps:bodyPr wrap=\"square\" lIns=\"" << inset << "\" tIns=\"" << inset << "\" rIns=\"" << inset
            << "\" bIns=\"" << inset << "\" anchor=\"" << BodyAnchor(frame->textAnchor) << "\">"
            // A box with a height keeps it; one without grows to its text.
            << (run.imageHeightPt > 0.0f ? "<a:noAutofit/>" : "<a:spAutoFit/>") << "</wps:bodyPr></wps:wsp>"
            << "</a:graphicData></a:graphic>" << (run.imageWrap == RichTextRun::ImageWrap::Inline ? "</wp:inline>" : "</wp:anchor>")
            << "</w:drawing></mc:Choice></mc:AlternateContent>";
    }

    // ===== DRAWN SHAPES =====
    static long long Emu(float points) { return static_cast<long long>(std::llround(points * kEmuPerPoint)); }

    // a:xfrm: a shape's box in EMU from the drawing's top-left, turned and
    // mirrored; a group's also maps its members' space onto itself.
    static void WriteShapeTransform(std::ostringstream& xml, const RichShape& shape, float originX, float originY,
                                    bool group) {
        const long long x = Emu(originX + shape.xPt), y = Emu(originY + shape.yPt);
        const long long cx = std::max(0LL, Emu(shape.widthPt)), cy = std::max(0LL, Emu(shape.heightPt));
        xml << "<a:xfrm";
        double turn = std::fmod(static_cast<double>(shape.rotationDeg), 360.0);
        if (turn < 0.0) turn += 360.0;
        if (turn != 0.0) xml << " rot=\"" << std::llround(turn * 60000.0) << "\"";
        if (shape.flipH) xml << " flipH=\"1\"";
        if (shape.flipV) xml << " flipV=\"1\"";
        xml << "><a:off x=\"" << x << "\" y=\"" << y << "\"/><a:ext cx=\"" << cx << "\" cy=\"" << cy << "\"/>";
        if (group) xml << "<a:chOff x=\"" << x << "\" y=\"" << y << "\"/><a:chExt cx=\"" << cx << "\" cy=\"" << cy << "\"/>";
        xml << "</a:xfrm>";
    }

    // A shape's outline (a:prstGeom or a:custGeom), fill and line.
    static void WriteShapeLook(std::ostringstream& xml, const RichShape& shape) {
        using Kind = RichShape::Kind;
        using Op = RichShape::PathPoint::Op;
        auto rgb = [](const std::string& hex) { return hex.size() == 7 ? hex.substr(1) : std::string("000000"); };
        switch (shape.kind) {
            case Kind::RoundRectangle:
                xml << "<a:prstGeom prst=\"roundRect\"><a:avLst><a:gd name=\"adj\" fmla=\"val "
                    << std::lround(std::clamp(shape.cornerRadius, 0.0f, 0.5f) * 100000.0f)
                    << "\"/></a:avLst></a:prstGeom>";
                break;
            case Kind::Ellipse: xml << "<a:prstGeom prst=\"ellipse\"><a:avLst/></a:prstGeom>"; break;
            case Kind::Line: xml << "<a:prstGeom prst=\"line\"><a:avLst/></a:prstGeom>"; break;
            case Kind::Path: {
                constexpr long long kSpan = 100000;
                xml << "<a:custGeom><a:avLst/><a:gdLst/><a:ahLst/><a:cxnLst/><a:rect l=\"0\" t=\"0\" r=\"r\" b=\"b\"/>"
                    << "<a:pathLst><a:path w=\"" << kSpan << "\" h=\"" << kSpan << "\""
                    << (shape.fillColor.empty() ? " fill=\"none\"" : "") << ">";
                auto pt = [&](float x, float y) {
                    xml << "<a:pt x=\"" << std::llround(x * kSpan) << "\" y=\"" << std::llround(y * kSpan) << "\"/>";
                };
                for (const RichShape::PathPoint& point : shape.path) {
                    switch (point.op) {
                        case Op::Move: xml << "<a:moveTo>"; pt(point.x, point.y); xml << "</a:moveTo>"; break;
                        case Op::Line: xml << "<a:lnTo>"; pt(point.x, point.y); xml << "</a:lnTo>"; break;
                        case Op::Curve:
                            xml << "<a:cubicBezTo>";
                            pt(point.c1x, point.c1y);
                            pt(point.c2x, point.c2y);
                            pt(point.x, point.y);
                            xml << "</a:cubicBezTo>";
                            break;
                        case Op::Close: xml << "<a:close/>"; break;
                    }
                }
                xml << "</a:path></a:pathLst></a:custGeom>";
                break;
            }
            default: xml << "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>"; break;
        }
        if (shape.HasPictureFill() && shape.IsClosed()) {
            xml << "<a:blipFill rotWithShape=\"1\"><a:blip r:embed=\"rIdImg" << (shape.fillMediaIndex + 1) << "\"/>"
                << (shape.fillPictureTiled
                        ? "<a:tile tx=\"0\" ty=\"0\" sx=\"100000\" sy=\"100000\" flip=\"none\" algn=\"tl\"/>"
                        : "<a:stretch><a:fillRect/></a:stretch>")
                << "</a:blipFill>";
        } else if (shape.fillColor.empty() || !shape.IsClosed()) {
            xml << "<a:noFill/>";
        } else if (shape.HasPattern()) {
            // No background: white, made fully transparent (as Writer saves it).
            xml << "<a:pattFill prst=\"" << EscapeXml(RichShape::NearestPresetPattern(shape.fillPattern))
                << "\"><a:fgClr><a:srgbClr val=\"" << EscapeXml(rgb(shape.fillColor)) << "\"/></a:fgClr><a:bgClr>"
                << (shape.patternBackColor.empty() ? std::string("<a:srgbClr val=\"FFFFFF\"><a:alpha val=\"0\"/></a:srgbClr>")
                                                   : "<a:srgbClr val=\"" + EscapeXml(rgb(shape.patternBackColor)) + "\"/>")
                << "</a:bgClr></a:pattFill>";
        } else if (shape.HasGradient()) {
            xml << "<a:gradFill rotWithShape=\"1\"><a:gsLst>";
            for (const RichShape::GradientStop& stop : shape.gradientStops) {
                xml << "<a:gs pos=\"" << std::lround(std::clamp(stop.position, 0.0f, 1.0f) * 100000.0f)
                    << "\"><a:srgbClr val=\"" << EscapeXml(rgb(stop.color)) << "\"/></a:gs>";
            }
            xml << "</a:gsLst>";
            if (shape.gradientRadial) {
                const long l = std::lround(std::clamp(shape.gradientCenterX, 0.0f, 1.0f) * 100000.0f);
                const long t = std::lround(std::clamp(shape.gradientCenterY, 0.0f, 1.0f) * 100000.0f);
                xml << "<a:path path=\"circle\"><a:fillToRect l=\"" << l << "\" t=\"" << t << "\" r=\"" << (100000 - l)
                    << "\" b=\"" << (100000 - t) << "\"/></a:path>";
            } else {
                double angle = std::fmod(static_cast<double>(shape.gradientAngleDeg), 360.0);
                if (angle < 0.0) angle += 360.0;
                xml << "<a:lin ang=\"" << std::llround(angle * 60000.0) << "\" scaled=\"0\"/>";
            }
            xml << "</a:gradFill>";
        } else {
            xml << "<a:solidFill><a:srgbClr val=\"" << EscapeXml(rgb(shape.fillColor)) << "\"/></a:solidFill>";
        }
        // The shadow, after the line (a:effectLst follows a:ln).
        auto shadow = [&]() {
            if (!shape.HasShadow()) return;
            const double distance = std::hypot(shape.shadowOffsetXPt, shape.shadowOffsetYPt);
            double degrees = std::atan2(shape.shadowOffsetYPt, shape.shadowOffsetXPt) * 180.0 / 3.14159265358979323846;
            if (degrees < 0.0) degrees += 360.0;
            xml << "<a:effectLst><a:outerShdw blurRad=\"" << Emu(shape.shadowBlurPt) << "\" dist=\""
                << Emu(static_cast<float>(distance)) << "\" dir=\"" << std::lround(degrees * 60000.0)
                << "\" algn=\"ctr\" rotWithShape=\"0\"><a:srgbClr val=\"" << EscapeXml(rgb(shape.shadowColor))
                << "\"><a:alpha val=\"" << std::lround(std::clamp(shape.shadowOpacity, 0.0f, 1.0f) * 100000.0f)
                << "\"/></a:srgbClr></a:outerShdw></a:effectLst>";
        };
        if (shape.lineColor.empty()) {
            xml << "<a:ln><a:noFill/></a:ln>";
            shadow();
            return;
        }
        xml << "<a:ln w=\"" << Emu(shape.lineWidthPt) << "\"><a:solidFill><a:srgbClr val=\""
            << EscapeXml(rgb(shape.lineColor)) << "\"/></a:solidFill>";
        if (shape.dash == RichShape::Dash::Dashed) xml << "<a:prstDash val=\"dash\"/>";
        else if (shape.dash == RichShape::Dash::Dotted) xml << "<a:prstDash val=\"sysDot\"/>";
        if (shape.arrowStart) xml << "<a:headEnd type=\"triangle\"/>";
        if (shape.arrowEnd) xml << "<a:tailEnd type=\"triangle\"/>";
        xml << "</a:ln>";
        shadow();
    }

    static const char* BodyAnchor(RichShape::TextAnchor anchor) {
        return anchor == RichShape::TextAnchor::Middle ? "ctr" : anchor == RichShape::TextAnchor::Bottom ? "b" : "t";
    }

    // A wps:wsp: its outline and look, then its words (a text box) if any.
    void WriteShapeElement(std::ostringstream& xml, const RichShape& shape, float originX, float originY,
                           bool member, const std::vector<RichDocBlock>* words, float padding,
                           RichShape::TextAnchor anchor) {
        xml << "<wps:wsp>";
        if (member) {
            const int id = ++inlineDrawingId_;
            xml << "<wps:cNvPr id=\"" << id << "\" name=\""
                << EscapeXml(shape.name.empty() ? "Shape " + std::to_string(id) : shape.name) << "\"/>";
        }
        xml << (shape.kind == RichShape::Kind::Line ? "<wps:cNvCnPr/>" : "<wps:cNvSpPr/>") << "<wps:spPr>";
        WriteShapeTransform(xml, shape, originX, originY, false);
        WriteShapeLook(xml, shape);
        xml << "</wps:spPr>";
        const long long inset = Emu(std::max(0.0f, padding));
        if (words && !words->empty() && shape.kind != RichShape::Kind::Line) {
            xml << "<wps:txbx><w:txbxContent>" << WriteBlocks(*words) << "</w:txbxContent></wps:txbx>";
        }
        xml << "<wps:bodyPr wrap=\"square\" lIns=\"" << inset << "\" tIns=\"" << inset << "\" rIns=\"" << inset
            << "\" bIns=\"" << inset << "\" anchor=\"" << BodyAnchor(anchor) << "\"><a:noAutofit/></wps:bodyPr></wps:wsp>";
    }

    // A group member: a shape, a picture or a group of its own.
    void WriteGroupMember(std::ostringstream& xml, const RichShape& shape, float originX, float originY, int depth) {
        if (depth > 16) return;
        if (shape.kind == RichShape::Kind::Group) {
            xml << "<wpg:grpSp><wpg:cNvGrpSpPr/><wpg:grpSpPr>";
            WriteShapeTransform(xml, shape, originX, originY, true);
            xml << "</wpg:grpSpPr>";
            for (const RichShape& member : shape.children) {
                WriteGroupMember(xml, member, originX + shape.xPt, originY + shape.yPt, depth + 1);
            }
            xml << "</wpg:grpSp>";
            return;
        }
        if (shape.mediaIndex >= 0 && shape.mediaIndex < static_cast<int>(doc_->media.size())) {
            const int id = ++inlineDrawingId_;
            xml << "<pic:pic><pic:nvPicPr><pic:cNvPr id=\"" << id << "\" name=\""
                << EscapeXml(shape.name.empty() ? "Picture " + std::to_string(id) : shape.name)
                << "\"/><pic:cNvPicPr/></pic:nvPicPr><pic:blipFill><a:blip r:embed=\"rIdImg" << (shape.mediaIndex + 1)
                << "\"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill><pic:spPr>";
            WriteShapeTransform(xml, shape, originX, originY, false);
            xml << "<a:prstGeom prst=\"rect\"><a:avLst/></a:prstGeom>";
            if (!shape.lineColor.empty()) {
                RichShape outline = shape;
                outline.fillColor.clear();
                std::ostringstream look;
                WriteShapeLook(look, outline);
                const std::string text = look.str();
                const size_t line = text.find("<a:ln");
                if (line != std::string::npos) xml << text.substr(line);
            }
            xml << "</pic:spPr></pic:pic>";
            return;
        }
        const RichTextFrame* words = shape.textFrame >= 0 && shape.textFrame < static_cast<int>(doc_->frames.size())
            ? &doc_->frames[static_cast<size_t>(shape.textFrame)] : nullptr;
        WriteShapeElement(xml, shape, originX, originY, true, words ? &words->blocks : nullptr,
                          shape.textPaddingPt, shape.textAnchor);
    }

    // A drawing as Word draws one: a lone shape as a wps shape (its frame's
    // words in its text box); several shapes, or a group, as a wpg group.
    // A chart's part (word/charts/chartN.xml): its kind, categories and
    // series, the values as literals (there is no workbook behind them).
    static std::string BuildChartPartXml(const RichChart& chart) {
        using Kind = RichChart::Kind;
        std::ostringstream xml;
        xml.imbue(std::locale::classic());
        xml.precision(15);
        auto color = [](const std::string& hex) {        // "#RRGGBB" -> a:solidFill
            return hex.size() == 7 && hex[0] == '#'
                ? "<a:solidFill><a:srgbClr val=\"" + hex.substr(1) + "\"/></a:solidFill>" : std::string();
        };
        auto numbers = [&](const char* tag, const std::vector<double>& values) {
            xml << "<" << tag << "><c:numLit><c:formatCode>General</c:formatCode><c:ptCount val=\"" << values.size() << "\"/>";
            for (size_t i = 0; i < values.size(); i++) {
                if (std::isfinite(values[i])) xml << "<c:pt idx=\"" << i << "\"><c:v>" << values[i] << "</c:v></c:pt>";
            }
            xml << "</c:numLit></" << tag << ">";
        };
        auto names = [&]() {
            if (chart.categories.empty()) return;
            xml << "<c:cat><c:strLit><c:ptCount val=\"" << chart.categories.size() << "\"/>";
            for (size_t i = 0; i < chart.categories.size(); i++) {
                xml << "<c:pt idx=\"" << i << "\"><c:v>" << EscapeXml(chart.categories[i]) << "</c:v></c:pt>";
            }
            xml << "</c:strLit></c:cat>";
        };
        const bool pie = chart.kind == Kind::Pie || chart.kind == Kind::Doughnut;
        const bool scatter = chart.kind == Kind::Scatter;
        const bool line = chart.kind == Kind::Line;
        const bool bars = chart.kind == Kind::Column || chart.kind == Kind::Bar;
        const char* tag = pie ? (chart.kind == Kind::Doughnut ? "c:doughnutChart" : "c:pieChart")
                        : scatter ? "c:scatterChart" : line ? "c:lineChart"
                        : chart.kind == Kind::Area ? "c:areaChart" : "c:barChart";
        xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
               "<c:chartSpace xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\""
               " xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\""
               " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\">"
               "<c:roundedCorners val=\"0\"/><c:chart>";
        if (!chart.title.empty()) {
            xml << "<c:title><c:tx><c:rich><a:bodyPr/><a:p><a:r><a:t>" << EscapeXml(chart.title)
                << "</a:t></a:r></a:p></c:rich></c:tx><c:overlay val=\"0\"/></c:title>";
        }
        xml << "<c:autoTitleDeleted val=\"" << (chart.title.empty() ? 1 : 0) << "\"/><c:plotArea><c:layout/><" << tag << ">";
        if (bars) xml << "<c:barDir val=\"" << (chart.kind == Kind::Bar ? "bar" : "col") << "\"/>";
        if (scatter) xml << "<c:scatterStyle val=\"lineMarker\"/>";
        if (bars) xml << "<c:grouping val=\"" << (chart.percent ? "percentStacked" : chart.stacked ? "stacked" : "clustered") << "\"/>";
        else if (!pie && !scatter) xml << "<c:grouping val=\"" << (chart.percent ? "percentStacked" : chart.stacked ? "stacked" : "standard") << "\"/>";
        xml << "<c:varyColors val=\"" << (pie ? 1 : 0) << "\"/>";
        bool anyMarked = false;
        for (size_t s = 0; s < chart.series.size(); s++) {
            const RichChartSeries& one = chart.series[s];
            anyMarked = anyMarked || one.marked;
            xml << "<c:ser><c:idx val=\"" << s << "\"/><c:order val=\"" << s << "\"/>";
            if (!one.name.empty()) xml << "<c:tx><c:v>" << EscapeXml(one.name) << "</c:v></c:tx>";
            const std::string fill = color(one.color);
            if (line || scatter) {
                if (!one.joined) xml << "<c:spPr><a:ln><a:noFill/></a:ln></c:spPr>";
                else if (!fill.empty()) xml << "<c:spPr><a:ln w=\"28575\">" << fill << "</a:ln></c:spPr>";
                xml << "<c:marker><c:symbol val=\"" << (one.marked ? "circle" : "none") << "\"/>";
                if (one.marked && !fill.empty()) xml << "<c:spPr>" << fill << "</c:spPr>";
                xml << "</c:marker>";
            } else if (!fill.empty()) {
                xml << "<c:spPr>" << fill << "</c:spPr>";
            }
            if (pie && s == 0) {
                for (size_t i = 0; i < chart.pointColors.size(); i++) {
                    const std::string slice = color(chart.pointColors[i]);
                    if (!slice.empty()) xml << "<c:dPt><c:idx val=\"" << i << "\"/><c:bubble3D val=\"0\"/><c:spPr>" << slice << "</c:spPr></c:dPt>";
                }
            }
            if (scatter) {
                if (one.xValues.size() == one.values.size()) numbers("c:xVal", one.xValues);
                numbers("c:yVal", one.values);
            } else {
                names();
                numbers("c:val", one.values);
            }
            xml << "</c:ser>";
        }
        if (bars && chart.stacked) xml << "<c:overlap val=\"100\"/>";
        if (line) xml << "<c:marker val=\"" << (anyMarked ? 1 : 0) << "\"/>";
        if (pie) {
            xml << "<c:firstSliceAng val=\"0\"/>";
            if (chart.kind == Kind::Doughnut) xml << "<c:holeSize val=\"50\"/>";
        } else {
            xml << "<c:axId val=\"1\"/><c:axId val=\"2\"/>";
        }
        xml << "</" << tag << ">";
        if (!pie) {
            // The axis the categories (a scatter chart's x) run along, and
            // the values' - at the left and foot for bars lying down.
            const bool lying = chart.kind == Kind::Bar;
            const char* across = scatter ? "c:valAx" : "c:catAx";
            xml << "<" << across << "><c:axId val=\"1\"/><c:scaling><c:orientation val=\"minMax\"/></c:scaling>"
                   "<c:delete val=\"0\"/><c:axPos val=\"" << (lying ? "l" : "b") << "\"/>"
                << (scatter ? "<c:majorGridlines/>" : "")
                << "<c:tickLblPos val=\"nextTo\"/><c:crossAx val=\"2\"/></" << across << ">"
                   "<c:valAx><c:axId val=\"2\"/><c:scaling><c:orientation val=\"minMax\"/></c:scaling>"
                   "<c:delete val=\"0\"/><c:axPos val=\"" << (lying ? "b" : "l") << "\"/><c:majorGridlines/>"
                   "<c:tickLblPos val=\"nextTo\"/><c:crossAx val=\"1\"/></c:valAx>";
        }
        xml << "</c:plotArea>";
        if (chart.legend) xml << "<c:legend><c:legendPos val=\"r\"/><c:overlay val=\"0\"/></c:legend>";
        xml << "<c:plotVisOnly val=\"1\"/></c:chart></c:chartSpace>";
        return xml.str();
    }
    std::vector<std::string> chartParts_;         // word/charts/chart<n>.xml, n from 1 (relationship rIdChart<n>)

    void WriteDrawingFrame(std::ostringstream& xml, const RichTextRun& run, const RichTextFrame& frame) {
        const int drawingId = ++inlineDrawingId_;
        const long long cx = std::max(0LL, Emu(run.imageWidthPt));
        const long long cy = std::max(0LL, Emu(run.imageHeightPt));
        const std::string name = frame.name.empty() ? "Drawing " + std::to_string(drawingId) : frame.name;
        if (frame.chart.IsChart() && !frame.chart.series.empty()) {
            // A chart stays a chart: a part of its own, referred to from
            // the drawing (not the shapes it is also held as).
            chartParts_.push_back(BuildChartPartXml(frame.chart));
            xml << "<w:drawing>";
            WriteAnchorPlacement(xml, run, drawingId, cx, cy);
            xml << "<wp:docPr id=\"" << drawingId << "\" name=\"" << EscapeXml(name) << "\"/><wp:cNvGraphicFramePr/>"
                << "<a:graphic xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">"
                   "<a:graphicData uri=\"http://schemas.openxmlformats.org/drawingml/2006/chart\">"
                   "<c:chart xmlns:c=\"http://schemas.openxmlformats.org/drawingml/2006/chart\""
                   " xmlns:r=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships\" r:id=\"rIdChart"
                << chartParts_.size() << "\"/></a:graphicData></a:graphic>"
                << (run.imageWrap == RichTextRun::ImageWrap::Inline ? "</wp:inline>" : "</wp:anchor>") << "</w:drawing>";
            return;
        }
        const bool lone = frame.shapes.size() == 1 && frame.shapes.front().kind != RichShape::Kind::Group
                       && frame.shapes.front().mediaIndex < 0;
        xml << "<mc:AlternateContent xmlns:mc=\"http://schemas.openxmlformats.org/markup-compatibility/2006\""
               " xmlns:wps=\"http://schemas.microsoft.com/office/word/2010/wordprocessingShape\""
               " xmlns:wpg=\"http://schemas.microsoft.com/office/word/2010/wordprocessingGroup\">"
               "<mc:Choice Requires=\"" << (lone ? "wps" : "wpg") << "\"><w:drawing>";
        WriteAnchorPlacement(xml, run, drawingId, cx, cy);
        xml << "<wp:docPr id=\"" << drawingId << "\" name=\"" << EscapeXml(name) << "\"/>"
            << "<a:graphic xmlns:a=\"http://schemas.openxmlformats.org/drawingml/2006/main\">";
        if (lone) {
            RichShape shape = frame.shapes.front();
            shape.xPt = shape.yPt = 0.0f;
            shape.widthPt = run.imageWidthPt;
            shape.heightPt = run.imageHeightPt;
            xml << "<a:graphicData uri=\"http://schemas.microsoft.com/office/word/2010/wordprocessingShape\">";
            WriteShapeElement(xml, shape, 0.0f, 0.0f, false, &frame.blocks, frame.paddingPt, frame.textAnchor);
        } else {
            // One group is the drawing's own (its members in its space);
            // several shapes are grouped in the frame's.
            const RichShape* only = frame.shapes.size() == 1 && frame.shapes.front().kind == RichShape::Kind::Group
                ? &frame.shapes.front() : nullptr;
            xml << "<a:graphicData uri=\"http://schemas.microsoft.com/office/word/2010/wordprocessingGroup\">"
                << "<wpg:wgp xmlns:pic=\"http://schemas.openxmlformats.org/drawingml/2006/picture\"><wpg:cNvGrpSpPr/>"
                << "<wpg:grpSpPr><a:xfrm";
            if (only) {
                double turn = std::fmod(static_cast<double>(only->rotationDeg), 360.0);
                if (turn < 0.0) turn += 360.0;
                if (turn != 0.0) xml << " rot=\"" << std::llround(turn * 60000.0) << "\"";
                if (only->flipH) xml << " flipH=\"1\"";
                if (only->flipV) xml << " flipV=\"1\"";
            }
            // The members' space: the group's own, else the size the shapes
            // were drawn for (a resized drawing), mapped onto the frame.
            const long long chX = only ? std::max(1LL, Emu(only->widthPt))
                                : frame.drawingWidthPt > 0.0f ? std::max(1LL, Emu(frame.drawingWidthPt)) : cx;
            const long long chY = only ? std::max(1LL, Emu(only->heightPt))
                                : frame.drawingHeightPt > 0.0f ? std::max(1LL, Emu(frame.drawingHeightPt)) : cy;
            xml << "><a:off x=\"0\" y=\"0\"/><a:ext cx=\"" << cx << "\" cy=\"" << cy
                << "\"/><a:chOff x=\"0\" y=\"0\"/><a:chExt cx=\"" << chX << "\" cy=\"" << chY << "\"/></a:xfrm></wpg:grpSpPr>";
            for (const RichShape& shape : only ? only->children : frame.shapes) WriteGroupMember(xml, shape, 0.0f, 0.0f, 0);
            xml << "</wpg:wgp>";
        }
        xml << "</a:graphicData></a:graphic>" << (run.imageWrap == RichTextRun::ImageWrap::Inline ? "</wp:inline>" : "</wp:anchor>")
            << "</w:drawing></mc:Choice></mc:AlternateContent>";
    }

    float pictureRotationDeg_ = 0.0f;             // of the in-line picture about to be written
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
            WriteAnchorPlacement(xml, *floating, drawingId, cx, cy);
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
            << "<pic:spPr><a:xfrm";
        const float turned = floating ? floating->imageRotationDeg : pictureRotationDeg_;
        pictureRotationDeg_ = 0.0f;
        if (turned != 0.0f) xml << " rot=\"" << std::lround(static_cast<double>(turned) * 60000.0) << "\"";
        xml << "><a:off x=\"0\" y=\"0\"/><a:ext cx=\"" << cx
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

    void WriteTable(std::ostringstream& xml, const RichDocBlock& block, const std::string& sectPr = std::string()) {
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
        if (block.tableIndentPt != 0.0f) xml << "<w:tblInd w:w=\"" << Twips(block.tableIndentPt) << "\" w:type=\"dxa\"/>";
        xml << "<w:tblBorders>";
        for (const char* side : {"top", "left", "bottom", "right", "insideH", "insideV"}) {
            xml << "<w:" << side << " " << tableLine;
        }
        xml << "</w:tblBorders>";
        if (!block.tableAutoFit) xml << "<w:tblLayout w:type=\"fixed\"/>";
        xml << "</w:tblPr><w:tblGrid>";
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
            if (row.header || row.heightPt > 0.0f) {
                xml << "<w:trPr>";
                if (row.heightPt > 0.0f) {
                    xml << "<w:trHeight w:val=\"" << std::lround(row.heightPt * 20.0f) << "\" w:hRule=\""
                        << (row.heightExact ? "exact" : "atLeast") << "\"/>";
                }
                if (row.header) xml << "<w:tblHeader/>";
                xml << "</w:trPr>";
            }

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
                xml << "</w:tcPr>";
                std::vector<RichTextRun> runs = cell.runs;
                if (row.header) {
                    for (auto& run : runs) run.bold = true;
                }
                paragraphBase_ = ParagraphBase(RichDocBlock{});     // a cell's paragraph is Normal
                // The cell's lines as paragraphs without spacing of their
                // own (it is the cell's padding, and would be counted twice
                // when the file is read again); a line of a set height
                // (RichTextRun::emptyLineHeightPt) is the space above the
                // paragraph that follows it.
                std::vector<RichTextRun> piece;
                float above = 0.0f;
                auto flush = [&]() {
                    xml << "<w:p><w:pPr><w:spacing w:before=\"" << std::lround(above * 20.0f) << "\" w:after=\"0\"";
                    // The line's own multiple goes before the cell's height
                    // in points, and that before the cell's multiple.
                    float own = 0.0f;
                    for (const RichTextRun& run : piece) {
                        if (run.lineSpacing > 0.0f) { own = run.lineSpacing; break; }
                    }
                    if (own > 0.0f) {
                        xml << " w:line=\"" << std::lround(own * 240.0f) << "\" w:lineRule=\"auto\"";
                    } else if (cell.lineHeightPt > 0.0f) {
                        xml << " w:line=\"" << std::lround(cell.lineHeightPt * 20.0f) << "\" w:lineRule=\""
                            << (cell.lineHeightAtLeast ? "atLeast" : "exact") << "\"";
                    } else if (cell.lineSpacing > 0.0f) {
                        xml << " w:line=\"" << std::lround(cell.lineSpacing * 240.0f) << "\" w:lineRule=\"auto\"";
                    }
                    xml << "/>";
                    if (const char* jc = JustificationFor(cell.align)) xml << "<w:jc w:val=\"" << jc << "\"/>";
                    // A paragraph without words is a line of its mark's font.
                    bool words = false;
                    for (const RichTextRun& run : piece) words = words || !run.text.empty();
                    if (!words && !piece.empty() && (piece.front().fontSizePt > 0.0f || !piece.front().fontFamily.empty())) {
                        xml << "<w:rPr>";
                        if (!piece.front().fontFamily.empty()) {
                            xml << "<w:rFonts w:ascii=\"" << EscapeXml(piece.front().fontFamily) << "\" w:hAnsi=\""
                                << EscapeXml(piece.front().fontFamily) << "\"/>";
                        }
                        if (piece.front().fontSizePt > 0.0f) {
                            xml << "<w:sz w:val=\"" << std::lround(piece.front().fontSizePt * 2.0f) << "\"/>";
                        }
                        xml << "</w:rPr>";
                    }
                    xml << "</w:pPr>";
                    WriteRuns(xml, piece);
                    xml << "</w:p>";
                    piece.clear();
                    above = 0.0f;
                };
                // Every line of the cell is a paragraph (an empty one keeps
                // its font in its mark); a table nested in the cell is
                // written as that table, between them.
                bool open = true;                 // a paragraph is begun: a line break ends it
                bool written = false;
                for (const RichTextRun& run : runs) {
                    if (run.lineBreakBefore && run.text.empty() && run.emptyLineHeightPt > 0.0f) {
                        if (open) { flush(); written = true; }
                        above = run.emptyLineHeightPt;
                        open = false;             // the break after a spacer is the new paragraph's start
                        continue;
                    }
                    const RichTextFrame* nested = run.IsTextFrame() ? doc_->FrameFor(run) : nullptr;
                    if (nested && nested->nestedTable) {
                        if (open && (!piece.empty() || (run.lineBreakBefore && written))) { flush(); written = true; }
                        piece.clear();
                        for (const RichDocBlock& inner : nested->blocks) {
                            if (inner.type == RichBlockType::Table) WriteTable(xml, inner);
                            else WriteParagraph(xml, inner);
                        }
                        paragraphBase_ = ParagraphBase(RichDocBlock{});
                        written = true;
                        open = false;
                        continue;
                    }
                    if (run.lineBreakBefore && open) { flush(); written = true; }
                    piece.push_back(run);
                    piece.back().lineBreakBefore = false;
                    open = true;
                }
                if (open || !written) flush();
                xml << "</w:tc>";
            }
            xml << "</w:tr>\n";
        }
        xml << "</w:tbl>\n"
            // Word requires a paragraph between a table and the section end
            // (and between two tables): a trailer the reader takes back out,
            // which carries the table's room below and a section's end.
            << TrailerParagraph(block.spaceAfterPt, sectPr);
    }

    // A paragraph a point high, its mark in a 1 pt font: after a table, or
    // only to carry a section break. `spaceAfterPt` < 0: none stated.
    static std::string TrailerParagraph(float spaceAfterPt, const std::string& sectPr) {
        std::string xml = "<w:p><w:pPr><w:spacing w:before=\"0\"";
        if (spaceAfterPt >= 0.0f) xml += " w:after=\"" + std::to_string(std::lround(spaceAfterPt * 20.0f)) + "\"";
        xml += " w:line=\"20\" w:lineRule=\"exact\"/><w:rPr><w:sz w:val=\"2\"/><w:szCs w:val=\"2\"/></w:rPr>"
             + sectPr + "</w:pPr></w:p>\n";
        return xml;
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
            // The last block of a section carries the section's properties;
            // a table or picture cannot, so a paragraph after it does.
            const bool endsSection = &blocks == &doc_->blocks && index + 1 < blocks.size()
                                     && blocks[index + 1].sectionStart;
            const bool viaParagraph = block.type != RichBlockType::Table && block.type != RichBlockType::Image
                                      && block.type != RichBlockType::PageBreak;
            if (endsSection && viaParagraph) pendingSectPr_ = SectionBreakXml(doc_->SectionFor(static_cast<int>(index)));
            bool sectionWritten = false;
            switch (block.type) {
                case RichBlockType::Table:
                    // Its trailer carries the section's end.
                    WriteTable(body, block, endsSection ? SectionBreakXml(doc_->SectionFor(static_cast<int>(index)))
                                                        : std::string());
                    break;
                case RichBlockType::Image:
                    WriteImage(body, block, ++drawingId_);
                    break;
                case RichBlockType::PageBreak:
                    // Before a section that starts a page, the section's
                    // break is the page break: one of the two, or Word and
                    // the reader turn two pages.
                    if (endsSection && blocks[index + 1].section.newPage) {
                        body << TrailerParagraph(-1.0f, SectionBreakXml(doc_->SectionFor(static_cast<int>(index))));
                        sectionWritten = true;
                    } else {
                        body << "<w:p><w:r><w:br w:type=\"page\"/></w:r></w:p>\n";
                    }
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
            if (endsSection && !viaParagraph && block.type != RichBlockType::Table && !sectionWritten) {
                body << TrailerParagraph(-1.0f, SectionBreakXml(doc_->SectionFor(static_cast<int>(index))));
            }
            pendingSectPr_.clear();
        }
        blocks_ = savedBlocks;
        return body.str();
    }

    // Sections: a paragraph's w:sectPr ends one. Its type says how the section
    // it ends began (on a new page or continuing the page).
    std::string pendingSectPr_;               // for the paragraph being written

    static std::string SectionColumnsXml(const RichSectionSetup& section) {
        if (section.columns <= 1) return "";
        return "<w:cols w:num=\"" + std::to_string(section.columns) + "\" w:space=\""
             + std::to_string(std::lround(section.columnGapPt * 20.0f)) + "\"/>";
    }

    static std::string SectionTypeXml(const RichSectionSetup& section) {
        return std::string("<w:type w:val=\"") + (section.newPage ? "nextPage" : "continuous") + "\"/>";
    }

    std::string SectionBreakXml(const RichSectionSetup& section) {
        bool titlePage = false;
        const std::string references = FurnitureReferencesXml(section, titlePage);
        return "<w:sectPr>" + references + SectionTypeXml(section) + PageGeometryXml(&section)
             + SectionColumnsXml(section) + (titlePage ? "<w:titlePg/>" : "") + "</w:sectPr>";
    }

    // The header and footer references of a section's w:sectPr, their parts
    // written once each. A section says all of its own (Word hands a section
    // without one the header of the section before): an empty part where it
    // has none but another section does. `titlePage`: its first page differs.
    std::map<std::pair<const void*, std::string>, size_t> furniturePartOf_;   // (blocks, kind + type) -> part number
    std::string FurnitureReferencesXml(const RichSectionSetup& section, bool& titlePage) {
        const UCRichDocument& doc = *doc_;
        const bool ownSet = section.furniture >= 0 && section.furniture < static_cast<int>(doc.sectionFurniture.size());
        const RichSectionFurniture* own = ownSet ? &doc.sectionFurniture[static_cast<size_t>(section.furniture)] : nullptr;
        // The document's first-page header is its first page's alone: only
        // the first section's pages begin with it.
        const bool documentFirst = !own && doc.firstPageDiffers && &section == &doc.SectionFor(0);
        titlePage = own ? own->firstPageDiffers : documentFirst;
        const RichPageFurniture& pages = own ? own->pages : doc.pageFurniture;
        const RichPageFurniture& first = own ? own->firstPage : doc.firstPageFurniture;
        bool anyHeader = false, anyFooter = false;
        for (const RichPageFurniture* furniture : doc.AllFurniture()) {
            anyHeader = anyHeader || !furniture->header.empty();
            anyFooter = anyFooter || !furniture->footer.empty();
        }
        std::ostringstream refs;
        auto part = [&](const std::vector<RichDocBlock>& blocks, const char* kind, const char* type, bool wanted) {
            if (!wanted) return;
            const auto key = std::make_pair(static_cast<const void*>(blocks.empty() ? nullptr : &blocks),
                                            std::string(kind) + type);
            auto known = furniturePartOf_.find(key);
            if (known == furniturePartOf_.end()) {
                FurniturePart p;
                p.kind = kind;
                p.type = type;
                p.name = std::string(kind) + std::to_string(furnitureParts_.size() + 1) + ".xml";
                const char* root = std::string(kind) == "header" ? "w:hdr" : "w:ftr";
                // Written apart from the body: its lists are its own.
                const auto savedList = currentListNumId_;
                const std::string savedSectPr = pendingSectPr_;
                p.xml = std::string("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<")
                      + root + " " + kPartNamespaces + ">" + (blocks.empty() ? std::string("<w:p/>") : WriteBlocks(blocks))
                      + "</" + root + ">\n";
                currentListNumId_ = savedList;
                pendingSectPr_ = savedSectPr;
                known = furniturePartOf_.emplace(key, furnitureParts_.size() + 1).first;
                furnitureParts_.push_back(std::move(p));
            }
            refs << "<w:" << kind << "Reference w:type=\"" << type << "\" r:id=\"rIdPart" << known->second << "\"/>";
        };
        part(pages.header, "header", "default", anyHeader);
        if (titlePage) part(first.header, "header", "first", anyHeader);
        part(pages.footer, "footer", "default", anyFooter);
        if (titlePage) part(first.footer, "footer", "first", anyFooter);
        return refs.str();
    }

    // The page size and margins, Word's defaults (A4, 2 cm margins) where
    // the document states no page.
    std::string PageGeometryXml(const RichSectionSetup* section = nullptr) const {
        RichPageSetup page = doc_->page;
        if (section) {
            // A section's own room for its header and footer.
            if (section->marginTopPt >= 0.0f) page.marginTopPt = section->marginTopPt;
            if (section->marginBottomPt >= 0.0f) page.marginBottomPt = section->marginBottomPt;
            if (section->headerTopPt >= 0.0f) page.headerTopPt = section->headerTopPt;
            if (section->footerBottomPt >= 0.0f) page.footerBottomPt = section->footerBottomPt;
        }
        auto twips = [](float pt) { return std::lround(pt * 20.0f); };
        const bool hasPage = page.HasPage();
        std::ostringstream xml;
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
        return xml.str();
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
        // The last section's: the one the body's own w:sectPr ends.
        const RichSectionSetup& last = doc.SectionFor(static_cast<int>(doc.blocks.size()) - 1);
        const bool sectioned = doc.HasColumns() || std::any_of(doc.blocks.begin(), doc.blocks.end(),
                                                               [](const RichDocBlock& b) { return b.sectionStart; });
        bool titlePage = false;
        const std::string references = FurnitureReferencesXml(last, titlePage);
        std::ostringstream xml;
        xml << "<w:sectPr>" << references;
        if (sectioned) xml << SectionTypeXml(last);
        xml << PageGeometryXml(&last) << SectionColumnsXml(last);
        if (titlePage) xml << "<w:titlePg/>";
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
        for (size_t i = 0; i < chartParts_.size(); ++i) {
            xml << "<Override PartName=\"/word/charts/chart" << (i + 1) << ".xml\" "
                   "ContentType=\"application/vnd.openxmlformats-officedocument.drawingml.chart+xml\"/>\n";
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
    bool HasSettings() const { return doc_->defaultTabStopPt > 0.0f || !doc_->fontLineGap; }

    std::string BuildSettingsXml() const {
        return std::string("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
               "<w:settings xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\">")
             + (doc_->defaultTabStopPt > 0.0f ? "<w:defaultTabStop w:val=\"" + Twips(doc_->defaultTabStopPt) + "\"/>"
                                             : std::string())
             // Lines without the font's line gap: Word's "don't add leading".
             + (doc_->fontLineGap ? "" : "<w:compat><w:noLeading/></w:compat>")
             + "</w:settings>\n";
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
        for (size_t i = 0; i < chartParts_.size(); ++i) {
            xml << "<Relationship Id=\"rIdChart" << (i + 1)
                << "\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/chart\" "
                << "Target=\"charts/chart" << (i + 1) << ".xml\"/>\n";
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
            if (c.letterSpacingPt) xml << CharacterSpacingXml(*c.letterSpacingPt);
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
            << (doc_->letterKerning ? "<w:kern w:val=\"2\"/>" : "")
            << "</w:rPr></w:rPrDefault></w:docDefaults>\n";
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

std::string WordFormatInternal::FindDocxMainPart(const UCZipPackageReader& zip) {
    std::string relsXml;
    if (zip.ReadEntry("_rels/.rels", relsXml)) {
        tinyxml2::XMLDocument rels;
        if (rels.Parse(relsXml.c_str()) == tinyxml2::XML_SUCCESS && rels.RootElement()) {
            const std::string officeDocument = "/officeDocument";
            for (auto* rel = rels.RootElement()->FirstChildElement(); rel; rel = rel->NextSiblingElement()) {
                const char* type = rel->Attribute("Type");
                const char* target = rel->Attribute("Target");
                if (!type || !target) continue;
                const std::string t(type);
                if (t.size() < officeDocument.size()
                    || t.compare(t.size() - officeDocument.size(), officeDocument.size(), officeDocument) != 0)
                    continue;
                std::string part(target);
                if (!part.empty() && part[0] == '/') part.erase(0, 1);
                if (!part.empty() && zip.HasEntry(part) && IsWordMainDocumentPart(zip, part)) return part;
            }
        }
    }
    return zip.HasEntry("word/document.xml") ? "word/document.xml" : std::string();
}

bool UCWordDocumentIO::LoadDocx(const std::string& filePath, UCRichDocument& outDocument,
                                std::string& outError, const std::string& password) {
    outDocument = UCRichDocument{};
    DocxReader reader;
    if (!reader.Load(filePath, password, outDocument, outError)) return false;
    outDocument.ReadCheckboxPrefixes();
    return true;
}

bool UCWordDocumentIO::SaveDocx(const std::string& filePath, const UCRichDocument& document,
                                std::string& outError, const std::string& password) {
    DocxWriter writer;
    // The format has no check list: its items go out as a ballot box
    // opening their text, which is what reads back in.
    for (const RichDocBlock& block : document.blocks) {
        if (block.checkbox)
            return writer.Save(filePath, document.WithCheckboxesAsPrefixes(), password, outError);
    }
    return writer.Save(filePath, document, password, outError);
}

} // namespace UltraCanvas
