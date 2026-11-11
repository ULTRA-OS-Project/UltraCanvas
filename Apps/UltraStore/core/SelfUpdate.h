// Apps/UltraStore/core/SelfUpdate.h
// The store as one of its own apps. UltraStore is installed into the tree
// it manages - <root>/apps/org.ultraos.ultrastore/<version>/ with the
// runtime it needs under components/ - so getting it onto a machine and
// moving it to a newer version are the engine's ordinary Install and Update.
// Two things make that work for the program doing it:
//
//   The store's own catalog. `ultrastore.json` (+ `.sig`) beside
//   catalog.json holds the store's app entry and the components it depends
//   on, nothing else, in the catalog's own format, signed with the same
//   keys. Its schema stays at kSelfCatalogSchema: members are added, never
//   changed, so every store since this one reads it - which is the point.
//   A catalog.json whose schema is too new for the running store
//   (SchemaTooNew) cannot be read, so it cannot tell the store where its
//   update is; the store's own catalog can, and the setup program
//   (Apps/UltraStore/setup) reads the same file to install the store on a
//   machine that has none.
//
//   The running version. A store that runs from the tree names its version
//   to the installer (ArchiveInstaller::SetInUse), which then neither
//   removes it nor collects it: the update unpacks the new version beside
//   it and moves `current`, the running copy keeps its files, and the next
//   start - from the new version - collects the old one.
//
// A store that does not run from the tree (a distribution package, an MSIX,
// a build folder) is not managed: FindRunningInstall answers so, and it is
// updated the way it was installed.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "Catalog.h"
#include "CatalogClient.h"
#include "Launcher.h"
#include "StoreEngine.h"

#include <string>
#include <string_view>

namespace UltraStore {

inline constexpr const char* kStoreAppId = "org.ultraos.ultrastore";
inline constexpr const char* kSelfCatalogFile = "ultrastore.json";
// The schema the store's own catalog is written with, whatever
// kSupportedSchema has become: see the note above.
inline constexpr int kSelfCatalogSchema = 2;

// The catalog cut down to one app and the components its releases depend
// on (transitively), with the app's publisher, the trusted keys, the
// sequence and the date: what the admin backend's publish writes as
// ultrastore.json. No codes and no payment service - the store is free.
// NotFound when the catalog does not list the app.
Result<Catalog> ExtractSelfCatalog(const Catalog& catalog, std::string_view appId = kStoreAppId);

// Where the store's own catalog is fetched: the main client's servers, the
// keys it trusts now (the configured ones and the ones the last catalog
// announced), the file name above, and a cache folder of its own
// (<cacheDir>/self), so its sequence and ETag are kept apart.
CatalogClientConfig SelfCatalogConfig(const CatalogClient& main);
CatalogClientConfig SelfCatalogConfig(const CatalogClientConfig& main);

// This process's program file, UTF-8, symlinks resolved; empty when the
// system does not say.
std::string RunningExecutable();

// The installed version a program file belongs to: the one whose folder
// <root>/apps/<id>/<version>/ holds it. Empty ids when it is outside the
// tree.
struct RunningInstall {
    std::string id, version;
    std::string folder;   // UTF-8, <root>/apps/<id>/<version>
    bool Managed() const { return !id.empty(); }
};
RunningInstall FindRunningInstall(const std::string& rootUtf8, const std::string& executableUtf8);

// Tells the engine which version this process runs from, when it runs from
// the engine's tree (see above); returns what it found. Call after Open.
RunningInstall ProtectRunningInstall(StoreEngine& engine, const std::string& executableUtf8 = RunningExecutable());

// What the store's own catalog offers this machine.
struct SelfUpdateCheck {
    std::string installedVersion;   // the registry's current version of the store; empty: not installed in the tree
    std::string newestVersion;      // the newest release with a package for the machine on the channel
    bool available = false;         // newestVersion is newer than installedVersion, or nothing is installed
};
// NotFound when the catalog does not list the store, NotAvailable when no
// release has a package for the machine.
Result<SelfUpdateCheck> CheckSelfUpdate(const Catalog& selfCatalog, StoreEngine& engine, const PlatformInfo& platform,
                                        std::string_view channel = {});

// Installs the store when the tree has none, updates it when it has: the
// newest release for the machine with the components it needs. The outcome
// names the version now current and the file to start.
//
// The version that was current before is left in place, whoever calls: a
// store window may be running from it (this process, or another one when
// the setup program or the command line does the update), and its files
// must not go from under it. RemoveOldStoreVersions takes it away at the
// store's next start.
struct SelfUpdateOutcome {
    InstallOutcome install;
    std::string version;      // the store's current version afterwards
    std::string launchPath;   // UTF-8, <root>/apps/<id>/<version>/<entry>; empty when the package names no entry
    bool changed = false;     // something was installed now
};
Result<SelfUpdateOutcome> InstallOrUpdateStore(StoreEngine& engine, const Catalog& selfCatalog, const PlatformInfo& platform,
                                               std::string_view channel = {}, const EngineCallbacks& callbacks = {});

// Removes the store's versions that are neither current nor the one this
// process runs from, then the components nothing needs any more: what an
// update left behind for the copy that was running. The store calls it at
// start. Returns what went.
Result<std::vector<InstalledRecord>> RemoveOldStoreVersions(StoreEngine& engine);

// The store's entry in the application menu for an installed version: its
// program file, and the icon the package carries (media/appicon/
// UltraStore.png beside the program or at the top of the folder) when it
// does. An empty executable when the record names no entry.
LauncherEntry StoreLauncherEntry(const InstalledRecord& record);

// Writes the menu entry for the store's current version (after the setup
// program installed it, after an update moved it); returns the entry's
// path. NotFound when the store is not installed in the tree.
Result<std::string> WriteStoreLauncher(StoreEngine& engine, const std::string& folderOverride = {});

// The same, but only when the entry is already there: after an update the
// entry follows the store to its new version, and a person who wanted no
// entry (ultrastore-setup --no-menu, or one they deleted) gets none back.
// Returns the path written, or an empty one when nothing was.
std::string RefreshStoreLauncher(StoreEngine& engine, const std::string& folderOverride = {});

} // namespace UltraStore
