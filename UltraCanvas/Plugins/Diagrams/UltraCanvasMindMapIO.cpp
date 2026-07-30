// Plugins/Diagrams/UltraCanvasMindMapIO.cpp
// Mind map interchange implementation
// Version: 1.0.0
// Last Modified: 2026-07-30
// Author: UltraCanvas Framework

#include "Plugins/Diagrams/UltraCanvasMindMapIO.h"
#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasZipPackage.h"

#include "tinyxml2.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <cmath>

namespace UltraCanvas {

namespace {

// ---------------------------------------------------------------------------
// SHARED HELPERS
// ---------------------------------------------------------------------------

std::string Trim(const std::string& text) {
    size_t begin = text.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return std::string();
    size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(begin, end - begin + 1);
}

// Indentation width in columns, counting a tab as `tabWidth`.
int IndentWidth(const std::string& line, int tabWidth = 4) {
    int width = 0;
    for (char c : line) {
        if (c == ' ') ++width;
        else if (c == '\t') width += tabWidth;
        else break;
    }
    return width;
}

std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

std::string EscapeXml(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:   out += c;        break;
        }
    }
    return out;
}

// Builds a tree from (level, text) pairs, replacing whatever the model held.
// Level 0 is the central topic. A level deeper than lastLevel+1 is clamped.
size_t BuildTree(MindMapModel& model,
                 const std::vector<std::pair<int, std::string>>& entries) {
    model.Clear();
    model.BuildFromOutline(entries);
    return model.GetTopicCount();
}

// Converts a hex colour such as "#ff8800" or "#ff8800cc" to a Color.
bool ParseHexColor(const std::string& text, Color& out) {
    std::string hex = Trim(text);
    if (hex.empty() || hex[0] != '#') return false;
    hex = hex.substr(1);
    if (hex.size() != 6 && hex.size() != 8) return false;
    auto component = [&](size_t index) -> int {
        return static_cast<int>(std::stoul(hex.substr(index * 2, 2), nullptr, 16));
    };
    try {
        out = Color(component(0), component(1), component(2),
                    hex.size() == 8 ? component(3) : 255);
    } catch (...) {
        return false;
    }
    return true;
}

std::string ToHexColor(const Color& color) {
    static const char* digits = "0123456789abcdef";
    std::string out = "#";
    for (int channel : {color.r, color.g, color.b}) {
        out += digits[(channel >> 4) & 0xF];
        out += digits[channel & 0xF];
    }
    return out;
}

// ---------------------------------------------------------------------------
// MERMAID SHAPE DELIMITERS
// ---------------------------------------------------------------------------

struct MermaidShapeSpec {
    const char* open;
    const char* close;
    MindMapNodeShape shape;
};

// Order matters: the longer delimiters must be tested before their prefixes,
// otherwise "((circle))" is mis-read as "(rounded)".
const MermaidShapeSpec kMermaidShapes[] = {
    {"((", "))", MindMapNodeShape::Circle},
    {"))", "((", MindMapNodeShape::Cloud},
    {"{{", "}}", MindMapNodeShape::Hexagon},
    {"[",  "]",  MindMapNodeShape::Rect},
    {"(",  ")",  MindMapNodeShape::RoundedRect},
    {">",  "]",  MindMapNodeShape::Bang},
};

// Strips a shape delimiter from a Mermaid node body. Returns the inner text and
// reports which shape was used.
//
// Mermaid allows an identifier before the delimiter - "root((Text))" - and the
// identifier carries no meaning here, so the opening delimiter is searched for
// anywhere in the line rather than only at position 0.
std::string StripMermaidShape(const std::string& body, MindMapNodeShape& outShape,
                              bool& hadShape) {
    hadShape = false;
    std::string text = Trim(body);
    for (const auto& spec : kMermaidShapes) {
        std::string open(spec.open);
        std::string close(spec.close);
        if (text.size() <= open.size() + close.size()) continue;
        if (text.compare(text.size() - close.size(), close.size(), close) != 0) continue;

        size_t openPos = text.find(open);
        if (openPos == std::string::npos) continue;
        size_t innerStart = openPos + open.size();
        size_t innerEnd = text.size() - close.size();
        if (innerStart >= innerEnd) continue;

        outShape = spec.shape;
        hadShape = true;
        return Trim(text.substr(innerStart, innerEnd - innerStart));
    }
    return text;
}

const char* MermaidShapeOpen(MindMapNodeShape shape) {
    switch (shape) {
        case MindMapNodeShape::Circle:  return "((";
        case MindMapNodeShape::Cloud:   return "))";
        case MindMapNodeShape::Hexagon: return "{{";
        case MindMapNodeShape::Rect:    return "[";
        case MindMapNodeShape::Bang:    return ">";
        case MindMapNodeShape::RoundedRect:
        case MindMapNodeShape::Pill:    return "(";
        default:                        return "";
    }
}

