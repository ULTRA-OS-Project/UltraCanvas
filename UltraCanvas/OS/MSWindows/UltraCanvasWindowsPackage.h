// OS/MSWindows/UltraCanvasWindowsPackage.h
// Whether this process runs from an MSIX package - the Microsoft Store copy,
// or one installed with Add-AppxPackage (package-win-msix.sh) - and what that
// package declares. A packaged program lives under rules an installed one does
// not: its writes under HKEY_CURRENT_USER land in the package's own registry,
// which nothing outside the package reads; it may start an elevated process
// only when its package declares the restricted capability allowElevation; and
// Windows shows its toasts only under the package's own applications, whose
// names, icons and toast activators come from the package manifest. The
// features that rely on any of that ask here first (the desktop switch, the
// elevated delete, the toasts and their activator). See
// Docs/UltraCanvas/UltraCanvasWindowsMSIX.md.
//
// Header-only, kernel32 alone and every call looked up at run time (Windows 7
// has none of them, and nothing there is packaged): the UltraMessage library,
// which shows the toasts and does not link the core, includes it as well.
// Internal to the Windows sources; not a public header.
//
// Version: 1.1.0 - header-only; the package family name, another process's
//                  AppUserModelID, any text in the manifest
// Version: 1.0.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace UltraCanvas {
namespace WindowsPackage {

namespace Detail {

    // GetCurrentPackageFullName, GetCurrentPackageFamilyName and
    // GetCurrentPackagePath share this shape: the length in characters in and
    // out (the terminating null counted), the buffer, or none to ask the length.
    using PackageStringFn = LONG(WINAPI*)(UINT32*, PWSTR);
    using ProcessAppIdFn = LONG(WINAPI*)(HANDLE, UINT32*, PWSTR);

    inline FARPROC KernelProcedure(const char* name) {
        const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
        return kernel ? GetProcAddress(kernel, name) : nullptr;
    }

    // One of the package strings above; empty outside a package.
    inline std::wstring CurrentPackageString(const char* function) {
        const auto get = reinterpret_cast<PackageStringFn>(reinterpret_cast<void*>(KernelProcedure(function)));
        if (!get) return {};
        UINT32 length = 0;
        if (get(&length, nullptr) != ERROR_INSUFFICIENT_BUFFER || length == 0) return {};
        std::wstring text(length, L'\0');
        if (get(&length, text.data()) != ERROR_SUCCESS) return {};
        text.resize(length > 0 ? length - 1 : 0);
        return text;
    }

    // The package's AppxManifest.xml, read once; empty outside a package.
    inline const std::string& Manifest() {
        static const std::string manifest = [] {
            const std::wstring folder = CurrentPackageString("GetCurrentPackagePath");
            if (folder.empty()) return std::string();
            const std::filesystem::path file = std::filesystem::path(folder) / L"AppxManifest.xml";   // path-string-ok: wide
            std::ifstream in(file, std::ios::binary);
            return in ? std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>())
                      : std::string();
        }();
        return manifest;
    }

} // namespace Detail

// True when this process has a package identity. It cannot change while the
// process runs, so it is worked out once.
inline bool IsRunningFromPackage() {
    static const bool packaged = !Detail::CurrentPackageString("GetCurrentPackageFullName").empty();
    return packaged;
}

// The package family name ("ULTRAOSDevelopment.UltraCanvas_8wekyb3d8bbwe"),
// which begins every AppUserModelID of the package's applications; empty
// outside a package.
inline std::wstring CurrentPackageFamilyName() {
    static const std::wstring family = Detail::CurrentPackageString("GetCurrentPackageFamilyName");
    return family;
}

// The AppUserModelID Windows gave the process `processId`
// ("<package family name>!<Application Id>") when it runs from a package;
// empty when it does not, has quit, or cannot be asked.
inline std::wstring ApplicationUserModelIdOfProcess(DWORD processId) {
    const auto get = reinterpret_cast<Detail::ProcessAppIdFn>(
            reinterpret_cast<void*>(Detail::KernelProcedure("GetApplicationUserModelId")));
    if (!get || processId == 0) return {};
    const HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process) return {};
    std::wstring id;
    UINT32 length = 0;
    if (get(process, &length, nullptr) == ERROR_INSUFFICIENT_BUFFER && length > 0) {
        id.assign(length, L'\0');
        if (get(process, &length, id.data()) == ERROR_SUCCESS) id.resize(length > 0 ? length - 1 : 0);
        else id.clear();
    }
    CloseHandle(process);
    return id;
}

// Whether the installed AppxManifest.xml contains `text` as written - a
// CLSID, a capability's quoted name. False outside a package, and when the
// manifest cannot be read.
inline bool ManifestMentions(const std::string& text) {
    return !text.empty() && IsRunningFromPackage() && Detail::Manifest().find(text) != std::string::npos;
}

// Whether the package's manifest declares the capability `name` (such as
// "allowElevation"): <rescap:Capability Name="allowElevation" />, quoted as
// it was written - Windows installs the manifest exactly as it was packed.
inline bool DeclaresCapability(const std::string& name) {
    return !name.empty() &&
           (ManifestMentions("Name=\"" + name + "\"") || ManifestMentions("Name='" + name + "'"));
}

} // namespace WindowsPackage
} // namespace UltraCanvas
