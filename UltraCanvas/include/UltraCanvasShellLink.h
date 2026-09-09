// include/UltraCanvasShellLink.h
// Windows shell links (".lnk" shortcuts) read on every platform.
//
// A shortcut is a small binary file (the MS-SHLLINK format) that names the
// file it points at, the icon it wants to be drawn with, and the command
// line to start it with. Windows resolves one through the shell; everywhere
// else - ULTRA OS, Linux and macOS looking at a mounted Windows disk or at
// the drive_c of a Wine prefix - nothing does, which is why file displays
// used to show a shortcut as a nameless "LNK" sheet. This reader is that
// missing piece: it parses the file itself, so the answer is the same on all
// four platforms.
//
// The paths inside a shortcut are Windows paths ("C:\Program Files\App.exe")
// and mean nothing to a POSIX open(). Every path this reader returns is
// therefore offered twice: as the link stores it, and mapped onto the host
// filesystem (ResolveWindowsPathOnHost) - empty when this machine has no
// such file.
// Version: 1.0.0
// Last Modified: 2026-09-05
// Author: UltraCanvas Framework
#pragma once
#include <cstdint>
#include <ctime>
#include <string>

namespace UltraCanvas {

    // ===== WHAT A SHORTCUT SAYS =====
    // Everything the file display, the properties dialog and the launcher
    // need. Windows-path fields are always filled when the link carries
    // them; the matching host* field is filled only when the file exists on
    // this machine.
    struct UCShellLink {
        std::string targetPath;        // "C:\\Program Files\\App\\App.exe"
        std::string hostTargetPath;    // the same file as this host opens it
        std::string arguments;         // command line passed to the target
        std::string workingDirectory;  // Windows path
        std::string description;       // the link's comment ("Name" string)
        std::string relativePath;      // ".\\App.exe", relative to the link
        std::string iconLocation;      // file holding the icon to draw
        std::string hostIconLocation;  // the same file on this host
        int iconIndex = 0;             // index into it (negative = resource id)
        bool targetIsDirectory = false;
        uint32_t targetAttributes = 0; // FILE_ATTRIBUTE_* of the target
        uint64_t targetSize = 0;       // size the target had when linked
        std::time_t targetModifiedTime = 0;
        // A UNC target ("\\\\server\\share\\file") - it resolves through the
        // network, not through a drive letter, so hostTargetPath stays empty
        // unless the share happens to be mounted at the same place.
        bool targetIsNetworkPath = false;
    };

    // Extension test only, no file access: is this file name a shortcut?
    bool IsShellLinkPath(const std::string& path);

    // Read `linkPath` and fill `out`. False when the file is missing, is not
    // a shell link (wrong header signature / CLSID) or is truncated - `out`
    // is left untouched in that case. Blocking file access; the file is a
    // few kilobytes, but on a cold network share that is still I/O.
    bool ReadShellLink(const std::string& linkPath, UCShellLink& out);

    // Map a Windows path onto this host. `contextPath` is a host path near
    // the file - normally the shortcut being read - and is what makes the
    // mapping possible off Windows: the drive the path names is looked for
    // by walking up from it, first for the drive_c / dosdevices layout of a
    // Wine prefix, then for the root of a mounted Windows system disk (a
    // directory holding both "Windows" and "Users"), and finally through
    // $WINEPREFIX and ~/.wine. Path components are matched case-insensitively,
    // because Windows wrote them that way and the host filesystem is not.
    // Returns "" when the path cannot be mapped or the file is not there.
    // On Windows the path is only environment-expanded and returned.
    std::string ResolveWindowsPathOnHost(const std::string& windowsPath,
                                         const std::string& contextPath);

    // Expand %VAR% references in a Windows path the way the shell would:
    // from the process environment on Windows, and off Windows from the
    // well-known layout of a Windows disk (%ProgramFiles% ->
    // "C:\Program Files", %SystemRoot% -> "C:\Windows", ...). An unknown
    // variable is left as it stands rather than replaced with nothing, so a
    // failed expansion is visible instead of silently producing a valid-
    // looking path to the wrong place.
    std::string ExpandWindowsEnvironmentPath(const std::string& path);

} // namespace UltraCanvas
