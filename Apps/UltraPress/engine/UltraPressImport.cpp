// Apps/UltraPress/engine/UltraPressImport.cpp
// The WordPress importer. TinyXML2 reads the export (privately: no XML
// type reaches the header); the HTML reader's parser and ExtractPlainText
// read the bodies; the JSON module reads the block editor's attributes.
// Version: 0.4.1 - attachments land flat in media/ under the library's clean names
// Version: 0.4.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressImport.h"
#include "UltraPressMedia.h"

#include "UltraPressBlocks.h"
#include "UltraPressPublish.h"

#include "HTMLReader/HTMLDocument.h"
#include "HTMLReader/HTMLParser.h"
#include "UltraCanvasPathUtf8.h"

#include <tinyxml2.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <regex>
#include <set>

using UltraCanvas::JSONValue;
namespace HTML = UltraCanvas::HTML;

namespace UltraPress {

std::string ImportReport::Summary() const {
    std::string s = std::to_string(pages) + " pages, " + std::to_string(posts) + " posts";
    if (drafts) s += " (" + std::to_string(drafts) + " drafts)";
    s += ", " + std::to_string(menus) + " menus, " + std::to_string(attachments) + " attachments";
    if (!fetched.empty()) s += " (" + std::to_string(fetched.size()) + " fetched)";
    s += "; " + std::to_string(blocksMapped) + " blocks mapped, " + std::to_string(blocksAsRichText) + " kept as rich text";
    if (!redirects.empty()) s += "; " + std::to_string(redirects.size()) + " redirects";
    if (!warnings.empty()) s += "; " + std::to_string(warnings.size()) + " warnings";
    return s;
}

namespace {

std::string Trim(const std::string& text) {
    const size_t start = text.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return std::string();
    const size_t end = text.find_last_not_of(" \t\r\n");
    return text.substr(start, end - start + 1);
}

std::string ChildText(const tinyxml2::XMLElement* parent, const char* name) {
    if (!parent) return std::string();
    const tinyxml2::XMLElement* child = parent->FirstChildElement(name);
    if (!child || !child->GetText()) return std::string();
    return child->GetText();
}

// ----- The block editor's markers -------------------------------------------------
//
// <!-- wp:name {"attr":1} -->inner<!-- /wp:name -->, or <!-- wp:name /-->
// for one without content. Nested blocks sit inside their parent's inner
// markup; the parser keeps both the raw inner markup and the children.

struct WpBlock {
    std::string name;          // "paragraph", "core/paragraph" is written without the prefix
    JSONValue attrs = JSONValue::MakeObject();
    std::string inner;         // between the markers, markers of children included
    std::vector<WpBlock> children;
    std::string html;          // free markup before the first marker (classic content)
};

struct Marker {
    size_t start = 0, end = 0;   // [start, end) of the whole comment
    std::string name;
    std::string attrs;
    bool closing = false;
    bool selfClosing = false;
};

bool NextMarker(const std::string& html, size_t from, Marker& out) {
    const size_t open = html.find("<!-- ", from);
    if (open == std::string::npos) return false;
    size_t p = open + 5;
    bool closing = false;
    if (html.compare(p, 1, "/") == 0) { closing = true; ++p; }
    if (html.compare(p, 3, "wp:") != 0) return NextMarker(html, open + 1, out);
    p += 3;
    const size_t nameStart = p;
    while (p < html.size() && (std::isalnum(static_cast<unsigned char>(html[p])) || html[p] == '-' || html[p] == '/' || html[p] == '_')) ++p;
    std::string name = html.substr(nameStart, p - nameStart);
    if (name.rfind("core/", 0) == 0) name = name.substr(5);
    const size_t close = html.find("-->", p);
    if (close == std::string::npos) return false;
    std::string between = Trim(html.substr(p, close - p));
    bool selfClosing = false;
    if (!between.empty() && between.back() == '/') {
        selfClosing = true;
        between = Trim(between.substr(0, between.size() - 1));
    }
    out.start = open;
    out.end = close + 3;
    out.name = name;
    out.attrs = between;
    out.closing = closing;
    out.selfClosing = selfClosing;
    return true;
}

// Parses the markup from `from`; stops at the closing marker of `until`
// (empty: the end) and reports where it stopped.
std::vector<WpBlock> ParseBlocks(const std::string& html, size_t from, const std::string& until, size_t& stoppedAt,
                                 std::string& freeMarkup) {
    std::vector<WpBlock> blocks;
    size_t pos = from;
    Marker marker;
    while (NextMarker(html, pos, marker)) {
        freeMarkup += html.substr(pos, marker.start - pos);
        if (marker.closing) {
            if (marker.name == until) {
                stoppedAt = marker.end;
                return blocks;
            }
            pos = marker.end;   // a stray closer: skipped
            continue;
        }
        WpBlock block;
        block.name = marker.name;
        if (!marker.attrs.empty()) {
            UltraCanvas::JSONParseResult result;
            JSONValue attrs = UltraCanvas::JSON::Parse(marker.attrs, &result);
            if (result.success && attrs.IsObject()) block.attrs = attrs;
        }
        if (marker.selfClosing) {
            blocks.push_back(std::move(block));
            pos = marker.end;
            continue;
        }
        size_t innerEnd = html.size();
        std::string innerFree;
        block.children = ParseBlocks(html, marker.end, marker.name, innerEnd, innerFree);
        // innerEnd is just past the closer; the inner markup ends before it.
        size_t closerStart = innerEnd;
        if (innerEnd < html.size() || innerEnd == html.size()) {
            const std::string closer = "<!-- /wp:";
            const size_t found = html.rfind(closer, innerEnd);
            closerStart = found != std::string::npos && found >= marker.end ? found : innerEnd;
        }
        block.inner = html.substr(marker.end, closerStart - marker.end);
        blocks.push_back(std::move(block));
        pos = innerEnd;
    }
    freeMarkup += html.substr(std::min(pos, html.size()));
    stoppedAt = html.size();
    return blocks;
}

// The inner markup with every marker removed: what the block looks like as HTML.
std::string StripMarkers(const std::string& html) {
    std::string out;
    size_t pos = 0;
    Marker marker;
    while (NextMarker(html, pos, marker)) {
        out += html.substr(pos, marker.start - pos);
        pos = marker.end;
    }
    out += html.substr(std::min(pos, html.size()));
    return Trim(out);
}

const HTML::Node* FindTag(const HTML::Node* node, const std::string& tag) {
    if (!node) return nullptr;
    if (node->type == HTML::NodeType::Element && node->tag == tag) return node;
    for (const auto& child : node->children) {
        if (const HTML::Node* found = FindTag(child.get(), tag)) return found;
    }
    return nullptr;
}

std::string Attribute(const HTML::Node* node, const std::string& name) {
    if (!node) return std::string();
    for (const auto& [key, value] : node->attributes) {
        if (key == name) return value;
    }
    return std::string();
}

// True when the markup holds nothing but text, <br>, and the one wrapper tag.
bool IsPlainParagraph(const std::string& html, const HTML::Node* paragraph) {
    (void)html;
    if (!paragraph) return false;
    for (const auto& child : paragraph->children) {
        if (child->type == HTML::NodeType::Element && child->tag != "br") return false;
    }
    return true;
}

std::string PlainText(const std::string& html) {
    return Trim(HTML::ExtractPlainText(html));
}

// Paragraph text with the breaks kept as newlines.
std::string ParagraphText(const HTML::Node* paragraph) {
    std::string out;
    for (const auto& child : paragraph->children) {
        if (child->type == HTML::NodeType::Text) out += child->text;
        else if (child->type == HTML::NodeType::Element && child->tag == "br") out += '\n';
    }
    return Trim(out);
}

struct Converter {
    const Site& site;
    ImportReport& report;
    std::vector<Block> out;
    std::string pendingText;   // consecutive plain paragraphs, merged into one text block

