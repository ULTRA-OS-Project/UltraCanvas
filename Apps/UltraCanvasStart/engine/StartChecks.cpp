// Apps/UltraCanvasStart/engine/StartChecks.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StartChecks.h"

#include "StartPackages.h"
#include "StartSystem.h"

#include "UltraCanvasUtils.h"

#include <cstdlib>

namespace UltraCanvasStart {

namespace {

std::string FirstLine(const std::vector<unsigned char>& bytes) {
    std::string text(bytes.begin(), bytes.end());
    const size_t newline = text.find('\n');
    if (newline != std::string::npos) text.resize(newline);
    if (!text.empty() && text.back() == '\r') text.pop_back();
    return text;
}

} // namespace

CheckResult CheckTool(const std::string& tool, const std::string& minimumVersion,
                      const std::vector<std::string>& searchDirectories) {
    CheckResult result;
    result.checked = true;
    const std::string program = FindProgram(tool, searchDirectories);
    if (program.empty()) {
        result.detail = tool + " is not on PATH";
        return result;
    }
    const auto output = UltraCanvas::RunProcessCaptured({ program, "--version" }, {});
    if (!output.started) {
        result.detail = "found " + program + " but it could not be run: " + output.error;
        return result;
    }
    result.present = true;
    std::string line = FirstLine(output.standardOutput);
    if (line.empty()) line = FirstLine(std::vector<unsigned char>(
        output.standardError.begin(), output.standardError.end()));
    result.version = ExtractVersion(line);
    result.detail = program;
    if (!minimumVersion.empty() && !result.version.empty() &&
        CompareVersions(result.version, minimumVersion) < 0) {
        result.versionOk = false;
        result.detail = result.version + " is older than the " + minimumVersion + " needed";
    }
    return result;
}

CheckResult CheckPkgConfigModule(const std::string& module, const std::string& pkgConfig) {
    CheckResult result;
    std::string program = pkgConfig;
    if (program.empty()) program = FindProgram("pkg-config");
    if (program.empty()) program = FindProgram("pkgconf");
    if (program.empty()) {
        result.checked = false;
        result.detail = "pkg-config is not installed, so the library cannot be checked";
        return result;
    }
    result.checked = true;
    const auto output = UltraCanvas::RunProcessCaptured(
        { program, "--modversion", module }, {});
    if (!output.started) {
        result.checked = false;
        result.detail = "pkg-config could not be run: " + output.error;
        return result;
    }
    if (output.exitCode != 0) {
        result.detail = "pkg-config knows no module named " + module;
        return result;
    }
    result.present = true;
    result.version = FirstLine(output.standardOutput);
    result.detail = module + " " + result.version;
    return result;
}

std::vector<std::string> CheckSearchDirectories(const SystemProfile& profile) {
    std::vector<std::string> directories;
    if (profile.platform == Platform::Windows) {
        if (!profile.msysPrefix.empty()) {
            directories.push_back(profile.msysPrefix +
                                  (profile.architecture == "arm64" ? "/clangarm64/bin" : "/clang64/bin"));
            directories.push_back(profile.msysPrefix + "/usr/bin");
        }
        // Git for Windows is the usual git on a Windows machine, and it is
        // not on PATH inside an MSYS2 shell (the CI runner's has none of its
        // own); the toolchain check accepts it where it is installed.
        for (const char* variable : { "ProgramFiles", "ProgramW6432", "LOCALAPPDATA" }) {
            if (const char* base = std::getenv(variable)) {
                directories.push_back(std::string(base) + (std::string(variable) == "LOCALAPPDATA"
                                                           ? "/Programs/Git/cmd" : "/Git/cmd"));
            }
        }
    } else if (profile.platform == Platform::MacOS) {
        directories.push_back("/opt/homebrew/bin");
        directories.push_back("/usr/local/bin");
    }
    return directories;
}

std::vector<CheckResult> RunChecks(const SystemProfile& profile, const Choices& choices,
                                   const std::function<void(const std::string&)>& progress) {
    std::vector<CheckResult> results;
    const auto directories = CheckSearchDirectories(profile);
    std::string pkgConfig = FindProgram("pkg-config", directories);
    if (pkgConfig.empty()) pkgConfig = FindProgram("pkgconf", directories);

    for (const auto* dependency : DependenciesFor(choices, profile.packageManager)) {
        if (progress) progress(dependency->title);
        CheckResult result;
        switch (dependency->checkKind) {
            case CheckKind::Tool:
                result = CheckTool(dependency->checkName, dependency->minimumVersion, directories);
                break;
            case CheckKind::PkgConfig:
                result = CheckPkgConfigModule(dependency->checkName, pkgConfig);
                break;
            case CheckKind::None:
                result.checked = false;
                result.detail = "no check; the package manager decides";
                break;
        }
        result.dependencyId = dependency->id;
        result.title = dependency->title;
        result.group = dependency->group;
        result.packageName = dependency->PackageFor(profile.packageManager);
        if (profile.packageManager == PackageManager::Msys2Pacman) {
            result.packageName = Msys2PackageForArchitecture(result.packageName,
                                                             profile.architecture);
        }
        results.push_back(std::move(result));
    }
    return results;
}

} // namespace UltraCanvasStart
