// Apps/UltraCanvasStart/engine/StartChecks.h
// Looks for the tools and libraries on this machine: programs by running them
// with --version, libraries by asking pkg-config. Every check runs a program
// through RunProcessCaptured with an argument list, never a shell.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "StartTypes.h"

#include <functional>
#include <vector>

namespace UltraCanvasStart {

// Runs `tool --version` and reports whether it ran and what version it said.
// `searchDirectories` are tried before PATH (the MSYS2 bin directories).
CheckResult CheckTool(const std::string& tool, const std::string& minimumVersion,
                      const std::vector<std::string>& searchDirectories = {});

// pkg-config --modversion <module>. `pkgConfig` is the pkg-config program to
// use (found on PATH when empty).
CheckResult CheckPkgConfigModule(const std::string& module,
                                 const std::string& pkgConfig = "");

// The directories a check should look in first on this system: the MSYS2
// environment's bin directory on Windows, Homebrew's on macOS.
std::vector<std::string> CheckSearchDirectories(const SystemProfile& profile);

// One result per dependency in the chosen groups for the profile's package
// manager. `progress`, when given, is called before each check with the
// dependency's title.
std::vector<CheckResult> RunChecks(const SystemProfile& profile, const Choices& choices,
                                   const std::function<void(const std::string&)>& progress = {});

} // namespace UltraCanvasStart
