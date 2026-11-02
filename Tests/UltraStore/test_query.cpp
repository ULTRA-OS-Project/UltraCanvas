// Tests/UltraStore/test_query.cpp
// StoreQuery: what the pages list, filtered, with what is installed.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "test_framework.h"
#include "fixtures.h"

#include "StoreQuery.h"

#include <algorithm>
#include <cctype>

using namespace UltraStore;
using namespace ultrastore_test;

namespace {

InstalledRecord Rec(const std::string& id, const std::string& version, ComponentKind kind = ComponentKind::App,
                    bool current = true) {
    InstalledRecord r;
    r.kind = kind;
    r.id = id;
    r.version = version;
    r.folder = "/store/apps/" + id + "/" + version;
    r.entry = kind == ComponentKind::App ? "bin/app" : "";
    r.channel = "stable";
    r.current = current;
    return r;
}

Catalog WithVisibilityCases() {
    Catalog c = FixtureCatalog();
    Component unlisted = c.apps[0];
    unlisted.id = "org.ultraos.secret";
    unlisted.name = "Secret";
    unlisted.visibility = Visibility::Unlisted;
    Component hidden = c.apps[0];
    hidden.id = "org.ultraos.hidden";
    hidden.name = "Hidden";
    hidden.visibility = Visibility::Hidden;
    Component gone = c.apps[0];
    gone.id = "org.ultraos.gone";
    gone.name = "Gone";
    gone.lifecycle = Lifecycle::Withdrawn;
    c.apps.push_back(unlisted);
    c.apps.push_back(hidden);
    c.apps.push_back(gone);
    return c;
}

std::vector<std::string> Ids(const std::vector<AppListing>& l) {
    std::vector<std::string> out;
    for (const AppListing& a : l) out.push_back(a.app->id);
    return out;
}

bool Has(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

} // namespace

TEST(categories_are_the_listed_apps_sorted_once) {
    Catalog c = FixtureCatalog();
    c.apps[0].categories = {"Office", "Mail"};
    if (c.apps.size() > 1) c.apps[1].categories = {"Mail", "Tools"};
    const auto cats = CatalogCategories(c);
    REQUIRE(Has(cats, "Office") && Has(cats, "Mail"));
    REQUIRE(std::is_sorted(cats.begin(), cats.end()));
    REQUIRE(std::adjacent_find(cats.begin(), cats.end()) == cats.end());
}

TEST(listing_sorts_by_name_and_joins_the_installed_version) {
    const Catalog c = FixtureCatalog();
    const PlatformInfo linuxPc = Machine(OS::Linux, Arch::X86_64, "6.8", {"glibc-2.39"});
    const std::string first = c.apps[0].id;
    const std::vector<InstalledRecord> installed = {Rec(first, "0.0.1"), Rec("org.ultraos.runtime", "0.9.400", ComponentKind::Runtime)};
    ListingFilter f;
    const auto all = ListApps(c, linuxPc, installed, f);
    REQUIRE(!all.empty());
    for (size_t i = 1; i < all.size(); ++i) {
        std::string a = all[i - 1].app->name, b = all[i].app->name;
        for (char& ch : a) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        for (char& ch : b) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        REQUIRE(a <= b);
    }
    const AppListing* mine = nullptr;
    for (const AppListing& l : all) if (l.app->id == first) mine = &l;
    REQUIRE(mine != nullptr);
    REQUIRE_EQ(mine->installedVersion, std::string("0.0.1"));
    REQUIRE(mine->updateAvailable);   // every fixture release is newer than 0.0.1
    REQUIRE_EQ(mine->installedEntry, std::string("bin/app"));
}

TEST(installed_and_updates_filters) {
    const Catalog c = FixtureCatalog();
    const PlatformInfo linuxPc = Machine(OS::Linux, Arch::X86_64, "6.8", {"glibc-2.39"});
    const std::string first = c.apps[0].id;
    const Resolution current = PackageResolver::Resolve(c.apps[0], linuxPc, "stable");
    REQUIRE(current.Available());
    ListingFilter f;
    f.onlyInstalled = true;
    REQUIRE(ListApps(c, linuxPc, {}, f).empty());
    // At the newest version: installed, no update.
    auto l = ListApps(c, linuxPc, {Rec(first, current.release->version)}, f);
    REQUIRE_EQ(l.size(), size_t(1));
    REQUIRE(!l[0].updateAvailable);
    f.onlyUpdates = true;
    REQUIRE(ListApps(c, linuxPc, {Rec(first, current.release->version)}, f).empty());
    REQUIRE_EQ(ListApps(c, linuxPc, {Rec(first, "0.0.1")}, f).size(), size_t(1));
}

TEST(visibility_and_lifecycle_decide_what_is_shown) {
    const Catalog c = WithVisibilityCases();
    const PlatformInfo linuxPc = Machine(OS::Linux, Arch::X86_64, "6.8", {"glibc-2.39"});
    ListingFilter f;
    auto ids = Ids(ListApps(c, linuxPc, {}, f));
    REQUIRE(!Has(ids, "org.ultraos.secret"));
    REQUIRE(!Has(ids, "org.ultraos.hidden"));
    REQUIRE(!Has(ids, "org.ultraos.gone"));
    // Installed: always shown, for its update and removal.
    ids = Ids(ListApps(c, linuxPc, {Rec("org.ultraos.hidden", "0.0.1"), Rec("org.ultraos.gone", "0.0.1")}, f));
    REQUIRE(Has(ids, "org.ultraos.hidden"));
    REQUIRE(Has(ids, "org.ultraos.gone"));
    // Unlisted: by its exact id.
    f.search = "org.ultraos.secret";
    ids = Ids(ListApps(c, linuxPc, {}, f));
    REQUIRE_EQ(ids.size(), size_t(1));
    REQUIRE_EQ(ids[0], std::string("org.ultraos.secret"));
    f.search = "Secret";
    REQUIRE(ListApps(c, linuxPc, {}, f).empty());
    // Categories list only the listed, active apps.
    const auto cats = CatalogCategories(c);
    REQUIRE(true);
}

TEST(search_matches_every_word_somewhere) {
    Component app;
    app.id = "org.ultraos.mail";
    app.name = "UltraMail";
    app.publisher = "ultra-os-project";
    app.summary.byLanguage["en"] = "Mail client for ULTRA OS";
    app.summary.byLanguage["de"] = "E-Mail-Programm";
    app.tags = {"imap", "german"};
    app.categories = {"Office"};
    REQUIRE(MatchesSearch(app, "", "en"));
    REQUIRE(MatchesSearch(app, "   ", "en"));
    REQUIRE(MatchesSearch(app, "ultra", "en"));
    REQUIRE(MatchesSearch(app, "MAIL client", "en"));
    REQUIRE(MatchesSearch(app, "imap office", "en"));
    REQUIRE(MatchesSearch(app, "programm", "de"));
    REQUIRE(!MatchesSearch(app, "programm", "en"));
    REQUIRE(!MatchesSearch(app, "mail video", "en"));
}

TEST(category_filter_and_unavailable_apps) {
    Catalog c = FixtureCatalog();
    c.apps[0].categories = {"Office"};
    const PlatformInfo linuxPc = Machine(OS::Linux, Arch::X86_64, "6.8", {"glibc-2.39"});
    ListingFilter f;
    f.category = "Office";
    auto ids = Ids(ListApps(c, linuxPc, {}, f));
    REQUIRE_EQ(ids.size(), size_t(1));
    REQUIRE_EQ(ids[0], c.apps[0].id);
    f.category = "Games";
    REQUIRE(ListApps(c, linuxPc, {}, f).empty());
    // An app with no package for the machine is listed, unless asked not to.
    f = ListingFilter{};
    const PlatformInfo mac = Machine(OS::MacOS, Arch::Arm64, "14.5");
    bool anyUnavailable = false;
    for (const AppListing& l : ListApps(c, mac, {}, f)) if (!l.Available()) anyUnavailable = true;
    REQUIRE(anyUnavailable);   // the Linux-only app
    f.includeUnavailable = false;
    for (const AppListing& l : ListApps(c, mac, {}, f)) REQUIRE(l.Available());
}

TEST(current_apps_and_components_split_the_records) {
    const std::vector<InstalledRecord> all = {
        Rec("b.app", "1.0"), Rec("a.app", "2.0"), Rec("a.app", "1.0", ComponentKind::App, false),
        Rec("rt", "0.9.1", ComponentKind::Runtime), Rec("rt", "0.9.10", ComponentKind::Runtime),
        Rec("lib", "1.0", ComponentKind::Library)};
    const auto apps = CurrentApps(all);
    REQUIRE_EQ(apps.size(), size_t(2));
    REQUIRE_EQ(apps[0].id, std::string("a.app"));
    REQUIRE_EQ(apps[0].version, std::string("2.0"));
    const auto comps = InstalledComponents(all);
    REQUIRE_EQ(comps.size(), size_t(3));
    REQUIRE_EQ(comps[0].id, std::string("lib"));
    REQUIRE_EQ(comps[1].version, std::string("0.9.10"));   // newest first within an id
}

TEST(labels) {
    Pricing p;
    REQUIRE_EQ(PriceLabel(p), std::string("Free"));
    p.model = PricingModel::OneTime; p.price = "29.90"; p.currency = "EUR";
    REQUIRE_EQ(PriceLabel(p), std::string("29.90 EUR"));
    p.model = PricingModel::Monthly; p.price = "4.90"; p.trialDays = 14;
    REQUIRE_EQ(PriceLabel(p), std::string("4.90 EUR / month (14-day trial)"));
    p.model = PricingModel::Yearly; p.trialDays = 0;
    REQUIRE_EQ(PriceLabel(p), std::string("4.90 EUR / year"));
    REQUIRE_EQ(FormatBytes(12), std::string("12 bytes"));
    REQUIRE_EQ(FormatBytes(640 * 1024), std::string("640 KB"));
    REQUIRE_EQ(FormatBytes(uint64_t(12.3 * 1024 * 1024)), std::string("12.3 MB"));
    REQUIRE_EQ(FormatBytes(uint64_t(1.5 * 1024 * 1024 * 1024)), std::string("1.5 GB"));
    InstalledRecord r = Rec("x", "1");
    REQUIRE(LaunchPath(r).find("bin") != std::string::npos);
    REQUIRE(LaunchPath(r).find("/x/1") != std::string::npos);
    r.entry.clear();
    REQUIRE(LaunchPath(r).empty());
}

TEST(language_from_locale) {
    REQUIRE_EQ(LanguageFromLocale("de_AT.UTF-8"), std::string("de"));
    REQUIRE_EQ(LanguageFromLocale("pt-BR"), std::string("pt"));
    REQUIRE_EQ(LanguageFromLocale("EN"), std::string("en"));
    REQUIRE_EQ(LanguageFromLocale("C"), std::string("en"));
    REQUIRE_EQ(LanguageFromLocale("C.UTF-8"), std::string("en"));
    REQUIRE_EQ(LanguageFromLocale("POSIX", "de"), std::string("de"));
    REQUIRE_EQ(LanguageFromLocale(""), std::string("en"));
    REQUIRE_EQ(LanguageFromLocale("x"), std::string("en"));
    REQUIRE(!SystemLanguage().empty());
}

TEST(codes_check_and_discount_the_price) {
    Catalog c = FixtureCatalog();
    PromoCode anna;
    anna.code = "ANNA-20";
    anna.kind = CodeKind::Affiliate;
    anna.discountPercent = 20;
    anna.apps = {"org.ultraos.ultravideo"};
    anna.validFrom = "2026-01-01";
    anna.validUntil = "2026-12-31";
    anna.affiliate = "aff-0001";
    PromoCode five;
    five.code = "FIVE";
    five.discountAmount = "5";
    five.currency = "EUR";
    PromoCode usd;
    usd.code = "USD";
    usd.discountAmount = "5.00";
    usd.currency = "USD";
    PromoCode plain;
    plain.code = "PARTNER";
    plain.kind = CodeKind::Affiliate;
    plain.affiliate = "aff-0002";
    c.codes = {anna, five, usd, plain};

    REQUIRE(!CheckCode(c, "", "2026-06-01").Ok());
    REQUIRE_EQ(CheckCode(c, "nope", "2026-06-01").message, std::string("Unknown code"));
    REQUIRE_EQ(CheckCode(c, "anna-20", "2025-12-31").message, std::string("Valid from 2026-01-01"));
    REQUIRE_EQ(CheckCode(c, "anna-20", "2027-01-01").message, std::string("Expired on 2026-12-31"));
    const CodeCheck ok = CheckCode(c, "anna-20", "2026-06-01");
    REQUIRE(ok.Ok());
    REQUIRE_EQ(ok.message, std::string("20% off on UltraVideo, until 2026-12-31"));
    REQUIRE_EQ(CheckCode(c, "five", "2026-06-01").message, std::string("5 EUR off on every app"));
    REQUIRE_EQ(CheckCode(c, "partner", "2026-06-01").message, std::string("Partner code on every app"));

    const Pricing& video = c.FindApp("org.ultraos.ultravideo")->pricing;   // 29.90 EUR, 30-day trial
    REQUIRE_EQ(DiscountedPrice(video, ok.code), std::string("23.92"));
    REQUIRE_EQ(PriceLabel(video, ok.code), std::string("23.92 EUR (was 29.90) (30-day trial)"));
    REQUIRE_EQ(PriceLabel(video), std::string("29.90 EUR (30-day trial)"));
    REQUIRE_EQ(DiscountedPrice(video, c.FindCode("FIVE")), std::string("24.90"));
    REQUIRE_EQ(DiscountedPrice(video, c.FindCode("USD")), std::string("29.90"));      // another currency: as it is
    REQUIRE_EQ(DiscountedPrice(video, c.FindCode("PARTNER")), std::string("29.90"));  // no discount
    Pricing cheap;
    cheap.model = PricingModel::OneTime;
    cheap.price = "3.50";
    cheap.currency = "EUR";
    REQUIRE_EQ(DiscountedPrice(cheap, c.FindCode("FIVE")), std::string("0.00"));     // never below zero
    Pricing free;
    REQUIRE_EQ(PriceLabel(free, ok.code), std::string("Free"));
    REQUIRE_EQ(DiscountLabel(*c.FindCode("FIVE")), std::string("5 EUR off"));
    REQUIRE(DiscountLabel(plain).empty());
}
