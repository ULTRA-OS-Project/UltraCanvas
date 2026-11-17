// Apps/UltraStoreAdmin/core/WebStoreHtml.cpp
// The web store as HTML + JavaScript (WebStoreHtml.h): the catalog's apps
// as UltraSite routes, in every language of the catalog's texts.
// Version: 0.1.1 - web.navigation "swap": the swap navigation; the install module starts again on us:page
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "WebStoreHtml.h"

#include "DataFormats/UltraCanvasJSON.h"
#include "HTMLReader/HTMLDocument.h"
#include "UltraCanvasPathUtf8.h"
#include "UltraLocalisation/UltraLocalisation.h"

#if ULTRASTOREADMIN_HAS_ULTRASITE
#include "UltraSite/UltraSite.h"
#endif

#include <algorithm>
#include <functional>
#include <filesystem>
#include <map>
#include <set>

namespace UltraStoreAdmin {

namespace fs = std::filesystem;
using UltraCanvas::HTML::EscapeText;
using UltraStore::Catalog;
using UltraStore::Component;
using UltraStore::ErrorCode;
using UltraStore::MediaVariant;
using UltraStore::Result;

namespace {

// The store's look, after Docs/UltraStore/design/README.md: the values of
// Apps/UltraStore/ui/StoreTheme.h (the window's, which this headless core
// cannot include), change both together. The fonts are the design's where
// the visitor has them, else the system's: nothing is downloaded.
const std::vector<std::pair<std::string, std::string>> kLightTokens = {
    {"bg", "#ffffff"}, {"fg", "#0f172a"}, {"muted", "#64748b"}, {"accent", "#2563eb"}, {"accent-fg", "#ffffff"},
    {"surface", "#f8fafc"}, {"border", "#e2e8f0"}, {"radius", "12px"}, {"width", "80rem"},
    {"font", "Inter, system-ui, -apple-system, \"Segoe UI\", Roboto, Ubuntu, sans-serif"},
    {"font-heading", "Manrope, Inter, system-ui, -apple-system, \"Segoe UI\", Roboto, sans-serif"},
    {"body", "#334155"}, {"accent-soft", "#dbeafe"}, {"accent-ink", "#1d4ed8"}, {"faint", "#eff6ff"},
    {"ok", "#15803d"}, {"ok-soft", "#dcfce7"}, {"soft", "#f1f5f9"},
};
const std::vector<std::pair<std::string, std::string>> kDarkTokens = {
    {"bg", "#0b1220"}, {"fg", "#e2e8f0"}, {"muted", "#94a3b8"}, {"accent", "#60a5fa"}, {"accent-fg", "#0b1220"},
    {"surface", "#111a2e"}, {"border", "#1e293b"}, {"body", "#cbd5e1"}, {"accent-soft", "#1e3a8a"},
    {"accent-ink", "#bfdbfe"}, {"faint", "#0f1b33"}, {"ok", "#4ade80"}, {"ok-soft", "#14532d"}, {"soft", "#1e293b"},
};

const char* kStoreCss = R"CSS(
.us-main h1, .us-main h2 { letter-spacing: -.01em; }
.store-sub { color: var(--us-muted); margin: -.25rem 0 1.25rem; }
.store-section { margin: 0 0 3rem; }
.store-section > h2 { margin: 0 0 .25rem; font-size: 1.6rem; }
.store-hero {
  display: grid; gap: 2rem; align-items: center; margin: 0 0 2.5rem; padding: clamp(1.5rem, 4vw, 2.5rem);
  background: var(--us-faint); border: 1px solid var(--us-accent-soft); border-radius: 20px;
}
@media (min-width: 60rem) { .store-hero:has(.store-shot) { grid-template-columns: minmax(0, 1fr) minmax(0, 1.1fr); } }
.store-eyebrow { font-size: .75rem; font-weight: 700; letter-spacing: .08em; text-transform: uppercase; color: var(--us-accent-ink); margin: 0; }
.store-hero h2 { display: flex; align-items: center; gap: .75rem; font-size: clamp(1.75rem, 1.3rem + 1.6vw, 2.4rem); margin: .4rem 0; }
.store-hero h2 a { color: var(--us-fg); text-decoration: none; }
.store-hero p { color: var(--us-body); }
.store-meta { color: var(--us-muted); font-size: .9rem; }
.store-chips { display: flex; flex-wrap: wrap; gap: .6rem; list-style: none; padding: 0; margin: 0 0 2rem; }
.store-chips a { display: inline-flex; align-items: center; gap: .4rem; min-height: 2.75rem; padding: 0 1rem; border: 1px solid var(--us-border); border-radius: 999px; color: var(--us-fg); text-decoration: none; }
.store-chips a:hover { border-color: var(--us-accent); }
.store-chips a[aria-current] { background: var(--us-accent); border-color: var(--us-accent); color: var(--us-accent-fg); }
.store-chips small { color: inherit; opacity: .7; }
.store-grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(17rem, 1fr)); gap: 1.25rem; list-style: none; padding: 0; margin: 0; }
.store-card { display: flex; flex-direction: column; gap: .75rem; height: 100%; padding: 1.25rem; background: var(--us-bg); border: 1px solid var(--us-border); border-radius: 20px; transition: box-shadow .2s, transform .2s; }
.store-card:hover { box-shadow: 0 12px 32px rgb(15 23 42 / .08); transform: translateY(-2px); }
.store-card-head { display: flex; gap: .9rem; align-items: flex-start; }
.store-card-head a { font: 800 1.05rem/1.3 var(--us-font-heading); color: var(--us-fg); text-decoration: none; }
.store-card-head a:hover { color: var(--us-accent); }
.store-card p { margin: 0; color: var(--us-body); font-size: .95rem; }
.store-card-foot { display: flex; justify-content: space-between; align-items: center; margin-top: auto; }
.store-icon { display: block; flex: none; width: 64px; height: 64px; border-radius: 22%; background: var(--us-soft); object-fit: cover; }
.store-icon-large { width: 112px; height: 112px; }
.store-icon-hero { width: 72px; height: 72px; }
.store-price { font-weight: 700; }
.store-price.free { color: var(--us-ok); }
.store-button {
  display: inline-flex; align-items: center; justify-content: center; min-height: 2.5rem; padding: 0 1.1rem;
  border-radius: 999px; background: var(--us-accent-soft); color: var(--us-accent-ink); font-weight: 700; text-decoration: none;
}
.store-button.main { min-height: 3.25rem; padding: 0 1.6rem; background: var(--us-accent); color: var(--us-accent-fg); font-size: 1.05rem; }
.store-button:hover { filter: brightness(.95); }
.store-actions { display: flex; flex-wrap: wrap; gap: .75rem; align-items: center; margin: 1rem 0 .5rem; }
.store-get { color: var(--us-muted); font-size: .9rem; }
.store-get[data-missing] { color: var(--us-fg); font-weight: 600; }
.store-app-head { display: flex; flex-wrap: wrap; gap: 1.5rem; align-items: flex-start; }
.store-app-head h1 { margin: 0; font-size: clamp(1.9rem, 1.4rem + 1.6vw, 2.6rem); }
.store-summary { font-size: 1.15rem; color: var(--us-body); margin: .3rem 0; }
.store-badges { display: flex; flex-wrap: wrap; gap: .5rem; list-style: none; padding: 0; margin: .75rem 0; }
.store-badges li { padding: .3rem .75rem; border-radius: 999px; background: var(--us-soft); color: var(--us-body); font-size: .8rem; font-weight: 700; }
.store-badges .trial { background: var(--us-accent-soft); color: var(--us-accent-ink); }
.store-badges .ok { background: var(--us-ok-soft); color: var(--us-ok); }
.store-facts { display: flex; flex-wrap: wrap; gap: 1.75rem; margin: 1.5rem 0 2rem; padding: 1rem 0; border-block: 1px solid var(--us-border); }
.store-facts div { font-weight: 700; }
.store-facts small { display: block; font-weight: 400; color: var(--us-muted); font-size: .8rem; }
.store-shots { display: flex; gap: 1rem; overflow-x: auto; scroll-snap-type: x mandatory; padding-bottom: .5rem; margin: 0 0 2.5rem; }
.store-shots a { flex: none; scroll-snap-align: start; }
.store-shots img { display: block; width: auto; height: min(22rem, 56vw); max-width: none; border-radius: 14px; background: var(--us-soft); }
.store-columns { display: grid; gap: 2.5rem; }
@media (min-width: 60rem) { .store-columns { grid-template-columns: minmax(0, 2fr) minmax(0, 1fr); } }
.store-columns p { color: var(--us-body); }
.store-info { align-self: start; padding: 1.25rem; background: var(--us-surface); border: 1px solid var(--us-border); border-radius: 20px; }
.store-info h2 { font-size: 1.15rem; margin: 0 0 .5rem; }
.store-info dl { margin: 0; }
.store-info dt { margin-top: .6rem; color: var(--us-muted); font-size: .85rem; }
.store-info dd { margin: 0; font-weight: 600; overflow-wrap: anywhere; }
.store-translated { color: var(--us-muted); font-size: .85rem; font-style: italic; }
)CSS";

