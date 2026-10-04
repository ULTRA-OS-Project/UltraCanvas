// Apps/UltraCanvasStart/engine/StartTypes.h
// The vocabulary of UltraCanvasStart: which platform and package manager a
// machine has, what the programmer chose, what the checks found and the plan
// the two together produce. Plain data, no UI, shared by the GUI, the CLI
// modes and the test suite.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace UltraCanvasStart {

// ===== PLATFORM =====
enum class Platform {
    Linux,
    MacOS,
    Windows,
    Unknown
};

// The tool that installs packages on the machine. MSYS2's pacman is kept
// apart from Arch's: same program, different package names.
enum class PackageManager {
    Apt,          // Debian, Ubuntu, Mint, Pop!_OS
    Dnf,          // Fedora, RHEL 8+
    Pacman,       // Arch, Manjaro, EndeavourOS
    Zypper,       // openSUSE
    Homebrew,     // macOS
    Msys2Pacman,  // Windows, MSYS2 CLANG64 / CLANGARM64
    None
};

std::string PlatformName(Platform platform);
std::string PackageManagerName(PackageManager manager);
// The platform a package manager belongs to.
Platform PlatformOf(PackageManager manager);

// ===== THE MACHINE =====
struct SystemProfile {
    Platform platform = Platform::Unknown;
    std::string osName;              // "Ubuntu 24.04 LTS", "macOS 15.3", "Windows 11 Pro"
    std::string osVersion;
    std::string architecture;        // "x86_64", "arm64" (normalised, see NormalizeArchitecture)
    std::string distributionId;      // Linux: ID= from os-release ("ubuntu"), else empty
    std::string distributionLike;    // Linux: ID_LIKE= ("debian"), else empty
    PackageManager packageManager = PackageManager::None;
    std::string packageManagerPath;  // where the manager's program was found, or empty
    std::string msysPrefix;          // Windows: the MSYS2 root ("C:/msys64"), or empty
    std::string msystem;             // Windows: MSYSTEM from the environment, or empty
    std::string homeDirectory;       // UTF-8
};

// "x86_64" / "arm64" from whatever uname, the registry or the firmware said.
std::string NormalizeArchitecture(const std::string& raw);

// ===== DEPENDENCIES =====
// What a dependency is for, so the choices page can offer groups rather than
// forty package names.
enum class DependencyGroup {
    Toolchain,    // compiler, CMake, pkg-config, git
    Core,         // the framework itself cannot build without these
    Cdr,          // the CDR plug-in (libcdr, librevenge, boost, lcms2, icu)
    Pdf,          // the PDF plug-in (mupdf)
    Ocr,          // the OCR plug-in (tesseract, leptonica)
    Vectorizer,   // the Vectorizer plug-in (a Rust toolchain)
    Audio,        // audio codecs
    Barcode,      // zbar
    Net           // UltraNet / UltraCrypt extras (c-ares, libsodium)
};

std::string DependencyGroupTitle(DependencyGroup group);
std::string DependencyGroupDescription(DependencyGroup group);

// How a dependency's presence is checked on the machine.
enum class CheckKind {
    Tool,         // a program on PATH, asked for --version
    PkgConfig,    // a pkg-config module (pkg-config --modversion <module>)
    None          // no check possible; the package manager is the authority
};

// One thing the framework needs, with the package that provides it on each
// package manager. An empty package name means "not needed / not available
// through that manager".
struct Dependency {
    std::string id;                  // "cairo", "cmake", "libcdr"
    std::string title;               // what it is, one line
    DependencyGroup group = DependencyGroup::Core;
    CheckKind checkKind = CheckKind::None;
    std::string checkName;           // the tool or the pkg-config module
    std::string minimumVersion;      // "3.16" for CMake; empty when any version does
    std::string apt, dnf, pacman, zypper, brew, msys2;

    const std::string& PackageFor(PackageManager manager) const;
};

// ===== WHAT THE CHECKS FOUND =====
struct CheckResult {
    std::string dependencyId;
    std::string title;
    DependencyGroup group = DependencyGroup::Core;
    bool checked = false;            // false: no check kind, or the check could not run
    bool present = false;
    bool versionOk = true;           // false when present but below minimumVersion
    std::string version;             // what the tool or pkg-config reported
    std::string detail;              // why not, in words
    std::string packageName;         // the package that would provide it here
};

// ===== WHAT THE PROGRAMMER CHOSE =====
struct Choices {
    // The platform the instructions are for. Preselected to the detected one,
    // but the user can read any other platform's plan.
    Platform platform = Platform::Unknown;
    std::vector<DependencyGroup> groups = { DependencyGroup::Toolchain,
                                            DependencyGroup::Core };
    bool useSdk = true;              // the prebuilt SDK rather than building the framework
    bool cloneFramework = false;     // clone the repository next to the project
    bool useAi = true;               // the programmer works with Claude Code
    bool cloudOnly = false;          // ...and has no compiler locally (GettingStarted-Cloud.md)
    std::string projectFolder;       // UTF-8; where the application lives
    std::string appName = "MyApp";

    bool Has(DependencyGroup group) const;
    void Set(DependencyGroup group, bool on);
};

// ===== THE PLAN =====
enum class StepKind {
    Install,      // run the package manager
    Download,     // fetch the SDK
    Unpack,       // unpack the SDK
    Clone,        // git clone the framework
    Scaffold,     // write the project skeleton
    Manual        // something the user does (install Xcode, open MSYS2, sign in)
};

struct PlanStep {
    StepKind kind = StepKind::Manual;
    std::string title;
    std::string description;          // what it does and why, for the report
    std::vector<std::string> argv;    // the command, as a list; empty for Manual
    bool needsElevation = false;      // sudo / pkexec / an administrator shell
    bool done = false;
};

struct Plan {
    SystemProfile profile;
    Choices choices;
    std::vector<CheckResult> checks;
    std::vector<PlanStep> steps;
    std::vector<std::string> notes;   // things worth knowing that are not steps

    size_t MissingCount() const;
};

// ===== VERSIONS =====
// Dot-separated numeric comparison: "3.16" < "3.22.1"; letters stop the parse.
// Returns <0, 0, >0.
int CompareVersions(const std::string& a, const std::string& b);

// The first thing in `text` that looks like a version number ("3.28.3" in
// "cmake version 3.28.3"), or empty.
std::string ExtractVersion(const std::string& text);

} // namespace UltraCanvasStart
