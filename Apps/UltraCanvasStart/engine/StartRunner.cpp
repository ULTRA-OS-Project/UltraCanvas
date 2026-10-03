// Apps/UltraCanvasStart/engine/StartRunner.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StartRunner.h"

#include "StartSystem.h"

#include "UltraCanvasUtils.h"

#include <sstream>

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

    const auto output = UltraCanvas::RunProcessCaptured(argv, {});
    result.started = output.started;
    result.exitCode = output.exitCode;
    result.error = output.error;
    result.output.assign(output.standardOutput.begin(), output.standardOutput.end());
    if (!output.standardError.empty()) {
        if (!result.output.empty() && result.output.back() != '\n') result.output += '\n';
        result.output += output.standardError;
    }
    if (onLine) {
        std::istringstream in(result.output);
        std::string line;
        while (std::getline(in, line)) onLine(line);
    }
    return result;
}

} // namespace UltraCanvasStart
