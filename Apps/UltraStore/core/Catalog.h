// Apps/UltraStore/core/Catalog.h
// The catalog: what the store knows about the apps and the shared-library
// components they run on, read from catalog.json. Apps and components share
// one shape - releases with packages per target - and a release names the
// component versions it was built against (depends), so an install is the
// app plus whatever of those the machine does not have yet.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraStoreTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace UltraStore {

// The newest catalog schema this build reads. A catalog with a higher
// "schema" needs a newer UltraStore (SchemaTooNew).
inline constexpr int kSupportedSchema = 1;

// What a catalog entry is. Apps are what the store shows; a runtime is the
// general shared libraries every app needs (libUltraCanvas and what it
// loads); a library is a specialised set some apps need (VideoFX, the PDF
// plug-in, ...). Components are installed where the apps find them and
// shown only on the Installed page.
enum class ComponentKind { App, Runtime, Library };
ComponentKind ParseComponentKind(std::string_view text);
std::string   ComponentKindName(ComponentKind kind);

struct Dependency {
    std::string id;        // the component's id
    std::string version;   // the exact version the release was built against
};

struct Package {
    Target target;
    std::string minOs;     // the lowest OS version that runs it ("14.0", "10.0.19041", "2.31" glibc on Linux); empty: any
    std::string format;    // "zip", "7z", "tar.zst", "appimage"
    std::string url;       // absolute
    std::string sha256;    // 64 lower-case hex chars
    std::string entry;     // the file to launch, relative to the install folder (apps)
    uint64_t size = 0;
};

struct Release {
    std::string version;   // SemVer-like, see CompareVersions
    std::string channel;   // "stable" or "beta"
    std::string date;      // "2026-10-01"
    LocalizedText notes;
    std::vector<Dependency> depends;
    std::vector<Package> packages;
};

// ===== COMMERCIAL TERMS =====
// How an app is paid for. The store shows the terms; the purchase itself is
// a later milestone (see Docs/UltraStore/README.md, "Payment"), so a v1
// store installs free apps and shows the price of the others.
enum class PricingModel { Free, OneTime, Monthly, Yearly };
PricingModel ParsePricingModel(std::string_view text);   // "free" / "one-time" / "monthly" / "yearly"
std::string  PricingModelName(PricingModel model);

struct Pricing {
    PricingModel model = PricingModel::Free;
    std::string price;      // "29.90" as written, decimal point; empty for Free
    std::string currency;   // ISO 4217, "EUR"
    int trialDays = 0;      // a paid app that runs free this long first; 0: no trial
};

// How long a purchase includes updates: always, never (the bought version
// only), or until a date - after it the store shows updates as a new purchase.
enum class UpdatePeriod { Always, Never, Until };
UpdatePeriod ParseUpdatePeriod(std::string_view text);    // "always" / "never" / "until"
std::string  UpdatePeriodName(UpdatePeriod period);

struct UpdatePolicy {
    UpdatePeriod period = UpdatePeriod::Always;
    std::string until;      // "2027-12-31" when period == Until
};

// Whether the store lists an app: listed on the pages, unlisted (found by
// id or link only - a beta programme, a customer-specific app), or hidden
// (served to those who have it installed, for updates, shown to no one else).
enum class Visibility { Listed, Unlisted, Hidden };
Visibility  ParseVisibility(std::string_view text);       // "listed" / "unlisted" / "hidden"
std::string VisibilityName(Visibility visibility);

// Where the app is in its life: active, deprecated (still installs, with a
// notice and the successor), or withdrawn (no new installs; existing ones
// keep updating while releases are published).
enum class Lifecycle { Active, Deprecated, Withdrawn };
Lifecycle   ParseLifecycle(std::string_view text);        // "active" / "deprecated" / "withdrawn"
std::string LifecycleName(Lifecycle lifecycle);

// An app or a component: the id is reverse-DNS, stable forever, the key of
// the local registry and the install folder's name.
struct Component {
    ComponentKind kind = ComponentKind::App;
    std::string id;
    std::string publisher;
    std::string name;
    std::string license;
    std::string homepage;
    std::string icon;                     // relative to the catalog URL
    LocalizedText summary, description;
    std::vector<std::string> categories;
    std::vector<std::string> screenshots; // relative to the catalog URL
    // The admin backend's settings beyond the builds (apps; components keep
    // the defaults): the terms, the update period, the flags.
    Pricing pricing;
    UpdatePolicy updates;
    Visibility visibility = Visibility::Listed;
    Lifecycle lifecycle = Lifecycle::Active;
    std::string successor;                // the id to install instead, when deprecated / withdrawn
    std::string ageRating;                // "3", "7", "12", "16", "18" (PEGI-like), empty: everyone
    bool requiresAccount = false;         // the app needs a user account of its own to be useful
    bool openSource = false;              // sourceUrl shows where
    std::string sourceUrl, supportUrl, privacyUrl;
    std::vector<std::string> tags;        // free-form, for search ("accounting", "german")
    std::vector<Release> releases;        // newest first after parsing

    // The newest release on a channel ("stable" also takes nothing else;
    // "beta" takes beta and stable). nullptr when there is none.
    const Release* Newest(std::string_view channel = "stable") const;
    // The release with exactly this version, or nullptr.
    const Release* Find(std::string_view version) const;
};

struct Publisher {
    std::string id, name, url;
};

struct Catalog {
    int schema = 0;
    uint64_t sequence = 0;
    std::string generated;
    std::vector<Publisher> publishers;
    std::vector<Component> apps;        // kind == App
    std::vector<Component> components;  // kind == Runtime or Library

    // An app or component by id, or nullptr.
    const Component* FindApp(std::string_view id) const;
    const Component* FindComponent(std::string_view id) const;
    const Component* FindAny(std::string_view id) const;
};

// Parses catalog.json. Strict about what the store relies on (ids, versions,
// package url / sha256 / target) and lenient about the rest; a catalog whose
// schema is higher than kSupportedSchema fails with SchemaTooNew, with the
// schema number in the detail. The text is the exact bytes the signature was
// made over; nothing here changes them.
Result<Catalog> ParseCatalog(const std::string& jsonText);

// The catalog as JSON, pretty-printed: what the admin application writes and
// signs, and what the tests build their fixtures with. Releases are written
// newest first.
std::string SerializeCatalog(const Catalog& catalog);

} // namespace UltraStore
