// OS/Android/UltraCanvasAndroidFileLoader.cpp
// Android implementation of UltraCanvasFileLoader::NotifyRecentFile.
// Android has no cross-app recent-files list an NDK app can feed (the
// freedesktop equivalent does not exist); deliberately a no-op.
// Version: 1.0.0
// Last Modified: 2026-08-10
// Author: UltraCanvas Framework

#include "UltraCanvasFileLoader.h"

namespace UltraCanvas {

    void UltraCanvasFileLoader::NotifyRecentFile(const std::string&) {}

} // namespace UltraCanvas
