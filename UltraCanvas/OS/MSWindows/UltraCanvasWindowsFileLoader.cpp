// OS/MSWindows/UltraCanvasWindowsFileLoader.cpp
// Windows implementation of UltraCanvasFileLoader::NotifyRecentFile.
// Registers the file with the Explorer recent-documents list and the per-app
// JumpList via SHAddToRecentDocs.
// Version: 1.0.0
// Last Modified: 2026-05-12
// Author: UltraCanvas Framework

#include "UltraCanvasFileLoader.h"

#if defined(_WIN32) || defined(_WIN64)

#include "UltraCanvasWindowsApplication.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shlobj.h>

namespace UltraCanvas {

    void UltraCanvasFileLoader::NotifyRecentFile(const std::string& filePath) {
        if (filePath.empty()) return;

        std::wstring wpath = UltraCanvasWindowsApplication::Utf8ToUtf16(filePath);
        SHAddToRecentDocs(SHARD_PATHW, wpath.c_str());
    }

} // namespace UltraCanvas

#endif // _WIN32 || _WIN64
