// UltraSite/include/UltraSite/UltraSite.h
// UltraSite - the HTML + JavaScript output of a website: a list of routes in,
// a folder any static host serves out. Every route is written as a complete,
// styled HTML page; one content-hashed stylesheet and a small set of
// content-hashed JavaScript modules - the core (prefetch, theme) on every
// page, a behaviour module only on the pages whose body asks for it with
// data-us="<behaviour>" - are shared by all of them and cached for a year.
// Beside them: sitemap.xml, robots.txt, 404.html, .htaccess and _headers.
// The build checks every link and anchor inside the site and the size of
// every page and asset against a budget, and fails over either.
//
// The site works with JavaScript turned off: the scripts only add behaviour
// to HTML that already works. No script, style or font comes from another
// origin, and the pages run under a strict Content-Security-Policy.
//
// UltraPress (option (b) of its two outputs) and the UltraStore admin
// backend publish through it. Design:
// Docs/Research/UltraSiteStaticSitesProposal.md; reference:
// Docs/Modules/UltraSite/README.md.
//
// Headless: links the core library (JSON, HTMLReader, UltraCrypt) and zlib,
// opens no window.
//
// Version: 0.4.0 - BuildOptions::precompress (.gz, and .br with Brotli), GzipBytes, BrotliBytes; the preview
//                  server (UltraSiteServer.h); Picture (responsive variants); versioned docs
// Version: 0.3.0 - a site under a path (its url's): Url(), BasePath(); every address the builder writes
//                  carries the path, the link check reads it back; files already in the folder count
//                  when nothing is swept (BuildOptions::removeStale off)
// Version: 0.2.0 - Phase B: the doc layout (sidebar, table of contents, breadcrumbs, previous/next),
//                  AddDocs (a Markdown folder, translations), search, the toc / tabs / copy / search
//                  modules, the language switcher, Labels
// Version: 0.1.0 - Phase A: routes, page and post layouts, tokens, hashed assets,
//                  the core script, behaviour modules, link check, budgets
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace UltraSite {

// One entry of the navbar or the footer. `href` is a site path ("/about/"),
// an absolute URL, or empty for a heading over its children.
struct NavItem {
    std::string label;
    std::string href;
    std::vector<NavItem> children;
};

// The design tokens, written as CSS custom properties `--us-<name>` on
// :root. The base stylesheet reads these names; an app's extraCss may read
// any other it defines:
//   bg, fg, muted, accent, accent-fg, surface, border    colours
//   font, font-heading, font-mono                        font stacks
//   radius, width                                        lengths
// A name missing from `light` keeps the base stylesheet's default. `dark`
// is the same names for the dark look; empty, the site has no dark look,
// no theme toggle, and its pages ask the browser for a light scheme.
// Names are [a-z0-9-]; a value carrying ; { } < > is refused by the build.
struct Tokens {
    std::vector<std::pair<std::string, std::string>> light;
    std::vector<std::pair<std::string, std::string>> dark;
};

// The budgets of §3.1 of the design, in bytes, compressed (gzip at level 9,
// which is larger than Brotli, so a page under budget here is under it on a
// Brotli host too) - raw where the build has no zlib. Zero: not checked.
struct Budgets {
    size_t pageHtml = 30 * 1024;
    size_t stylesheet = 20 * 1024;
    size_t coreScript = 4 * 1024;
    size_t behaviourScript = 3 * 1024;
    size_t pageScripts = 15 * 1024;     // all script one page loads
    size_t inlineScripts = 1024;        // raw: the theme script and the speculation rules, which block nothing but are in every page
    size_t searchIndex = 150 * 1024;    // one language's search index, fetched when the visitor first searches
};

// The words the site's frame says, in the site's language: an app writing
// a German site sets them in German.
struct Labels {
    std::string skipLink = "Skip to the content";
    std::string mainNav = "Main";                    // the navbar's <nav aria-label>
    std::string footerNav = "Footer";
    std::string themeToggle = "Switch between the dark and the light look";
    std::string notFoundTitle = "Page not found";    // the 404 page, when no route has the path "/404.html"
    std::string notFoundText = "There is nothing at this address.";
    std::string search = "Search";                   // the search box's label, the search page's title
    std::string searchNoScript = "Searching needs JavaScript. Every page of the site is listed below.";
    std::string searchNothing = "Nothing found for";
    std::string toc = "On this page";
    std::string docsNav = "Pages";                   // the doc layout's sidebar <nav aria-label>
    std::string menu = "Menu";                       // opens the sidebar on a phone
    std::string close = "Close";
    std::string breadcrumbs = "Breadcrumbs";
    std::string pagination = "More pages";
    std::string previous = "Previous";
    std::string next = "Next";
    std::string edit = "Edit this page";
    std::string lastUpdated = "Last updated";
    std::string copy = "Copy";                       // a code block's copy button
    std::string copied = "Copied";
    std::string languages = "Languages";             // the language switcher's <nav aria-label>
    std::string versions = "Versions";               // the doc layout's version switcher
    std::string outdated = "This is the documentation of version {0}.";   // above an earlier version's page
    std::string latest = "Read the latest version";
};

// The frame of the pages in one language: its navbar, footer and words
// (SiteConfig::frames). A language without one takes SiteConfig's own.
struct Frame {
    std::vector<NavItem> navbar;
    std::vector<NavItem> footer;
    std::string footerText;
    Labels labels;
};

// A heading of a page, for its table of contents.
struct Heading {
    int level = 2;
    std::string text;
    std::string id;
};

struct SiteConfig {
    // "https://example.org", no trailing slash: canonical links, sitemap.
    // A site under a path ("https://store.example/v1") is served from that
    // path: routes keep their site paths ("/app/x/"), and the builder writes
    // every address with the path in front ("/v1/app/x/") - an app writing
    // a body does the same with Builder::Url.
    std::string url;
    std::string name;                        // the brand in the navbar, og:site_name, the default <title>
    std::string tagline;
    std::string logo;                        // a picture file: hashed into assets/ and shown before the name; empty: the name alone
    std::string language = "en";             // <html lang> where a route names none
    Tokens tokens;
    std::vector<NavItem> navbar;
    std::vector<NavItem> footer;
    std::string footerText;                  // a line under the footer's links (text, escaped)
    std::string extraCss;                    // the app's own rules: its blocks and layouts, after the base stylesheet
    Budgets budgets;
    std::string generator = "UltraSite";     // <meta name="generator">
    // Paths that are never prefetched or prerendered: downloads, endpoints.
    // "/assets/" is always among them.
    std::vector<std::string> noPrefetch;
    // Sources added to the Content-Security-Policy, per directive
    // ({"frame-src", "https://www.youtube-nocookie.com"}).
    std::vector<std::pair<std::string, std::string>> cspAdditions;
    Labels labels;
    // A search box in the navbar, an index per language and a /search/
    // page that lists every page (what a visitor without script gets).
    bool search = false;
    // The language switcher's names: "de" -> "Deutsch". A language not
    // named shows its code.
    std::map<std::string, std::string> languageNames;
    // The frame of another language's pages - its navbar leading into its
    // own pages, its words - by language ("de"). A route takes its
    // language's; the site's own language, and any other without one,
    // takes navbar, footer, footerText and labels above.
    std::map<std::string, Frame> frames;
};

struct Route {
    // "/" and "/about/" are written as index.html and about/index.html;
    // "/404.html" as itself. A path starts with "/".
    std::string path;
    std::string language;                    // empty: SiteConfig::language
    std::string layout = "page";             // "page", "post", or one an app added with AddLayout
    std::string title;                       // the page's own title: og:title, the post layout's <h1>
    std::string documentTitle;               // <title>; empty: "<title> - <site name>", or the site name for an untitled page
    std::string description;                 // <meta name="description"> and the social card
    std::string image;                       // the social card's picture: an absolute URL
    int imageWidth = 0, imageHeight = 0;     // its size, when known
    std::string bodyHtml;                    // what goes into <main>, written by the app; data-us="..." asks for behaviours
    std::string date;                        // YYYY-MM-DD: the post layout's date, the sitemap's lastmod
    std::string kind = "website";            // og:type: "website" or "article"
    std::vector<std::string> jsonLd;         // each a JSON object, written as <script type="application/ld+json">
    std::map<std::string, std::string> alternates;   // language -> path of this page in that language (hreflang)
    std::string extraHead;                   // more <head> lines, written as they are (a feed's <link>)
    bool noindex = false;                    // <meta name="robots" content="noindex">, left out of the sitemap and the search
    // The doc layout's: the sidebar it shows (AddSidebar), its table of
    // contents (empty: the body's h2 and h3 that carry an id), the "Edit
    // this page" link.
    std::string sidebar;
    std::vector<Heading> toc;
    std::string editUrl;
    // The doc layout's versions: this page's ("2.0"), every version's
    // label and the address of this page in it (its root where the page is
    // not there), newest first, and for an earlier version's page the
    // latest version's address, which a banner above it names.
    std::string version;
    std::vector<std::pair<std::string, std::string>> versions;
    std::string latestPath;
    bool searchable = true;                  // in the search index
};

// A Markdown folder as doc pages (Builder::AddDocs), the way Docusaurus
// reads its docs/: a file is a page at routePrefix + its path without
// ".md" ("guide/install.md" -> "/docs/guide/install/"), a folder's index.md
// or README.md is the folder's page, and the sidebar follows the folders,
// ordered by `sidebar_position` and then by name. Front matter between
// "---" lines sets title, description, slug, sidebar_label,
// sidebar_position and draft (true: left out). A folder's _category_.json
// gives its label and position. A link to another .md file becomes that
// page's address; a link to another file in the folder publishes it as an
// asset; a link out of the folder goes to sourceUrl when one is set.
struct DocsTranslation {
    std::string language;                    // "de"
    std::string folder;                      // the same files, translated
    std::string routePrefix;                 // "/de/docs/"
};

// An earlier version of the docs: the same folder as it was at that
// release, published at its own prefix with its own sidebar.
struct DocsVersion {
    std::string label;                       // "1.0"
    std::string folder;
    std::string routePrefix;                 // "/docs/1.0/"
};

struct DocsOptions {
    std::string folder;
    std::string routePrefix = "/docs/";
    std::string language;                    // the pages' language; empty: the site's
    std::string sidebar = "docs";            // the sidebar's id; a translation's is "<sidebar>-<language>"
    std::string editUrl;                     // + a file's path in the folder: its "Edit this page" link
    std::string sourceRoot;                  // a link to a file outside the folder: sourceUrl + its path under sourceRoot
    std::string sourceUrl;
    std::vector<std::string> exclude;        // file and folder names left out wherever they are ("CHANGELOG.md")
    std::vector<DocsTranslation> translations;
    // Earlier versions, newest first. With any, `folder` is the latest
    // version, named currentVersion; an earlier version's pages say so, link
    // to the latest, and are left out of the search.
    std::vector<DocsVersion> versions;
    std::string currentVersion = "Latest";
};

struct DocsReport {
    size_t pages = 0;
    std::vector<std::string> errors;         // a file that cannot be read, two files for one address
    std::vector<std::string> warnings;       // a link that leads nowhere the site knows
};

// How Builder::Picture writes a picture.
struct PictureOptions {
    std::string alt;                                   // what it shows, for those who cannot see it
    std::string cssClass;
    std::vector<int> widths = {480, 960, 1440};       // the variants, those below the picture's own width
    std::string sizes = "100vw";                       // how wide it is shown: the <img> sizes attribute
    bool first = false;                                // the page's large first picture: fetched at once
    std::string originalUrl;                           // where the original is served; empty: as a hashed asset
};

// What a layout writes into <main>, given the route.
using LayoutFunction = std::function<std::string(const Route&)>;

struct BuildOptions {
    // Delete what an earlier build wrote and this one did not. Off, a file
    // already in the folder is a target links may name (a folder the site
    // shares with other files: a store's catalog and media).
    bool removeStale = true;
    std::vector<std::string> keep;           // relative paths removeStale leaves alone (a deploy manifest)
    bool checkLinks = true;                  // a broken internal link or anchor is an error
    bool brokenLinksWarn = false;            // ... or only a warning (Docusaurus's onBrokenLinks: 'warn')
    bool enforceBudgets = true;              // over budget is an error; off, a warning
    // Beside every page, stylesheet, script, index and list of the site, its
    // .gz copy - and its .br copy where the build has Brotli - for a server
    // that sends a file's compressed copy itself (nginx gzip_static /
    // brotli_static, Caddy's precompressed, the preview server).
    bool precompress = false;
};

struct SizeEntry {
    std::string path;                        // relative to the output folder
    size_t raw = 0;
    size_t compressed = 0;                   // gzip -9; equal to raw without zlib
    size_t budget = 0;                       // 0: none
    bool OverBudget() const { return budget != 0 && compressed > budget; }
};

struct BuildResult {
    bool ok = false;
    std::vector<std::string> files;          // written, relative to the output folder, '/'-separated
    std::vector<std::string> removed;
    std::vector<std::string> errors;         // a file that could not be written, a broken link, a budget overrun
    std::vector<std::string> warnings;
    std::vector<SizeEntry> sizes;
    std::string SizeReport() const;          // a table: path, raw, compressed, budget
};

class Builder {
public:
    explicit Builder(SiteConfig config);
    Builder(const Builder&) = delete;                // the built-in layouts hold the builder
    Builder& operator=(const Builder&) = delete;

    // A file published under its content hash: "assets/<stem>.<hash10>.<ext>",
    // cached for a year. Returns its URL ("/assets/logo.3f9a1c2b7e.png",
    // under the site's path when it has one), or
    // empty (and an error at Build) when the file cannot be read.
    std::string Asset(const std::string& sourcePath);
    // The same for content in memory, named `fileName`.
    std::string AssetFromText(const std::string& fileName, const std::string& content);

    // A picture as <picture>: AVIF and WebP variants at the widths below
    // its own (written by Build under assets/img/, named by the picture's
    // content hash, so a later build reuses them), and the original as the
    // <img>, with its width and height so the page does not shift, fetched
    // lazily unless it is the page's first. A picture the image library
    // cannot read (or a build without it) is a plain <img>.
    std::string Picture(const std::string& sourcePath, const PictureOptions& options = {});

    // A folder copied as it is under `publishedPrefix` ("media"), not
    // hashed: an app's media library. Links into it are checked against
    // its files.
    void AddDirectory(const std::string& sourceDirectory, const std::string& publishedPrefix);
    // A file written as it is at `relativePath` ("feed.xml").
    void AddFile(const std::string& relativePath, std::string content);

    // A layout of the app's own, used by routes that name it.
    void AddLayout(const std::string& name, LayoutFunction layout);
    // A behaviour module: an ES module run on the pages whose body has an
    // element with data-us="<behaviour>" (several separated by spaces). It
    // finds those elements itself (document.querySelectorAll('[data-us~=
    // "<behaviour>"]')) and enhances them. Replaces one of the same name.
    void AddScript(const std::string& behaviour, std::string source);

    void AddRoute(Route route);

    // A sidebar the doc layout shows on the routes that name it.
    void AddSidebar(const std::string& id, std::vector<NavItem> items);
    // A Markdown folder (and its translations) as doc pages with their
    // sidebars (DocsOptions).
    DocsReport AddDocs(const DocsOptions& options);

    // Writes the site into `outputDirectory`, created when missing.
    BuildResult Build(const std::string& outputDirectory, const BuildOptions& options = {});

    // The whole page of one route, as Build writes it (asset URLs included).
    // For previews and tests; the route need not have been added.
    std::string RenderPage(const Route& route);

    const SiteConfig& Config() const { return config; }
    // The path the site is served from: "/", or "/v1/" for a url of
    // "https://store.example/v1".
    const std::string& BasePath() const { return basePath; }
    // A site path as the pages write it: "/app/x/" -> "/v1/app/x/". Any
    // other text (an absolute URL, "#top") is returned as it is.
    std::string Url(const std::string& sitePath) const;

    struct PageFacts;      // what reading a page back finds (the build's own)

private:
    struct Prepared;
    void Prepare(Prepared& prepared, std::vector<std::string>& errors);
    std::string Page(const Route& route, const Prepared& prepared, std::vector<std::string>& errors,
                     PageFacts* facts = nullptr) const;
    std::string Head(const Route& route, const Prepared& prepared, const std::vector<std::string>& behaviours) const;
    std::string DocLayout(const Route& route) const;
    Frame FrameFor(const std::string& language) const;
    std::string SearchForm(const Route& route, const Prepared& prepared, bool large) const;
    Route SearchPage(const Prepared& prepared) const;

    SiteConfig config;
    std::string basePath = "/";
    std::vector<Route> routes;
    std::map<std::string, LayoutFunction> layouts;
    std::map<std::string, std::string> scripts;                     // behaviour -> source
    std::map<std::string, std::vector<NavItem>> sidebars;
    struct PendingAsset { std::string sourcePath, content, url, file; };
    std::vector<PendingAsset> assets;
    struct PendingVariant { std::string sourcePath, file, format; int width = 0; };
    std::vector<PendingVariant> variants;
    std::vector<std::string> pictureNotes;   // a format the image library cannot write
    std::vector<std::pair<std::string, std::string>> directories;   // source, prefix
    std::vector<std::pair<std::string, std::string>> files;         // relative path, content
    std::vector<std::string> assetErrors;
};

// ----- Helpers the apps share ---------------------------------------------------

// The path a site with this address is served from:
// "https://store.example/v1" -> "/v1/", "https://example.org" -> "/".
std::string BasePathOf(const std::string& url);

// The file a route path is written to: "/" -> "index.html", "/a/b/" ->
// "a/b/index.html", "/x.html" -> "x.html", "/a" -> "a/index.html".
std::string RouteFile(const std::string& path);

// The first `length` hex digits of the content's SHA-256 (FNV-1a when the
// build has no libsodium; the build then warns).
std::string ContentHash(const std::string& content, size_t length = 10);

// Bytes after gzip -9 (raw size without zlib).
size_t CompressedSize(const std::string& content);

// The content as a .gz file (gzip -9) and as a .br file (Brotli at its best
// quality); empty when the build has no zlib, or no Brotli.
std::string GzipBytes(const std::string& content);
std::string BrotliBytes(const std::string& content);
bool BrotliAvailable();

// The CSS of a token set: ":root{--us-bg:#fff;...}" and the dark rules.
// Throws nothing; a bad name or value is skipped and reported in `errors`.
std::string TokensCss(const Tokens& tokens, std::vector<std::string>& errors);

// Comments out, whitespace collapsed; strings are left alone.
std::string MinifyCss(const std::string& css);
// Lines that are only a // comment removed, indentation removed. The
// module sources are written so that this is safe: a // comment never
// follows code on its line.
std::string StripScript(const std::string& js);

} // namespace UltraSite
