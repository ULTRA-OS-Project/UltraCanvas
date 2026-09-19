// include/UltraCanvasFileAssociations.h
// Cross-platform "Open with" service: enumerates the applications the
// operating system has registered for a file (display name, icon, default
// flag, in the OS's own preference order), launches files with the default
// or a specific application (always detached from the calling process), and
// prewarms its lookups on a lazily started background thread so building an
// "Open with" menu is a cache read instead of an association-database parse.
// Backends: Linux/BSD (freedesktop shared-mime-info + mimeapps.list +
// .desktop entries), Windows (SHAssocEnumHandlers / IAssocHandler — the
// handlers Explorer's own "Open with" lists) and macOS (NSWorkspace /
// Launch Services, macOS 12+). WebAssembly has no application registry and
// reports no candidates.
// Version: 1.3.0
// Last Modified: 2026-09-17
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
        // `paths`. Returns empty for an empty selection, for a file whose
        // type the platform cannot determine (Windows and macOS associate by
        // extension, so an extension-less name has no candidates there), and
        // on the platforms without an enumeration backend.
        std::vector<FileAssociationApp> GetApplicationsForFiles(
                const std::vector<std::string>& paths);

        // True when the OS names a DEFAULT application for this file — the
        // program a double-click in Explorer / Finder / the desktop's file
        // manager starts. This is the question to ask before handling a file
        // some other way ("has this file type an owner?"); the candidate list
        // above answers a different one, since it also carries the
        // applications that merely offer to open the type, and on Windows
        // falls back to the unfiltered handler list for a type nothing is
        // registered for. Cache read like GetApplicationsForFiles, and false
        // on the platforms without an enumeration backend.
        bool HasDefaultApplication(const std::string& path);

        // The type name the system knows this file by, as a freedesktop MIME
        // name ("text/plain", "image/png", "application/zip"). Matched by
        // file name — the extension, or a literal name like "makefile" — and
        // never by reading the file, so it costs no I/O beyond the
        // association database this service already keeps and is safe to ask
        // for a path that is not on this machine.
        //
        // "" where the platform keeps no MIME database of its own: Windows
        // and macOS associate by extension and by UTI respectively, and
        // neither can be asked this question. It is therefore a Linux/BSD
        // service in practice — what the freedesktop icon-naming rules need
        // (see UltraCanvasHostFileIcons.h), not a portable type test. Cache
        // read like the calls above once the index is warm.
        std::string GetMimeType(const std::string& path);

        // The GENERIC icon name a type falls back to when nothing on this
        // system has an icon for the type itself: "application/pdf" is drawn
        // as "x-office-document", a tarball as "package-x-generic". The
        // freedesktop type database carries these as exceptions to the rule
        // that a type falls back to the generic icon of its media type
        // ("image/*" to "image-x-generic"), so "" means "the rule applies",
        // not "no icon" - see UltraCanvasHostFileIcons.h, which is what asks.
        // "" on the platforms with no MIME database, like GetMimeType.
        std::string GetMimeGenericIcon(const std::string& mime);

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

        // ===== DIRECT EXECUTION (POSIX platforms) =====
        // What double-click activation should do with an executable file.
        // On Windows this always reports NotExecutable: ShellExecute's
        // "open" verb already runs .exe/.bat/… through
        // OpenWithDefaultApplication, Explorer-style. On POSIX platforms the
        // MIME machinery opens files but never runs them, so executables
        // need this separate path.
        enum class ExecutableKind {
            NotExecutable,   // no execute permission, or content is neither
                             // a native binary nor a script
            Binary,          // native executable (ELF / Mach-O — AppImages
                             // included): running is the only sensible open
            Script           // executable with a #! line: could be run or
                             // opened for viewing — worth asking
        };
        ExecutableKind ClassifyExecutable(const std::string& path);

        // Run the executable directly, detached, with its own folder as the
        // working directory. Scripts run through their #! interpreter (the
        // kernel resolves it).
        bool LaunchExecutable(const std::string& path, std::string& outError);

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
