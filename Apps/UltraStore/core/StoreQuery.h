// Apps/UltraStore/core/StoreQuery.h
// What the window's pages show, computed without a window: the apps of a
// catalog joined with what is installed and with the package this machine
// gets, filtered by a search text, a category, "installed only" or "updates
// only", and sorted by name. The command line's `list` and the window's
// Discover, Categories, Installed and Updates pages are this one function
// with four filters, so they never disagree about what is shown or hidden.
//
// Shown or hidden follows the admin backend's settings: a *listed* app is
// always shown; an *unlisted* one only when it is installed or searched for
// by its exact id; a *hidden* one only when it is installed (it is served
// for updates); a *withdrawn* one only when it is installed (no new
// installs, existing ones keep updating).
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "Catalog.h"
#include "PackageInstaller.h"
#include "PackageResolver.h"
#include "PlatformInfo.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraStore {

struct ListingFilter {
    std::string channel = "stable";
    std::string language = "en";      // for the summary the search looks into
    std::string search;               // case-insensitive, over name, id, publisher, summary, tags, categories
    std::string category;             // empty: every category
    bool onlyInstalled = false;
    bool onlyUpdates = false;         // installed, and a newer release has a package for the machine
    bool includeUnavailable = true;   // apps with no package for the machine stay listed (with the note)
};

struct AppListing {
    const Component* app = nullptr;
    Resolution resolution;            // the release and the package this machine gets
    std::string installedVersion;     // empty when the app is not installed
    std::string installedFolder;      // the current version's folder (UTF-8)
    std::string installedEntry;       // what to launch, relative to the folder
    std::string installedChannel;
    bool updateAvailable = false;     // installed, and resolution names a newer release

    bool Installed() const { return !installedVersion.empty(); }
    bool Available() const { return resolution.Available(); }
};

// The listed apps' categories, sorted, each once.
std::vector<std::string> CatalogCategories(const Catalog& catalog);

// The apps the filter keeps, sorted by name (case-insensitive), ties by id.
std::vector<AppListing> ListApps(const Catalog& catalog, const PlatformInfo& platform,
                                 const std::vector<InstalledRecord>& installed, const ListingFilter& filter);

// One app's listing, whatever its visibility; nullptr app when the catalog
// does not have it.
AppListing ListingFor(const Catalog& catalog, const Component& app, const PlatformInfo& platform,
                      const std::vector<InstalledRecord>& installed, std::string_view channel);

// Whether a search text matches an app: every space-separated word must occur
// in the name, the id, the publisher, the summary in the language, a tag or
// a category, case-insensitively (ASCII; other scripts compare as typed).
bool MatchesSearch(const Component& app, const std::string& search, const std::string& language);

// The installed app records (kind App, current version) out of every record.
std::vector<InstalledRecord> CurrentApps(const std::vector<InstalledRecord>& records);
// The installed runtimes and libraries.
std::vector<InstalledRecord> InstalledComponents(const std::vector<InstalledRecord>& records);

// "Free", "29.90 EUR", "4.90 EUR / month", "49.00 EUR / year"; a trial adds
// " (14-day trial)".
std::string PriceLabel(const Pricing& pricing);

// "1.5 GB", "12.3 MB", "640 KB", "12 bytes".
std::string FormatBytes(uint64_t bytes);

// The file to start for an installed app: its folder joined with its entry
// (UTF-8); empty when the record names no entry.
std::string LaunchPath(const InstalledRecord& record);

// The two-letter language of a POSIX locale string: "de_AT.UTF-8" -> "de",
// "pt-BR" -> "pt"; "C", "POSIX" and an empty string give the fallback.
std::string LanguageFromLocale(const std::string& locale, const std::string& fallback = "en");

// LanguageFromLocale over the environment (LC_ALL, LC_MESSAGES, LANG), "en"
// when none says.
std::string SystemLanguage();

} // namespace UltraStore
