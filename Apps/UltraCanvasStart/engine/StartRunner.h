// Apps/UltraCanvasStart/engine/StartRunner.h
// Executes a plan step on this machine and reports its output. An elevated
// step goes through pkexec or sudo on Linux; on macOS Homebrew needs no
// elevation; on Windows pacman runs from the MSYS2 tree, which the user owns.
// Everything runs through RunProcessCaptured with an argument list.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "StartTypes.h"

#include <functional>
#include <string>

namespace UltraCanvasStart {

struct RunResult {
    bool started = false;
    int exitCode = -1;
    std::string output;      // stdout then stderr, as text
    std::string error;       // why it could not start
    bool Succeeded() const { return started && exitCode == 0; }
};

// The argv actually executed for `step` on this machine: the step's own, or
// the same behind pkexec / sudo when it needs elevation and one is present.
// `error` names the problem when no elevation helper exists.
std::vector<std::string> ElevatedArgv(const PlanStep& step, const SystemProfile& profile,
                                      std::string& error);

// Runs the step and waits for it. `onLine`, when given, receives the output
// after completion, one call per line (RunProcessCaptured does not stream).
RunResult RunStep(const PlanStep& step, const SystemProfile& profile,
                  const std::function<void(const std::string&)>& onLine = {});

} // namespace UltraCanvasStart
