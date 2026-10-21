// OS/MSWindows/UltraCanvasWindowsPackage.cpp
// See UltraCanvasWindowsPackage.h.
// Version: 1.0.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "UltraCanvasWindowsPackage.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace UltraCanvas {
namespace WindowsPackage {

namespace {

// GetCurrentPackageFullName and GetCurrentPackagePath share this shape: the
// length in characters in and out, the buffer (or none, to ask the length).
using PackageStringFn = LONG(WINAPI*)(UINT32*, PWSTR);

PackageStringFn KernelFunction(const char* name) {
    const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    return kernel ? reinterpret_cast<PackageStringFn>(reinterpret_cast<void*>(GetProcAddress(kernel, name)))
                  : nullptr;
}

// The package's install folder, where its AppxManifest.xml is; empty outside
// a package.
std::wstring PackageFolder() {
    const PackageStringFn getPath = KernelFunction("GetCurrentPackagePath");
    if (!getPath) return {};
    UINT32 length = 0;
    if (getPath(&length, nullptr) != ERROR_INSUFFICIENT_BUFFER || length == 0) return {};
    std::wstring folder(length, L'\0');
    if (getPath(&length, folder.data()) != ERROR_SUCCESS) return {};
    folder.resize(length > 0 ? length - 1 : 0);   // the length counts the terminating null
    return folder;
}

// The package's AppxManifest.xml, read once; empty outside a package.
const std::string& Manifest() {
    static const std::string manifest = [] {
        const std::wstring folder = PackageFolder();
        if (folder.empty()) return std::string();
        const std::filesystem::path file = std::filesystem::path(folder) / L"AppxManifest.xml";   // path-string-ok: wide
        std::ifstream in(file, std::ios::binary);
        return in ? std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>())
                  : std::string();
    }();
    return manifest;
}

} // namespace

bool IsRunningFromPackage() {
    static const bool packaged = [] {
        const PackageStringFn getName = KernelFunction("GetCurrentPackageFullName");
        if (!getName) return false;
        UINT32 length = 0;
        // APPMODEL_ERROR_NO_PACKAGE (15700) without a package; with one, the
        // empty buffer is too small for its name.
        return getName(&length, nullptr) == ERROR_INSUFFICIENT_BUFFER;
    }();
    return packaged;
}

bool DeclaresCapability(const std::string& name) {
    if (name.empty() || !IsRunningFromPackage()) return false;
    // <rescap:Capability Name="allowElevation" /> - the name is quoted as
    // written; Windows installs the manifest exactly as it was packed.
    const std::string& manifest = Manifest();
    return manifest.find("Name=\"" + name + "\"") != std::string::npos ||
           manifest.find("Name='" + name + "'") != std::string::npos;
}

} // namespace WindowsPackage
} // namespace UltraCanvas
