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

} // namespace UltraPress