// The `install` module: when a click on ultrastore:// leaves the page where
// it was - no store took the link - the store's download next to it says so.
const char* kInstallJs = R"JS(// UltraStore install module: a link to ultrastore:// that no store answers
// leaves the page in focus; the download beside it is then pointed out.
// Without it the download link simply stands there. It starts again on
// every page the swap navigation brings in (the us:page event).
function start() {
  for (const link of document.querySelectorAll('a[data-us~="install"]')) {
    link.addEventListener('click', () => {
      const note = link.closest('.store-actions') && link.closest('.store-actions').nextElementSibling;
      let left = false;
      const away = () => { left = true; };
      window.addEventListener('blur', away, { once: true });
      document.addEventListener('visibilitychange', away, { once: true });
      setTimeout(() => {
        if (!left && note && note.classList.contains('store-get')) note.setAttribute('data-missing', '');
      }, 1500);
    });
  }
}

start();
document.addEventListener('us:page', start);
)JS";

// The store's words in one language: its own catalogue over the standard
// set, English where neither has the text.
class Words {
public:
    Words(const std::string& folder, const std::string& language) {
        if (folder.empty() || language == "en") return;
        for (const char* set : {"Standard", "UltraStore"}) {
            UltraCanvas::LocalisationCatalogue catalogue;
            std::string error;
            const std::string path = folder + "/" + language + "/" + set + ".json";
            if (UltraCanvas::UltraLocalisation::ReadCatalogue(path, catalogue, error)) {
                for (const auto& [key, value] : catalogue.entries) {
                    if (!value.empty()) entries[key] = value;
                }
            }
        }
    }
    std::string operator()(const std::string& text) const {
        auto found = entries.find(text);
        return found == entries.end() ? text : found->second;
    }
    std::string In(const std::string& context, const std::string& text) const {
        auto found = entries.find(context + "|" + text);
        return found == entries.end() ? (*this)(text) : found->second;
    }
    std::string Fill(const std::string& text, const std::string& value) const {
        std::string out = (*this)(text);
        for (const char* mark : {"{0}", "{n}"}) {
            const size_t at = out.find(mark);
            if (at != std::string::npos) out.replace(at, 3, value);
        }
        return out;
    }

private:
    std::map<std::string, std::string> entries;
};

