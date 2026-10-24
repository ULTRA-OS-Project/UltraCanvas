// Apps/UltraStore/core/PackageInstaller.h
// The seam between the store and UltraInstaller, the part that unpacks a
// verified package into the per-user tree and remembers it. Nothing here is
// implemented yet: v1 fixes the interface and the registry's shape so the
// installer (the next pull request) and the store's UI are written against
// the same words, and so a test can hand the planner a fake installer.
//
// The layout UltraInstaller fills (Docs/UltraStore/README.md, "On disk"):
//   <root>/apps/<id>/<version>/...          an app, one folder per version
//   <root>/components/<id>/<version>/...    a runtime or library, side by side
//   <root>/apps/<id>/current -> <version>   what the launcher starts
//   <root>/registry.sqlite                  what is installed (IInstallRegistry)
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "PackageResolver.h"
#include "UltraStoreTypes.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace UltraStore {

// Progress of one install step: bytes unpacked so far of the total, and the
// file being written. Returning false asks the installer to stop and undo.
using InstallProgress = std::function<bool(uint64_t done, uint64_t total, const std::string& file)>;

// One row of the registry: an installed app or component.
struct InstalledRecord {
    ComponentKind kind = ComponentKind::App;
    std::string id;
    std::string version;
    std::string folder;        // UTF-8, the <id>/<version> folder
    std::string entry;         // the launchable file (apps), relative to folder
    std::string sha256;        // of the package it came from
    std::string installedAt;   // ISO 8601, UTC
    std::string channel;       // the channel it was installed from
    std::vector<Dependency> depends;   // what it was installed with (for the GC)
    bool current = true;       // the version the launcher starts (apps)
};

// The registry of what is installed, kept by UltraInstaller on
// UltraDatabase (SQLite) under <root>/registry.sqlite. Versions are exact:
// two versions of one runtime are two rows.
class IInstallRegistry {
public:
    virtual ~IInstallRegistry() = default;
    virtual Result<std::vector<InstalledRecord>> All() const = 0;
    virtual Result<std::vector<InstalledRecord>> Of(std::string_view id) const = 0;
    virtual Result<void> Add(const InstalledRecord& record) = 0;
    virtual Result<void> Remove(std::string_view id, std::string_view version) = 0;
    virtual Result<void> SetCurrent(std::string_view id, std::string_view version) = 0;
    // The (id, version) pairs the resolver's plan skips.
    virtual InstalledVersions Versions() const = 0;
};

// Unpacks one verified package and records it; removes one.
class IPackageInstaller {
public:
    virtual ~IPackageInstaller() = default;

    // Unpack the downloaded file (already hash-checked by the caller) for the
    // step into its <id>/<version> folder, atomically: into a temporary
    // folder beside it, renamed into place when complete, nothing left on
    // failure. Formats: "zip", "7z", "tar.zst" (VirtualFS / libarchive),
    // "appimage" (copied, made executable).
    virtual Result<void> Install(const InstallStep& step, const std::string& archivePath,
                                 const InstallProgress& progress) = 0;

    // Remove one version's folder and its registry row. An app's current
    // version moves to the newest remaining one, or the app is gone.
    virtual Result<void> Remove(std::string_view id, std::string_view version) = 0;

    // Remove the components no installed app depends on any more (the
    // older runtimes after every app moved on). Returns what was removed.
    virtual Result<std::vector<InstalledRecord>> CollectGarbage() = 0;

    virtual IInstallRegistry& Registry() = 0;
};

} // namespace UltraStore
