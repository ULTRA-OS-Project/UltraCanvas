// Apps/UltraStore/core/Installer.cpp
// Unpacking and removing installed versions (Installer.h).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "Installer.h"
#include "CatalogClient.h"   // NowIso8601Utc

#include "UltraCanvasPathUtf8.h"
#if defined(ULTRACANVAS_HAS_VIRTUALFS)
#include "UltraCanvasVirtualFSBridge.h"
#endif

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <set>
#include <system_error>

namespace UltraStore {

namespace fs = std::filesystem;
using UltraCanvas::PathFromUtf8;
using UltraCanvas::PathToUtf8;

namespace {

bool IsArchiveFormat(const std::string& format) {
    return format == "zip" || format == "7z" || format == "tar" || format == "tar.gz" || format == "tgz" ||
           format == "tar.xz" || format == "tar.zst" || format == "tar.bz2";
}

std::string KindFolder(ComponentKind kind) { return kind == ComponentKind::App ? "apps" : "components"; }

// The newest version among records.
const InstalledRecord* Newest(const std::vector<InstalledRecord>& records) {
    const InstalledRecord* best = nullptr;
    for (const InstalledRecord& r : records) if (!best || VersionLess(best->version, r.version)) best = &r;
    return best;
}

void MakeExecutable(const fs::path& p) {
    std::error_code ec;
    fs::permissions(p, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add, ec);
}

} // namespace

ArchiveInstaller::ArchiveInstaller(std::string rootUtf8, IInstallRegistry& registry)
    : root_(std::move(rootUtf8)), registry_(registry) {}

bool ArchiveInstaller::SupportsFormat(const std::string& format) {
    if (format == "appimage") return true;
#if defined(ULTRACANVAS_HAS_VIRTUALFS)
    if (!IsArchiveFormat(format)) return false;
    const std::string probe = "package." + (format == "tgz" ? std::string("tar.gz") : format);
    return UltraCanvas::UltraCanvasVirtualFSBridge::IsArchiveFormat(probe);
#else
    (void)IsArchiveFormat;
    return false;
#endif
}

std::string ArchiveInstaller::FolderFor(ComponentKind kind, std::string_view id, std::string_view version) const {
    return PathToUtf8(PathFromUtf8(root_) / PathFromUtf8(KindFolder(kind)) / PathFromUtf8(std::string(id)) / PathFromUtf8(std::string(version)));
}

std::string ArchiveInstaller::CurrentLinkFor(std::string_view id) const {
    return PathToUtf8(PathFromUtf8(root_) / "apps" / PathFromUtf8(std::string(id)) / "current");
}

std::string ArchiveInstaller::CurrentVersionOf(std::string_view id) const {
    std::error_code ec;
    const fs::path link = PathFromUtf8(CurrentLinkFor(id));
    if (fs::is_symlink(link, ec)) {
        const fs::path target = fs::read_symlink(link, ec);
        if (!ec) return PathToUtf8(target.filename());
    }
    std::ifstream in(link, std::ios::binary);
    std::string version;
    if (in) std::getline(in, version);
    while (!version.empty() && (version.back() == '\r' || version.back() == '\n' || version.back() == ' ')) version.pop_back();
    return version;
}

Result<void> ArchiveInstaller::PointCurrentAt(std::string_view id, std::string_view version) {
    std::error_code ec;
    const fs::path link = PathFromUtf8(CurrentLinkFor(id));
    fs::remove(link, ec);
    ec.clear();
    // Relative, so the whole tree can be moved. Where a symlink cannot be
    // made (Windows without developer mode), a file names the version; the
    // registry's `current` column is the truth either way.
    fs::create_directory_symlink(PathFromUtf8(std::string(version)), link, ec);
    if (ec) {
        ec.clear();
        std::ofstream out(link, std::ios::binary | std::ios::trunc);
        out << version << '\n';
        if (!out) return Result<void>::Failure(ErrorCode::IoError, "The current-version marker could not be written", CurrentLinkFor(id));
    }
    return Result<void>::Succeed();
}

void ArchiveInstaller::ClearCurrent(std::string_view id) {
    std::error_code ec;
    fs::remove(PathFromUtf8(CurrentLinkFor(id)), ec);
}

Result<void> ArchiveInstaller::Install(const InstallStep& step, const std::string& archivePath,
                                       const InstallProgress& progress) {
    if (!step.component || !step.release || !step.package) {
        return Result<void>::Failure(ErrorCode::InvalidArgument, "The install step names no component, release or package");
    }
    const Component& component = *step.component;
    const Release& release = *step.release;
    const Package& package = *step.package;
    std::error_code ec;
    const fs::path archive = PathFromUtf8(archivePath);
    if (!fs::is_regular_file(archive, ec)) {
        return Result<void>::Failure(ErrorCode::IoError, "The package file is not there", archivePath);
    }
    if (!SupportsFormat(package.format)) {
        return Result<void>::Failure(ErrorCode::NotAvailable,
                                     "This UltraStore cannot unpack " + package.format + " packages",
                                     "libarchive missing from the build, or an unknown format");
    }

    const fs::path dest = PathFromUtf8(FolderFor(component.kind, component.id, release.version));
    const fs::path staging = dest.parent_path() / PathFromUtf8(PathToUtf8(dest.filename()) + ".installing");
    fs::remove_all(staging, ec);
    ec.clear();
    fs::create_directories(staging, ec);
    if (ec) return Result<void>::Failure(ErrorCode::IoError, "The install folder could not be created", PathToUtf8(staging));

    if (package.format == "appimage") {
        const std::string name = package.entry.empty() ? PathToUtf8(archive.filename()) : package.entry;
        const fs::path target = staging / PathFromUtf8(name);
        fs::create_directories(target.parent_path(), ec);
        fs::copy_file(archive, target, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            fs::remove_all(staging, ec);
            return Result<void>::Failure(ErrorCode::IoError, "The AppImage could not be copied", ec.message());
        }
        MakeExecutable(target);
        if (progress) progress(package.size, package.size, name);
    } else {
#if defined(ULTRACANVAS_HAS_VIRTUALFS)
        std::string error;
        const bool ok = UltraCanvas::UltraCanvasVirtualFSBridge::ExtractArchive(
            archivePath, PathToUtf8(staging),
            progress ? UltraCanvas::UCVFSProgressCallback([&](uint64_t done, uint64_t total, const std::string& file) {
                return progress(done, total, file);
            }) : UltraCanvas::UCVFSProgressCallback(nullptr),
            &error);
        if (!ok) {
            std::error_code ignore;
            fs::remove_all(staging, ignore);
            return Result<void>::Failure(ErrorCode::IoError, "The package could not be unpacked",
                                         error.empty() ? archivePath : error);
        }
#else
        fs::remove_all(staging, ec);
        return Result<void>::Failure(ErrorCode::NotAvailable, "This UltraStore was built without VirtualFS");
#endif
        // A zip made on Windows records no execute bit: the entry gets one.
        if (!package.entry.empty()) {
            const fs::path entry = staging / PathFromUtf8(package.entry);
            if (fs::is_regular_file(entry, ec)) MakeExecutable(entry);
        }
    }

    // Into place: a leftover of the same version goes first.
    fs::remove_all(dest, ec);
    ec.clear();
    fs::rename(staging, dest, ec);
    if (ec) {
        std::error_code ignore;
        fs::remove_all(staging, ignore);
        return Result<void>::Failure(ErrorCode::IoError, "The unpacked package could not be moved into place", ec.message());
    }

    InstalledRecord rec;
    rec.kind = component.kind;
    rec.id = component.id;
    rec.version = release.version;
    rec.folder = PathToUtf8(dest);
    rec.entry = package.entry;
    rec.sha256 = package.sha256;
    rec.installedAt = NowIso8601Utc();
    rec.channel = release.channel.empty() ? std::string("stable") : release.channel;
    rec.depends = release.depends;
    rec.current = component.kind == ComponentKind::App;
    if (const auto r = registry_.Add(rec); !r) return r;
    if (rec.current) return PointCurrentAt(rec.id, rec.version);
    return Result<void>::Succeed();
}

Result<void> ArchiveInstaller::Remove(std::string_view id, std::string_view version) {
    const auto rows = registry_.Of(id);
    if (!rows) return Result<void>::Failure(rows.error.code, rows.error.message, rows.error.detail);
    const InstalledRecord* row = nullptr;
    for (const InstalledRecord& r : rows.Value()) if (VersionEqual(r.version, version)) row = &r;
    if (!row) return Result<void>::Failure(ErrorCode::NotFound, std::string(id) + " " + std::string(version) + " is not installed");

    std::error_code ec;
    const fs::path folder = row->folder.empty() ? PathFromUtf8(FolderFor(row->kind, id, version)) : PathFromUtf8(row->folder);
    fs::remove_all(folder, ec);
    if (ec && fs::exists(folder, ec)) {
        return Result<void>::Failure(ErrorCode::IoError, "The install folder could not be removed", PathToUtf8(folder));
    }
    const bool wasCurrent = row->current;
    const ComponentKind kind = row->kind;
    if (const auto r = registry_.Remove(id, version); !r) return r;

    std::vector<InstalledRecord> remaining;
    for (const InstalledRecord& r : rows.Value()) if (!VersionEqual(r.version, version)) remaining.push_back(r);
    if (kind == ComponentKind::App) {
        if (remaining.empty()) {
            ClearCurrent(id);
            fs::remove_all(folder.parent_path(), ec);   // <root>/apps/<id>, now empty
        } else if (wasCurrent) {
            const InstalledRecord* next = Newest(remaining);
            if (const auto r = registry_.SetCurrent(id, next->version); !r) return r;
            if (const auto r = PointCurrentAt(id, next->version); !r) return r;
        }
    } else if (remaining.empty()) {
        fs::remove(folder.parent_path(), ec);          // <root>/components/<id>, now empty
    }
    return Result<void>::Succeed();
}

Result<std::vector<InstalledRecord>> ArchiveInstaller::CollectGarbage() {
    using R = Result<std::vector<InstalledRecord>>;
    const auto all = registry_.All();
    if (!all) return R::Failure(all.error.code, all.error.message, all.error.detail);
    const std::vector<InstalledRecord>& rows = all.Value();

    // What stays: every app's current version and, transitively, the exact
    // component versions it was installed with.
    std::set<std::string> keep;
    std::vector<const InstalledRecord*> frontier;
    for (const InstalledRecord& r : rows) {
        if (r.kind == ComponentKind::App && r.current) { keep.insert(r.id + "@" + r.version); frontier.push_back(&r); }
    }
    while (!frontier.empty()) {
        const InstalledRecord* r = frontier.back();
        frontier.pop_back();
        for (const Dependency& d : r->depends) {
            const std::string key = d.id + "@" + d.version;
            if (!keep.insert(key).second) continue;
            for (const InstalledRecord& c : rows) {
                if (c.id == d.id && VersionEqual(c.version, d.version)) frontier.push_back(&c);
            }
        }
    }

    std::vector<InstalledRecord> removed;
    for (const InstalledRecord& r : rows) {
        if (keep.count(r.id + "@" + r.version)) continue;
        if (const auto res = Remove(r.id, r.version); !res) return R::Failure(res.error.code, res.error.message, res.error.detail);
        removed.push_back(r);
    }
    return R::Succeed(std::move(removed));
}

} // namespace UltraStore
