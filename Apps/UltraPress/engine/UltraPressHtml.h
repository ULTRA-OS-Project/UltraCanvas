// Apps/UltraPress/engine/UltraPressHtml.h
// The HTML emitter: the plain, script-free HTML shadow of every page - what
// search engines, link previews, mail clients, screen readers and a browser
// without WebAssembly read - plus sitemap.xml, robots.txt and the RSS feed.
// It carries no layout and no styling of the site's own beyond a short
// built-in stylesheet that makes it read as a document; the WebAssembly
// renderer takes the viewport over when it has started (Phase 1 of
// Docs/Research/UltraPressDesignProposal.md, §4.4). Decided 2026-10-13.
// Version: 0.1.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraPressBlocks.h"
#include "UltraPressModel.h"

#include <string>

namespace UltraPress {

struct HtmlOptions {
    // The renderer's loader script (e.g. "ultrapress-0.1.0.js"), referenced
    // from every page with the page's slug in <meta name="ultrapress-page">.
    // Empty: the shadow alone, which is what Phase 1 publishes.
    std::string rendererScript;
    // The UltraWeb app of the site ("site.wasm"), as <link rel="ultraweb-app">
    // (UltraWebProposal.md §4.3). Empty: none.
    std::string ultrawebApp;
    bool includeStyle = true;          // the built-in stylesheet
    std::string generator = "UltraPress";
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

    // What a block becomes, on its own (for tests and previews).
    std::string Block(const UltraPress::Block& block) const;

private:
    struct PageContext;
    void EmitBlocks(const std::vector<UltraPress::Block>& blocks, PageContext& context) const;
    void EmitBlock(const UltraPress::Block& block, PageContext& context) const;
    void EmitMenu(const Menu* menu, const std::string& label, std::string& out) const;
    std::string Head(const UltraPress::Page& page, const PageContext& context) const;

    const Site& site;
    HtmlOptions options;
    const BlockRegistry& registry;
};

// Built-in helpers the emitter and the newsletter writer share.
std::string Paragraphs(const std::string& longText);          // blank-line-separated text as <p>s, newlines as <br>
std::string Rfc822Date(const std::string& isoDate);           // "2026-10-13" -> "Tue, 13 Oct 2026 00:00:00 +0000"; empty when not a date

} // namespace UltraPress
