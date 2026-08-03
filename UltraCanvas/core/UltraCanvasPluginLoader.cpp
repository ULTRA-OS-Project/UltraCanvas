// core/UltraCanvasPluginLoader.cpp
// Low-level DSO helpers shared by the framework's plugin loaders
// Version: 1.0.0
// Last Modified: 2026-08-01
// Author: UltraCanvas Framework

#include "UltraCanvasPluginLoader.h"

#if defined(_WIN32) || defined(_WIN64)
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #include <windows.h>
#else
  #include <dlfcn.h>
#endif

namespace UltraCanvas {

UCPluginLibHandle UCPluginOpen(const std::string& path, std::string* error) {
#if defined(_WIN32) || defined(_WIN64)
    HMODULE h = LoadLibraryA(path.c_str());
    if (!h && error) {
        *error = "LoadLibraryA failed with error " + std::to_string(GetLastError());
    }
    return reinterpret_cast<UCPluginLibHandle>(h);
#else
    // RTLD_GLOBAL so typeinfo is shared across modules: dynamic_cast on an
    // element created in one DSO must work in the host and in sibling DSOs
    // (the typed-configuration path of the element plugin plan).
    void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!h && error) {
        const char* reason = dlerror();
        *error = reason ? reason : "dlopen failed";
    }
    return h;
#endif
}

void* UCPluginSymbol(UCPluginLibHandle handle, const char* symbolName) {
    if (!handle) return nullptr;
#if defined(_WIN32) || defined(_WIN64)
    return reinterpret_cast<void*>(
        GetProcAddress(reinterpret_cast<HMODULE>(handle), symbolName));
#else
    return dlsym(handle, symbolName);
#endif
}

void UCPluginClose(UCPluginLibHandle handle) {
    if (!handle) return;
#if defined(_WIN32) || defined(_WIN64)
    FreeLibrary(reinterpret_cast<HMODULE>(handle));
#else
    dlclose(handle);
#endif
}

bool UCIsPluginFile(const std::filesystem::path& path) {
    const std::string ext = path.extension().string();
#if defined(_WIN32) || defined(_WIN64)
    return ext == ".dll";
#else
    return ext == ".so" || ext == ".dylib";
#endif
}

std::string UCPluginCanonicalPath(const std::filesystem::path& path) {
    std::error_code ec;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
    return ec ? path.string() : canonical.string();
}

} // namespace UltraCanvas
