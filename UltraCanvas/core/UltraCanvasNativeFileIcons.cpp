// core/UltraCanvasNativeFileIcons.cpp
// Fallback for platforms without a native file-icon extractor. The real
// implementations live in the platform directories (currently
// OS/MSWindows/UltraCanvasWindowsFileIcons.cpp); everywhere else these
// stubs report "no icon" and file displays keep their generic glyphs.
// Version: 1.1.0
// Last Modified: 2026-09-04
// Author: UltraCanvas Framework
#include "UltraCanvasNativeFileIcons.h"

#ifndef _WIN32
namespace UltraCanvas {

    bool NativeFileIconAvailable(const std::string&) { return false; }

    std::shared_ptr<UCPixmap> LoadNativeFileIconPixmap(const std::string&,
                                                       int) {
        return nullptr;
    }

    // No extractor, so nothing for a worker thread to set up.
    NativeFileIconThreadScope::NativeFileIconThreadScope() = default;
    NativeFileIconThreadScope::~NativeFileIconThreadScope() = default;

} // namespace UltraCanvas
#endif // !_WIN32
