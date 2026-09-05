// core/UltraCanvasMacBundle.cpp
// Application bundles and web-location shortcuts, read from the files
// themselves through UltraCanvasPropertyList — no Apple API, so a Mac disk
// reads the same wherever it is mounted. The one thing that cannot be done
// that way is a Finder alias, whose bookmark data only macOS can resolve;
// the stub here reports "not resolvable" and the macOS backend
// (OS/MacOS/UltraCanvasMacAlias.mm) does the real thing.
// Version: 1.0.0
// Last Modified: 2026-09-05
// Author: UltraCanvas Framework
#include "UltraCanvasMacBundle.h"
#include "UltraCanvasPropertyList.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace UltraCanvas {

    namespace {

        std::string LowerExtensionOf(const std::string& path) {
            const size_t dot = path.find_last_of('.');
            const size_t slash = path.find_last_of("/\\");
            if (dot == std::string::npos ||
                (slash != std::string::npos && dot < slash))
                return {};
            std::string ext = path.substr(dot + 1);
            // A trailing separator ("/Applications/Mail.app/") must not hide
            // the extension.
            while (!ext.empty() && (ext.back() == '/' || ext.back() == '\\'))
                ext.pop_back();
            std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return ext;
        }

        bool IsFile(const fs::path& p) {
            std::error_code ec;
            return fs::is_regular_file(p, ec) && !ec;
        }

        // The icon a bundle is drawn with. Info.plist names it, sometimes
        // without its extension; a modern bundle may instead name an entry in
        // a compiled asset catalog, which is not a file at all — then the one
        // .icns in Resources is the best answer available.
        std::string FindBundleIcon(const fs::path& resources,
                                   const std::string& iconFile,
                                   const std::string& iconName) {
            std::error_code ec;
            for (const std::string& candidate : {iconFile, iconName}) {
                if (candidate.empty()) continue;
                const fs::path direct = resources / candidate;
                if (IsFile(direct)) return direct.string();
                const fs::path suffixed = resources / (candidate + ".icns");
                if (IsFile(suffixed)) return suffixed.string();
            }
            if (!fs::is_directory(resources, ec) || ec) return {};
            // No name that resolves: take an .icns from Resources, preferring
            // the conventional one.
            std::string firstFound;
            for (fs::directory_iterator it(resources, ec), end;
                 it != end && !ec; it.increment(ec)) {
                if (!it->is_regular_file(ec)) continue;
                if (LowerExtensionOf(it->path().filename().string()) != "icns")
                    continue;
                const std::string name = it->path().filename().string();
                if (name == "AppIcon.icns") return it->path().string();
                if (firstFound.empty()) firstFound = it->path().string();
            }
            return firstFound;
        }

    } // namespace

    bool IsBundlePath(const std::string& path) {
        const std::string ext = LowerExtensionOf(path);
        return ext == "app" || ext == "framework" || ext == "bundle" ||
               ext == "plugin" || ext == "kext" || ext == "prefpane" ||
               ext == "appex" || ext == "xpc" || ext == "docset";
    }

    bool IsApplicationBundlePath(const std::string& path) {
        return LowerExtensionOf(path) == "app";
    }

    bool ReadApplicationBundle(const std::string& path, UCAppBundle& out) {
        std::error_code ec;
        if (!IsBundlePath(path)) return false;
        if (!fs::is_directory(path, ec) || ec) return false;
        const fs::path root(path);
        const fs::path contents = root / "Contents";
        UCPropertyList info;
        // Applications keep Info.plist under Contents; a framework or a
        // loadable bundle can keep it in Resources instead.
        if (!UCPropertyList::Read((contents / "Info.plist").string(), info) &&
            !UCPropertyList::Read((root / "Resources" / "Info.plist").string(),
                                  info))
            return false;

        UCAppBundle bundle;
        bundle.bundlePath = path;
        bundle.identifier = info.GetString("CFBundleIdentifier");
        bundle.version = info.GetString("CFBundleShortVersionString",
                                        info.GetString("CFBundleVersion"));
        bundle.displayName = info.GetString(
                "CFBundleDisplayName", info.GetString("CFBundleName"));
        if (bundle.displayName.empty()) {
            // The directory name without its extension, which is what the
            // Finder falls back to as well.
            std::string name = root.filename().string();
            const size_t dot = name.find_last_of('.');
            if (dot != std::string::npos && dot > 0) name = name.substr(0, dot);
            bundle.displayName = name;
        }

        const std::string executable = info.GetString("CFBundleExecutable");
        if (!executable.empty()) {
            const fs::path macOs = contents / "MacOS" / executable;
            if (IsFile(macOs)) bundle.executable = macOs.string();
        }
        bundle.iconFile = FindBundleIcon(contents / "Resources",
                                         info.GetString("CFBundleIconFile"),
                                         info.GetString("CFBundleIconName"));
        // "APPL" is the package type of an application; a bundle that says
        // nothing but has an executable where an application keeps one is one
        // too, which is what the Finder goes by.
        bundle.isApplication = info.GetString("CFBundlePackageType") == "APPL" ||
                               (!bundle.executable.empty() &&
                                IsApplicationBundlePath(path));
        out = std::move(bundle);
        return true;
    }

    bool IsWebLocationPath(const std::string& path) {
        return LowerExtensionOf(path) == "webloc";
    }

    bool ReadWebLocation(const std::string& path, std::string& outUrl) {
        if (!IsWebLocationPath(path)) return false;
        UCPropertyList plist;
        if (!UCPropertyList::Read(path, plist)) return false;
        // "URL" is what Safari writes; "url" appears in files written by
        // other producers.
        const std::string url = plist.GetString("URL", plist.GetString("url"));
        if (url.empty()) return false;
        outUrl = url;
        return true;
    }

    bool IsFinderAliasFile(const std::string& path) {
        std::error_code ec;
        if (!fs::is_regular_file(path, ec) || ec) return false;
        std::ifstream in(path, std::ios::binary);
        if (!in) return false;
        char magic[4] = {};
        in.read(magic, sizeof(magic));
        if (in.gcount() != sizeof(magic)) return false;
        // "book" is bookmark data (10.6 and later); "alis" the alias record
        // that preceded it.
        return std::memcmp(magic, "book", 4) == 0 ||
               std::memcmp(magic, "alis", 4) == 0;
    }

#ifndef __APPLE__
    bool ResolveFinderAlias(const std::string&, std::string&) {
        // Bookmark data is opaque: it is a set of hints (volume, inode, path,
        // creation date) that only the system that wrote it can follow, and
        // following it is the whole point — an alias survives its target
        // being moved. Off macOS there is nothing to resolve it with.
        return false;
    }
#endif

} // namespace UltraCanvas
