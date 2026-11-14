// Apps/UltraStoreAdmin/core/WebSite.cpp
// The web store's site in the publish folder (WebSite.h).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "WebSite.h"

#include "Verifier.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <system_error>

namespace UltraStoreAdmin {

namespace fs = std::filesystem;
using UltraStore::Catalog;
using UltraStore::Component;
using UltraStore::ErrorCode;
using UltraStore::MediaVariant;
using UltraStore::Result;

namespace {

constexpr const char* kModuleFile = "ultrastore-web.wasm";
constexpr const char* kLoaderFile = "ultracanvas-loader.js";
constexpr const char* kMarkFile = "ultrastore.png";
constexpr const char* kSiteJson = "site.json";
const char* const kRuntimeFiles[] = {"ultracanvas-runtime.js", "ultracanvas-runtime.wasm", "ultracanvas-runtime.data"};

// The shadow's own look: the shop's colours and type, no font from anywhere
// else, and the rule that hides it once the module paints.
const char* const kStylesheet = R"css(:root{--ink:#0F172A;--body:#334155;--muted:#64748B;--line:#E2E8F0;--soft:#F1F5F9;--accent:#2563EB;--accent-soft:#DBEAFE;--accent-ink:#1D4ED8;--ok:#15803D;--ok-soft:#DCFCE7}
*{box-sizing:border-box}html,body{margin:0;background:#fff;color:var(--ink)}
body{font:15px/1.5 Inter,system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
h1,h2,h3,.name{font-family:Manrope,Inter,system-ui,sans-serif;font-weight:800;letter-spacing:-.01em;margin:0}
a{color:var(--accent-ink)}p{margin:.4em 0;color:var(--body)}
.wrap{max-width:1280px;margin:0 auto;padding:0 32px}
header.top{display:flex;flex-wrap:wrap;align-items:center;gap:16px;padding:12px 0;border-bottom:1px solid var(--line)}
header.top .brand{display:flex;align-items:center;gap:8px;font:800 21px Manrope,Inter,sans-serif;color:var(--ink);text-decoration:none}
header.top nav a{display:inline-block;padding:9px 16px;border-radius:20px;color:var(--body);text-decoration:none}
header.top nav a.lit{background:var(--accent-soft);color:var(--accent-ink)}
header.top .get{margin-left:auto;background:var(--accent);color:#fff;border-radius:20px;padding:10px 18px;font-weight:700;text-decoration:none}
#ultracanvas-status{font-size:13px;color:var(--muted);padding:6px 0}
section{padding:24px 0}section h2{font-size:26px;margin-bottom:4px}section .sub{color:var(--muted);margin:0 0 16px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));gap:20px;list-style:none;padding:0;margin:0}
.card{border:1px solid var(--line);border-radius:20px;padding:20px;display:flex;flex-direction:column;gap:12px;background:#fff}
.card .head{display:flex;gap:14px;align-items:flex-start}.card .head img{width:64px;height:64px;border-radius:22%;background:var(--soft)}
.card .name{font-size:17px}.card .meta{font-size:13px;color:var(--muted)}.card .foot{display:flex;justify-content:space-between;align-items:center;margin-top:auto}
.price{font-weight:700}.price.free{color:var(--ok)}
.btn{display:inline-block;background:var(--accent);color:#fff;border-radius:26px;padding:14px 24px;font-weight:700;text-decoration:none}
.btn.soft{background:var(--accent-soft);color:var(--accent-ink);padding:10px 18px;border-radius:20px}
.hero{display:flex;flex-wrap:wrap;gap:32px;align-items:flex-start;background:#EFF6FF;border:1px solid var(--accent-soft);border-radius:20px;padding:36px;margin-top:40px}
.hero .text{flex:1 1 360px}.hero .eyebrow{font-size:12px;font-weight:700;letter-spacing:.08em;color:var(--accent-ink)}.hero h2{font-size:34px}
.hero img.shot{flex:0 1 560px;max-width:100%;border-radius:14px}.hero img.icon{width:72px;height:72px;border-radius:22%;vertical-align:middle;margin-right:12px}
.chips{display:flex;flex-wrap:wrap;gap:10px;list-style:none;padding:0;margin:16px 0}.chips a{display:inline-block;border:1px solid var(--line);border-radius:22px;padding:10px 16px;color:var(--ink);text-decoration:none}
.chips a.lit{background:var(--accent);color:#fff;border-color:var(--accent)}
.app .head{display:flex;flex-wrap:wrap;gap:24px;align-items:flex-start;padding-top:20px}.app .head img{width:112px;height:112px;border-radius:22%}
.app h1{font-size:36px}.app .summary{font-size:18px}.badges{display:flex;flex-wrap:wrap;gap:8px;margin:8px 0}.badges span{background:var(--soft);color:var(--body);border-radius:14px;padding:5px 12px;font-size:12px;font-weight:700}
.badges span.trial{background:var(--accent-soft);color:var(--accent-ink)}.badges span.ok{background:var(--ok-soft);color:var(--ok)}
.facts{display:flex;flex-wrap:wrap;gap:28px;padding:24px 0}.facts div{font-weight:700}.facts small{display:block;font-weight:400;color:var(--muted);font-size:13px}
.shots{display:flex;flex-wrap:wrap;gap:16px}.shots img{width:560px;max-width:100%;border-radius:14px;background:var(--soft)}
.columns{display:flex;flex-wrap:wrap;gap:32px;align-items:flex-start;padding-top:24px}.columns .texts{flex:2 1 420px}.columns .info{flex:1 1 300px;background:#F8FAFC;border:1px solid var(--line);border-radius:20px;padding:20px}
.info dt{font-size:13px;color:var(--muted);margin-top:8px}.info dd{margin:0;font-weight:700}
footer{display:flex;flex-wrap:wrap;justify-content:space-between;gap:16px;background:#F8FAFC;padding:28px 0 36px;margin-top:40px;color:var(--muted);font-size:13px}
footer .brand{font:800 18px Manrope,Inter,sans-serif;color:var(--ink)}
html[data-ultracanvas-state="running"]{overflow:hidden}html[data-ultracanvas-state="running"] #ultrastore-shadow,html[data-ultracanvas-state="running"] #ultracanvas-status{display:none}
@media (max-width:640px){.wrap{padding:0 16px}.hero{padding:24px}.app h1{font-size:28px}}
)css";

std::string Escape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char c : text) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

std::string TrimSlash(std::string s) {
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

bool ReadWhole(const fs::path& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream buffer;
    buffer << in.rdbuf();
    out = buffer.str();
    return true;
}

bool WriteWhole(const fs::path& path, const std::string& bytes, std::string& error) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    fs::path temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) { error = "cannot write " + UltraCanvas::PathToUtf8(path); return false; }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!out) { error = "cannot write " + UltraCanvas::PathToUtf8(path); return false; }
    }
    fs::rename(temp, path, ec);
    if (ec) { error = "cannot write " + UltraCanvas::PathToUtf8(path) + ": " + ec.message(); return false; }
    return true;
}

bool CopySiteFile(const fs::path& from, const fs::path& to, std::string& error) {
    std::error_code ec;
    if (fs::is_regular_file(to, ec) && fs::file_size(to, ec) == fs::file_size(from, ec)) return true;
    fs::create_directories(to.parent_path(), ec);
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) { error = "cannot copy " + UltraCanvas::PathToUtf8(from) + " to " + UltraCanvas::PathToUtf8(to) + ": " + ec.message(); return false; }
    return true;
}

std::string Text(const UltraStore::LocalizedText& text, const Component& app) {
    std::string out = text.Text(app.sourceLanguage.empty() ? "en" : app.sourceLanguage);
    if (out.empty()) out = text.Text("en");
    if (out.empty() && !text.byLanguage.empty()) out = text.byLanguage.begin()->second;
    return out;
}

std::string CategoryTitle(const std::string& id) {
    // English, the shadow's language; the module translates.
    if (id == "audio") return "Music and audio";
    if (id == "video") return "Video";
    if (id == "graphics") return "Graphics and photos";
    if (id == "office") return "Office";
    if (id == "communication") return "Communication";
    if (id == "internet") return "Internet";
    if (id == "development") return "Developer tools";
    if (id == "education") return "Education";
    if (id == "science") return "Science";
    if (id == "finance") return "Finance";
    if (id == "security") return "Security";
    if (id == "games") return "Games";
    if (id == "system") return "System";
    if (id == "utilities") return "Utilities";
    return id;
}

std::string PrimaryCategory(const Component& app) {
    for (const std::string& c : app.categories) {
        const std::string id = UltraStore::CategoryId(c);
        if (UltraStore::IsStoreCategory(id)) return id;
    }
    return {};
}

bool Listed(const Component& app) {
    return app.visibility == UltraStore::Visibility::Listed && app.lifecycle != UltraStore::Lifecycle::Withdrawn;
}

std::string PriceText(const Component& app) {
    if (app.pricing.model == UltraStore::PricingModel::Free) return "Free";
    std::string s = app.pricing.price + (app.pricing.currency.empty() ? "" : " " + app.pricing.currency);
    if (app.pricing.model == UltraStore::PricingModel::Monthly) s += " / month";
    if (app.pricing.model == UltraStore::PricingModel::Yearly) s += " / year";
    return s;
}

std::string PublisherName(const Catalog& catalog, const Component& app) {
    for (const UltraStore::Publisher& p : catalog.publishers) if (p.id == app.publisher) return p.name;
    return app.publisher;
}

const UltraStore::Release* NewestOf(const Component& app) {
    if (const UltraStore::Release* r = app.Newest("stable")) return r;
    return app.releases.empty() ? nullptr : &app.releases.front();
}

// An icon as <picture>: the AVIF and WebP sources the catalog names at the
// slot's size (and its 2x), the original as the <img>.
std::string PictureHtml(const std::string& root, const std::string& original, const std::vector<MediaVariant>& variants,
                        int width, const std::string& alt, const std::string& cssClass) {
    if (original.empty()) return "<span class=\"" + cssClass + "\"></span>";
    std::string html = "<picture>";
    for (const char* format : {"avif", "webp"}) {
        const MediaVariant* one = VariantFor(variants, format, width);
        const MediaVariant* two = VariantFor(variants, format, width * 2);
        if (!one) continue;
        std::string srcset = Escape(root + one->url) + " 1x";
        if (two && two != one) srcset += ", " + Escape(root + two->url) + " 2x";
        html += "<source type=\"image/" + std::string(format) + "\" srcset=\"" + srcset + "\">";
    }
    html += "<img class=\"" + cssClass + "\" src=\"" + Escape(root + original) + "\" alt=\"" + Escape(alt) + "\" loading=\"lazy\"></picture>";
    return html;
}

std::string AppHref(const std::string& root, const Component& app) { return root + "app/" + app.id + "/"; }

std::string CardHtml(const Catalog& catalog, const std::string& root, const Component& app) {
    const bool free = app.pricing.model == UltraStore::PricingModel::Free;
    std::string meta = PublisherName(catalog, app);
    if (const std::string c = PrimaryCategory(app); !c.empty()) meta += " · " + CategoryTitle(c);
    return "<li class=\"card\"><div class=\"head\">" + PictureHtml(root, app.icon, app.iconVariants, 64, app.name, "icon") +
           "<div><a class=\"name\" href=\"" + Escape(AppHref(root, app)) + "\">" + Escape(app.name) + "</a><div class=\"meta\">" + Escape(meta) + "</div></div></div>" +
           "<p>" + Escape(Text(app.summary, app)) + "</p>" +
           "<div class=\"foot\"><span class=\"price" + std::string(free ? " free" : "") + "\">" + Escape(PriceText(app)) + "</span>" +
           "<a class=\"btn soft\" href=\"ultrastore://app/" + Escape(app.id) + "\">" + (free ? "Install" : (app.pricing.trialDays > 0 ? "Try" : "Buy")) + "</a></div></li>\n";
}

std::string Head(const std::string& title, const std::string& description, const std::string& canonical, const std::string& image,
                 const std::string& page, const std::string& root, const WebSiteOptions& options, const WebSiteReport& site) {
    std::string html = "<!DOCTYPE html>\n<html lang=\"en\" data-page=\"" + Escape(page) + "\" data-root=\"" + Escape(root) + "\"";
    if (!options.storeDownloadUrl.empty()) html += " data-store-download=\"" + Escape(options.storeDownloadUrl) + "\"";
    if (!options.contactUrl.empty()) html += " data-store-contact=\"" + Escape(options.contactUrl) + "\"";
    html += ">\n<head>\n<meta charset=\"utf-8\">\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
            "<title>" + Escape(title) + "</title>\n";
    if (!description.empty()) html += "<meta name=\"description\" content=\"" + Escape(description) + "\">\n";
    if (!canonical.empty()) html += "<link rel=\"canonical\" href=\"" + Escape(canonical) + "\">\n";
    html += "<meta property=\"og:title\" content=\"" + Escape(title) + "\">\n<meta property=\"og:type\" content=\"website\">\n";
    if (!description.empty()) html += "<meta property=\"og:description\" content=\"" + Escape(description) + "\">\n";
    if (!canonical.empty()) html += "<meta property=\"og:url\" content=\"" + Escape(canonical) + "\">\n";
    if (!image.empty()) html += "<meta property=\"og:image\" content=\"" + Escape(image) + "\">\n";
    html += "<link rel=\"icon\" href=\"" + Escape(root) + kMarkFile + "\">\n<style>" + std::string(kStylesheet) + "</style>\n</head>\n<body>\n";
    (void)site;
    return html;
}

std::string Header(const std::string& root, const std::string& lit, const WebSiteOptions& options) {
    std::string html = "<div class=\"wrap\"><header class=\"top\"><a class=\"brand\" href=\"" + Escape(root.empty() ? std::string("./") : root) + "\">"
                       "<img src=\"" + Escape(root) + kMarkFile + "\" alt=\"\" width=\"36\" height=\"36\">" + Escape(options.storeName) + "</a>"
                       "<nav><a" + std::string(lit == "discover" ? " class=\"lit\"" : "") + " href=\"" + Escape(root.empty() ? std::string("./") : root) + "\">Discover</a>"
                       "<a" + std::string(lit == "categories" ? " class=\"lit\"" : "") + " href=\"" + Escape(root) + "categories/\">Categories</a></nav>";
    if (!options.storeDownloadUrl.empty()) html += "<a class=\"get\" href=\"" + Escape(options.storeDownloadUrl) + "\">Get UltraStore</a>";
    html += "</header>\n<p id=\"ultracanvas-status\"></p>\n<main id=\"ultrastore-shadow\">\n";
    return html;
}

std::string Footer(const Catalog& catalog, const WebSiteOptions& options) {
    std::string html = "</main>\n<footer><span><span class=\"brand\">UltraStore</span> · the ULTRA OS app store</span><span>";
    if (!options.storeDownloadUrl.empty()) html += "<a href=\"" + Escape(options.storeDownloadUrl) + "\">Get UltraStore</a> · ";
    if (!options.contactUrl.empty()) {
        const bool link = options.contactUrl.find("://") != std::string::npos || options.contactUrl.find('@') != std::string::npos;
        const std::string href = options.contactUrl.find('@') != std::string::npos && options.contactUrl.find("://") == std::string::npos ? "mailto:" + options.contactUrl : options.contactUrl;
        html += link ? "<a href=\"" + Escape(href) + "\">Contact</a> · " : Escape(options.contactUrl) + " · ";
    }
    html += "Signed catalog " + std::to_string(catalog.sequence) + "</span></footer></div>\n";
    return html;
}

std::string Loader(const std::string& root, const WebSiteReport& site) {
    if (!site.moduleCopied) return "</body>\n</html>\n";
    std::string html = "<script src=\"" + Escape(root) + kLoaderFile + "\" data-app=\"" + Escape(root) + kModuleFile + "\"";
    if (!site.runtimeFolder.empty()) html += " data-runtime=\"" + Escape(root + site.runtimeFolder) + "\"";
    if (!site.runtimeVersion.empty()) html += " data-runtime-version=\"" + Escape(site.runtimeVersion) + "\"";
    html += "></script>\n</body>\n</html>\n";
    return html;
}

std::vector<const Component*> ListedApps(const Catalog& catalog) {
    std::vector<const Component*> out;
    for (const Component& app : catalog.apps) if (Listed(app)) out.push_back(&app);
    std::sort(out.begin(), out.end(), [](const Component* a, const Component* b) { return a->name < b->name; });
    return out;
}

std::vector<const Component*> NewestFirst(std::vector<const Component*> apps) {
    std::stable_sort(apps.begin(), apps.end(), [](const Component* a, const Component* b) {
        const UltraStore::Release* ra = NewestOf(*a);
        const UltraStore::Release* rb = NewestOf(*b);
        return (ra ? ra->date : std::string()) > (rb ? rb->date : std::string());
    });
    return apps;
}

} // namespace

const MediaVariant* VariantFor(const std::vector<MediaVariant>& variants, const std::string& format, int width) {
    const MediaVariant* best = nullptr;
    const MediaVariant* widest = nullptr;
    for (const MediaVariant& v : variants) {
        if (v.format != format) continue;
        if (!widest || v.width > widest->width) widest = &v;
        if (v.width >= width && (!best || v.width < best->width)) best = &v;
    }
    return best ? best : widest;
}

std::string WebPageHtml(const Catalog& catalog, const std::string& page, const std::string& root,
                        const WebSiteOptions& options, const WebSiteReport& site) {
    const std::string base = TrimSlash(options.catalogBaseUrl);
    auto absolute = [&](const std::string& relative) { return base.empty() ? std::string() : base + "/" + relative; };
    const std::vector<const Component*> listed = ListedApps(catalog);

    if (page.rfind("app/", 0) == 0) {
        const Component* app = catalog.FindApp(page.substr(4));
        if (!app) return {};
        const UltraStore::Release* release = NewestOf(*app);
        const std::string summary = Text(app->summary, *app);
        const MediaVariant* preview = VariantFor(app->iconVariants, "png", 256);
        const std::string image = absolute(preview ? preview->url : app->icon);
        std::string html = Head(app->name + " - " + options.storeName, summary, absolute("app/" + app->id + "/"), image, page, root, options, site);
        html += Header(root, "discover", options);
        html += "<article class=\"app\"><div class=\"head\">" + PictureHtml(root, app->icon, app->iconVariants, 112, app->name, "icon") + "<div>";
        html += "<h1>" + Escape(app->name) + "</h1>";
        if (!summary.empty()) html += "<p class=\"summary\">" + Escape(summary) + "</p>";
        html += "<p class=\"meta\">" + Escape(PublisherName(catalog, *app)) + "</p><div class=\"badges\">";
        if (app->pricing.model != UltraStore::PricingModel::Free && app->pricing.trialDays > 0) html += "<span class=\"trial\">" + std::to_string(app->pricing.trialDays) + "-day free trial</span>";
        if (app->openSource) html += "<span class=\"ok\">Open source" + std::string(app->license.empty() ? "" : " · " + Escape(app->license)) + "</span>";
        else if (!app->license.empty()) html += "<span>" + Escape(app->license) + "</span>";
        if (!app->ageRating.empty()) html += "<span>PEGI " + Escape(app->ageRating) + "</span>";
        html += std::string("<span>") + (app->requiresAccount ? "Needs an account" : "No account needed") + "</span>";
        if (app->updates.period == UltraStore::UpdatePeriod::Until && !app->updates.until.empty()) html += "<span>Updates free until " + Escape(app->updates.until) + "</span>";
        html += "</div><p><a class=\"btn\" href=\"ultrastore://app/" + Escape(app->id) + "\">" +
                (app->pricing.model == UltraStore::PricingModel::Free ? std::string("Install") : (app->pricing.trialDays > 0 ? std::string("Try free") : "Buy · " + Escape(PriceText(*app)))) + "</a>";
        if (app->pricing.model != UltraStore::PricingModel::Free) html += " <span class=\"price\">" + Escape(PriceText(*app)) + "</span>";
        html += "</p><p class=\"meta\">Installing, trying and buying happen in the UltraStore app, which verifies every package." +
                std::string(options.storeDownloadUrl.empty() ? "" : " <a href=\"" + Escape(options.storeDownloadUrl) + "\">Get UltraStore</a>.") + "</p></div></div>\n";
        html += "<div class=\"facts\">";
        if (release) {
            html += "<div>" + Escape(release->version) + "<small>Version</small></div>";
            if (!release->date.empty()) html += "<div>" + Escape(release->date) + "<small>Released</small></div>";
            html += std::string("<div>") + (release->channel == "beta" ? "Beta" : "Stable") + "<small>Channel</small></div>";
        }
        if (const std::string c = PrimaryCategory(*app); !c.empty()) html += "<div><a href=\"" + Escape(root) + "category/" + Escape(c) + "/\">" + Escape(CategoryTitle(c)) + "</a><small>Category</small></div>";
        html += "<div>" + Escape(app->id) + "<small>App id</small></div></div>\n";
        if (!app->screenshots.empty()) {
            html += "<section><h2>Screenshots</h2><div class=\"shots\">";
            for (size_t i = 0; i < app->screenshots.size(); ++i) {
                const std::vector<MediaVariant> none;
                html += PictureHtml(root, app->screenshots[i], i < app->screenshotVariants.size() ? app->screenshotVariants[i] : none, 560, app->name + " screenshot " + std::to_string(i + 1), "shot");
            }
            html += "</div></section>\n";
        }
        html += "<div class=\"columns\"><div class=\"texts\">";
        if (const std::string about = Text(app->description, *app); !about.empty()) {
            html += "<section><h2>About this app</h2>";
            size_t at = 0;
            while (at <= about.size()) {
                size_t end = about.find("\n\n", at);
                if (end == std::string::npos) end = about.size();
                if (end > at) html += "<p>" + Escape(about.substr(at, end - at)) + "</p>";
                at = end + 2;
            }
            html += "</section>";
        }
        if (release && !release->notes.Empty()) html += "<section><h2>What's new in " + Escape(release->version) + "</h2><p>" + Escape(Text(release->notes, *app)) + "</p></section>";
        html += "</div><dl class=\"info\"><h2>Information</h2><dt>Publisher</dt><dd>" + Escape(PublisherName(catalog, *app)) + "</dd>";
        if (!app->license.empty()) html += "<dt>Licence</dt><dd>" + Escape(app->license) + "</dd>";
        html += "<dt>Price</dt><dd>" + Escape(PriceText(*app)) + "</dd>";
        if (!app->homepage.empty()) html += "<dt>Homepage</dt><dd><a href=\"" + Escape(app->homepage) + "\">" + Escape(app->homepage) + "</a></dd>";
        if (!app->supportUrl.empty()) html += "<dt>Support</dt><dd><a href=\"" + Escape(app->supportUrl) + "\">" + Escape(app->supportUrl) + "</a></dd>";
        if (!app->sourceUrl.empty()) html += "<dt>Source code</dt><dd><a href=\"" + Escape(app->sourceUrl) + "\">" + Escape(app->sourceUrl) + "</a></dd>";
        if (!app->privacyUrl.empty()) html += "<dt>Privacy policy</dt><dd><a href=\"" + Escape(app->privacyUrl) + "\">" + Escape(app->privacyUrl) + "</a></dd>";
        html += "</dl></div></article>\n";
        return html + Footer(catalog, options) + Loader(root, site);
    }

    if (page == "categories" || page.rfind("category/", 0) == 0) {
        const std::string selected = page == "categories" ? std::string() : UltraStore::CategoryId(page.substr(9));
        const std::string title = selected.empty() ? std::string("Browse apps") : CategoryTitle(selected);
        std::string html = Head(title + " - " + options.storeName, "Every app in the signed catalog, by category.", absolute(selected.empty() ? "categories/" : "category/" + selected + "/"), "", page, root, options, site);
        html += Header(root, "categories", options);
        html += "<section><h2>" + Escape(title) + "</h2><p class=\"sub\">Every app in the signed catalog, by category.</p><ul class=\"chips\">";
        html += "<li><a" + std::string(selected.empty() ? " class=\"lit\"" : "") + " href=\"" + Escape(root) + "categories/\">All</a></li>";
        for (const std::string& id : UltraStore::StoreCategories()) {
            size_t count = 0;
            for (const Component* app : listed) for (const std::string& c : app->categories) if (UltraStore::CategoryId(c) == id) { ++count; break; }
            if (!count) continue;
            html += "<li><a" + std::string(id == selected ? " class=\"lit\"" : "") + " href=\"" + Escape(root) + "category/" + Escape(id) + "/\">" + Escape(CategoryTitle(id)) + " " + std::to_string(count) + "</a></li>";
        }
        html += "</ul><ul class=\"grid\">\n";
        for (const Component* app : listed) {
            bool in = selected.empty();
            for (const std::string& c : app->categories) if (UltraStore::CategoryId(c) == selected) in = true;
            if (in) html += CardHtml(catalog, root, *app);
        }
        html += "</ul></section>\n";
        return html + Footer(catalog, options) + Loader(root, site);
    }

    // Discover.
    std::string html = Head(options.storeName + " - the ULTRA OS app store", "Apps for ULTRA OS, Linux, macOS and Windows from a signed catalog: install, try and buy with UltraStore.",
                            absolute(""), "", "", root, options, site);
    html += Header(root, "discover", options);
    const std::vector<const Component*> newest = NewestFirst(listed);
    const Component* hero = nullptr;
    for (const Component* app : newest) if (app->featured) { hero = app; break; }
    if (!hero && !newest.empty()) hero = newest.front();
    if (hero) {
        const UltraStore::Release* release = NewestOf(*hero);
        html += "<section class=\"hero\"><div class=\"text\"><div class=\"eyebrow\">FEATURED APP</div><h2>" + PictureHtml(root, hero->icon, hero->iconVariants, 72, hero->name, "icon") +
                "<a href=\"" + Escape(AppHref(root, *hero)) + "\">" + Escape(hero->name) + "</a></h2><p class=\"meta\">" + Escape(PublisherName(catalog, *hero)) +
                (release ? " · Version " + Escape(release->version) : std::string()) + "</p><p>" + Escape(Text(hero->summary, *hero)) + "</p>" +
                "<p><a class=\"btn\" href=\"ultrastore://app/" + Escape(hero->id) + "\">" +
                (hero->pricing.model == UltraStore::PricingModel::Free ? std::string("Install") : (hero->pricing.trialDays > 0 ? std::string("Try free") : "Buy · " + Escape(PriceText(*hero)))) +
                "</a> <a class=\"btn soft\" href=\"" + Escape(AppHref(root, *hero)) + "\">See the app</a></p></div>";
        if (!hero->screenshots.empty()) {
            const std::vector<MediaVariant> none;
            html += PictureHtml(root, hero->screenshots.front(), hero->screenshotVariants.empty() ? none : hero->screenshotVariants.front(), 560, hero->name, "shot");
        }
        html += "</section>\n";
    }
    html += "<ul class=\"chips\">";
    for (const std::string& id : UltraStore::StoreCategories()) {
        bool any = false;
        for (const Component* app : listed) for (const std::string& c : app->categories) if (UltraStore::CategoryId(c) == id) any = true;
        if (any) html += "<li><a href=\"" + Escape(root) + "category/" + Escape(id) + "/\">" + Escape(CategoryTitle(id)) + "</a></li>";
    }
    html += "</ul>\n";
    std::vector<const Component*> picks;
    for (const Component* app : newest) if (app->featured && picks.size() < 4) picks.push_back(app);
    if (picks.size() > 1) {
        html += "<section><h2>Editor's picks</h2><p class=\"sub\">Apps the ULTRA OS team uses every day.</p><ul class=\"grid\">\n";
        for (const Component* app : picks) html += CardHtml(catalog, root, *app);
        html += "</ul></section>\n";
    }
    html += "<section><h2>New and updated</h2><p class=\"sub\">Fresh releases from the signed catalog, newest first.</p><ul class=\"grid\">\n";
    size_t n = 0;
    for (const Component* app : newest) { if (n++ >= 8) break; html += CardHtml(catalog, root, *app); }
    html += "</ul></section>\n";
    html += "<section><h2>All apps</h2><ul class=\"grid\">\n";
    for (const Component* app : listed) html += CardHtml(catalog, root, *app);
    html += "</ul></section>\n";
    return html + Footer(catalog, options) + Loader(root, site);
}

Result<WebSiteReport> WriteWebSite(const Catalog& catalog, const std::string& outDirUtf8, const WebSiteOptions& options) {
    using R = Result<WebSiteReport>;
    WebSiteReport report;
    std::error_code ec;
    std::string error;
    const fs::path out = UltraCanvas::PathFromUtf8(outDirUtf8);
    const fs::path site = UltraCanvas::PathFromUtf8(options.siteDirUtf8);
    if (options.siteDirUtf8.empty() || !fs::is_directory(site, ec)) {
        report.warnings.push_back("web.site_dir does not name a folder (" + options.siteDirUtf8 + "): the pages are written without the web store's module");
    } else {
        // The module, the loader and the mark.
        if (fs::is_regular_file(site / kModuleFile, ec)) {
            if (!CopySiteFile(site / kModuleFile, out / kModuleFile, error)) return R::Failure(ErrorCode::IoError, error);
            if (fs::is_regular_file(site / kLoaderFile, ec)) {
                if (!CopySiteFile(site / kLoaderFile, out / kLoaderFile, error)) return R::Failure(ErrorCode::IoError, error);
                report.moduleCopied = true;
            } else {
                report.warnings.push_back(std::string("the site folder has no ") + kLoaderFile + ": the pages are written without the web store's module");
            }
        } else {
            report.warnings.push_back(std::string("the site folder has no ") + kModuleFile + ": the pages are written without the web store's module");
        }
        if (fs::is_regular_file(site / kMarkFile, ec)) {
            if (!CopySiteFile(site / kMarkFile, out / kMarkFile, error)) return R::Failure(ErrorCode::IoError, error);
        }
        // The runtime, under its hash: a visitor's browser keeps it for a year
        // (the host's cache header), and a new runtime is a new folder.
        const fs::path runtimeWasm = site / "runtime" / "ultracanvas-runtime.wasm";
        if (fs::is_regular_file(runtimeWasm, ec)) {
            const std::string hash = UltraStore::Sha256HexOfFile(UltraCanvas::PathToUtf8(runtimeWasm));
            if (hash.size() >= 12) {
                report.runtimeFolder = "runtime/" + hash.substr(0, 12) + "/";
                for (const char* name : kRuntimeFiles) {
                    const fs::path from = site / "runtime" / name;
                    if (!fs::is_regular_file(from, ec)) { report.warnings.push_back(std::string("the site folder's runtime lacks ") + name); continue; }
                    if (!CopySiteFile(from, out / UltraCanvas::PathFromUtf8(report.runtimeFolder) / name, error)) return R::Failure(ErrorCode::IoError, error);
                }
            }
        } else if (report.moduleCopied) {
            report.warnings.push_back("the site folder has no runtime/ultracanvas-runtime.wasm: visitors need the UltraCanvas extension");
        }
        std::string siteJson;
        if (ReadWhole(site / kSiteJson, siteJson)) {
            UltraCanvas::JSONParseResult parse;
            const UltraCanvas::JSONValue v = UltraCanvas::JSON::Parse(siteJson, &parse);
            if (parse.success && v.IsObject()) report.runtimeVersion = v.Get("runtime_version").GetString();
        }
    }

    // The pages.
    auto write = [&](const std::string& page, const std::string& folder, const std::string& root) -> bool {
        const std::string html = WebPageHtml(catalog, page, root, options, report);
        if (html.empty()) return true;
        if (!WriteWhole(out / UltraCanvas::PathFromUtf8(folder) / "index.html", html, error)) return false;
        ++report.pages;
        return true;
    };
    if (!write("", "", "")) return R::Failure(ErrorCode::IoError, error);
    if (!write("categories", "categories", "../")) return R::Failure(ErrorCode::IoError, error);
    std::set<std::string> categories;
    for (const Component& app : catalog.apps) {
        if (!Listed(app)) continue;
        for (const std::string& c : app.categories) {
            const std::string id = UltraStore::CategoryId(c);
            if (UltraStore::IsStoreCategory(id)) categories.insert(id);
        }
    }
    for (const std::string& id : categories) if (!write("category/" + id, "category/" + id, "../../")) return R::Failure(ErrorCode::IoError, error);
    for (const Component& app : catalog.apps) {
        if (app.visibility == UltraStore::Visibility::Hidden) continue;
        if (!write("app/" + app.id, "app/" + app.id, "../../")) return R::Failure(ErrorCode::IoError, error);
    }
    return R::Succeed(std::move(report));
}

} // namespace UltraStoreAdmin