    void FlushText() {
        if (pendingText.empty()) return;
        Block text{"text"};
        text.Set("text", pendingText);
        out.push_back(text);
        pendingText.clear();
    }
    void RichText(const std::string& html, const std::string& why) {
        FlushText();
        const std::string markup = StripMarkers(html);
        if (markup.empty()) return;
        Block block{"richtext"};
        block.Set("html", markup);
        out.push_back(block);
        ++report.blocksAsRichText;
        if (!why.empty()) report.warnings.push_back(why);
    }
    JSONValue LinkFor(const std::string& href) const {
        JSONValue link = JSONValue::MakeObject();
        if (!site.url.empty() && href.rfind(site.url + "/", 0) == 0) {
            std::string path = href.substr(site.url.size());
            while (!path.empty() && path.back() == '/') path.pop_back();
            if (path.empty()) return link.Set("page", "home");
            const size_t slash = path.rfind('/');
            const std::string slug = path.substr(slash + 1);
            if (site.FindPage(slug)) return link.Set("page", slug);
        }
        return link.Set("url", href);
    }
    void Convert(const WpBlock& wp) {
        HTML::Parser parser;
        const std::string& name = wp.name;
        if (name == "paragraph") {
            HTML::Document doc = parser.Parse(wp.inner);
            const HTML::Node* p = FindTag(doc.root.get(), "p");
            if (p && IsPlainParagraph(wp.inner, p)) {
                const std::string text = ParagraphText(p);
                if (!text.empty()) {
                    if (!pendingText.empty()) pendingText += "\n\n";
                    pendingText += text;
                }
                ++report.blocksMapped;
                return;
            }
            RichText(wp.inner, "");
            return;
        }
        FlushText();
        if (name == "heading") {
            Block block{"heading"};
            block.Set("text", PlainText(wp.inner));
            int level = static_cast<int>(wp.attrs["level"].GetNumber(2));
            if (level < 2) level = 2;
            if (level > 4) level = 4;
            block.Set("level", level);
            out.push_back(block);
            ++report.blocksMapped;
        } else if (name == "image" || (name == "cover" && !wp.attrs["url"].GetString().empty() && wp.children.empty())) {
            HTML::Document doc = parser.Parse(wp.inner);
            const HTML::Node* img = FindTag(doc.root.get(), "img");
            Block block{"picture"};
            std::string src = Attribute(img, "src");
            if (src.empty()) src = wp.attrs["url"].GetString();
            block.Set("src", src);
            block.Set("alt", Attribute(img, "alt"));
            if (const HTML::Node* caption = FindTag(doc.root.get(), "figcaption")) block.Set("caption", PlainText(HTML::ExtractPlainText(*caption)));
            out.push_back(block);
            ++report.blocksMapped;
        } else if (name == "cover" || name == "media-text") {
            Block block{"hero"};
            std::string title, text;
            for (const WpBlock& child : wp.children) {
                if (child.name == "heading" && title.empty()) title = PlainText(child.inner);
                else if (child.name == "paragraph") text += (text.empty() ? "" : "\n\n") + PlainText(child.inner);
            }
            if (title.empty()) title = PlainText(wp.inner);
            block.Set("title", title);
            if (!text.empty()) block.Set("text", text);
            std::string picture = wp.attrs["url"].GetString();
            if (picture.empty()) {
                HTML::Document doc = parser.Parse(wp.inner);
                picture = Attribute(FindTag(doc.root.get(), "img"), "src");
            }
            if (!picture.empty()) block.Set("picture", picture);
            out.push_back(block);
            ++report.blocksMapped;
        } else if (name == "quote" || name == "pullquote") {
            HTML::Document doc = parser.Parse(wp.inner);
            Block block{"quote"};
            std::string text;
            if (const HTML::Node* quote = FindTag(doc.root.get(), "blockquote")) {
                for (const auto& child : quote->children) {
                    if (child->type == HTML::NodeType::Element && child->tag == "p") {
                        text += (text.empty() ? "" : "\n\n") + ParagraphText(child.get());
                    }
                }
                if (const HTML::Node* cite = FindTag(quote, "cite")) block.Set("cite", Trim(HTML::ExtractPlainText(*cite)));
            }
            if (text.empty()) text = PlainText(wp.inner);
            block.Set("text", text);
            out.push_back(block);
            ++report.blocksMapped;
        } else if (name == "buttons") {
            Block block{"buttons"};
            JSONValue items = JSONValue::MakeArray();
            for (const WpBlock& child : wp.children) {
                if (child.name != "button") continue;
                HTML::Document doc = parser.Parse(child.inner);
                const HTML::Node* a = FindTag(doc.root.get(), "a");
                const std::string href = Attribute(a, "href");
                const std::string title = a ? Trim(HTML::ExtractPlainText(*a)) : PlainText(child.inner);
                if (href.empty() || title.empty()) continue;
                JSONValue item = LinkFor(href);
                item.Set("title", title);
                if (items.GetSize() == 0) item.Set("primary", true);
                items.Append(item);
            }
            block.Set("items", items);
            out.push_back(block);
            ++report.blocksMapped;
        } else if (name == "columns") {
            Block block{"columns"};
            for (const WpBlock& column : wp.children) {
                if (column.name != "column") continue;
                if (column.children.size() == 1) {
                    Converter inner{site, report};
                    inner.Convert(column.children[0]);
                    inner.FlushText();
                    if (inner.out.size() == 1) {
                        block.children.push_back(inner.out[0]);
                        continue;
                    }
                }
                Block cell{"richtext"};
                cell.Set("html", StripMarkers(column.inner));
                block.children.push_back(cell);
                ++report.blocksAsRichText;
            }
            out.push_back(block);
            ++report.blocksMapped;
        } else if (name == "gallery") {
            Block block{"columns"};
            for (const WpBlock& child : wp.children) {
                if (child.name != "image") continue;
                Converter inner{site, report};
                inner.Convert(child);
                for (Block& b : inner.out) block.children.push_back(b);
            }
            if (block.children.empty()) {
                HTML::Document doc = parser.Parse(wp.inner);
                std::function<void(const HTML::Node*)> each = [&](const HTML::Node* node) {
                    if (!node) return;
                    if (node->type == HTML::NodeType::Element && node->tag == "img") {
                        Block picture{"picture"};
                        picture.Set("src", Attribute(node, "src"));
                        picture.Set("alt", Attribute(node, "alt"));
                        block.children.push_back(picture);
                    }
                    for (const auto& child : node->children) each(child.get());
                };
                each(doc.root.get());
            }
            out.push_back(block);
            ++report.blocksMapped;
        } else if (name == "separator" || name == "spacer") {
            Block block{"spacer"};
            const std::string height = wp.attrs["height"].GetString();
            const long value = height.empty() ? 32 : std::strtol(height.c_str(), nullptr, 10);   // locale-ok: "100px", digits
            block.Set("height", static_cast<int64_t>(value > 0 ? value : 32));
            out.push_back(block);
            ++report.blocksMapped;
        } else if (name == "html" || name == "shortcode" || name == "embed") {
            Block block{"html"};
            block.Set("html", StripMarkers(wp.inner));
            out.push_back(block);
            ++report.blocksMapped;
        } else if (name == "list" || name == "table" || name == "code" || name == "preformatted" || name == "verse" ||
                   name == "details" || name == "group" || name == "freeform") {
            RichText(wp.inner, "");
        } else {
            RichText(wp.inner, "block kept as rich text: wp:" + name);
        }
    }
};

std::string StripShortcodes(const std::string& html, int& count) {
    static const std::regex caption(R"(\[/?caption[^\]]*\])");
    static const std::regex any(R"(\[/?[A-Za-z_][A-Za-z0-9_-]*[^\]]*\])");
    std::string out = std::regex_replace(html, caption, "");
    count = static_cast<int>(std::distance(std::sregex_iterator(out.begin(), out.end(), any), std::sregex_iterator()));
    return out;
}

// ----- The export ---------------------------------------------------------------------

std::string MapStatus(const std::string& wp) {
    if (wp == "publish") return "published";
    if (wp == "future") return "scheduled";
    return "draft";   // draft, pending, private, trash, auto-draft
}

std::string LanguageCode(const std::string& language) {
    const size_t dash = language.find_first_of("-_");
    std::string code = dash == std::string::npos ? language : language.substr(0, dash);
    for (auto& c : code) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return code.empty() ? std::string("en") : code;
}

// https://example.org/wp-content/uploads/2026/10/My Picture.PNG → media/my-picture.png:
// the library's convention (UltraPressMedia.h, CleanMediaName), flat, so an
// imported site and one built in the editor read alike; a second file of
// the same name from another month gets "-2".
std::string LocalMediaPath(const std::string& url, std::set<std::string>& taken) {
    std::string tail = url.substr(url.rfind('/') + 1);
    const size_t query = tail.find('?');
    if (query != std::string::npos) tail = tail.substr(0, query);
    const std::string name = CleanMediaName(tail, [&taken](const std::string& candidate) { return taken.count(candidate) > 0; });
    taken.insert(name);
    return "media/" + name;
}

std::string ReplaceAll(std::string text, const std::string& from, const std::string& to) {
    if (from.empty()) return text;
    size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
        pos += to.size();
    }
    return text;
}

} // namespace