std::string Esc(const std::string& text) { return EscapeText(text); }

const char* CategoryEnglish(const std::string& id) {
    static const std::map<std::string, const char*> names = {
        {"audio", "Music and audio"}, {"video", "Video"}, {"graphics", "Graphics and photos"}, {"office", "Office"},
        {"communication", "Communication"}, {"internet", "Internet"}, {"development", "Developer tools"},
        {"education", "Education"}, {"science", "Science"}, {"finance", "Finance"}, {"security", "Security"},
        {"games", "Games"}, {"system", "System"}, {"utilities", "Utilities"},
    };
    auto found = names.find(id);
    return found == names.end() ? "" : found->second;
}

bool Listed(const Component& app) {
    return app.visibility == UltraStore::Visibility::Listed && app.lifecycle != UltraStore::Lifecycle::Withdrawn;
}

bool Free(const Component& app) { return app.pricing.model == UltraStore::PricingModel::Free; }

std::vector<std::string> CategoriesOf(const Component& app) {
    std::vector<std::string> out;
    for (const std::string& c : app.categories) {
        const std::string id = UltraStore::CategoryId(c);
        if (UltraStore::IsStoreCategory(id) && std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
    }
    return out;
}

const UltraStore::Release* NewestOf(const Component& app) {
    if (const UltraStore::Release* r = app.Newest("stable")) return r;
    return app.releases.empty() ? nullptr : &app.releases.front();
}

// One language's view of the catalog.
struct View {
    const Catalog& catalog;
    const Words& words;
    std::string language;          // "en"
    std::string prefix;            // "" at the root, "/de" for another language
    const WebSiteOptions& options;
    std::function<std::string(const std::string&)> url;   // a site path as the pages write it

    std::string Page(const std::string& path) const { return url(prefix + path); }
    std::string Media(const std::string& relative) const { return url("/" + relative); }

    std::string Text(const UltraStore::LocalizedText& text, const Component& app) const {
        std::string out = text.Text(language);
        if (out.empty() && !app.sourceLanguage.empty()) out = text.Text(app.sourceLanguage);
        if (out.empty()) out = text.Text("en");
        if (out.empty() && !text.byLanguage.empty()) out = text.byLanguage.begin()->second;
        return out;
    }
    // Whether the app's texts in this language were translated by machine.
    bool Machine(const Component& app) const {
        return std::find(app.machineTranslated.begin(), app.machineTranslated.end(), language) != app.machineTranslated.end();
    }
    std::string Category(const std::string& id) const {
        const char* english = CategoryEnglish(id);
        return *english ? words.In("app category", english) : id;
    }
    std::string Price(const Component& app) const {
        if (Free(app)) return words("Free");
        const std::string amount = app.pricing.price + (app.pricing.currency.empty() ? "" : " " + app.pricing.currency);
        if (app.pricing.model == UltraStore::PricingModel::Monthly) return words.Fill("{0} / month", amount);
        if (app.pricing.model == UltraStore::PricingModel::Yearly) return words.Fill("{0} / year", amount);
        return amount;
    }
    std::string Publisher(const Component& app) const {
        for (const UltraStore::Publisher& p : catalog.publishers) if (p.id == app.publisher) return p.name;
        return app.publisher;
    }
    std::string Action(const Component& app) const {
        if (Free(app)) return words("Install");
        return app.pricing.trialDays > 0 ? words("Try") : words("Buy");
    }

    // A picture as <picture>: the AVIF and WebP the catalog names at the
    // slot's width and twice it, the original as the <img>.
    std::string Picture(const std::string& original, const std::vector<MediaVariant>& variants, int width, int height,
                        const std::string& alt, const std::string& cssClass, bool first) const {
        if (original.empty()) return "<span class=\"" + cssClass + "\"></span>";
        std::string html = "<picture>";
        for (const char* format : {"avif", "webp"}) {
            const MediaVariant* one = VariantFor(variants, format, width);
            const MediaVariant* two = VariantFor(variants, format, width * 2);
            if (!one) continue;
            std::string srcset = Esc(Media(one->url)) + " 1x";
            if (two && two != one) srcset += ", " + Esc(Media(two->url)) + " 2x";
            html += "<source type=\"image/" + std::string(format) + "\" srcset=\"" + srcset + "\">";
        }
        html += "<img class=\"" + cssClass + "\" src=\"" + Esc(Media(original)) + "\" alt=\"" + Esc(alt) + "\"";
        if (width > 0 && height > 0) html += " width=\"" + std::to_string(width) + "\" height=\"" + std::to_string(height) + "\"";
        html += first ? " fetchpriority=\"high\" decoding=\"async\"" : " loading=\"lazy\" decoding=\"async\"";
        return html + "></picture>";
    }

    // Install / Try / Buy - the page's main action when `main` - and, with
    // `note`, the store's download beside them.
    std::string Actions(const Component& app, bool main, bool note) const {
        std::string html = "<div class=\"store-actions\"><a class=\"store-button" + std::string(main ? " main" : "") +
                           "\" href=\"ultrastore://app/" + Esc(app.id) + "\" data-us=\"install\">" + Esc(Action(app)) + "</a>";
        if (main && !Free(app)) html += "<span class=\"store-price\">" + Esc(Price(app)) + "</span>";
        html += "</div>";
        if (note && !options.storeDownloadUrl.empty()) {
            html += "<p class=\"store-get\">" + Esc(words("Installing, trying and buying happen in the UltraStore app, which verifies every package. Not installed yet? Get UltraStore above.")) +
                    " <a href=\"" + Esc(options.storeDownloadUrl) + "\">" + Esc(words("Get UltraStore")) + "</a></p>";
        }
        return html;
    }

    std::string Card(const Component& app) const {
        std::string meta = Publisher(app);
        const std::vector<std::string> categories = CategoriesOf(app);
        if (!categories.empty()) meta += " \xC2\xB7 " + Category(categories.front());
        return "<li class=\"store-card\"><div class=\"store-card-head\">" +
               Picture(app.icon, app.iconVariants, 64, 64, "", "store-icon", false) + "<div><a href=\"" + Esc(Page("/app/" + app.id + "/")) +
               "\">" + Esc(app.name) + "</a><div class=\"store-meta\">" + Esc(meta) + "</div></div></div><p>" +
               Esc(Text(app.summary, app)) + "</p><div class=\"store-card-foot\"><span class=\"store-price" +
               std::string(Free(app) ? " free" : "") + "\">" + Esc(Price(app)) + "</span><a class=\"store-button\" href=\"ultrastore://app/" +
               Esc(app.id) + "\" data-us=\"install\">" + Esc(Action(app)) + "</a></div></li>\n";
    }

    std::string Grid(const std::vector<const Component*>& apps) const {
        std::string html = "<ul class=\"store-grid\">\n";
        for (const Component* app : apps) html += Card(*app);
        return html + "</ul>\n";
    }
};

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

// The languages the pages are written in: English at the root, then every
// language the listed apps' summaries are in.
std::vector<std::string> Languages(const Catalog& catalog) {
    std::set<std::string> found;
    for (const Component& app : catalog.apps) {
        if (!Listed(app)) continue;
        for (const auto& [language, text] : app.summary.byLanguage) {
            if (!text.empty() && language != "en" && !language.empty()) found.insert(language);
        }
    }
    std::vector<std::string> out = {"en"};
    out.insert(out.end(), found.begin(), found.end());
    return out;
}

} // namespace

