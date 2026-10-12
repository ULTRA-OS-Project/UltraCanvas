// VideoFX/OS/MacOS/VideoFXCacheDir.cpp
// The per-user cache folder (macOS): ~/Library/Caches.
// Version: 0.11.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "VideoFXPlatform.h"

#include "../../../UltraCanvas/include/UltraCanvasPathUtf8.h"   // GetEnvUtf8

#include <string>

namespace VideoFX {
namespace Internal {

std::string UserCacheDir() {
    const std::string home = UltraCanvas::GetEnvUtf8("HOME");
    return home.empty() ? "" : home + "/Library/Caches";
}

} // namespace Internal
} // namespace VideoFX
