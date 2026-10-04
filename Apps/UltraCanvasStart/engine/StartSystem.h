// Apps/UltraCanvasStart/engine/StartSystem.h
// Finds out what machine UltraCanvasStart is running on: platform, OS name,
// architecture, distribution and the package manager that goes with it.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "StartTypes.h"

#include <map>
#include <string>

namespace UltraCanvasStart {

// The platform this binary was compiled for.
Platform CurrentPlatform();

// Everything DetectSystem can learn without asking the user.
SystemProfile DetectSystem();

// The key=value pairs of an /etc/os-release file, quotes removed. Separate so
// the parser can be tested with a text rather than a file.
std::map<std::string, std::string> ParseOsRelease(const std::string& text);

// Which manager a Linux distribution uses, from os-release's ID and ID_LIKE.
PackageManager PackageManagerForDistribution(const std::string& id,
                                             const std::string& idLike);

// The program behind a package manager ("apt-get", "dnf", "pacman", "zypper",
// "brew"), as it is invoked.
std::string PackageManagerProgram(PackageManager manager);

// Where a program on PATH lives, or empty. `extraDirectories` are looked in
// first (UTF-8 paths), which is how the MSYS2 tree is found on Windows.
std::string FindProgram(const std::string& name,
                        const std::vector<std::string>& extraDirectories = {});

// The MSYS2 installation root on Windows ("C:/msys64"): MSYS2_ROOT, the
// directory an MSYSTEM shell runs from, then the usual places. Empty elsewhere.
std::string FindMsys2Root();

} // namespace UltraCanvasStart