bool HasHtmlWebStore() {
#if ULTRASTOREADMIN_HAS_ULTRASITE
    return true;
#else
    return false;
#endif
}

Result<WebSiteReport> WriteHtmlWebSite(const Catalog& catalog, const std::string& outDirUtf8, const WebSiteOptions& options) {
    using R = Result<WebSiteReport>;
#if ULTRASTOREADMIN_HAS_ULTRASITE
    WebSiteReport report;
    std::string base = options.catalogBaseUrl;
    while (!base.empty() && base.back() == '/') base.pop_back();
    if (base.find("://") == std::string::npos) {
        report.warnings.push_back("no catalog.base_url: the pages' canonical links name https://store.invalid");
        base = "https://store.invalid";
    }
    const std::string localeDir = options.localeDirUtf8.empty() ? UltraCanvas::UltraLocalisation::ShippedFolder() : options.localeDirUtf8;
    report.languages = Languages(catalog);

    UltraSite::SiteConfig config;
    config.url = base;
    config.name = options.storeName;
    config.language = "en";
    config.tokens.light = kLightTokens;
    config.tokens.dark = kDarkTokens;
    config.extraCss = kStoreCss;
    config.generator = "UltraStore";
    config.search = true;
    if (options.navigation == "swap") config.navigation = UltraSite::Navigation::Swap;
    // The catalog's own files are never prerendered.
    config.noPrefetch = {"/media/", "/packages/", "/restricted/", "/catalog.json"};
    // Each language's frame: the navbar into its own pages, its words.
    for (const std::string& language : report.languages) {
        const std::string name = UltraCanvas::UltraLocalisation::NativeName(language);
        if (!name.empty() && name != language) config.languageNames[language] = name;
        const Words words(localeDir, language);
        const std::string prefix = language == "en" ? std::string() : "/" + language;
        UltraSite::Frame frame;
        frame.navbar = {{words("Discover"), prefix + "/", {}}, {words("Categories"), prefix + "/categories/", {}}};
        if (!options.storeDownloadUrl.empty()) {
            frame.navbar.push_back({words("Get UltraStore"), options.storeDownloadUrl, {}});
            frame.footer.push_back({words("Get UltraStore"), options.storeDownloadUrl, {}});
        }
        if (!options.contactUrl.empty()) {
            std::string href = options.contactUrl;
            if (href.find("://") == std::string::npos && href.find('@') != std::string::npos) href = "mailto:" + href;
            frame.footer.push_back({words("Contact"), href, {}});
        }
        frame.footerText = options.storeName + " \xC2\xB7 " + words("Signed catalog") + " " + std::to_string(catalog.sequence);
        frame.labels.search = words("Search apps");
        frame.labels.searchNothing = words("Nothing found for");
        frame.labels.copy = words("Copy");
        frame.labels.copied = words("Copied");
        frame.labels.close = words("Close");
        frame.labels.languages = words("Language");
        if (language == "en") {
            config.navbar = frame.navbar;
            config.footer = frame.footer;
            config.footerText = frame.footerText;
            config.labels = frame.labels;
        } else {
            config.frames[language] = frame;
        }
    }
    UltraSite::Builder builder(config);
    builder.AddScript("install", kInstallJs, true);
    auto url = [&builder](const std::string& path) { return builder.Url(path); };

    const std::vector<const Component*> listed = ListedApps(catalog);
    const std::vector<const Component*> newest = NewestFirst(listed);
    std::set<std::string> usedCategories;
    for (const Component* app : listed) for (const std::string& c : CategoriesOf(*app)) usedCategories.insert(c);

    // Every page in every language: a site path -> the language -> the
    // path of that page in it, for the alternates.
    std::vector<UltraSite::Route> routes;
    std::map<std::string, std::map<std::string, std::string>> alternates;
    for (const std::string& language : report.languages) {
        const Words words(localeDir, language);
        const std::string prefix = language == "en" ? std::string() : "/" + language;
        View view{catalog, words, language, prefix, options, url};
        auto add = [&](const std::string& path, UltraSite::Route route) {
            route.path = prefix + path;
            route.language = language == "en" ? std::string() : language;
            alternates[path][language] = route.path;
            routes.push_back(std::move(route));
        };

        // Discover.
        {
            UltraSite::Route route;
            route.title = options.storeName;
            route.documentTitle = words("UltraStore - the ULTRA OS app store");
            route.description = words("Fresh releases from the signed catalog, newest first.");
            std::string body = "<h1 class=\"us-visually-hidden\">" + Esc(options.storeName) + "</h1>\n";
            const Component* hero = nullptr;
            for (const Component* app : newest) if (app->featured) { hero = app; break; }
            if (!hero && !newest.empty()) hero = newest.front();
            if (hero) {
                const UltraStore::Release* release = NewestOf(*hero);
                body += "<section class=\"store-hero\"><div><p class=\"store-eyebrow\">" + Esc(words("Featured app")) + "</p><h2>" +
                        view.Picture(hero->icon, hero->iconVariants, 72, 72, "", "store-icon store-icon-hero", true) + "<a href=\"" +
                        Esc(view.Page("/app/" + hero->id + "/")) + "\">" + Esc(hero->name) + "</a></h2><p class=\"store-meta\">" +
                        Esc(view.Publisher(*hero)) + (release ? " \xC2\xB7 " + Esc(release->version) : std::string()) + "</p><p>" +
                        Esc(view.Text(hero->summary, *hero)) + "</p>" + view.Actions(*hero, true, false) + "</div>";
                if (!hero->screenshots.empty()) {
                    const std::vector<MediaVariant> none;
                    body += "<div class=\"store-shot\">" +
                            view.Picture(hero->screenshots.front(), hero->screenshotVariants.empty() ? none : hero->screenshotVariants.front(),
                                         560, 0, hero->name, "store-shot-img", false) + "</div>";
                }
                body += "</section>\n";
            }
            body += "<ul class=\"store-chips\">";
            for (const std::string& id : UltraStore::StoreCategories()) {
                if (usedCategories.count(id)) body += "<li><a href=\"" + Esc(view.Page("/category/" + id + "/")) + "\">" + Esc(view.Category(id)) + "</a></li>";
            }
            body += "</ul>\n";
            std::vector<const Component*> picks;
            for (const Component* app : newest) if (app->featured && picks.size() < 4) picks.push_back(app);
            if (picks.size() > 1) {
                body += "<section class=\"store-section\"><h2>" + Esc(words("Editor's picks")) + "</h2>\n" + view.Grid(picks) + "</section>\n";
            }
            std::vector<const Component*> fresh(newest.begin(), newest.begin() + static_cast<std::ptrdiff_t>(std::min<size_t>(8, newest.size())));
            body += "<section class=\"store-section\"><h2>" + Esc(words("New and updated")) + "</h2><p class=\"store-sub\">" +
                    Esc(words("Fresh releases from the signed catalog, newest first.")) + "</p>\n" + view.Grid(fresh) + "</section>\n";
            std::vector<const Component*> free;
            for (const Component* app : listed) if (Free(*app) && free.size() < 8) free.push_back(app);
            if (!free.empty()) body += "<section class=\"store-section\"><h2>" + Esc(words("Free essentials")) + "</h2>\n" + view.Grid(free) + "</section>\n";
            route.bodyHtml = body;
            add("/", std::move(route));
        }

        // The categories: every app, the free ones, the paid ones, and one
        // page per category - the filters are pages, so no script is needed.
        auto listing = [&](const std::string& path, const std::string& selected, const std::string& price) {
            UltraSite::Route route;
            const std::string title = selected.empty() ? words("Browse apps") : view.Category(selected);
            route.title = title;
            route.description = words("Every app in the signed catalog, by category. Pick a category and a price to narrow the list.");
            std::string body = "<h1>" + Esc(title) + "</h1>\n<p class=\"store-sub\">" + Esc(route.description) + "</p>\n<ul class=\"store-chips\">";
            auto chip = [&](const std::string& href, const std::string& label, bool current, size_t count) {
                body += "<li><a href=\"" + Esc(view.Page(href)) + "\"" + (current ? " aria-current=\"page\"" : "") + ">" + Esc(label) +
                        (count ? " <small>" + std::to_string(count) + "</small>" : std::string()) + "</a></li>";
            };
            if (selected.empty()) {
                chip("/categories/", words("All"), price.empty(), 0);
                chip("/categories/free/", words("Free"), price == "free", 0);
                chip("/categories/paid/", words("Paid"), price == "paid", 0);
            }
            for (const std::string& id : UltraStore::StoreCategories()) {
                size_t count = 0;
                for (const Component* app : listed) {
                    const std::vector<std::string> c = CategoriesOf(*app);
                    if (std::find(c.begin(), c.end(), id) != c.end()) ++count;
                }
                if (count) chip("/category/" + id + "/", view.Category(id), id == selected, count);
            }
            body += "</ul>\n";
            std::vector<const Component*> apps;
            for (const Component* app : listed) {
                const std::vector<std::string> c = CategoriesOf(*app);
                if (!selected.empty() && std::find(c.begin(), c.end(), selected) == c.end()) continue;
                if (price == "free" && !Free(*app)) continue;
                if (price == "paid" && Free(*app)) continue;
                apps.push_back(app);
            }
            body += view.Grid(apps);
            route.bodyHtml = body;
            add(path, std::move(route));
        };
        listing("/categories/", "", "");
        listing("/categories/free/", "", "free");
        listing("/categories/paid/", "", "paid");
        for (const std::string& id : usedCategories) listing("/category/" + id + "/", id, "");

        // Each app; an unlisted one gets its page too (a link handed out),
        // a hidden one none.
        for (const Component& app : catalog.apps) {
            if (app.visibility == UltraStore::Visibility::Hidden) continue;
            const UltraStore::Release* release = NewestOf(app);
            const std::string summary = view.Text(app.summary, app);
            UltraSite::Route route;
            route.title = app.name;
            route.description = summary;
            route.kind = "website";
            route.searchable = Listed(app);
            route.noindex = !Listed(app);
            const MediaVariant* preview = VariantFor(app.iconVariants, "png", 256);
            route.image = base + "/" + (preview ? preview->url : app.icon);
            if (app.icon.empty() && !preview) route.image.clear();
            std::string body = "<article class=\"store-app\"><div class=\"store-app-head\">" +
                               view.Picture(app.icon, app.iconVariants, 112, 112, "", "store-icon store-icon-large", true) + "<div>";
            body += "<h1>" + Esc(app.name) + "</h1>";
            if (!summary.empty()) body += "<p class=\"store-summary\">" + Esc(summary) + "</p>";
            body += "<p class=\"store-meta\">" + Esc(view.Publisher(app)) + "</p><ul class=\"store-badges\">";
            if (!Free(app) && app.pricing.trialDays > 0) {
                body += "<li class=\"trial\">" + Esc(words.Fill("{n}-day free trial", std::to_string(app.pricing.trialDays))) + "</li>";
            }
            if (app.openSource) body += "<li class=\"ok\">" + Esc(words("Open source")) + (app.license.empty() ? "" : " \xC2\xB7 " + Esc(app.license)) + "</li>";
            else if (!app.license.empty()) body += "<li>" + Esc(app.license) + "</li>";
            if (!app.ageRating.empty()) body += "<li>" + Esc(words.Fill("PEGI {0}", app.ageRating)) + "</li>";
            body += "<li>" + Esc(app.requiresAccount ? words("Needs an account") : words("No account needed")) + "</li>";
            if (app.updates.period == UltraStore::UpdatePeriod::Until && !app.updates.until.empty()) {
                body += "<li>" + Esc(words.Fill("Free updates until {0}", app.updates.until)) + "</li>";
            }
            body += "</ul>" + view.Actions(app, true, true) + "</div></div>\n<div class=\"store-facts\">";
            if (release) {
                body += "<div>" + Esc(release->version) + "<small>" + Esc(words("Version")) + "</small></div>";
                if (!release->date.empty()) body += "<div>" + Esc(release->date) + "<small>" + Esc(words("Released")) + "</small></div>";
                body += "<div>" + Esc(release->channel == "beta" ? words("Beta") : words("Stable")) + "<small>" + Esc(words("Channel")) + "</small></div>";
            }
            const std::vector<std::string> categories = CategoriesOf(app);
            if (!categories.empty()) {
                body += "<div><a href=\"" + Esc(view.Page("/category/" + categories.front() + "/")) + "\">" + Esc(view.Category(categories.front())) +
                        "</a><small>" + Esc(words("Category")) + "</small></div>";
            }
            body += "</div>\n";
            if (!app.screenshots.empty()) {
                body += "<h2 class=\"us-visually-hidden\">" + Esc(words("Screenshots")) + "</h2><div class=\"store-shots\">";
                for (size_t i = 0; i < app.screenshots.size(); ++i) {
                    const std::vector<MediaVariant> none;
                    body += "<a href=\"" + Esc(view.Media(app.screenshots[i])) + "\">" +
                            view.Picture(app.screenshots[i], i < app.screenshotVariants.size() ? app.screenshotVariants[i] : none, 560, 0,
                                         app.name + " " + std::to_string(i + 1), "store-shot-img", false) + "</a>";
                }
                body += "</div>\n";
            }
            body += "<div class=\"store-columns\"><div>";
            if (const std::string about = view.Text(app.description, app); !about.empty()) {
                body += "<h2>" + Esc(words("About this app")) + "</h2>";
                if (view.Machine(app) && !app.sourceLanguage.empty()) {
                    body += "<p class=\"store-translated\">" + Esc(words.Fill("Translated automatically from {0}",
                                                                               UltraCanvas::UltraLocalisation::NativeName(app.sourceLanguage))) + "</p>";
                }
                size_t at = 0;
                while (at <= about.size()) {
                    size_t end = about.find("\n\n", at);
                    if (end == std::string::npos) end = about.size();
                    if (end > at) body += "<p>" + Esc(about.substr(at, end - at)) + "</p>";
                    at = end + 2;
                }
            }
            if (release && !release->notes.Empty()) {
                body += "<h2>" + Esc(words("Updates")) + " \xC2\xB7 " + Esc(release->version) + "</h2><p>" + Esc(view.Text(release->notes, app)) + "</p>";
            }
            body += "</div><aside class=\"store-info\"><h2>" + Esc(words("Information")) + "</h2><dl><dt>" + Esc(words("Publisher")) + "</dt><dd>" +
                    Esc(view.Publisher(app)) + "</dd>";
            if (!app.license.empty()) body += "<dt>" + Esc(words("Licence")) + "</dt><dd>" + Esc(app.license) + "</dd>";
            body += "<dt>" + Esc(words("Price")) + "</dt><dd>" + Esc(view.Price(app)) + "</dd>";
            auto link = [&](const char* label, const std::string& href) {
                if (!href.empty()) body += "<dt>" + Esc(words(label)) + "</dt><dd><a href=\"" + Esc(href) + "\" rel=\"noopener\">" + Esc(href) + "</a></dd>";
            };
            link("Homepage", app.homepage);
            link("Support", app.supportUrl);
            link("Source code", app.sourceUrl);
            link("Privacy policy", app.privacyUrl);
            body += "<dt>" + Esc(words("App id")) + "</dt><dd>" + Esc(app.id) + "</dd></dl></aside></div>\n";
            // More from the publisher.
            std::vector<const Component*> more;
            for (const Component* other : listed) if (other->publisher == app.publisher && other->id != app.id && more.size() < 4) more.push_back(other);
            if (!more.empty()) {
                body += "<section class=\"store-section\"><h2>" + Esc(words.Fill("More from {0}", view.Publisher(app))) + "</h2>\n" + view.Grid(more) + "</section>\n";
            }
            body += "</article>\n";
            route.bodyHtml = body;
            // Structured data: the app and its offer.
            std::string data = "{\"@context\":\"https://schema.org\",\"@type\":\"SoftwareApplication\",\"name\":" + UltraCanvas::JSON::EscapeString(app.name) +
                               ",\"description\":" + UltraCanvas::JSON::EscapeString(summary) +
                               ",\"url\":" + UltraCanvas::JSON::EscapeString(base + view.prefix + "/app/" + app.id + "/") +
                               ",\"offers\":{\"@type\":\"Offer\",\"price\":" + UltraCanvas::JSON::EscapeString(Free(app) ? "0" : app.pricing.price) +
                               (app.pricing.currency.empty() ? std::string() : ",\"priceCurrency\":" + UltraCanvas::JSON::EscapeString(app.pricing.currency)) + "}";
            if (!categories.empty()) data += ",\"applicationCategory\":" + UltraCanvas::JSON::EscapeString(CategoryEnglish(categories.front()));
            if (release) data += ",\"softwareVersion\":" + UltraCanvas::JSON::EscapeString(release->version);
            route.jsonLd.push_back(data + "}");
            add("/app/" + app.id + "/", std::move(route));
        }
    }

    for (UltraSite::Route& route : routes) {
        std::string sitePath = route.path;
        if (!route.language.empty()) sitePath = sitePath.substr(route.language.size() + 1);
        auto other = alternates.find(sitePath);
        if (other != alternates.end() && other->second.size() > 1) route.alternates = other->second;
        builder.AddRoute(std::move(route));
    }

    UltraSite::BuildOptions build;
    build.removeStale = false;          // the folder holds the catalog, its media and packages, the partner pages
    build.brokenLinksWarn = true;       // a missing picture is reported, the catalog published
    build.enforceBudgets = false;
    const UltraSite::BuildResult built = builder.Build(outDirUtf8, build);
    for (const std::string& error : built.errors) report.warnings.push_back(error);
    for (const std::string& warning : built.warnings) report.warnings.push_back(warning);
    for (const std::string& file : built.files) {
        if (file.size() > 10 && file.compare(file.size() - 10, 10, "index.html") == 0) ++report.pages;
    }
    report.sizeReport = built.SizeReport();
    if (!built.ok && built.files.empty()) return R::Failure(ErrorCode::IoError, "The web store's pages could not be written", built.errors.empty() ? "" : built.errors.front());
    return R::Succeed(std::move(report));
#else
    (void)catalog;
    (void)outDirUtf8;
    (void)options;
    return R::Failure(ErrorCode::InvalidArgument, "This build has no UltraSite: set web.output to wasm");
#endif
}

} // namespace UltraStoreAdmin
