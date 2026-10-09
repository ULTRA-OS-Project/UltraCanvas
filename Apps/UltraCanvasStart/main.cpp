// Apps/UltraCanvasStart/main.cpp
// UltraCanvasStart — sets a computer up for writing UltraCanvas applications.
// It finds out what the machine has, installs the development packages that
// are missing, points at the prebuilt SDK, writes a project skeleton and
// explains how to work with Claude Code on it, locally or through GitHub.
//
// Without arguments it opens the window, on the platform page preselected to
// the platform it runs on - the same choice Docs/GettingStarted.md offers,
// so a programmer can read another platform's instructions too. With --check
// or --plan it runs headless, which is what CI uses to smoke-test it.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "ui/UltraCanvasStartWindow.h"

#include "StartChecks.h"
#include "StartPlan.h"
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
        "  --all             Check every feature group, not only the toolchain\n"
        "                    and the framework core\n"
        "  -v, --version     Show version information\n"
        "  -h, --help        Show this message\n"
        "\n"
        "Exit status with --check: 0 when nothing is missing, 2 when something is.\n",
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

} // namespace

int main(int argc, char* argv[]) {
    bool headlessCheck = false;
    bool headlessPlan = false;
    UltraCanvasStart::Choices choices;

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

    if (headlessCheck || headlessPlan) {
        return RunHeadless(headlessPlan, choices);
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
        window.Show();
        app.Run();
    } catch (const std::exception& e) {
        debugOutput << "Unhandled exception: " << e.what() << std::endl;
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