const char* MermaidShapeClose(MindMapNodeShape shape) {
    switch (shape) {
        case MindMapNodeShape::Circle:  return "))";
        case MindMapNodeShape::Cloud:   return "((";
        case MindMapNodeShape::Hexagon: return "}}";
        case MindMapNodeShape::Rect:    return "]";
        case MindMapNodeShape::Bang:    return "]";
        case MindMapNodeShape::RoundedRect:
        case MindMapNodeShape::Pill:    return ")";
        default:                        return "";
    }
}

} // namespace

// ===========================================================================
// MERMAID
// ===========================================================================

MindMapImportResult UltraCanvasMindMapIO::FromMermaid(MindMapModel& model,
                                                      const std::string& source) {
    MindMapImportResult result;

    std::vector<std::string> lines = SplitLines(source);
    // Collect (indent, text, shape, icon) before touching the model, so a
    // malformed source leaves the caller's map intact.
    struct Entry {
        int indent = 0;
        std::string text;
        MindMapNodeShape shape = MindMapNodeShape::RoundedRect;
        bool hasShape = false;
        std::string iconName;
    };
    std::vector<Entry> entries;
    bool sawHeader = false;

    for (const auto& rawLine : lines) {
        std::string trimmed = Trim(rawLine);
        if (trimmed.empty()) continue;
        if (trimmed.rfind("%%", 0) == 0) continue;   // Mermaid comment

        if (!sawHeader) {
            if (trimmed == "mindmap" || trimmed.rfind("mindmap ", 0) == 0) {
                sawHeader = true;
                continue;
            }
            // Tolerate a source that omits the header.
            sawHeader = true;
        }

        // ::icon(fa fa-book) attaches to the previous entry.
        if (trimmed.rfind("::icon(", 0) == 0) {
            size_t close = trimmed.find(')');
            if (close != std::string::npos && !entries.empty()) {
                entries.back().iconName = Trim(trimmed.substr(7, close - 7));
            }
            continue;
        }
        // :::className also attaches to the previous entry; kept as-is since
        // CSS classes have no direct equivalent here.
        if (trimmed.rfind(":::", 0) == 0) continue;

        Entry entry;
        entry.indent = IndentWidth(rawLine);

        entry.text = StripMermaidShape(trimmed, entry.shape, entry.hasShape);
        if (entry.text.empty()) continue;
        entries.push_back(entry);
    }

    if (entries.empty()) {
        result.error = "no mindmap nodes found";
        return result;
    }

    // Indentation columns are arbitrary in Mermaid; rank the distinct widths
    // seen and use the rank as the level, so 2-, 4- or tab-indented sources
    // all import identically.
    std::vector<int> widths;
    for (const auto& entry : entries) widths.push_back(entry.indent);
    std::sort(widths.begin(), widths.end());
    widths.erase(std::unique(widths.begin(), widths.end()), widths.end());

    auto levelOf = [&](int indent) {
        auto it = std::lower_bound(widths.begin(), widths.end(), indent);
        return static_cast<int>(std::distance(widths.begin(), it));
    };

    std::vector<std::pair<int, std::string>> outline;
    outline.reserve(entries.size());
    for (const auto& entry : entries) {
        outline.emplace_back(levelOf(entry.indent), entry.text);
    }

    BuildTree(model, outline);

    // Re-apply shapes and icons in the same pre-order the outline was built in.
    std::vector<std::string> orderedIds;
    model.ForEachTopic([&](const MindMapTopic& topic) { orderedIds.push_back(topic.id); });
    for (size_t i = 0; i < orderedIds.size() && i < entries.size(); ++i) {
        MindMapTopic* topic = model.GetTopic(orderedIds[i]);
        if (!topic) continue;
        if (entries[i].hasShape) {
            MindMapTopicStyle style = topic->styleOverride.value_or(MindMapTopicStyle());
            style.shape = entries[i].shape;
            topic->styleOverride = style;
        }
        if (!entries[i].iconName.empty()) topic->iconPath = entries[i].iconName;
    }

    result.success = true;
    result.topicsImported = model.GetTopicCount();
    return result;
}

std::string UltraCanvasMindMapIO::ToMermaid(const MindMapModel& model) {
    std::ostringstream out;
    out << "mindmap\n";

    std::function<void(const std::string&, int)> walk =
        [&](const std::string& id, int depth) {
            const MindMapTopic* topic = model.GetTopic(id);
            if (!topic) return;

            std::string indent((depth + 1) * 2, ' ');
            MindMapNodeShape shape = topic->styleOverride.has_value()
                ? topic->styleOverride->shape
                : MindMapNodeShape::RoundedRect;

            const char* open = MermaidShapeOpen(shape);
            const char* close = MermaidShapeClose(shape);
            if (open[0] == '\0') {
                out << indent << topic->text << "\n";
            } else {
                out << indent << open << topic->text << close << "\n";
            }
            if (!topic->iconPath.empty()) {
                out << indent << "::icon(" << topic->iconPath << ")\n";
            }
            for (const auto& childId : topic->childIds) walk(childId, depth + 1);
        };

    if (!model.GetRootId().empty()) walk(model.GetRootId(), 0);
    return out.str();
}

