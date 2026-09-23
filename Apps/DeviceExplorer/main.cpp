// Apps/DeviceExplorer/main.cpp
// DeviceExplorer - shows the devices connected to this computer, as the
// IODeviceManager module finds them: a tree of printers, scanners and cameras
// (grouped by category, connection or backend) on the left, and everything
// known about the selected device on the right. It observes; it never opens,
// configures or prints to a device.
//
// Two ways in. Without arguments it opens the UltraCanvas window. With
// --list it scans once, prints the same tree as text and exits - usable over
// ssh and checkable in CI.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

// Before the window header: on Linux that one reaches X11, whose `None`
// macro would otherwise break HardwareQuery::None in this header.
#include "UltraCanvasHardwareInfo.h"

#include "ui/DeviceExplorerModel.h"
#include "ui/DeviceExplorerWindow.h"

#include "IODeviceManager/UltraCanvasIODeviceManager.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasUtils.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>

#ifdef __linux__
#include <X11/Xlib.h>
#endif

// DEVICEEXPLORER_VERSION comes from the build alone: CMake reads the first
// line of Docs/DeviceExplorer/CHANGELOG.md (cmake/UltraCanvasVersion.cmake)
// and passes it as a compile definition. No fallback here, so a build that
// lost it fails instead of reporting a wrong number.
#ifndef DEVICEEXPLORER_VERSION
#error "DEVICEEXPLORER_VERSION is not defined: build through CMake, which reads it from Docs/DeviceExplorer/CHANGELOG.md"
#endif

using namespace UltraCanvas;

namespace {

UltraCanvasApplication* g_app = nullptr;

void SignalHandler(int) {
    if (g_app) g_app->RequestExit();
    std::exit(EXIT_SUCCESS);
}

void PrintUsage(const char* programName) {
    std::printf(
        "DeviceExplorer - the devices connected to this computer\n"
        "Powered by the UltraCanvas framework (IODeviceManager)\n"
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "  (no options)        Open the DeviceExplorer window\n"
        "  --list              Scan once, print the device tree and exit\n"
        "  --details           With --list: print every property of every device\n"
        "  --group <by>        Group the tree by category (default), connection\n"
        "                      or backend - in the window and with --list\n"
        "  --show-serials      With --list: print serial numbers unmasked\n"
        "  -v, --version       Show version information\n"
        "  -h, --help          Show this message\n"
        "\n"
        "Printers come from CUPS (Linux, macOS) or the spooler (Windows),\n"
        "cameras from V4L2 (Linux), scanners from SANE (Linux) and eSCL; which\n"
        "of those this build carries is listed under the computer node.\n",
        programName);
}

// The computer node's description. Only the System category is probed:
// it is a few file reads, where the full capture walks every bus.
DeviceExplorer::MachineSummary DescribeMachine() {
    DeviceExplorer::MachineSummary machine;
    const HardwareSnapshot snapshot = UltraCanvasHardwareInfo::Capture(HardwareQuery::System);
    machine.hostName = snapshot.system.hostName;
    machine.operatingSystem = snapshot.system.osName;
    if (!snapshot.system.osVersion.empty() &&
        machine.operatingSystem.find(snapshot.system.osVersion) == std::string::npos) {
        if (!machine.operatingSystem.empty()) machine.operatingSystem += " ";
        machine.operatingSystem += snapshot.system.osVersion;
    }
    machine.kernel = snapshot.system.kernelVersion;
    machine.manufacturer = snapshot.system.manufacturer;
    machine.model = snapshot.system.productName;
    return machine;
}

int RunList(DeviceExplorer::DeviceGrouping grouping, bool details, bool showSerials) {
    auto& manager = IODeviceManager::GetInstance();
    const DeviceExplorer::DeviceInventory inventory = DeviceExplorer::ScanInventory(manager);
    const DeviceExplorer::IdentifierMask mask =
        showSerials ? DeviceExplorer::IdentifierMask{}
                    : DeviceExplorer::IdentifierMask(&UltraCanvasHardwareInfo::MaskIdentifier);
    const std::string text = DeviceExplorer::FormatInventoryText(
        inventory, grouping, DescribeMachine(), details, mask);
    std::fputs(text.c_str(), stdout);
    manager.Shutdown();
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char* argv[]) {
    bool list = false;
    bool details = false;
    bool showSerials = false;
    DeviceExplorer::DeviceGrouping grouping = DeviceExplorer::DeviceGrouping::Category;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return EXIT_SUCCESS;
        } else if (arg == "--version" || arg == "-v") {
            std::printf("DeviceExplorer %s\nUltraCanvas Framework %s\n",
                        DEVICEEXPLORER_VERSION, UltraCanvas::versionString);
            return EXIT_SUCCESS;
        } else if (arg == "--list") {
            list = true;
        } else if (arg == "--details") {
            details = true;
        } else if (arg == "--show-serials") {
            showSerials = true;
        } else if (arg == "--group") {
            if (i + 1 >= argc) {
                std::printf("--group needs a value: category, connection or backend\n");
                return EXIT_FAILURE;
            }
            if (!DeviceExplorer::ParseGrouping(argv[++i], grouping)) {
                std::printf("Unknown grouping: %s (use category, connection or backend)\n", argv[i]);
                return EXIT_FAILURE;
            }
        } else {
            std::printf("Unknown argument: %s\nUse --help for usage.\n", arg.c_str());
            return EXIT_FAILURE;
        }
    }

    if (list) return RunList(grouping, details, showSerials);

    UltraCanvasApplication app;
    g_app = &app;

#ifdef __linux__
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    if (!XInitThreads()) {
        debugOutput << "Warning: X11 threading initialization failed" << std::endl;
    }
#endif

    int exitCode = EXIT_SUCCESS;
    try {
        if (!app.Initialize("DeviceExplorer")) {
            debugOutput << "Failed to initialize the UltraCanvas application" << std::endl;
            return EXIT_FAILURE;
        }
        // One icon, everywhere the app is drawn: the window and the taskbar
        // entry read this file; the .ico embedded in the Windows binary and
        // the desktop entry's theme icon are rendered from the same
        // media/appicon/DeviceExplorer.svg (see CMakeLists.txt).
        app.SetDefaultWindowIcon(
            NormalizePath(GetResourcesDir() + "media/appicon/DeviceExplorer.png"));
        UltraCanvasDialogManager::SetUseNativeDialogs(true);

        DeviceExplorer::DeviceExplorerWindow window;
        if (!window.Initialize(DescribeMachine(), grouping)) {
            debugOutput << "Failed to create the DeviceExplorer window" << std::endl;
            exitCode = EXIT_FAILURE;
        } else {
            window.Show();
            app.Run();
        }
    } catch (const std::exception& e) {
        debugOutput << "Unhandled exception: " << e.what() << std::endl;
        exitCode = EXIT_FAILURE;
    }
    // After the window, whose destructor stops the watcher it started.
    IODeviceManager::GetInstance().Shutdown();
    return exitCode;
}
