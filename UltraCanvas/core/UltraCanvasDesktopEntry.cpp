// core/UltraCanvasDesktopEntry.cpp
// Reader for freedesktop desktop entries and for the icon themes their
// Icon= names point into. Plain text parsing plus std::filesystem: no GTK,
// no GIO, no dependency, safe to call from a background thread.
//
// The icon lookup is the part with real work in it. An icon name is not a
// file: it is a name to be found in the configured theme, in whatever that
// theme inherits, and finally in hicolor, at whichever of the installed
// sizes fits best. This resolves that the way the icon-theme specification
// says, with one directory listing per theme (cached) rather than a
// recursive walk per icon.
// Version: 1.0.0
// Last Modified: 2026-09-05
// Author: UltraCanvas Framework
#include "UltraCanvasDesktopEntry.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <unordered_map>

#if !defined(_WIN32)
#include <unistd.h>       // access(X_OK) for the Exec / TryExec lookup
#endif

namespace fs = std::filesystem;

namespace UltraCanvas {

    namespace {

        // A desktop entry is a small text file; a "*.desktop" larger than
        // this is not one and is not read.
        constexpr uintmax_t kMaxDesktopEntryBytes = 1024 * 1024;
        // Themes inherit from themes; the chain is short in practice and
        // this only stops a cycle from becoming a walk.
        constexpr int kMaxThemeDepth = 8;

        std::string Trim(const std::string& s) {
            const size_t first = s.find_first_not_of(" \t\r\n");
            if (first == std::string::npos) return {};
            const size_t last = s.find_last_not_of(" \t\r\n");
            return s.substr(first, last - first + 1);
        }

        std::string GetEnvString(const char* name) {
            const char* value = std::getenv(name);
            return value ? std::string(value) : std::string();
        }

        std::vector<std::string> SplitList(const std::string& value, char sep) {
            std::vector<std::string> parts;
            std::string current;
            for (char c : value) {
                if (c == sep) { parts.push_back(current); current.clear(); }
                else current += c;
            }
            parts.push_back(current);
            return parts;
        }

        // ===== THE USER'S LANGUAGE =====
        // "de_DE.UTF-8@euro" offers three keys, most specific first:
        // Name[de_DE@euro], Name[de_DE], Name[de].
        std::vector<std::string> LanguageKeys() {
            std::string locale = GetEnvString("LC_MESSAGES");
            if (locale.empty()) locale = GetEnvString("LC_ALL");
            if (locale.empty()) locale = GetEnvString("LANG");
            if (locale.empty() || locale == "C" || locale == "POSIX") return {};
            std::string modifier;
            const size_t at = locale.find('@');
            if (at != std::string::npos) {
                modifier = locale.substr(at + 1);
                locale = locale.substr(0, at);
            }
            const size_t dot = locale.find('.');        // drop the encoding
            if (dot != std::string::npos) locale = locale.substr(0, dot);
            if (locale.empty()) return {};
            std::vector<std::string> keys;
            if (!modifier.empty()) keys.push_back(locale + "@" + modifier);
            keys.push_back(locale);
            const size_t underscore = locale.find('_');
            if (underscore != std::string::npos)
                keys.push_back(locale.substr(0, underscore));
            return keys;
        }

        // How good a match a key's language suffix is: 0 = no localization,
        // higher = more specific. -1 = a language this user does not read.
        // The keys are worked out per file rather than once per process, so
        // an application that changes locale is not stuck with the language
        // its first folder listing happened to use.
        int LanguageRank(const std::string& suffix,
                         const std::vector<std::string>& keys) {
            if (suffix.empty()) return 0;
            for (size_t i = 0; i < keys.size(); ++i)
                if (keys[i] == suffix) return static_cast<int>(keys.size() - i);
            return -1;
        }

        // ===== WHERE ICONS LIVE =====
        std::vector<fs::path> IconBaseDirs() {
            std::vector<fs::path> dirs;
            const std::string home = GetEnvString("HOME");
            std::string dataHome = GetEnvString("XDG_DATA_HOME");
            if (dataHome.empty() && !home.empty()) dataHome = home + "/.local/share";
            if (!dataHome.empty()) dirs.push_back(fs::path(dataHome) / "icons");
            if (!home.empty()) dirs.push_back(fs::path(home) / ".icons");
            std::string dataDirs = GetEnvString("XDG_DATA_DIRS");
            if (dataDirs.empty()) dataDirs = "/usr/local/share:/usr/share";
            for (const std::string& dir : SplitList(dataDirs, ':'))
                if (!dir.empty()) dirs.push_back(fs::path(dir) / "icons");
            return dirs;
        }

