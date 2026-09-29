// VideoFX/OS/MSWindows/VideoFXExecutableDir.cpp
// Directory of the running executable (Windows), as UTF-8: read through the
// wide API, since the ANSI one cannot hold a Thai or emoji folder name.
// Version: 0.2.0
// Last Modified: 2026-09-29
// Author: UltraCanvas Framework

#include "VideoFXPlatform.h"

#include "../../../UltraCanvas/include/UltraCanvasPathUtf8.h"   // PathToUtf8

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace VideoFX {
namespace Internal {

std::string ExecutableDir() {
    std::vector<wchar_t> buf(MAX_PATH);
    while (true) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return "";
        if (n < buf.size()) {
            // path-string-ok: built from a wide string, no code-page conversion
            const std::filesystem::path exe(std::wstring(buf.data(), n));
            return UltraCanvas::PathToUtf8(exe.parent_path());
        }
        buf.resize(buf.size() * 2);
    }
}

} // namespace Internal
} // namespace VideoFX