// ===========================================================================
// FREEMIND / FREEPLANE
// ===========================================================================

MindMapImportResult UltraCanvasMindMapIO::FromFreeMind(MindMapModel& model,
                                                       const std::string& xml) {
    MindMapImportResult result;

    tinyxml2::XMLDocument document;
    if (document.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS) {
        result.error = document.ErrorStr() ? document.ErrorStr() : "malformed XML";
        return result;
    }

    const tinyxml2::XMLElement* mapElement = document.FirstChildElement("map");
    if (!mapElement) {
        result.error = "not a FreeMind document (no <map> root)";
        return result;
    }
    const tinyxml2::XMLElement* rootNode = mapElement->FirstChildElement("node");
    if (!rootNode) {
        result.error = "FreeMind document has no root <node>";
        return result;
    }

    model.Clear();

    // FreeMind nests <node TEXT="..."> and marks side with POSITION on the
    // first level. Richer content lives in <richcontent>, which is read as
    // plain text when TEXT is absent.
    std::function<std::string(const tinyxml2::XMLElement*, const std::string&)> importNode =
        [&](const tinyxml2::XMLElement* element, const std::string& parentId) -> std::string {
            const char* textAttr = element->Attribute("TEXT");
            std::string text = textAttr ? textAttr : std::string();
            if (text.empty()) {
                if (const tinyxml2::XMLElement* rich = element->FirstChildElement("richcontent")) {
                    if (const char* inner = rich->GetText()) text = Trim(inner);
                }
            }

            std::string id = parentId.empty() ? model.SetCentralTopic(text)
                                              : model.AddTopic(parentId, text);
            if (id.empty()) return id;

            MindMapTopic* topic = model.GetTopic(id);
            if (topic) {
                if (const char* position = element->Attribute("POSITION")) {
                    std::string side(position);
                    topic->side = (side == "left") ? MindMapTopicSide::Left
                                                   : MindMapTopicSide::Right;
                }
                if (const char* folded = element->Attribute("FOLDED")) {
                    topic->collapsed = (std::string(folded) == "true");
                }
                if (const char* link = element->Attribute("LINK")) {
                    topic->link = link;
                }
                if (const tinyxml2::XMLElement* icon = element->FirstChildElement("icon")) {
                    if (const char* builtin = icon->Attribute("BUILTIN")) {
                        topic->iconPath = builtin;
                    }
                }
                if (const char* colorAttr = element->Attribute("COLOR")) {
                    Color color;
                    if (ParseHexColor(colorAttr, color)) {
                        MindMapTopicStyle style = topic->styleOverride.value_or(MindMapTopicStyle());
                        style.textColor = color;
                        topic->styleOverride = style;
                    }
                }
            }

            for (const tinyxml2::XMLElement* child = element->FirstChildElement("node");
                 child; child = child->NextSiblingElement("node")) {
                importNode(child, id);
            }
            return id;
        };

    importNode(rootNode, std::string());

    result.success = true;
    result.topicsImported = model.GetTopicCount();
    return result;
}

std::string UltraCanvasMindMapIO::ToFreeMind(const MindMapModel& model) {
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<map version=\"1.0.1\">\n";

    std::function<void(const std::string&, int)> walk =
        [&](const std::string& id, int depth) {
            const MindMapTopic* topic = model.GetTopic(id);
            if (!topic) return;

            std::string indent((depth + 1) * 2, ' ');
            out << indent << "<node TEXT=\"" << EscapeXml(topic->text) << "\"";
            if (depth == 1) {
                out << " POSITION=\""
                    << (topic->side == MindMapTopicSide::Left ? "left" : "right") << "\"";
            }
            if (topic->collapsed) out << " FOLDED=\"true\"";
            if (!topic->link.empty()) out << " LINK=\"" << EscapeXml(topic->link) << "\"";
            if (topic->styleOverride.has_value()) {
                out << " COLOR=\"" << ToHexColor(topic->styleOverride->textColor) << "\"";
            }

            if (topic->childIds.empty() && topic->iconPath.empty()) {
                out << "/>\n";
                return;
            }
            out << ">\n";
            if (!topic->iconPath.empty()) {
                out << indent << "  <icon BUILTIN=\"" << EscapeXml(topic->iconPath) << "\"/>\n";
            }
            for (const auto& childId : topic->childIds) walk(childId, depth + 1);
            out << indent << "</node>\n";
        };

    if (!model.GetRootId().empty()) walk(model.GetRootId(), 0);
    out << "</map>\n";
    return out.str();
}

