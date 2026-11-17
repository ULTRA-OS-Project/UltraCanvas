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
};

struct SiteConfig {
    std::string url;                         // "https://example.org", no trailing slash: canonical links, sitemap
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
    // The 404 page, when no route has the path "/404.html".
    std::string notFoundTitle = "Page not found";
    std::string notFoundText = "There is nothing at this address.";
    std::string themeToggleLabel = "Switch between the dark and the light look";
    std::string skipLinkLabel = "Skip to the content";
    std::string mainNavLabel = "Main";           // the navbar's <nav aria-label>
    std::string footerNavLabel = "Footer";
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
    bool noindex = false;                    // <meta name="robots" content="noindex">, left out of the sitemap
};

// What a layout writes into <main>, given the route.
using LayoutFunction = std::function<std::string(const Route&)>;

struct BuildOptions {
    bool removeStale = true;                 // delete what an earlier build wrote and this one did not
    std::vector<std::string> keep;           // relative paths removeStale leaves alone (a deploy manifest)
    bool checkLinks = true;                  // a broken internal link or anchor is an error
    bool enforceBudgets = true;              // over budget is an error; off, a warning
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

    // A file published under its content hash: "assets/<stem>.<hash10>.<ext>",
    // cached for a year. Returns its URL ("/assets/logo.3f9a1c2b7e.png"), or
    // empty (and an error at Build) when the file cannot be read.
    std::string Asset(const std::string& sourcePath);
    // The same for content in memory, named `fileName`.
    std::string AssetFromText(const std::string& fileName, const std::string& content);

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

    // Writes the site into `outputDirectory`, created when missing.
    BuildResult Build(const std::string& outputDirectory, const BuildOptions& options = {});

    // The whole page of one route, as Build writes it (asset URLs included).
    // For previews and tests; the route need not have been added.
    std::string RenderPage(const Route& route);

    const SiteConfig& Config() const { return config; }

private:
    struct Prepared;
    void Prepare(Prepared& prepared, std::vector<std::string>& errors);
    std::string Page(const Route& route, const Prepared& prepared, std::vector<std::string>& errors) const;
    std::string Head(const Route& route, const Prepared& prepared, const std::vector<std::string>& behaviours) const;

    SiteConfig config;
    std::vector<Route> routes;
    std::map<std::string, LayoutFunction> layouts;
    std::map<std::string, std::string> scripts;                     // behaviour -> source
    struct PendingAsset { std::string sourcePath, content, url; bool fromFile = true; };
    std::vector<PendingAsset> assets;
    std::vector<std::pair<std::string, std::string>> directories;   // source, prefix
    std::vector<std::pair<std::string, std::string>> files;         // relative path, content
    std::vector<std::string> assetErrors;
};

// ----- Helpers the apps share ---------------------------------------------------

// The file a route path is written to: "/" -> "index.html", "/a/b/" ->
// "a/b/index.html", "/x.html" -> "x.html", "/a" -> "a/index.html".
std::string RouteFile(const std::string& path);

// The first `length` hex digits of the content's SHA-256 (FNV-1a when the
// build has no libsodium; the build then warns).
std::string ContentHash(const std::string& content, size_t length = 10);

// Bytes after gzip -9 (raw size without zlib).
size_t CompressedSize(const std::string& content);

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