        std::vector<fs::path> PixmapDirs() {
            std::vector<fs::path> dirs;
            std::string dataDirs = GetEnvString("XDG_DATA_DIRS");
            if (dataDirs.empty()) dataDirs = "/usr/local/share:/usr/share";
            for (const std::string& dir : SplitList(dataDirs, ':'))
                if (!dir.empty()) dirs.push_back(fs::path(dir) / "pixmaps");
            const std::string dataHome = GetEnvString("XDG_DATA_HOME");
            if (!dataHome.empty()) dirs.push_back(fs::path(dataHome) / "pixmaps");
            return dirs;
        }

        // The value of one key in a desktop-file-style config, read from the
        // first group that has it. Used for the desktop's own settings files.
        std::string ReadSettingsKey(const fs::path& file, const std::string& key) {
            std::error_code ec;
            if (!fs::is_regular_file(file, ec) || ec) return {};
            std::ifstream in(file);
            if (!in) return {};
            std::string line;
            while (std::getline(in, line)) {
                const std::string s = Trim(line);
                if (s.empty() || s[0] == '#' || s[0] == '[') continue;
                const size_t eq = s.find('=');
                if (eq == std::string::npos) continue;
                if (Trim(s.substr(0, eq)) == key) return Trim(s.substr(eq + 1));
            }
            return {};
        }

        // The theme the desktop is set to, from the settings files the two
        // big toolkits keep. "hicolor" when nothing says otherwise - which
        // is not a guess: every theme is required to fall back to it.
        std::string DetectIconTheme() {
            const std::string home = GetEnvString("HOME");
            std::string configHome = GetEnvString("XDG_CONFIG_HOME");
            if (configHome.empty() && !home.empty()) configHome = home + "/.config";
            if (!configHome.empty()) {
                for (const char* file : {"gtk-4.0/settings.ini",
                                         "gtk-3.0/settings.ini"}) {
                    const std::string theme = ReadSettingsKey(
                            fs::path(configHome) / file, "gtk-icon-theme-name");
                    if (!theme.empty()) return theme;
                }
                const std::string kde = ReadSettingsKey(
                        fs::path(configHome) / "kdeglobals", "Theme");
                if (!kde.empty()) return kde;
            }
            if (!home.empty()) {
                const std::string theme = ReadSettingsKey(
                        fs::path(home) / ".gtkrc-2.0", "gtk-icon-theme-name");
                if (!theme.empty()) {
                    // The GTK2 file quotes its values.
                    std::string value = theme;
                    if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
                        value = value.substr(1, value.size() - 2);
                    if (!value.empty()) return value;
                }
            }
            return "hicolor";
        }

        // ===== ONE THEME, AS A LIST OF DIRECTORIES TO TRY =====
        struct ThemeDir {
            fs::path path;      // <base>/<theme>/<subdir>
            int size = 0;       // pixels; 0 for a scalable directory
            bool scalable = false;
        };

        struct ThemeInfo {
            std::vector<ThemeDir> dirs;
            std::vector<std::string> inherits;
        };

        // "48x48" -> 48, "22x22@2" -> 44, "scalable" -> scalable. A name
        // that is neither is kept with size 0 so it is still searched, last.
        void ClassifyThemeDir(const std::string& name, ThemeDir& out) {
            if (name == "scalable" || name.rfind("scalable", 0) == 0) {
                out.scalable = true;
                return;
            }
            size_t digits = 0;
            while (digits < name.size() &&
                   std::isdigit(static_cast<unsigned char>(name[digits])))
                ++digits;
            if (digits == 0) return;
            int size = std::atoi(name.substr(0, digits).c_str());
            const size_t at = name.find('@');
            if (at != std::string::npos) {
                const int scale = std::atoi(name.substr(at + 1).c_str());
                if (scale > 1) size *= scale;
            }
            out.size = size;
        }

        struct IconCache {
            std::mutex mutex;
            bool themeResolved = false;
            std::string themeOverride;         // set by SetDesktopIconTheme
            std::string theme;                 // in use
            std::vector<std::string> chain;    // theme + inherits + hicolor
            std::map<std::string, ThemeInfo> themes;   // by theme name
            std::map<std::string, std::string> lookups;  // "name|size" -> file
        };

