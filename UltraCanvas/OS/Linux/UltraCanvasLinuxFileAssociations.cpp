// OS/Linux/UltraCanvasLinuxFileAssociations.cpp
// freedesktop.org backend of UltraCanvasFileAssociations: file types from
// shared-mime-info glob files (globs2), candidate applications from the
// mimeapps.list chain (defaults / added / removed associations) plus each
// applications directory's mimeinfo.cache, application names / icons / Exec
// lines from .desktop entries, and icons resolved through hicolor + pixmaps.
// Plain text parsing throughout — no GIO/GTK dependency. Also serves BSD,
// which builds this platform directory too.
// Launching expands the Exec field codes (%f/%F/%u/%U; %i/%c/%k dropped) and
// hands the argv to LaunchDetachedProcess. Terminal=true entries are skipped
// (Explorer and Finder do not offer them either). Default open falls back to
// xdg-open when the database names no handler.
// All entry points are serialized by the core's backend mutex (see
// UltraCanvasFileAssociationsBackend.h) — no locking here.
// Version: 1.0.0
// Last Modified: 2026-08-16
// Author: UltraCanvas Framework

#include "UltraCanvasFileAssociationsBackend.h"
#include "UltraCanvasDesktopEntry.h"   // the shared .desktop reader + icon lookup
#include "UltraCanvasUtils.h"   // ToLowerCase, Split, Trim, LaunchDetachedProcess

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;

namespace UltraCanvas {
namespace FileAssociationsBackend {

namespace {

    // ===== XDG BASE DIRECTORIES =====

    std::string HomeDir() {
        const char* home = std::getenv("HOME");
        return home ? home : "";
    }

    // Preference order: user first ($XDG_DATA_HOME, default ~/.local/share),
    // then the system directories from $XDG_DATA_DIRS.
    std::vector<std::string> DataDirs() {
        std::vector<std::string> dirs;
        if (const char* dataHome = std::getenv("XDG_DATA_HOME"); dataHome && *dataHome)
            dirs.push_back(dataHome);
        else if (!HomeDir().empty())
            dirs.push_back(HomeDir() + "/.local/share");
        const char* dataDirs = std::getenv("XDG_DATA_DIRS");
        for (const std::string& d :
             Split(dataDirs && *dataDirs ? dataDirs : "/usr/local/share:/usr/share", ':'))
            if (!d.empty()) dirs.push_back(d);
        return dirs;
    }

    std::vector<std::string> ConfigDirs() {
        std::vector<std::string> dirs;
        if (const char* configHome = std::getenv("XDG_CONFIG_HOME"); configHome && *configHome)
            dirs.push_back(configHome);
        else if (!HomeDir().empty())
            dirs.push_back(HomeDir() + "/.config");
        const char* configDirs = std::getenv("XDG_CONFIG_DIRS");
        for (const std::string& d :
             Split(configDirs && *configDirs ? configDirs : "/etc/xdg", ':'))
            if (!d.empty()) dirs.push_back(d);
        return dirs;
    }

    // "GNOME:GNOME-Classic" → {"gnome", "gnome-classic"} for the
    // desktop-prefixed mimeapps.list variants.
    std::vector<std::string> CurrentDesktops() {
        std::vector<std::string> desktops;
        if (const char* current = std::getenv("XDG_CURRENT_DESKTOP"))
            for (const std::string& d : Split(current, ':'))
                if (!d.empty()) desktops.push_back(ToLowerCase(d));
        return desktops;
    }

    // ===== THE GLOBAL INDEX =====

    struct DesktopEntry {
        std::string id;         // desktop-file id, e.g. "org.gnome.gedit.desktop"
        std::string name;
        std::string iconName;   // raw Icon= value (name or absolute path)
        std::string iconPath;   // resolved lazily, cached here
        bool iconResolved = false;
        std::string exec;
        bool terminal = false;
        bool noDisplay = false;
        bool valid = false;     // Type=Application with a non-empty Exec
        std::vector<std::string> mimeTypes;
    };

