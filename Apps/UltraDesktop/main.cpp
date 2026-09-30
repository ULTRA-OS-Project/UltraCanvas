// Apps/UltraDesktop/main.cpp
// UltraDesktop - the ULTRA OS desktop: the taskbar with the running
// applications, the desktop organiser with the virtual desktops, the
// Stickerboard, the clipboard and the screenshot, and the info panel with
// the devices and services - on one screen-sized window under everything
// else, built from UltraCanvas elements on the UltraCanvasDesktopShell
// module.
//
// Without arguments it opens the desktop. The headless modes print what the
// module sees and exit, which is what makes the module checkable over ssh:
// --windows lists the open windows, --apps the installed applications,
// --devices the device activity, --screenshot takes one.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "ui/UltraDesktopSettings.h"
#include "ui/UltraDesktopWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasDesktopShell.h"
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

// ULTRADESKTOP_VERSION comes from the build alone: CMake reads the first line
// of Docs/UltraDesktop/CHANGELOG.md (cmake/UltraCanvasVersion.cmake) and
// passes it as a compile definition. No fallback, so a build that lost it
// fails instead of reporting a wrong number.
#ifndef ULTRADESKTOP_VERSION
#error "ULTRADESKTOP_VERSION is not defined: build through CMake, which reads it from Docs/UltraDesktop/CHANGELOG.md"
#endif

using namespace UltraCanvas;

