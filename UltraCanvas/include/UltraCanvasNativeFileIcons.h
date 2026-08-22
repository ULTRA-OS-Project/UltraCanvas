// include/UltraCanvasNativeFileIcons.h
// Icons the operating system embeds in (or registers for) a file — the
// application icon inside a Windows .exe / .dll, or an .ico file — rendered
// as pixmaps for file displays (the filer widget's tiles and icon columns).
// Version: 1.0.0
// Last Modified: 2026-08-21
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

} // namespace UltraCanvas