std::vector<Block> BlocksFromWordPressContent(const std::string& html, const Site& site, ImportReport& report) {
    Converter converter{site, report};
    if (html.find("<!-- wp:") == std::string::npos) {
        int shortcodes = 0;
        const std::string cleaned = Trim(StripShortcodes(html, shortcodes));
        if (shortcodes) report.warnings.push_back(std::to_string(shortcodes) + " shortcode(s) left as text in classic content");
        if (!cleaned.empty()) {
            Block block{"richtext"};
            block.Set("html", cleaned);
            converter.out.push_back(block);
            ++report.blocksAsRichText;
        }
        return converter.out;
    }
    size_t stopped = 0;
    std::string freeMarkup;
    const std::vector<WpBlock> blocks = ParseBlocks(html, 0, "", stopped, freeMarkup);
    for (const WpBlock& block : blocks) converter.Convert(block);
    converter.FlushText();
    if (!Trim(freeMarkup).empty() && Trim(freeMarkup) != "") {
        const std::string markup = Trim(freeMarkup);
        if (markup.find('<') != std::string::npos) {
            Block block{"richtext"};
            block.Set("html", markup);
            converter.out.push_back(block);
            ++report.blocksAsRichText;
        }
    }
    return converter.out;
}

bool ImportWordPressFile(const std::string& path, const ImportOptions& options, Site& site, ImportReport& report) {
    std::string xml;
    if (!ReadTextFile(path, xml)) {
        report.warnings.push_back("cannot read " + path);
        return false;
    }
    return ImportWordPress(xml, options, site, report);
}

