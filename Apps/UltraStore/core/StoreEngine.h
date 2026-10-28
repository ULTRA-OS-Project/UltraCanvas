// Apps/UltraStore/core/StoreEngine.h
// The store's operations in one place, for the command line and the window
// alike: install an app with the layers it needs, update it, remove it,
// collect what nothing uses any more. Resolves with PackageResolver, fetches
// with Downloader, unpacks with ArchiveInstaller, remembers in
// SqliteRegistry; the layout is Installer.h's under one root.
// Version: 0.2.0 - the URL signer for restricted packages
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "Catalog.h"
#include "Downloader.h"
#include "Installer.h"
#include "PackageResolver.h"
#include "PlatformInfo.h"
#include "Registry.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace UltraStore {

// What the caller hears while an operation runs. Each returns true to go on.
struct EngineCallbacks {
    std::function<void(const InstallStep& step)> onStep;                                  // a step begins
    std::function<bool(const InstallStep& step, int64_t done, int64_t total)> onDownload;   // bytes of its package
    std::function<bool(const InstallStep& step, uint64_t done, uint64_t total, const std::string& file)> onUnpack;
};

struct InstallOutcome {
    InstallPlan plan;                          // what was decided
    std::vector<std::string> installed;        // "id version" of what was unpacked now
    uint64_t bytesDownloaded = 0;
};

class StoreEngine {
public:
    // root: the store's folder; created on Open with cache/, downloads/,
    // apps/, components/ and registry.sqlite inside.
    explicit StoreEngine(std::string rootUtf8);
    ~StoreEngine();

    Result<void> Open();

    // Installs the app's newest release on the channel that has a package for
    // the machine, with every component it needs that is not there yet. An
    // app already at that version installs nothing and says so in the plan.
    Result<InstallOutcome> Install(const Catalog& catalog, std::string_view appId, const PlatformInfo& platform,
                                   std::string_view channel = "stable", const EngineCallbacks& callbacks = {});

    // Moves an installed app to the newest release available for the machine
    // on the channel it was installed from (or the one given), removes the
    // older app versions and collects unused components. NotFound when the
    // app is not installed; a plan with nothing to do when it is current.
    Result<InstallOutcome> Update(const Catalog& catalog, std::string_view appId, const PlatformInfo& platform,
                                  std::string_view channel = {}, const EngineCallbacks& callbacks = {});

    // Every installed app in turn; the outcomes in the same order.
    Result<std::vector<std::pair<std::string, Result<InstallOutcome>>>> UpdateAll(
        const Catalog& catalog, const PlatformInfo& platform, const EngineCallbacks& callbacks = {});

    // Removes every installed version of the app, then the components nothing
    // depends on any more.
    Result<std::vector<InstalledRecord>> Remove(std::string_view appId);

    Result<std::vector<InstalledRecord>> CollectGarbage() { return installer_->CollectGarbage(); }
    Result<std::vector<InstalledRecord>> Installed() const { return registry_.All(); }

    SqliteRegistry& Registry() { return registry_; }
    Downloader& Downloads() { return *downloader_; }
    ArchiveInstaller& Installer() { return *installer_; }
    const std::string& Root() const { return root_; }
    std::string CacheDir() const;
    std::string DownloadsDir() const;

    // The package of a paid app is served from restricted/, which the web
    // server gives out only on a URL the payment service signed: the signer
    // turns the catalog's URL into that, or returns "" when the app was not
    // bought here (the install then fails with NotAvailable). Called on the
    // thread that installs.
    using UrlSigner = std::function<std::string(const Package& package, const Component& app)>;
    void SetUrlSigner(UrlSigner signer);

private:
    Result<InstallOutcome> Run(const Catalog& catalog, const Component& app, const Release& release,
                               const PlatformInfo& platform, const EngineCallbacks& callbacks);

    UrlSigner Signer() const;
    std::string root_;
    SqliteRegistry registry_;
    mutable std::mutex signerMutex_;
    UrlSigner signer_;
    std::unique_ptr<Downloader> downloader_;
    std::unique_ptr<ArchiveInstaller> installer_;
};

} // namespace UltraStore
