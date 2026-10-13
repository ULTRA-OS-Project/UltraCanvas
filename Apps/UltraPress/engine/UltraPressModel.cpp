// Apps/UltraPress/engine/UltraPressModel.cpp
// The content model: JSON in and out, the site folder, addresses, the page list.
// Version: 0.5.0 - "hosting" in site.json
// Version: 0.1.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressModel.h"

#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

namespace fs = std::filesystem;
using UltraCanvas::JSONValue;

namespace UltraPress {

// ----- Block ------------------------------------------------------------------

std::string Block::Text(const std::string& name, const std::string& fallback) const {
    return props[name].GetString(fallback);
}
bool Block::Flag(const std::string& name, bool fallback) const {
    return props[name].GetBoolean(fallback);
}
double Block::Number(const std::string& name, double fallback) const {
    return props[name].GetNumber(fallback);
}
const JSONValue& Block::Items(const std::string& name) const {
    return props[name];
}
Block& Block::Set(const std::string& name, JSONValue value) {
    props.Set(name, std::move(value));
    return *this;
}

// ----- Site lookups --------------------------------------------------------------

const Page* Site::FindPage(const std::string& slug) const {
    for (const Page& page : pages) {
        if (page.slug == slug) return &page;
    }
    return nullptr;
}
const Menu* Site::FindMenu(const std::string& name) const {
    for (const Menu& menu : menus) {
        if (menu.name == name) return &menu;
    }
    return nullptr;
}

// ----- JSON ------------------------------------------------------------------------

JSONValue BlockToJson(const Block& block) {
    JSONValue json = JSONValue::MakeObject();
    json.Set("type", block.type);
    json.Set("props", block.props.IsObject() ? block.props : JSONValue::MakeObject());
    if (!block.children.empty()) {
        JSONValue children = JSONValue::MakeArray();
        for (const Block& child : block.children) children.Append(BlockToJson(child));
        json.Set("children", children);
    }
    return json;
}

Block BlockFromJson(const JSONValue& json) {
    Block block;
    block.type = json["type"].GetString();
    block.props = json["props"].IsObject() ? json["props"] : JSONValue::MakeObject();
    for (const JSONValue& child : json["children"].GetElements()) {
        block.children.push_back(BlockFromJson(child));
    }
    return block;
}

JSONValue PageToJson(const Page& page) {
    JSONValue json = JSONValue::MakeObject();
    json.Set("slug", page.slug);
    json.Set("title", page.title);
    json.Set("kind", page.kind);
    json.Set("status", page.status);
    if (!page.date.empty()) json.Set("date", page.date);
    if (!page.language.empty()) json.Set("language", page.language);
    json.Set("template", page.templateName);
    json.Set("pinned", page.pinned);
    JSONValue seo = JSONValue::MakeObject();
    seo.Set("description", page.seo.description);
    if (!page.seo.image.empty()) seo.Set("image", page.seo.image);
    seo.Set("noindex", page.seo.noindex);
    json.Set("seo", seo);
    JSONValue blocks = JSONValue::MakeArray();
    for (const Block& block : page.blocks) blocks.Append(BlockToJson(block));
    json.Set("blocks", blocks);
    return json;
}

Page PageFromJson(const JSONValue& json) {
    Page page;
    page.slug = json["slug"].GetString();
    page.title = json["title"].GetString();
    page.kind = json["kind"].GetString("page");
    page.status = json["status"].GetString("published");
    page.date = json["date"].GetString();
    page.language = json["language"].GetString();
    page.templateName = json["template"].GetString("default");
    page.pinned = json["pinned"].GetBoolean(false);
    page.seo.description = json["seo"]["description"].GetString();
    page.seo.image = json["seo"]["image"].GetString();
    page.seo.noindex = json["seo"]["noindex"].GetBoolean(false);
    for (const JSONValue& block : json["blocks"].GetElements()) {
        page.blocks.push_back(BlockFromJson(block));
    }
    return page;
}

static JSONValue MenuItemToJson(const MenuItem& item) {
    JSONValue json = JSONValue::MakeObject();
    json.Set("title", item.title);
    if (!item.page.empty()) json.Set("page", item.page);
    if (!item.url.empty()) json.Set("url", item.url);
    if (!item.children.empty()) {
        JSONValue children = JSONValue::MakeArray();
        for (const MenuItem& child : item.children) children.Append(MenuItemToJson(child));
        json.Set("items", children);
    }
    return json;
}

static MenuItem MenuItemFromJson(const JSONValue& json) {
    MenuItem item;
    item.title = json["title"].GetString();
    item.page = json["page"].GetString();
    item.url = json["url"].GetString();
    for (const JSONValue& child : json["items"].GetElements()) {
        item.children.push_back(MenuItemFromJson(child));
    }
    return item;
}

JSONValue MenusToJson(const std::vector<Menu>& menus) {
    JSONValue json = JSONValue::MakeObject();
    for (const Menu& menu : menus) {
        JSONValue items = JSONValue::MakeArray();
        for (const MenuItem& item : menu.items) items.Append(MenuItemToJson(item));
        json.Set(menu.name, items);
    }
    return json;
}

std::vector<Menu> MenusFromJson(const JSONValue& json) {
    std::vector<Menu> menus;
    for (const auto& [name, items] : json.GetMembers()) {
        Menu menu;
        menu.name = name;
        for (const JSONValue& item : items.GetElements()) menu.items.push_back(MenuItemFromJson(item));
        menus.push_back(std::move(menu));
    }
    return menus;
}

JSONValue ThemeToJson(const Theme& theme) {
    JSONValue json = JSONValue::MakeObject();
    json.Set("name", theme.name);
    JSONValue colors = JSONValue::MakeObject();
    for (const auto& [name, value] : theme.colors) colors.Set(name, value);
    json.Set("colors", colors);
    json.Set("fontFamily", theme.fontFamily);
    return json;
}

Theme ThemeFromJson(const JSONValue& json) {
    Theme theme;
    theme.name = json["name"].GetString("default");
    for (const auto& [name, value] : json["colors"].GetMembers()) {
        theme.colors.emplace_back(name, value.GetString());
    }
    theme.fontFamily = json["fontFamily"].GetString("Ubuntu");
    return theme;
}

JSONValue SiteToJson(const Site& site) {
    JSONValue json = JSONValue::MakeObject();
    json.Set("name", site.name);
    json.Set("tagline", site.tagline);
    json.Set("url", site.url);
    json.Set("language", site.language);
    JSONValue languages = JSONValue::MakeArray();
    for (const std::string& language : site.languages) languages.Append(language);
    json.Set("languages", languages);
    json.Set("owner", site.owner);
    if (!site.hosting.account.empty()) {
        json.Set("hosting", JSONValue::MakeObject().Set("account", site.hosting.account).Set("path", site.hosting.path));
    }
    return json;
}

void SiteFromJson(const JSONValue& json, Site& site) {
    site.name = json["name"].GetString();
    site.tagline = json["tagline"].GetString();
    site.url = json["url"].GetString();
    while (!site.url.empty() && site.url.back() == '/') site.url.pop_back();
    site.language = json["language"].GetString("en");
    site.languages.clear();
    for (const JSONValue& language : json["languages"].GetElements()) {
        site.languages.push_back(language.GetString());
    }
    site.owner = json["owner"].GetString();
    site.hosting = Hosting();
    if (json.Contains("hosting")) {
        site.hosting.account = json["hosting"]["account"].GetString();
        site.hosting.path = json["hosting"]["path"].GetString("/");
        if (site.hosting.path.empty()) site.hosting.path = "/";
    }
}

// ----- The site folder --------------------------------------------------------------

static bool ParseJsonFile(const fs::path& path, JSONValue& out, std::vector<std::string>& errors) {
    UltraCanvas::JSONParseResult result;
    out = UltraCanvas::JSON::ParseFile(UltraCanvas::PathToUtf8(path), &result);
    if (!result.success) {
        errors.push_back(UltraCanvas::PathToUtf8(path) + ": " + result.errorMessage);
        return false;
    }
    return true;
}

bool LoadSite(const std::string& directory, Site& site, std::vector<std::string>& errors) {
    const fs::path dir = UltraCanvas::PathFromUtf8(directory);
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        errors.push_back(directory + ": not a folder");
        return false;
    }
    JSONValue json;
    if (!fs::exists(dir / "site.json", ec)) {
        errors.push_back(directory + ": no site.json");
        return false;
    }
    if (!ParseJsonFile(dir / "site.json", json, errors)) return false;
    site = Site();
    SiteFromJson(json, site);
    if (fs::exists(dir / "theme.json", ec)) {
        if (ParseJsonFile(dir / "theme.json", json, errors)) site.theme = ThemeFromJson(json);
    }
    if (fs::exists(dir / "menus.json", ec)) {
        if (ParseJsonFile(dir / "menus.json", json, errors)) site.menus = MenusFromJson(json);
    }
    const fs::path pagesDir = dir / "pages";
    std::vector<fs::path> pageFiles;
    if (fs::is_directory(pagesDir, ec)) {
        for (const fs::directory_entry& entry : fs::directory_iterator(pagesDir, ec)) {
            if (entry.is_regular_file() && entry.path().extension() == ".json") pageFiles.push_back(entry.path());
        }
    }
    std::sort(pageFiles.begin(), pageFiles.end());   // a stable order, whatever the file system gives
    const size_t errorsBefore = errors.size();
    for (const fs::path& file : pageFiles) {
        if (!ParseJsonFile(file, json, errors)) continue;
        Page page = PageFromJson(json);
        if (page.slug.empty()) page.slug = UltraCanvas::PathToUtf8(file.stem());
        site.pages.push_back(std::move(page));
    }
    return errors.size() == errorsBefore;
}

