// include/UltraCanvasNativeFileIcons.h
// Icons the operating system embeds in (or registers for) a file — the
// application icon inside a Windows .exe / .dll, or an .ico file — rendered
// as pixmaps for file displays (the filer widget's tiles and icon columns).
// Version: 1.1.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework
#pragma once
#include "UltraCanvasImage.h"
#include <memory>
#include <string>

namespace UltraCanvas {

    // Cheap check (extension only, no file access): can this platform render
    // a native icon for `path`? Windows: .exe / .dll / .ico. Other
    // platforms: always false, so callers change no behavior there.
    bool NativeFileIconAvailable(const std::string& path);

    // Extract the file's icon and rasterize it at roughly `desiredSize`
    // pixels (the nearest of the sizes the file actually embeds). Blocking
    // file + resource access — call from a worker thread. Null when the file
    // holds no icon or the platform has no extractor.
    std::shared_ptr<UCPixmap> LoadNativeFileIconPixmap(const std::string& path,
                                                       int desiredSize);

    // Per-thread setup the platform extractor needs, held for the lifetime
    // of a worker thread that calls LoadNativeFileIconPixmap. On Windows the
    // extraction goes through the shell, which expects the calling thread to
    // have joined a COM apartment; a thread that has not can get an
    // extraction failure for a file that extracts perfectly well elsewhere,
    // which is what made application icons show up on some runs and not on
    // others. On platforms without an extractor this is an empty object.
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