namespace {

void SignalHandler(int) {
    UltraCanvasApplicationBase::RequestExitFromSignal();
}

void PrintUsage(const char* programName) {
    std::printf(
        "UltraDesktop - the ULTRA OS desktop\n"
        "Powered by the UltraCanvas framework (UltraCanvasDesktopShell)\n"
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "  (no options)        Open the desktop\n"
        "  --edge <where>      Put the taskbar on the left (default), top or bottom for\n"
        "                      this run, without changing the settings file\n"
        "  --settings <file>   Use this settings file instead of the default\n"
        "  --windows           Print the open windows and exit\n"
        "  --apps              Print the installed applications and exit\n"
        "  --devices           Print the device activity and exit\n"
        "  --screenshot [file] Capture the screen to file (default: the Pictures folder)\n"
        "  -v, --version       Show version information\n"
        "  -h, --help          Show this message\n",
        programName);
}

int PrintWindows() {
    if (!UltraCanvasDesktopShell::IsAvailable()) {
        std::printf("The window list is not available here (backend: %s).\n",
                    UltraCanvasDesktopShell::GetBackendName().c_str());
        return EXIT_FAILURE;
    }
    const int current = UltraCanvasDesktopShell::GetCurrentVirtualDesktop();
    std::printf("Virtual desktops: %d, current: %d\n", UltraCanvasDesktopShell::GetVirtualDesktopCount(),
                current + 1);
    for (const DesktopWindowInfo& w : UltraCanvasDesktopShell::ListWindows()) {
        std::printf("%s%s %-24s %s%s%s\n",
                    w.active ? "*" : " ", w.skipTaskbar ? "-" : " ",
                    w.appClass.c_str(), w.title.c_str(),
                    w.minimized ? " [minimized]" : "",
                    w.virtualDesktop < 0 ? " [every desktop]" : "");
    }
    return EXIT_SUCCESS;
}

int PrintApplications() {
    const auto apps = UltraCanvasDesktopShell::ListApplications();
    for (const UCDesktopEntry& app : apps) {
        std::printf("%-30s %s\n", app.name.c_str(), app.program.c_str());
    }
    std::printf("%zu applications\n", apps.size());
    return EXIT_SUCCESS;
}

int PrintDevices() {
    const DesktopDeviceActivity a = UltraCanvasDesktopShell::ReadDeviceActivity();
    const auto yesNo = [](bool b) { return b ? "yes" : "no"; };
    std::printf("Webcam in use:     %s\n", yesNo(a.webcamInUse));
    std::printf("Microphone in use: %s\n", yesNo(a.microphoneInUse));
    std::printf("Speaker playing:   %s\n", yesNo(a.speakerPlaying));
    std::printf("Bluetooth:         %s%s, %d connected\n", a.bluetoothPresent ? "" : "no adapter; ",
                yesNo(a.bluetoothPowered), a.bluetoothConnectedDevices);
    std::printf("Wi-Fi:             %s%s %s\n", a.wifiPresent ? "" : "no adapter; ",
                yesNo(a.wifiConnected), a.wifiSsid.c_str());
    std::printf("LAN:               %s\n", yesNo(a.lanConnected));
    std::printf("VPN:               %s\n", yesNo(a.vpnConnected));
    std::printf("Traffic:           %s received, %s sent\n",
                FormatFileSize(static_cast<size_t>(a.bytesReceived)).c_str(),
                FormatFileSize(static_cast<size_t>(a.bytesSent)).c_str());
    std::printf("USB devices:       %d\n", a.usbDeviceCount);
    if (a.batteryPresent) std::printf("Battery:           %d%%%s\n", a.batteryPercent, a.batteryCharging ? ", charging" : "");
    else std::printf("Battery:           none\n");
    std::printf("Keyboard layout:   %s\n", a.keyboardLayout.empty() ? "unknown" : a.keyboardLayout.c_str());
    for (const std::string& warning : a.warnings) std::printf("Note: %s\n", warning.c_str());
    return EXIT_SUCCESS;
}

int Screenshot(const std::string& file) {
    const std::string path = file.empty() ? UltraCanvasDesktopShell::DefaultScreenshotPath() : file;
    std::string error;
    if (!UltraCanvasDesktopShell::CaptureScreen(path, &error)) {
        std::printf("Screenshot failed: %s\n", error.c_str());
        return EXIT_FAILURE;
    }
    std::printf("Saved %s\n", path.c_str());
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char* argv[]) {
    std::string settingsPath = UltraDesktop::DesktopSettings::DefaultPath();
    bool haveEdge = false;
    UltraDesktop::TaskbarEdge edge = UltraDesktop::TaskbarEdge::Left;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return EXIT_SUCCESS;
        } else if (arg == "--version" || arg == "-v") {
            std::printf("UltraDesktop %s\nUltraCanvas Framework %s\n", ULTRADESKTOP_VERSION, UltraCanvas::versionString);
            return EXIT_SUCCESS;
        } else if (arg == "--windows") {
            return PrintWindows();
        } else if (arg == "--apps") {
            return PrintApplications();
        } else if (arg == "--devices") {
            return PrintDevices();
        } else if (arg == "--screenshot") {
            std::string file;
            if (i + 1 < argc && argv[i + 1][0] != '-') file = argv[++i];
            return Screenshot(file);
        } else if (arg == "--edge") {
            if (i + 1 >= argc || !UltraDesktop::ParseTaskbarEdge(argv[i + 1], edge)) {
                std::printf("--edge needs a value: left, top or bottom\n");
                return EXIT_FAILURE;
            }
            ++i;
            haveEdge = true;
        } else if (arg == "--settings") {
            if (i + 1 >= argc) {
                std::printf("--settings needs a file name\n");
                return EXIT_FAILURE;
            }
            settingsPath = argv[++i];
        } else {
            std::printf("Unknown argument: %s\nUse --help for usage.\n", arg.c_str());
            return EXIT_FAILURE;
        }
    }

    UltraCanvasApplication app;

#ifdef __linux__
    std::signal(SIGINT, SignalHandler);
    std::signal(SIGTERM, SignalHandler);
    if (!XInitThreads()) {
        debugOutput << "Warning: X11 threading initialization failed" << std::endl;
    }
#endif

    int exitCode = EXIT_SUCCESS;
    try {
        if (!app.Initialize("UltraDesktop")) {
            debugOutput << "Failed to initialize the UltraCanvas application" << std::endl;
            return EXIT_FAILURE;
        }
        app.SetDefaultWindowIcon(NormalizePath(GetResourcesDir() + "media/appicon/UltraDesktop.png"));
        UltraCanvasDialogManager::SetUseNativeDialogs(true);

        // The desktop outlives the loop it runs: its destructor stops the
        // monitor and the device poll before the application goes.
        UltraDesktop::UltraDesktopWindow desktop;
        if (!desktop.Initialize(settingsPath, haveEdge ? &edge : nullptr)) {
            debugOutput << "Failed to create the desktop window" << std::endl;
            exitCode = EXIT_FAILURE;
        } else {
            desktop.Show();
            app.Run();
        }
    } catch (const std::exception& e) {
        debugOutput << "Unhandled exception: " << e.what() << std::endl;
        exitCode = EXIT_FAILURE;
    }
    return exitCode;
}