// ===========================================================================
// OPML
// ===========================================================================

MindMapImportResult UltraCanvasMindMapIO::FromOpml(MindMapModel& model,
                                                   const std::string& xml) {
    MindMapImportResult result;

    tinyxml2::XMLDocument document;
    if (document.Parse(xml.c_str(), xml.size()) != tinyxml2::XML_SUCCESS) {
        result.error = document.ErrorStr() ? document.ErrorStr() : "malformed XML";
        return result;
    }

    const tinyxml2::XMLElement* opml = document.FirstChildElement("opml");
    if (!opml) {
        result.error = "not an OPML document (no <opml> root)";
        return result;
    }
    const tinyxml2::XMLElement* body = opml->FirstChildElement("body");
    if (!body) {
        result.error = "OPML document has no <body>";
        return result;
    }
    const tinyxml2::XMLElement* firstOutline = body->FirstChildElement("outline");
    if (!firstOutline) {
        result.error = "OPML body has no <outline>";
        return result;
    }

    model.Clear();

    auto outlineText = [](const tinyxml2::XMLElement* element) -> std::string {
        if (const char* text = element->Attribute("text")) return text;
        if (const char* title = element->Attribute("title")) return title;
        return std::string();
    };

    std::function<void(const tinyxml2::XMLElement*, const std::string&)> importOutline =
        [&](const tinyxml2::XMLElement* element, const std::string& parentId) {
            std::string text = outlineText(element);
            std::string id = parentId.empty() ? model.SetCentralTopic(text)
                                              : model.AddTopic(parentId, text);
            if (id.empty()) return;

            if (MindMapTopic* topic = model.GetTopic(id)) {
                if (const char* link = element->Attribute("url")) topic->link = link;
                if (const char* note = element->Attribute("_note")) topic->note = note;
            }
            for (const tinyxml2::XMLElement* child = element->FirstChildElement("outline");
                 child; child = child->NextSiblingElement("outline")) {
                importOutline(child, id);
            }
        };

    // A body with several top-level outlines has no single root, so the first
    // becomes the centre and the rest become its children rather than being
    // dropped.
    importOutline(firstOutline, std::string());
    for (const tinyxml2::XMLElement* sibling = firstOutline->NextSiblingElement("outline");
         sibling; sibling = sibling->NextSiblingElement("outline")) {
        importOutline(sibling, model.GetRootId());
    }

    result.success = true;
    result.topicsImported = model.GetTopicCount();
    return result;
}

std::string UltraCanvasMindMapIO::ToOpml(const MindMapModel& model, const std::string& title) {
    std::ostringstream out;
    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<opml version=\"2.0\">\n";
    out << "  <head>\n    <title>" << EscapeXml(title) << "</title>\n  </head>\n";
    out << "  <body>\n";

    std::function<void(const std::string&, int)> walk =
        [&](const std::string& id, int depth) {
            const MindMapTopic* topic = model.GetTopic(id);
            if (!topic) return;

            std::string indent((depth + 2) * 2, ' ');
            out << indent << "<outline text=\"" << EscapeXml(topic->text) << "\"";
            if (!topic->link.empty()) out << " url=\"" << EscapeXml(topic->link) << "\"";
            if (!topic->note.empty()) out << " _note=\"" << EscapeXml(topic->note) << "\"";

            if (topic->childIds.empty()) {
                out << "/>\n";
                return;
            }
            out << ">\n";
            for (const auto& childId : topic->childIds) walk(childId, depth + 1);
            out << indent << "</outline>\n";
        };

    if (!model.GetRootId().empty()) walk(model.GetRootId(), 0);
    out << "  </body>\n</opml>\n";
    return out.str();
}

// ===========================================================================
// INDENTED TEXT
// ===========================================================================

MindMapImportResult UltraCanvasMindMapIO::FromIndentedText(MindMapModel& model,
                                                           const std::string& text,
                                                           int spacesPerLevel) {
    MindMapImportResult result;
    if (spacesPerLevel <= 0) spacesPerLevel = 2;

    std::vector<std::pair<int, std::string>> outline;
    for (const auto& rawLine : SplitLines(text)) {
        std::string trimmed = Trim(rawLine);
        if (trimmed.empty()) continue;

        int width = IndentWidth(rawLine, spacesPerLevel);
        int level = width / spacesPerLevel;

        // Tolerate a leading bullet.
        if (!trimmed.empty() && (trimmed[0] == '-' || trimmed[0] == '*' || trimmed[0] == '+')) {
            std::string body = Trim(trimmed.substr(1));
            if (!body.empty()) trimmed = body;
        }
        outline.emplace_back(level, trimmed);
    }

    if (outline.empty()) {
        result.error = "no non-empty lines";
        return result;
    }

    BuildTree(model, outline);
    result.success = true;
    result.topicsImported = model.GetTopicCount();
    return result;
}

