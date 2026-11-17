// Apps/UltraPress/engine/UltraPressSite.h
// Option (b) of UltraPress's two outputs: the site as HTML + JavaScript,
// written by UltraSite (UltraSite/include/UltraSite/UltraSite.h). Every
// published page becomes a complete, styled page: the blocks as
// UltraPressHtml.cpp writes them (its styled markup), the theme's colours
// as the site's tokens - with the dark look when theme.json has one - and
// the blocks' stylesheet; the main and footer menus become the navbar and
// the footer. The media folder is copied, feed.xml written beside the
// pages; UltraSite adds the hashed stylesheet and scripts, sitemap.xml,
// robots.txt, 404.html, .htaccess and _headers, and fails the publish on a
// broken link or a page over budget.
//
// Publish() writes whichever output the site has chosen (Site::output, or
// PublishOptions::output for one run): this one, or the HTML shadow with
// the WebAssembly renderer (PublishStatic, UltraPressPublish.h).
// Design: Docs/Research/UltraSiteStaticSitesProposal.md §6.
// Version: 0.3.1 - the swap navigation (Site::navigation, DocsSiteOptions::navigation)
// Version: 0.3.0 - responsive pictures, precompressed copies, versioned docs, the blog's index and tags
// Version: 0.2.0 - documentation: a site's docs/ folder (and i18n/<lang>/docs) as doc pages with search; PublishDocsFolder
// Version: 0.1.0
// Last Modified: 2026-11-17
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraPressModel.h"
#include "UltraPressPublish.h"

#include <string>
#include <utility>
#include <vector>

namespace UltraPress {

// Whether this build has the HTML + JavaScript output (UltraSite).
bool HasHtmlOutput();

// The site as HTML + JavaScript into `outputDirectory`. PublishOptions'
// copyMedia, includeDrafts and removeStale apply as to PublishStatic; the
// deploy's manifest is kept. PublishResult::sizeReport names what every
// page and asset weighs.
PublishResult PublishHtml(const Site& site, const std::string& siteDirectory,
                          const std::string& outputDirectory, const PublishOptions& options = {});

// The site's chosen output (options.output when set, else site.output).
PublishResult Publish(const Site& site, const std::string& siteDirectory,
                      const std::string& outputDirectory, const PublishOptions& options = {});

// The theme's colours as UltraSite token names and values: light from
// Theme::colors, dark from Theme::darkColors, each over the defaults the
// element renderer uses (PaletteFromTheme), so both outputs agree.
std::vector<std::pair<std::string, std::string>> ThemeTokens(const Theme& theme, bool dark);

// The stylesheet of the built-in blocks, in the HTML + JavaScript output.
const char* BlockStylesheet();

// A site's documentation, in the HTML + JavaScript output: Markdown files
// under <site>/docs are published at /docs/ in the doc layout - a sidebar
// after the folders, a table of contents, previous and next - and the site
// gets a search box (UltraSite's AddDocs). A translation is the same files
// under <site>/i18n/<language>/docs, at /<language>/docs/, joined page by
// page. Earlier versions are versioned_docs/<label> at /docs/<label>/,
// ordered by versions.json (["2.0", "1.0"]) or newest first by name. A site
// with posts gets /blog/ - the posts, newest first - and /blog/tags/<tag>/
// for each post's tags. The WebAssembly output shows none of them.
constexpr const char* kDocsFolder = "docs";

// A folder of Markdown alone published as a documentation site, the way
// Docusaurus publishes one: `ultrapress docs`.
struct DocsSiteOptions {
    std::string folder;                    // the Markdown
    std::string name = "Documentation";
    std::string url = "https://example.org";
    std::string language = "en";
    std::string editUrl;                   // + a file's path: its "Edit this page" link
    std::string sourceRoot;                // a link out of the folder: sourceUrl + its path under sourceRoot
    std::string sourceUrl;
    std::vector<std::string> exclude;      // names left out wherever they are
    bool enforceBudgets = true;
    bool brokenLinksWarn = false;          // a broken link a warning, not an error
    bool removeStale = true;
    bool precompress = false;              // .gz (and .br) copies beside the pages
    // Earlier versions: a folder whose subfolders are versions ("1.0"),
    // ordered by its versions.json or newest first by name, each published
    // at /<label>/ with a banner leading to the latest.
    std::string versionsFolder;
    // "load", or "swap": pages swapped in place where the browser does not
    // prerender (UltraSite::Navigation::Swap).
    std::string navigation = "load";
};
PublishResult PublishDocsFolder(const DocsSiteOptions& options, const std::string& outputDirectory);

} // namespace UltraPress
