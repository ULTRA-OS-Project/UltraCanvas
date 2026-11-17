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

bool ReadFile(const std::string& path, std::string& out) {
    std::ifstream in(UltraCanvas::PathFromUtf8(path), std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
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

// A relative path as the output names it: '/'-separated. A '\\' is a
// separator on Windows only; elsewhere it is a character of the name.
std::string OutputPath(const fs::path& relative) {
    std::string text = UltraCanvas::PathToUtf8(relative);
#ifdef _WIN32
    std::replace(text.begin(), text.end(), '\\', '/');
#endif
    return text;
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

// Every value of data-us in an HTML fragment, through the HTMLReader.
std::vector<std::string> Behaviours(const std::string& html) {
    std::set<std::string> found;
    if (html.find("data-us") == std::string::npos) return {};
    UltraCanvas::HTML::Parser parser;
    UltraCanvas::HTML::Document document = parser.Parse(html);
    if (document.root) {
        document.root->ForEachElement([&](UltraCanvas::HTML::Node& node) {
            const std::string value = node.GetAttribute("data-us");
            std::istringstream words(value);
            std::string word;
            while (words >> word) found.insert(word);
            return true;
        });
    }
    return std::vector<std::string>(found.begin(), found.end());
}

// The links and ids of a written page, read back through the HTMLReader.
struct PageLinks {
    std::set<std::string> ids;
    std::vector<std::string> references;
};

PageLinks ReadLinks(const std::string& html) {
    PageLinks links;
    UltraCanvas::HTML::Parser parser;
    UltraCanvas::HTML::Document document = parser.Parse(html);
    if (!document.root) return links;
    document.root->ForEachElement([&](UltraCanvas::HTML::Node& node) {
        const std::string id = node.GetId();
        if (!id.empty()) links.ids.insert(id);
        const std::string name = node.GetAttribute("name");
        if (node.tag == "a" && !name.empty()) links.ids.insert(name);
        if (node.tag == "a" || node.tag == "area") {
            if (node.HasAttribute("href")) links.references.push_back(node.GetAttribute("href"));
        } else if (node.tag == "link") {
            const std::string rel = node.GetAttribute("rel");
            if (rel == "stylesheet" || rel == "icon" || rel == "preload" || rel == "modulepreload") {
                links.references.push_back(node.GetAttribute("href"));
            }
        } else if (node.tag == "img" || node.tag == "script" || node.tag == "source" || node.tag == "video" ||
                   node.tag == "audio" || node.tag == "iframe" || node.tag == "track") {
            if (node.HasAttribute("src")) links.references.push_back(node.GetAttribute("src"));
        }
        return true;
    });
    return links;
}

} // namespace

// ----- Helpers ---------------------------------------------------------------------

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

struct Builder::Prepared {
    std::string css, cssFile, cssUrl;
    std::string core, coreFile, coreUrl;
    std::map<std::string, std::pair<std::string, std::string>> behaviours;   // name -> file, content
    std::string logoUrl;
    std::string themeScript;
    std::string speculationRules;
    std::string csp;           // the policy; empty when the inline scripts cannot be hashed
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
}

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

    for (const auto& [name, source] : scripts) {
        if (!IsBehaviourName(name) || name == "core") {
            errors.push_back("behaviour name '" + name + "' is not [a-z0-9-], or is 'core'");
            continue;
        }
        const std::string stripped = StripScript(source);
        prepared.behaviours[name] = {"assets/js/" + name + "." + ContentHash(stripped) + ".js", stripped};
    }

    if (!config.logo.empty() && prepared.logoUrl.empty()) prepared.logoUrl = Asset(config.logo);

    if (!config.tokens.dark.empty()) prepared.themeScript = kThemeScript;

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

std::string Builder::Page(const Route& route, const Prepared& prepared, std::vector<std::string>& errors) const {
    auto layout = layouts.find(route.layout);
    std::string main;
    if (layout == layouts.end()) {
        errors.push_back(route.path + ": no layout '" + route.layout + "'");
        main = route.bodyHtml;
    } else {
        main = layout->second(route);
    }
    std::vector<std::string> behaviours = Behaviours(main);
    for (const std::string& name : behaviours) {
        if (!prepared.behaviours.count(name)) {
            errors.push_back(route.path + ": data-us=\"" + name + "\" names a behaviour no module provides (Builder::AddScript)");
        }
    }

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

    std::string out = Head(route, prepared, behaviours);
    out += "<body>\n<a class=\"us-skip\" href=\"#main\">" + EscapeText(config.skipLinkLabel) + "</a>\n";
    out += "<header class=\"us-navbar\">\n<div class=\"us-container\">\n<a class=\"us-brand\" href=\"/\">";
    if (!prepared.logoUrl.empty()) out += "<img src=\"" + prepared.logoUrl + "\" alt=\"\" width=\"32\" height=\"32\">";
    out += "<span>" + EscapeText(config.name) + "</span></a>\n";
    if (!config.navbar.empty()) {
        out += "<nav class=\"us-nav\" aria-label=\"" + Attr(config.mainNavLabel) + "\">";
        list(config.navbar, out);
        out += "</nav>\n";
    }
    if (!prepared.themeScript.empty()) {
        out += "<button type=\"button\" class=\"us-theme-toggle\" data-us-theme-toggle hidden aria-pressed=\"false\" aria-label=\"" +
               Attr(config.themeToggleLabel) + "\" title=\"" + Attr(config.themeToggleLabel) + "\">" + kSunIcon + kMoonIcon +
               "</button>\n";
    }
    out += "</div>\n</header>\n<main id=\"main\" class=\"us-main\" tabindex=\"-1\">\n";
    out += main;
    if (!main.empty() && main.back() != '\n') out += "\n";
    out += "</main>\n<footer class=\"us-footer\">\n<div class=\"us-container\">\n";
    if (!config.footer.empty()) {
        out += "<nav aria-label=\"" + Attr(config.footerNavLabel) + "\">";
        list(config.footer, out);
        out += "</nav>\n";
    }
    if (!config.footerText.empty()) out += "<p>" + EscapeText(config.footerText) + "</p>\n";
    out += "</div>\n</footer>\n</body>\n</html>\n";
    return out;
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
        notFound.title = config.notFoundTitle;
        notFound.noindex = true;
        notFound.bodyHtml = "<h1>" + EscapeText(config.notFoundTitle) + "</h1>\n<p>" + EscapeText(config.notFoundText) +
                            "</p>\n<p><a href=\"/\">" + EscapeText(config.name) + "</a></p>\n";
        all.push_back(notFound);
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
    for (const Route& route : all) {
        auto file = fileOfRoute.find(route.path);
        if (file == fileOfRoute.end()) continue;
        output[file->second] = Page(route, prepared, errors);
        behavioursOfPage[file->second] = Behaviours(output[file->second]);
    }

    output[prepared.cssFile] = prepared.css;
    output[prepared.coreFile] = prepared.core;
    std::set<std::string> usedBehaviours;
    for (const auto& [file, names] : behavioursOfPage) usedBehaviours.insert(names.begin(), names.end());
    for (const auto& [name, script] : prepared.behaviours) {
        if (usedBehaviours.count(name)) output[script.first] = script.second;
    }
    for (const PendingAsset& asset : assets) output[asset.url.substr(1)] = asset.content;
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
        std::map<std::string, std::set<std::string>> idsOfFile;
        std::map<std::string, std::vector<std::string>> referencesOfFile;
        for (const auto& [path, file] : fileOfRoute) {
            PageLinks links = ReadLinks(output[file]);
            idsOfFile[file] = std::move(links.ids);
            referencesOfFile[file] = std::move(links.references);
        }
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
                    errors.push_back(routePath + ": broken link to " + reference);
                    continue;
                }
                if (!fragment.empty() && !idsOfFile[targetFile].count(fragment)) {
                    errors.push_back(routePath + ": broken anchor " + reference);
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
