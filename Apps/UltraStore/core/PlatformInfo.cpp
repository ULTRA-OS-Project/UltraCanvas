// Apps/UltraStore/core/PlatformInfo.cpp
// Platform detection (PlatformInfo.h).
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "PlatformInfo.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/utsname.h>
#else
#include <sys/utsname.h>
#if defined(__GLIBC__)
#include <gnu/libc-version.h>
#endif
#endif

namespace UltraStore {

namespace {

std::string ReadTextFile(const char* path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::string Trim(const std::string& s) {
    const size_t first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const size_t last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string Unquote(std::string value) {
    value = Trim(value);
    if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') &&
        value.back() == value.front()) {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

// "11.2" >= "11.0": numeric, part by part.
bool VersionAtLeast(const std::string& have, const std::string& want) {
    if (have.empty()) return false;
    return CompareVersions(have, want) >= 0;
}

#if defined(__APPLE__)
std::string MacOSProductVersion() {
    // The plist is XML: <key>ProductVersion</key><string>14.5</string>.
    const std::string plist = ReadTextFile("/System/Library/CoreServices/SystemVersion.plist");
    const size_t key = plist.find("<key>ProductVersion</key>");
    if (key == std::string::npos) return {};
    const size_t open = plist.find("<string>", key);
    const size_t close = plist.find("</string>", open);
    if (open == std::string::npos || close == std::string::npos) return {};
    return Trim(plist.substr(open + 8, close - open - 8));
}
#endif

} // namespace

std::map<std::string, std::string> ParseOsRelease(const std::string& text) {
    std::map<std::string, std::string> values;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == '#') continue;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Trim(line.substr(0, eq));
        if (key.empty() || key.find(' ') != std::string::npos) continue;
        values[key] = Unquote(line.substr(eq + 1));
    }
    return values;
}

Arch NormalizeArch(const std::string& raw) {
    std::string t;
    for (char c : raw) t.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    if (t == "x86_64" || t == "amd64" || t == "x64")               return Arch::X86_64;
    if (t == "arm64" || t == "aarch64" || t == "arm64e")           return Arch::Arm64;
    if (t.rfind("armv7", 0) == 0 || t == "armhf" || t == "arm")    return Arch::Armv7;
    if (t == "riscv64")                                            return Arch::Riscv64;
    return Arch::Unknown;
}

std::string GlibcVersion() {
#if defined(__GLIBC__) && !defined(__APPLE__) && !defined(_WIN32)
    const char* v = gnu_get_libc_version();
    return v ? v : "";
#else
    return {};
#endif
}

PlatformInfo PlatformInfo::Detect() {
    PlatformInfo info;
#if defined(_WIN32)
    info.target.os = OS::Windows;
    SYSTEM_INFO si{};
    GetNativeSystemInfo(&si);
    switch (si.wProcessorArchitecture) {
        case PROCESSOR_ARCHITECTURE_AMD64: info.target.arch = Arch::X86_64; break;
        case PROCESSOR_ARCHITECTURE_ARM64: info.target.arch = Arch::Arm64;  break;
        default: info.target.arch = Arch::Unknown; break;
    }
    // RtlGetVersion, not GetVersionEx: the latter lies to a program without
    // a compatibility manifest.
    using RtlGetVersionFn = LONG (WINAPI*)(OSVERSIONINFOW*);
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        auto fn = reinterpret_cast<RtlGetVersionFn>(
            reinterpret_cast<void*>(GetProcAddress(ntdll, "RtlGetVersion")));
        OSVERSIONINFOW v{};
        v.dwOSVersionInfoSize = sizeof(v);
        if (fn && fn(&v) == 0) {
            info.osVersion = std::to_string(v.dwMajorVersion) + "." +
                             std::to_string(v.dwMinorVersion) + "." +
                             std::to_string(v.dwBuildNumber);
        }
    }
#elif defined(__APPLE__)
    info.target.os = OS::MacOS;
    struct utsname u{};
    if (::uname(&u) == 0) info.target.arch = NormalizeArch(u.machine);
    info.osVersion = MacOSProductVersion();
    if (info.target.arch == Arch::Arm64) {
        // Rosetta 2 installs this runtime; without it an x86_64 app does not start.
        struct stat st{};
        info.canRunX86_64 = ::stat("/Library/Apple/usr/share/rosetta/rosetta", &st) == 0;
    } else if (info.target.arch == Arch::X86_64) {
        info.canRunX86_64 = true;
    }
#else
    info.target.os = OS::Linux;
    struct utsname u{};
    if (::uname(&u) == 0) {
        info.target.arch = NormalizeArch(u.machine);
        info.osVersion = u.release;
    }
    const auto release = ParseOsRelease(ReadTextFile("/etc/os-release"));
    if (auto it = release.find("ID"); it != release.end()) info.distroId = it->second;
    if (const std::string glibc = GlibcVersion(); !glibc.empty()) {
        info.target.variants.push_back("glibc-" + glibc);
    }
    const std::string model = ReadTextFile("/proc/device-tree/model");
    if (model.find("Raspberry Pi") != std::string::npos) info.target.variants.push_back("rpi");
    if (info.target.arch == Arch::X86_64) info.canRunX86_64 = true;
#endif
    if (info.target.arch == Arch::X86_64) info.canRunX86_64 = true;
    return info;
}

std::string PlatformInfo::Describe() const {
    std::string out = target.Describe();
    if (!osVersion.empty()) out += " - " + OSName(target.os) + " " + osVersion;
    if (!distroId.empty()) out += " (" + distroId + ")";
    if (canRunX86_64 && target.arch != Arch::X86_64) out += ", runs x86_64 through Rosetta";
    return out;
}

bool PlatformInfo::MeetsMinOs(const std::string& minOs) const {
    if (minOs.empty()) return true;
    if (osVersion.empty()) return false;
    return VersionAtLeast(osVersion, minOs);
}

bool PlatformInfo::Satisfies(const std::string& variant) const {
    if (variant.rfind("glibc-", 0) == 0) {
        for (const std::string& have : target.variants) {
            if (have.rfind("glibc-", 0) == 0) {
                return VersionAtLeast(have.substr(6), variant.substr(6));
            }
        }
        return false;
    }
    if (variant.rfind("min-os=", 0) == 0) return VersionAtLeast(osVersion, variant.substr(7));
    if (variant == "rpi") {
        for (const std::string& have : target.variants) if (have == "rpi") return true;
        return false;
    }
    if (!distroId.empty() && variant == distroId) return true;
    for (const std::string& have : target.variants) if (have == variant) return true;
    return false;
}

} // namespace UltraStore
