// Apps/UltraCanvasStart/main.cpp
// UltraCanvasStart — sets a computer up for writing UltraCanvas applications.
// It finds out what the machine has, installs the development packages that
// are missing, points at the prebuilt SDK, writes a project skeleton and
// explains how to work with an AI assistant on it, locally or through GitHub.
//
// Without arguments it opens the window on step 1, what was found, with the
// guide for any platform under it - the same choice Docs/GettingStarted.md
// offers, so a programmer can read another platform's instructions too. With --check
// or --plan it runs headless, which is what CI uses to smoke-test it, and
// --create writes the project skeleton headless, which CI builds against
// the SDK it just installed, so the skeleton is known to compile.
// Version: 0.3.0 - --update-check, --update-sdk <folder>, --update-project <folder>:
//                  the newest SDK on the server (pub/LATEST), headless
// Version: 0.2.3 - --assistant none: no AI assistant, no instruction file
// Version: 0.2.2 - --step reaches 6 (Build); --page build
// Version: 0.2.1 - --create <folder> (--name, --sdk) writes the skeleton headless
// Version: 0.2.0 - --step <n>; --page maps the old names to steps
// Version: 0.1.2 - --assistant <ai> chooses the assistant
// Version: 0.1.1 - --page <name> opens the window on a page
// Author: UltraCanvas Framework / ULTRA OS
#include "ui/UltraCanvasStartWindow.h"

#include "StartChecks.h"
#include "StartPlan.h"
#include "StartProject.h"
#include "StartRunner.h"
#include "StartSdk.h"
#include "StartSystem.h"
#include "StartTypes.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasUtils.h"

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

#ifdef __linux__
#include <X11/Xlib.h>
#include <csignal>
#endif

// ULTRACANVASSTART_VERSION comes from the build alone: CMake reads the first
// line of Docs/UltraCanvasStart/CHANGELOG.md (cmake/UltraCanvasVersion.cmake).
#ifndef ULTRACANVASSTART_VERSION
#error "ULTRACANVASSTART_VERSION is not defined: build through CMake, which reads it from Docs/UltraCanvasStart/CHANGELOG.md"
#endif

using namespace UltraCanvas;

