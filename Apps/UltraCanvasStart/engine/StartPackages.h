// Apps/UltraCanvasStart/engine/StartPackages.h
// The dependency table: what the framework needs, grouped by purpose, with
// the package that provides it on each package manager, and the command that
// installs a list of packages. Mirrors step 1 of Docs/GettingStarted.md and
// the CI install steps; change those and this together.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "StartTypes.h"

#include <vector>

namespace UltraCanvasStart {

// Every dependency UltraCanvasStart knows about.
const std::vector<Dependency>& AllDependencies();

// The dependencies in the chosen groups that have a package on `manager`.
std::vector<const Dependency*> DependenciesFor(const Choices& choices,
                                               PackageManager manager);

// The package names `dependencies` resolve to on `manager`, in table order,
// duplicates removed. `onlyMissing` keeps the ones a check reported absent.
std::vector<std::string> PackageNames(const std::vector<const Dependency*>& dependencies,
                                      PackageManager manager);

// The command that installs `packages` with `manager`, as an argument list:
//   apt-get install -y <pkgs>        (needs elevation)
//   dnf install -y <pkgs>            (needs elevation)
//   pacman -S --needed --noconfirm   (needs elevation on Arch; not under MSYS2)
//   zypper install -y <pkgs>         (needs elevation)
//   brew install <pkgs>
// `program` is the manager's path when known, else its bare name.
PlanStep InstallStep(PackageManager manager, const std::vector<std::string>& packages,
                     const std::string& program = "");

// The architecture prefix MSYS2 packages carry: "mingw-w64-clang-x86_64-" on
// CLANG64, "mingw-w64-clang-aarch64-" on CLANGARM64. The table stores the
// x86_64 names; this rewrites them for the other environment.
std::string Msys2PackagePrefix(const std::string& architecture);
std::string Msys2PackageForArchitecture(const std::string& package,
                                        const std::string& architecture);

} // namespace UltraCanvasStart
