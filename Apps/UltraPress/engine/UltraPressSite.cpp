// Apps/UltraPress/engine/UltraPressSite.cpp
// The HTML + JavaScript output: a site's pages, menus and theme handed to
// UltraSite, and the choice between the two outputs.
// Version: 0.3.0 - pictures through UltraSite::Picture, precompress, versioned_docs, the blog
// Version: 0.2.0 - the site's docs/ (and its translations) through AddDocs, with search; PublishDocsFolder
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressSite.h"
#include "UltraPressHtml.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "HTMLReader/HTMLDocument.h"
#include "UltraCanvasPathUtf8.h"

#if ULTRAPRESS_HAS_ULTRASITE
#include "UltraSite/UltraSite.h"
#endif

#include <filesystem>
#include <map>
#include <algorithm>

namespace fs = std::filesystem;

namespace UltraPress {

namespace {

// The blocks as UltraPressHtml.cpp writes them, on the site's tokens. The
// hero and the call to action are full-width bands in the night colours,
// as the element renderer draws them.
const char* kBlockStylesheet = R"CSS(
.us-main > section, .us-main > blockquote, .us-main > figure, .us-main > form, .us-main > p.actions { margin: 0 0 3rem; }
.us-main section > h2:first-child { margin-top: 0; }
.us-main > .hero, .us-main > .cta {
  margin: 0 calc(50% - 50vw) 3rem;
  padding: clamp(3rem, 7vw, 6rem) max(1.25rem, calc(50vw - var(--us-width) / 2 + 1.25rem));
  background: linear-gradient(160deg, var(--us-night), var(--us-night-soft));
  color: var(--us-on-dark);
}
.us-main > .hero:first-child { margin-top: -2rem; }
.us-main > .hero:last-child, .us-main > .cta:last-child { margin-bottom: -3rem; }
.hero { display: grid; gap: 0 3rem; align-items: center; }
@media (min-width: 60rem) {
  .hero:has(> figure) { grid-template-columns: minmax(0, 1.1fr) minmax(0, .9fr); }
  .hero > :not(figure) { grid-column: 1; }
  .hero > figure { grid-column: 2; grid-row: 1 / span 5; margin: 0; }
}
.hero h1 { color: #fff; font-size: clamp(2rem, 1.4rem + 2.6vw, 3.4rem); margin: .25em 0 .4em; max-width: 22ch; }
.hero p, .cta p { max-width: 42rem; font-size: 1.15rem; }
.hero figure img { border-radius: calc(var(--us-radius) * 2); box-shadow: 0 20px 50px rgb(0 0 0 / .35); }
.cta { text-align: center; }
.cta h2 { color: #fff; margin-top: 0; }
.cta p { margin-inline: auto; }
.cta .actions { justify-content: center; }
.kicker { text-transform: uppercase; letter-spacing: .12em; font-size: .8rem; font-weight: 700; color: var(--us-accent); margin: 0; }
.hero .kicker { color: color-mix(in srgb, var(--us-accent) 55%, #fff); }
.actions { display: flex; flex-wrap: wrap; gap: .75rem; margin: 1.5rem 0 0; }
.actions a, .actions button {
  display: inline-flex; align-items: center; min-height: 2.75rem; padding: .55rem 1.35rem;
  border: 1.5px solid currentColor; border-radius: 999px; background: transparent;
  color: var(--us-accent); font: inherit; font-weight: 600; text-decoration: none; cursor: pointer;
  transition: background-color .15s, color .15s, border-color .15s;
}
.actions a.primary, .actions button[type="submit"] { background: var(--us-accent); border-color: var(--us-accent); color: var(--us-accent-fg); }
.actions a.primary:hover, .actions button[type="submit"]:hover { background: var(--us-accent-hover); border-color: var(--us-accent-hover); }
.actions a:not(.primary):hover { background: color-mix(in srgb, var(--us-accent) 10%, transparent); }
.hero .actions a:not(.primary), .cta .actions a:not(.primary) { color: #fff; }
.hero .actions a:not(.primary):hover, .cta .actions a:not(.primary):hover { background: rgb(255 255 255 / .1); }
.columns { display: grid; grid-template-columns: repeat(auto-fit, minmax(16rem, 1fr)); gap: 1.25rem; }
.columns > div > :last-child { margin-bottom: 0; }
.card {
  height: 100%; padding: 1.25rem 1.25rem .25rem; overflow: hidden;
  background: var(--us-bg); border: 1px solid var(--us-border); border-radius: calc(var(--us-radius) * 1.5);
  transition: box-shadow .2s, transform .2s;
}
.card:hover { box-shadow: 0 12px 32px rgb(0 0 0 / .08); transform: translateY(-2px); }
.card figure { margin: -1.25rem -1.25rem 1rem; }
.card figure img { width: 100%; aspect-ratio: 16 / 9; object-fit: cover; border-radius: 0; }
.card h3 { margin-top: 0; }
.features { list-style: none; padding: 0; display: grid; grid-template-columns: repeat(auto-fit, minmax(16rem, 1fr)); gap: .9rem 2rem; }
.features li { position: relative; padding-left: 2rem; }
.features li::before {
  content: ""; position: absolute; left: .15rem; top: .4em; width: .95rem; height: .5rem;
  border-left: 2.5px solid var(--us-accent); border-bottom: 2.5px solid var(--us-accent); transform: rotate(-45deg);
}
.faq details { margin: 0 0 .6rem; padding: 0 1.1rem; background: var(--us-bg); border: 1px solid var(--us-border); border-radius: var(--us-radius); }
.faq summary { display: flex; justify-content: space-between; gap: 1rem; padding: .9rem 0; font-weight: 600; cursor: pointer; list-style: none; }
.faq summary::-webkit-details-marker { display: none; }
.faq summary::after { content: "+"; color: var(--us-accent); font-size: 1.3rem; line-height: 1; }
.faq details[open] summary::after { content: "\2212"; }
.us-main blockquote {
  margin-inline: 0; padding: 1.1rem 1.5rem; font-size: 1.15rem;
  background: var(--us-surface); border-left: 4px solid var(--us-accent); border-radius: 0 var(--us-radius) var(--us-radius) 0;
}
.us-main blockquote p:last-of-type { margin-bottom: .5rem; }
.us-main blockquote cite { display: block; color: var(--us-muted); font-style: normal; font-size: .95rem; }
.us-main blockquote cite::before { content: "\2014\00a0"; }
.us-main figure img { display: block; border-radius: var(--us-radius); }
.us-main figcaption { margin-top: .5rem; color: var(--us-muted); font-size: .9rem; }
.us-main form { max-width: 38rem; }
.us-main form label { display: block; margin: 0 0 1rem; font-weight: 600; }
.us-main form input:not([type="checkbox"]):not([type="hidden"]), .us-main form textarea {
  display: block; width: 100%; margin-top: .35rem; padding: .65rem .85rem;
  font: inherit; font-weight: 400; color: var(--us-fg); background: var(--us-bg);
  border: 1px solid var(--us-border); border-radius: var(--us-radius);
}
.us-main form textarea { min-height: 8rem; resize: vertical; }
.us-main form input:focus, .us-main form textarea:focus { outline: 2px solid var(--us-accent); outline-offset: 1px; }
.newsletter { padding: 1.5rem 1.75rem; background: var(--us-surface); border-radius: calc(var(--us-radius) * 1.5); }
.downloads ul { list-style: none; padding: 0; }
.downloads li { padding: .8rem 0; border-bottom: 1px solid var(--us-border); }
.downloads small { color: var(--us-muted); }
.up-posts { list-style: none; padding: 0; margin: 0; max-width: 46rem; }
.up-posts li { padding: 1.25rem 0; border-bottom: 1px solid var(--us-border); }
.up-posts h2 { margin: 0 0 .25rem; font-size: 1.4rem; }
.up-posts h2 a { color: var(--us-fg); text-decoration: none; }
.up-posts h2 a:hover { color: var(--us-accent); }
.up-posts p { margin: .35rem 0; }
.up-tags { display: flex; flex-wrap: wrap; gap: .4rem; }
.up-tags a { padding: .15rem .65rem; border-radius: 999px; background: var(--us-surface); border: 1px solid var(--us-border); color: var(--us-fg); font-size: .85rem; text-decoration: none; }
.up-tags a:hover { border-color: var(--us-accent); }
)CSS";

// The defaults of the element renderer (PaletteFromTheme), by UltraSite's
// token names, and a dark set for a theme that has a dark look.
struct TokenDefault {
    const char* themeName;   // the name in theme.json
    const char* token;       // the UltraSite token
    const char* light;
    const char* dark;
};

const TokenDefault kTokenDefaults[] = {
    {"paper", "bg", "#ffffff", "#121218"},
    {"ink", "fg", "#202020", "#e8e8ee"},
    {"inkSoft", "muted", "#5a5a62", "#a4a4b0"},
    {"accent", "accent", "#a90707", "#ff6b6b"},
    {"accentHover", "accent-hover", "", ""},
    {"onAccent", "accent-fg", "#ffffff", "#ffffff"},
    {"paperSoft", "surface", "#efefef", "#1c1c25"},
    {"rule", "border", "#dcdce0", "#2e2e3a"},
    {"night", "night", "#0d0c22", "#08081a"},
    {"nightSoft", "night-soft", "#18193f", "#14153a"},
    {"onDark", "on-dark", "#d0d0dc", "#d0d0dc"},
    {"tile", "tile", "#e2e4ec", "#24242e"},
};

std::string Lookup(const std::vector<std::pair<std::string, std::string>>& colors, const std::string& name) {
    for (const auto& [key, value] : colors) {
        if (key == name) return value;
    }
    return std::string();
}

#if ULTRAPRESS_HAS_ULTRASITE
std::vector<UltraSite::NavItem> NavItems(const Site& site, const std::vector<MenuItem>& items) {
    std::vector<UltraSite::NavItem> out;
    for (const MenuItem& item : items) {
        UltraSite::NavItem nav;
        nav.label = item.title;
        nav.href = item.url;
        if (nav.href.empty() && !item.page.empty()) {
            if (const Page* target = site.FindPage(item.page)) nav.href = PagePath(*target);
        }
        nav.children = NavItems(site, item.children);
        out.push_back(std::move(nav));
    }
    return out;
}
#endif

#if ULTRAPRESS_HAS_ULTRASITE
// The earlier versions of the docs in `folder`: its subfolders, in the order
// of its versions.json (["2.0", "1.0"], newest first) or, without one,
// newest first by name; each published at `prefix` + its label.
std::vector<UltraSite::DocsVersion> VersionsIn(const fs::path& folder, const fs::path& orderFile, const std::string& prefix) {
    std::vector<UltraSite::DocsVersion> out;
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) return out;
    std::vector<std::string> labels;
    if (fs::is_regular_file(orderFile, ec)) {
        UltraCanvas::JSONParseResult parsed;
        const UltraCanvas::JSONValue order = UltraCanvas::JSON::ParseFile(UltraCanvas::PathToUtf8(orderFile), &parsed);
        if (parsed.success) {
            for (const UltraCanvas::JSONValue& label : order.GetElements()) labels.push_back(label.GetString());
        }
    }
    if (labels.empty()) {
        for (const fs::directory_entry& entry : fs::directory_iterator(folder, ec)) {
            if (entry.is_directory(ec)) labels.push_back(UltraCanvas::PathToUtf8(entry.path().filename()));
        }
        std::sort(labels.rbegin(), labels.rend());
    }
    for (const std::string& label : labels) {
        const fs::path versionFolder = folder / UltraCanvas::PathFromUtf8(label);
        if (!label.empty() && fs::is_directory(versionFolder, ec)) {
            out.push_back({label, UltraCanvas::PathToUtf8(versionFolder), prefix + label + "/"});
        }
    }
    return out;
}

// The blog: an index of the posts, newest first, and a page per tag.
void AddBlog(UltraSite::Builder& builder, const Site& site, bool includeDrafts, std::vector<std::string>& warnings) {
    std::vector<const Page*> posts;
    for (const Page& page : site.pages) {
        if (page.kind == "post" && (page.IsPublished() || includeDrafts)) posts.push_back(&page);
    }
    if (posts.empty()) return;
    if (site.FindPage("blog")) {
        warnings.push_back("the site has a page called blog: the blog's index is left out");
        return;
    }
    std::stable_sort(posts.begin(), posts.end(), [](const Page* a, const Page* b) { return a->date > b->date; });
    auto list = [&](const std::vector<const Page*>& entries) {
        std::string html = "<ul class=\"up-posts\">\n";
        for (const Page* post : entries) {
            html += "<li><article><h2><a href=\"" + UltraCanvas::HTML::EscapeText(builder.Url(PagePath(*post))) + "\">" +
                    UltraCanvas::HTML::EscapeText(post->title) + "</a></h2>";
            if (!post->date.empty()) html += "<p class=\"us-post-date\"><time datetime=\"" + UltraCanvas::HTML::EscapeText(post->date) + "\">" +
                                             UltraCanvas::HTML::EscapeText(post->date) + "</time></p>";
            if (!post->seo.description.empty()) html += "<p>" + UltraCanvas::HTML::EscapeText(post->seo.description) + "</p>";
            if (!post->tags.empty()) {
                html += "<p class=\"up-tags\">";
                for (const std::string& tag : post->tags) {
                    html += "<a href=\"" + UltraCanvas::HTML::EscapeText(builder.Url("/blog/tags/" + MakeSlug(tag) + "/")) + "\">" +
                            UltraCanvas::HTML::EscapeText(tag) + "</a> ";
                }
                html += "</p>";
            }
            html += "</article></li>\n";
        }
        return html + "</ul>\n";
    };
    UltraSite::Route index;
    index.path = "/blog/";
    index.title = "Blog";
    index.description = site.tagline;
    index.bodyHtml = "<h1>Blog</h1>\n" + list(posts);
    builder.AddRoute(std::move(index));
    std::map<std::string, std::pair<std::string, std::vector<const Page*>>> tags;   // slug -> name, posts
    for (const Page* post : posts) {
        for (const std::string& tag : post->tags) {
            auto& entry = tags[MakeSlug(tag)];
            if (entry.first.empty()) entry.first = tag;
            entry.second.push_back(post);
        }
    }
    for (const auto& [slug, entry] : tags) {
        UltraSite::Route page;
        page.path = "/blog/tags/" + slug + "/";
        page.title = entry.first;
        page.noindex = true;   // the posts are indexed on their own pages
        page.bodyHtml = "<h1>" + UltraCanvas::HTML::EscapeText(entry.first) + "</h1>\n<p><a href=\"" +
                        UltraCanvas::HTML::EscapeText(builder.Url("/blog/")) + "\">Blog</a></p>\n" + list(entry.second);
        builder.AddRoute(std::move(page));
    }
}
#endif

} // namespace

const char* BlockStylesheet() { return kBlockStylesheet; }

std::vector<std::pair<std::string, std::string>> ThemeTokens(const Theme& theme, bool dark) {
    const auto& colors = dark ? theme.darkColors : theme.colors;
    std::vector<std::pair<std::string, std::string>> out;
    std::string accent;
    for (const TokenDefault& entry : kTokenDefaults) {
        std::string value = Lookup(colors, entry.themeName);
        if (value.empty()) value = dark ? entry.dark : entry.light;
        if (std::string(entry.token) == "accent") accent = value;
        if (std::string(entry.token) == "accent-hover" && value.empty()) {
            // The renderer darkens the accent by a quarter; CSS mixes it.
            value = "color-mix(in srgb, " + accent + " 78%, #000)";
        }
        out.emplace_back(entry.token, value);
    }
    if (!dark && !theme.fontFamily.empty()) {
        std::string family;
        for (char c : theme.fontFamily) {
            if (c != '"' && c != '\\') family += c;
        }
        // The site's family where the visitor has it; nothing is downloaded.
        out.emplace_back("font", "\"" + family + "\", system-ui, -apple-system, \"Segoe UI\", Roboto, Cantarell, \"Noto Sans\", sans-serif");
    }
    return out;
}

bool HasHtmlOutput() {
#if ULTRAPRESS_HAS_ULTRASITE
    return true;
#else
    return false;
#endif
}

PublishResult PublishHtml(const Site& site, const std::string& siteDirectory,
                          const std::string& outputDirectory, const PublishOptions& options) {
    PublishResult result;
    result.issues = ValidateSite(site);
#if ULTRAPRESS_HAS_ULTRASITE
    std::error_code ec;
    UltraSite::SiteConfig config;
    config.url = site.url;
    config.name = site.name;
    config.tagline = site.tagline;
    config.language = site.language.empty() ? "en" : site.language;
    config.tokens.light = ThemeTokens(site.theme, false);
    if (!site.theme.darkColors.empty()) config.tokens.dark = ThemeTokens(site.theme, true);
    if (const Menu* main = site.FindMenu("main")) config.navbar = NavItems(site, main->items);
    if (const Menu* footer = site.FindMenu("footer")) config.footer = NavItems(site, footer->items);
    config.footerText = site.owner.empty() ? site.name : site.owner;
    config.extraCss = kBlockStylesheet;
    config.generator = options.html.generator.empty() ? "UltraPress" : options.html.generator;
    // Downloads and the forms' endpoints are never prerendered.
    config.noPrefetch = {"/media/", "/api/"};
    // An HTML block may embed a video or a map from another site.
    config.cspAdditions = {{"frame-src", "https:"}, {"media-src", "https:"}};
    // A site with documentation can be searched.
    config.search = fs::is_directory(UltraCanvas::PathFromUtf8(siteDirectory) / kDocsFolder, ec);

    HtmlOptions html = options.html;
    html.styled = true;
    html.includeStyle = false;
    html.rendererScript.clear();
    if (html.siteDirectory.empty()) html.siteDirectory = siteDirectory;
    UltraSite::Builder builder(config);
    // The site's pictures as <picture>s with their variants, the original
    // where the media folder puts it; each role at the widths it is shown.
    html.picture = [&builder, siteDirectory](const std::string& mediaPath, const std::string& alt, const std::string& role) {
        const std::string relative = mediaPath[0] == '/' ? mediaPath.substr(1) : mediaPath;
        const fs::path source = UltraCanvas::PathFromUtf8(siteDirectory) / UltraCanvas::PathFromUtf8(relative);
        std::error_code exists;
        if (!fs::is_regular_file(source, exists)) return std::string();
        UltraSite::PictureOptions picture;
        picture.alt = alt;
        picture.originalUrl = builder.Url("/" + relative);
        picture.first = role == "hero";
        if (role == "hero") {
            picture.widths = {480, 800, 1200};
            picture.sizes = "(min-width: 60rem) 40vw, 100vw";
        } else if (role == "card") {
            picture.widths = {400, 800};
            picture.sizes = "(min-width: 60rem) 24rem, 100vw";
        } else {
            picture.widths = {480, 960, 1440};
            picture.sizes = "(min-width: 72rem) 72rem, 100vw";
        }
        return builder.Picture(UltraCanvas::PathToUtf8(source), picture);
    };
    HtmlEmitter emitter(site, html);

    const std::string feedLink = "<link rel=\"alternate\" type=\"application/rss+xml\" title=\"" +
                                 UltraCanvas::HTML::EscapeText(site.name) + "\" href=\"/feed.xml\">";
    for (const Page& page : site.pages) {
        if (!page.IsPublished() && !options.includeDrafts) continue;
        const PageParts parts = emitter.Parts(page);
        UltraSite::Route route;
        route.path = PagePath(page);
        route.language = page.language;
        route.layout = page.kind == "post" ? "post" : "page";
        route.kind = page.kind == "post" ? "article" : "website";
        route.title = page.title;
        route.documentTitle = parts.documentTitle;
        route.description = parts.description;
        route.image = parts.image;
        route.imageWidth = parts.imageWidth;
        route.imageHeight = parts.imageHeight;
        route.date = page.date;
        route.noindex = page.seo.noindex;
        route.jsonLd = parts.jsonLd;
        route.extraHead = feedLink;
        // A page states its title as its <h1> unless its hero does; the
        // front page's hero is its title, and the post layout writes one.
        const bool heroFirst = !page.blocks.empty() && page.blocks.front().type == "hero";
        route.bodyHtml = route.layout == "page" && page.slug != "home" && !heroFirst
                             ? "<h1>" + UltraCanvas::HTML::EscapeText(page.title) + "</h1>\n" + parts.body
                             : parts.body;
        if (page.kind == "post" && !page.tags.empty()) {
            route.bodyHtml += "<p class=\"up-tags\">";
            for (const std::string& tag : page.tags) {
                route.bodyHtml += "<a href=\"" + UltraCanvas::HTML::EscapeText(builder.Url("/blog/tags/" + MakeSlug(tag) + "/")) + "\">" +
                                  UltraCanvas::HTML::EscapeText(tag) + "</a> ";
            }
            route.bodyHtml += "</p>\n";
        }
        builder.AddRoute(std::move(route));
    }
    builder.AddFile("feed.xml", emitter.Feed());
    AddBlog(builder, site, options.includeDrafts, result.warnings);
    const fs::path media = UltraCanvas::PathFromUtf8(siteDirectory) / "media";
    if (options.copyMedia && fs::is_directory(media, ec)) builder.AddDirectory(UltraCanvas::PathToUtf8(media), "media");

    // The documentation, and its translations.
    const fs::path docs = UltraCanvas::PathFromUtf8(siteDirectory) / kDocsFolder;
    if (fs::is_directory(docs, ec)) {
        UltraSite::DocsOptions docsOptions;
        docsOptions.folder = UltraCanvas::PathToUtf8(docs);
        docsOptions.routePrefix = "/docs/";
        for (const std::string& language : site.languages) {
            const fs::path translated = UltraCanvas::PathFromUtf8(siteDirectory) / "i18n" / UltraCanvas::PathFromUtf8(language) / kDocsFolder;
            if (language != site.language && fs::is_directory(translated, ec)) {
                docsOptions.translations.push_back({language, UltraCanvas::PathToUtf8(translated), "/" + language + "/docs/"});
            }
        }
        // Earlier versions: versioned_docs/<label>, ordered by versions.json.
        docsOptions.versions = VersionsIn(UltraCanvas::PathFromUtf8(siteDirectory) / "versioned_docs",
                                          UltraCanvas::PathFromUtf8(siteDirectory) / "versions.json", "/docs/");
        const UltraSite::DocsReport docsReport = builder.AddDocs(docsOptions);
        result.errors.insert(result.errors.end(), docsReport.errors.begin(), docsReport.errors.end());
        result.warnings.insert(result.warnings.end(), docsReport.warnings.begin(), docsReport.warnings.end());
    }

    UltraSite::BuildOptions build;
    build.removeStale = options.removeStale;
    build.keep = {".ultrapress-deploy.json"};
    build.enforceBudgets = options.enforceBudgets;
    build.precompress = options.precompress;
    const UltraSite::BuildResult built = builder.Build(outputDirectory, build);
    result.ok = built.ok && result.errors.empty();
    result.files = built.files;
    result.removed = built.removed;
    result.errors.insert(result.errors.end(), built.errors.begin(), built.errors.end());
    result.warnings.insert(result.warnings.end(), built.warnings.begin(), built.warnings.end());
    result.sizeReport = built.SizeReport();
#else
    (void)siteDirectory;
    (void)outputDirectory;
    (void)options;
    result.errors.push_back("this build has no UltraSite, so no HTML + JavaScript output: publish the WebAssembly output (--output wasm)");
#endif
    return result;
}

PublishResult PublishDocsFolder(const DocsSiteOptions& options, const std::string& outputDirectory) {
    PublishResult result;
#if ULTRAPRESS_HAS_ULTRASITE
    UltraSite::SiteConfig config;
    config.url = options.url;
    while (!config.url.empty() && config.url.back() == '/') config.url.pop_back();
    config.name = options.name;
    config.language = options.language;
    config.search = true;
    config.generator = "UltraPress";
    // UltraSite's own light look, and a dark one.
    config.tokens.dark = {{"bg", "#101218"}, {"fg", "#e6e7eb"}, {"muted", "#9aa0ab"}, {"accent", "#7aa7ff"},
                          {"accent-fg", "#0b1020"}, {"surface", "#1a1d26"}, {"border", "#2b2f3a"}};
    UltraSite::Builder builder(config);
    UltraSite::DocsOptions docs;
    docs.folder = options.folder;
    docs.routePrefix = "/";
    docs.editUrl = options.editUrl;
    docs.sourceRoot = options.sourceRoot;
    docs.sourceUrl = options.sourceUrl;
    docs.exclude = options.exclude;
    if (!options.versionsFolder.empty()) {
        const fs::path versions = UltraCanvas::PathFromUtf8(options.versionsFolder);
        docs.versions = VersionsIn(versions, versions / "versions.json", "/");
        if (docs.versions.empty()) result.warnings.push_back(options.versionsFolder + " holds no version folders");
    }
    const UltraSite::DocsReport report = builder.AddDocs(docs);
    result.errors = report.errors;
    result.warnings = report.warnings;
    UltraSite::BuildOptions build;
    build.removeStale = options.removeStale;
    build.keep = {".ultrapress-deploy.json"};
    build.enforceBudgets = options.enforceBudgets;
    build.precompress = options.precompress;
    build.brokenLinksWarn = options.brokenLinksWarn;
    const UltraSite::BuildResult built = builder.Build(outputDirectory, build);
    result.ok = built.ok && result.errors.empty();
    result.files = built.files;
    result.removed = built.removed;
    result.errors.insert(result.errors.end(), built.errors.begin(), built.errors.end());
    result.warnings.insert(result.warnings.end(), built.warnings.begin(), built.warnings.end());
    result.sizeReport = built.SizeReport();
#else
    (void)options;
    (void)outputDirectory;
    result.errors.push_back("this build has no UltraSite, so no documentation sites");
#endif
    return result;
}

PublishResult Publish(const Site& site, const std::string& siteDirectory,
                      const std::string& outputDirectory, const PublishOptions& options) {
    const std::string output = options.output.empty() ? site.output : options.output;
    if (output == kOutputWasm) return PublishStatic(site, siteDirectory, outputDirectory, options);
    if (output != kOutputHtml) {
        PublishResult result;
        result.errors.push_back("unknown output '" + output + "': html or wasm");
        return result;
    }
    return PublishHtml(site, siteDirectory, outputDirectory, options);
}

} // namespace UltraPress