        IconCache& Icons() {
            static IconCache cache;
            return cache;
        }

        // The subdirectories of one theme across every base dir, plus what
        // it inherits. One directory listing per base dir, kept.
        const ThemeInfo& ThemeInfoLocked(IconCache& cache, const std::string& theme) {
            auto it = cache.themes.find(theme);
            if (it != cache.themes.end()) return it->second;
            ThemeInfo info;
            std::error_code ec;
            for (const fs::path& base : IconBaseDirs()) {
                const fs::path themeDir = base / theme;
                if (!fs::is_directory(themeDir, ec) || ec) continue;
                const std::string inherits =
                        ReadSettingsKey(themeDir / "index.theme", "Inherits");
                for (const std::string& parent : SplitList(inherits, ',')) {
                    const std::string trimmed = Trim(parent);
                    if (!trimmed.empty()) info.inherits.push_back(trimmed);
                }
                for (fs::directory_iterator dir(themeDir, ec), end;
                     dir != end && !ec; dir.increment(ec)) {
                    if (!dir->is_directory(ec)) continue;
                    ThemeDir entry;
                    entry.path = dir->path();
                    ClassifyThemeDir(dir->path().filename().string(), entry);
                    info.dirs.push_back(std::move(entry));
                }
            }
            return cache.themes.emplace(theme, std::move(info)).first->second;
        }

        // The themes to search, in order: the configured one, everything it
        // inherits (breadth first), then hicolor.
        const std::vector<std::string>& ThemeChainLocked(IconCache& cache) {
            if (cache.themeResolved) return cache.chain;
            cache.theme = cache.themeOverride.empty() ? DetectIconTheme()
                                                      : cache.themeOverride;
            std::vector<std::string> queue{cache.theme};
            for (size_t i = 0; i < queue.size() && i < kMaxThemeDepth; ++i) {
                const ThemeInfo& info = ThemeInfoLocked(cache, queue[i]);
                for (const std::string& parent : info.inherits) {
                    if (std::find(queue.begin(), queue.end(), parent) == queue.end())
                        queue.push_back(parent);
                }
            }
            if (std::find(queue.begin(), queue.end(), "hicolor") == queue.end())
                queue.push_back("hicolor");
            cache.chain = std::move(queue);
            cache.themeResolved = true;
            return cache.chain;
        }

        // The categories an icon can be filed under. "apps" first: that is
        // where a launcher's icon is, and it is what this module is asked
        // for most.
        const char* const kIconCategories[] = {
                "apps", "devices", "places", "mimetypes", "categories",
                "status", "actions", "emblems", "animations", "intl", "legacy"};
        const char* const kIconExtensions[] = {"png", "svg", "xpm"};

        // Best directory first: the exact size, then scalable (an SVG is
        // every size), then the nearest larger, then the largest smaller.
        std::vector<const ThemeDir*> DirsBySize(const ThemeInfo& info, int wanted) {
            std::vector<const ThemeDir*> dirs;
            dirs.reserve(info.dirs.size());
            for (const ThemeDir& dir : info.dirs) dirs.push_back(&dir);
            std::stable_sort(dirs.begin(), dirs.end(),
                             [wanted](const ThemeDir* a, const ThemeDir* b) {
                                 auto rank = [wanted](const ThemeDir* d) {
                                     if (d->size == wanted) return 0;
                                     if (d->scalable) return 1;
                                     if (d->size > wanted) return 2;
                                     return 3;
                                 };
                                 const int ra = rank(a), rb = rank(b);
                                 if (ra != rb) return ra < rb;
                                 if (ra == 2) return a->size < b->size;  // smallest larger
                                 if (ra == 3) return a->size > b->size;  // largest smaller
                                 return false;
                             });
            return dirs;
        }

        bool IsFile(const fs::path& p) {
            std::error_code ec;
            return fs::is_regular_file(p, ec) && !ec;
        }

        std::string LookupInThemesLocked(IconCache& cache, const std::string& name,
                                         int desiredSize) {
            for (const std::string& theme : ThemeChainLocked(cache)) {
                const ThemeInfo& info = ThemeInfoLocked(cache, theme);
                for (const ThemeDir* dir : DirsBySize(info, desiredSize)) {
                    for (const char* category : kIconCategories) {
                        for (const char* ext : kIconExtensions) {
                            const fs::path candidate =
                                    dir->path / category / (name + "." + ext);
                            if (IsFile(candidate)) return candidate.string();
                        }
                    }
                }
            }
            // The flat directories that predate the theme spec, and the
            // themes' own top level (some packages drop icons there).
            for (const fs::path& dir : PixmapDirs()) {
                for (const char* ext : kIconExtensions) {
                    const fs::path candidate = dir / (name + "." + ext);
                    if (IsFile(candidate)) return candidate.string();
                }
            }
            return {};
        }

