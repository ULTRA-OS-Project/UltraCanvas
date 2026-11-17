// UltraSite/core/UltraSiteBuilder.cpp
// The builder: routes laid into their layouts and the page frame, the
// stylesheet and the scripts hashed, the internal links and anchors checked
// by reading every page back through the HTMLReader, the budgets measured,
// and the folder written - with what an earlier build left and this one did
// not removed.
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraSite/UltraSite.h"
#include "UltraSiteInternal.h"
#include "UltraSiteResources.h"
#include "UltraSite/UltraSiteMarkdown.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "HTMLReader/HTMLDocument.h"
#include "HTMLReader/HTMLParser.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
using UltraCanvas::HTML::EscapeText;

namespace UltraSite {

namespace {

constexpr const char* kThemeScript =
    "try{var t=localStorage.getItem(\"us-theme\");"
    "if(t===\"dark\"||t===\"light\")document.documentElement.dataset.theme=t}catch(e){}";

constexpr const char* kSunIcon =
    "<svg class=\"us-sun\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" "
    "stroke-width=\"2\" stroke-linecap=\"round\" aria-hidden=\"true\" focusable=\"false\"><circle cx=\"12\" cy=\"12\" r=\"4\"/>"
    "<path d=\"M12 2v2M12 20v2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M2 12h2M20 12h2M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4\"/></svg>";
constexpr const char* kMoonIcon =
    "<svg class=\"us-moon\" width=\"20\" height=\"20\" viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"currentColor\" "
    "stroke-width=\"2\" stroke-linecap=\"round\" stroke-linejoin=\"round\" aria-hidden=\"true\" focusable=\"false\">"
    "<path d=\"M21 12.8A9 9 0 1 1 11.2 3a7 7 0 0 0 9.8 9.8z\"/></svg>";

std::string Resource(const unsigned char* bytes, size_t size) {
    return std::string(reinterpret_cast<const char*>(bytes), size);
}

std::string Attr(const std::string& value) { return EscapeText(value); }

bool IsAbsoluteUrl(const std::string& url) { return url.find("://") != std::string::npos; }

bool IsBehaviourName(const std::string& name) {
    if (name.empty()) return false;
    for (char c : name) {
        if (!(std::islower(static_cast<unsigned char>(c)) || std::isdigit(static_cast<unsigned char>(c)) || c == '-')) return false;
    }
    return true;
}

bool WriteFile(const fs::path& path, const std::string& content) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << content;
    return static_cast<bool>(out);
}

// "/a/b/../c/./" -> "/a/c/"; the query and the fragment are the caller's.
std::string NormalizePath(const std::string& path) {
    std::vector<std::string> parts;
    size_t start = 1;
    while (start <= path.size()) {
        size_t end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        const std::string part = path.substr(start, end - start);
        if (part == "..") {
            if (!parts.empty()) parts.pop_back();
        } else if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        start = end + 1;
    }
    std::string out;
    for (const std::string& part : parts) out += "/" + part;
    if (out.empty() || (!path.empty() && path.back() == '/')) out += "/";
    return out;
}

// The folder a route's relative links resolve against: "/a/b/" for "/a/b/",
// "/" for "/x.html".
std::string BaseFolder(const std::string& routePath) {
    if (!routePath.empty() && routePath.back() == '/') return routePath;
    const size_t slash = routePath.rfind('/');
    return slash == std::string::npos ? "/" : routePath.substr(0, slash + 1);
}

std::string SplitFileName(const std::string& name, std::string& extension) {
    const size_t slash = name.find_last_of("/\\");
    std::string base = slash == std::string::npos ? name : name.substr(slash + 1);
    const size_t dot = base.rfind('.');
    if (dot == std::string::npos || dot == 0) {
        extension.clear();
        return base;
    }
    extension = base.substr(dot);
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return base.substr(0, dot);
}

// A file stem safe in a URL: letters, digits, '-' and '_'; the rest '-'.
std::string SafeStem(const std::string& stem) {
    std::string out;
    for (unsigned char c : stem) {
        out += (std::isalnum(c) || c == '-' || c == '_') ? static_cast<char>(c) : '-';
    }
    return out.empty() ? "file" : out;
}

std::string FormatSize(size_t bytes) {
    char buffer[32];
    if (bytes < 1024) std::snprintf(buffer, sizeof buffer, "%zu B", bytes);   // locale-ok: integer
    else std::snprintf(buffer, sizeof buffer, "%zu.%zu KB", bytes / 1024, (bytes % 1024) * 10 / 1024);   // locale-ok: integers, dot on purpose
    return buffer;
}

// The search records of a page: one per part between its h2 / h3
// headings, the part before the first heading the page's own.
struct SearchRecord {
    std::string heading, anchor, text;
};

std::string Collapse(const std::string& text) {
    std::string out;
    for (char c : text) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!out.empty() && out.back() != ' ') out += ' ';
        } else {
            out += c;
        }
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// At most `limit` bytes, cut before a character, not inside one.
std::string Cut(const std::string& text, size_t limit) {
    if (text.size() <= limit) return text;
    size_t cut = limit;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) --cut;
    return text.substr(0, cut);
}

std::vector<SearchRecord> SearchSections(const std::string& html) {
    constexpr size_t kLimit = 360;
    std::vector<SearchRecord> records(1);
    UltraCanvas::HTML::Parser parser;
    UltraCanvas::HTML::Document document = parser.Parse(html);
    std::function<void(const UltraCanvas::HTML::Node&)> walk = [&](const UltraCanvas::HTML::Node& node) {
        for (const UltraCanvas::HTML::NodePtr& child : node.children) {
            if (child->IsText()) {
                if (records.back().text.size() < kLimit) records.back().text += child->text + " ";
                continue;
            }
            if (!child->IsElement() || child->tag == "script" || child->tag == "style" || child->tag == "template") continue;
            if ((child->tag == "h2" || child->tag == "h3") && !child->GetId().empty()) {
                // A heading a link can reach starts the next record.
                records.push_back({Collapse(child->TextContent()), child->GetId(), std::string()});
                continue;
            }
            walk(*child);
        }
    };
    if (document.root) walk(*document.root);
    for (SearchRecord& record : records) record.text = Cut(Collapse(record.text), kLimit);
    return records;
}

