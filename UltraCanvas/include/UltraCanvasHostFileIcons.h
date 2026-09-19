// include/UltraCanvasHostFileIcons.h
// The icon the HOST DESKTOP shows for a file of this type — the picture
// Explorer, Finder or the Linux file manager draws for a ".txt", a ".zip" or
// a folder, taken from the system the application is running on.
//
// This is the type-wide counterpart of UltraCanvasNativeFileIcons.h, and the
// two answer different questions:
//
//   UltraCanvasNativeFileIcons  "what icon does THIS FILE carry?" — the
//                               application icon inside a .exe / .dll, an
//                               .ico, the icon a .lnk or .desktop names, the
//                               .icns in a bundle. Read out of the file, so
//                               the answer is the same on every platform and
//                               a Windows disk mounted on Linux still shows
//                               its programs with their own icons.
//
//   UltraCanvasHostFileIcons    "what does THIS SYSTEM draw for a file of
//                               this KIND?" — the registered/themed type
//                               icon. There is nothing in the file to read:
//                               the answer comes from the host's icon theme
//                               or shell, it is per TYPE rather than per
//                               file, and it differs from desktop to desktop.
//
// A file display asks the native module first (a program's own icon wins over
// the generic "application" icon) and this one for everything else, so the
// listing looks like the rest of the desktop instead of like a second file
// manager. The filer widget does exactly that under
// FilerFileIconStyle::HostOperatingSystem.
//
// Backends: Linux/BSD (freedesktop — the file's MIME type resolved through
// UltraCanvasFileAssociations, then the icon-naming-spec names looked up in
// the installed icon themes by UltraCanvasDesktopEntry), Windows (the shell's
// system image list, which is what Explorer itself draws from) and macOS
// (NSWorkspace). Platforms with no desktop to ask — WebAssembly, Android —
// report unavailable, and a caller then keeps its own drawn icons.
// Version: 1.0.0
// Last Modified: 2026-09-17
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasImage.h"

#include <memory>
#include <string>

namespace UltraCanvas {

    // Does this build have a host desktop to ask at all? False on the
    // platforms without an icon source, where LoadHostFileIconPixmap always
    // returns null — a settings page offering "host icons" should say so
    // rather than offer a switch that changes nothing.
    bool HostFileIconsAvailable();

    // What the answer for `path` depends on — the cache key a caller holds
    // its resolved icons under. Files of the same kind share one key, which
    // is the point: a folder of four thousand ".txt" files resolves ONE icon,
    // not four thousand. Pure string work, no file access:
    //
    //   a directory                     → "dir"
    //   a file carrying its own icon    → "file:<path>"  (.exe / .lnk / … —
    //                                     those belong to the native module,
    //                                     and are per file, not per type)
    //   a named file with no extension  → "name:<lowercased file name>"
    //                                     (the freedesktop database matches
    //                                     "makefile" and "README" by name)
    //   everything else                 → "ext:<lowercased extension>"
    std::string HostFileIconKey(const std::string& path, bool isDirectory);

    // The host's icon for `path`, rasterized at roughly `desiredSize` pixels.
    // `isDirectory` is passed rather than probed so a caller that has already
    // listed the folder does not pay for a second stat, and so the icon can
    // be resolved for a path that is not on this machine at all.
    //
    // Blocking: it reads icon themes / asks the shell — call it from a worker
    // thread, holding a NativeFileIconThreadScope for the life of that thread
    // (the Windows shell wants the calling thread in a COM apartment; see
    // UltraCanvasNativeFileIcons.h). Null when this system has no icon for
    // the type, which is not a failure — it is the answer on a machine with
    // no icon theme installed, and the caller draws its own icon instead.
    std::shared_ptr<UCPixmap> LoadHostFileIconPixmap(const std::string& path,
                                                     bool isDirectory,
                                                     int desiredSize);

    // Forget what the host said — after the user switches desktop theme, or a
    // package installs icons. The caller drops its own cached pixmaps; this
    // drops the lookups underneath them.
    void RefreshHostFileIcons();

} // namespace UltraCanvas