std::string UltraCanvasMindMapIO::ToIndentedText(const MindMapModel& model, int spacesPerLevel) {
    if (spacesPerLevel <= 0) spacesPerLevel = 2;
    std::ostringstream out;
    model.ForEachTopic([&](const MindMapTopic& topic) {
        int depth = model.GetDepth(topic.id);
        out << std::string(std::max(0, depth) * spacesPerLevel, ' ') << topic.text << "\n";
    });
    return out.str();
}

// ===========================================================================
// CSV
// ===========================================================================

namespace {

// Splits one CSV record, honouring quoted fields and doubled quotes.
std::vector<std::string> SplitCsvRecord(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    bool inQuotes = false;

    for (size_t i = 0; i < line.size(); ++i) {
        char c = line[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    current += '"';
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                current += c;
            }
        } else if (c == '"') {
            inQuotes = true;
        } else if (c == ',') {
            fields.push_back(current);
            current.clear();
        } else {
            current += c;
        }
    }
    fields.push_back(current);
    return fields;
}

std::string QuoteCsvField(const std::string& field) {
    bool needsQuotes = field.find_first_of(",\"\n\r") != std::string::npos;
    if (!needsQuotes) return field;
    std::string out = "\"";
    for (char c : field) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += '"';
    return out;
}

} // namespace

