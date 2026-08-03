// include/UltraCanvasPluginLoader.h
// Low-level, platform-neutral DSO helpers shared by every plugin loader in the
// framework (element plugins now; UltraNet ports onto these later). Lifted
// from the working code in core/UltraNet/UltraNetPlugins.cpp.
//
// Version: 1.0.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework
#pragma once

#include <filesystem>
#include <string>

namespace UltraCanvas {

// Opaque library handle (HMODULE on Windows, dlopen handle elsewhere).
using UCPluginLibHandle = void*;

// Opens a DSO. On failure returns null and, when `error` is given, fills it
// with the platform's reason (dlerror / GetLastError).
UCPluginLibHandle UCPluginOpen(const std::string& path, std::string* error = nullptr);

void* UCPluginSymbol(UCPluginLibHandle handle, const char* symbolName);

void UCPluginClose(UCPluginLibHandle handle);

// Accept a plugin file by extension. CMake MODULE libraries use .so on both
// Linux and macOS; a SHARED build is .dylib on macOS - accept both on POSIX so
// discovery never depends on the build style.
bool UCIsPluginFile(const std::filesystem::path& path);

// Canonical filesystem path for idempotency keys; falls back to the input
// string when canonicalization fails.
std::string UCPluginCanonicalPath(const std::filesystem::path& path);

} // namespace UltraCanvas
