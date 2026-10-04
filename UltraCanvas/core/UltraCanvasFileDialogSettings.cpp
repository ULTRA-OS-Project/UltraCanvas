// core/UltraCanvasFileDialogSettings.cpp
// FileDialog.conf: what the framework's file dialog remembers. See
// UltraCanvasFileDialogSettings.h.
// Version: 1.0.0
// Last Modified: 2026-10-01
// Author: UltraCanvas Framework

#include "UltraCanvasFileDialogSettings.h"
#include "UltraCanvasPathUtf8.h"   // PathFromUtf8

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <system_error>

namespace UltraCanvas {

    namespace {
        constexpr int kMinWidth = 520;
        constexpr int kMinHeight = 380;
        constexpr int kMaxSide = 8000;
        constexpr int kMinColumn = 44;     // the Filer widget's own minimum
        constexpr int kMaxColumn = 2000;
        constexpr int kViewCount = 6;

        const char* ScopeName(LastFolderScope scope) {
            return scope == LastFolderScope::Global ? "global" : "individual";
        }

        bool ParseScope(const std::string& text, LastFolderScope& out) {
            if (text == "global") { out = LastFolderScope::Global; return true; }
            if (text == "individual") { out = LastFolderScope::Individual; return true; }
            return false;
        }

        // An application name inside a key: "app.<name>.folder". A name
        // carrying the separators would make the line unreadable, so those
        // characters are dropped from it.
        std::string KeySafe(const std::string& name) {
            std::string out;
            for (char c : name)
                if (c != '=' && c != '\n' && c != '\r') out += c;
            return out;
        }

        bool ParseInt(const std::string& text, int& out) {
            try {
                size_t used = 0;
                const int v = std::stoi(text, &used);
                if (used != text.size()) return false;
                out = v;
                return true;
            } catch (...) {
                return false;
            }
        }
    } // namespace

    LastFolderScope FileDialogSettings::EffectiveScope(const std::string& appName) const {
        if (lastFolderMode == LastFolderScope::Global) return LastFolderScope::Global;
        auto it = apps.find(KeySafe(appName));
        return it == apps.end() ? LastFolderScope::Individual : it->second.scope;
    }

    std::string FileDialogSettings::LastFolderFor(const std::string& appName) const {
        if (EffectiveScope(appName) == LastFolderScope::Global) return globalFolder;
        // An application switched to its own folder starts from the shared
        // one until it has used a folder of its own.
        auto it = apps.find(KeySafe(appName));
        if (it == apps.end() || it->second.lastFolder.empty()) return globalFolder;
        return it->second.lastFolder;
    }

    void FileDialogSettings::SetLastFolderFor(const std::string& appName,
                                              const std::string& folder) {
        const std::string key = KeySafe(appName);
        // Every application that used the dialog gets its row, so the
        // settings can list it whichever scope it runs under.
        FileDialogAppSettings& app = apps[key];
        if (EffectiveScope(key) == LastFolderScope::Global) {
            globalFolder = folder;
        } else {
            app.lastFolder = folder;
        }
    }

    std::filesystem::path FileDialogSettings::FilePath() {
        std::filesystem::path base;
#if defined(_WIN32) || defined(_WIN64)
        if (const wchar_t* appData = _wgetenv(L"APPDATA"))
            base = std::filesystem::path(appData);   // path-string-ok: wide
#elif defined(__APPLE__)
        if (const char* home = std::getenv("HOME"))
            base = PathFromUtf8(home) / "Library" / "Application Support";
#else
        if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && *xdg) {
            base = PathFromUtf8(xdg);
        } else if (const char* home = std::getenv("HOME")) {
            base = PathFromUtf8(home) / ".config";
        }
#endif
        if (base.empty()) return {};
        return base / "UltraCanvas" / "FileDialog.conf";
    }

    FileDialogSettings FileDialogSettings::Load() {
        FileDialogSettings s;
        const std::filesystem::path path = FilePath();
        if (path.empty()) return s;
        std::ifstream in(path);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const size_t eq = line.find('=');
            if (eq == std::string::npos) continue;
            const std::string key = line.substr(0, eq);
            const std::string value = line.substr(eq + 1);

            // Per application: app.<name>.scope / app.<name>.folder. The name
            // may itself hold dots, so the suffix is matched from the end.
            if (key.rfind("app.", 0) == 0) {
                const size_t dot = key.rfind('.');
                if (dot <= 4) continue;
                const std::string name = key.substr(4, dot - 4);
                const std::string field = key.substr(dot + 1);
                if (field == "scope") {
                    LastFolderScope scope;
                    if (ParseScope(value, scope)) s.apps[name].scope = scope;
                } else if (field == "folder") {
                    s.apps[name].lastFolder = value;
                }
                continue;
            }
            if (key == "lastfolder.mode") {
                ParseScope(value, s.lastFolderMode);
                continue;
            }
            // "folder" is the global folder (also what the first version of
            // this file wrote, before there were per-application folders).
            if (key == "folder") {
                s.globalFolder = value;
                continue;
            }

            int n = 0;
            if (!ParseInt(value, n)) continue;
            if (key == "view" && n >= 0 && n < kViewCount) s.view = n;
            else if (key == "width" && n >= kMinWidth && n <= kMaxSide) s.width = n;
            else if (key == "height" && n >= kMinHeight && n <= kMaxSide) s.height = n;
            else if (n >= kMinColumn && n <= kMaxColumn) {
                if (key == "column.size") s.sizeColumn = n;
                else if (key == "column.type") s.typeColumn = n;
                else if (key == "column.modified") s.modifiedColumn = n;
            }
        }
        return s;
    }

    bool FileDialogSettings::Save() const {
        const std::filesystem::path path = FilePath();
        if (path.empty()) return false;
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        // Written beside the file and renamed over it, so an application
        // reading it at the same moment sees the old file or the new one,
        // never half of one.
        std::filesystem::path temp = path;
        temp += ".tmp";
        {
            std::ofstream out(temp, std::ios::trunc);
            if (!out) return false;
            out << "view=" << view << "\n"
                << "width=" << width << "\n"
                << "height=" << height << "\n"
                << "column.size=" << sizeColumn << "\n"
                << "column.type=" << typeColumn << "\n"
                << "column.modified=" << modifiedColumn << "\n"
                << "lastfolder.mode=" << ScopeName(lastFolderMode) << "\n";
            if (!globalFolder.empty()) out << "folder=" << globalFolder << "\n";
            for (const auto& [name, app] : apps) {
                out << "app." << name << ".scope=" << ScopeName(app.scope) << "\n";
                if (!app.lastFolder.empty())
                    out << "app." << name << ".folder=" << app.lastFolder << "\n";
            }
            if (!out) return false;
        }
        std::filesystem::rename(temp, path, ec);
        if (ec) {
            std::filesystem::remove(temp, ec);
            return false;
        }
        return true;
    }

    bool FileDialogSettings::Update(const std::function<void(FileDialogSettings&)>& change) {
        FileDialogSettings s = Load();
        if (change) change(s);
        return s.Save();
    }

    const std::vector<std::string>& KnownFileDialogApplications() {
        // The applications that keep native dialogs off (main.cpp: no
        // SetUseNativeDialogs(true)), by the name they pass to
        // UltraCanvasApplication::Initialize.
        static const std::vector<std::string> kApps = {
            "AnchorPoint", "EmailCleaner", "UltraAI", "UltraAuthenticator",
            "UltraCleaner", "UltraFIBU", "UltraMail", "UltraPassword", "UltraSocial",
            "UltraWinManager",
        };
        return kApps;
    }

} // namespace UltraCanvas
