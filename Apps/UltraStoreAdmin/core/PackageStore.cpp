// Apps/UltraStoreAdmin/core/PackageStore.cpp
// Version: 0.3.0 - ImportMediaVariants
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "PackageStore.h"

#include "UltraCanvasImage.h"   // UCImageRaster: the variants are written through the image library

#include "Verifier.h"

#include "UltraCanvasDataFolder.h"
#include "UltraCanvasPathUtf8.h"
#ifdef ULTRACANVAS_HAS_VIRTUALFS
#include "UltraCanvasVirtualFSBridge.h"
#endif

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <system_error>

namespace fs = std::filesystem;

namespace UltraStoreAdmin {

namespace {

std::string Lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool EndsWith(const std::string& s, const std::string& suffix) {
    return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string BaseName(const std::string& entry) {
    const size_t slash = entry.find_last_of('/');
    return slash == std::string::npos ? entry : entry.substr(slash + 1);
}

} // namespace

std::string FormatOf(const std::string& fileName) {
    const std::string name = Lower(BaseName(fileName));
    if (EndsWith(name, ".tar.zst")) return "tar.zst";
    if (EndsWith(name, ".tar.gz") || EndsWith(name, ".tgz")) return "tar.gz";
    if (EndsWith(name, ".tar.xz")) return "tar.xz";
    if (EndsWith(name, ".zip")) return "zip";
    if (EndsWith(name, ".7z")) return "7z";
    if (EndsWith(name, ".appimage")) return "appimage";
    return {};
}

Result<ImportedPackage> ImportPackage(const std::string& sourceUtf8, const std::string& packagesDirUtf8) {
    using R = Result<ImportedPackage>;
    std::error_code ec;
    const fs::path source = UltraCanvas::PathFromUtf8(sourceUtf8);
    if (!fs::is_regular_file(source, ec)) return R::Failure(ErrorCode::NotFound, "No file at " + sourceUtf8);
    ImportedPackage out;
    out.format = FormatOf(sourceUtf8);
    if (out.format.empty()) return R::Failure(ErrorCode::InvalidArgument, "The store installs zip, 7z, tar.zst, tar.gz, tar.xz and AppImage files, not " + BaseName(sourceUtf8));
    out.sha256 = UltraStore::Sha256HexOfFile(sourceUtf8);
    if (out.sha256.empty()) return R::Failure(ErrorCode::IoError, "Could not hash " + sourceUtf8);
    out.size = static_cast<uint64_t>(fs::file_size(source, ec));
    if (ec) return R::Failure(ErrorCode::IoError, "Could not read the size of " + sourceUtf8, ec.message());
    const fs::path dir = UltraCanvas::PathFromUtf8(packagesDirUtf8);
    fs::create_directories(dir, ec);
    const fs::path target = dir / UltraCanvas::PathFromUtf8(out.sha256 + "." + out.format);
    out.file = UltraCanvas::PathToUtf8(target);
    if (fs::is_regular_file(target, ec) && fs::file_size(target, ec) == out.size) return R::Succeed(std::move(out));
    const fs::path temp = UltraCanvas::PathFromUtf8(out.file + ".tmp");
    fs::copy_file(source, temp, fs::copy_options::overwrite_existing, ec);
    if (ec) return R::Failure(ErrorCode::IoError, "Could not copy " + sourceUtf8 + " into the package store", ec.message());
    fs::remove(target, ec);
    ec.clear();
    fs::rename(temp, target, ec);
    if (ec) return R::Failure(ErrorCode::IoError, "Could not place " + out.file, ec.message());
    return R::Succeed(std::move(out));
}

Result<std::vector<std::string>> ListPackageEntries(const std::string& fileUtf8) {
    using R = Result<std::vector<std::string>>;
#ifdef ULTRACANVAS_HAS_VIRTUALFS
    if (FormatOf(fileUtf8) == "appimage") return R::Succeed(std::vector<std::string>{BaseName(fileUtf8)});
    // The bridge lists one level (a virtual path: the archive, or a folder
    // inside it); a name that lists again is a folder, walked in turn.
    std::vector<std::string> entries;
    std::function<bool(const std::string&, const std::string&, int)> walk =
        [&](const std::string& virtualDir, const std::string& prefix, int depth) -> bool {
            std::vector<std::string> names;
            if (!UltraCanvas::UltraCanvasVirtualFSBridge::ListArchiveContents(virtualDir, names)) return false;
            for (const std::string& name : names) {
                if (name.empty() || name == "." || name == "..") continue;
                std::vector<std::string> inside;
                const bool folder = depth < 8 &&
                    UltraCanvas::UltraCanvasVirtualFSBridge::ListArchiveContents(virtualDir + "/" + name, inside) && !inside.empty();
                if (folder) {
                    entries.push_back(prefix + name + "/");
                    walk(virtualDir + "/" + name, prefix + name + "/", depth + 1);
                } else {
                    entries.push_back(prefix + name);
                }
            }
            return true;
        };
    if (!walk(fileUtf8, "", 0)) return R::Failure(ErrorCode::IoError, "The archive does not list: " + fileUtf8);
    return R::Succeed(std::move(entries));
#else
    (void)fileUtf8;
    return R::Failure(ErrorCode::NotAvailable, "This build has no archive provider (VirtualFS / libarchive)");
#endif
}

Result<std::string> ImportMedia(const std::string& sourceUtf8, const std::string& storeDirUtf8, const std::string& componentId) {
    using R = Result<std::string>;
    std::error_code ec;
    const fs::path source = UltraCanvas::PathFromUtf8(sourceUtf8);
    if (!fs::is_regular_file(source, ec)) return R::Failure(ErrorCode::NotFound, "No file at " + sourceUtf8);
    if (componentId.empty()) return R::Failure(ErrorCode::InvalidArgument, "A picture belongs to a component");
    const std::string relative = "media/" + componentId + "/" + UltraCanvas::PathToUtf8(source.filename());
    const fs::path target = UltraCanvas::PathFromUtf8(storeDirUtf8) / UltraCanvas::PathFromUtf8(relative);
    fs::create_directories(target.parent_path(), ec);
    if (!fs::equivalent(source, target, ec)) {
        ec.clear();
        fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
        if (ec) return R::Failure(ErrorCode::IoError, "Could not copy into the media store", ec.message());
    }
    return R::Succeed(relative);
}

const std::vector<int>& MediaWidths(MediaKind kind) {
    static const std::vector<int> icon = {64, 128, 256};
    static const std::vector<int> shot = {560, 1120};
    return kind == MediaKind::Icon ? icon : shot;
}

size_t MediaBudget(MediaKind kind, int width) {
    if (width <= 0) return 0;
    // 12 KB at 256 px and 120 KB at 1120 px, scaled with the area (integer
    // arithmetic, so the reference sizes are exact).
    const uint64_t reference = kind == MediaKind::Icon ? 12u * 1024 : 120u * 1024;
    const uint64_t referenceWidth = kind == MediaKind::Icon ? 256 : 1120;
    const uint64_t w = static_cast<uint64_t>(width);
    // A floor for the small sizes: a container's own bytes (an AVIF's boxes,
    // a WebP's chunks) do not shrink with the picture.
    return static_cast<size_t>(std::max<uint64_t>(2048, reference * w * w / (referenceWidth * referenceWidth)));
}

Result<ImportedMedia> ImportMediaVariants(const std::string& sourceUtf8, const std::string& storeDirUtf8,
                                          const std::string& componentId, MediaKind kind) {
    using R = Result<ImportedMedia>;
    const auto original = ImportMedia(sourceUtf8, storeDirUtf8, componentId);
    if (!original) return R::Failure(original.error.code, original.error.message, original.error.detail);
    ImportedMedia out;
    out.original = original.Value();

    // The image library is started by the window; the command line and the
    // tests come here without one. Starting it twice is harmless.
    static const bool imageLibrary = UltraCanvas::UCImageRaster::InitializeImageSubsysterm("UltraStoreAdmin");
    if (!imageLibrary) {
        out.warnings.push_back(out.original + ": the image library did not start, so no variants were written (the store shows the original)");
        return R::Succeed(std::move(out));
    }
    const fs::path target = UltraCanvas::PathFromUtf8(storeDirUtf8) / UltraCanvas::PathFromUtf8(out.original);
    auto picture = UltraCanvas::UCImageRaster::Load(UltraCanvas::PathToUtf8(target), /*loadOnlyHeader=*/false);
    if (!picture || picture->GetWidth() <= 0 || picture->GetHeight() <= 0) {
        out.warnings.push_back(out.original + ": the picture does not decode here, so no variants were written (the store shows the original)");
        return R::Succeed(std::move(out));
    }
    const int sourceWidth = picture->GetWidth();
    // The widths the original covers; a smaller original gives its own
    // width once, so the store still has a browser format for it.
    std::vector<int> widths;
    for (int w : MediaWidths(kind)) if (w <= sourceWidth) widths.push_back(w);
    if (widths.empty()) widths.push_back(sourceWidth);

    const std::string folder = out.original.substr(0, out.original.rfind('/') + 1);
    std::string stem = out.original.substr(folder.size());
    if (const size_t dot = stem.rfind('.'); dot != std::string::npos && dot > 0) stem.erase(dot);

    struct Format { const char* name; UltraCanvas::UCImageSaveFormat format; };
    static const Format kFormats[] = {{"avif", UltraCanvas::UCImageSaveFormat::AVIF}, {"webp", UltraCanvas::UCImageSaveFormat::WEBP}};
    std::vector<std::string> cannotWrite;
    for (int width : widths) {
        for (const Format& f : kFormats) {
            UltraCanvas::UCImageSave::ImageExportOptions options;
            options.format = f.format;
            options.targetWidth = width;
            options.maintainAspectRatio = true;
            options.preserveMetadata = false;
            options.preserveTransparency = kind == MediaKind::Icon;
            options.webp.lossless = kind == MediaKind::Icon;
            options.webp.quality = 80;
            options.avif.quality = 65;
            const std::string relative = folder + stem + "-" + std::to_string(width) + "." + f.name;
            const fs::path file = UltraCanvas::PathFromUtf8(storeDirUtf8) / UltraCanvas::PathFromUtf8(relative);
            const std::string error = picture->Save(UltraCanvas::PathToUtf8(file), options);
            std::error_code ec;
            if (!error.empty() || !fs::is_regular_file(file, ec)) {
                if (std::find(cannotWrite.begin(), cannotWrite.end(), f.name) == cannotWrite.end()) cannotWrite.push_back(f.name);
                fs::remove(file, ec);
                continue;
            }
            out.variants.push_back({f.name, width, relative});
            const size_t budget = MediaBudget(kind, width);
            const auto size = fs::file_size(file, ec);
            if (!ec && budget && size > budget) {
                out.warnings.push_back(relative + ": " + std::to_string((size + 1023) / 1024) + " KB, over the " +
                                       std::to_string(budget / 1024) + " KB budget at " + std::to_string(width) + " px");
            }
        }
    }
    for (const std::string& f : cannotWrite) {
        out.warnings.push_back(out.original + ": this machine's image library writes no " + f + ", so the web store has no " + f + " of it");
    }
    return R::Succeed(std::move(out));
}

std::string AdminStore::DatabasePath() const { return UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(dir) / "admin.sqlite"); }
std::string AdminStore::PackagesDir() const { return UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(dir) / "packages"); }
std::string AdminStore::MediaDir() const { return UltraCanvas::PathToUtf8(UltraCanvas::PathFromUtf8(dir) / "media"); }

