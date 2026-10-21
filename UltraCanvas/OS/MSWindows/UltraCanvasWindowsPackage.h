// OS/MSWindows/UltraCanvasWindowsPackage.h
// Whether this process runs from an MSIX package - the Microsoft Store copy,
// or one installed with Add-AppxPackage (package-win-msix.sh) - and what that
// package declares. A packaged program lives under rules an installed one does
// not: its writes under HKEY_CURRENT_USER land in the package's own registry,
// which nothing outside the package reads, and it may start an elevated
// process only when its package declares the restricted capability
// allowElevation. The features that rely on either ask here first (the
// desktop switch, the elevated delete). See
// Docs/UltraCanvas/UltraCanvasWindowsMSIX.md.
//
// Internal to the Windows sources; not a public header.
//
// Version: 1.0.0
// Last Modified: 2026-10-21
// Author: UltraCanvas Framework
#pragma once

#include <string>

namespace UltraCanvas {
namespace WindowsPackage {

    // True when this process has a package identity. Looked up at run time:
    // Windows 7 has no GetCurrentPackageFullName, and nothing there is
    // packaged. The answer cannot change while the process runs, so it is
    // worked out once.
    bool IsRunningFromPackage();

    // Whether the package's manifest declares the capability `name` (such as
    // "allowElevation"). False outside a package, and when the manifest
    // cannot be read.
    bool DeclaresCapability(const std::string& name);

} // namespace WindowsPackage
} // namespace UltraCanvas
