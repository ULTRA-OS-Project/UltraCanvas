// Apps/UltraCanvasStart/engine/StartBuild.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StartBuild.h"

#include "StartChecks.h"
#include "StartSystem.h"

#include "UltraCanvasUtils.h"

#include <algorithm>
#include <cstdlib>
#include <thread>

namespace UltraCanvasStart {

namespace {

std::string ParallelJobs() {
    const unsigned cores = std::thread::hardware_concurrency();
    return std::to_string(std::clamp(cores == 0 ? 2u : cores, 1u, 8u));
}

// A command for the console: the words joined, the ones with a space quoted.
std::string DisplayLine(const std::vector<std::string>& argv) {
    std::string line = "$";
    for (const auto& word : argv) {
        line += ' ';
        if (word.find(' ') != std::string::npos) line += "\"" + word + "\"";
        else line += word;
    }
    return line;
}

} // namespace

std::vector<std::vector<std::string>> BuildCommands(const std::string& folder,
                                                    const SystemProfile& profile) {
    const std::string cmake = FindProgram("cmake", CheckSearchDirectories(profile));
    if (cmake.empty()) return {};
    return {
        { cmake, "-S", folder, "--preset", "default" },
        { cmake, "--build", folder + "/build", "--parallel", ParallelJobs() },
    };
}

std::string BuildCommandsText(const std::string& folder) {
    return "cd \"" + folder + "\"\n"
           "cmake --preset default\n"
           "cmake --build --preset default\n";
}

BuildResult BuildProject(const std::string& folder, const SystemProfile& profile,
                         const std::function<void(const std::string&)>& onOutput) {
    BuildResult result;
    if (folder.empty()) {
        result.error = "No project folder was given.";
        return result;
    }
    const auto commands = BuildCommands(folder, profile);
    if (commands.empty()) {
        result.error = "cmake was not found; the Tools step installs it.";
        return result;
    }

    // The child's PATH: on Windows the MSYS2 bin directories first, so cmake
    // finds the compiler and the build tool the way the CLANG64 shell does.
    std::vector<std::pair<std::string, std::string>> environment;
    if (profile.platform == Platform::Windows && !profile.msysPrefix.empty()) {
        std::string path;
        for (const auto& directory : CheckSearchDirectories(profile)) path += directory + ";";
        if (const char* current = std::getenv("PATH")) path += current;
        environment.emplace_back("PATH", path);
    }

    for (size_t i = 0; i < commands.size(); ++i) {
        const auto& argv = commands[i];
        const std::string line = DisplayLine(argv) + "\n";
        result.output += line;
        if (onOutput) onOutput(line);
        const auto output = UltraCanvas::RunProcessCaptured(argv, {}, environment);
        result.started = output.started;
        result.exitCode = output.exitCode;
        if (!output.started) {
            result.error = output.error.empty() ? "cmake could not be started." : output.error;
            result.output += result.error + "\n";
            if (onOutput) onOutput(result.error + "\n");
            return result;
        }
        std::string text(output.standardOutput.begin(), output.standardOutput.end());
        if (!output.standardError.empty()) {
            if (!text.empty() && text.back() != '\n') text += '\n';
            text += output.standardError;
        }
        if (!text.empty() && text.back() != '\n') text += '\n';
        result.output += text;
        if (onOutput) onOutput(text);
        if (!output.Succeeded()) return result;
        if (i == 0) result.configured = true; else result.built = true;
    }
    return result;
}

} // namespace UltraCanvasStart
