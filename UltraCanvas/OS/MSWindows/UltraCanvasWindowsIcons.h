// OS/MSWindows/UltraCanvasWindowsIcons.h
// INTERNAL Windows-only helper: shell icon resources rasterized into
// UCPixmaps, and those written out as PNG files. Shared by the native
// file-icon module (UltraCanvasWindowsFileIcons.cpp, which shows the icon a
// .exe/.dll/.ico carries), the "Open with" backend
// (UltraCanvasWindowsFileAssociations.cpp, which turns the icon location of
// every registered handler into a PNG file for the menu) and the desktop
// shell (UltraCanvasWindowsDesktopShell.cpp, which does the same for the
// running programs and the Start menu). Not a public header — application
// code uses UltraCanvasNativeFileIcons.h.
// Version: 1.1.0 - WriteSurfacePng
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework
#pragma once

#include "UltraCanvasImage.h"

#include <windows.h>

#include <memory>
#include <string>

namespace UltraCanvas {
    namespace WindowsIcons {

        // HICON → premultiplied ARGB32 pixmap. Icons without an alpha
        // channel (pre-XP resources) get their coverage from the AND mask.
        // Null when the icon cannot be read. Does NOT take ownership of the
        // HICON — the caller still destroys it.
        std::shared_ptr<UCPixmap> PixmapFromIcon(HICON icon);

        // Icon number `index` of an icon-carrying file (.exe / .dll / .ico —
        // a shell icon location as GetIconLocation reports it, so a negative
        // index means "resource ID"), rasterized at roughly `desiredSize`
        // pixels: the nearest of the sizes the resource actually embeds.
        // Blocking resource access — call from a worker thread. Null when the
        // file holds no such icon.
        std::shared_ptr<UCPixmap> LoadIconResourcePixmap(
                const std::wstring& location, int index, int desiredSize);

        // `surface` as a PNG file at `path` (UTF-8). Written beside the
        // target and renamed over it, so another process writing the same
        // file at the same moment never leaves half a PNG behind, and opened
        // through the wide API, so a path outside the ANSI code page works.
        // False when the file cannot be written.
        bool WriteSurfacePng(cairo_surface_t* surface, const std::string& path);

    } // namespace WindowsIcons
} // namespace UltraCanvas