    struct GlobalIndex {
        // "*.ext" globs: lowercase suffix (no leading dot, may contain dots,
        // e.g. "tar.gz") → best weight seen → MIME type.
        std::unordered_map<std::string, std::pair<int, std::string>> suffixToMime;
        // Literal-name globs ("makefile") → MIME type.
        std::unordered_map<std::string, std::string> literalToMime;
        // MIME subclassing ("text/x-python" → "text/plain").
        std::unordered_map<std::string, std::vector<std::string>> mimeParents;

        std::unordered_map<std::string, DesktopEntry> apps;   // by desktop id

        // Per MIME type, from the mimeapps.list chain + mimeinfo.cache files,
        // already in preference order and already deduplicated.
        std::unordered_map<std::string, std::vector<std::string>> mimeDefaults;
        std::unordered_map<std::string, std::vector<std::string>> mimeCandidates;
        std::unordered_map<std::string, std::unordered_set<std::string>> mimeRemoved;

        // Freshness: every file that fed the index, with its mtime; a change
        // (or a new/removed file) triggers a rebuild.
        std::vector<std::pair<std::string, time_t>> sources;
        bool built = false;
    };

    GlobalIndex g_index;

    time_t FileMTime(const std::string& path) {
        struct stat st{};
        return ::stat(path.c_str(), &st) == 0 ? st.st_mtime : 0;
    }

    void NoteSource(GlobalIndex& index, const std::string& path) {
        index.sources.emplace_back(path, FileMTime(path));
    }

    // ===== PARSERS =====

    // shared-mime-info globs2: "weight:mime/type:glob[:flags]".
    void ParseGlobs2(GlobalIndex& index, const std::string& path) {
        NoteSource(index, path);
        std::ifstream in(path);
        if (!in.is_open()) return;
        std::string line;
        while (std::getline(in, line)) {
            if (line.empty() || line[0] == '#') continue;
            const std::vector<std::string> parts = Split(line, ':');
            if (parts.size() < 3) continue;
            const int weight = std::atoi(parts[0].c_str());
            const std::string& mime = parts[1];
            const std::string glob = ToLowerCase(parts[2]);
            if (glob.rfind("*.", 0) == 0 &&
                glob.find_first_of("*?[", 2) == std::string::npos) {
                const std::string suffix = glob.substr(2);
                auto it = index.suffixToMime.find(suffix);
                if (it == index.suffixToMime.end() || weight > it->second.first)
                    index.suffixToMime[suffix] = {weight, mime};
            } else if (glob.find_first_of("*?[") == std::string::npos) {
                index.literalToMime.emplace(glob, mime);
            }
            // Anything with mid-pattern wildcards is rarer than the menu is
            // worth — those fall through to application/octet-stream.
        }
    }

    // shared-mime-info subclasses: "child/type parent/type" per line.
    void ParseSubclasses(GlobalIndex& index, const std::string& path) {
        NoteSource(index, path);
        std::ifstream in(path);
        if (!in.is_open()) return;
        std::string child, parent;
        while (in >> child >> parent)
            index.mimeParents[child].push_back(parent);
    }

    // One "mime/type=app1.desktop;app2.desktop;" association line.
    void AddAssociations(std::unordered_map<std::string, std::vector<std::string>>& into,
                         const std::string& mime, const std::string& ids) {
        std::vector<std::string>& list = into[mime];
        for (const std::string& raw : Split(ids, ';')) {
            const std::string id = Trim(raw);
            if (id.empty()) continue;
            if (std::find(list.begin(), list.end(), id) == list.end())
                list.push_back(id);
        }
    }