bool ContainsPath(const NavItem& item, const std::string& path) {
    if (item.href == path) return true;
    for (const NavItem& child : item.children) {
        if (ContainsPath(child, path)) return true;
    }
    return false;
}

// The items from the top of the tree down to the one at `path`.
bool TrailTo(const std::vector<NavItem>& items, const std::string& path, std::vector<const NavItem*>& trail) {
    for (const NavItem& item : items) {
        trail.push_back(&item);
        if (item.href == path || TrailTo(item.children, path, trail)) return true;
        trail.pop_back();
    }
    return false;
}

void Flatten(const std::vector<NavItem>& items, std::vector<const NavItem*>& out, std::set<std::string>& seen) {
    for (const NavItem& item : items) {
        if (!item.href.empty() && item.href[0] == '/' && seen.insert(item.href).second) out.push_back(&item);
        Flatten(item.children, out, seen);
    }
}

} // namespace

// ----- Helpers ---------------------------------------------------------------------

bool ReadFile(const std::string& path, std::string& out) {
    std::ifstream in(UltraCanvas::PathFromUtf8(path), std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

std::string OutputPath(const fs::path& relative) {
    std::string text = UltraCanvas::PathToUtf8(relative);
#ifdef _WIN32
    std::replace(text.begin(), text.end(), '\\', '/');
#endif
    return text;
}

std::string RouteFile(const std::string& path) {
    if (path.empty() || path == "/") return "index.html";
    std::string relative = path[0] == '/' ? path.substr(1) : path;
    if (relative.back() == '/') return relative + "index.html";
    if (relative.size() > 5 && relative.compare(relative.size() - 5, 5, ".html") == 0) return relative;
    return relative + "/index.html";
}

std::string BuildResult::SizeReport() const {
    std::string out = CompressionAvailable() ? "size (gzip -9)  raw        budget    file\n"
                                             : "size (raw)      raw        budget    file\n";
    for (const SizeEntry& entry : sizes) {
        char line[96];
        std::snprintf(line, sizeof line, "%-15s %-10s %-9s ", FormatSize(entry.compressed).c_str(),   // locale-ok: text columns
                      FormatSize(entry.raw).c_str(), entry.budget ? FormatSize(entry.budget).c_str() : "-");
        out += line + entry.path + (entry.OverBudget() ? "  OVER BUDGET" : "") + "\n";
    }
    return out;
}

// ----- The builder -------------------------------------------------------------------

// What one reading of a page's body finds: the behaviours it asks for, the
// ids its anchors can name, the links and sources it holds.
struct Builder::PageFacts {
    std::vector<std::string> behaviours;
    std::set<std::string> ids;
    std::vector<std::string> references;
};

namespace {

void ReadPage(const std::string& html, Builder::PageFacts& facts) {
    std::set<std::string> behaviours;
    UltraCanvas::HTML::Parser parser;
    UltraCanvas::HTML::Document document = parser.Parse(html);
    if (document.root) {
        document.root->ForEachElement([&](UltraCanvas::HTML::Node& node) {
            const std::string id = node.GetId();
            if (!id.empty()) facts.ids.insert(id);
            const std::string value = node.GetAttribute("data-us");
            if (!value.empty()) {
                std::istringstream words(value);
                std::string word;
                while (words >> word) behaviours.insert(word);
            }
            if (node.tag == "a" || node.tag == "area") {
                const std::string name = node.GetAttribute("name");
                if (!name.empty()) facts.ids.insert(name);
                if (node.HasAttribute("href")) facts.references.push_back(node.GetAttribute("href"));
            } else if (node.tag == "link") {
                const std::string rel = node.GetAttribute("rel");
                if (rel == "stylesheet" || rel == "icon" || rel == "preload" || rel == "modulepreload") {
                    facts.references.push_back(node.GetAttribute("href"));
                }
            } else if (node.tag == "img" || node.tag == "script" || node.tag == "source" || node.tag == "video" ||
                       node.tag == "audio" || node.tag == "iframe" || node.tag == "track") {
                if (node.HasAttribute("src")) facts.references.push_back(node.GetAttribute("src"));
            }
            return true;
        });
    }
    facts.behaviours.assign(behaviours.begin(), behaviours.end());
}

} // namespace

struct Builder::Prepared {
    std::string css, cssFile, cssUrl;
    std::string core, coreFile, coreUrl;
    std::map<std::string, std::pair<std::string, std::string>> behaviours;   // name -> file, content
    std::string logoUrl;
    std::string themeScript;
    std::string speculationRules;
    std::string csp;           // the policy; empty when the inline scripts cannot be hashed
    std::map<std::string, std::pair<std::string, std::string>> searchIndex;   // language -> file, content
    std::vector<std::string> notes;                                            // warnings for the build
};

Builder::Builder(SiteConfig siteConfig) : config(std::move(siteConfig)) {
    layouts["page"] = [](const Route& route) { return route.bodyHtml; };
    layouts["post"] = [](const Route& route) {
        std::string out = "<article class=\"us-post\">\n<h1>" + EscapeText(route.title) + "</h1>\n";
        if (!route.date.empty()) {
            out += "<p class=\"us-post-date\"><time datetime=\"" + Attr(route.date) + "\">" + EscapeText(route.date) + "</time></p>\n";
        }
        return out + route.bodyHtml + "</article>\n";
    };
    layouts["doc"] = [this](const Route& route) { return DocLayout(route); };
}

void Builder::AddSidebar(const std::string& id, std::vector<NavItem> items) { sidebars[id] = std::move(items); }

std::string Builder::AssetFromText(const std::string& fileName, const std::string& content) {
    std::string extension;
    const std::string stem = SafeStem(SplitFileName(fileName, extension));
    const std::string url = "/assets/" + stem + "." + ContentHash(content) + extension;
    for (const PendingAsset& asset : assets) {
        if (asset.url == url) return url;
    }
    assets.push_back({fileName, content, url, false});
    return url;
}

std::string Builder::Asset(const std::string& sourcePath) {
    std::string content;
    if (!ReadFile(sourcePath, content)) {
        assetErrors.push_back("cannot read the asset " + sourcePath);
        return std::string();
    }
    return AssetFromText(sourcePath, content);
}

void Builder::AddDirectory(const std::string& sourceDirectory, const std::string& publishedPrefix) {
    std::string prefix = publishedPrefix;
    while (!prefix.empty() && prefix.front() == '/') prefix.erase(prefix.begin());
    while (!prefix.empty() && prefix.back() == '/') prefix.pop_back();
    directories.emplace_back(sourceDirectory, prefix);
}

void Builder::AddFile(const std::string& relativePath, std::string content) {
    std::string relative = relativePath;
    while (!relative.empty() && relative.front() == '/') relative.erase(relative.begin());
    files.emplace_back(relative, std::move(content));
}

void Builder::AddLayout(const std::string& name, LayoutFunction layout) { layouts[name] = std::move(layout); }

void Builder::AddScript(const std::string& behaviour, std::string source) { scripts[behaviour] = std::move(source); }

void Builder::AddRoute(Route route) { routes.push_back(std::move(route)); }

void Builder::Prepare(Prepared& prepared, std::vector<std::string>& errors) {
    std::vector<std::string> tokenErrors;
    const std::string css = Resource(Resources::kBaseCss, Resources::kBaseCssSize) + "\n" +
                            TokensCss(config.tokens, tokenErrors) + "\n" + config.extraCss;
    errors.insert(errors.end(), tokenErrors.begin(), tokenErrors.end());
    prepared.css = MinifyCss(css);
    prepared.cssFile = "assets/site." + ContentHash(prepared.css) + ".css";
    prepared.cssUrl = "/" + prepared.cssFile;

    prepared.core = StripScript(Resource(Resources::kCoreJs, Resources::kCoreJsSize));
    prepared.coreFile = "assets/js/core." + ContentHash(prepared.core) + ".js";
    prepared.coreUrl = "/" + prepared.coreFile;

    // The library's behaviours, then the app's (which may replace one).
    std::map<std::string, std::string> all = {
        {"search", Resource(Resources::kSearchJs, Resources::kSearchJsSize)},
        {"toc", Resource(Resources::kTocJs, Resources::kTocJsSize)},
        {"tabs", Resource(Resources::kTabsJs, Resources::kTabsJsSize)},
        {"copy", Resource(Resources::kCopyJs, Resources::kCopyJsSize)},
    };
    for (const auto& [name, source] : scripts) all[name] = source;
    for (const auto& [name, source] : all) {
        if (!IsBehaviourName(name) || name == "core") {
            errors.push_back("behaviour name '" + name + "' is not [a-z0-9-], or is 'core'");
            continue;
        }
        const std::string stripped = StripScript(source);
        prepared.behaviours[name] = {"assets/js/" + name + "." + ContentHash(stripped) + ".js", stripped};
    }

    if (!config.logo.empty() && prepared.logoUrl.empty()) prepared.logoUrl = Asset(config.logo);

    if (!config.tokens.dark.empty()) prepared.themeScript = kThemeScript;

    // One search index per language, a page to an entry: [title, address,
    // description, [[anchor, heading, text], ...]] - the first section, the
    // part before any heading, with an empty anchor and heading. A site
    // whose texts would make the index too large to fetch gets its titles,
    // headings and descriptions only.
    if (config.search) {
        struct Entry {
            const Route* route;
            std::vector<SearchRecord> sections;
        };
        std::map<std::string, std::vector<Entry>> pages;
        for (const Route& route : routes) {
            if (route.noindex || !route.searchable) continue;
            pages[route.language.empty() ? config.language : route.language].push_back({&route, SearchSections(route.bodyHtml)});
        }
        if (!pages.count(config.language)) pages[config.language];
        auto write = [](const std::vector<Entry>& entries, bool texts) {
            std::string index = "[";
            for (const Entry& entry : entries) {
                if (index.size() > 1) index += ",\n";
                index += "[" + UltraCanvas::JSON::EscapeString(entry.route->title) + "," + UltraCanvas::JSON::EscapeString(entry.route->path) +
                         "," + UltraCanvas::JSON::EscapeString(entry.route->description) + ",[";
                bool first = true;
                for (const SearchRecord& section : entry.sections) {
                    const std::string text = texts ? section.text : std::string();
                    if (section.anchor.empty() && text.empty()) continue;
                    index += std::string(first ? "" : ",") + "[" + UltraCanvas::JSON::EscapeString(section.anchor) + "," +
                             UltraCanvas::JSON::EscapeString(section.heading) +
                             (text.empty() ? std::string() : "," + UltraCanvas::JSON::EscapeString(text)) + "]";
                    first = false;
                }
                index += "]]";
            }
            return index + "]";
        };
        for (const auto& [language, entries] : pages) {
            std::string index = write(entries, true);
            if (config.budgets.searchIndex && CompressedSize(index) > config.budgets.searchIndex) {
                index = write(entries, false);
                prepared.notes.push_back("search/" + language + ": the site's text would make the index too large to fetch, so it "
                                         "searches the titles, headings and descriptions only");
            }
            const std::string file = "search/" + SafeStem(language) + "." + ContentHash(index) + ".json";
            prepared.searchIndex[language] = {file, index};
        }
    }

    // Prerender a page of the site when the pointer rests on its link
    // (Chromium); never an asset, a download or a path the app excludes.
    std::vector<std::string> excluded = {"/assets/"};
    for (const std::string& prefix : config.noPrefetch) {
        if (!prefix.empty() && std::find(excluded.begin(), excluded.end(), prefix) == excluded.end()) excluded.push_back(prefix);
    }
    std::string rules = "{\"prerender\":[{\"where\":{\"and\":[{\"href_matches\":\"/*\"}";
    for (const std::string& prefix : excluded) {
        rules += ",{\"not\":{\"href_matches\":" + UltraCanvas::JSON::EscapeString(prefix + "*") + "}}";
    }
    rules += ",{\"not\":{\"selector_matches\":\"[download], [data-no-prefetch], [rel~=nofollow]\"}}]},\"eagerness\":\"moderate\"}]}";
    prepared.speculationRules = rules;

    // The policy: nothing from another origin runs; the two inline scripts
    // by their hashes. Pictures may come from https: (rich text the owner
    // pasted); inline style attributes stay allowed for the same reason.
    std::vector<std::pair<std::string, std::string>> directives = {
        {"default-src", "'self'"},
        {"script-src", "'self'"},
        {"style-src", "'self' 'unsafe-inline'"},
        {"img-src", "'self' data: https:"},
        {"font-src", "'self'"},
        {"connect-src", "'self'"},
        {"media-src", "'self'"},
        {"frame-src", "'self'"},
        {"object-src", "'none'"},
        {"base-uri", "'self'"},
        {"form-action", "'self'"},
    };
    bool hashed = true;
    for (const std::string& script : {prepared.themeScript, prepared.speculationRules}) {
        if (script.empty()) continue;
        const std::string hash = CspHash(script);
        if (hash.empty()) {
            hashed = false;
            break;
        }
        directives[1].second += " " + hash;
    }
    for (const auto& [directive, sources] : config.cspAdditions) {
        auto found = std::find_if(directives.begin(), directives.end(), [&](const auto& d) { return d.first == directive; });
        if (found == directives.end()) directives.emplace_back(directive, sources);
        else found->second += " " + sources;
    }
    if (hashed) {
        for (const auto& [directive, sources] : directives) {
            prepared.csp += (prepared.csp.empty() ? "" : "; ") + directive + " " + sources;
        }
    }
}

std::string Builder::Head(const Route& route, const Prepared& prepared, const std::vector<std::string>& behaviours) const {
    const std::string language = route.language.empty() ? config.language : route.language;
    const std::string url = config.url + route.path;
    std::string documentTitle = route.documentTitle;
    if (documentTitle.empty()) {
        documentTitle = route.title.empty() || route.title == config.name ? config.name : route.title + " - " + config.name;
    }
    std::string out = "<!doctype html>\n<html lang=\"" + Attr(language) + "\">\n<head>\n<meta charset=\"utf-8\">\n";
    out += "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n";
    if (!prepared.csp.empty()) out += "<meta http-equiv=\"Content-Security-Policy\" content=\"" + Attr(prepared.csp) + "\">\n";
    out += "<title>" + EscapeText(documentTitle) + "</title>\n";
    if (!route.description.empty()) out += "<meta name=\"description\" content=\"" + Attr(route.description) + "\">\n";
    if (!config.generator.empty()) out += "<meta name=\"generator\" content=\"" + Attr(config.generator) + "\">\n";
    if (route.noindex) out += "<meta name=\"robots\" content=\"noindex\">\n";
    out += "<meta name=\"color-scheme\" content=\"" + std::string(prepared.themeScript.empty() ? "light" : "light dark") + "\">\n";
    out += "<link rel=\"canonical\" href=\"" + Attr(url) + "\">\n";
    for (const auto& [otherLanguage, path] : route.alternates) {
        out += "<link rel=\"alternate\" hreflang=\"" + Attr(otherLanguage) + "\" href=\"" + Attr(config.url + path) + "\">\n";
    }
    if (!route.alternates.empty() && !route.alternates.count(language)) {
        out += "<link rel=\"alternate\" hreflang=\"" + Attr(language) + "\" href=\"" + Attr(url) + "\">\n";
    }
    out += "<meta property=\"og:type\" content=\"" + Attr(route.kind.empty() ? "website" : route.kind) + "\">\n";
    out += "<meta property=\"og:title\" content=\"" + Attr(route.title.empty() ? config.name : route.title) + "\">\n";
    if (!route.description.empty()) out += "<meta property=\"og:description\" content=\"" + Attr(route.description) + "\">\n";
    out += "<meta property=\"og:url\" content=\"" + Attr(url) + "\">\n";
    out += "<meta property=\"og:site_name\" content=\"" + Attr(config.name) + "\">\n";
    if (!route.image.empty()) {
        out += "<meta property=\"og:image\" content=\"" + Attr(route.image) + "\">\n";
        if (route.imageWidth > 0 && route.imageHeight > 0) {
            out += "<meta property=\"og:image:width\" content=\"" + std::to_string(route.imageWidth) + "\">\n";
            out += "<meta property=\"og:image:height\" content=\"" + std::to_string(route.imageHeight) + "\">\n";
        }
        out += "<meta name=\"twitter:card\" content=\"summary_large_image\">\n";
    } else {
        out += "<meta name=\"twitter:card\" content=\"summary\">\n";
    }
    if (!prepared.themeScript.empty()) out += "<script>" + prepared.themeScript + "</script>\n";
    out += "<link rel=\"stylesheet\" href=\"" + prepared.cssUrl + "\">\n";
    out += "<script type=\"module\" src=\"" + prepared.coreUrl + "\"></script>\n";
    for (const std::string& name : behaviours) {
        auto found = prepared.behaviours.find(name);
        if (found != prepared.behaviours.end()) out += "<script type=\"module\" src=\"/" + found->second.first + "\"></script>\n";
    }
    out += "<script type=\"speculationrules\">" + prepared.speculationRules + "</script>\n";
    out += route.extraHead;
    if (!route.extraHead.empty() && route.extraHead.back() != '\n') out += "\n";
    for (const std::string& data : route.jsonLd) {
        // "</" cannot end the element early inside JSON: "<\/".
        std::string safe;
        for (size_t i = 0; i < data.size(); ++i) {
            if (data[i] == '<' && i + 1 < data.size() && data[i + 1] == '/') safe += "<\\";
            else safe += data[i];
        }
        out += "<script type=\"application/ld+json\">" + safe + "</script>\n";
    }
    out += "</head>\n";
    return out;
}

std::string Builder::SearchForm(const Route& route, const Prepared& prepared, bool large) const {
    const std::string language = route.language.empty() ? config.language : route.language;
    auto index = prepared.searchIndex.find(language);
    if (index == prepared.searchIndex.end()) index = prepared.searchIndex.find(config.language);
    if (index == prepared.searchIndex.end()) return std::string();
    const Labels& labels = config.labels;
    std::string out = std::string("<form class=\"us-search") + (large ? " us-search-large" : "") +
                      "\" role=\"search\" action=\"/search/\" method=\"get\" data-us=\"search\" data-index=\"/" + index->second.first +
                      "\" data-nothing=\"" + Attr(labels.searchNothing) + "\"" + (large ? " data-results=\"us-search-results\"" : "") + ">";
    out += "<input type=\"search\" name=\"q\" placeholder=\"" + Attr(labels.search) + "\" aria-label=\"" + Attr(labels.search) +
           "\" autocomplete=\"off\" spellcheck=\"false\">";
    return out + "</form>";
}

Route Builder::SearchPage(const Prepared& prepared) const {
    const Labels& labels = config.labels;
    Route page;
    page.path = "/search/";
    page.title = labels.search;
    page.noindex = true;
    page.searchable = false;
    page.bodyHtml = "<h1>" + EscapeText(labels.search) + "</h1>\n" + SearchForm(page, prepared, true) +
                    "\n<div id=\"us-search-results\" class=\"us-search-page-results\" aria-live=\"polite\"></div>\n" +
                    "<p class=\"us-search-noscript\">" + EscapeText(labels.searchNoScript) + "</p>\n<ul class=\"us-site-index\">\n";
    // Every page, so a visitor without script finds them too.
    for (const Route& route : routes) {
        if (route.noindex || !route.searchable) continue;
        page.bodyHtml += "<li><a href=\"" + Attr(route.path) + "\"" +
                         (route.language.empty() ? std::string() : " hreflang=\"" + Attr(route.language) + "\"") + ">" +
                         EscapeText(route.title.empty() ? route.path : route.title) + "</a></li>\n";
    }
    page.bodyHtml += "</ul>\n";
    return page;
}

std::string Builder::DocLayout(const Route& route) const {
    const Labels& labels = config.labels;
    static const std::vector<NavItem> kNone;
    auto sidebar = sidebars.find(route.sidebar);
    const std::vector<NavItem>& items = sidebar == sidebars.end() ? kNone : sidebar->second;

    std::function<void(const std::vector<NavItem>&, std::string&)> tree = [&](const std::vector<NavItem>& list, std::string& out) {
        out += "<ul>";
        for (const NavItem& item : list) {
            std::string link = item.href.empty()
                                   ? "<span>" + EscapeText(item.label) + "</span>"
                                   : "<a href=\"" + Attr(item.href) + "\"" + (item.href == route.path ? " aria-current=\"page\"" : "") +
                                         ">" + EscapeText(item.label) + "</a>";
            if (item.children.empty()) {
                out += "<li>" + link + "</li>";
                continue;
            }
            // A category away from this page is its link alone, as a
            // collapsed one in Docusaurus: its page lists what it holds,
            // and the sidebar stays small however large the site grows.
            const bool here = ContainsPath(item, route.path);
            if (!here && !item.href.empty()) {
                out += "<li class=\"us-category us-category-closed\">" + link + "</li>";
                continue;
            }
            out += std::string("<li class=\"us-category\"><details") + (here ? " open" : "") + "><summary>" + link + "</summary>";
            tree(item.children, out);
            out += "</details></li>";
        }
        out += "</ul>";
    };

    // The table of contents: the route's, or the body's h2 / h3 with an id.
    std::vector<Heading> toc = route.toc;
    if (toc.empty() && route.bodyHtml.find("<h") != std::string::npos) {
        UltraCanvas::HTML::Parser parser;
        UltraCanvas::HTML::Document document = parser.Parse(route.bodyHtml);
        if (document.root) {
            document.root->ForEachElement([&](UltraCanvas::HTML::Node& node) {
                if ((node.tag == "h2" || node.tag == "h3") && !node.GetId().empty()) {
                    toc.push_back({node.tag == "h2" ? 2 : 3, Collapse(node.TextContent()), node.GetId()});
                }
                return true;
            });
        }
    }
    std::string tocList;
    if (!toc.empty()) {
        tocList = "<ul>";
        for (const Heading& heading : toc) {
            tocList += std::string("<li") + (heading.level > 2 ? " class=\"us-toc-sub\"" : "") + "><a href=\"#" + Attr(heading.id) + "\">" +
                       EscapeText(heading.text) + "</a></li>";
        }
        tocList += "</ul>";
    }

    std::string out = "<div class=\"us-doc\">\n";
    out += "<aside class=\"us-sidebar\" id=\"us-sidebar\">\n<a class=\"us-sidebar-close\" href=\"#main\">" + EscapeText(labels.close) +
           "</a>\n<nav aria-label=\"" + Attr(labels.docsNav) + "\">";
    tree(items, out);
    out += "</nav>\n</aside>\n<div class=\"us-doc-main\">\n";
    if (!items.empty()) out += "<a class=\"us-sidebar-open\" href=\"#us-sidebar\">" + EscapeText(labels.menu) + "</a>\n";

    std::vector<const NavItem*> trail;
    if (TrailTo(items, route.path, trail) && trail.size() > 1) {
        out += "<nav class=\"us-breadcrumbs\" aria-label=\"" + Attr(labels.breadcrumbs) + "\"><ol>";
        for (size_t i = 0; i < trail.size(); ++i) {
            const NavItem& step = *trail[i];
            if (i + 1 == trail.size()) out += "<li><span aria-current=\"page\">" + EscapeText(step.label) + "</span></li>";
            else if (step.href.empty()) out += "<li><span>" + EscapeText(step.label) + "</span></li>";
            else out += "<li><a href=\"" + Attr(step.href) + "\">" + EscapeText(step.label) + "</a></li>";
        }
        out += "</ol></nav>\n";
    }
    if (!tocList.empty()) {
        out += "<details class=\"us-toc-mobile\"><summary>" + EscapeText(labels.toc) + "</summary>" + tocList + "</details>\n";
    }
    out += "<article class=\"us-doc-content\">\n";
    if (route.bodyHtml.find("<h1") == std::string::npos) out += "<h1>" + EscapeText(route.title) + "</h1>\n";
    out += route.bodyHtml;
    if (!route.bodyHtml.empty() && route.bodyHtml.back() != '\n') out += "\n";
    out += "</article>\n";
    if (!route.editUrl.empty() || !route.date.empty()) {
        out += "<footer class=\"us-doc-footer\">";
        if (!route.editUrl.empty()) {
            out += "<a href=\"" + Attr(route.editUrl) + "\" rel=\"noopener\">" + EscapeText(labels.edit) + "</a>";
        }
        if (!route.date.empty()) {
            out += "<p>" + EscapeText(labels.lastUpdated) + " <time datetime=\"" + Attr(route.date) + "\">" + EscapeText(route.date) +
                   "</time></p>";
        }
        out += "</footer>\n";
    }
    std::vector<const NavItem*> order;
    std::set<std::string> seen;
    Flatten(items, order, seen);
    auto here = std::find_if(order.begin(), order.end(), [&](const NavItem* item) { return item->href == route.path; });
    if (here != order.end() && order.size() > 1) {
        out += "<nav class=\"us-pagination\" aria-label=\"" + Attr(labels.pagination) + "\">";
        if (here != order.begin()) {
            const NavItem& previous = **(here - 1);
            out += "<a class=\"us-prev\" href=\"" + Attr(previous.href) + "\" rel=\"prev\"><small>" + EscapeText(labels.previous) +
                   "</small><span>" + EscapeText(previous.label) + "</span></a>";
        }
        if (here + 1 != order.end()) {
            const NavItem& next = **(here + 1);
            out += "<a class=\"us-next\" href=\"" + Attr(next.href) + "\" rel=\"next\"><small>" + EscapeText(labels.next) +
                   "</small><span>" + EscapeText(next.label) + "</span></a>";
        }
        out += "</nav>\n";
    }
    out += "</div>\n";
    if (!tocList.empty()) {
        out += "<nav class=\"us-toc\" aria-label=\"" + Attr(labels.toc) + "\" data-us=\"toc\"><p class=\"us-toc-title\">" +
               EscapeText(labels.toc) + "</p>" + tocList + "</nav>\n";
    }
    out += "</div>\n";
    return out;
}

std::string Builder::Page(const Route& route, const Prepared& prepared, std::vector<std::string>& errors, PageFacts* facts) const {
    auto layout = layouts.find(route.layout);
    std::string main;
    if (layout == layouts.end()) {
        errors.push_back(route.path + ": no layout '" + route.layout + "'");
        main = route.bodyHtml;
    } else {
        main = layout->second(route);
    }
    const Labels& labels = config.labels;
    const std::string language = route.language.empty() ? config.language : route.language;

    std::function<void(const std::vector<NavItem>&, std::string&)> list = [&](const std::vector<NavItem>& items, std::string& out) {
        out += "<ul>";
        for (const NavItem& item : items) {
            out += "<li>";
            if (item.href.empty()) {
                out += "<span>" + EscapeText(item.label) + "</span>";
            } else {
                out += "<a href=\"" + Attr(item.href) + "\"";
                if (item.href == route.path) out += " aria-current=\"page\"";
                if (IsAbsoluteUrl(item.href)) out += " rel=\"noopener\"";
                out += ">" + EscapeText(item.label) + "</a>";
            }
            if (!item.children.empty()) list(item.children, out);
            out += "</li>";
        }
        out += "</ul>";
    };

    std::string body = "<body>\n<a class=\"us-skip\" href=\"#main\">" + EscapeText(labels.skipLink) + "</a>\n";
    body += "<header class=\"us-navbar\">\n<div class=\"us-container\">\n<a class=\"us-brand\" href=\"/\">";
    if (!prepared.logoUrl.empty()) body += "<img src=\"" + prepared.logoUrl + "\" alt=\"\" width=\"32\" height=\"32\">";
    body += "<span>" + EscapeText(config.name) + "</span></a>\n";
    if (!config.navbar.empty()) {
        body += "<nav class=\"us-nav\" aria-label=\"" + Attr(labels.mainNav) + "\">";
        list(config.navbar, body);
        body += "</nav>\n";
    }
    if (config.search) body += SearchForm(route, prepared, false) + "\n";
    if (!route.alternates.empty()) {
        // The same page in the site's other languages.
        std::map<std::string, std::string> languages = route.alternates;
        languages.emplace(language, route.path);
        body += "<nav class=\"us-langs\" aria-label=\"" + Attr(labels.languages) + "\"><ul>";
        for (const auto& [code, path] : languages) {
            auto name = config.languageNames.find(code);
            std::string label = name == config.languageNames.end() ? code : name->second;
            if (name == config.languageNames.end()) {
                std::transform(label.begin(), label.end(), label.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            }
            body += "<li><a href=\"" + Attr(path) + "\" hreflang=\"" + Attr(code) + "\" lang=\"" + Attr(code) + "\"" +
                    (code == language ? " aria-current=\"page\"" : "") + ">" + EscapeText(label) + "</a></li>";
        }
        body += "</ul></nav>\n";
    }
    if (!prepared.themeScript.empty()) {
        body += "<button type=\"button\" class=\"us-theme-toggle\" data-us-theme-toggle hidden aria-pressed=\"false\" aria-label=\"" +
                Attr(labels.themeToggle) + "\" title=\"" + Attr(labels.themeToggle) + "\">" + kSunIcon + kMoonIcon + "</button>\n";
    }
    body += "</div>\n</header>\n<main id=\"main\" class=\"us-main\" tabindex=\"-1\">\n";
    body += main;
    if (!main.empty() && main.back() != '\n') body += "\n";
    body += "</main>\n<footer class=\"us-footer\">\n<div class=\"us-container\">\n";
    if (!config.footer.empty()) {
        body += "<nav aria-label=\"" + Attr(labels.footerNav) + "\">";
        list(config.footer, body);
        body += "</nav>\n";
    }
    if (!config.footerText.empty()) body += "<p>" + EscapeText(config.footerText) + "</p>\n";
    body += "</div>\n</footer>\n</body>\n</html>\n";

    // One reading of the body: the behaviours it asks for (the frame's
    // too), and for the build its ids and links.
    PageFacts local;
    PageFacts& read = facts ? *facts : local;
    ReadPage(body, read);
    for (const std::string& name : read.behaviours) {
        if (!prepared.behaviours.count(name)) {
            errors.push_back(route.path + ": data-us=\"" + name + "\" names a behaviour no module provides (Builder::AddScript)");
        }
    }
    return Head(route, prepared, read.behaviours) + body;
}

std::string Builder::RenderPage(const Route& route) {
    Prepared prepared;
    std::vector<std::string> errors;
    Prepare(prepared, errors);
    return Page(route, prepared, errors);
}

BuildResult Builder::Build(const std::string& outputDirectory, const BuildOptions& options) {
    BuildResult result;
    std::vector<std::string>& errors = result.errors;
    Prepared prepared;
    Prepare(prepared, errors);
    errors.insert(errors.end(), assetErrors.begin(), assetErrors.end());
    result.warnings.insert(result.warnings.end(), prepared.notes.begin(), prepared.notes.end());
    if (!HashAvailable()) {
        result.warnings.push_back("no SHA-256 in this build (UltraCrypt without libsodium): asset names carry an FNV-1a hash "
                                  "and the pages carry no Content-Security-Policy");
    }
    if (!IsAbsoluteUrl(config.url)) errors.push_back("the site's url '" + config.url + "' is not an absolute URL");

    // The routes, and the 404 page when the app wrote none.
    std::vector<Route> all = routes;
    if (std::none_of(all.begin(), all.end(), [](const Route& r) { return r.path == "/404.html"; })) {
        Route notFound;
        notFound.path = "/404.html";
        notFound.title = config.labels.notFoundTitle;
        notFound.noindex = true;
        notFound.searchable = false;
        notFound.bodyHtml = "<h1>" + EscapeText(config.labels.notFoundTitle) + "</h1>\n<p>" + EscapeText(config.labels.notFoundText) +
                            "</p>\n<p><a href=\"/\">" + EscapeText(config.name) + "</a></p>\n";
        all.push_back(notFound);
    }
    if (config.search && std::none_of(all.begin(), all.end(), [](const Route& r) { return r.path == "/search/"; })) {
        all.push_back(SearchPage(prepared));
    }

    // What the folder will hold: relative path -> content (pages and
    // generated files), and the copied folders' files (on disk already).
    std::map<std::string, std::string> output;
    std::map<std::string, fs::path> copied;
    std::map<std::string, const Route*> pageByPath;   // every spelling a link may use
    std::map<std::string, std::string> fileOfRoute;
    std::set<std::string> seenFiles;
    for (const Route& route : all) {
        if (route.path.empty() || route.path[0] != '/' || route.path.find("..") != std::string::npos) {
            errors.push_back("route path '" + route.path + "' must start with / and hold no ..");
            continue;
        }
        const std::string file = RouteFile(route.path);
        if (!seenFiles.insert(file).second) {
            errors.push_back("two routes write " + file + " (" + route.path + ")");
            continue;
        }
        fileOfRoute[route.path] = file;
        pageByPath[route.path] = &route;
        pageByPath["/" + file] = &route;
        if (route.path.size() > 1 && route.path.back() == '/') pageByPath[route.path.substr(0, route.path.size() - 1)] = &route;
    }

    std::map<std::string, std::vector<std::string>> behavioursOfPage;
    std::map<std::string, std::set<std::string>> idsOfFile;
    std::map<std::string, std::vector<std::string>> referencesOfFile;
    for (const Route& route : all) {
        auto file = fileOfRoute.find(route.path);
        if (file == fileOfRoute.end()) continue;
        PageFacts facts;
        output[file->second] = Page(route, prepared, errors, &facts);
        behavioursOfPage[file->second] = std::move(facts.behaviours);
        idsOfFile[file->second] = std::move(facts.ids);
        referencesOfFile[file->second] = std::move(facts.references);
    }

    output[prepared.cssFile] = prepared.css;
    output[prepared.coreFile] = prepared.core;
    std::set<std::string> usedBehaviours;
    for (const auto& [file, names] : behavioursOfPage) usedBehaviours.insert(names.begin(), names.end());
    for (const auto& [name, script] : prepared.behaviours) {
        if (usedBehaviours.count(name)) output[script.first] = script.second;
    }
    for (const PendingAsset& asset : assets) output[asset.url.substr(1)] = asset.content;
    for (const auto& [language, index] : prepared.searchIndex) output[index.first] = index.second;
    for (const auto& [relative, content] : files) output[relative] = content;

    // sitemap.xml and robots.txt.
    std::string sitemap = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<urlset xmlns=\"http://www.sitemaps.org/schemas/sitemap/0.9\">\n";
    for (const Route& route : all) {
        if (route.noindex || !fileOfRoute.count(route.path)) continue;
        sitemap += "<url><loc>" + EscapeText(config.url + route.path) + "</loc>";
        if (!route.date.empty()) sitemap += "<lastmod>" + EscapeText(route.date) + "</lastmod>";
        sitemap += "</url>\n";
    }
    sitemap += "</urlset>\n";
    output["sitemap.xml"] = sitemap;
    output["robots.txt"] = "User-agent: *\nAllow: /\nSitemap: " + config.url + "/sitemap.xml\n";

    // The host's rules: Apache (.htaccess) and Netlify / Cloudflare Pages
    // (_headers). Hashed files are immutable; pages are checked every visit.
    const std::string policy = prepared.csp.empty() ? std::string() : prepared.csp + "; frame-ancestors 'self'";
    std::string htaccess =
        "# Written by UltraSite. Files named by their content hash are cached for a\n"
        "# year; pages and the site's lists are checked again on every visit.\n"
        "AddType text/javascript .js .mjs\nAddType image/avif .avif\nAddType image/webp .webp\nAddType font/woff2 .woff2\n"
        "ErrorDocument 404 /404.html\n"
        "<IfModule mod_headers.c>\n"
        "  Header set X-Content-Type-Options \"nosniff\"\n"
        "  Header set Referrer-Policy \"strict-origin-when-cross-origin\"\n";
    if (!policy.empty()) htaccess += "  Header set Content-Security-Policy \"" + policy + "\"\n";
    htaccess +=
        "  <FilesMatch \"\\.[0-9a-f]{10}\\.[A-Za-z0-9]+$\">\n"
        "    Header set Cache-Control \"public, max-age=31536000, immutable\"\n"
        "  </FilesMatch>\n"
        "  <FilesMatch \"\\.(html|xml|txt)$\">\n"
        "    Header set Cache-Control \"no-cache\"\n"
        "  </FilesMatch>\n"
        "</IfModule>\n"
        "<IfModule mod_deflate.c>\n"
        "  AddOutputFilterByType DEFLATE text/html text/css text/javascript application/javascript application/json "
        "image/svg+xml text/xml application/xml text/plain\n"
        "</IfModule>\n";
    output[".htaccess"] = htaccess;
    std::string headers = "/*\n  X-Content-Type-Options: nosniff\n  Referrer-Policy: strict-origin-when-cross-origin\n";
    if (!policy.empty()) headers += "  Content-Security-Policy: " + policy + "\n";
    headers += "/assets/*\n  Cache-Control: public, max-age=31536000, immutable\n";
    output["_headers"] = headers;

    // The copied folders, file by file, so links into them can be checked.
    std::error_code ec;
    for (const auto& [source, prefix] : directories) {
        const fs::path root = UltraCanvas::PathFromUtf8(source);
        if (!fs::is_directory(root, ec)) continue;
        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            const std::string relative = OutputPath(entry.path().lexically_relative(root));
            copied[(prefix.empty() ? "" : prefix + "/") + relative] = entry.path();
        }
    }

    // Every internal link and anchor resolves.
    if (options.checkLinks) {
        for (const auto& [routePath, file] : fileOfRoute) {
            for (const std::string& reference : referencesOfFile[file]) {
                if (reference.empty() || IsAbsoluteUrl(reference) || reference.rfind("//", 0) == 0 ||
                    reference.rfind("mailto:", 0) == 0 || reference.rfind("tel:", 0) == 0 ||
                    reference.rfind("data:", 0) == 0 || reference.rfind("javascript:", 0) == 0) {
                    continue;
                }
                std::string target = reference;
                std::string fragment;
                const size_t hash = target.find('#');
                if (hash != std::string::npos) {
                    fragment = target.substr(hash + 1);
                    target = target.substr(0, hash);
                }
                const size_t query = target.find('?');
                if (query != std::string::npos) target = target.substr(0, query);
                if (target.empty()) target = routePath;
                else if (target[0] != '/') target = BaseFolder(routePath) + target;
                target = NormalizePath(target);

                std::string targetFile;
                auto page = pageByPath.find(target);
                if (page != pageByPath.end()) {
                    targetFile = fileOfRoute[page->second->path];
                } else {
                    const std::string relative = target.substr(1);
                    if (output.count(relative) || copied.count(relative)) continue;   // a file: no anchors to check
                    (options.brokenLinksWarn ? result.warnings : errors).push_back(routePath + ": broken link to " + reference);
                    continue;
                }
                if (!fragment.empty() && !idsOfFile[targetFile].count(fragment)) {
                    (options.brokenLinksWarn ? result.warnings : errors).push_back(routePath + ": broken anchor " + reference);
                }
            }
        }
    }

    // The budgets.
    auto measure = [&](const std::string& path, const std::string& content, size_t budget) {
        SizeEntry entry{path, content.size(), CompressedSize(content), budget};
        if (entry.OverBudget()) {
            const std::string message = path + " is " + FormatSize(entry.compressed) + " compressed, over its budget of " +
                                        FormatSize(budget);
            (options.enforceBudgets ? errors : result.warnings).push_back(message);
        }
        result.sizes.push_back(entry);
        return entry.compressed;
    };
    const Budgets& budgets = config.budgets;
    measure(prepared.cssFile, prepared.css, budgets.stylesheet);
    const size_t coreSize = measure(prepared.coreFile, prepared.core, budgets.coreScript);
    std::map<std::string, size_t> behaviourSize;
    for (const auto& [name, script] : prepared.behaviours) {
        if (usedBehaviours.count(name)) behaviourSize[name] = measure(script.first, script.second, budgets.behaviourScript);
    }
    for (const auto& [path, file] : fileOfRoute) {
        measure(file, output[file], budgets.pageHtml);
        size_t scriptsTotal = coreSize;
        for (const std::string& name : behavioursOfPage[file]) scriptsTotal += behaviourSize[name];
        if (budgets.pageScripts && scriptsTotal > budgets.pageScripts) {
            const std::string message = file + " loads " + FormatSize(scriptsTotal) + " of script, over its budget of " +
                                        FormatSize(budgets.pageScripts);
            (options.enforceBudgets ? errors : result.warnings).push_back(message);
        }
    }
    for (const auto& [language, index] : prepared.searchIndex) measure(index.first, index.second, budgets.searchIndex);
    const size_t inlineSize = prepared.themeScript.size() + prepared.speculationRules.size();
    if (budgets.inlineScripts && inlineSize > budgets.inlineScripts) {
        const std::string message = "the inline scripts are " + FormatSize(inlineSize) + ", over their budget of " +
                                    FormatSize(budgets.inlineScripts);
        (options.enforceBudgets ? errors : result.warnings).push_back(message);
    }

    // Write.
    const fs::path outDir = UltraCanvas::PathFromUtf8(outputDirectory);
    fs::create_directories(outDir, ec);
    if (ec) {
        errors.push_back("cannot create " + outputDirectory + ": " + ec.message());
        return result;
    }
    for (const auto& [relative, content] : output) {
        if (!WriteFile(outDir / UltraCanvas::PathFromUtf8(relative), content)) {
            errors.push_back("cannot write " + relative);
            continue;
        }
        result.files.push_back(relative);
    }
    for (const auto& [relative, source] : copied) {
        const fs::path target = outDir / UltraCanvas::PathFromUtf8(relative);
        fs::create_directories(target.parent_path(), ec);
        fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            errors.push_back("cannot copy " + relative + ": " + ec.message());
            ec.clear();
            continue;
        }
        result.files.push_back(relative);
    }

    // What an earlier build wrote and this one did not, then the folders
    // left empty, innermost first.
    if (options.removeStale) {
        std::set<std::string> written(result.files.begin(), result.files.end());
        written.insert(options.keep.begin(), options.keep.end());
        std::vector<fs::path> stale;
        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(outDir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            const std::string relative = OutputPath(entry.path().lexically_relative(outDir));
            if (written.count(relative)) continue;
            stale.push_back(entry.path());
            result.removed.push_back(relative);
        }
        for (const fs::path& file : stale) fs::remove(file, ec);
        std::vector<fs::path> folders;
        for (const fs::directory_entry& entry : fs::recursive_directory_iterator(outDir, ec)) {
            if (entry.is_directory(ec)) folders.push_back(entry.path());
        }
        std::sort(folders.begin(), folders.end(), [](const fs::path& a, const fs::path& b) {
            return a.native().size() > b.native().size();
        });
        for (const fs::path& folder : folders) {
            if (fs::is_empty(folder, ec)) fs::remove(folder, ec);
        }
    }
    result.ok = errors.empty();
    return result;
}

} // namespace UltraSite
