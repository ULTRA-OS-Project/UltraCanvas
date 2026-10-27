// Apps/UltraStoreAdmin/core/PackageStore.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "PackageStore.h"

#include "Verifier.h"

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
