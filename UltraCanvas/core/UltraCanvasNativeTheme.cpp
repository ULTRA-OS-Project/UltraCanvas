// core/UltraCanvasNativeTheme.cpp
// freedesktop.org icon-theme resolver + native UI font bridge.
// See UltraCanvasNativeTheme.h for the contract.
// Version: 1.0.0
// Last Modified: 2026-07-16
// Author: UltraCanvas Framework

#include "UltraCanvasNativeTheme.h"
#include "UltraCanvasApplication.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace UltraCanvas {

    namespace {

        std::string EnvOr(const char* name, const std::string& fallback) {
            const char* v = std::getenv(name);
            return (v && *v) ? std::string(v) : fallback;
        }

        std::string HomeDir() {
            const char* h = std::getenv("HOME");
            return (h && *h) ? std::string(h) : std::string();
        }

        std::string Trim(const std::string& s) {
            size_t a = s.find_first_not_of(" \t\r\n");
            if (a == std::string::npos) return "";
            size_t b = s.find_last_not_of(" \t\r\n");
            return s.substr(a, b - a + 1);
        }

        // Read a "key=value" from a flat/sectioned INI file. When `section` is
        // non-empty only lines inside "[section]" are considered. Returns "" if
        // the key is absent. Tolerant of the loose GTK/KDE ini dialects.
        std::string ReadIniKey(const std::string& path, const std::string& section,
                               const std::string& key) {
            std::ifstream in(path);
            if (!in) return "";
            std::string line;
            bool inSection = section.empty();
            while (std::getline(in, line)) {
                std::string t = Trim(line);
                if (t.empty() || t[0] == '#' || t[0] == ';') continue;
                if (t.front() == '[' && t.back() == ']') {
                    if (!section.empty())
                        inSection = (t.substr(1, t.size() - 2) == section);
                    continue;
                }
                if (!inSection) continue;
                size_t eq = t.find('=');
                if (eq == std::string::npos) continue;
                if (Trim(t.substr(0, eq)) == key)
                    return Trim(t.substr(eq + 1));
            }
            return "";
        }

        std::vector<std::string> SplitCsv(const std::string& s) {
            std::vector<std::string> out;
            std::stringstream ss(s);
            std::string item;
            while (std::getline(ss, item, ',')) {
                std::string t = Trim(item);
                if (!t.empty()) out.push_back(t);
            }
            return out;
        }

        int ToIntOr(const std::string& s, int fallback) {
            if (s.empty()) return fallback;
            char* end = nullptr;
            long v = std::strtol(s.c_str(), &end, 10);
            return (end && end != s.c_str()) ? static_cast<int>(v) : fallback;
        }

        std::string ConfigHome() {
            std::string x = EnvOr("XDG_CONFIG_HOME", "");
            if (!x.empty()) return x;
            std::string h = HomeDir();
            return h.empty() ? "" : h + "/.config";
        }

        // The desktop icon theme name, read from GTK then KDE settings.
        std::string DetectIconThemeName() {
            std::string cfg = ConfigHome();
            if (!cfg.empty()) {
                for (const char* v : {"gtk-4.0", "gtk-3.0"}) {
                    std::string val = ReadIniKey(cfg + "/" + v + "/settings.ini",
                                                 "Settings", "gtk-icon-theme-name");
                    if (!val.empty()) return val;
                }
                std::string kde = ReadIniKey(cfg + "/kdeglobals", "Icons", "Theme");
                if (!kde.empty()) return kde;
            }
            std::string home = HomeDir();
            if (!home.empty()) {
                std::string val = ReadIniKey(home + "/.gtkrc-2.0", "",
                                             "gtk-icon-theme-name");
                // gtkrc quotes the value: gtk-icon-theme-name = "Adwaita"
                if (!val.empty()) {
                    if (val.size() >= 2 && val.front() == '"' && val.back() == '"')
                        val = val.substr(1, val.size() - 2);
                    return val;
                }
            }
            return "";
        }

    } // namespace

    UltraCanvasNativeTheme& UltraCanvasNativeTheme::Get() {
        static UltraCanvasNativeTheme instance;
        return instance;
    }

    void UltraCanvasNativeTheme::InvalidateCache() {
        std::lock_guard<std::mutex> lock(mutex);
        initialised = false;
        iconThemeName.clear();
        baseDirs.clear();
        themeIndexCache.clear();
        resolveCache.clear();
    }

    void UltraCanvasNativeTheme::EnsureInit() {
        if (initialised) return;

        // Base icon directories in freedesktop priority order.
        std::vector<std::string> dirs;
        std::string home = HomeDir();
        std::string dataHome = EnvOr("XDG_DATA_HOME", "");
        if (dataHome.empty() && !home.empty()) dataHome = home + "/.local/share";
        if (!dataHome.empty()) dirs.push_back(dataHome + "/icons");
        if (!home.empty()) dirs.push_back(home + "/.icons");

        std::string dataDirs = EnvOr("XDG_DATA_DIRS", "/usr/local/share:/usr/share");
        std::stringstream ss(dataDirs);
        std::string d;
        while (std::getline(ss, d, ':')) {
            d = Trim(d);
            if (!d.empty()) dirs.push_back(d + "/icons");
        }
        dirs.push_back("/usr/share/pixmaps");

        baseDirs.clear();
        for (const std::string& p : dirs) {
            std::error_code ec;
            if (fs::is_directory(p, ec)) baseDirs.push_back(p);
        }

        iconThemeName = DetectIconThemeName();
        initialised = true;
    }

    const std::vector<std::string>& UltraCanvasNativeTheme::BaseDirs() {
        return baseDirs;
    }

    const std::string& UltraCanvasNativeTheme::IconThemeName() {
        std::lock_guard<std::mutex> lock(mutex);
        EnsureInit();
        return iconThemeName;
    }

    const UltraCanvasNativeTheme::ThemeIndex&
    UltraCanvasNativeTheme::LoadThemeIndex(const std::string& theme) {
        auto it = themeIndexCache.find(theme);
        if (it != themeIndexCache.end()) return it->second;

        ThemeIndex idx;
        std::string indexPath;
        for (const std::string& base : baseDirs) {
            std::string p = base + "/" + theme + "/index.theme";
            std::error_code ec;
            if (fs::is_regular_file(p, ec)) { indexPath = p; break; }
        }

        if (!indexPath.empty()) {
            idx.inherits = SplitCsv(ReadIniKey(indexPath, "Icon Theme", "Inherits"));
            std::vector<std::string> dirNames =
                SplitCsv(ReadIniKey(indexPath, "Icon Theme", "Directories"));
            std::vector<std::string> scaled =
                SplitCsv(ReadIniKey(indexPath, "Icon Theme", "ScaledDirectories"));
            dirNames.insert(dirNames.end(), scaled.begin(), scaled.end());

            for (const std::string& name : dirNames) {
                ThemeDir td;
                td.path = name;
                td.size = ToIntOr(ReadIniKey(indexPath, name, "Size"), 0);
                td.scale = ToIntOr(ReadIniKey(indexPath, name, "Scale"), 1);
                td.threshold = ToIntOr(ReadIniKey(indexPath, name, "Threshold"), 2);
                td.minSize = ToIntOr(ReadIniKey(indexPath, name, "MinSize"), td.size);
                td.maxSize = ToIntOr(ReadIniKey(indexPath, name, "MaxSize"), td.size);
                std::string type = ReadIniKey(indexPath, name, "Type");
                if (type == "Fixed") td.type = 1;
                else if (type == "Scalable") td.type = 2;
                else td.type = 0;   // Threshold (spec default)
                idx.dirs.push_back(td);
            }
            idx.loaded = true;
        }

        auto res = themeIndexCache.emplace(theme, std::move(idx));
        return res.first->second;
    }

    // Full theme search order: the theme, its (recursive) inherited themes, and
    // finally hicolor — deduplicated, hicolor guaranteed last.
    std::vector<std::string>
    UltraCanvasNativeTheme::ThemeSearchChain(const std::string& start) {
        std::vector<std::string> chain;
        std::vector<std::string> stack;
        if (!start.empty()) stack.push_back(start);

        auto contains = [&](const std::string& s) {
            return std::find(chain.begin(), chain.end(), s) != chain.end();
        };
        while (!stack.empty()) {
            std::string t = stack.front();
            stack.erase(stack.begin());
            if (t.empty() || t == "hicolor" || contains(t)) continue;
            chain.push_back(t);
            const ThemeIndex& idx = LoadThemeIndex(t);
            for (const std::string& parent : idx.inherits)
                if (!contains(parent)) stack.push_back(parent);
        }
        chain.push_back("hicolor");
        return chain;
    }

    namespace {
        // freedesktop "directory matches size" and distance helpers.
        struct DirMatch {
            static bool Matches(int type, int dsize, int dmin, int dmax,
                                int thr, int size) {
                switch (type) {
                    case 1: return dsize == size;                     // Fixed
                    case 2: return dmin <= size && size <= dmax;      // Scalable
                    default: return (dsize - thr) <= size &&          // Threshold
                                    size <= (dsize + thr);
                }
            }
            static int Distance(int type, int dsize, int dmin, int dmax, int size) {
                switch (type) {
                    case 2:                                           // Scalable
                        if (size < dmin) return dmin - size;
                        if (size > dmax) return size - dmax;
                        return 0;
                    default:                                          // Fixed/Threshold
                        return std::abs(dsize - size);
                }
            }
        };
    }

    std::string UltraCanvasNativeTheme::FindIconInTheme(const std::string& theme,
                                                        const std::string& name,
                                                        int sizePx) {
        const ThemeIndex& idx = LoadThemeIndex(theme);
        if (!idx.loaded) return "";

        // Extensions tried per directory. SVG first so scalable dirs stay crisp
        // on HiDPI; xpm last as a legacy fallback.
        static const char* kExts[] = {"svg", "png", "xpm"};

        std::string best;
        int bestDist = 1 << 30;

        for (const ThemeDir& d : idx.dirs) {
            bool exact = DirMatch::Matches(d.type, d.size, d.minSize, d.maxSize,
                                           d.threshold, sizePx);
            int dist = DirMatch::Distance(d.type, d.size, d.minSize, d.maxSize, sizePx);
            for (const std::string& base : baseDirs) {
                for (const char* ext : kExts) {
                    std::string path =
                        base + "/" + theme + "/" + d.path + "/" + name + "." + ext;
                    std::error_code ec;
                    if (!fs::is_regular_file(path, ec)) continue;
                    if (exact) return path;         // best possible match
                    if (dist < bestDist) { bestDist = dist; best = path; }
                }
            }
        }
        return best;
    }

    std::string UltraCanvasNativeTheme::FindIconInPixmaps(const std::string& name) {
        static const char* kExts[] = {"svg", "png", "xpm"};
        for (const char* ext : kExts) {
            std::string path = "/usr/share/pixmaps/" + std::string(name) + "." + ext;
            std::error_code ec;
            if (fs::is_regular_file(path, ec)) return path;
        }
        return "";
    }

    std::string UltraCanvasNativeTheme::ResolveIcon(
            const std::vector<std::string>& names, int sizePx) {
        if (names.empty()) return "";
        std::lock_guard<std::mutex> lock(mutex);
        EnsureInit();
        if (baseDirs.empty()) return "";
        if (sizePx <= 0) sizePx = 32;

        std::vector<std::string> chain = ThemeSearchChain(iconThemeName);

        for (const std::string& name : names) {
            std::string cacheKey = name + "@" + std::to_string(sizePx);
            auto cached = resolveCache.find(cacheKey);
            if (cached != resolveCache.end()) {
                if (!cached->second.empty()) return cached->second;
                continue;   // known miss for this name; try the next candidate
            }
            std::string found;
            for (const std::string& theme : chain) {
                found = FindIconInTheme(theme, name, sizePx);
                if (!found.empty()) break;
            }
            if (found.empty()) found = FindIconInPixmaps(name);
            resolveCache[cacheKey] = found;   // cache hits and misses alike
            if (!found.empty()) return found;
        }
        return "";
    }

    std::string UltraCanvasNativeTheme::ResolveFileIcon(bool isDirectory,
                                                        const std::string& extension,
                                                        int sizePx) {
        std::vector<std::string> names;
        if (isDirectory) {
            names = {"folder", "inode-directory"};
        } else {
            // A small map of common extensions to their specific freedesktop
            // MIME icon names; anything unmatched still gets a generic below.
            static const std::map<std::string, const char*> kSpecific = {
                {"pdf", "application-pdf"},
                {"svg", "image-svg+xml"},
                {"html", "text-html"}, {"htm", "text-html"},
                {"zip", "application-zip"},
                {"deb", "application-x-deb"},
                {"rpm", "application-x-rpm"},
                {"iso", "application-x-cd-image"},
                {"doc", "application-msword"}, {"docx", "application-msword"},
                {"xls", "application-vnd.ms-excel"},
                {"xlsx", "application-vnd.ms-excel"},
            };
            auto it = kSpecific.find(extension);
            if (it != kSpecific.end()) names.push_back(it->second);
            names.push_back("text-x-generic");
            names.push_back("unknown");
        }
        return ResolveIcon(names, sizePx);
    }

    FontStyle UltraCanvasNativeTheme::UIFont() {
        FontStyle f;
        if (auto* app = UltraCanvasApplicationBase::GetCurrent()) {
            f = app->GetSystemFontStyle();
        }
        if (f.fontFamily.empty()) f.fontFamily = "DejaVu Sans";
        if (f.fontSize <= 0) f.fontSize = 12.0;
        return f;
    }

} // namespace UltraCanvas