    // mimeapps.list ([Default Applications] / [Added Associations] /
    // [Removed Associations]) and mimeinfo.cache ([MIME Cache]) share the
    // same ini shape; which sections matter differs per file kind.
    void ParseMimeApps(GlobalIndex& index, const std::string& path, bool isCache) {
        NoteSource(index, path);
        std::ifstream in(path);
        if (!in.is_open()) return;
        enum class Section { Other, Defaults, Added, Removed, Cache } section = Section::Other;
        std::string line;
        while (std::getline(in, line)) {
            const std::string s = Trim(line);
            if (s.empty() || s[0] == '#') continue;
            if (s.front() == '[') {
                if (s == "[Default Applications]")     section = Section::Defaults;
                else if (s == "[Added Associations]")  section = Section::Added;
                else if (s == "[Removed Associations]")section = Section::Removed;
                else if (s == "[MIME Cache]")          section = Section::Cache;
                else                                   section = Section::Other;
                continue;
            }
            const size_t eq = s.find('=');
            if (eq == std::string::npos) continue;
            const std::string mime = Trim(s.substr(0, eq));
            const std::string ids = s.substr(eq + 1);
            if (isCache) {
                if (section == Section::Cache)
                    AddAssociations(index.mimeCandidates, mime, ids);
                continue;
            }
            switch (section) {
                case Section::Defaults: AddAssociations(index.mimeDefaults, mime, ids); break;
                case Section::Added:    AddAssociations(index.mimeCandidates, mime, ids); break;
                case Section::Removed:
                    for (const std::string& raw : Split(ids, ';')) {
                        const std::string id = Trim(raw);
                        if (!id.empty()) index.mimeRemoved[mime].insert(id);
                    }
                    break;
                default: break;
            }
        }
    }

    // One .desktop file, read by the shared reader (UltraCanvasDesktopEntry)
    // and kept in the index under its desktop-file id. What is added here is
    // what only an application index cares about: the id, whether the entry
    // is launchable at all, and the freedesktop rule that a Hidden entry
    // counts as deleted.
    void ParseDesktopFile(GlobalIndex& index, const std::string& path,
                          const std::string& id) {
        if (index.apps.count(id)) return;   // an earlier (user) dir already won
        UCDesktopEntry parsed;
        if (!ReadDesktopEntry(path, parsed)) return;
        // Hidden means "treat as deleted" — record nothing usable, so a
        // system entry the user masked in ~/.local/share/applications really
        // disappears instead of being served by the lower-priority copy.
        if (parsed.hidden) {
            index.apps.emplace(id, DesktopEntry{});   // block lower-priority dirs
            return;
        }
        DesktopEntry entry;
        entry.id = id;
        entry.name = parsed.name.empty() ? id : parsed.name;
        entry.iconName = parsed.iconName;
        entry.exec = parsed.exec;
        entry.terminal = parsed.terminal;
        entry.noDisplay = parsed.noDisplay;
        entry.mimeTypes = std::move(parsed.mimeTypes);
        entry.valid = parsed.kind == UCDesktopEntry::Kind::Application &&
                      !entry.exec.empty();
        index.apps.emplace(id, std::move(entry));
    }

    void ScanApplicationsDir(GlobalIndex& index, const std::string& appsDir) {
        std::error_code ec;
        if (!fs::is_directory(appsDir, ec) || ec) return;
        // Desktop-file ids replace subdirectory separators with '-'
        // ("kde4/okular.desktop" → "kde4-okular.desktop").
        for (fs::recursive_directory_iterator it(appsDir, ec), end;
             it != end && !ec; it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const fs::path& p = it->path();
            if (p.extension() != ".desktop") continue;
            std::string id = fs::relative(p, appsDir, ec).string();
            if (ec) continue;
            std::replace(id.begin(), id.end(), '/', '-');
            ParseDesktopFile(index, p.string(), id);
        }
        // The scan feeds the freshness list through the directory itself: a
        // package install changes the directory mtime.
        NoteSource(index, appsDir);
    }

    // ===== ICONS =====

