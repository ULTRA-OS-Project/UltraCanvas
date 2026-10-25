// Apps/UltraStore/core/StoreQuery.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StoreQuery.h"

#include "UltraCanvasPathUtf8.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <set>

namespace UltraStore {

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool Contains(const std::string& haystackLower, const std::string& needleLower) {
    return haystackLower.find(needleLower) != std::string::npos;
}

const InstalledRecord* CurrentRecord(const std::vector<InstalledRecord>& installed, const std::string& id) {
    const InstalledRecord* fallback = nullptr;
    for (const InstalledRecord& r : installed) {
        if (r.id != id || r.kind != ComponentKind::App) continue;
        if (r.current) return &r;
        if (!fallback) fallback = &r;
    }
    return fallback;
}

// Whether the app may appear at all, given its visibility and lifecycle.
bool Shown(const Component& app, bool installed, const std::string& search) {
    if (installed) return true;   // what is on the machine is always shown, for its update and removal
    if (app.lifecycle == Lifecycle::Withdrawn) return false;
    switch (app.visibility) {
        case Visibility::Listed:   return true;
        case Visibility::Unlisted: return !search.empty() && search == app.id;
        case Visibility::Hidden:   return false;
    }
    return false;
}

} // namespace

std::vector<std::string> CatalogCategories(const Catalog& catalog) {
    std::set<std::string> set;
    for (const Component& app : catalog.apps) {
        if (app.visibility != Visibility::Listed || app.lifecycle == Lifecycle::Withdrawn) continue;
        for (const std::string& c : app.categories) if (!c.empty()) set.insert(c);
    }
    return {set.begin(), set.end()};
}

AppListing ListingFor(const Catalog& catalog, const Component& app, const PlatformInfo& platform,
                      const std::vector<InstalledRecord>& installed, std::string_view channel) {
    (void)catalog;
    AppListing l;
    l.app = &app;
    if (const InstalledRecord* rec = CurrentRecord(installed, app.id)) {
        l.installedVersion = rec->version;
        l.installedFolder = rec->folder;
        l.installedEntry = rec->entry;
        l.installedChannel = rec->channel;
    }
    // An installed app follows the channel it came from when that is wider
    // than the one asked for: a beta install keeps seeing betas.
    std::string effective(channel);
    if (!l.installedChannel.empty() && l.installedChannel == "beta") effective = "beta";
    l.resolution = PackageResolver::Resolve(app, platform, effective);
    l.updateAvailable = l.Installed() && l.resolution.Available() &&
                        VersionLess(l.installedVersion, l.resolution.release->version);
    return l;
}

bool MatchesSearch(const Component& app, const std::string& search, const std::string& language) {
    const std::string needle = Lower(search);
    if (needle.find_first_not_of(' ') == std::string::npos) return true;
    std::string hay = Lower(app.name) + '\n' + Lower(app.id) + '\n' + Lower(app.publisher) + '\n' +
                      Lower(app.summary.Text(language));
    for (const std::string& t : app.tags) hay += '\n' + Lower(t);
    for (const std::string& c : app.categories) hay += '\n' + Lower(c);
    size_t pos = 0;
    while (pos < needle.size()) {
        const size_t end = needle.find(' ', pos);
        const std::string word = needle.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        if (!word.empty() && !Contains(hay, word)) return false;
        if (end == std::string::npos) break;
        pos = end + 1;
    }
    return true;
}

std::vector<AppListing> ListApps(const Catalog& catalog, const PlatformInfo& platform,
                                 const std::vector<InstalledRecord>& installed, const ListingFilter& filter) {
    std::vector<AppListing> out;
    for (const Component& app : catalog.apps) {
        if (app.kind != ComponentKind::App) continue;
        AppListing l = ListingFor(catalog, app, platform, installed, filter.channel);
        if (!Shown(app, l.Installed(), filter.search)) continue;
        if (filter.onlyInstalled && !l.Installed()) continue;
        if (filter.onlyUpdates && !l.updateAvailable) continue;
        if (!filter.includeUnavailable && !l.Available() && !l.Installed()) continue;
        if (!filter.category.empty() &&
            std::find(app.categories.begin(), app.categories.end(), filter.category) == app.categories.end()) continue;
        if (!MatchesSearch(app, filter.search, filter.language)) continue;
        out.push_back(std::move(l));
    }
    std::sort(out.begin(), out.end(), [](const AppListing& a, const AppListing& b) {
        const std::string an = Lower(a.app->name), bn = Lower(b.app->name);
        if (an != bn) return an < bn;
        return a.app->id < b.app->id;
    });
    return out;
}

std::vector<InstalledRecord> CurrentApps(const std::vector<InstalledRecord>& records) {
    std::vector<InstalledRecord> out;
    for (const InstalledRecord& r : records) if (r.kind == ComponentKind::App && r.current) out.push_back(r);
    std::sort(out.begin(), out.end(), [](const InstalledRecord& a, const InstalledRecord& b) { return a.id < b.id; });
    return out;
}

std::vector<InstalledRecord> InstalledComponents(const std::vector<InstalledRecord>& records) {
    std::vector<InstalledRecord> out;
    for (const InstalledRecord& r : records) if (r.kind != ComponentKind::App) out.push_back(r);
    std::sort(out.begin(), out.end(), [](const InstalledRecord& a, const InstalledRecord& b) {
        return a.id != b.id ? a.id < b.id : VersionLess(b.version, a.version);
    });
    return out;
}

std::string PriceLabel(const Pricing& pricing) {
    if (pricing.model == PricingModel::Free) return "Free";
    std::string label = pricing.price;
    if (!pricing.currency.empty()) label += " " + pricing.currency;
    if (pricing.model == PricingModel::Monthly) label += " / month";
    else if (pricing.model == PricingModel::Yearly) label += " / year";
    if (pricing.trialDays > 0) label += " (" + std::to_string(pricing.trialDays) + "-day trial)";
    return label;
}

std::string FormatBytes(uint64_t bytes) {
    char buffer[32];
    // locale-ok: a size shown to the user, in the classic form every store uses
    if (bytes >= (uint64_t(1) << 30)) std::snprintf(buffer, sizeof(buffer), "%.1f GB", bytes / double(uint64_t(1) << 30));
    else if (bytes >= (uint64_t(1) << 20)) std::snprintf(buffer, sizeof(buffer), "%.1f MB", bytes / double(uint64_t(1) << 20));
    else if (bytes >= 1024) std::snprintf(buffer, sizeof(buffer), "%llu KB", static_cast<unsigned long long>(bytes / 1024));
    else std::snprintf(buffer, sizeof(buffer), "%llu bytes", static_cast<unsigned long long>(bytes));
    return buffer;
}

std::string LaunchPath(const InstalledRecord& record) {
    if (record.entry.empty() || record.folder.empty()) return {};
    return UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(record.folder) / UltraCanvas::PathFromUtf8(record.entry));
}

std::string LanguageFromLocale(const std::string& locale, const std::string& fallback) {
    if (locale.empty() || locale == "C" || locale == "POSIX" || locale.rfind("C.", 0) == 0) return fallback;
    std::string lang;
    for (char c : locale) {
        if (!std::isalpha(static_cast<unsigned char>(c))) break;
        lang += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (lang.size() < 2 || lang.size() > 3) return fallback;
    return lang;
}

std::string SystemLanguage() {
    for (const char* name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
        const std::string value = UltraCanvas::GetEnvUtf8(name);   // path-string-ok: GetEnvUtf8 is the UTF-8 read
        if (value.empty()) continue;
        return LanguageFromLocale(value);
    }
    return "en";
}

} // namespace UltraStore
