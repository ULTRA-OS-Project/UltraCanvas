// VideoFX/OS/Linux/VideoFXCacheDir.cpp
// The per-user cache folder (Linux / Unix): $XDG_CACHE_HOME, else ~/.cache,
// as the XDG Base Directory specification says.
// Version: 0.11.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "VideoFXPlatform.h"

#include "../../../UltraCanvas/include/UltraCanvasPathUtf8.h"   // GetEnvUtf8

#include <string>

namespace VideoFX {
namespace Internal {

std::string UserCacheDir() {
    std::string dir = UltraCanvas::GetEnvUtf8("XDG_CACHE_HOME");
    if (!dir.empty() && dir.front() == '/') return dir;     // the spec ignores a relative one
    const std::string home = UltraCanvas::GetEnvUtf8("HOME");
    return home.empty() ? "" : home + "/.cache";
}

} // namespace Internal
} // namespace VideoFX