        // ===== EXEC =====
        // Split an Exec line the way the specification does: double quotes
        // group, and inside them a backslash escapes.
        std::vector<std::string> TokenizeExec(const std::string& exec) {
            std::vector<std::string> tokens;
            std::string current;
            bool quoted = false, have = false;
            for (size_t i = 0; i < exec.size(); ++i) {
                const char c = exec[i];
                if (c == '"') { quoted = !quoted; have = true; continue; }
                if (quoted && c == '\\' && i + 1 < exec.size()) {
                    current += exec[++i];
                    continue;
                }
                if (!quoted && (c == ' ' || c == '\t')) {
                    if (have) { tokens.push_back(current); current.clear(); have = false; }
                    continue;
                }
                current += c;
                have = true;
            }
            if (have) tokens.push_back(current);
            return tokens;
        }

        // The executable a command names, as a path this system can run:
        // absolute as it stands, else looked up in PATH. "" when not there.
        std::string ResolveProgram(const std::string& command) {
            if (command.empty()) return {};
#if defined(_WIN32)
            return IsFile(command) ? command : std::string();
#else
            if (command.find('/') != std::string::npos)
                return ::access(command.c_str(), X_OK) == 0 ? command : std::string();
            for (const std::string& dir : SplitList(GetEnvString("PATH"), ':')) {
                if (dir.empty()) continue;
                const std::string candidate = dir + "/" + command;
                if (::access(candidate.c_str(), X_OK) == 0) return candidate;
            }
            return {};
#endif
        }

    } // namespace

    bool IsDesktopEntryPath(const std::string& path) {
        const size_t dot = path.find_last_of('.');
        if (dot == std::string::npos) return false;
        std::string ext = path.substr(dot + 1);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return ext == "desktop";
    }

    bool ReadDesktopEntry(const std::string& path, UCDesktopEntry& out) {
        std::error_code ec;
        if (!fs::is_regular_file(path, ec) || ec) return false;
        const uintmax_t size = fs::file_size(path, ec);
        if (ec || size == 0 || size > kMaxDesktopEntryBytes) return false;
        std::ifstream in(path);
        if (!in) return false;

        UCDesktopEntry entry;
        const std::vector<std::string> languages = LanguageKeys();
        // Localized keys win over the plain one, and a better-matching
        // language wins over a worse one, whatever order the file lists them
        // in: each localizable field remembers the rank it was filled at.
        std::map<std::string, int> ranks;
        bool inMainGroup = false, sawMainGroup = false;
        std::string line;
        while (std::getline(in, line)) {
            const std::string s = Trim(line);
            if (s.empty() || s[0] == '#') continue;
            if (s.front() == '[') {
                if (inMainGroup) break;      // the main group ended - done
                inMainGroup = (s == "[Desktop Entry]");
                sawMainGroup = sawMainGroup || inMainGroup;
                continue;
            }
            if (!inMainGroup) continue;
            const size_t eq = s.find('=');
            if (eq == std::string::npos) continue;
            std::string key = Trim(s.substr(0, eq));
            const std::string value = Trim(s.substr(eq + 1));
            // "Name[de_DE]" -> key "Name", language "de_DE".
            std::string language;
            const size_t bracket = key.find('[');
            if (bracket != std::string::npos && !key.empty() && key.back() == ']') {
                language = key.substr(bracket + 1, key.size() - bracket - 2);
                key = key.substr(0, bracket);
            }
            const int rank = LanguageRank(language, languages);
            if (rank < 0) continue;          // a language this user does not read

            auto setLocalized = [&](const std::string& field, std::string& target) {
                auto it = ranks.find(field);
                if (it != ranks.end() && it->second >= rank) return;
                target = value;
                ranks[field] = rank;
            };
            if (key == "Name")             setLocalized("Name", entry.name);
            else if (key == "GenericName") setLocalized("GenericName", entry.genericName);
            else if (key == "Comment")     setLocalized("Comment", entry.comment);
            else if (rank != 0)            continue;   // the rest is not localizable
            else if (key == "Type") {
                if (value == "Application")    entry.kind = UCDesktopEntry::Kind::Application;
                else if (value == "Link")      entry.kind = UCDesktopEntry::Kind::Link;
                else if (value == "Directory") entry.kind = UCDesktopEntry::Kind::Directory;
            }
            else if (key == "Exec")      entry.exec = value;
            else if (key == "TryExec")   entry.tryExec = value;
            else if (key == "Path")      entry.workingDirectory = value;
            else if (key == "Icon")      entry.iconName = value;
            else if (key == "URL")       entry.url = value;
            else if (key == "Terminal")  entry.terminal = (value == "true");
            else if (key == "NoDisplay") entry.noDisplay = (value == "true");
            else if (key == "Hidden")    entry.hidden = (value == "true");
            else if (key == "MimeType") {
                for (const std::string& raw : SplitList(value, ';')) {
                    const std::string mime = Trim(raw);
                    if (!mime.empty()) entry.mimeTypes.push_back(mime);
                }
            }
        }
        if (!sawMainGroup) return false;     // not a desktop entry at all

        // What it actually runs: TryExec is the specification's own "is this
        // installed?" answer, and the first token of Exec is the fallback.
        entry.program = ResolveProgram(entry.tryExec);
        if (entry.program.empty()) {
            const std::vector<std::string> argv = TokenizeExec(entry.exec);
            if (!argv.empty()) entry.program = ResolveProgram(argv.front());
        }
        out = std::move(entry);
        return true;
    }

