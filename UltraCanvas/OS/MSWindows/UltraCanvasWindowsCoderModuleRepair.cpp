// OS/MSWindows/UltraCanvasWindowsCoderModuleRepair.cpp
// Windows backend for UltraCanvasCoderModuleRepair: whether the system
// directory holds a DLL of a given name. That directory is what the loader
// would have found for an import by that name had our own module not
// already answered to it.
// Version: 1.0.0
// Last Modified: 2026-10-01
// Author: UltraCanvas Framework
#include "UltraCanvasCoderModuleRepair.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <filesystem>
#include <string>
#include <system_error>

namespace UltraCanvas {
    namespace CoderModuleRepair {

        bool NativeIsSystemDllName(const std::string& lowerName) {
            static const std::wstring system32 = [] {
                wchar_t buffer[MAX_PATH] = {};
                const UINT n = GetSystemDirectoryW(buffer, MAX_PATH);
                return (n == 0 || n >= MAX_PATH) ? std::wstring() : std::wstring(buffer, n);
            }();
            if (system32.empty() || lowerName.empty()) return false;
            // The name is ASCII (a DLL name Windows itself uses), so the
            // widening is a plain copy.
            std::wstring wide(lowerName.begin(), lowerName.end());
            std::error_code ec;
            return std::filesystem::is_regular_file(
                    std::filesystem::path(system32) / wide, ec);   // path-string-ok: wide
        }

    } // namespace CoderModuleRepair
} // namespace UltraCanvas