    // Menu-sized: what an "Open with" row draws. The lookup itself — the
    // configured theme, what it inherits, hicolor, then the flat pixmaps
    // directories — belongs to the shared reader, which the file display
    // uses for the very same icons.
    constexpr int kMenuIconSize = 48;

    const std::string& IconPathFor(DesktopEntry& entry) {
        if (!entry.iconResolved) {
            entry.iconPath = FindDesktopIconFile(entry.iconName, kMenuIconSize);
            entry.iconResolved = true;
        }
        return entry.iconPath;
    }

    // ===== BUILDING / REFRESHING THE INDEX =====

    void BuildIndex() {
        GlobalIndex index;
        const std::vector<std::string> dataDirs = DataDirs();

        for (const std::string& d : dataDirs) {
            ParseGlobs2(index, d + "/mime/globs2");
            ParseSubclasses(index, d + "/mime/subclasses");
        }

        // mimeapps.list chain, most specific first (the AddAssociations
        // dedup keeps the first occurrence, i.e. the user's choice):
        // config dirs (desktop-prefixed variant before the plain file), then
        // each applications directory's mimeapps.list.
        const std::vector<std::string> desktops = CurrentDesktops();
        for (const std::string& c : ConfigDirs()) {
            for (const std::string& desktop : desktops)
                ParseMimeApps(index, c + "/" + desktop + "-mimeapps.list", false);
            ParseMimeApps(index, c + "/mimeapps.list", false);
        }
        for (const std::string& d : dataDirs) {
            for (const std::string& desktop : desktops)
                ParseMimeApps(index, d + "/applications/" + desktop + "-mimeapps.list", false);
            ParseMimeApps(index, d + "/applications/mimeapps.list", false);
        }
        for (const std::string& d : dataDirs) {
            ScanApplicationsDir(index, d + "/applications");
            ParseMimeApps(index, d + "/applications/mimeinfo.cache", true);
        }

        // The MimeType= lines of the desktop entries themselves are the
        // lowest-priority candidate source: mimeinfo.cache is only a
        // pre-computed copy of them, and it is missing on systems where
        // update-desktop-database never ran (containers, fresh installs) —
        // without this, such systems would list no applications at all.
        // Sorted by name so the fallback order is stable.
        std::vector<const DesktopEntry*> entries;
        for (const auto& [id, entry] : index.apps)
            if (entry.valid && !entry.mimeTypes.empty())
                entries.push_back(&entry);
        std::sort(entries.begin(), entries.end(),
                  [](const DesktopEntry* a, const DesktopEntry* b) {
                      return a->name == b->name ? a->id < b->id
                                                : a->name < b->name;
                  });
        for (const DesktopEntry* entry : entries)
            for (const std::string& mime : entry->mimeTypes)
                AddAssociations(index.mimeCandidates, mime, entry->id);

        index.built = true;
        g_index = std::move(index);
    }

    bool IndexIsStale() {
        if (!g_index.built) return true;
        for (const auto& [path, mtime] : g_index.sources)
            if (FileMTime(path) != mtime) return true;
        return false;
    }

    // ===== RESOLUTION =====

    std::string MimeTypeForName(const std::string& fileName) {
        const std::string lower = ToLowerCase(fileName);
        auto literal = g_index.literalToMime.find(lower);
        if (literal != g_index.literalToMime.end()) return literal->second;
        // Longest matching "*.suffix" glob wins ("tar.gz" over "gz"), weight
        // breaking ties between equally long ones (already folded in above).
        for (size_t dot = lower.find('.'); dot != std::string::npos;
             dot = lower.find('.', dot + 1)) {
            auto it = g_index.suffixToMime.find(lower.substr(dot + 1));
            if (it != g_index.suffixToMime.end()) return it->second.second;
        }
        return "application/octet-stream";
    }

