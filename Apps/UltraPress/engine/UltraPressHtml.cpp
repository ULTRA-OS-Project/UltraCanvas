// Apps/UltraPress/engine/UltraPressHtml.cpp
// The HTML shadow, sitemap, robots and feed. Text goes through
// HTML::EscapeText everywhere; the two blocks that carry HTML (richtext,
// html) are the owner's own and pass through.
// Version: 0.7.0 - paragraphs split by the model (SplitParagraphs)
// Version: 0.1.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressHtml.h"

#include "HTMLReader/HTMLDocument.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <sstream>

using UltraCanvas::JSONValue;
using UltraCanvas::HTML::EscapeText;

namespace UltraPress {

namespace {

const char* kStyle =
    "body{margin:0;font-family:system-ui,sans-serif;line-height:1.5;color:#202020;background:#fff}"
    "main,header,footer{max-width:48rem;margin:0 auto;padding:1rem}"
    "header nav ul,footer nav ul{list-style:none;padding:0;margin:0;display:flex;flex-wrap:wrap;gap:.75rem}"
    "header nav ul ul{display:block;margin-left:1rem}"
    ".kicker{text-transform:uppercase;letter-spacing:.08em;font-size:.8rem;color:#a90707;margin:0}"
    ".actions{display:flex;flex-wrap:wrap;gap:.75rem}"
    ".actions a{display:inline-block;padding:.5rem 1rem;border:1.5px solid #a90707;border-radius:6px;color:#a90707;text-decoration:none}"
    ".actions a.primary{background:#a90707;color:#fff}"
    ".columns{display:flex;flex-wrap:wrap;gap:1rem}.columns>*{flex:1 1 14rem}"
    "figure{margin:1rem 0}figure img{max-width:100%;height:auto}"
    "blockquote{border-left:3px solid #efefef;margin:1rem 0;padding-left:1rem}"
    "details{margin:.5rem 0}summary{cursor:pointer;font-weight:bold}"
    "form label{display:block;margin:.5rem 0}form input,form textarea{display:block;width:100%;max-width:28rem;padding:.4rem}";

std::string Attr(const std::string& value) { return EscapeText(value); }

std::string JsonString(const std::string& text) { return UltraCanvas::JSON::EscapeString(text); }

bool IsAbsoluteUrl(const std::string& url) {
    return url.find("://") != std::string::npos || url.rfind("mailto:", 0) == 0 || url.rfind("tel:", 0) == 0;
}

} // namespace

std::string Paragraphs(const std::string& longText) {
    std::string out;
    for (const std::string& text : SplitParagraphs(longText)) {
        std::string escaped = EscapeText(text);
        std::string withBreaks;
        for (char c : escaped) {
            if (c == '\r') continue;
            if (c == '\n') withBreaks += "<br>";
            else withBreaks += c;
        }
        out += "<p>" + withBreaks + "</p>\n";
    }
    return out;
}

std::string Rfc822Date(const std::string& isoDate) {
    int y = 0, m = 0, d = 0;
    if (std::sscanf(isoDate.c_str(), "%4d-%2d-%2d", &y, &m, &d) != 3) return std::string();   // locale-ok: digits only
    if (m < 1 || m > 12 || d < 1 || d > 31) return std::string();
    using namespace std::chrono;
    const year_month_day ymd{year{y}, month{static_cast<unsigned>(m)}, day{static_cast<unsigned>(d)}};
    if (!ymd.ok()) return std::string();
    const weekday wd{sys_days{ymd}};
    static const char* kDays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char buffer[48];
    std::snprintf(buffer, sizeof buffer, "%s, %02d %s %04d 00:00:00 +0000",   // locale-ok: integers only
                  kDays[wd.c_encoding()], d, kMonths[m - 1], y);
    return buffer;
}

// ----- The emitter -----------------------------------------------------------------

struct HtmlEmitter::PageContext {
    std::string body;
    std::vector<std::pair<std::string, std::string>> faq;   // for the FAQPage structured data
    const UltraPress::Page* page = nullptr;
};

HtmlEmitter::HtmlEmitter(const Site& siteRef, HtmlOptions emitterOptions, const BlockRegistry& blockRegistry)
    : site(siteRef), options(std::move(emitterOptions)), registry(blockRegistry) {}

void HtmlEmitter::EmitMenu(const Menu* menu, const std::string& label, std::string& out) const {
    if (!menu || menu->items.empty()) return;
    out += "<nav aria-label=\"" + Attr(label) + "\">\n";
    std::function<void(const std::vector<MenuItem>&)> list = [&](const std::vector<MenuItem>& items) {
        out += "<ul>\n";
        for (const MenuItem& item : items) {
            std::string href = item.url;
            if (href.empty() && !item.page.empty()) {
                if (const UltraPress::Page* target = site.FindPage(item.page)) href = PagePath(*target);
            }
            out += "<li>";
            if (href.empty()) out += "<span>" + EscapeText(item.title) + "</span>";
            else out += "<a href=\"" + Attr(href) + "\">" + EscapeText(item.title) + "</a>";
            if (!item.children.empty()) list(item.children);
            out += "</li>\n";
        }
        out += "</ul>\n";
    };
    list(menu->items);
    out += "</nav>\n";
}

static void EmitLinks(const Site& site, const JSONValue& links, std::string& out) {
    if (!links.IsArray() || links.GetSize() == 0) return;
    out += "<p class=\"actions\">";
    for (const JSONValue& link : links.GetElements()) {
        const std::string href = LinkHref(site, link);
        const std::string title = link["title"].GetString();
        if (href.empty() || title.empty()) continue;
        out += "<a href=\"" + Attr(href) + "\"" + (link["primary"].GetBoolean(false) ? " class=\"primary\"" : "") +
               ">" + EscapeText(title) + "</a> ";
    }
    out += "</p>\n";
}

static std::string MediaSrc(const std::string& path) {
    // A media path is relative to the site folder and published as it is;
    // an absolute URL stays.
    if (IsAbsoluteUrl(path) || path.empty() || path[0] == '/') return path;
    return "/" + path;
}

void HtmlEmitter::EmitBlock(const UltraPress::Block& block, PageContext& context) const {
    std::string& out = context.body;
    const std::string& type = block.type;
    if (!registry.Find(type)) {
        out += "<!-- unknown block type: " + EscapeText(type) + " -->\n";
        return;
    }
    if (type == "heading") {
        int level = static_cast<int>(block.Number("level", 2));
        if (level < 2 || level > 4) level = 2;
        const std::string tag = "h" + std::to_string(level);
        out += "<" + tag + ">" + EscapeText(block.Text("text")) + "</" + tag + ">\n";
    } else if (type == "text") {
        out += "<section>\n";
        if (!block.Text("heading").empty()) out += "<h2>" + EscapeText(block.Text("heading")) + "</h2>\n";
        out += Paragraphs(block.Text("text"));
        out += "</section>\n";
    } else if (type == "richtext" || type == "html") {
        out += "<section>\n" + block.Text("html") + "\n</section>\n";
    } else if (type == "hero") {
        out += "<section class=\"hero\">\n";
        if (!block.Text("kicker").empty()) out += "<p class=\"kicker\">" + EscapeText(block.Text("kicker")) + "</p>\n";
        out += "<h1>" + EscapeText(block.Text("title")) + "</h1>\n";
        out += Paragraphs(block.Text("text"));
        if (!block.Text("picture").empty()) {
            out += "<figure><img src=\"" + Attr(MediaSrc(block.Text("picture"))) + "\" alt=\"\"></figure>\n";
        }
        EmitLinks(site, block.Items("buttons"), out);
        out += "</section>\n";
    } else if (type == "picture") {
        out += "<figure><img src=\"" + Attr(MediaSrc(block.Text("src"))) + "\" alt=\"" + Attr(block.Text("alt")) + "\">";
        if (!block.Text("caption").empty()) out += "<figcaption>" + EscapeText(block.Text("caption")) + "</figcaption>";
        out += "</figure>\n";
    } else if (type == "columns") {
        out += "<section>\n";
        if (!block.Text("heading").empty()) out += "<h2>" + EscapeText(block.Text("heading")) + "</h2>\n";
        out += "<div class=\"columns\">\n";
        for (const UltraPress::Block& child : block.children) {
            out += "<div>\n";
            EmitBlock(child, context);
            out += "</div>\n";
        }
        out += "</div>\n</section>\n";
    } else if (type == "card") {
        out += "<article class=\"card\">\n";
        if (!block.Text("picture").empty()) {
            out += "<figure><img src=\"" + Attr(MediaSrc(block.Text("picture"))) + "\" alt=\"\"></figure>\n";
        }
        out += "<h3>" + EscapeText(block.Text("title")) + "</h3>\n";
        out += Paragraphs(block.Text("text"));
        const std::string href = LinkHref(site, block.props["link"]);
        if (!href.empty()) {
            out += "<p><a href=\"" + Attr(href) + "\">" + EscapeText(block.Text("linkTitle", "Read more")) + "</a></p>\n";
        }
        out += "</article>\n";
    } else if (type == "features") {
        out += "<section>\n";
        if (!block.Text("heading").empty()) out += "<h2>" + EscapeText(block.Text("heading")) + "</h2>\n";
        out += "<ul class=\"features\">\n";
        for (const JSONValue& item : block.Items("items").GetElements()) {
            out += "<li>" + EscapeText(item.GetString()) + "</li>\n";
        }
        out += "</ul>\n</section>\n";
    } else if (type == "cta") {
        out += "<section class=\"cta\">\n<h2>" + EscapeText(block.Text("title")) + "</h2>\n";
        out += Paragraphs(block.Text("text"));
        EmitLinks(site, block.Items("buttons"), out);
        out += "</section>\n";
    } else if (type == "faq") {
        out += "<section class=\"faq\">\n";
        if (!block.Text("heading").empty()) out += "<h2>" + EscapeText(block.Text("heading")) + "</h2>\n";
        for (const JSONValue& item : block.Items("items").GetElements()) {
            const std::string q = item["q"].GetString();
            const std::string a = item["a"].GetString();
            if (q.empty()) continue;
            out += "<details><summary>" + EscapeText(q) + "</summary>\n" + Paragraphs(a) + "</details>\n";
            context.faq.emplace_back(q, a);
        }
        out += "</section>\n";
    } else if (type == "quote") {
        out += "<blockquote>\n" + Paragraphs(block.Text("text"));
        if (!block.Text("cite").empty()) out += "<cite>" + EscapeText(block.Text("cite")) + "</cite>\n";
        out += "</blockquote>\n";
    } else if (type == "buttons") {
        EmitLinks(site, block.Items("items"), out);
    } else if (type == "form") {
        const std::string id = block.Text("id");
        out += "<form method=\"post\" action=\"/api/form/" + Attr(id) + "\" id=\"form-" + Attr(id) + "\">\n";
        if (!block.Text("heading").empty()) out += "<h2>" + EscapeText(block.Text("heading")) + "</h2>\n";
        out += Paragraphs(block.Text("text"));
        for (const JSONValue& field : block.Items("fields").GetElements()) {
            const std::string name = field["name"].GetString();
            const std::string label = field["label"].GetString(name);
            const std::string fieldType = field["type"].GetString("text");
            const std::string required = field["required"].GetBoolean(false) ? " required" : "";
            if (name.empty()) continue;
            out += "<label>" + EscapeText(label);
            if (fieldType == "textarea") {
                out += "<textarea name=\"" + Attr(name) + "\"" + required + "></textarea>";
            } else if (fieldType == "checkbox") {
                out += "<input type=\"checkbox\" name=\"" + Attr(name) + "\"" + required + ">";
            } else {
                out += "<input type=\"" + Attr(fieldType) + "\" name=\"" + Attr(name) + "\"" + required + ">";
            }
            out += "</label>\n";
        }
        out += "<p class=\"actions\"><button type=\"submit\">" + EscapeText(block.Text("submit", "Send")) + "</button></p>\n</form>\n";
    } else if (type == "newsletter") {
        out += "<form method=\"post\" action=\"/api/newsletter/subscribe\" class=\"newsletter\">\n";
        if (!block.Text("heading").empty()) out += "<h2>" + EscapeText(block.Text("heading")) + "</h2>\n";
        out += Paragraphs(block.Text("text"));
        out += "<input type=\"hidden\" name=\"list\" value=\"" + Attr(block.Text("list", "default")) + "\">\n";
        out += "<label>E-mail<input type=\"email\" name=\"email\" required></label>\n";
        out += "<p class=\"actions\"><button type=\"submit\">Subscribe</button></p>\n</form>\n";
    } else if (type == "downloads") {
        out += "<section class=\"downloads\">\n";
        if (!block.Text("heading").empty()) out += "<h2>" + EscapeText(block.Text("heading")) + "</h2>\n";
        out += "<ul>\n";
        for (const JSONValue& item : block.Items("items").GetElements()) {
            const std::string file = item["file"].GetString();
            const std::string title = item["title"].GetString(file);
            if (file.empty()) continue;
            out += "<li><a href=\"" + Attr(MediaSrc(file)) + "\" download>" + EscapeText(title) + "</a>";
            std::string details;
            if (!item["version"].GetString().empty()) details += "version " + item["version"].GetString();
            if (!item["size"].GetString().empty()) details += (details.empty() ? "" : ", ") + item["size"].GetString();
            if (!details.empty()) out += " <small>(" + EscapeText(details) + ")</small>";
            if (!item["description"].GetString().empty()) out += "<br>" + EscapeText(item["description"].GetString());
            out += "</li>\n";
        }
        out += "</ul>\n</section>\n";
    } else if (type == "spacer") {
        // nothing in a document
    }
}

void HtmlEmitter::EmitBlocks(const std::vector<UltraPress::Block>& blocks, PageContext& context) const {
    for (const UltraPress::Block& block : blocks) EmitBlock(block, context);
}

std::string HtmlEmitter::Block(const UltraPress::Block& block) const {
    PageContext context;
    EmitBlock(block, context);
    return context.body;
}

std::string HtmlEmitter::Head(const UltraPress::Page& page, const PageContext& context) const {
    std::string out;
    const std::string language = page.language.empty() ? site.language : page.language;
    const std::string url = PageUrl(site, page);
    const std::string fullTitle = page.slug == "home" || page.title == site.name
                                      ? site.name + (site.tagline.empty() ? "" : " - " + site.tagline)
                                      : page.title + " - " + site.name;
    const std::string description = page.seo.description.empty() ? site.tagline : page.seo.description;
    out += "<!doctype html>\n<html lang=\"" + Attr(language) + "\">\n<head>\n<meta charset=\"utf-8\">\n";
    out += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
    out += "<title>" + EscapeText(fullTitle) + "</title>\n";
    if (!description.empty()) out += "<meta name=\"description\" content=\"" + Attr(description) + "\">\n";
    if (!options.generator.empty()) out += "<meta name=\"generator\" content=\"" + Attr(options.generator) + "\">\n";
    if (page.seo.noindex) out += "<meta name=\"robots\" content=\"noindex\">\n";
    out += "<link rel=\"canonical\" href=\"" + Attr(url) + "\">\n";
    out += "<meta property=\"og:type\" content=\"" + std::string(page.kind == "post" ? "article" : "website") + "\">\n";
    out += "<meta property=\"og:title\" content=\"" + Attr(page.title) + "\">\n";
    if (!description.empty()) out += "<meta property=\"og:description\" content=\"" + Attr(description) + "\">\n";
    out += "<meta property=\"og:url\" content=\"" + Attr(url) + "\">\n";
    out += "<meta property=\"og:site_name\" content=\"" + Attr(site.name) + "\">\n";
    if (!page.seo.image.empty()) {
        const std::string image = IsAbsoluteUrl(page.seo.image) ? page.seo.image : site.url + MediaSrc(page.seo.image);
        out += "<meta property=\"og:image\" content=\"" + Attr(image) + "\">\n";
        out += "<meta name=\"twitter:card\" content=\"summary_large_image\">\n";
    } else {
        out += "<meta name=\"twitter:card\" content=\"summary\">\n";
    }
    for (const std::string& other : site.languages) {
        // One site, one language per page in Phase 1: the alternates are
        // the same page in the site's other languages once pages carry a
        // translation; until then only the page's own language is named.
        (void)other;
    }
    if (!site.languages.empty()) out += "<link rel=\"alternate\" hreflang=\"" + Attr(language) + "\" href=\"" + Attr(url) + "\">\n";
    out += "<link rel=\"alternate\" type=\"application/rss+xml\" title=\"" + Attr(site.name) + "\" href=\"/feed.xml\">\n";
    if (!options.ultrawebApp.empty()) out += "<link rel=\"ultraweb-app\" href=\"" + Attr(options.ultrawebApp) + "\">\n";
    if (!options.rendererScript.empty()) {
        out += "<meta name=\"ultrapress-page\" content=\"" + Attr(page.slug) + "\">\n";
        out += "<script src=\"" + Attr(options.rendererScript) + "\" async></script>\n";
    }
    if (options.includeStyle) out += "<style>" + std::string(kStyle) + "</style>\n";
    // Structured data: the site on the front page, an article for a post,
    // the FAQ wherever one is.
    if (page.slug == "home") {
        out += "<script type=\"application/ld+json\">{\"@context\":\"https://schema.org\",\"@type\":\"WebSite\",\"name\":" +
               JsonString(site.name) + ",\"url\":" + JsonString(site.url + "/") +
               (site.owner.empty() ? "" : ",\"publisher\":{\"@type\":\"Organization\",\"name\":" + JsonString(site.owner) + "}") +
               "}</script>\n";
    } else if (page.kind == "post") {
        out += "<script type=\"application/ld+json\">{\"@context\":\"https://schema.org\",\"@type\":\"Article\",\"headline\":" +
               JsonString(page.title) + (page.date.empty() ? "" : ",\"datePublished\":" + JsonString(page.date)) +
               ",\"url\":" + JsonString(url) + "}</script>\n";
    }
    if (!context.faq.empty()) {
        out += "<script type=\"application/ld+json\">{\"@context\":\"https://schema.org\",\"@type\":\"FAQPage\",\"mainEntity\":[";
        for (size_t i = 0; i < context.faq.size(); ++i) {
            if (i) out += ",";
            out += "{\"@type\":\"Question\",\"name\":" + JsonString(context.faq[i].first) +
                   ",\"acceptedAnswer\":{\"@type\":\"Answer\",\"text\":" + JsonString(context.faq[i].second) + "}}";
        }
        out += "]}</script>\n";
    }
    out += "</head>\n";
    return out;
}

std::string HtmlEmitter::Page(const UltraPress::Page& page) const {
    PageContext context;
    context.page = &page;
    EmitBlocks(page.blocks, context);
    std::string out = Head(page, context);
    out += "<body>\n<header>\n";
    out += "<p><a href=\"/\"><strong>" + EscapeText(site.name) + "</strong></a>";
    if (!site.tagline.empty()) out += " <span>" + EscapeText(site.tagline) + "</span>";
    out += "</p>\n";
    EmitMenu(site.FindMenu("main"), "Main", out);
    out += "</header>\n<main>\n";
    if (page.slug != "home" && page.kind == "post") {
        out += "<article>\n<h1>" + EscapeText(page.title) + "</h1>\n";
        if (!page.date.empty()) out += "<p><time datetime=\"" + Attr(page.date) + "\">" + EscapeText(page.date) + "</time></p>\n";
    } else if (page.slug != "home") {
        bool heroFirst = !page.blocks.empty() && page.blocks.front().type == "hero";
        if (!heroFirst) out += "<h1>" + EscapeText(page.title) + "</h1>\n";
    }
    out += context.body;
    if (page.slug != "home" && page.kind == "post") out += "</article>\n";
    out += "</main>\n<footer>\n";
    EmitMenu(site.FindMenu("footer"), "Footer", out);
    out += "<p><small>" + EscapeText(site.owner.empty() ? site.name : site.owner) + "</small></p>\n";
    out += "</footer>\n</body>\n</html>\n";
    return out;
}

std::string HtmlEmitter::Sitemap() const {
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n";
    for (const UltraPress::Page& page : site.pages) {
        if (!page.IsPublished() || page.seo.noindex) continue;
        out += "<url><loc>" + EscapeText(PageUrl(site, page)) + "</loc>";
        if (!page.date.empty()) out += "<lastmod>" + EscapeText(page.date) + "</lastmod>";
        out += "</url>\n";
    }
    out += "</urlset>\n";
    return out;
}

std::string HtmlEmitter::Robots() const {
    return "User-agent: *\nAllow: /\nSitemap: " + site.url + "/sitemap.xml\n";
}

std::string HtmlEmitter::Feed() const {
    std::vector<const UltraPress::Page*> posts;
    for (const UltraPress::Page& page : site.pages) {
        if (page.kind == "post" && page.IsPublished()) posts.push_back(&page);
    }
    std::stable_sort(posts.begin(), posts.end(), [](const UltraPress::Page* a, const UltraPress::Page* b) {
        return a->date > b->date;   // ISO dates sort as text
    });
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<rss version=\"2.0\">\n<channel>\n";
    out += "<title>" + EscapeText(site.name) + "</title>\n<link>" + EscapeText(site.url + "/") + "</link>\n";
    out += "<description>" + EscapeText(site.tagline) + "</description>\n";
    out += "<language>" + EscapeText(site.language) + "</language>\n";
    for (const UltraPress::Page* post : posts) {
        const std::string url = PageUrl(site, *post);
        out += "<item>\n<title>" + EscapeText(post->title) + "</title>\n<link>" + EscapeText(url) + "</link>\n";
        out += "<guid>" + EscapeText(url) + "</guid>\n";
        const std::string date = Rfc822Date(post->date);
        if (!date.empty()) out += "<pubDate>" + date + "</pubDate>\n";
        if (!post->seo.description.empty()) out += "<description>" + EscapeText(post->seo.description) + "</description>\n";
        out += "</item>\n";
    }
    out += "</channel>\n</rss>\n";
    return out;
}

} // namespace UltraPress
