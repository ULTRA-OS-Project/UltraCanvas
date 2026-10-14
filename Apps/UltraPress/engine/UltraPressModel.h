// Apps/UltraPress/engine/UltraPressModel.h
// The content model of an UltraPress site: a site is a folder of JSON files
// (site.json, theme.json, menus.json, pages/<slug>.json) and a page is an
// ordered tree of blocks. Everything here is data; the renderers (the HTML
// emitter, later the element renderer) read it and never change it.
// Design: Docs/Research/UltraPressDesignProposal.md §5.1.
// Version: 0.7.0 - SplitParagraphs, TextLocation / SetBlockText, ResolveBlockPath: text edited in place
// Version: 0.5.0 - the hosting target (where Publish uploads to)
// Version: 0.1.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "DataFormats/UltraCanvasJSON.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace UltraPress {

// One block of a page: its type names an entry of the block registry
// (UltraPressBlocks.h), its properties are what that entry declares, and
// a block whose type accepts children (columns) carries them in order.
struct Block {
    std::string type;
    UltraCanvas::JSONValue props = UltraCanvas::JSONValue::MakeObject();
    std::vector<Block> children;

    std::string Text(const std::string& name, const std::string& fallback = std::string()) const;
    bool Flag(const std::string& name, bool fallback = false) const;
    double Number(const std::string& name, double fallback = 0.0) const;
    const UltraCanvas::JSONValue& Items(const std::string& name) const;   // an array, or Null
    Block& Set(const std::string& name, UltraCanvas::JSONValue value);
};

struct PageSeo {
    std::string description;   // <meta name="description">, and the social card's
    std::string image;         // media path of the social card picture
    bool noindex = false;      // left out of the sitemap, <meta name="robots" content="noindex">
};

struct Page {
    std::string slug;                    // "home" is the front page, at /; every other page at /<slug>/
    std::string title;
    std::string kind = "page";           // "page", or "post" (dated, in the feed)
    std::string status = "published";    // "published", "draft" or "scheduled"
    std::string date;                    // YYYY-MM-DD: a post's date, a page's last change
    std::string language;                // empty: the site's
    std::string templateName = "default";
    bool pinned = false;                 // sorts first in the page list
    PageSeo seo;
    std::vector<Block> blocks;

    bool IsPublished() const { return status == "published"; }
};

// A menu item names a page of the site (by slug) or an outside URL, never both.
struct MenuItem {
    std::string title;
    std::string page;
    std::string url;
    std::vector<MenuItem> children;
};

struct Menu {
    std::string name;    // "main" is the header's, "footer" the footer's
    std::vector<MenuItem> items;
};

// The theme model of Phase 1: named colours and the type family. The HTML
// shadow does not use it (it is unstyled on purpose); the element renderer
// will.
struct Theme {
    std::string name = "default";
    std::vector<std::pair<std::string, std::string>> colors;   // name → #rrggbb, insertion order kept
    std::string fontFamily = "Ubuntu";
};

// Where "Publish" puts the site: an UltraPress host account (an UltraCloud
// account id - FTP, SFTP, WebDAV, Nextcloud - kept by the ultrapress program
// under the settings folder, never in the site) and the folder on it that
// is the site's root, "/public_html" on a classic web host.
struct Hosting {
    std::string account;
    std::string path = "/";
};

struct Site {
    std::string name;
    std::string tagline;
    std::string url;                      // "https://example.org", no trailing slash
    std::string language = "en";
    std::vector<std::string> languages;   // every language the site has pages in; empty: just `language`
    std::string owner;                    // the organisation named in the feed and the structured data
    Theme theme;
    std::vector<Menu> menus;
    std::vector<Page> pages;
    Hosting hosting;

    const Page* FindPage(const std::string& slug) const;
    const Menu* FindMenu(const std::string& name) const;
};

// ----- Text in a block ----------------------------------------------------------
// The paragraphs of a long text: split on blank lines, each trimmed. What the
// renderer shows as separate labels and the shadow as separate <p>s.
std::vector<std::string> SplitParagraphs(const std::string& text);

// Where a piece of text sits in a block: a property ("title"); an item of a
// Strings property ("items", 2) or of an Items property with its key
// ("items", 0, "a"); and, for a long text, one of its paragraphs.
struct TextLocation {
    std::string property;
    int item = -1;
    std::string key;
    int paragraph = -1;
};

// Writes `text` at the location: the whole property, the item, the item's
// key, or the one paragraph with the others kept. False when the location
// is not there (no such item, no such paragraph).
bool SetBlockText(Block& block, const TextLocation& where, const std::string& text);

// The block a path names: "blocks[2]", "blocks[2].children[0]". Null when
// the path does not resolve.
Block* ResolveBlockPath(Page& page, const std::string& path);
const Block* ResolveBlockPath(const Page& page, const std::string& path);

// ----- JSON -----------------------------------------------------------------
UltraCanvas::JSONValue BlockToJson(const Block& block);
Block BlockFromJson(const UltraCanvas::JSONValue& json);
UltraCanvas::JSONValue PageToJson(const Page& page);
Page PageFromJson(const UltraCanvas::JSONValue& json);
UltraCanvas::JSONValue MenusToJson(const std::vector<Menu>& menus);
std::vector<Menu> MenusFromJson(const UltraCanvas::JSONValue& json);
UltraCanvas::JSONValue ThemeToJson(const Theme& theme);
Theme ThemeFromJson(const UltraCanvas::JSONValue& json);
UltraCanvas::JSONValue SiteToJson(const Site& site);        // site.json: everything but theme, menus and pages
void SiteFromJson(const UltraCanvas::JSONValue& json, Site& site);

// ----- The site folder --------------------------------------------------------
// Reads site.json, theme.json (optional), menus.json (optional) and every
// pages/*.json. False, with every problem in `errors`, when site.json is
// missing or a file does not parse; a page that does not parse is reported
// and skipped, so one bad file does not hide the site.
bool LoadSite(const std::string& directory, Site& site, std::vector<std::string>& errors);
// Writes the same files, pretty-printed and in a stable order, so a site
// diffs and versions well. Creates the folder. A page file whose slug no
// longer exists is removed.
bool SaveSite(const std::string& directory, const Site& site, std::vector<std::string>& errors);

// ----- Addresses ---------------------------------------------------------------
std::string PagePath(const Page& page);                          // "/" or "/<slug>/"
std::string PageUrl(const Site& site, const Page& page);         // site.url + PagePath
// A link object {"url": "..."} or {"page": "<slug>"}: the href, or empty
// when it names a page the site does not have.
std::string LinkHref(const Site& site, const UltraCanvas::JSONValue& link);

// ----- The page list ------------------------------------------------------------
enum class PageOrder { Stored, Alphabetic, MostPopular };
// Pinned pages first in every order; MostPopular reads views[slug] (a page
// without a count sorts last among the unpinned).
std::vector<const Page*> SortedPages(const Site& site, PageOrder order,
                                     const std::map<std::string, long long>& views = {});

// A slug: lower-case ASCII letters, digits and hyphens, from any title.
std::string MakeSlug(const std::string& title);

} // namespace UltraPress