    // Candidate desktop ids for one MIME type: defaults first, then added /
    // cached associations, minus the removed ones — each id only once.
    void CollectIdsForMime(const std::string& mime,
                           std::vector<std::string>& ids,
                           std::unordered_set<std::string>& seen,
                           size_t* defaultCount) {
        const auto* removed = [&]() -> const std::unordered_set<std::string>* {
            auto it = g_index.mimeRemoved.find(mime);
            return it == g_index.mimeRemoved.end() ? nullptr : &it->second;
        }();
        auto add = [&](const std::vector<std::string>& list, bool isDefault) {
            for (const std::string& id : list) {
                if (removed && removed->count(id)) continue;
                if (!seen.insert(id).second) continue;
                ids.push_back(id);
                if (isDefault && defaultCount) ++*defaultCount;
            }
        };
        if (auto it = g_index.mimeDefaults.find(mime); it != g_index.mimeDefaults.end())
            add(it->second, true);
        if (auto it = g_index.mimeCandidates.find(mime); it != g_index.mimeCandidates.end())
            add(it->second, false);
    }

    std::vector<FileAssociationApp> ResolveByMime(const std::string& mime) {
        std::vector<std::string> ids;
        std::unordered_set<std::string> seen;
        size_t defaultCount = 0;
        CollectIdsForMime(mime, ids, seen, &defaultCount);
        // Handlers of the parent types come after the specific ones — this is
        // what makes every text editor an "Open with" option for source code.
        std::vector<std::string> pending{mime};
        std::unordered_set<std::string> visited{mime};
        while (!pending.empty()) {
            const std::string current = pending.back();
            pending.pop_back();
            auto parents = g_index.mimeParents.find(current);
            if (parents == g_index.mimeParents.end()) continue;
            for (const std::string& parent : parents->second) {
                if (!visited.insert(parent).second) continue;
                CollectIdsForMime(parent, ids, seen, nullptr);
                pending.push_back(parent);
            }
        }

        std::vector<FileAssociationApp> apps;
        bool first = true;
        for (size_t i = 0; i < ids.size(); ++i) {
            auto it = g_index.apps.find(ids[i]);
            if (it == g_index.apps.end() || !it->second.valid) continue;
            DesktopEntry& entry = it->second;
            if (entry.terminal) continue;
            // NoDisplay entries stay out of the menu unless the user (or the
            // desktop) explicitly made one the default handler.
            if (entry.noDisplay && i >= defaultCount) continue;
            FileAssociationApp app;
            app.id = entry.id;
            app.name = entry.name;
            app.iconPath = IconPathFor(entry);
            app.isDefault = first;   // the first surviving candidate is what
            first = false;           // a default open would launch
            apps.push_back(std::move(app));
        }
        return apps;
    }

    // ===== EXEC EXPANSION =====
    // Tokenizing an Exec= value and expanding its field codes is the shared
    // reader's (UltraCanvasDesktopEntry): the file display expands the same
    // lines when it launches a shortcut, and two expanders would eventually
    // disagree about the same launcher.
    std::vector<std::string> BuildArgv(const std::string& exec,
                                       const std::vector<std::string>& paths) {
        UCDesktopEntry entry;
        entry.exec = exec;
        return DesktopEntryCommand(entry, paths);
    }

    bool LaunchDesktopEntry(const DesktopEntry& entry,
                            const std::vector<std::string>& paths,
                            std::string& outError) {
        const std::vector<std::string> argv = BuildArgv(entry.exec, paths);
        if (argv.empty()) {
            outError = "\"" + entry.name + "\" has no launchable command.";
            return false;
        }
        const std::string workingDir = paths.empty()
                ? std::string() : fs::path(paths[0]).parent_path().string();
        return LaunchDetachedProcess(argv, workingDir, outError);
    }

