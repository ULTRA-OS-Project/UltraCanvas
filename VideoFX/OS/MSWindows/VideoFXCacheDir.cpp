// VideoFX/OS/MSWindows/VideoFXCacheDir.cpp
// The per-user cache folder (Windows): %LOCALAPPDATA% - local, not roaming,
// so proxies are never copied around with the user's profile. Read as
// UTF-8 through the wide environment (GetEnvUtf8).
// Version: 0.11.0
// Last Modified: 2026-10-12
// Author: UltraCanvas Framework

#include "VideoFXPlatform.h"

#include "../../../UltraCanvas/include/UltraCanvasPathUtf8.h"   // GetEnvUtf8

#include <string>

namespace VideoFX {
namespace Internal {

std::string UserCacheDir() {
    std::string dir = UltraCanvas::GetEnvUtf8("LOCALAPPDATA");
    if (!dir.empty()) return dir;
    const std::string profile = UltraCanvas::GetEnvUtf8("USERPROFILE");
    return profile.empty() ? "" : profile + "\\AppData\\Local";
}

} // namespace Internal
} // namespace VideoFX
