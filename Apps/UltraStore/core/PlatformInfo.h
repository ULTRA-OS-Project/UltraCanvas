// Apps/UltraStore/core/PlatformInfo.h
// What machine the store runs on: its target (os / arch / variants), the OS
// version, the Linux distribution, and whether x86_64 packages run here
// through Rosetta. Pure helpers beside Detect() so each platform's rule is
// testable on any platform.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include "UltraStoreTypes.h"

#include <map>
#include <string>

namespace UltraStore {

struct PlatformInfo {
    Target target;
    std::string osVersion;       // "14.5", "10.0.22631", "6.8.0"
    std::string distroId;        // /etc/os-release ID ("ubuntu"), empty elsewhere
    bool canRunX86_64 = false;   // Rosetta present (macOS on Apple silicon)

    // This machine. Never fails: what cannot be learned stays empty.
    static PlatformInfo Detect();

    // "linux/x86_64 (glibc-2.39) - Ubuntu 24.04", for the Settings page and
    // the CLI's `platform` command.
    std::string Describe() const;

    // Whether this machine satisfies a package's variant: "glibc-2.31" when
    // the machine's glibc is at least that, "rpi" when it is a Raspberry Pi,
    // "min-os=11.0" when osVersion is at least that, a distribution id when
    // it is this one; an unknown variant is not satisfied.
    bool Satisfies(const std::string& variant) const;

    // Whether osVersion is at least a package's min_os ("14.0" on macOS,
    // "10.0.19041" on Windows, the kernel on Linux, where the glibc variant
    // is the usual bar); an empty minimum is always met, an unknown version
    // never is.
    bool MeetsMinOs(const std::string& minOs) const;
};

// The key=value pairs of an /etc/os-release text, quotes removed.
std::map<std::string, std::string> ParseOsRelease(const std::string& text);

// "x86_64" / "arm64" / "armv7" / "riscv64" from what uname, the registry or
// the compiler said ("amd64", "aarch64", "armv7l", "AMD64", ...).
Arch NormalizeArch(const std::string& raw);

// The glibc version of this process ("2.39"), or "" where there is none.
std::string GlibcVersion();

} // namespace UltraStore