MindMapImportResult UltraCanvasMindMapIO::FromCsv(MindMapModel& model, const std::string& csv,
                                                  const std::string& idColumn,
                                                  const std::string& parentColumn,
                                                  const std::string& textColumn) {
    MindMapImportResult result;

    std::vector<std::string> lines = SplitLines(csv);
    lines.erase(std::remove_if(lines.begin(), lines.end(),
                               [](const std::string& l) { return Trim(l).empty(); }),
                lines.end());
    if (lines.size() < 2) {
        result.error = "CSV needs a header row and at least one data row";
        return result;
    }

    std::vector<std::string> header = SplitCsvRecord(lines[0]);
    for (auto& field : header) {
        field = Trim(field);
        std::transform(field.begin(), field.end(), field.begin(),
                       [](unsigned char c) { return std::tolower(c); });
    }

    auto columnIndex = [&](const std::string& name) -> int {
        std::string needle = name;
        std::transform(needle.begin(), needle.end(), needle.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        for (size_t i = 0; i < header.size(); ++i) {
            if (header[i] == needle) return static_cast<int>(i);
        }
        return -1;
    };

    int idIdx = columnIndex(idColumn);
    int parentIdx = columnIndex(parentColumn);
    int textIdx = textColumn.empty() ? -1 : columnIndex(textColumn);
    if (textIdx < 0) {
        for (const char* candidate : {"text", "label", "title", "name"}) {
            textIdx = columnIndex(candidate);
            if (textIdx >= 0) break;
        }
    }

    if (idIdx < 0) {
        result.error = "CSV has no '" + idColumn + "' column";
        return result;
    }
    if (parentIdx < 0) {
        result.error = "CSV has no '" + parentColumn + "' column";
        return result;
    }
    if (textIdx < 0) {
        result.error = "CSV has no text column (tried text, label, title, name)";
        return result;
    }

    // Read every row before building, so parents may appear after children.
    struct Row { std::string id, parent, text; };
    std::vector<Row> rows;
    for (size_t i = 1; i < lines.size(); ++i) {
        std::vector<std::string> fields = SplitCsvRecord(lines[i]);
        auto field = [&](int index) -> std::string {
            return (index >= 0 && index < static_cast<int>(fields.size()))
                ? Trim(fields[index]) : std::string();
        };
        Row row{field(idIdx), field(parentIdx), field(textIdx)};
        if (row.id.empty()) continue;
        rows.push_back(row);
    }
    if (rows.empty()) {
        result.error = "CSV has no usable data rows";
        return result;
    }

    model.Clear();

    // Insert every topic first, then wire parents - same two-pass shape the
    // JSON reader uses, for the same reason.
    for (const auto& row : rows) {
        MindMapTopic topic(row.id, row.text);
        topic.parentId = row.parent;
        model.Topics()[row.id] = topic;
    }
    for (const auto& row : rows) {
        if (row.parent.empty()) continue;
        auto parentIt = model.Topics().find(row.parent);
        if (parentIt == model.Topics().end()) continue;
        parentIt->second.childIds.push_back(row.id);
    }

    // The root is the first row with no parent (or an unresolvable one).
    std::string rootId;
    for (const auto& row : rows) {
        if (row.parent.empty() || model.Topics().find(row.parent) == model.Topics().end()) {
            rootId = row.id;
            break;
        }
    }
    model.RestoreStructure(rootId);

    result.success = true;
    result.topicsImported = model.GetTopicCount();
    return result;
}

std::string UltraCanvasMindMapIO::ToCsv(const MindMapModel& model) {
    std::ostringstream out;
    out << "id,parent,text,collapsed\n";
    model.ForEachTopic([&](const MindMapTopic& topic) {
        out << QuoteCsvField(topic.id) << ','
            << QuoteCsvField(topic.parentId) << ','
            << QuoteCsvField(topic.text) << ','
            << (topic.collapsed ? "true" : "false") << '\n';
    });
    return out.str();
}

// ===========================================================================
// XMIND
// ===========================================================================

MindMapImportResult UltraCanvasMindMapIO::FromXMindJson(MindMapModel& model,
                                                        const std::string& json) {
    MindMapImportResult result;

    JSONParseResult parseResult;
    JSONValue root = JSON::Parse(json, &parseResult);
    if (!parseResult.success) {
        result.error = "content.json: " + parseResult.errorMessage;
        return result;
    }

    // content.json is an array of sheets; the first sheet's rootTopic is the map.
    const JSONValue* sheet = nullptr;
    if (root.IsArray() && root.GetSize() > 0) {
        sheet = &root.At(0);
    } else if (root.IsObject()) {
        sheet = &root;
    }
    if (!sheet) {
        result.error = "content.json has no sheet";
        return result;
    }

    const JSONValue& rootTopic = sheet->Get("rootTopic");
    if (!rootTopic.IsObject()) {
        result.error = "sheet has no rootTopic";
        return result;
    }

    model.Clear();

    std::function<void(const JSONValue&, const std::string&)> importTopic =
        [&](const JSONValue& value, const std::string& parentId) {
            std::string title = value.Get("title").GetString();
            std::string id = parentId.empty() ? model.SetCentralTopic(title)
                                              : model.AddTopic(parentId, title);
            if (id.empty()) return;

            if (MindMapTopic* topic = model.GetTopic(id)) {
                // XMind marks folded state on the topic itself.
                topic->collapsed = value.Get("branch").GetString() == "folded";
                const JSONValue& href = value.Get("href");
                if (href.IsString()) topic->link = href.GetString();
                const JSONValue& notes = value.Get("notes");
                if (notes.IsObject()) {
                    topic->note = notes.Get("plain").Get("content").GetString();
                }
            }

            // Children live under children.attached (and .detached for
            // floating topics, which XMind hangs off the same node).
            const JSONValue& children = value.Get("children");
            if (children.IsObject()) {
                const JSONValue& attached = children.Get("attached");
                for (size_t i = 0; i < attached.GetSize(); ++i) {
                    importTopic(attached.At(i), id);
                }
                const JSONValue& detached = children.Get("detached");
                for (size_t i = 0; i < detached.GetSize(); ++i) {
                    // Detached topics become floating topics at the origin;
                    // XMind stores their position separately per sheet.
                    std::string floatingId =
                        model.AddFloatingTopic(detached.At(i).Get("title").GetString(), 0.0, 0.0);
                    const JSONValue& nested = detached.At(i).Get("children");
                    if (nested.IsObject()) {
                        const JSONValue& nestedAttached = nested.Get("attached");
                        for (size_t j = 0; j < nestedAttached.GetSize(); ++j) {
                            importTopic(nestedAttached.At(j), floatingId);
                        }
                    }
                }
            }
        };

    importTopic(rootTopic, std::string());

    result.success = true;
    result.topicsImported = model.GetTopicCount();
    return result;
}

MindMapImportResult UltraCanvasMindMapIO::FromXMindFile(MindMapModel& model,
                                                        const std::string& filePath) {
    MindMapImportResult result;

    UCZipPackageReader reader;
    if (!reader.Open(filePath)) {
        result.error = "cannot open .xmind archive: " + reader.GetLastError();
        return result;
    }

    std::string content;
    if (reader.ReadEntry("content.json", content)) {
        return FromXMindJson(model, content);
    }

    // Pre-2021 archives use an XML content stream instead. Say so plainly
    // rather than failing with a confusing JSON parse error.
    if (reader.HasEntry("content.xml")) {
        result.error = "legacy XMind archive (content.xml); only content.json "
                       "archives are supported";
        return result;
    }

    result.error = "archive contains no content.json";
    return result;
}

// ===========================================================================
// SVG EXPORT
// ===========================================================================

std::string UltraCanvasMindMapIO::ToSvg(const MindMapModel& model,
                                        const MindMapLayoutResult& layout,
                                        const MindMapSvgOptions& options) {
    std::ostringstream out;

    const DiagramContentBounds& bounds = layout.contentBounds;
    if (!bounds.IsUsable()) {
        // Still emit a valid, empty document rather than a broken file.
        out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            << "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"1\" height=\"1\"/>\n";
        return out.str();
    }

    double padding = options.padding;
    double width = (bounds.GetWidth() + padding * 2.0) * options.scale;
    double height = (bounds.GetHeight() + padding * 2.0) * options.scale;
    double originX = bounds.minX - padding;
    double originY = bounds.minY - padding;

    auto styleFor = [&](const MindMapTopic& topic) -> MindMapTopicStyle {
        if (options.styleOf) return options.styleOf(topic);
        return topic.styleOverride.value_or(MindMapTopicStyle());
    };

    auto colorAttr = [](const Color& color) { return ToHexColor(color); };
    auto opacityAttr = [](const Color& color) { return color.a / 255.0; };

    out << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out << "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" "
        << "width=\"" << width << "\" height=\"" << height << "\" "
        << "viewBox=\"" << originX << " " << originY << " "
        << (bounds.GetWidth() + padding * 2.0) << " "
        << (bounds.GetHeight() + padding * 2.0) << "\">\n";

    out << "  <rect x=\"" << originX << "\" y=\"" << originY
        << "\" width=\"" << (bounds.GetWidth() + padding * 2.0)
        << "\" height=\"" << (bounds.GetHeight() + padding * 2.0)
        << "\" fill=\"" << colorAttr(options.backgroundColor) << "\"/>\n";

    // Connectors first so nodes paint over their ends.
    out << "  <g stroke-linecap=\"round\" fill=\"none\">\n";
    for (const auto& [id, box] : layout.bounds) {
        const MindMapTopic* topic = model.GetTopic(id);
        if (!topic || topic->parentId.empty()) continue;
        if (!layout.Has(topic->parentId)) continue;

        Rect2Dd parentBox = layout.Get(topic->parentId);
        double x1 = parentBox.x + parentBox.width * 0.5;
        double y1 = parentBox.y + parentBox.height * 0.5;
        double x2 = box.x + box.width * 0.5;
        double y2 = box.y + box.height * 0.5;
        double midX = (x1 + x2) * 0.5;

        out << "    <path d=\"M " << x1 << " " << y1
            << " C " << midX << " " << y1 << ", " << midX << " " << y2
            << ", " << x2 << " " << y2 << "\" stroke=\""
            << colorAttr(options.connectorColor) << "\" stroke-width=\""
            << options.connectorWidth << "\"/>\n";
    }
    out << "  </g>\n";

    // Relationships.
    for (const auto& relationship : model.Relationships()) {
        if (!layout.Has(relationship.fromTopicId) || !layout.Has(relationship.toTopicId)) continue;
        Rect2Dd fromBox = layout.Get(relationship.fromTopicId);
        Rect2Dd toBox = layout.Get(relationship.toTopicId);
        out << "  <line x1=\"" << (fromBox.x + fromBox.width * 0.5)
            << "\" y1=\"" << (fromBox.y + fromBox.height * 0.5)
            << "\" x2=\"" << (toBox.x + toBox.width * 0.5)
            << "\" y2=\"" << (toBox.y + toBox.height * 0.5)
            << "\" stroke=\"" << colorAttr(relationship.color)
            << "\" stroke-width=\"" << relationship.lineWidth << "\"";
        if (relationship.dashed) out << " stroke-dasharray=\"6,4\"";
        out << "/>\n";
    }

    // Nodes.
    for (const auto& [id, box] : layout.bounds) {
        const MindMapTopic* topic = model.GetTopic(id);
        if (!topic) continue;
        MindMapTopicStyle style = styleFor(*topic);

        switch (style.shape) {
            case MindMapNodeShape::Circle:
            case MindMapNodeShape::Ellipse:
                out << "  <ellipse cx=\"" << (box.x + box.width * 0.5)
                    << "\" cy=\"" << (box.y + box.height * 0.5)
                    << "\" rx=\"" << (box.width * 0.5)
                    << "\" ry=\"" << (box.height * 0.5) << "\"";
                break;
            case MindMapNodeShape::Diamond: {
                out << "  <polygon points=\""
                    << (box.x + box.width * 0.5) << "," << box.y << " "
                    << (box.x + box.width) << "," << (box.y + box.height * 0.5) << " "
                    << (box.x + box.width * 0.5) << "," << (box.y + box.height) << " "
                    << box.x << "," << (box.y + box.height * 0.5) << "\"";
                break;
            }
            case MindMapNodeShape::Underline:
            case MindMapNodeShape::NoShape:
                out << "  <rect x=\"" << box.x << "\" y=\"" << box.y
                    << "\" width=\"" << box.width << "\" height=\"" << box.height
                    << "\" fill=\"none\" stroke=\"none\"";
                break;
            default: {
                double radius = (style.shape == MindMapNodeShape::Pill)
                    ? box.height * 0.5
                    : (style.shape == MindMapNodeShape::Rect ? 0.0 : style.cornerRadius);
                out << "  <rect x=\"" << box.x << "\" y=\"" << box.y
                    << "\" width=\"" << box.width << "\" height=\"" << box.height
                    << "\" rx=\"" << radius << "\"";
                break;
            }
        }

        if (style.shape != MindMapNodeShape::Underline &&
            style.shape != MindMapNodeShape::NoShape) {
            Color fill = style.outlineOnly ? Color(0, 0, 0, 0) : style.fillColor;
            out << " fill=\"" << (fill.a == 0 ? "none" : colorAttr(fill)) << "\"";
            if (fill.a != 0 && fill.a != 255) out << " fill-opacity=\"" << opacityAttr(fill) << "\"";
            out << " stroke=\"" << colorAttr(style.borderColor)
                << "\" stroke-width=\"" << style.borderWidth << "\"";
        }
        out << "/>\n";

        if (style.shape == MindMapNodeShape::Underline) {
            out << "  <line x1=\"" << box.x << "\" y1=\"" << (box.y + box.height)
                << "\" x2=\"" << (box.x + box.width) << "\" y2=\"" << (box.y + box.height)
                << "\" stroke=\"" << colorAttr(style.borderColor)
                << "\" stroke-width=\"" << style.borderWidth << "\"/>\n";
        }

        if (!topic->text.empty()) {
            out << "  <text x=\"" << (box.x + box.width * 0.5)
                << "\" y=\"" << (box.y + box.height * 0.5)
                << "\" font-family=\"" << EscapeXml(options.fontFamily)
                << "\" font-size=\"" << style.fontSize << "\""
                << (style.bold ? " font-weight=\"bold\"" : "")
                << " fill=\"" << colorAttr(style.textColor)
                << "\" text-anchor=\"middle\" dominant-baseline=\"central\">"
                << EscapeXml(topic->text) << "</text>\n";
        }
    }

    out << "</svg>\n";
    return out.str();
}

bool UltraCanvasMindMapIO::WriteSvgFile(const std::string& filePath,
                                        const MindMapModel& model,
                                        const MindMapLayoutResult& layout,
                                        const MindMapSvgOptions& options) {
    std::ofstream file(filePath, std::ios::binary);
    if (!file) return false;
    file << ToSvg(model, layout, options);
    return file.good();
}

// ===========================================================================
// FORMAT DETECTION
// ===========================================================================

UltraCanvasMindMapIO::Format UltraCanvasMindMapIO::DetectFormat(const std::string& text) {
    std::string head = text.substr(0, std::min<size_t>(text.size(), 4096));
    std::string lower = head;
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower.find("<opml") != std::string::npos) return Format::Opml;
    if (lower.find("<map") != std::string::npos &&
        lower.find("<node") != std::string::npos) return Format::FreeMind;
    if (lower.find("mindmap") != std::string::npos &&
        lower.find("<") == std::string::npos) return Format::Mermaid;

    std::string trimmed = Trim(head);
    if (!trimmed.empty() && (trimmed[0] == '{' || trimmed[0] == '[')) return Format::Json;
    if (trimmed.rfind("#", 0) == 0 || lower.find("\n#") != std::string::npos) {
        return Format::Markdown;
    }

    // A header row with an id and a parent column reads as the CSV shape.
    size_t firstNewline = trimmed.find('\n');
    std::string firstLine = trimmed.substr(0, firstNewline);
    std::string firstLower = firstLine;
    std::transform(firstLower.begin(), firstLower.end(), firstLower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (firstLower.find(',') != std::string::npos &&
        firstLower.find("id") != std::string::npos &&
        firstLower.find("parent") != std::string::npos) {
        return Format::Csv;
    }

    if (!trimmed.empty()) return Format::IndentedText;
    return Format::Unknown;
}

UltraCanvasMindMapIO::Format UltraCanvasMindMapIO::FormatFromExtension(
        const std::string& extension) {
    std::string ext = extension;
    if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (ext == "mm")                        return Format::FreeMind;
    if (ext == "opml")                      return Format::Opml;
    if (ext == "mmd" || ext == "mermaid")   return Format::Mermaid;
    if (ext == "md" || ext == "markdown")   return Format::Markdown;
    if (ext == "csv")                       return Format::Csv;
    if (ext == "json")                      return Format::Json;
    if (ext == "txt")                       return Format::IndentedText;
    return Format::Unknown;
}

} // namespace UltraCanvas
