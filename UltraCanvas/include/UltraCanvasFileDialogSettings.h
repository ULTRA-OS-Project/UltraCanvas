// include/UltraCanvasFileDialogSettings.h
// What UltraCanvasFileDialog remembers between showings, per user and shared
// by every application: the view, the window size, the Details column widths
// and the last used folder - and whether that folder is one for all
// applications (Global) or kept per application (Individual). ULTRA OS
// settings (UOS-Settings) edits the last two.
//
// The file is FileDialog.conf in the UltraCanvas settings folder:
//   Windows  %APPDATA%\UltraCanvas
//   macOS    ~/Library/Application Support/UltraCanvas
//   others   $XDG_CONFIG_HOME/UltraCanvas, else ~/.config/UltraCanvas
// Several applications write it, so every change goes through Update(),
// which re-reads the file, applies the change and writes it back: one
// application's write never throws away what another wrote meanwhile.
//
// Version: 1.0.0
// Last Modified: 2026-10-01
// Author: UltraCanvas Framework
#pragma once

#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace UltraCanvas {

    // Where an application's file dialog finds its last used folder.
    enum class LastFolderScope {
        Global,       // the one folder every application shares
        Individual    // the application's own
    };

    struct FileDialogAppSettings {
        // Used while the overall mode is Individual; under Global every
        // application shares the global folder whatever this says.
        LastFolderScope scope = LastFolderScope::Individual;
        std::string lastFolder;   // UTF-8; the application's own last folder

        bool operator==(const FileDialogAppSettings&) const = default;
    };

    struct FileDialogSettings {
        // ----- last used folder -----
        // The switch: Global - every application opens where any of them
        // last was; Individual - each application decides (apps below).
        LastFolderScope lastFolderMode = LastFolderScope::Global;
        std::string globalFolder;                            // UTF-8
        std::map<std::string, FileDialogAppSettings> apps;   // by application name

        // ----- look -----
        int view = 0;              // index of the view button (Details, List, icons...)
        int width = 0;             // window size; 0 = not remembered
        int height = 0;
        int sizeColumn = 80;       // Details column widths; Name takes the rest
        int typeColumn = 105;
        int modifiedColumn = 145;

        // The scope that applies to `appName`: Global while the mode is
        // Global, the application's own choice otherwise (Individual for an
        // application not listed yet).
        LastFolderScope EffectiveScope(const std::string& appName) const;
        // The folder `appName`'s file dialog should open in ("" = none). An
        // Individual application that has no folder of its own yet gets the
        // global one.
        std::string LastFolderFor(const std::string& appName) const;
        // Records `folder` as `appName`'s last used folder - the global one or
        // the application's own, whichever its scope says.
        void SetLastFolderFor(const std::string& appName, const std::string& folder);

        bool operator==(const FileDialogSettings&) const = default;

        // ----- persistence -----
        static std::filesystem::path FilePath();
        // Reads the file; a missing file is the defaults. A file that is
        // there but cannot be read is reported on the debug stream, once.
        static FileDialogSettings Load();
        bool Save() const;
        // Re-read, change, write back. Returns false when the file could not
        // be written. A change that leaves the settings as they were writes
        // nothing: every file dialog closes through here, and most closes
        // change nothing.
        static bool Update(const std::function<void(FileDialogSettings&)>& change);
    };

    // The applications of the suite whose file dialog is this framework's own
    // (the others use the platform's picker), so the settings can list them
    // before any of them has opened a file dialog.
    const std::vector<std::string>& KnownFileDialogApplications();

} // namespace UltraCanvas