namespace {

#ifdef __linux__
void SignalHandler(int) {
    UltraCanvasApplicationBase::RequestExitFromSignal();
}
#endif

void PrintUsage(const char* programName) {
    std::printf(
        "UltraCanvasStart - sets this computer up for UltraCanvas development\n"
        "Powered by the UltraCanvas framework\n"
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "  (no options)      Open the UltraCanvasStart window\n"
        "  --check           Detect the system, check the tools and libraries\n"
        "                    the chosen features need, and print the report\n"
        "  --plan            Like --check, and also print the install plan\n"
        "  --for <os>        Plan for linux, macos or windows instead of this one\n"
        "                    (in the window: preselect that platform's guide)\n"
        "  --all             Check every feature group, not only the toolchain\n"
        "                    and the framework core\n"
        "  --step <n>        Open the window on step 1 to 6 (your computer, features,\n"
        "                    tools, framework, project, build)\n"
        "  --page <name>     The page names before 0.3.0 (platform, system, choices,\n"
        "                    install, project, ai) and build, mapped to their step;\n"
        "                    guide and report open those dialogs\n"
        "  --assistant <ai>  none, claude, codex, copilot, gemini or other: the AI assistant\n"
        "                    the plan and the project are for (default claude)\n"
        "  --create <folder> Write the project skeleton into the folder (as the\n"
        "                    Project step does) and exit; no window\n"
        "  --name <app>      The application's name for --create (default MyApp)\n"
        "  --sdk <prefix>    The SDK prefix --create points the skeleton at; with\n"
        "                    --update-check and --update-sdk the SDK installed now,\n"
        "                    with --update-project the SDK to move the project to\n"
        "  --update-check    Ask the server for the newest SDK and compare it with\n"
        "                    --sdk's version, or with this build's\n"
        "  --update-sdk <folder>\n"
        "                    Download the newest SDK into the folder, check it and\n"
        "                    unpack it there, when it is newer; print its prefix\n"
        "  --update-project <folder>\n"
        "                    Point the project in the folder at the SDK --sdk names\n"
        "                    (its CMakePresets.json; the CMake cache is removed)\n"
        "  -v, --version     Show version information\n"
        "  -h, --help        Show this message\n"
        "\n"
        "Exit status with --check: 0 when nothing is missing, 2 when something is.\n"
        "Exit status with --create: 0 when the files were written, 1 otherwise.\n"
        "Exit status with --update-check: 0 when nothing newer is there, 3 when a\n"
        "newer SDK can be downloaded, 1 when the server could not be asked.\n",
        programName);
}

int RunHeadless(bool withPlan, const UltraCanvasStart::Choices& choicesIn) {
    using namespace UltraCanvasStart;
    const SystemProfile profile = DetectSystem();
    Choices choices = choicesIn;
    if (choices.platform == Platform::Unknown) choices.platform = profile.platform;

    std::vector<CheckResult> checks;
    if (choices.platform == profile.platform) {
        checks = RunChecks(profile, choices);
    }
    const Plan plan = BuildPlan(profile, choices, checks);
    std::string report = RenderReport(plan);
    if (!withPlan) {
        // --check stops before the plan section.
        const size_t planAt = report.find("\nPlan\n");
        if (planAt != std::string::npos) report.resize(planAt + 1);
    }
    std::fputs(report.c_str(), stdout);
    return plan.MissingCount() == 0 ? EXIT_SUCCESS : 2;
}

// --create: the Project step's files, written without a window. The
// folder must be empty of a CMakeLists.txt, as in the window.
int RunCreate(const UltraCanvasStart::ProjectOptions& options) {
    const UltraCanvasStart::ProjectResult result = UltraCanvasStart::ScaffoldProject(options);
    if (!result.ok) {
        std::fprintf(stderr, "%s\n", result.error.c_str());
        return EXIT_FAILURE;
    }
    for (const auto& path : result.written) std::printf("wrote %s\n", path.c_str());
    return EXIT_SUCCESS;
}

// The version the update is measured from: the SDK at --sdk when it says,
// otherwise the framework this build was made from.
std::string InstalledSdkVersion(const std::string& prefix) {
    const std::string fromPrefix = UltraCanvasStart::SdkPrefixVersion(prefix);
    return fromPrefix.empty() ? UltraCanvasStart::FrameworkVersion() : fromPrefix;
}

void PrintUpdate(const UltraCanvasStart::SdkUpdateCheck& check,
                 const UltraCanvasStart::SystemProfile& profile) {
    using namespace UltraCanvasStart;
    std::printf("Installed: %s\n", check.installed.c_str());
    std::printf("Newest:    %s (%s)\n", check.latest.c_str(), SdkLatestUrl().c_str());
    if (!check.newer) {
        std::printf("The SDK is up to date.\n");
    } else if (!check.archiveListed) {
        std::printf("%s has no %s yet.\n", check.latest.c_str(), check.archive.c_str());
    } else {
        std::printf("Newer SDK: %s\n", SdkDownloadUrl(profile.platform, check.latest, profile.architecture).c_str());
        std::printf("Matching UltraCanvasStart: %s\n",
                    StartAppDownloadUrl(profile.platform, check.latest, profile.architecture).c_str());
    }
}

// --update-check: exit 0 up to date, 3 newer, 1 not asked.
int RunUpdateCheck(const std::string& prefix) {
    using namespace UltraCanvasStart;
    const SystemProfile profile = DetectSystem();
    const SdkUpdateCheck check = CheckSdkUpdate(profile.platform, InstalledSdkVersion(prefix), profile.architecture);
    if (!check.ok) {
        std::fprintf(stderr, "%s\n", check.error.c_str());
        return EXIT_FAILURE;
    }
    PrintUpdate(check, profile);
    return check.newer && check.archiveListed ? 3 : EXIT_SUCCESS;
}

// --update-sdk <folder>: the newer SDK downloaded, checked and unpacked
// there, the way the Framework step's "Download" does it.
int RunUpdateSdk(const std::string& folder, const std::string& prefix) {
    using namespace UltraCanvasStart;
    const SystemProfile profile = DetectSystem();
    const SdkUpdateCheck check = CheckSdkUpdate(profile.platform, InstalledSdkVersion(prefix), profile.architecture);
    if (!check.ok) {
        std::fprintf(stderr, "%s\n", check.error.c_str());
        return EXIT_FAILURE;
    }
    PrintUpdate(check, profile);
    if (!check.newer || !check.archiveListed) return EXIT_SUCCESS;
    std::printf("Downloading %s into %s...\n", check.archive.c_str(), folder.c_str());
    const SdkDownloadResult download = DownloadSdk(profile.platform, check.latest, profile.architecture, folder);
    if (!download.ok) {
        std::fprintf(stderr, "%s\n", download.error.c_str());
        return EXIT_FAILURE;
    }
    const RunResult unpack = RunStep(UnpackStep(download.archive, folder), profile);
    if (!unpack.Succeeded()) {
        std::fprintf(stderr, "tar could not unpack %s:\n%s\n", download.archive.c_str(),
                     unpack.started ? unpack.output.c_str() : unpack.error.c_str());
        return EXIT_FAILURE;
    }
    std::string newPrefix = FindSdkPrefix(SdkUnpackedFolder(download.archive));
    if (newPrefix.empty()) newPrefix = FindSdkPrefix(folder);
    if (newPrefix.empty()) {
        std::fprintf(stderr, "No lib/cmake/UltraCanvas/UltraCanvasConfig.cmake under %s\n", folder.c_str());
        return EXIT_FAILURE;
    }
    std::printf("SDK %s unpacked: %s\n", check.latest.c_str(), newPrefix.c_str());
    std::printf("Move a project to it: UltraCanvasStart --update-project <folder> --sdk \"%s\"\n",
                newPrefix.c_str());
    return EXIT_SUCCESS;
}

// --update-project <folder> --sdk <prefix>
int RunUpdateProject(const std::string& folder, const std::string& prefix) {
    using namespace UltraCanvasStart;
    if (prefix.empty()) {
        std::fprintf(stderr, "--update-project needs --sdk <prefix>, the SDK to move the project to\n");
        return EXIT_FAILURE;
    }
    // The folder itself, or the SDK folder an archive unpacked into it.
    const std::string sdk = FindSdkPrefix(prefix);
    if (sdk.empty()) {
        std::fprintf(stderr, "No lib/cmake/UltraCanvas/UltraCanvasConfig.cmake under %s\n", prefix.c_str());
        return EXIT_FAILURE;
    }
    const RepointResult result = RepointProjectSdk(folder, sdk);
    if (!result.ok) {
        std::fprintf(stderr, "%s\n", result.error.c_str());
        return EXIT_FAILURE;
    }
    if (!result.changed) std::printf("%s already names %s\n", result.presets.c_str(), sdk.c_str());
    else std::printf("%s now names %s%s\n", result.presets.c_str(), sdk.c_str(),
                     result.oldPrefix.empty() ? "" : (" (was " + result.oldPrefix + ")").c_str());
    if (result.cacheRemoved) std::printf("Removed build/CMakeCache.txt: the next configure finds the new SDK.\n");
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char* argv[]) {
    bool headlessCheck = false;
    bool headlessPlan = false;
    std::string page;
    int step = 0;
    UltraCanvasStart::Choices choices;
    UltraCanvasStart::ProjectOptions project;   // --create, --name, --sdk
    bool create = false;
    bool updateCheck = false;
    std::string updateSdkFolder;       // --update-sdk
    std::string updateProjectFolder;   // --update-project

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return EXIT_SUCCESS;
        } else if (arg == "--version" || arg == "-v") {
            std::printf("UltraCanvasStart %s\nUltraCanvas Framework %s\n",
                        ULTRACANVASSTART_VERSION, UltraCanvas::versionString);
            return EXIT_SUCCESS;
        } else if (arg == "--check") {
            headlessCheck = true;
        } else if (arg == "--plan") {
            headlessPlan = true;
        } else if (arg == "--all") {
            for (auto group : { UltraCanvasStart::DependencyGroup::Cdr,
                                UltraCanvasStart::DependencyGroup::Pdf,
                                UltraCanvasStart::DependencyGroup::Ocr,
                                UltraCanvasStart::DependencyGroup::Vectorizer,
                                UltraCanvasStart::DependencyGroup::Audio,
                                UltraCanvasStart::DependencyGroup::Barcode,
                                UltraCanvasStart::DependencyGroup::Net }) {
                choices.Set(group, true);
            }
        } else if (arg == "--step") {
            if (i + 1 >= argc || (step = std::atoi(argv[i + 1])) < 1 || step > 6) {
                std::printf("--step needs a number from 1 to 6\n");
                return EXIT_FAILURE;
            }
            ++i;
        } else if (arg == "--page") {
            if (i + 1 >= argc) {
                std::printf("--page needs platform, system, choices, install, project, build, ai, guide or report\n");
                return EXIT_FAILURE;
            }
            page = argv[++i];
        } else if (arg == "--assistant") {
            if (i + 1 >= argc || !UltraCanvasStart::AssistantFromName(argv[i + 1], choices.assistant)) {
                std::printf("--assistant needs none, claude, codex, copilot, gemini or other\n");
                return EXIT_FAILURE;
            }
            if (choices.assistant == UltraCanvasStart::Assistant::NoAssistant) choices.useAi = false;
            ++i;
        } else if (arg == "--create") {
            if (i + 1 >= argc) {
                std::printf("--create needs a folder\n");
                return EXIT_FAILURE;
            }
            project.folder = argv[++i];
            create = true;
        } else if (arg == "--name") {
            if (i + 1 >= argc) {
                std::printf("--name needs the application's name\n");
                return EXIT_FAILURE;
            }
            project.appName = argv[++i];
        } else if (arg == "--sdk") {
            if (i + 1 >= argc) {
                std::printf("--sdk needs the SDK prefix\n");
                return EXIT_FAILURE;
            }
            project.sdkPrefix = argv[++i];
        } else if (arg == "--update-check") {
            updateCheck = true;
        } else if (arg == "--update-sdk") {
            if (i + 1 >= argc) {
                std::printf("--update-sdk needs the folder to download the SDK into\n");
                return EXIT_FAILURE;
            }
            updateSdkFolder = argv[++i];
        } else if (arg == "--update-project") {
            if (i + 1 >= argc) {
                std::printf("--update-project needs the project's folder\n");
                return EXIT_FAILURE;
            }
            updateProjectFolder = argv[++i];
        } else if (arg == "--for") {
            if (i + 1 >= argc) {
                std::printf("--for needs linux, macos or windows\n");
                return EXIT_FAILURE;
            }
            const std::string os = argv[++i];
            if (os == "linux") choices.platform = UltraCanvasStart::Platform::Linux;
            else if (os == "macos") choices.platform = UltraCanvasStart::Platform::MacOS;
            else if (os == "windows") choices.platform = UltraCanvasStart::Platform::Windows;
            else {
                std::printf("unknown platform: %s\n", os.c_str());
                return EXIT_FAILURE;
            }
        } else {
            std::printf("Unknown argument: %s\nUse --help for usage.\n", arg.c_str());
            return EXIT_FAILURE;
        }
    }

    if (updateCheck) return RunUpdateCheck(project.sdkPrefix);
    if (!updateSdkFolder.empty()) return RunUpdateSdk(updateSdkFolder, project.sdkPrefix);
    if (!updateProjectFolder.empty()) return RunUpdateProject(updateProjectFolder, project.sdkPrefix);
    if (headlessCheck || headlessPlan) {
        return RunHeadless(headlessPlan, choices);
    }
    if (create) {
        project.assistant = choices.assistant;
        project.withAiNotes = choices.useAi;
        return RunCreate(project);
    }

    UltraCanvasApplication app;

