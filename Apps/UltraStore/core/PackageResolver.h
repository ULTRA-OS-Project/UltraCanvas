// Apps/UltraStore/core/PackageResolver.h
// Which package of a release this machine gets, and what has to be installed
// for an app: the release's dependencies first, each at the exact version
// the app was built against, skipping what is already installed.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "Catalog.h"
#include "PlatformInfo.h"

#include <string>
#include <string_view>
#include <vector>

namespace UltraStore {

// How well a package fits the machine, best first. Emulated is an x86_64
// package on Apple silicon with Rosetta: offered, labelled "runs under
// Rosetta". None: the app is shown greyed out, with the systems it supports.
enum class MatchKind { Exact, Universal, Any, Emulated, None };
std::string MatchKindName(MatchKind kind);

struct Resolution {
    MatchKind kind = MatchKind::None;
    const Release* release = nullptr;
    const Package* package = nullptr;

    bool Available() const { return kind != MatchKind::None && package != nullptr; }
};

// One step of an install: a component or the app itself, the release and the
// package for this machine. Steps come dependencies first.
struct InstallStep {
    const Component* component = nullptr;
    const Release* release = nullptr;
    const Package* package = nullptr;
    MatchKind match = MatchKind::None;
    bool alreadyInstalled = false;   // present at this exact version: nothing to fetch
};

struct InstallPlan {
    std::vector<InstallStep> steps;  // the app is the last step
    uint64_t bytesToDownload = 0;    // over the steps that are not installed
};

// What is on this machine: component id -> installed versions (several
// runtimes may coexist, each used by the apps built against it).
struct InstalledVersions {
    std::vector<std::pair<std::string, std::string>> entries;   // id, version
    bool Has(std::string_view id, std::string_view version) const;
};

class PackageResolver {
public:
    // The best package of a release for the machine, or None.
    static Resolution ResolveRelease(const Release& release, const PlatformInfo& platform);

    // The newest release of an app on a channel that has a package for the
    // machine. A release with no package for it is skipped for an older one
    // that has, so an app stays available while its newest build is missing.
    static Resolution Resolve(const Component& app, const PlatformInfo& platform,
                              std::string_view channel = "stable");

    // The install plan for an app release: every dependency, transitively,
    // at its exact version and with a package for the machine, dependencies
    // before dependents, the app last. DependencyError when a dependency
    // names no component or version in the catalog, has no package for the
    // machine, or the dependencies cycle; NotAvailable when the app itself
    // has no package.
    static Result<InstallPlan> Plan(const Catalog& catalog, const Component& app,
                                    const Release& release, const PlatformInfo& platform,
                                    const InstalledVersions& installed);

    // The systems a release has packages for, for "not available for your
    // system" ("linux/x86_64, windows/x86_64").
    static std::string SupportedSystems(const Release& release);
};

} // namespace UltraStore
