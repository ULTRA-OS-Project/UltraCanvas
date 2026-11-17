// Apps/UltraPress/engine/UltraPressHtml.h
// The HTML emitter: the plain, script-free HTML shadow of every page - what
// search engines, link previews, mail clients, screen readers and a browser
// without WebAssembly read - plus sitemap.xml, robots.txt and the RSS feed.
// It carries no layout and no styling of the site's own beyond a short
// built-in stylesheet that makes it read as a document; the WebAssembly
// renderer takes the viewport over when it has started (Phase 1 of
// Docs/Research/UltraPressDesignProposal.md, §4.4). Decided 2026-10-13.
// Version: 0.3.0 - HtmlOptions::styled and Parts(): the body and the head's facts for the HTML + JavaScript output
// Version: 0.2.0 - HtmlOptions::siteDirectory: width and height on every <img>, the social card's size
// Version: 0.1.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraPressBlocks.h"
#include "UltraPressModel.h"

#include <string>
#include <vector>

namespace UltraPress {

struct HtmlOptions {
    // The renderer's loader script (e.g. "ultrapress-0.1.0.js"), referenced
    // from every page with the page's slug in <meta name="ultrapress-page">.
    // Empty: the shadow alone, which is what Phase 1 publishes.
    std::string rendererScript;
    // The UltraWeb app of the site ("site.wasm"), as <link rel="ultraweb-app">
    // (UltraWebProposal.md §4.3). Empty: none.
    std::string ultrawebApp;
    // The site folder: with it, every <img> of a media picture carries its
    // width and height (read from the file's header, UltraPressMedia.h), so
    // a browser lays the page out before the picture arrives, and the
    // social card names its size. Empty: no sizes.
    std::string siteDirectory;
    bool includeStyle = true;          // the built-in stylesheet
    std::string generator = "UltraPress";
    // The markup of the HTML + JavaScript output (UltraPressSite.h) rather
    // than the shadow's: the hero's picture fetched first and every other
    // lazily, the FAQ an accordion that keeps one answer open. The text,
    // the links and the order are the shadow's.
    bool styled = false;
};

// What the HTML + JavaScript output takes from a page: its blocks as HTML
// and the facts its <head> states.
struct PageParts {
    std::string body;                      // the blocks, in order
    std::string documentTitle;             // "<page> - <site>", or "<site> - <tagline>" on the front page
    std::string description;               // the page's, or the site's tagline
    std::string image;                     // the social card: an absolute URL, or empty
    int imageWidth = 0, imageHeight = 0;   // its size, when the site folder gives it
    std::vector<std::string> jsonLd;       // WebSite, Article, FAQPage, as the page calls for
};

class HtmlEmitter {
public:
    explicit HtmlEmitter(const Site& site, HtmlOptions options = {},
                         const BlockRegistry& registry = BlockRegistry::BuiltIn());

    // The whole document of one page.
    std::string Page(const Page& page) const;
    // Every published page that is not noindex, with its date as lastmod.
    std::string Sitemap() const;
    // Allows everything and names the sitemap.
    std::string Robots() const;
    // RSS 2.0 of the published posts, newest first.
    std::string Feed() const;

    // The page's parts, for the HTML + JavaScript output.
    PageParts Parts(const UltraPress::Page& page) const;

    // What a block becomes, on its own (for tests and previews).
    std::string Block(const UltraPress::Block& block) const;

private:
    struct PageContext;
    void EmitBlocks(const std::vector<UltraPress::Block>& blocks, PageContext& context) const;
    void EmitBlock(const UltraPress::Block& block, PageContext& context) const;
    void EmitMenu(const Menu* menu, const std::string& label, std::string& out) const;
    std::string Head(const UltraPress::Page& page, const PageContext& context) const;
    std::string DocumentTitle(const UltraPress::Page& page) const;
    std::string Description(const UltraPress::Page& page) const;
    std::string SocialImage(const UltraPress::Page& page, int& width, int& height) const;
    std::vector<std::string> StructuredData(const UltraPress::Page& page, const PageContext& context) const;
    std::string Img(const std::string& src, const std::string& alt, bool first) const;

    const Site& site;
    HtmlOptions options;
    const BlockRegistry& registry;
};

// Built-in helpers the emitter and the newsletter writer share.
std::string Paragraphs(const std::string& longText);          // blank-line-separated text as <p>s, newlines as <br>
std::string Rfc822Date(const std::string& isoDate);           // "2026-10-13" -> "Tue, 13 Oct 2026 00:00:00 +0000"; empty when not a date

} // namespace UltraPress
