// include/UltraCanvasFileAssociations.h
// Cross-platform "Open with" service: enumerates the applications the
// operating system has registered for a file (display name, icon, default
// flag, in the OS's own preference order), launches files with the default
// or a specific application (always detached from the calling process), and
// prewarms its lookups on a lazily started background thread so building an
// "Open with" menu is a cache read instead of an association-database parse.
// Backends: Linux/BSD (freedesktop shared-mime-info + mimeapps.list +
// .desktop entries); Windows and macOS currently launch with the default
// application only and report no enumerable candidates (their full backends
// are later phases — see UltraCanvasFileAssociationsProposal.md).
// Version: 1.0.0
// Last Modified: 2026-08-16
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <vector>

namespace UltraCanvas {

    // One application the OS has registered for a file type.
    struct FileAssociationApp {
        std::string id;        // stable key: .desktop id / ProgID / bundle path
        std::string name;      // user-visible name, e.g. "LibreOffice Writer"
        std::string iconPath;  // resolved icon image file; empty when none found
        bool isDefault = false;
    };

    namespace FileAssociations {

        // Applications registered for ALL of the given files (intersection
        // across their types), in the order the OS prefers them for the first
        // file; that file's default application comes first and carries
        // isDefault. Served from the prewarm cache when warm (no I/O); a cold
        // call resolves synchronously, bounded by the distinct file types in
        // `paths`. Returns empty for an empty selection or on the platforms
        // without an enumeration backend.
        std::vector<FileAssociationApp> GetApplicationsForFiles(
                const std::vector<std::string>& paths);

        // Launch with the OS default application (Explorer / Finder
        // double-click semantics), detached: closing the caller never takes
        // the launched application down.
        bool OpenWithDefaultApplication(const std::vector<std::string>& paths,
                                        std::string& outError);

        // Launch with one specific application from GetApplicationsForFiles().
        bool OpenWithApplication(const FileAssociationApp& app,
                                 const std::vector<std::string>& paths,
                                 std::string& outError);

        // Launch with an arbitrary application the user picked ("Other
        // application…"): an executable, or a .app bundle on macOS. The
        // picker UI itself lives with the caller (it needs a parent window)
        // — see GetApplicationFilter() / GetApplicationsDirectory() for the
        // file-dialog setup.
        bool OpenWithApplicationPath(const std::string& applicationPath,
                                     const std::vector<std::string>& paths,
                                     std::string& outError);

        // File-dialog helpers for an "Other application…" picker: what an
        // application looks like on this platform, and where they live.
        struct ApplicationFilter {
            std::string description;
            std::vector<std::string> extensions;   // {"*"} where extension-less
        };
        ApplicationFilter GetApplicationFilter();
        std::string GetApplicationsDirectory();

        // ===== PREWARM (see UltraCanvasFileAssociationsProposal.md §7) =====
        // Parse the platform association database on the background worker so
        // later lookups are cache hits. Cheap to call repeatedly; the worker
        // thread is only spawned by the first Prewarm* call, so applications
        // that never use file associations pay nothing.
        void PrewarmAsync();

        // Resolve candidates (and icons) for these extensions on the worker,
        // e.g. the distinct extensions of a folder just scanned. Lowercase,
        // without the leading dot; already-cached extensions are skipped.
        void PrewarmExtensionsAsync(const std::vector<std::string>& extensions);

    } // namespace FileAssociations
} // namespace UltraCanvas
