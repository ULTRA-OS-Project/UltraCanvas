// include/UltraCanvasTrash.h
// Moves files and folders into the desktop's trash - the Recycle Bin on
// Windows, the Trash on macOS and on freedesktop.org desktops - from where the
// system's own file manager can restore them.
//
//   std::string error;
//   if (!MoveToTrash(path, error)) ReportError("Cannot move to the " +
//                                              TrashDisplayName() + ": " + error);
//
// Backends:
//   Windows   SHFileOperationW with FOF_ALLOWUNDO. An item the Recycle Bin
//             cannot hold (a network share, an oversized file) makes the shell
//             ask before it is deleted for good (FOF_WANTNUKEWARNING); it is
//             never destroyed silently.
//   macOS     NSFileManager trashItemAtURL, so Finder's "Put Back" works.
//   Linux/BSD The freedesktop.org Trash specification 1.0: the home trash
//             ($XDG_DATA_HOME/Trash) for files on the home file system, the
//             drive's own $topdir/.Trash/$uid or $topdir/.Trash-$uid for files
//             on another one (a USB stick, a second partition) - a trash never
//             copies a file across drives. Each item gets its .trashinfo, which
//             is what "Restore" in the desktop's file manager reads.
// Android and WebAssembly have no trash: TrashAvailable() is false there and
// MoveToTrash() fails, so a caller offers a permanent delete instead.
// Version: 1.0.0
// Last Modified: 2026-09-24
// Author: UltraCanvas Framework
#pragma once

#include <string>

namespace UltraCanvas {

    // Whether this platform has a trash MoveToTrash can put things into. A
    // particular file can still fail (a read-only drive, a drive whose trash
    // folder cannot be created) - MoveToTrash answers for that one.
    bool TrashAvailable();

    // What the platform calls its trash, for menu items and dialogs:
    // "Recycle Bin" on Windows, "Trash" everywhere else.
    std::string TrashDisplayName();

    // Moves `path` (UTF-8; a file, a folder with everything in it, or a
    // symbolic link itself) into the user's trash. Returns false with a short,
    // user-readable reason in `error` when it could not, in which case nothing
    // was moved or removed.
    bool MoveToTrash(const std::string& path, std::string& error);

    // ===== BACKEND INTERFACE (implemented per platform under OS/<Platform>/) =====
    // Not part of the public surface: callers use MoveToTrash. Platforms
    // without a backend get a fallback in core/UltraCanvasTrash.cpp that
    // always fails; CMake sets ULTRACANVAS_HAS_NATIVE_TRASH where one exists.
    bool NativeMoveToTrash(const std::string& path, std::string& error);

} // namespace UltraCanvas