bool ImportWordPress(const std::string& wxrXml, const ImportOptions& options, Site& site, ImportReport& report) {
    tinyxml2::XMLDocument doc;
    if (doc.Parse(wxrXml.c_str(), wxrXml.size()) != tinyxml2::XML_SUCCESS) {
        report.warnings.push_back(std::string("the export does not parse as XML: ") + (doc.ErrorStr() ? doc.ErrorStr() : ""));
        return false;
    }
    const tinyxml2::XMLElement* rss = doc.FirstChildElement("rss");
    const tinyxml2::XMLElement* channel = rss ? rss->FirstChildElement("channel") : nullptr;
    if (!channel) {
        report.warnings.push_back("not a WordPress export: no <rss><channel>");
        return false;
    }
    if (!channel->FirstChildElement("wp:wxr_version") && !channel->FirstChildElement("item")) {
        report.warnings.push_back("not a WordPress export: no wp:wxr_version and no items");
        return false;
    }

    site = Site();
    site.name = Trim(ChildText(channel, "title"));
    site.tagline = Trim(ChildText(channel, "description"));
    site.url = Trim(ChildText(channel, "link"));
    while (!site.url.empty() && site.url.back() == '/') site.url.pop_back();
    site.language = LanguageCode(Trim(ChildText(channel, "language")));
    site.languages = {site.language};
    if (const tinyxml2::XMLElement* author = channel->FirstChildElement("wp:author")) {
        site.owner = Trim(ChildText(author, "wp:author_display_name"));
    }
    if (site.owner.empty()) site.owner = site.name;
    site.theme = CreateStarterSite(site.name, site.url, site.language).theme;

    // Menus by their term slug, with their display names.
    std::map<std::string, std::string> menuNames;   // slug → name
    for (const tinyxml2::XMLElement* term = channel->FirstChildElement("wp:term"); term; term = term->NextSiblingElement("wp:term")) {
        if (ChildText(term, "wp:term_taxonomy") == "nav_menu") {
            menuNames[Trim(ChildText(term, "wp:term_slug"))] = Trim(ChildText(term, "wp:term_name"));
        }
    }

    struct Item {
        std::string id, type, status, title, link, slug, content, excerpt, date, parent, menuOrder;
        std::string attachmentUrl;
        std::map<std::string, std::string> meta;
        std::string menuSlug;
    };
    std::vector<Item> items;
    for (const tinyxml2::XMLElement* el = channel->FirstChildElement("item"); el; el = el->NextSiblingElement("item")) {
        Item item;
        item.id = Trim(ChildText(el, "wp:post_id"));
        item.type = Trim(ChildText(el, "wp:post_type"));
        item.status = Trim(ChildText(el, "wp:status"));
        item.title = Trim(ChildText(el, "title"));
        item.link = Trim(ChildText(el, "link"));
        item.slug = Trim(ChildText(el, "wp:post_name"));
        item.content = ChildText(el, "content:encoded");
        item.excerpt = ChildText(el, "excerpt:encoded");
        item.date = Trim(ChildText(el, "wp:post_date")).substr(0, 10);
        item.parent = Trim(ChildText(el, "wp:post_parent"));
        item.menuOrder = Trim(ChildText(el, "wp:menu_order"));
        item.attachmentUrl = Trim(ChildText(el, "wp:attachment_url"));
        for (const tinyxml2::XMLElement* meta = el->FirstChildElement("wp:postmeta"); meta; meta = meta->NextSiblingElement("wp:postmeta")) {
            item.meta[Trim(ChildText(meta, "wp:meta_key"))] = Trim(ChildText(meta, "wp:meta_value"));
        }
        for (const tinyxml2::XMLElement* category = el->FirstChildElement("category"); category; category = category->NextSiblingElement("category")) {
            const char* domain = category->Attribute("domain");
            const char* nicename = category->Attribute("nicename");
            if (domain && nicename && std::string(domain) == "nav_menu") item.menuSlug = nicename;
        }
        items.push_back(std::move(item));
    }

    // Attachments: their URLs become media paths.
    std::vector<std::pair<std::string, std::string>> mediaMap;
    std::set<std::string> mediaNames;
    for (const Item& item : items) {
        if (item.type != "attachment" || item.attachmentUrl.empty()) continue;
        const std::string local = LocalMediaPath(item.attachmentUrl, mediaNames);
        mediaMap.emplace_back(item.attachmentUrl, local);
        report.media.emplace_back(item.attachmentUrl, local);
        ++report.attachments;
    }
    auto rewriteMedia = [&](std::string html) {
        for (const auto& [url, local] : mediaMap) {
            // The file and its sized variants (picture-300x200.png).
            const size_t dot = url.rfind('.');
            const size_t slash = url.rfind('/');
            if (dot != std::string::npos && slash != std::string::npos && dot > slash) {
                const std::string stem = url.substr(0, dot), ext = url.substr(dot);
                const std::regex sized(std::regex_replace(stem, std::regex(R"([.^$|()\[\]{}*+?\\])"), R"(\$&)") + R"(-\d+x\d+)" +
                                       std::regex_replace(ext, std::regex(R"([.^$|()\[\]{}*+?\\])"), R"(\$&)"));
                html = std::regex_replace(html, sized, "/" + local);
            }
            html = ReplaceAll(html, url, "/" + local);
        }
        return html;
    };

    // Pages and posts. Slugs first, so links between them resolve.
    std::map<std::string, std::string> slugById;
    std::set<std::string> slugs;
    for (Item& item : items) {
        if (item.type != "page" && item.type != "post") continue;
        if (item.slug.empty()) item.slug = MakeSlug(item.title);
        std::string slug = item.slug;
        int n = 2;
        while (slugs.count(slug)) slug = item.slug + "-" + std::to_string(n++);
        item.slug = slug;
        slugs.insert(slug);
        slugById[item.id] = slug;
    }
    std::string frontSlug = options.frontPage;
    if (frontSlug.empty()) {
        for (const Item& item : items) {
            if (item.type == "page" && (item.slug == "home" || item.slug == "front-page" || item.slug == "homepage")) { frontSlug = item.slug; break; }
        }
    }
    if (frontSlug.empty()) {
        for (const Item& item : items) {
            if (item.type == "page" && item.status == "publish") { frontSlug = item.slug; break; }
        }
        if (!frontSlug.empty()) report.warnings.push_back("no page is named home: '" + frontSlug + "' is the front page (--front <slug> chooses another)");
    }
    for (Item& item : items) {
        if (item.type != "page" && item.type != "post") continue;
        if (item.status == "trash" || item.status == "auto-draft" || item.status == "inherit") continue;
        const std::string status = MapStatus(item.status);
        if (status == "draft" && !options.includeDrafts) continue;
        Page page;
        page.slug = item.slug == frontSlug ? "home" : item.slug;
        if (item.slug == frontSlug) {
            slugById[item.id] = "home";
            page.pinned = true;
        }
        page.title = item.title.empty() ? item.slug : item.title;
        page.kind = item.type;
        page.status = status;
        page.date = item.date;
        page.seo.description = PlainText(item.excerpt);
        page.language = "";
        if (status == "draft") ++report.drafts;
        if (item.type == "page") ++report.pages; else ++report.posts;
        site.pages.push_back(std::move(page));
    }
    // Bodies, now that every slug is known for links between pages.
    for (Item& item : items) {
        if (item.type != "page" && item.type != "post") continue;
        auto it = slugById.find(item.id);
        if (it == slugById.end()) continue;
        Page* page = nullptr;
        for (Page& candidate : site.pages) {
            if (candidate.slug == it->second) { page = &candidate; break; }
        }
        if (!page) continue;
        page->blocks = BlocksFromWordPressContent(rewriteMedia(item.content), site, report);
        for (Block& block : page->blocks) {
            if (block.type == "picture" || block.type == "hero") {
                const std::string key = block.type == "picture" ? "src" : "picture";
                std::string path = block.Text(key);
                if (!path.empty() && path[0] == '/' && path.rfind("/media/", 0) == 0) block.Set(key, path.substr(1));
            }
        }
        // Where the WordPress address differs from the site's, a redirect.
        const std::string newPath = PagePath(*page);
        if (!item.link.empty() && !site.url.empty() && item.link.rfind(site.url, 0) == 0) {
            std::string oldPath = item.link.substr(site.url.size());
            if (oldPath.empty()) oldPath = "/";
            if (oldPath != newPath) report.redirects.emplace_back(oldPath, newPath);
        }
    }

    // Menus: items ordered, nested by their parent.
    struct MenuEntry { std::string id, parent; int order; MenuItem item; };
    std::map<std::string, std::vector<MenuEntry>> menus;
    for (const Item& item : items) {
        if (item.type != "nav_menu_item") continue;
        MenuEntry entry;
        entry.id = item.id;
        auto meta = [&](const char* key) { auto f = item.meta.find(key); return f == item.meta.end() ? std::string() : f->second; };
        entry.parent = meta("_menu_item_menu_item_parent");
        entry.order = static_cast<int>(std::strtol(item.menuOrder.c_str(), nullptr, 10));   // locale-ok: digits
        entry.item.title = item.title;
        const std::string type = meta("_menu_item_type");
        if (type == "post_type") {
            auto target = slugById.find(meta("_menu_item_object_id"));
            if (target != slugById.end()) entry.item.page = target->second;
            else { entry.item.url = meta("_menu_item_url"); if (entry.item.url.empty()) report.warnings.push_back("menu item '" + item.title + "' names a post that was not imported"); }
        } else {
            entry.item.url = meta("_menu_item_url");
        }
        if (entry.item.title.empty()) {
            if (const Page* target = site.FindPage(entry.item.page)) entry.item.title = target->title;
        }
        const std::string menuSlug = item.menuSlug.empty() ? "menu" : item.menuSlug;
        menus[menuSlug].push_back(std::move(entry));
    }
    bool mainTaken = false;
    for (auto& [slug, entries] : menus) {
        std::stable_sort(entries.begin(), entries.end(), [](const MenuEntry& a, const MenuEntry& b) { return a.order < b.order; });
        std::function<std::vector<MenuItem>(const std::string&)> children = [&](const std::string& parent) {
            std::vector<MenuItem> result;
            for (const MenuEntry& entry : entries) {
                if ((entry.parent.empty() ? std::string("0") : entry.parent) != (parent.empty() ? std::string("0") : parent)) continue;
                MenuItem item = entry.item;
                item.children = children(entry.id);
                if (item.page.empty() && item.url.empty() && item.children.empty()) continue;
                result.push_back(std::move(item));
            }
            return result;
        };
        Menu menu;
        const std::string lower = [&]() { std::string s = slug; for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }();
        if (lower.find("footer") != std::string::npos) menu.name = "footer";
        else if (!mainTaken) { menu.name = "main"; mainTaken = true; }
        else menu.name = slug;
        menu.items = children("0");
        if (menu.items.empty()) continue;
        site.menus.push_back(std::move(menu));
        ++report.menus;
        (void)menuNames;
    }
    if (site.menus.empty() && !site.pages.empty()) {
        // No menu in the export: one of the published pages, the front page first.
        Menu menu{"main", {}};
        for (const Page* page : SortedPages(site, PageOrder::Stored)) {
            if (page->kind == "page" && page->IsPublished()) menu.items.push_back({page->title, page->slug, "", {}});
        }
        if (!menu.items.empty()) { site.menus.push_back(menu); report.warnings.push_back("no menu in the export: a main menu of the published pages was made"); }
    }

    // The media, when a fetcher was given.
    if (options.fetchMedia) {
        for (const auto& [url, local] : mediaMap) {
            if (options.fetchMedia(url, local)) report.fetched.emplace_back(url, local);
            else report.warnings.push_back("could not fetch " + url);
        }
    }
    return true;
}

} // namespace UltraPress