    void SetDesktopIconTheme(const std::string& themeName) {
        IconCache& cache = Icons();
        std::lock_guard<std::mutex> lk(cache.mutex);
        cache.themeOverride = themeName;
        cache.themeResolved = false;
        cache.chain.clear();
        cache.themes.clear();
        cache.lookups.clear();
    }

    std::string GetDesktopIconTheme() {
        IconCache& cache = Icons();
        std::lock_guard<std::mutex> lk(cache.mutex);
        ThemeChainLocked(cache);
        return cache.theme;
    }

    void RefreshDesktopIconThemes() {
        SetDesktopIconTheme(Icons().themeOverride);
    }

    std::string FindDesktopIconFile(const std::string& iconName, int desiredSize) {
        if (iconName.empty()) return {};
        if (iconName.front() == '/' ||
            (iconName.size() > 2 && iconName[1] == ':'))   // an absolute path
            return IsFile(iconName) ? iconName : std::string();

        // Icon= should carry a name, but files that write "firefox.png" are
        // common enough that the name without its extension is worth a try.
        std::string name = iconName;
        const size_t dot = name.find_last_of('.');
        if (dot != std::string::npos && dot > 0) {
            const std::string ext = name.substr(dot + 1);
            for (const char* known : kIconExtensions) {
                if (ext == known) { name = name.substr(0, dot); break; }
            }
        }

        const int size = std::max(1, desiredSize);
        const std::string key = name + "|" + std::to_string(size);
        IconCache& cache = Icons();
        std::lock_guard<std::mutex> lk(cache.mutex);
        auto cached = cache.lookups.find(key);
        if (cached != cache.lookups.end()) return cached->second;
        std::string file = LookupInThemesLocked(cache, name, size);
        // A bound, so a folder of thousands of launchers cannot grow this
        // without limit; it is a convenience, not a store.
        if (cache.lookups.size() > 4096) cache.lookups.clear();
        cache.lookups.emplace(key, file);
        return file;
    }

    std::vector<std::string> DesktopEntryCommand(
            const UCDesktopEntry& entry, const std::vector<std::string>& files) {
        std::vector<std::string> argv;
        bool inserted = false;
        for (const std::string& token : TokenizeExec(entry.exec)) {
            if (token == "%f" || token == "%F" || token == "%u" || token == "%U") {
                argv.insert(argv.end(), files.begin(), files.end());
                inserted = true;
                continue;
            }
            if (token == "%i" || token == "%c" || token == "%k") continue;
            std::string expanded;
            for (size_t i = 0; i < token.size(); ++i) {
                if (token[i] != '%' || i + 1 >= token.size()) {
                    expanded += token[i];
                    continue;
                }
                const char code = token[++i];
                if (code == '%') expanded += '%';
                // Any other embedded field code expands to nothing.
            }
            argv.push_back(expanded);
        }
        if (argv.empty()) return argv;
        if (!inserted) argv.insert(argv.end(), files.begin(), files.end());
        return argv;
    }

} // namespace UltraCanvas
