// OS/MSWindows/UltraCanvasWindowsIcons.h
// INTERNAL Windows-only helper: shell icon resources rasterized into
// UCPixmaps. Shared by the native file-icon module
// (UltraCanvasWindowsFileIcons.cpp, which shows the icon a .exe/.dll/.ico
// carries) and the "Open with" backend
// (UltraCanvasWindowsFileAssociations.cpp, which turns the icon location of
// every registered handler into a PNG file for the menu). Not a public
// header — application code uses UltraCanvasNativeFileIcons.h.
// Version: 1.0.0
// Last Modified: 2026-08-24
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

    } // namespace WindowsIcons
} // namespace UltraCanvas