static bool WriteJsonFile(const fs::path& path, const JSONValue& json, std::vector<std::string>& errors) {
    UltraCanvas::JSONSerializeOptions pretty;
    pretty.pretty = true;
    pretty.indentWidth = 2;
    if (!UltraCanvas::JSON::SerializeToFile(UltraCanvas::PathToUtf8(path), json, pretty)) {
        errors.push_back(UltraCanvas::PathToUtf8(path) + ": cannot write");
        return false;
    }
    return true;
}

bool SaveSite(const std::string& directory, const Site& site, std::vector<std::string>& errors) {
    const fs::path dir = UltraCanvas::PathFromUtf8(directory);
    std::error_code ec;
    fs::create_directories(dir / "pages", ec);
    if (ec) {
        errors.push_back(directory + ": cannot create: " + ec.message());
        return false;
    }
    bool ok = WriteJsonFile(dir / "site.json", SiteToJson(site), errors);
    ok = WriteJsonFile(dir / "theme.json", ThemeToJson(site.theme), errors) && ok;
    ok = WriteJsonFile(dir / "menus.json", MenusToJson(site.menus), errors) && ok;
    std::set<std::string> written;
    for (const Page& page : site.pages) {
        const std::string file = page.slug + ".json";
        ok = WriteJsonFile(dir / "pages" / UltraCanvas::PathFromUtf8(file), PageToJson(page), errors) && ok;
        written.insert(file);
    }
    for (const fs::directory_entry& entry : fs::directory_iterator(dir / "pages", ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".json") continue;
        if (!written.count(UltraCanvas::PathToUtf8(entry.path().filename()))) fs::remove(entry.path(), ec);
    }
    return ok;
}

