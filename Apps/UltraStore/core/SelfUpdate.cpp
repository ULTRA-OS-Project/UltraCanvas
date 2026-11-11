// Apps/UltraStore/core/SelfUpdate.cpp
// The store as one of its own apps (SelfUpdate.h).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "SelfUpdate.h"

#include "StoreQuery.h"

#include "UltraCanvasPathUtf8.h"

#include <filesystem>
#include <set>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#elif defined(__APPLE__)
#  include <mach-o/dyld.h>
#endif

namespace UltraStore {

namespace fs = std::filesystem;
using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

Result<Catalog> ExtractSelfCatalog(const Catalog& catalog, std::string_view appId) {
    const Component* app = catalog.FindApp(appId);
    if (!app) return Result<Catalog>::Failure(ErrorCode::NotFound, "The catalog does not list " + std::string(appId));

    Catalog out;
    out.schema = kSelfCatalogSchema;
    out.sequence = catalog.sequence;
    out.generated = catalog.generated;
    out.trustedKeys = catalog.trustedKeys;
    for (const Publisher& p : catalog.publishers) if (p.id == app->publisher) out.publishers.push_back(p);
    out.apps.push_back(*app);

    // Every component a release of the app names, and what those name.
    std::set<std::string> seen;
    std::vector<const Component*> frontier{app};
    while (!frontier.empty()) {
        const Component* c = frontier.back();
        frontier.pop_back();
        for (const Release& r : c->releases) {
            for (const Dependency& d : r.depends) {
                if (!seen.insert(d.id).second) continue;
                if (const Component* dep = catalog.FindComponent(d.id)) frontier.push_back(dep);
            }
        }
    }
    // In the catalog's order, so the same catalog gives the same bytes.
    for (const Component& c : catalog.components) if (seen.count(c.id)) out.components.push_back(c);
    return Result<Catalog>::Succeed(std::move(out));
}

CatalogClientConfig SelfCatalogConfig(const CatalogClientConfig& main) {
    CatalogClientConfig config = main;
    config.fileName = kSelfCatalogFile;
    if (!config.cacheDir.empty()) config.cacheDir = PathToUtf8(PathFromUtf8(config.cacheDir) / "self");
    return config;
}

CatalogClientConfig SelfCatalogConfig(const CatalogClient& main) {
    CatalogClientConfig config = SelfCatalogConfig(main.Config());
    config.trustedKeys = main.KeysTrustedNow();
    return config;
}

std::string RunningExecutable() {
    fs::path path;
#if defined(_WIN32)
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) return {};
    buffer.resize(length);
    path = fs::path(buffer);   // path-string-ok: wide
#elif defined(__APPLE__)
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (size == 0 || _NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    path = PathFromUtf8(buffer.c_str());
#else
    std::error_code linkError;
    path = fs::read_symlink("/proc/self/exe", linkError);
    if (linkError) return {};
#endif
    std::error_code ec;
    const fs::path resolved = fs::weakly_canonical(path, ec);
    return PathToUtf8(ec ? path : resolved);
}

RunningInstall FindRunningInstall(const std::string& rootUtf8, const std::string& executableUtf8) {
    RunningInstall out;
    if (rootUtf8.empty() || executableUtf8.empty()) return out;
    std::error_code ec;
    fs::path apps = fs::weakly_canonical(PathFromUtf8(rootUtf8) / "apps", ec);
    if (ec) return out;
    fs::path exe = fs::weakly_canonical(PathFromUtf8(executableUtf8), ec);
    if (ec) return out;
    // <apps>/<id>/<version>/...: the first two names below apps.
    const fs::path relative = exe.lexically_relative(apps);
    std::vector<fs::path> parts(relative.begin(), relative.end());
    if (parts.size() < 3 || parts[0] == PathFromUtf8("..") || parts[0] == PathFromUtf8(".")) return out;
    out.id = PathToUtf8(parts[0]);
    out.version = PathToUtf8(parts[1]);
    out.folder = PathToUtf8(apps / parts[0] / parts[1]);
    return out;
}

RunningInstall ProtectRunningInstall(StoreEngine& engine, const std::string& executableUtf8) {
    RunningInstall running = FindRunningInstall(engine.Root(), executableUtf8);
    if (running.Managed()) engine.Installer().SetInUse(running.id, running.version);
    return running;
}

Result<SelfUpdateCheck> CheckSelfUpdate(const Catalog& selfCatalog, StoreEngine& engine, const PlatformInfo& platform,
                                        std::string_view channel) {
    using R = Result<SelfUpdateCheck>;
    const Component* app = selfCatalog.FindApp(kStoreAppId);
    if (!app) return R::Failure(ErrorCode::NotFound, "The store's own catalog does not list UltraStore", kStoreAppId);
    SelfUpdateCheck check;
    std::string useChannel(channel);
    if (const Result<InstalledRecord> current = engine.Registry().Current(kStoreAppId)) {
        check.installedVersion = current.Value().version;
        if (useChannel.empty()) useChannel = current.Value().channel;
    }
    if (useChannel.empty()) useChannel = "stable";
    const Resolution res = PackageResolver::Resolve(*app, platform, useChannel);
    if (!res.Available()) {
        return R::Failure(ErrorCode::NotAvailable, "UltraStore has no release for your system",
                          res.release ? "available for " + PackageResolver::SupportedSystems(*res.release) : std::string());
    }
    check.newestVersion = res.release->version;
    check.available = check.installedVersion.empty() || VersionLess(check.installedVersion, check.newestVersion);
    return R::Succeed(std::move(check));
}

Result<SelfUpdateOutcome> InstallOrUpdateStore(StoreEngine& engine, const Catalog& selfCatalog, const PlatformInfo& platform,
                                               std::string_view channel, const EngineCallbacks& callbacks) {
    using R = Result<SelfUpdateOutcome>;
    const Result<InstalledRecord> before = engine.Registry().Current(kStoreAppId);
    const bool installed = static_cast<bool>(before);
    // The version current until now stays (see the header): named in use
    // for this call unless the caller already named the one it runs from.
    ArchiveInstaller& installer = engine.Installer();
    const bool guard = installed && installer.InUseId().empty();
    if (guard) installer.SetInUse(kStoreAppId, before.Value().version);
    const Result<InstallOutcome> done = installed
        ? engine.Update(selfCatalog, kStoreAppId, platform, channel, callbacks)
        : engine.Install(selfCatalog, kStoreAppId, platform, channel.empty() ? std::string_view("stable") : channel, callbacks);
    if (guard) installer.SetInUse({}, {});
    if (!done) return R::Failure(done.error.code, done.error.message, done.error.detail);

    SelfUpdateOutcome out;
    out.install = done.Value();
    out.changed = !out.install.installed.empty();
    const Result<InstalledRecord> now = engine.Registry().Current(kStoreAppId);
    if (!now) return R::Failure(now.error.code, "UltraStore was installed but the registry does not show it", now.error.message);
    out.version = now.Value().version;
    out.launchPath = LaunchPath(now.Value());
    return R::Succeed(std::move(out));
}

Result<std::vector<InstalledRecord>> RemoveOldStoreVersions(StoreEngine& engine) {
    using R = Result<std::vector<InstalledRecord>>;
    const auto rows = engine.Registry().Of(kStoreAppId);
    if (!rows) return R::Failure(rows.error.code, rows.error.message, rows.error.detail);
    std::vector<InstalledRecord> removed;
    for (const InstalledRecord& r : rows.Value()) {
        if (r.current || engine.Installer().IsInUse(r.id, r.version)) continue;
        // A folder another process still holds (Windows) stays for the next start.
        if (engine.Installer().Remove(r.id, r.version)) removed.push_back(r);
    }
    if (removed.empty()) return R::Succeed(std::move(removed));
    if (const auto collected = engine.CollectGarbage()) {
        for (const InstalledRecord& r : collected.Value()) removed.push_back(r);
    }
    return R::Succeed(std::move(removed));
}

LauncherEntry StoreLauncherEntry(const InstalledRecord& record) {
    LauncherEntry entry;
    entry.id = kStoreAppId;
    entry.name = "UltraStore";
    // The words of Apps/UltraStore/UltraStore.desktop, which a packaged
    // store installs: the same entry, whichever way the store arrived.
    entry.comment = "Install, update and remove the ULTRA OS applications";
    entry.categories = "System;PackageManager;";
    entry.mimeTypes = "x-scheme-handler/ultrastore;";
    entry.windowClass = "UltraStore";
    entry.executable = LaunchPath(record);
    if (entry.executable.empty()) return entry;
    std::error_code ec;
    const fs::path folder = PathFromUtf8(record.folder);
    const fs::path beside = PathFromUtf8(entry.executable).parent_path();
    for (const fs::path& base : {beside, folder}) {
        const fs::path icon = base / "media" / "appicon" / "UltraStore.png";
        if (fs::is_regular_file(icon, ec)) { entry.icon = PathToUtf8(icon); break; }
    }
    return entry;
}

Result<std::string> WriteStoreLauncher(StoreEngine& engine, const std::string& folderOverride) {
    const Result<InstalledRecord> current = engine.Registry().Current(kStoreAppId);
    if (!current) return Result<std::string>::Failure(current.error.code, "UltraStore is not installed in this store folder", current.error.message);
    LauncherEntry entry = StoreLauncherEntry(current.Value());
    entry.folderOverride = folderOverride;
    return WriteLauncher(entry);
}

std::string RefreshStoreLauncher(StoreEngine& engine, const std::string& folderOverride) {
    const Result<InstalledRecord> current = engine.Registry().Current(kStoreAppId);
    if (!current) return {};
    LauncherEntry entry = StoreLauncherEntry(current.Value());
    entry.folderOverride = folderOverride;
    const std::string path = LauncherPath(entry);
    std::error_code ec;
    // symlink_status: on macOS the entry is a link, and one whose old target
    // is gone still counts as there.
    if (path.empty() || !fs::exists(fs::symlink_status(PathFromUtf8(path), ec))) return {};
    const Result<std::string> written = WriteLauncher(entry);
    return written ? written.Value() : std::string();
}

} // namespace UltraStore
