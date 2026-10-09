// Apps/UltraCanvasStart/engine/StartPlan.cpp
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "StartPlan.h"

#include "StartPackages.h"
#include "StartProject.h"
#include "StartSdk.h"
#include "StartSystem.h"

#include <algorithm>
#include <map>

namespace UltraCanvasStart {

namespace {

PackageManager DefaultManagerFor(Platform platform) {
    switch (platform) {
        case Platform::Linux:   return PackageManager::Apt;
        case Platform::MacOS:   return PackageManager::Homebrew;
        case Platform::Windows: return PackageManager::Msys2Pacman;
        default:                return PackageManager::None;
    }
}

} // namespace

std::vector<std::string> PlatformNotes(const SystemProfile& profile, Platform target) {
    std::vector<std::string> notes;
    const bool local = target == profile.platform;
    switch (target) {
        case Platform::MacOS:
            notes.push_back("Install the Xcode command line tools first: xcode-select --install");
            if (!local || profile.packageManagerPath.empty()) {
                notes.push_back("Homebrew installs the libraries: https://brew.sh (one command in Terminal)");
            }
            notes.push_back("Build with cmake -G Xcode or the default generator; package with package-macos.sh");
            break;
        case Platform::Windows:
            notes.push_back("The build runs inside MSYS2 (https://www.msys2.org), in the CLANG64 shell "
                            "(CLANGARM64 on an ARM machine), not in Visual Studio");
            if (local && profile.msysPrefix.empty()) {
                notes.push_back("MSYS2 was not found; install it to C:\\msys64, open the CLANG64 shell, "
                                "run pacman -Syu and start UltraCanvasStart again");
            }
            notes.push_back("build-win.cmd at the repository root configures and builds; "
                            "package-win.sh makes the standalone zip");
            break;
        case Platform::Linux:
            if (local && profile.packageManager == PackageManager::None) {
                notes.push_back("No apt, dnf, pacman or zypper was found; install the packages "
                                "named below with your distribution's tool");
            }
            notes.push_back("A C++20 compiler: clang 14+ or GCC 11+. CI builds with clang.");
            break;
        default:
            break;
    }
    return notes;
}

Plan BuildPlan(const SystemProfile& profile, const Choices& choices,
               const std::vector<CheckResult>& checks) {
    Plan plan;
    plan.profile = profile;
    plan.choices = choices;
    plan.checks = checks;

    const Platform target = choices.platform == Platform::Unknown ? profile.platform
                                                                  : choices.platform;
    const bool local = target == profile.platform;
    PackageManager manager = local ? profile.packageManager : DefaultManagerFor(target);
    if (local && manager == PackageManager::None) manager = DefaultManagerFor(target);

    plan.notes = PlatformNotes(profile, target);

    // ---- packages -----------------------------------------------------------
    std::vector<std::string> packages;
    if (local && !checks.empty()) {
        // Only what the checks say is missing, plus the ones that cannot be
        // checked (the manager skips installed ones anyway).
        for (const auto& check : checks) {
            if (check.packageName.empty()) continue;
            const bool wanted = !check.checked || !check.present || !check.versionOk;
            if (wanted && std::find(packages.begin(), packages.end(), check.packageName) == packages.end()) {
                packages.push_back(check.packageName);
            }
        }
    } else {
        packages = PackageNames(DependenciesFor(choices, manager), manager);
        if (manager == PackageManager::Msys2Pacman) {
            const std::string arch = local ? profile.architecture : "x86_64";
            for (auto& p : packages) p = Msys2PackageForArchitecture(p, arch);
        }
    }
    if (!packages.empty()) {
        std::string program;
        if (local) program = profile.packageManagerPath;
        PlanStep install = InstallStep(manager, packages, program);
        if (manager == PackageManager::Msys2Pacman) {
            install.description += " (run in the MSYS2 CLANG64 shell)";
        }
        plan.steps.push_back(install);
    }

    // ---- the framework ------------------------------------------------------
    const std::string version = FrameworkVersion();
    const std::string arch = local ? profile.architecture : "x86_64";
    if (choices.useSdk) {
        PlanStep download;
        download.kind = StepKind::Download;
        download.title = "Get the UltraCanvas SDK " + SdkArtifactName(target, version, arch);
        download.description = "The framework prebuilt for " + PlatformName(target) + " " + arch +
                               ": " + SdkReleaseAssetUrl(target, version, arch) +
                               " - the Project page's Download button fetches and unpacks it; "
                               "while that release is still building, the archive of the same name "
                               "is a workflow artifact at " + SdkDownloadPage();
        plan.steps.push_back(download);
    }
    if (choices.cloneFramework || !choices.useSdk) {
        std::string destination = choices.projectFolder.empty() ? "UltraCanvas"
                                                                : choices.projectFolder + "/../UltraCanvas";
        plan.steps.push_back(CloneStep(destination));
    }

    // ---- the project --------------------------------------------------------
    {
        PlanStep scaffold;
        scaffold.kind = StepKind::Scaffold;
        scaffold.title = "Create the " + IdentifierFrom(choices.appName) + " project";
        scaffold.description = "CMakeLists.txt, main.cpp, CMakePresets.json, README.md" +
                               std::string(choices.useAi ? " and CLAUDE.md" : "") + " in " +
                               (choices.projectFolder.empty() ? std::string("the chosen folder")
                                                              : choices.projectFolder);
        plan.steps.push_back(scaffold);
    }

    // ---- the assistant ------------------------------------------------------
    if (choices.useAi) {
        PlanStep ai;
        ai.kind = StepKind::Manual;
        ai.title = choices.cloudOnly ? "Connect Claude Code to the repository on GitHub"
                                     : "Install Claude Code and open the project with it";
        ai.description = choices.cloudOnly
            ? "No compiler here: GitHub Actions builds. Follow Docs/GettingStarted-Cloud.md; "
              "the AI page has the checklist."
            : "The AI page has the install command and the first prompt to give it.";
        plan.steps.push_back(ai);
    }
    return plan;
}

std::string DisplayCommand(const std::vector<std::string>& argv, bool elevated) {
    std::string line = elevated ? "sudo " : "";
    for (size_t i = 0; i < argv.size(); ++i) {
        if (i) line += ' ';
        const bool quote = argv[i].find(' ') != std::string::npos;
        if (quote) line += '"';
        line += argv[i];
        if (quote) line += '"';
    }
    return line;
}

std::string RenderReport(const Plan& plan) {
    const auto& p = plan.profile;
    std::string out;
    out += "UltraCanvasStart report\n";
    out += "=======================\n\n";
    out += "System\n";
    out += "  Platform:         " + PlatformName(p.platform) + "\n";
    out += "  OS:               " + p.osName + (p.osVersion.empty() ? "" : " " + p.osVersion) + "\n";
    out += "  Architecture:     " + p.architecture + "\n";
    if (!p.distributionId.empty()) out += "  Distribution:     " + p.distributionId + "\n";
    out += "  Package manager:  " + PackageManagerName(p.packageManager) +
           (p.packageManagerPath.empty() ? "" : " (" + p.packageManagerPath + ")") + "\n";
    if (!p.msysPrefix.empty()) out += "  MSYS2:            " + p.msysPrefix + "\n";
    const Platform target = plan.choices.platform == Platform::Unknown ? p.platform : plan.choices.platform;
    out += "  Instructions for: " + PlatformName(target) + "\n\n";

    out += "Choices\n";
    out += "  Features: ";
    for (size_t i = 0; i < plan.choices.groups.size(); ++i) {
        if (i) out += ", ";
        out += DependencyGroupTitle(plan.choices.groups[i]);
    }
    out += "\n";
    out += std::string("  Framework: ") + (plan.choices.useSdk ? "prebuilt SDK" : "built from source") +
           (plan.choices.cloneFramework ? ", repository cloned" : "") + "\n";
    out += std::string("  AI assistant: ") + (plan.choices.useAi ? (plan.choices.cloudOnly ? "Claude Code via GitHub, no local compiler" : "Claude Code locally") : "no") + "\n";
    out += "  Application: " + plan.choices.appName +
           (plan.choices.projectFolder.empty() ? "" : " in " + plan.choices.projectFolder) + "\n\n";

    if (!plan.checks.empty()) {
        out += "Checks (" + std::to_string(plan.MissingCount()) + " missing)\n";
        std::map<DependencyGroup, std::vector<const CheckResult*>> byGroup;
        for (const auto& c : plan.checks) byGroup[c.group].push_back(&c);
        for (const auto& [group, list] : byGroup) {
            out += "  " + DependencyGroupTitle(group) + "\n";
            for (const auto* c : list) {
                const char* mark = !c->checked ? "?" : (c->present && c->versionOk ? "+" : "-");
                std::string line = std::string("    [") + mark + "] " + c->title;
                if (!c->version.empty()) line += "  " + c->version;
                if (!c->present || !c->versionOk || !c->checked) line += "  (" + c->detail + ")";
                if (!c->packageName.empty() && (!c->present || !c->versionOk)) line += "  -> " + c->packageName;
                out += line + "\n";
            }
        }
        out += "\n";
    }

    out += "Plan\n";
    int n = 0;
    for (const auto& step : plan.steps) {
        out += "  " + std::to_string(++n) + ". " + step.title + (step.done ? "  [done]" : "") + "\n";
        out += "     " + step.description + "\n";
        if (!step.argv.empty()) out += "     $ " + DisplayCommand(step.argv, step.needsElevation) + "\n";
    }
    if (!plan.notes.empty()) {
        out += "\nNotes\n";
        for (const auto& note : plan.notes) out += "  * " + note + "\n";
    }
    return out;
}

} // namespace UltraCanvasStart