// ----- Addresses -----------------------------------------------------------------------

std::string PagePath(const Page& page) {
    return page.slug == "home" ? std::string("/") : "/" + page.slug + "/";
}

std::string PageUrl(const Site& site, const Page& page) {
    return site.url + PagePath(page);
}

std::string LinkHref(const Site& site, const JSONValue& link) {
    const std::string url = link["url"].GetString();
    if (!url.empty()) return url;
    const std::string slug = link["page"].GetString();
    if (const Page* page = site.FindPage(slug)) return PagePath(*page);
    return std::string();
}

// ----- The page list ---------------------------------------------------------------------

std::vector<const Page*> SortedPages(const Site& site, PageOrder order,
                                     const std::map<std::string, long long>& views) {
    std::vector<const Page*> pages;
    for (const Page& page : site.pages) pages.push_back(&page);
    auto viewsOf = [&](const Page* page) {
        auto it = views.find(page->slug);
        return it == views.end() ? -1LL : it->second;
    };
    std::stable_sort(pages.begin(), pages.end(), [&](const Page* a, const Page* b) {
        if (a->pinned != b->pinned) return a->pinned;
        switch (order) {
            case PageOrder::Alphabetic: return a->title < b->title;
            case PageOrder::MostPopular: return viewsOf(a) > viewsOf(b);
            case PageOrder::Stored: break;
        }
        return false;
    });
    return pages;
}

std::string MakeSlug(const std::string& title) {
    std::string slug;
    bool pendingHyphen = false;
    for (unsigned char c : title) {
        if (std::isalnum(c)) {
            if (pendingHyphen && !slug.empty()) slug += '-';
            pendingHyphen = false;
            slug += static_cast<char>(std::tolower(c));
        } else if (c < 0x80) {
            pendingHyphen = true;     // a space, punctuation: one hyphen between words
        } else {
            pendingHyphen = true;     // a non-ASCII letter: not representable, a word break
        }
    }
    return slug.empty() ? std::string("page") : slug;
}

} // namespace UltraPress