#ifdef __linux__
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    if (!XInitThreads()) {
        debugOutput << "Warning: X11 threading initialization failed" << std::endl;
    }
#endif

    try {
        if (!app.Initialize("UltraCanvasStart")) {
            debugOutput << "Failed to initialize the UltraCanvas application" << std::endl;
            return EXIT_FAILURE;
        }
        app.SetDefaultWindowIcon(
            NormalizePath(GetResourcesDir() + "media/appicon/UltraCanvasStart.png"));
        UltraCanvasDialogManager::SetUseNativeDialogs(false);

        UltraCanvasStart::UltraCanvasStartWindow window;
        if (!window.Initialize()) {
            debugOutput << "Failed to create the UltraCanvasStart window" << std::endl;
            return EXIT_FAILURE;
        }
        if (choices.platform != UltraCanvasStart::Platform::Unknown) {
            window.PreselectPlatform(choices.platform);
        }
        if (choices.assistant != UltraCanvasStart::Assistant::ClaudeCode) {
            window.PreselectAssistant(choices.assistant);
        }
        window.Show();
        // After Show(): "guide" and "report" open dialogs over the window.
        if (!page.empty() && !window.ShowPage(page)) {
            std::printf("unknown page: %s\n", page.c_str());
            return EXIT_FAILURE;
        }
        if (step > 0) window.ShowStep(step);
        app.Run();
    } catch (const std::exception& e) {
        debugOutput << "Unhandled exception: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
