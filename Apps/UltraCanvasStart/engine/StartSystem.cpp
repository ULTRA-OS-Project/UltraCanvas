// Apps/UltraCanvasStart/engine/StartSystem.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StartSystem.h"

#include "UltraCanvasHardwareInfo.h"
#include "UltraCanvasPathUtf8.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace UltraCanvasStart {

Platform CurrentPlatform() {
#if defined(_WIN32)
    return Platform::Windows;
#elif defined(__APPLE__)
    return Platform::MacOS;
#elif defined(__linux__)
    return Platform::Linux;
#else
    return Platform::Unknown;
#endif
}

std::map<std::string, std::string> ParseOsRelease(const std::string& text) {
    std::map<std::string, std::string> values;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string value = line.substr(eq + 1);
        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') &&
            value.back() == value.front()) {
            value = value.substr(1, value.size() - 2);
        }
        values[key] = value;
    }
    return values;
}

PackageManager PackageManagerForDistribution(const std::string& id,
                                             const std::string& idLike) {
    const std::string both = id + " " + idLike;
    auto has = [&](const char* word) {
        return both.find(word) != std::string::npos;
    };
    if (has("debian") || has("ubuntu")) return PackageManager::Apt;
    if (has("fedora") || has("rhel") || has("centos")) return PackageManager::Dnf;
    if (has("arch") || has("manjaro")) return PackageManager::Pacman;
    if (has("suse")) return PackageManager::Zypper;
    return PackageManager::None;
}

std::string PackageManagerProgram(PackageManager manager) {
    switch (manager) {
        case PackageManager::Apt:         return "apt-get";
        case PackageManager::Dnf:         return "dnf";
        case PackageManager::Pacman:      return "pacman";
        case PackageManager::Zypper:      return "zypper";
        case PackageManager::Homebrew:    return "brew";
        case PackageManager::Msys2Pacman: return "pacman";
        default:                          return "";
    }
}

std::string FindProgram(const std::string& name,
                        const std::vector<std::string>& extraDirectories) {
    std::vector<std::string> directories = extraDirectories;
    if (const char* path = std::getenv("PATH")) {
#if defined(_WIN32)
        const char separator = ';';
#else
        const char separator = ':';
#endif
        std::string entry;
        std::istringstream in(path);
        while (std::getline(in, entry, separator)) {
            if (!entry.empty()) directories.push_back(entry);
        }
    }
    std::vector<std::string> names = { name };
#if defined(_WIN32)
    names.push_back(name + ".exe");
#endif
    std::error_code ec;
    for (const auto& directory : directories) {
        for (const auto& candidate : names) {
            const fs::path full = UltraCanvas::PathFromUtf8(directory) /
                                  UltraCanvas::PathFromUtf8(candidate);
            if (fs::is_regular_file(full, ec)) return UltraCanvas::PathToUtf8(full);
        }
    }
    return {};
}

std::string FindMsys2Root() {
#if defined(_WIN32)
    std::vector<std::string> candidates;
    if (const char* root = std::getenv("MSYS2_ROOT")) candidates.push_back(root);
    // An MSYSTEM shell exports MSYSTEM_PREFIX ("/clang64"), which is of no use
    // natively, but WD points at the usr/bin of the installation it runs from.
    if (const char* wd = std::getenv("WD")) {
        const fs::path usrBin = UltraCanvas::PathFromUtf8(wd);
        if (usrBin.has_parent_path() && usrBin.parent_path().has_parent_path()) {
            candidates.push_back(UltraCanvas::PathToUtf8(usrBin.parent_path().parent_path()));
        }
    }
    candidates.push_back("C:/msys64");
    candidates.push_back("C:/msys2");
    if (const char* programs = std::getenv("ProgramFiles")) {
        candidates.push_back(std::string(programs) + "/msys64");
    }
    std::error_code ec;
    for (const auto& candidate : candidates) {
        const fs::path root = UltraCanvas::PathFromUtf8(candidate);
        if (fs::is_regular_file(root / "usr" / "bin" / "pacman.exe", ec)) {
            return UltraCanvas::PathToUtf8(root);
        }
    }
#endif
    return {};
}

SystemProfile DetectSystem() {
    SystemProfile profile;
    profile.platform = CurrentPlatform();

    const auto snapshot = UltraCanvas::UltraCanvasHardwareInfo::Capture(
        UltraCanvas::HardwareQuery::System);
    profile.osName       = snapshot.system.osName;
    profile.osVersion    = snapshot.system.osVersion;
    profile.architecture = NormalizeArchitecture(snapshot.system.architecture);

#if defined(_WIN32)
    if (const char* home = std::getenv("USERPROFILE")) profile.homeDirectory = home;
    if (const char* msystem = std::getenv("MSYSTEM")) profile.msystem = msystem;
    profile.msysPrefix = FindMsys2Root();
    if (!profile.msysPrefix.empty()) {
        profile.packageManager = PackageManager::Msys2Pacman;
        profile.packageManagerPath = profile.msysPrefix + "/usr/bin/pacman.exe";
    }
#else
    if (const char* home = std::getenv("HOME")) profile.homeDirectory = home;
#endif

    if (profile.platform == Platform::Linux) {
        std::error_code ec;
        for (const char* file : { "/etc/os-release", "/usr/lib/os-release" }) {
            if (!fs::exists(file, ec)) continue;
            std::ifstream in(file);
            std::stringstream buffer;
            buffer << in.rdbuf();
            const auto values = ParseOsRelease(buffer.str());
            auto get = [&](const char* key) {
                auto found = values.find(key);
                return found == values.end() ? std::string() : found->second;
            };
            profile.distributionId   = get("ID");
            profile.distributionLike = get("ID_LIKE");
            if (profile.osName.empty()) profile.osName = get("PRETTY_NAME");
            if (profile.osVersion.empty()) profile.osVersion = get("VERSION_ID");
            break;
        }
        profile.packageManager = PackageManagerForDistribution(
            profile.distributionId, profile.distributionLike);
        if (profile.packageManager == PackageManager::None) {
            // Not a known family: take whichever manager is on PATH.
            for (auto manager : { PackageManager::Apt, PackageManager::Dnf,
                                  PackageManager::Pacman, PackageManager::Zypper }) {
                if (!FindProgram(PackageManagerProgram(manager)).empty()) {
                    profile.packageManager = manager;
                    break;
                }
            }
        }
    } else if (profile.platform == Platform::MacOS) {
        profile.packageManager = PackageManager::Homebrew;
    }

    if (profile.packageManagerPath.empty() &&
        profile.packageManager != PackageManager::None) {
        profile.packageManagerPath = FindProgram(
            PackageManagerProgram(profile.packageManager),
            { "/opt/homebrew/bin", "/usr/local/bin" });
    }
    return profile;
}

} // namespace UltraCanvasStart
