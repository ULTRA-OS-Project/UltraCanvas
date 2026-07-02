// Plugins/Documents/eBook/FB2Engine.cpp
// FictionBook 2 engine: FB2 XML → metadata + chapters as HTML + binaries.
// The XML is parsed with the tolerant HTMLReader parser (namespace prefixes
// are stripped from tag names but kept on attribute names, so <image
// l:href="#id"/> arrives as tag "image" with attribute "l:href"). FB2 markup
// is converted to simple HTML per section so chapters flow through the same
// HTML::ElementBuilder path as EPUB.
// Version: 1.0.0
// Last Modified: 2026-07-02
// Author: UltraCanvas Framework

#include "FB2Engine.h"
#include "EBookArchive.h"

#include "HTMLReader/HTMLParser.h"
#include "HTMLReader/CSSStyleSheet.h"   // HTML::Trim

#include <algorithm>
#include <cctype>

namespace UltraCanvas {

namespace {

std::string EscapeHTML(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c; break;
        }
    }
    return out;
}

std::string CollapseWhitespace(const std::string& text) {
    std::string out;
    bool lastWasSpace = true;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!lastWasSpace) out += ' ';
            lastWasSpace = true;
        } else {
            out += c;
            lastWasSpace = false;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

bool IsWhitespaceText(const HTML::Node& n) {
    if (!n.IsText()) return false;
    for (char c : n.text) {
        if (!std::isspace(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

// FB2 links and images use xlink hrefs; producers vary in the prefix.
std::string LinkHref(const HTML::Node& n) {
    std::string href = n.GetAttribute("l:href");
    if (href.empty()) href = n.GetAttribute("xlink:href");
    if (href.empty()) href = n.GetAttribute("href");
    return href;
}

// Title text for the TOC / chapter list: the <p> lines of a <title> joined
// with spaces (TextContent alone would run the lines together).
std::string TitleText(const HTML::Node& title) {
    std::string out;
    for (const auto& child : title.children) {
        if (!child->IsElement()) continue;
        std::string line = CollapseWhitespace(child->TextContent());
        if (line.empty()) continue;
        if (!out.empty()) out += ' ';
        out += line;
    }
    if (out.empty()) out = CollapseWhitespace(title.TextContent());
    return out;
}

// ============================================================================
// FB2 → HTML CONVERSION
// ============================================================================

void EmitInline(const HTML::Node& n, std::string& out);

void EmitInlineChildren(const HTML::Node& n, std::string& out) {
    for (const auto& child : n.children) EmitInline(*child, out);
}

void EmitInline(const HTML::Node& n, std::string& out) {
    if (n.IsText()) {
        out += EscapeHTML(n.text);
        return;
    }
    if (!n.IsElement()) return;

    const std::string& tag = n.tag;
    if (tag == "emphasis") {
        out += "<em>"; EmitInlineChildren(n, out); out += "</em>";
    } else if (tag == "strong") {
        out += "<strong>"; EmitInlineChildren(n, out); out += "</strong>";
    } else if (tag == "strikethrough") {
        out += "<del>"; EmitInlineChildren(n, out); out += "</del>";
    } else if (tag == "sub" || tag == "sup" || tag == "code") {
        out += "<" + tag + ">"; EmitInlineChildren(n, out); out += "</" + tag + ">";
    } else if (tag == "a") {
        std::string href = LinkHref(n);
        out += "<a href=\"" + EscapeHTML(href) + "\">";
        EmitInlineChildren(n, out);
        out += "</a>";
    } else if (tag == "image") {
        std::string href = LinkHref(n);
        std::string alt = n.GetAttribute("alt");
        out += "<img src=\"" + EscapeHTML(href) + "\" alt=\"" + EscapeHTML(alt) + "\"/>";
    } else if (tag == "style") {
        // FB2's inline <style name="..."> element (unrelated to CSS).
        out += "<span>"; EmitInlineChildren(n, out); out += "</span>";
    } else {
        EmitInlineChildren(n, out);
    }
}

// <title> content becomes one heading; multiple <p> lines separated by <br/>.
void EmitHeading(const HTML::Node& title, int level, std::string& out) {
    level = std::max(1, std::min(level, 6));
    std::string inner;
    for (const auto& child : title.children) {
        if (!child->IsElement()) continue;
        std::string line;
        EmitInlineChildren(*child, line);
        if (HTML::Trim(line).empty()) continue;
        if (!inner.empty()) inner += "<br/>";
        inner += line;
    }
    if (inner.empty()) {
        EmitInlineChildren(title, inner);
        inner = HTML::Trim(inner);
    }
    if (inner.empty()) return;
    std::string h = std::to_string(level);
    out += "<h" + h + ">" + inner + "</h" + h + ">";
}

// `depth` is the section nesting depth; section titles map to h(depth + 1)
// (body title → h1, top-level section title → h2, ...).
void EmitBlockChildren(const HTML::Node& parent, std::string& out, int depth);

void EmitBlock(const HTML::Node& n, std::string& out, int depth) {
    if (n.IsText()) {
        // Stray text directly inside a section (invalid FB2, but tolerated).
        if (!IsWhitespaceText(n)) out += "<p>" + EscapeHTML(n.text) + "</p>";
        return;
    }
    if (!n.IsElement()) return;

    const std::string& tag = n.tag;
    if (tag == "p") {
        out += "<p>"; EmitInlineChildren(n, out); out += "</p>";
    } else if (tag == "empty-line") {
        out += "<p class=\"empty-line\"></p>";
    } else if (tag == "title") {
        EmitHeading(n, depth + 1, out);
    } else if (tag == "subtitle") {
        out += "<h4 class=\"subtitle\">"; EmitInlineChildren(n, out); out += "</h4>";
    } else if (tag == "epigraph") {
        out += "<blockquote class=\"epigraph\">";
        EmitBlockChildren(n, out, depth);
        out += "</blockquote>";
    } else if (tag == "cite") {
        out += "<blockquote class=\"cite\">";
        EmitBlockChildren(n, out, depth);
        out += "</blockquote>";
    } else if (tag == "annotation") {
        out += "<div class=\"annotation\">";
        EmitBlockChildren(n, out, depth);
        out += "</div>";
    } else if (tag == "poem") {
        out += "<div class=\"poem\">";
        EmitBlockChildren(n, out, depth);
        out += "</div>";
    } else if (tag == "stanza") {
        out += "<div class=\"stanza\">";
        EmitBlockChildren(n, out, depth);
        out += "</div>";
    } else if (tag == "v") {
        out += "<p class=\"verse\">"; EmitInlineChildren(n, out); out += "</p>";
    } else if (tag == "text-author") {
        out += "<p class=\"text-author\">"; EmitInlineChildren(n, out); out += "</p>";
    } else if (tag == "date") {
        out += "<p class=\"date\">"; EmitInlineChildren(n, out); out += "</p>";
    } else if (tag == "image") {
        std::string href = LinkHref(n);
        std::string alt = n.GetAttribute("alt");
        out += "<img src=\"" + EscapeHTML(href) + "\" alt=\"" + EscapeHTML(alt) + "\"/>";
    } else if (tag == "table") {
        out += "<table>";
        for (const auto& row : n.children) {
            if (!row->IsElement("tr")) continue;
            out += "<tr>";
            for (const auto& cell : row->children) {
                if (!cell->IsElement()) continue;
                if (cell->tag != "td" && cell->tag != "th") continue;
                out += "<" + cell->tag + ">";
                EmitInlineChildren(*cell, out);
                out += "</" + cell->tag + ">";
            }
            out += "</tr>";
        }
        out += "</table>";
    } else if (tag == "section") {
        out += "<div class=\"section\">";
        EmitBlockChildren(n, out, depth + 1);
        out += "</div>";
    } else {
        EmitBlockChildren(n, out, depth);
    }
}

void EmitBlockChildren(const HTML::Node& parent, std::string& out, int depth) {
    for (const auto& child : parent.children) EmitBlock(*child, out, depth);
}

} // namespace

// ============================================================================
// ENCODING
// ============================================================================

namespace {

void AppendUTF8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

// windows-1251 codepoints for bytes 0x80..0xBF; 0xC0..0xFF are the contiguous
// Cyrillic range U+0410..U+044F.
constexpr uint16_t kCp1251High[64] = {
    0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021,
    0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
    0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x0098, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
    0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7,
    0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
    0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7,
    0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457
};

std::string DeclaredEncoding(const std::vector<uint8_t>& data) {
    // Look for encoding="..." inside the XML declaration (first ~200 bytes).
    size_t limit = std::min<size_t>(data.size(), 200);
    std::string head(reinterpret_cast<const char*>(data.data()), limit);
    std::transform(head.begin(), head.end(), head.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    size_t at = head.find("encoding");
    if (at == std::string::npos) return {};
    size_t quote = head.find_first_of("\"'", at);
    if (quote == std::string::npos) return {};
    size_t end = head.find(head[quote], quote + 1);
    if (end == std::string::npos) return {};
    return head.substr(quote + 1, end - quote - 1);
}

std::string FromUTF16(const std::vector<uint8_t>& data, bool bigEndian) {
    std::string out;
    out.reserve(data.size());
    size_t i = 2;   // skip BOM
    auto unit = [&](size_t at) -> uint32_t {
        return bigEndian ? (data[at] << 8) | data[at + 1]
                         : (data[at + 1] << 8) | data[at];
    };
    while (i + 1 < data.size()) {
        uint32_t cp = unit(i);
        i += 2;
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < data.size()) {
            uint32_t low = unit(i);
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                i += 2;
            }
        }
        AppendUTF8(out, cp);
    }
    return out;
}

} // namespace

std::string FB2Engine::ToUTF8(const std::vector<uint8_t>& data) {
    if (data.size() >= 2) {
        if (data[0] == 0xFF && data[1] == 0xFE) return FromUTF16(data, false);
        if (data[0] == 0xFE && data[1] == 0xFF) return FromUTF16(data, true);
    }

    std::string encoding = DeclaredEncoding(data);
    bool cp1251 = (encoding == "windows-1251" || encoding == "cp1251");
    bool latin1 = (encoding == "iso-8859-1" || encoding == "latin-1" ||
                   encoding == "latin1");
    if (!cp1251 && !latin1) {
        // UTF-8 (declared or assumed) passes through.
        return std::string(reinterpret_cast<const char*>(data.data()), data.size());
    }

    std::string out;
    out.reserve(data.size() * 2);
    for (uint8_t byte : data) {
        if (byte < 0x80) {
            out += static_cast<char>(byte);
        } else if (latin1) {
            AppendUTF8(out, byte);
        } else if (byte < 0xC0) {
            AppendUTF8(out, kCp1251High[byte - 0x80]);
        } else {
            AppendUTF8(out, 0x0410 + (byte - 0xC0));
        }
    }
    return out;
}

// ============================================================================
// BASE64
// ============================================================================

std::vector<uint8_t> FB2Engine::DecodeBase64(const std::string& encoded) {
    static const char* kChars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    int table[256];
    std::fill(std::begin(table), std::end(table), -1);
    for (int i = 0; i < 64; ++i) {
        table[static_cast<unsigned char>(kChars[i])] = i;
    }

    std::vector<uint8_t> out;
    out.reserve(encoded.size() * 3 / 4);
    int val = 0;
    int bits = -8;
    for (unsigned char c : encoded) {
        if (c == '=') break;
        if (table[c] == -1) continue;   // whitespace / line breaks
        val = (val << 6) + table[c];
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<uint8_t>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

// ============================================================================
// LIFECYCLE
// ============================================================================

bool FB2Engine::LoadFromMemory(std::vector<uint8_t> data,
                               const std::string& password) {
    (void)password;   // FB2 has no encryption
    Close();

    if (EBookArchive::IsZipData(data.data(), data.size())) {
        ReportProgress(0.05f, "Unpacking fb2.zip...");
        EBookArchive archive;
        if (!archive.OpenFromMemory(std::move(data))) {
            Fail("Cannot open fb2.zip: " + archive.GetLastError());
            return false;
        }
        // Take the first .fb2 entry (fb2.zip archives hold one book).
        std::string inner;
        for (const auto& name : archive.FileNames()) {
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (lower.size() >= 4 && lower.rfind(".fb2") == lower.size() - 4) {
                inner = name;
                break;
            }
            if (inner.empty()) inner = name;
        }
        std::vector<uint8_t> unpacked;
        if (inner.empty() || !archive.ReadFile(inner, unpacked)) {
            Fail("fb2.zip contains no readable FB2 document");
            return false;
        }
        data = std::move(unpacked);
    }

    ReportProgress(0.2f, "Parsing FB2...");
    if (!ParseXML(ToUTF8(data))) return false;

    metadata.format = EBookFormat::FB2;
    metadata.formatVersion = "2.0";
    metadata.chapterCount = static_cast<int>(chapters.size());
    metadata.hasCover = !coverId.empty() && binaries.count(coverId) > 0;

    loaded = true;
    ReportProgress(1.0f, "Complete");
    return true;
}

void FB2Engine::Close() {
    ResetBaseState();
    chapters.clear();
    binaries.clear();
    coverId.clear();
}

// ============================================================================
// XML → CHAPTERS
// ============================================================================

bool FB2Engine::ParseXML(const std::string& xml) {
    HTML::Parser parser;
    HTML::Document doc = parser.Parse(xml);

    HTML::Node* fb = doc.root ? doc.root->FindFirst("fictionbook") : nullptr;
    if (!fb) {
        Fail("Not an FB2 document: <FictionBook> root element missing");
        return false;
    }

    for (const auto& child : fb->children) {
        if (child->IsElement("description")) {
            ParseDescription(*child);
            break;
        }
    }

    ParseBinaries(*fb);
    BuildChapters(*fb);

    if (chapters.empty()) {
        Fail("FB2 document has no readable body");
        return false;
    }

    BuildTOC();
    return true;
}

void FB2Engine::ParseDescription(HTML::Node& description) {
    HTML::Node* titleInfo = nullptr;
    HTML::Node* publishInfo = nullptr;
    for (const auto& child : description.children) {
        if (child->IsElement("title-info")) titleInfo = child.get();
        else if (child->IsElement("publish-info")) publishInfo = child.get();
    }

    if (titleInfo) {
        for (const auto& child : titleInfo->children) {
            if (!child->IsElement()) continue;
            const std::string& tag = child->tag;
            if (tag == "book-title") {
                metadata.title = CollapseWhitespace(child->TextContent());
            } else if (tag == "author" || tag == "translator") {
                // first/middle/last-name children; nickname as fallback.
                std::string name, nickname;
                for (const auto& part : child->children) {
                    if (!part->IsElement()) continue;
                    if (part->tag == "first-name" || part->tag == "middle-name" ||
                        part->tag == "last-name") {
                        std::string piece = CollapseWhitespace(part->TextContent());
                        if (piece.empty()) continue;
                        if (!name.empty()) name += ' ';
                        name += piece;
                    } else if (part->tag == "nickname") {
                        nickname = CollapseWhitespace(part->TextContent());
                    }
                }
                if (name.empty()) name = nickname;
                if (!name.empty() && tag == "author") {
                    metadata.authors.push_back(name);
                }
            } else if (tag == "genre") {
                std::string genre = CollapseWhitespace(child->TextContent());
                if (!genre.empty()) metadata.subjects.push_back(genre);
            } else if (tag == "annotation") {
                metadata.description = CollapseWhitespace(child->TextContent());
            } else if (tag == "lang") {
                metadata.language = CollapseWhitespace(child->TextContent());
            } else if (tag == "date") {
                if (metadata.publicationDate.empty()) {
                    metadata.publicationDate = CollapseWhitespace(child->TextContent());
                }
            } else if (tag == "sequence") {
                metadata.series = child->GetAttribute("name");
                std::string number = child->GetAttribute("number");
                if (!number.empty()) {
                    try { metadata.seriesIndex = std::stoi(number); } catch (...) {}
                }
            } else if (tag == "coverpage") {
                if (HTML::Node* image = child->FindFirst("image")) {
                    std::string href = LinkHref(*image);
                    if (!href.empty() && href[0] == '#') href.erase(0, 1);
                    coverId = href;
                }
            }
        }
    }

    if (publishInfo) {
        for (const auto& child : publishInfo->children) {
            if (!child->IsElement()) continue;
            const std::string& tag = child->tag;
            if (tag == "publisher") {
                metadata.publisher = CollapseWhitespace(child->TextContent());
            } else if (tag == "year") {
                // Publisher's year beats the title-info writing date.
                std::string year = CollapseWhitespace(child->TextContent());
                if (!year.empty()) metadata.publicationDate = year;
            } else if (tag == "isbn") {
                metadata.isbn = CollapseWhitespace(child->TextContent());
            }
        }
    }
}

void FB2Engine::ParseBinaries(HTML::Node& fictionBook) {
    for (const auto& child : fictionBook.children) {
        if (!child->IsElement("binary")) continue;
        std::string id = child->GetAttribute("id");
        if (id.empty()) continue;
        std::vector<uint8_t> bytes = DecodeBase64(child->TextContent());
        if (!bytes.empty()) binaries[id] = std::move(bytes);
    }
}

void FB2Engine::BuildChapters(HTML::Node& fictionBook) {
    // Main body: the first <body> without a name attribute (or the first one).
    HTML::Node* mainBody = nullptr;
    std::vector<HTML::Node*> extraBodies;
    for (const auto& child : fictionBook.children) {
        if (!child->IsElement("body")) continue;
        if (!mainBody && child->GetAttribute("name").empty()) {
            mainBody = child.get();
        } else {
            extraBodies.push_back(child.get());
        }
    }
    if (!mainBody && !extraBodies.empty()) {
        mainBody = extraBodies.front();
        extraBodies.erase(extraBodies.begin());
    }
    if (!mainBody) return;

    // Body-level title / epigraphs / images become a preamble prepended to
    // the first chapter. When the whole book hides inside a single top-level
    // section ("Part 1" wrappers), descend so chapters match real chapters.
    std::string preamble;
    HTML::Node* container = mainBody;
    int depth = 0;   // body title → h1, section titles below → h2...
    for (;;) {
        std::vector<HTML::Node*> sections;
        for (const auto& child : container->children) {
            if (child->IsElement("section")) sections.push_back(child.get());
            else EmitBlock(*child, preamble, depth);
        }
        if (sections.size() == 1) {
            bool hasSubsections = false;
            for (const auto& child : sections.front()->children) {
                if (child->IsElement("section")) { hasSubsections = true; break; }
            }
            if (hasSubsections) {
                container = sections.front();
                ++depth;
                continue;
            }
        }

        if (sections.empty()) {
            // Sectionless body: everything already went into the preamble.
            EBookChapter chapter;
            chapter.title = metadata.title.empty() ? "Book" : metadata.title;
            chapter.content = preamble;
            chapters.push_back(std::move(chapter));
            preamble.clear();
        } else {
            for (HTML::Node* section : sections) {
                EBookChapter chapter;
                std::string id = section->GetAttribute("id");
                if (!id.empty()) chapter.href = "#" + id;
                for (const auto& child : section->children) {
                    if (child->IsElement("title")) {
                        chapter.title = TitleText(*child);
                        break;
                    }
                }
                if (chapter.title.empty()) {
                    chapter.title = "Chapter " + std::to_string(chapters.size() + 1);
                }
                std::string html;
                EmitBlockChildren(*section, html, depth + 1);
                if (!preamble.empty()) {
                    html = preamble + html;
                    preamble.clear();
                }
                chapter.content = std::move(html);
                chapters.push_back(std::move(chapter));
            }
        }
        break;
    }

    // Notes / comments bodies: one trailing chapter per body.
    for (HTML::Node* body : extraBodies) {
        EBookChapter chapter;
        std::string name = body->GetAttribute("name");
        for (const auto& child : body->children) {
            if (child->IsElement("title")) {
                chapter.title = TitleText(*child);
                break;
            }
        }
        if (chapter.title.empty()) {
            if (!name.empty()) {
                name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
                chapter.title = name;
            } else {
                chapter.title = "Notes";
            }
        }
        std::string html;
        EmitBlockChildren(*body, html, 0);
        if (html.empty()) continue;
        chapter.content = std::move(html);
        chapters.push_back(std::move(chapter));
    }
}

void FB2Engine::BuildTOC() {
    tableOfContents.clear();
    tableOfContents.reserve(chapters.size());
    for (size_t i = 0; i < chapters.size(); ++i) {
        EBookTOCEntry entry(chapters[i].title, chapters[i].href,
                            static_cast<int>(i));
        entry.index = static_cast<int>(i);
        tableOfContents.push_back(std::move(entry));
    }
}

// ============================================================================
// CONTENT ACCESS
// ============================================================================

EBookChapter FB2Engine::GetChapter(int index) const {
    if (index < 0 || index >= static_cast<int>(chapters.size())) return {};
    return chapters[index];
}

std::string FB2Engine::GetStylesheets() const {
    // Default rendering for FB2's semantic classes.
    return
        ".epigraph { font-style: italic; margin: 1em 0 1em 15%; }\n"
        ".cite { font-style: italic; }\n"
        ".annotation { font-style: italic; }\n"
        ".poem { margin: 1em 10%; }\n"
        ".stanza { margin: 0.6em 0; }\n"
        ".verse { margin: 0; }\n"
        ".text-author { text-align: right; font-style: italic; margin: 0.3em 0; }\n"
        ".subtitle { text-align: center; }\n"
        ".date { text-align: right; font-style: italic; margin: 0.3em 0; }\n"
        ".empty-line { height: 1em; margin: 0; }\n"
        ".section { margin: 0; }\n";
}

std::vector<uint8_t> FB2Engine::GetResource(const std::string& href) const {
    std::string id = href;
    if (!id.empty() && id[0] == '#') id.erase(0, 1);
    auto it = binaries.find(id);
    return it == binaries.end() ? std::vector<uint8_t>{} : it->second;
}

std::vector<uint8_t> FB2Engine::GetCoverImage() const {
    if (coverId.empty()) return {};
    auto it = binaries.find(coverId);
    return it == binaries.end() ? std::vector<uint8_t>{} : it->second;
}

// ============================================================================
// REGISTRATION
// ============================================================================

void RegisterFB2Engine() {
    RegisterEBookEngine({"fb2", "fb2.zip"}, [] {
        return std::static_pointer_cast<IEBookEngine>(std::make_shared<FB2Engine>());
    });
}

} // namespace UltraCanvas
