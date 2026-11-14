// Apps/UltraStoreAdmin/core/WebSite.h
// The web store's site, laid into the publish folder beside catalog.json
// (Docs/UltraStore/WebstoreBrief.md): the site's document root is the
// catalog base, so the shop is at the store's own address.
//
//   <out>/index.html                       Discover
//   <out>/categories/index.html            the categories
//   <out>/category/<id>/index.html         one category
//   <out>/app/<id>/index.html              one app
//   <out>/ultrastore-web.wasm              the web store, a side module
//   <out>/ultracanvas-loader.js            the runtime's loader, unchanged
//   <out>/runtime/<hash>/ultracanvas-runtime.{js,wasm,data}
//                                          the UltraCanvas runtime, under
//                                          the first 12 hex digits of its
//                                          .wasm's SHA-256, so the host can
//                                          cache it for a year and a new
//                                          runtime is a new folder
//   <out>/ultrastore.png                   the mark the pages show
//
// Every page is an HTML shadow of what the module draws - the title, the
// texts, the icon as a <picture> of the catalog's variants, the price, the
// ultrastore:// link - for search engines, link previews, a browser without
// WebAssembly, and the moment before the module paints; then the loader
// script starts the module, which hides the shadow and takes the viewport.
// The loader looks for the UltraCanvas extension first and falls back to
// the runtime under runtime/, so an extension can come later without a
// change here.
//
// The module, the loader and the runtime come from the web store's build
// (Apps/UltraStore/web, its site/ folder): the setting web.site_dir names
// that folder. Without the setting nothing of the site is written and the
// publish folder is what it was; with it and a folder lacking the module,
// the pages are written without the script and the report warns.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "Catalog.h"
#include "UltraStoreTypes.h"

#include <string>
#include <vector>

namespace UltraStoreAdmin {

struct WebSiteOptions {
    std::string siteDirUtf8;          // the web store build's site/ folder (web.site_dir)
    std::string catalogBaseUrl;       // the site's address, for canonical links and previews
    std::string storeDownloadUrl;     // where the native store is downloaded (web.store_download_url); empty: not offered
    std::string contactUrl;           // the store's contact (store.contact), a URL or an address
    std::string storeName = "UltraStore";
};

struct WebSiteReport {
    size_t pages = 0;                 // HTML pages written
    bool moduleCopied = false;        // ultrastore-web.wasm found and copied
    std::string runtimeFolder;        // "runtime/<hash>/" when the runtime was copied, else ""
    std::string runtimeVersion;       // from site.json, "" when unknown
    std::vector<std::string> warnings;
};

// The media variant of a format and the smallest width at or above `width`,
// nullptr when there is none of the format.
const UltraStore::MediaVariant* VariantFor(const std::vector<UltraStore::MediaVariant>& variants, const std::string& format, int width);

// Writes the site into the publish folder: the files from the site folder,
// then a page for Discover, the categories, each category and each app the
// catalog lists (hidden apps get none; unlisted apps get their page, which
// nothing links to). The relative media paths of the catalog work as they
// are, since every page is under the catalog base.
UltraStore::Result<WebSiteReport> WriteWebSite(const UltraStore::Catalog& catalog, const std::string& outDirUtf8, const WebSiteOptions& options);

// One page's HTML, for the tests and the window's preview: `page` is "",
// "categories", "category/<id>" or "app/<id>"; `root` the path of the site
// root relative to the page ("", "../", "../../").
std::string WebPageHtml(const UltraStore::Catalog& catalog, const std::string& page, const std::string& root,
                        const WebSiteOptions& options, const WebSiteReport& site);

} // namespace UltraStoreAdmin
