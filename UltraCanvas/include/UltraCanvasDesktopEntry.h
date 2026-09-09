// include/UltraCanvasDesktopEntry.h
// Freedesktop desktop entries (".desktop") — what a Linux/BSD shortcut is.
//
// A desktop entry is the Linux counterpart of a Windows ".lnk"
// (UltraCanvasShellLink.h): a small text file that names a program to start,
// a document or URL to open, the command line to use, and the icon it should
// be drawn with. This module reads one, resolves its icon name through the
// icon themes installed on the machine, and expands its Exec line into an
// argument vector.
//
// It is the framework's single reader for the format: the "Open with"
// service builds its application index from it, and the filer widget draws
// and launches the entries a folder holds. Reading is plain text plus
// std::filesystem, so it is safe on background threads and costs no
// dependency; nothing here launches anything (see UltraCanvasFileAssociations
// for that).
// Version: 1.0.0
// Last Modified: 2026-09-05
// Author: UltraCanvas Framework
#pragma once

#include <string>
#include <vector>

namespace UltraCanvas {

    // ===== WHAT A DESKTOP ENTRY SAYS =====
    struct UCDesktopEntry {
        // Type=. An entry of an unknown type is still readable — it simply
        // has nothing to start.
        enum class Kind { Unknown, Application, Link, Directory };

        Kind kind = Kind::Unknown;
        std::string name;             // Name=, in the user's language
        std::string genericName;      // GenericName=, e.g. "Web Browser"
        std::string comment;          // Comment=, the tooltip text
        std::string exec;             // Exec=, verbatim (field codes intact)
        std::string tryExec;          // TryExec=
        std::string workingDirectory; // Path=
        std::string iconName;         // Icon=, an icon name or an absolute path
        std::string url;              // URL=, for Kind::Link
        std::vector<std::string> mimeTypes;   // MimeType=
        bool terminal = false;        // Terminal=true — needs a terminal window
        bool noDisplay = false;       // NoDisplay=true — not for menus
        bool hidden = false;          // Hidden=true — "deleted", ignore it

        // Resolved against this machine, empty when it is not there:
        std::string iconFile;         // the image file `iconName` resolves to
        std::string program;          // the executable Exec/TryExec runs
    };

    // Extension test only, no file access.
    bool IsDesktopEntryPath(const std::string& path);

    // Read `path`'s [Desktop Entry] group. False when the file cannot be read
    // or carries no such group — a file that merely ends in ".desktop" is
    // therefore never mistaken for one. Localized keys are resolved against
    // the user's language (LC_MESSAGES / LC_ALL / LANG), falling back to the
    // unlocalized value.
    bool ReadDesktopEntry(const std::string& path, UCDesktopEntry& out);

    // The image file an Icon= value names: an absolute path as it stands, or
    // an icon name looked up in the installed icon themes — the configured
    // theme and what it inherits, then hicolor (which every theme falls back
    // to), then the flat pixmaps directories. `desiredSize` picks between the
    // sizes a theme installs: the exact size if it has it, else a scalable
    // (SVG) icon, else the nearest larger one. "" when nothing matches.
    // Answers are cached; the cache is dropped by RefreshDesktopIconThemes().
    std::string FindDesktopIconFile(const std::string& iconName, int desiredSize);

    // The icon theme in use. Detected once from the desktop's own settings
    // (GTK's settings.ini, KDE's kdeglobals), and overridable by a host that
    // tracks the setting itself — pass "" to go back to detection.
    void SetDesktopIconTheme(const std::string& themeName);
    std::string GetDesktopIconTheme();
    // Forget the detected theme, its inheritance chain and every cached
    // lookup — after the user changes theme, or a package installs icons.
    void RefreshDesktopIconThemes();

    // Exec= expanded into an argument vector, ready for exec(): the field
    // codes %f/%F/%u/%U are replaced by `files` (%i/%c/%k and unknown codes
    // are dropped, "%%" is a literal percent), and an entry with no file code
    // gets them appended. Empty when the entry has no command.
    std::vector<std::string> DesktopEntryCommand(
            const UCDesktopEntry& entry,
            const std::vector<std::string>& files = {});

} // namespace UltraCanvas
