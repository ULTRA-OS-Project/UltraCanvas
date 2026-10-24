// Apps/UltraStore/core/PackageResolver.cpp
// Target matching and install plans (PackageResolver.h).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "PackageResolver.h"

#include <functional>
#include <set>

namespace UltraStore {

namespace {

bool VariantsSatisfied(const Package& package, const PlatformInfo& platform) {
    for (const std::string& v : package.target.variants) {
        if (!platform.Satisfies(v)) return false;
    }
    return true;
}

MatchKind MatchOf(const Package& package, const PlatformInfo& platform) {
    if (package.target.os != platform.target.os) return MatchKind::None;
    if (!platform.MeetsMinOs(package.minOs)) return MatchKind::None;
    if (!VariantsSatisfied(package, platform)) return MatchKind::None;
    if (package.target.arch == platform.target.arch) return MatchKind::Exact;
    if (package.target.arch == Arch::Universal) return MatchKind::Universal;
    if (package.target.arch == Arch::Any) return MatchKind::Any;
    if (package.target.arch == Arch::X86_64 && platform.target.arch == Arch::Arm64 &&
        platform.canRunX86_64 && platform.target.os == OS::MacOS) {
        return MatchKind::Emulated;
    }
    return MatchKind::None;
}

int Rank(MatchKind kind) {
    switch (kind) {
        case MatchKind::Exact:     return 0;
        case MatchKind::Universal: return 1;
        case MatchKind::Any:       return 2;
        case MatchKind::Emulated:  return 3;
        case MatchKind::None:      break;
    }
    return 4;
}

} // namespace

std::string MatchKindName(MatchKind kind) {
    switch (kind) {
        case MatchKind::Exact:     return "exact";
        case MatchKind::Universal: return "universal";
        case MatchKind::Any:       return "any";
        case MatchKind::Emulated:  return "emulated";
        case MatchKind::None:      break;
    }
    return "none";
}

bool InstalledVersions::Has(std::string_view id, std::string_view version) const {
    for (const auto& [i, v] : entries) {
        if (i == id && VersionEqual(v, version)) return true;
    }
    return false;
}

Resolution PackageResolver::ResolveRelease(const Release& release, const PlatformInfo& platform) {
    Resolution best;
    best.release = &release;
    for (const Package& p : release.packages) {
        const MatchKind kind = MatchOf(p, platform);
        if (kind == MatchKind::None) continue;
        if (best.package == nullptr || Rank(kind) < Rank(best.kind)) {
            best.kind = kind;
            best.package = &p;
        }
    }
    if (!best.package) best.kind = MatchKind::None;
    return best;
}

Resolution PackageResolver::Resolve(const Component& app, const PlatformInfo& platform,
                                    std::string_view channel) {
    Resolution none;
    for (const Release& r : app.releases) {
        if (!(r.channel == "stable" || r.channel == channel)) continue;
        const Resolution res = ResolveRelease(r, platform);
        if (res.Available()) return res;
        if (!none.release) none.release = &r;   // the newest release, for "not available" and its systems
    }
    return none;
}

Result<InstallPlan> PackageResolver::Plan(const Catalog& catalog, const Component& app,
                                          const Release& release, const PlatformInfo& platform,
                                          const InstalledVersions& installed) {
    InstallPlan plan;
    std::set<std::string> done;       // "id@version" already in the plan
    std::vector<std::string> trail;   // the dependency chain being walked, for cycles
    std::string failure;
    ErrorCode failureCode = ErrorCode::None;

    std::function<bool(const Component&, const Release&, bool)> visit =
        [&](const Component& component, const Release& rel, bool isApp) -> bool {
            const std::string key = component.id + "@" + rel.version;
            for (const std::string& t : trail) {
                if (t == key) {
                    failureCode = ErrorCode::DependencyError;
                    failure = "The dependencies of " + app.name + " form a cycle at " + key;
                    return false;
                }
            }
            if (done.count(key)) return true;
            trail.push_back(key);
            for (const Dependency& d : rel.depends) {
                const Component* dep = catalog.FindComponent(d.id);
                if (!dep) {
                    failureCode = ErrorCode::DependencyError;
                    failure = app.name + " needs " + d.id + ", which the catalog does not have";
                    return false;
                }
                const Release* depRelease = dep->Find(d.version);
                if (!depRelease) {
                    failureCode = ErrorCode::DependencyError;
                    failure = app.name + " needs " + dep->name + " " + d.version +
                              ", which the catalog does not have";
                    return false;
                }
                if (!visit(*dep, *depRelease, false)) return false;
            }
            trail.pop_back();
            InstallStep step;
            step.component = &component;
            step.release = &rel;
            step.alreadyInstalled = installed.Has(component.id, rel.version);
            const Resolution res = ResolveRelease(rel, platform);
            if (!res.Available()) {
                if (step.alreadyInstalled) {
                    // Installed from elsewhere, nothing to fetch: fine.
                    done.insert(key);
                    plan.steps.push_back(step);
                    return true;
                }
                failureCode = isApp ? ErrorCode::NotAvailable : ErrorCode::DependencyError;
                failure = isApp ? app.name + " is not available for your system"
                                : app.name + " needs " + component.name + " " + rel.version +
                                      ", which is not available for your system";
                return false;
            }
            step.package = res.package;
            step.match = res.kind;
            if (!step.alreadyInstalled) plan.bytesToDownload += res.package->size;
            done.insert(key);
            plan.steps.push_back(step);
            return true;
        };

    // The app's own package first: "not available for your system" is the
    // answer when the app has no build for the machine, whatever its
    // dependencies have.
    if (!ResolveRelease(release, platform).Available()) {
        return Result<InstallPlan>::Failure(ErrorCode::NotAvailable, app.name + " is not available for your system",
                                            "available for " + SupportedSystems(release));
    }
    if (!visit(app, release, true)) {
        return Result<InstallPlan>::Failure(failureCode, failure);
    }
    return Result<InstallPlan>::Success(std::move(plan));
}

std::string PackageResolver::SupportedSystems(const Release& release) {
    std::set<std::string> systems;
    for (const Package& p : release.packages) systems.insert(OSName(p.target.os) + "/" + ArchName(p.target.arch));
    std::string out;
    for (const std::string& s : systems) {
        if (!out.empty()) out += ", ";
        out += s;
    }
    return out;
}

} // namespace UltraStore
