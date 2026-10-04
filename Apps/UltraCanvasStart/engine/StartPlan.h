// Apps/UltraCanvasStart/engine/StartPlan.h
// Turns the machine, the choices and the check results into the plan: which
// packages to install, whether to fetch the SDK or clone the framework, the
// project to scaffold, and the notes a platform needs (Xcode, MSYS2, Rosetta).
// Also renders the plan as a plain-text report for the terminal, the report
// page and the clipboard.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "StartTypes.h"

#include <string>

namespace UltraCanvasStart {

// The plan for `choices.platform`. When that is the machine's own platform
// the checks decide what is missing; for another platform every package the
// chosen groups need is listed, since nothing can be checked.
Plan BuildPlan(const SystemProfile& profile, const Choices& choices,
               const std::vector<CheckResult>& checks);

// The plan as text: system, checks, steps, notes. `argvAsCommand` joins each
// step's argument list with spaces for display only.
std::string RenderReport(const Plan& plan);

// A command list as one display line, quoting arguments with spaces. Display
// only: nothing ever executes this string.
std::string DisplayCommand(const std::vector<std::string>& argv, bool elevated);

// Platform notes that are not steps: what to install by hand first.
std::vector<std::string> PlatformNotes(const SystemProfile& profile, Platform target);

} // namespace UltraCanvasStart