bool AdminStore::Exists() const {
    std::error_code ec;
    return fs::is_regular_file(UltraCanvas::PathFromUtf8(DatabasePath()), ec);
}

Result<void> AdminStore::CreateFolders() const {
    std::error_code ec;
    fs::create_directories(UltraCanvas::PathFromUtf8(PackagesDir()), ec);
    if (ec) return Result<void>::Failure(ErrorCode::IoError, "Cannot create " + PackagesDir(), ec.message());
    fs::create_directories(UltraCanvas::PathFromUtf8(MediaDir()), ec);
    if (ec) return Result<void>::Failure(ErrorCode::IoError, "Cannot create " + MediaDir(), ec.message());
    return Result<void>::Succeed();
}

std::string DefaultAdminStoreDir() {
    return UltraCanvas::PathToUtf8(UltraCanvas::UltraCanvasDataFolderRoot() / "UltraStoreAdmin");
}

std::string SuggestEntry(const std::vector<std::string>& entries, UltraStore::OS os) {
    auto depth = [](const std::string& e) { return std::count(e.begin(), e.end(), '/'); };
    std::vector<std::string> sorted = entries;
    for (std::string& e : sorted) while (!e.empty() && e.front() == '/') e.erase(e.begin());
    std::sort(sorted.begin(), sorted.end(), [&](const std::string& a, const std::string& b) {
        return depth(a) != depth(b) ? depth(a) < depth(b) : a < b;
    });
    auto first = [&](auto pred) -> std::string {
        for (const std::string& e : sorted) if (!e.empty() && e.back() != '/' && pred(e)) return e;
        return {};
    };
    if (os == UltraStore::OS::Windows) {
        return first([](const std::string& e) { return EndsWith(Lower(e), ".exe") && !EndsWith(Lower(e), "uninstall.exe"); });
    }
    if (os == UltraStore::OS::MacOS) {
        // The bundle is a folder: the first entry inside names it.
        for (const std::string& e : sorted) {
            const size_t app = Lower(e).find(".app/");
            if (app != std::string::npos) return e.substr(0, app + 4);
            if (EndsWith(Lower(e), ".app")) return e;
        }
        return {};
    }
    if (std::string s = first([](const std::string& e) { return EndsWith(Lower(e), ".appimage"); }); !s.empty()) return s;
    if (std::string s = first([](const std::string& e) {
            const std::string name = BaseName(e);
            return e.find("bin/") != std::string::npos && name.find('.') == std::string::npos;
        }); !s.empty()) return s;
    // A single file at the top without an extension.
    std::string only;
    for (const std::string& e : sorted) {
        if (e.find('/') != std::string::npos || e.find('.') != std::string::npos) continue;
        if (!only.empty()) return {};
        only = e;
    }
    return only;
}

} // namespace UltraStoreAdmin
