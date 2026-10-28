// Apps/UltraStore/core/StoreEngine.cpp
// Install, update, remove, collect (StoreEngine.h).
// Version: 0.2.0 - the URL signer for restricted packages
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StoreEngine.h"

#include "UltraCanvasPathUtf8.h"

#include <filesystem>
#include <system_error>

namespace UltraStore {

namespace fs = std::filesystem;
using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

StoreEngine::StoreEngine(std::string rootUtf8) : root_(std::move(rootUtf8)) {}
StoreEngine::~StoreEngine() = default;

void StoreEngine::SetUrlSigner(UrlSigner signer) {
    std::lock_guard<std::mutex> lock(signerMutex_);
    signer_ = std::move(signer);
}

StoreEngine::UrlSigner StoreEngine::Signer() const {
    std::lock_guard<std::mutex> lock(signerMutex_);
    return signer_;
}

std::string StoreEngine::CacheDir() const { return PathToUtf8(PathFromUtf8(root_) / "cache"); }
std::string StoreEngine::DownloadsDir() const { return PathToUtf8(PathFromUtf8(root_) / "downloads"); }

Result<void> StoreEngine::Open() {
    if (root_.empty()) return Result<void>::Failure(ErrorCode::InvalidArgument, "The store has no folder");
    std::error_code ec;
    const fs::path root = PathFromUtf8(root_);
    for (const char* sub : {"cache", "downloads", "apps", "components"}) {
        fs::create_directories(root / sub, ec);
        if (ec) return Result<void>::Failure(ErrorCode::IoError, "The store folder could not be created", PathToUtf8(root / sub));
    }
    if (const auto r = registry_.Open(PathToUtf8(root / "registry.sqlite")); !r) return r;
    downloader_ = std::make_unique<Downloader>(DownloadsDir());
    installer_ = std::make_unique<ArchiveInstaller>(root_, registry_);
    return Result<void>::Succeed();
}

Result<InstallOutcome> StoreEngine::Run(const Catalog& catalog, const Component& app, const Release& release,
                                        const PlatformInfo& platform, const EngineCallbacks& callbacks) {
    using R = Result<InstallOutcome>;
    if (!registry_.IsOpen()) return R::Failure(ErrorCode::InvalidArgument, "The store is not open");
    const Result<InstallPlan> plan = PackageResolver::Plan(catalog, app, release, platform, registry_.Versions());
    if (!plan) return R::Failure(plan.error.code, plan.error.message, plan.error.detail);

    InstallOutcome outcome;
    outcome.plan = plan.Value();
    for (const InstallStep& step : outcome.plan.steps) {
        if (step.alreadyInstalled) continue;
        if (callbacks.onStep) callbacks.onStep(step);
        if (!ArchiveInstaller::SupportsFormat(step.package->format)) {
            return R::Failure(ErrorCode::NotAvailable,
                              "This UltraStore cannot unpack " + step.package->format + " packages (" +
                                  step.component->name + ")");
        }
        DownloadProgress progress;
        if (callbacks.onDownload) {
            progress = [&](int64_t done, int64_t total) { return callbacks.onDownload(step, done, total); };
        }
        // A restricted package needs the URL the payment service signs for a
        // buyer; a free one is fetched as the catalog says.
        Package fetched = *step.package;
        if (fetched.url.find("/restricted/") != std::string::npos) {
            const UrlSigner signer = Signer();
            const std::string signed_ = signer ? signer(*step.package, app) : std::string();
            if (signed_.empty()) {
                return R::Failure(ErrorCode::NotAvailable, step.component->name + " is a paid app: buy it first, then install",
                                  "the package is in restricted/ and this machine has no licence for " + app.id);
            }
            fetched.url = signed_;
        }
        const Result<std::string> file = downloader_->Fetch(fetched, progress);
        if (!file) return R::Failure(file.error.code, step.component->name + ": " + file.error.message, file.error.detail);
        outcome.bytesDownloaded += step.package->size;

        InstallProgress unpack;
        if (callbacks.onUnpack) {
            unpack = [&](uint64_t done, uint64_t total, const std::string& f) { return callbacks.onUnpack(step, done, total, f); };
        }
        const Result<void> installed = installer_->Install(step, file.Value(), unpack);
        if (!installed) return R::Failure(installed.error.code, step.component->name + ": " + installed.error.message, installed.error.detail);
        std::error_code ec;
        fs::remove(PathFromUtf8(file.Value()), ec);   // unpacked: the download has done its work
        outcome.installed.push_back(step.component->id + " " + step.release->version);
    }
    return R::Succeed(std::move(outcome));
}

Result<InstallOutcome> StoreEngine::Install(const Catalog& catalog, std::string_view appId, const PlatformInfo& platform,
                                            std::string_view channel, const EngineCallbacks& callbacks) {
    using R = Result<InstallOutcome>;
    const Component* app = catalog.FindApp(appId);
    if (!app) return R::Failure(ErrorCode::NotFound, "The catalog has no app " + std::string(appId));
    const Resolution res = PackageResolver::Resolve(*app, platform, channel);
    if (!res.Available()) {
        return R::Failure(ErrorCode::NotAvailable, app->name + " is not available for your system",
                          res.release ? "available for " + PackageResolver::SupportedSystems(*res.release) : std::string());
    }
    return Run(catalog, *app, *res.release, platform, callbacks);
}

Result<InstallOutcome> StoreEngine::Update(const Catalog& catalog, std::string_view appId, const PlatformInfo& platform,
                                           std::string_view channel, const EngineCallbacks& callbacks) {
    using R = Result<InstallOutcome>;
    const Result<InstalledRecord> current = registry_.Current(appId);
    if (!current) return R::Failure(current.error.code, current.error.message, current.error.detail);
    const Component* app = catalog.FindApp(appId);
    if (!app) return R::Failure(ErrorCode::NotFound, "The catalog no longer lists " + std::string(appId));
    const std::string useChannel = channel.empty() ? current.Value().channel : std::string(channel);
    const Resolution res = PackageResolver::Resolve(*app, platform, useChannel);
    if (!res.Available()) {
        return R::Failure(ErrorCode::NotAvailable, app->name + " has no release for your system any more");
    }
    if (!VersionLess(current.Value().version, res.release->version)) {
        // Up to date. The plan for the installed release says so with every
        // step already installed - and repairs a component that went
        // missing; a release the catalog dropped has nothing to plan.
        if (const Release* same = app->Find(current.Value().version)) return Run(catalog, *app, *same, platform, callbacks);
        return R::Succeed(InstallOutcome{});
    }
    Result<InstallOutcome> outcome = Run(catalog, *app, *res.release, platform, callbacks);
    if (!outcome) return outcome;
    // The new version is current now: the older ones and what only they
    // needed go.
    const auto rows = registry_.Of(appId);
    if (rows) {
        for (const InstalledRecord& r : rows.Value()) {
            if (!r.current) installer_->Remove(r.id, r.version);
        }
    }
    installer_->CollectGarbage();
    return outcome;
}

Result<std::vector<std::pair<std::string, Result<InstallOutcome>>>> StoreEngine::UpdateAll(
    const Catalog& catalog, const PlatformInfo& platform, const EngineCallbacks& callbacks) {
    using R = Result<std::vector<std::pair<std::string, Result<InstallOutcome>>>>;
    const auto all = registry_.All();
    if (!all) return R::Failure(all.error.code, all.error.message, all.error.detail);
    std::vector<std::pair<std::string, Result<InstallOutcome>>> out;
    for (const InstalledRecord& r : all.Value()) {
        if (r.kind != ComponentKind::App || !r.current) continue;
        out.emplace_back(r.id, Update(catalog, r.id, platform, {}, callbacks));
    }
    return R::Succeed(std::move(out));
}

Result<std::vector<InstalledRecord>> StoreEngine::Remove(std::string_view appId) {
    using R = Result<std::vector<InstalledRecord>>;
    const auto rows = registry_.Of(appId);
    if (!rows) return R::Failure(rows.error.code, rows.error.message, rows.error.detail);
    if (rows.Value().empty()) return R::Failure(ErrorCode::NotFound, std::string(appId) + " is not installed");
    std::vector<InstalledRecord> removed;
    for (const InstalledRecord& r : rows.Value()) {
        if (const auto res = installer_->Remove(r.id, r.version); !res) return R::Failure(res.error.code, res.error.message, res.error.detail);
        removed.push_back(r);
    }
    const auto collected = installer_->CollectGarbage();
    if (!collected) return collected;
    for (const InstalledRecord& r : collected.Value()) removed.push_back(r);
    return R::Succeed(std::move(removed));
}

} // namespace UltraStore
