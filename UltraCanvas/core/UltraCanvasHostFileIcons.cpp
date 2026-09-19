// core/UltraCanvasHostFileIcons.cpp
// The portable half of the host file-icon service: the cache key every
// backend shares, and the no-op backend for the platforms that have no
// desktop to ask. The lookups themselves are per platform —
// OS/Linux/UltraCanvasLinuxHostFileIcons.cpp (freedesktop icon themes),
// OS/MSWindows/UltraCanvasWindowsHostFileIcons.cpp (the shell's system image
// list) and OS/MacOS/UltraCanvasMacOSHostFileIcons.mm (NSWorkspace).
// Version: 1.0.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework
#include "UltraCanvasHostFileIcons.h"
#include "UltraCanvasNativeFileIcons.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>

namespace UltraCanvas {

    namespace {
        std::string ToLower(std::string text) {
            std::transform(text.begin(), text.end(), text.begin(),
                           [](unsigned char c) {
                               return static_cast<char>(std::tolower(c));
                           });
            return text;
        }
    } // namespace

    std::string HostFileIconKey(const std::string& path, bool isDirectory) {
        // Every folder is drawn with the same icon, so they all share one
        // slot. A folder the host gave an icon of its own never gets here:
        // that is the caller's choice and wins before the type is asked.
        if (isDirectory) return "dir";
        // A file that carries its own icon is not a type at all — a program
        // is drawn as itself. Those belong to UltraCanvasNativeFileIcons; the
        // key is per file so that a caller which does route them here (a
        // build where the native module found nothing, say) cannot hand every
        // ".exe" in the folder the icon of the first one.
        if (NativeFileIconAvailable(path)) return "file:" + path;

        std::error_code ec;
        std::string name = std::filesystem::path(path).filename().string();
        if (name.empty()) name = path;
        name = ToLower(name);

        // The suffix chain from the FIRST dot on, because that is the
        // finest distinction the type databases make: "archive.tar.gz" is a
        // compressed tarball and "archive.gz" is a gzip file, and they are
        // drawn differently. A leading dot is the name itself (".bashrc"),
        // not a suffix.
        const size_t dot = name.find('.', name.empty() ? 0 : 1);
        if (dot == std::string::npos || dot + 1 >= name.size())
            return "name:" + name;
        return "ext:" + name.substr(dot + 1);
    }

} // namespace UltraCanvas

// ===== BACKEND FOR PLATFORMS WITHOUT A DESKTOP =====
// WebAssembly runs in a page and Android draws its own file UI: neither has
// an icon theme this could read, so they get inline no-op backends and the
// caller keeps its own icons. Every other platform ships one of the three
// real backends listed at the top of this file.
#if defined(__EMSCRIPTEN__) || defined(__ANDROID__)
namespace UltraCanvas {

    bool HostFileIconsAvailable() { return false; }

    std::shared_ptr<UCPixmap> LoadHostFileIconPixmap(const std::string&, bool,
                                                     int) {
        return nullptr;
    }

    void RefreshHostFileIcons() {}

} // namespace UltraCanvas
#endif
