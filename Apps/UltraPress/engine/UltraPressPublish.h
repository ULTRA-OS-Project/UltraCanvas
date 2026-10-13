// Apps/UltraPress/engine/UltraPressPublish.h
// The static tier of publishing: the site folder becomes a folder of files
// any web host serves - one shadow page per published page, sitemap.xml,
// robots.txt, feed.xml, the media, an .htaccess with the cache rules - and
// the starter site a new site begins from.
// Version: 0.5.1 - the output mirrors the site: stale files removed
// Version: 0.1.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraPressBlocks.h"
#include "UltraPressHtml.h"
#include "UltraPressModel.h"

#include <string>
#include <vector>

namespace UltraPress {

struct PublishOptions {
    HtmlOptions html;
    bool copyMedia = true;        // <site>/media -> <out>/media
    bool writeHtaccess = true;    // cache and type rules for Apache hosts
    bool includeDrafts = false;   // a draft is left out of the output and the sitemap
    // The output mirrors the site: a file an earlier publish wrote into the
    // folder and this one does not - a removed page's, a picture no longer
    // in media/ - is deleted, and the folders left empty with it. Only the
    // deploy's manifest (UltraPressDeploy.h) stays. Off, nothing is removed.
    bool removeStale = true;
};

struct PublishResult {
    bool ok = false;
    std::vector<std::string> files;             // what was written, relative to the output folder
    std::vector<std::string> removed;           // what an earlier publish wrote and this one did not
    std::vector<std::string> errors;
    std::vector<ValidationIssue> issues;        // the validator's findings; publishing goes on past them
};

// Writes the site into `outputDirectory` (created when missing; existing
// files are overwritten, and with `removeStale` the rest of an earlier
// publish is removed). `siteDirectory` is where the media is copied from.
PublishResult PublishStatic(const Site& site, const std::string& siteDirectory,
                            const std::string& outputDirectory, const PublishOptions& options = {});

// A new site: the theme, a main and a footer menu, a front page with a
// hero, a text, a feature list, an FAQ and a contact form, and an about
// page - every predefined block the editor should show a newcomer.
Site CreateStarterSite(const std::string& name, const std::string& url, const std::string& language = "en");

// Reads a whole file; false when it cannot be read.
bool ReadTextFile(const std::string& path, std::string& out);
bool WriteTextFile(const std::string& path, const std::string& text);

} // namespace UltraPress
