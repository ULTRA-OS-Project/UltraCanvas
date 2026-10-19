// core/UltraCanvasHostFileIcons.cpp
// The portable half of the host file-icon service: the cache key every
// backend shares, and the no-op backend for the platforms that have no
// desktop to ask. The lookups themselves are per platform —
// OS/Linux/UltraCanvasLinuxHostFileIcons.cpp (freedesktop icon themes),
// OS/MSWindows/UltraCanvasWindowsHostFileIcons.cpp (the shell's system image
// list) and OS/MacOS/UltraCanvasMacOSHostFileIcons.mm (NSWorkspace).
// Version: 1.2.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#include "UltraCanvasHostFileIcons.h"
#include "UltraCanvasUtils.h"   // PathFromUtf8 / PathToUtf8

#include <cairo/cairo.h>

#include <algorithm>
#include <cctype>
#include <cmath>
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
        // A program, a shortcut or a bundle is drawn as itself - but that
        // icon is read out of the file by UltraCanvasNativeFileIcons, and is
        // never what THIS module answers: every backend resolves the host's
        // icon from the name alone (the shell is asked with
        // SHGFI_USEFILEATTRIBUTES, the desktops by MIME type or UTType), so
        // two ".exe" files get the same answer here whatever either carries.
        // They therefore key by type like everything else. Until October 2026
        // they were keyed per file, which made a ".dll" the one type that
        // cost a slot per file: a maximised window of ninety of them held
        // ninety copies of one picture, three times the whole budget.

        std::error_code ec;
        std::string name = PathToUtf8(PathFromUtf8(path).filename());
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

    std::shared_ptr<UCPixmap> FitHostIconToSize(
            const std::shared_ptr<UCPixmap>& pixmap, int desiredSize) {
        if (!pixmap || !pixmap->IsValid() || desiredSize <= 0) return pixmap;
        const int w = pixmap->GetRawWidth();
        const int h = pixmap->GetRawHeight();
        const int longest = std::max(w, h);
        if (w <= 0 || h <= 0 || longest <= desiredSize) return pixmap;
        const double factor = static_cast<double>(desiredSize) / longest;
        const int outW = std::max(1, static_cast<int>(std::lround(w * factor)));
        const int outH = std::max(1, static_cast<int>(std::lround(h * factor)));
        cairo_surface_t* source = pixmap->GetSurface();
        if (!source) return pixmap;
        auto out = std::make_shared<UCPixmap>(outW, outH);
        cairo_surface_t* outSurface = out->GetSurface();
        if (!outSurface || !out->IsValid()) return pixmap;
        cairo_surface_flush(source);
        cairo_t* cr = cairo_create(outSurface);
        cairo_scale(cr, static_cast<double>(outW) / w,
                    static_cast<double>(outH) / h);
        cairo_set_source_surface(cr, source, 0, 0);
        // GOOD, not BEST: a box-filtered downsample is what a 256-to-128
        // reduction wants, and it is what cairo's GOOD does for a shrink.
        cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_GOOD);
        cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
        cairo_paint(cr);
        cairo_destroy(cr);
        cairo_surface_flush(outSurface);
        out->MarkDirty();
        return out;
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