    void EnsureIndex() {
        if (IndexIsStale()) BuildIndex();
    }

} // namespace

// ===== BACKEND ENTRY POINTS =====

bool RefreshGlobalIndex() {
    if (!IndexIsStale()) return false;
    BuildIndex();
    return true;
}

std::vector<FileAssociationApp> ResolveFile(const std::string& fileName) {
    EnsureIndex();
    const size_t slash = fileName.find_last_of('/');
    const std::string name = slash == std::string::npos
                             ? fileName : fileName.substr(slash + 1);
    return ResolveByMime(MimeTypeForName(name));
}

bool LaunchDefault(const std::vector<std::string>& paths, std::string& outError) {
    EnsureIndex();
    // The selection may span types with different defaults: group the files
    // per default handler and launch each handler once with its files.
    std::vector<std::pair<std::string, std::vector<std::string>>> groups;
    for (const std::string& path : paths) {
        const size_t slash = path.find_last_of('/');
        const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        std::string handlerId;
        const std::vector<FileAssociationApp> apps = ResolveByMime(MimeTypeForName(name));
        if (!apps.empty()) handlerId = apps[0].id;
        auto group = std::find_if(groups.begin(), groups.end(),
                                  [&](const auto& g) { return g.first == handlerId; });
        if (group == groups.end()) groups.push_back({handlerId, {path}});
        else group->second.push_back(path);
    }
    bool allOk = true;
    for (const auto& [handlerId, files] : groups) {
        std::string error;
        bool ok = false;
        if (!handlerId.empty()) {
            auto it = g_index.apps.find(handlerId);
            if (it != g_index.apps.end() && it->second.valid)
                ok = LaunchDesktopEntry(it->second, files, error);
        }
        if (!ok) {
            // No registered handler (or it failed to start): xdg-open knows
            // desktop-specific fallbacks this backend does not.
            std::vector<std::string> argv{"xdg-open"};
            argv.insert(argv.end(), files.begin(), files.end());
            ok = LaunchDetachedProcess(argv, fs::path(files[0]).parent_path().string(),
                                       error);
        }
        if (!ok) {
            allOk = false;
            if (!outError.empty()) outError += "\n";
            outError += error;
        }
    }
    return allOk;
}

bool LaunchWith(const FileAssociationApp& app,
                const std::vector<std::string>& paths, std::string& outError) {
    EnsureIndex();
    auto it = g_index.apps.find(app.id);
    if (it == g_index.apps.end() || !it->second.valid) {
        outError = "The application \"" + app.name + "\" is no longer installed.";
        return false;
    }
    return LaunchDesktopEntry(it->second, paths, outError);
}

bool LaunchWithPath(const std::string& applicationPath,
                    const std::vector<std::string>& paths, std::string& outError) {
    std::vector<std::string> argv;
    std::string workingDir;
    // An application on this platform is usually a program, but it can also
    // be the .desktop file that describes one — what the user picks in a
    // file dialog pointed at /usr/share/applications, and what the file
    // display activates when a folder holds a launcher. Running the file
    // itself would fail (it is text, not a program): run what it says.
    UCDesktopEntry entry;
    if (IsDesktopEntryPath(applicationPath) &&
        ReadDesktopEntry(applicationPath, entry)) {
        argv = DesktopEntryCommand(entry, paths);
        if (argv.empty()) {
            outError = "\"" + (entry.name.empty() ? applicationPath : entry.name) +
                       "\" has no launchable command.";
            return false;
        }
        workingDir = entry.workingDirectory;
    } else {
        argv.push_back(applicationPath);
        argv.insert(argv.end(), paths.begin(), paths.end());
    }
    if (workingDir.empty() && !paths.empty())
        workingDir = fs::path(paths[0]).parent_path().string();
    return LaunchDetachedProcess(argv, workingDir, outError);
}

FileAssociations::ApplicationFilter GetApplicationFilter() {
    // Linux executables carry no extension — everything has to stay visible.
    return {"Applications", {"*"}};
}

std::string GetApplicationsDirectory() {
    std::error_code ec;
    return fs::is_directory("/usr/bin", ec) ? "/usr/bin" : std::string();
}

} // namespace FileAssociationsBackend
} // namespace UltraCanvas
