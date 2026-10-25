// Apps/UltraStore/core/Installer.h
// UltraInstaller: unpacks a verified package into the per-user tree and
// records it (the IPackageInstaller of PackageInstaller.h). The tree:
//   <root>/apps/<id>/<version>/           an app version
//   <root>/apps/<id>/current              -> <version> (a symlink; where none
//                                            can be made, a file naming it)
//   <root>/components/<id>/<version>/     a runtime or library, side by side
// An install unpacks into <version>.installing beside the destination and
// renames it into place when complete, so a failure leaves nothing half
// installed; zip, 7z and tar.* go through VirtualFS (libarchive), an
// AppImage is copied and made executable.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "PackageInstaller.h"

#include <string>
#include <vector>

namespace UltraStore {

class ArchiveInstaller final : public IPackageInstaller {
public:
    // root: the store's folder (UltraCanvasLocalDataFolderRoot()/UltraStore
    // in the program; a temporary folder in a test). The registry is the
    // caller's and outlives the installer.
    ArchiveInstaller(std::string rootUtf8, IInstallRegistry& registry);

    Result<void> Install(const InstallStep& step, const std::string& archivePath,
                         const InstallProgress& progress) override;
    Result<void> Remove(std::string_view id, std::string_view version) override;
    Result<std::vector<InstalledRecord>> CollectGarbage() override;
    IInstallRegistry& Registry() override { return registry_; }

    // <root>/apps/<id>/<version> or <root>/components/<id>/<version>, UTF-8.
    std::string FolderFor(ComponentKind kind, std::string_view id, std::string_view version) const;
    // <root>/apps/<id>/current, UTF-8.
    std::string CurrentLinkFor(std::string_view id) const;
    // The version <root>/apps/<id>/current points at, or "" when none.
    std::string CurrentVersionOf(std::string_view id) const;
    // Whether a package format is one this installer unpacks here.
    static bool SupportsFormat(const std::string& format);

    const std::string& Root() const { return root_; }

private:
    Result<void> PointCurrentAt(std::string_view id, std::string_view version);
    void ClearCurrent(std::string_view id);

    std::string root_;
    IInstallRegistry& registry_;
};

} // namespace UltraStore
