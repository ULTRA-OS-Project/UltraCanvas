// Apps/UltraCanvasStart/engine/StartRunner.cpp
// Version: 0.1.2 - the output line by line as it comes (RunProcessCaptured's onOutput)
// Version: 0.1.1 - on Windows the MSYS2 bin directories on the child's PATH and
//                  MSYSTEM set, found again when MSYS2 was installed meanwhile
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StartRunner.h"

#include "StartChecks.h"
#include "StartSystem.h"

#include "UltraCanvasPathUtf8.h"
#include "UltraCanvasUtils.h"


namespace UltraCanvasStart {

std::vector<std::string> ElevatedArgv(const PlanStep& step, const SystemProfile& profile,
                                      std::string& error) {
    error.clear();
    if (!step.needsElevation) return step.argv;
#if defined(_WIN32)
    (void)profile;
    error = "This step needs an administrator shell; run the command there.";
    return {};
#else
    if (profile.platform == Platform::Linux) {
        // pkexec asks in a desktop dialog, which is what a GUI wants; sudo
        // only works when there is a terminal to type the password into.
        const std::string pkexec = FindProgram("pkexec");
        if (!pkexec.empty()) {
            std::vector<std::string> argv = { pkexec };
            argv.insert(argv.end(), step.argv.begin(), step.argv.end());
            return argv;
        }
    }
    const std::string sudo = FindProgram("sudo");
    if (!sudo.empty()) {
        std::vector<std::string> argv = { sudo, "-n" };
        argv.insert(argv.end(), step.argv.begin(), step.argv.end());
        return argv;
    }
    error = "Neither pkexec nor sudo was found; run the command as root.";
    return {};
#endif
}

RunResult RunStep(const PlanStep& step, const SystemProfile& profile,
                  const std::function<void(const std::string&)>& onLine) {
    RunResult result;
    if (step.argv.empty()) {
        result.error = "This step has no command; it is done by hand.";
        return result;
    }
    const std::vector<std::string> argv = ElevatedArgv(step, profile, result.error);
    if (argv.empty()) return result;

    // On Windows pacman and its hooks find MSYS2's programs the way the
    // CLANG64 shell does. The root is looked for again: winget may have just
    // installed it.
    std::vector<std::pair<std::string, std::string>> environment;
    if (profile.platform == Platform::Windows) {
        SystemProfile here = profile;
        if (here.msysPrefix.empty()) here.msysPrefix = FindMsys2Root();
        if (!here.msysPrefix.empty()) {
            std::string path;
            for (const auto& directory : CheckSearchDirectories(here)) path += directory + ";";
            path += UltraCanvas::GetEnvUtf8("PATH");
            environment.emplace_back("PATH", path);
            environment.emplace_back("MSYSTEM", here.architecture == "arm64" ? "CLANGARM64" : "CLANG64");
        }
    }
    // Lines as they come, per stream; pacman and winget end a progress
    // line with '\r', which counts as an end too.
    std::string pending[2];
    UltraCanvas::ProcessOutputCallback chunk;
    if (onLine) {
        chunk = [&pending, &onLine](const char* data, size_t size, bool fromError) {
            std::string& line = pending[fromError ? 1 : 0];
            for (size_t i = 0; i < size; ++i) {
                const char c = data[i];
                if (c == '\n' || c == '\r') {
                    if (!line.empty()) onLine(line);
                    line.clear();
                } else {
                    line += c;
                }
            }
        };
    }
    const auto output = UltraCanvas::RunProcessCaptured(argv, {}, environment, chunk);
    if (onLine) {
        for (const auto& rest : pending) if (!rest.empty()) onLine(rest);
    }
    result.started = output.started;
    result.exitCode = output.exitCode;
    result.error = output.error;
    result.output.assign(output.standardOutput.begin(), output.standardOutput.end());
    if (!output.standardError.empty()) {
        if (!result.output.empty() && result.output.back() != '\n') result.output += '\n';
        result.output += output.standardError;
    }
    return result;
}

} // namespace UltraCanvasStart
