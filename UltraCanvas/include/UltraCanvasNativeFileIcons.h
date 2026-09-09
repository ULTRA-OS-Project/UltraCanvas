// include/UltraCanvasNativeFileIcons.h
// Icons the operating system embeds in (or registers for) a file — the
// application icon inside a Windows .exe / .dll, an .ico file, or the icon a
// .lnk shortcut names — rendered as pixmaps for file displays (the filer
// widget's tiles and icon columns).
//
// Two implementations answer this. On Windows the shell does, so a file also
// gets the icon its registry association provides. Everywhere else the files
// are parsed directly (UltraCanvasIconResource, UltraCanvasShellLink), which
// is what lets a Windows disk mounted on ULTRA OS, Linux or macOS — or the
// drive_c of a Wine prefix — show its programs and shortcuts with their own
// icons rather than with a generic sheet.
// Version: 1.2.0
// Last Modified: 2026-09-05
// Author: UltraCanvas Framework
#pragma once
#include "UltraCanvasImage.h"
#include <memory>
#include <string>

namespace UltraCanvas {

    // Cheap check (extension only, no file access): can a native icon be
    // rendered for `path`? True for the icon-carrying Windows file kinds
    // (.exe, .dll, .ico and the icon libraries that share the PE format) and
    // for .lnk shortcuts, which name one of them. The same answer on every
    // platform — the icons live in the files, not in the host system.
    bool NativeFileIconAvailable(const std::string& path);

    // Extract the file's icon and rasterize it at roughly `desiredSize`
    // pixels (the nearest of the sizes the file actually embeds). For a
    // shortcut this is the icon it names — normally the icon of the program
    // it starts. Blocking file + resource access — call from a worker
    // thread. Null when the file holds no icon this build can read.
    std::shared_ptr<UCPixmap> LoadNativeFileIconPixmap(const std::string& path,
                                                       int desiredSize);

    // Per-thread setup the platform extractor needs, held for the lifetime
    // of a worker thread that calls LoadNativeFileIconPixmap. On Windows the
    // extraction goes through the shell, which expects the calling thread to
    // have joined a COM apartment; a thread that has not can get an
    // extraction failure for a file that extracts perfectly well elsewhere,
    // which is what made application icons show up on some runs and not on
    // others. Where the icons are read from the files themselves there is
    // nothing to set up and this is an empty object.
    class NativeFileIconThreadScope {
    public:
        NativeFileIconThreadScope();
        ~NativeFileIconThreadScope();
        NativeFileIconThreadScope(const NativeFileIconThreadScope&) = delete;
        NativeFileIconThreadScope& operator=(const NativeFileIconThreadScope&) = delete;

    private:
        // True only when this object is the one that joined the apartment,
        // so the matching leave stays balanced.
        bool joined = false;
    };

} // namespace UltraCanvas
