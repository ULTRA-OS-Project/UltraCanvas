// include/UltraCanvasNativeTheme.h
// Bridge to the desktop environment's icon theme and UI font, so widgets can
// present files with the same folder / file-type icons and typography as the
// native file manager.
//
// Icon resolution follows the freedesktop.org Icon Theme Specification and is
// fully implemented on Linux / BSD (and any XDG desktop): it reads the active
// icon theme (GTK / KDE settings), walks the theme + its inherited themes +
// hicolor across the standard icon directories, and returns an absolute path to
// the best-matching PNG / SVG for a requested icon name and pixel size.
//
// On platforms without an XDG icon theme (Windows / macOS in this iteration)
// ResolveIcon() simply returns "" and callers fall back to their own drawing.
// UIFont() always returns something usable: the detected desktop font, or the
// application's bundled default.
//
// Version: 1.0.0
// Last Modified: 2026-07-16
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasRenderContext.h"   // FontStyle
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace UltraCanvas {

    // Process-wide, lazily initialised. Thread-safe; results are cached until
    // InvalidateCache() is called (e.g. after a live desktop theme change).
    class UltraCanvasNativeTheme {
    public:
        static UltraCanvasNativeTheme& Get();

        // Active desktop icon theme name (e.g. "Adwaita", "Yaru", "breeze").
        // Empty when no XDG desktop is present.
        const std::string& IconThemeName();

        // Resolve an absolute path to a themed icon file for the first entry of
        // `names` that resolves, choosing the directory closest to `sizePx`.
        // `names` are freedesktop icon names in priority order (most specific
        // first), e.g. {"application-pdf", "x-office-document", "unknown"}.
        // Returns "" on a miss.
        std::string ResolveIcon(const std::vector<std::string>& names, int sizePx);

        // Convenience wrapper mapping a plain file/folder to freedesktop icon
        // names and resolving them. `extension` is lowercase, without a dot.
        std::string ResolveFileIcon(bool isDirectory, const std::string& extension,
                                    int sizePx);

        // Native desktop UI font (family + point size). Falls back to the
        // application's system font, then to a bundled default, so the returned
        // FontStyle always has a usable family and size.
        FontStyle UIFont();

        // Drop the cached theme name, parsed theme index files and resolved
        // icon paths. Call after the desktop theme changes at runtime.
        void InvalidateCache();

    private:
        UltraCanvasNativeTheme() = default;
        UltraCanvasNativeTheme(const UltraCanvasNativeTheme&) = delete;
        UltraCanvasNativeTheme& operator=(const UltraCanvasNativeTheme&) = delete;

        // ----- freedesktop icon theme lookup (implemented in the .cpp) -----
        struct ThemeDir {
            std::string path;      // sub-directory inside the theme, e.g. "48x48/places"
            int  size = 0;
            int  minSize = 0;
            int  maxSize = 0;
            int  threshold = 2;
            int  scale = 1;
            int  type = 0;         // 0 Threshold, 1 Fixed, 2 Scalable
        };
        struct ThemeIndex {
            bool loaded = false;
            std::vector<ThemeDir> dirs;
            std::vector<std::string> inherits;
        };

        void EnsureInit();
        const std::vector<std::string>& BaseDirs();
        const ThemeIndex& LoadThemeIndex(const std::string& theme);
        std::vector<std::string> ThemeSearchChain(const std::string& start);
        std::string FindIconInTheme(const std::string& theme,
                                    const std::string& name, int sizePx);
        std::string FindIconInPixmaps(const std::string& name);

        std::mutex mutex;
        bool initialised = false;
        std::string iconThemeName;
        std::vector<std::string> baseDirs;
        std::map<std::string, ThemeIndex> themeIndexCache;   // theme -> parsed index
        std::map<std::string, std::string> resolveCache;     // "name@size" -> path
    };

} // namespace UltraCanvas
