// Apps/UltraPress/engine/UltraPressImport.h
// The WordPress importer: a WXR export (Tools > Export, an RSS 2.0 file with
// the wp: namespace) becomes a site of the content model - the site's name,
// address and owner, every page and post with its slug, status and date,
// the menus with their nesting, and the bodies as blocks: the block
// editor's `<!-- wp:… -->` markers map onto the registry where a block
// exists (paragraphs, headings, images, lists, quotes, buttons, columns,
// covers, galleries, spacers, HTML) and the rest, and classic content,
// becomes rich text. Attachment URLs are rewritten to the site's media
// folder and listed for fetching (Docs/Research/UltraPressDesignProposal.md
// §7.1).
// Version: 0.4.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraPressModel.h"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace UltraPress {

struct ImportOptions {
    // The slug of the page that becomes the front page ("home"). Empty: the
    // page WordPress names "home" or "front-page", else the first page.
    std::string frontPage;
    // Called for every attachment with its URL and the path the site will
    // hold it at (relative to the site folder); returns false when it could
    // not be fetched. Without it nothing is fetched and the report lists
    // what is to fetch.
    std::function<bool(const std::string& url, const std::string& localPath)> fetchMedia;
    // Keep drafts and pending posts (as drafts); otherwise they are skipped.
    bool includeDrafts = true;
};

struct ImportReport {
    int pages = 0, posts = 0, drafts = 0, attachments = 0, menus = 0;
    int blocksMapped = 0, blocksAsRichText = 0;
    std::vector<std::pair<std::string, std::string>> media;      // URL → site-relative path, every attachment
    std::vector<std::pair<std::string, std::string>> fetched;    // of those, what fetchMedia brought in
    std::vector<std::pair<std::string, std::string>> redirects;  // the WordPress link → the site's path, where they differ
    std::vector<std::string> warnings;
    std::string Summary() const;
};

// Parses the export and fills `site` (replaced whole). False when the XML
// does not parse or is not a WordPress export; the reason is in the
// report's warnings.
bool ImportWordPress(const std::string& wxrXml, const ImportOptions& options, Site& site, ImportReport& report);
bool ImportWordPressFile(const std::string& path, const ImportOptions& options, Site& site, ImportReport& report);

// The block editor's markup, split at its markers, as blocks of the model;
// classic content (no markers) is one rich-text block. Exposed for tests.
std::vector<Block> BlocksFromWordPressContent(const std::string& html, const Site& site, ImportReport& report);

} // namespace UltraPress
