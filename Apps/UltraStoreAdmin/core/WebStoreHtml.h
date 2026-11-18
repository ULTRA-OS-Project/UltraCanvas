// Apps/UltraStoreAdmin/core/WebStoreHtml.h
// The web store as HTML + JavaScript - option (b) of the store's two web
// outputs (Docs/Research/UltraSiteStaticSitesProposal.md §7), written by
// UltraSite into the publish folder beside the catalog it was built from.
// Option (a), the WebAssembly store, is WebSite.h's.
//
//   <out>/index.html                         Discover
//   <out>/categories/index.html              every app; free/ and paid/ the same, filtered
//   <out>/category/<id>/index.html           one category
//   <out>/app/<id>/index.html                one app
//   <out>/<language>/...                     the same pages in every language the catalog's
//                                            texts are in, joined by hreflang and a switcher
//   <out>/search/                            the search page; the index per language
//   <out>/assets/...                         the stylesheet and the scripts, under their hashes
//   <out>/sitemap.xml, robots.txt, 404.html, .htaccess, _headers
//
// Every page is complete without script. Install, Try and Buy open
// ultrastore://app/<id> as the WebAssembly store's do - the licence must
// land in the store that installs the app - and the store's download
// stands beside them; the `install` module points at it when no store
// answered the link. The pages' words are the store's own translations
// (media/locale/<language>/UltraStore.json and Standard.json), the texts
// the catalog's. The pages are written only from the catalog the same
// publish has just built; the native store verifies it again before it
// installs anything. Nothing in the folder is swept: it holds the catalog,
// its media and packages, and the partner pages too.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "Catalog.h"
#include "UltraStoreTypes.h"
#include "WebSite.h"

#include <string>

namespace UltraStoreAdmin {

// Whether this build has the HTML + JavaScript output (UltraSite).
bool HasHtmlWebStore();

// Writes the HTML + JavaScript store. A missing picture, a page over its
// size budget and the like are warnings in the report, not failures: the
// catalog is published either way.
UltraStore::Result<WebSiteReport> WriteHtmlWebSite(const UltraStore::Catalog& catalog, const std::string& outDirUtf8,
                                                   const WebSiteOptions& options);

} // namespace UltraStoreAdmin
