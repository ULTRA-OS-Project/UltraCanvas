// Apps/UltraCanvasStart/engine/StartBuild.h
// Compiles the project skeleton the way a programmer does - the default
// preset configured, then built - so the Build step can prove that the
// toolchain, the SDK and the skeleton work together before the hand-over,
// and so a test can compile what ScaffoldProject writes. Everything runs
// through RunProcessCaptured with an argument list, never a shell.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "StartTypes.h"

#include <functional>
#include <string>
#include <vector>

namespace UltraCanvasStart {

struct BuildResult {
    bool started = false;      // cmake was found and ran
    bool configured = false;   // the configure step succeeded
    bool built = false;        // the build step succeeded
    int exitCode = -1;         // of the last command that ran
    std::string output;        // both commands' output, as text
    std::string error;         // why it could not start
    bool Succeeded() const { return configured && built; }
};

// The two commands, as argv, in the order they run: the default preset
// configured (`cmake -S <folder> --preset default`) and the preset's build
// directory built (`cmake --build <folder>/build --parallel N`). `cmake` is
// the one the checks find (CheckSearchDirectories), so on Windows the MSYS2
// one is used. The paths are UTF-8. Empty when no cmake is found.
std::vector<std::vector<std::string>> BuildCommands(const std::string& folder,
                                                    const SystemProfile& profile);

// The same commands as a programmer types them, one per line, for display.
std::string BuildCommandsText(const std::string& folder);

// Runs the commands and waits for them; stops at the first failure.
// `onOutput`, when given, receives each command's line (`$ ...`) and its
// output once it has finished (RunProcessCaptured does not stream). On
// Windows the MSYS2 bin directories are put on PATH for the child, as the
// CLANG64 shell would have them.
BuildResult BuildProject(const std::string& folder, const SystemProfile& profile,
                         const std::function<void(const std::string&)>& onOutput = {});

} // namespace UltraCanvasStart
