// Apps/UltraDesktop/main.cpp
// UltraDesktop - the ULTRA OS desktop: the taskbar with the running
// applications, the desktop organiser with the virtual desktops, the
// Sticker board, the notes, the clipboard and the screenshot, and the info panel with
// the devices and services - on one screen-sized window under everything
// else, built from UltraCanvas elements on the UltraCanvasDesktopShell
// module.
//
// Without arguments it opens the desktop. The headless modes print what the
// module sees and exit, which is what makes the module checkable over ssh:
// --windows lists the open windows, --apps the installed applications,
// --devices the device activity, --screenshot takes one.
//
// On Windows it can also take the place of the Windows desktop for this
// user: --use-as-desktop makes the sign-in start it (with --session) beside
// Explorer, whose taskbar hides; --use-instead-of-explorer makes it the
// session's shell from the next sign-in (with --shell, through
// UltraCanvasShellGuard, which brings Explorer back should it fail);
// --restore-desktop undoes either - the way back the installer's
// uninstaller and a stuck user (Ctrl+Shift+Esc, Run new task) take.
// Version: 0.3.0 - the command-line output is translated: the language is loaded before the arguments are read
// Version: 0.2.0 - --use-instead-of-explorer, --shell
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
#include "UltraLocalisation/UltraLocalisation.h"

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

// One option and what it does. The option's own spelling is not translated;
// the text is, and the terminal wraps it.
void Option(const char* option, const std::string& text) {
    std::printf("  %-26s %s\n", option, text.c_str());
}

void PrintUsage(const char* programName) {
    std::printf("%s\n%s\n\n%s\n\n",
                Tr("UltraDesktop - the ULTRA OS desktop").c_str(),
                Tr("Powered by the UltraCanvas framework (UltraCanvasDesktopShell)").c_str(),
                TrF("Usage: {0} [options]", programName).c_str());
    Option("", Tr("(no options) Open the desktop"));
    Option("--edge <where>", Tr("Put the taskbar on the left (default), top or bottom for this run, "
                                "without changing the settings file"));
    Option("--settings <file>", Tr("Use this settings file instead of the default"));
    Option("--windows", Tr("Print the open windows and exit"));
    Option("--apps", Tr("Print the installed applications and exit"));
    Option("--devices", Tr("Print the device activity and exit"));
    Option("--screenshot [file]", Tr("Capture the screen to file (default: the Pictures folder)"));
    Option("--use-as-desktop", Tr("Windows: start UltraDesktop at sign-in in place of the Windows desktop "
                                  "(Explorer's taskbar hides), then open it"));
    Option("--use-instead-of-explorer", Tr("Windows: from the next sign-in UltraDesktop is the desktop and "
                                           "Explorer does not start (it comes back by itself should "
                                           "UltraDesktop fail), then exit"));
    Option("--restore-desktop", Tr("Windows: back to the Windows desktop - no start at sign-in, the "
                                   "taskbar as it was - and exit"));
    Option("--session", Tr("Started by the sign-in: close when switched back"));
    Option("--shell", Tr("Started as the session's shell: as --session, and host the notification "
                         "area's icons"));
    Option("-v, --version", Tr("Show version information"));
    Option("-h, --help", Tr("Show this message"));
}

int PrintWindows() {
    if (!UltraCanvasDesktopShell::IsAvailable()) {
        std::printf("%s\n", TrF("The window list is not available here (backend: {0}).",
                                UltraCanvasDesktopShell::GetBackendName()).c_str());
        return EXIT_FAILURE;
    }
    const int current = UltraCanvasDesktopShell::GetCurrentVirtualDesktop();
    std::printf("%s\n", TrF("Virtual desktops: {0}, current: {1}",
                            UltraCanvasDesktopShell::GetVirtualDesktopCount(), current + 1).c_str());
    const std::string minimized = " [" + Tr("minimized") + "]";
    const std::string everywhere = " [" + Tr("every desktop") + "]";
    for (const DesktopWindowInfo& w : UltraCanvasDesktopShell::ListWindows()) {
        std::printf("%s%s %-24s %s%s%s\n",
                    w.active ? "*" : " ", w.skipTaskbar ? "-" : " ",
                    w.appClass.c_str(), w.title.c_str(),
                    w.minimized ? minimized.c_str() : "",
                    w.virtualDesktop < 0 ? everywhere.c_str() : "");
    }
    return EXIT_SUCCESS;
}

int PrintApplications() {
    const auto apps = UltraCanvasDesktopShell::ListApplications();
    for (const UCDesktopEntry& app : apps) {
        std::printf("%-30s %s\n", app.name.c_str(), app.program.c_str());
    }
    std::printf("%s\n", TrN("{n} application", "{n} applications", static_cast<long long>(apps.size())).c_str());
    return EXIT_SUCCESS;
}

int PrintDevices() {
    const DesktopDeviceActivity a = UltraCanvasDesktopShell::ReadDeviceActivity();
    const auto yesNo = [](bool b) { return b ? Tr("yes") : Tr("no"); };
    // A label and its value: the labels differ in length from language to
    // language, so they are not padded into a column.
    const auto line = [](const std::string& label, const std::string& value) {
        std::printf("%s: %s\n", label.c_str(), value.c_str());
    };
    line(Tr("Webcam in use"), yesNo(a.webcamInUse));
    line(Tr("Microphone in use"), yesNo(a.microphoneInUse));
    line(Tr("Speaker playing"), yesNo(a.speakerPlaying));
    line("Bluetooth", !a.bluetoothPresent ? Tr("no adapter")
                      : TrF("{0}, {1} connected", yesNo(a.bluetoothPowered), a.bluetoothConnectedDevices));
    line("Wi-Fi", !a.wifiPresent ? Tr("no adapter")
                  : a.wifiSsid.empty() ? yesNo(a.wifiConnected)
                                       : yesNo(a.wifiConnected) + " " + a.wifiSsid);
    line("LAN", yesNo(a.lanConnected));
    line("VPN", yesNo(a.vpnConnected));
    line(Tr("Traffic"), TrF("{0} received, {1} sent", FormatFileSize(static_cast<size_t>(a.bytesReceived)),
                            FormatFileSize(static_cast<size_t>(a.bytesSent))));
    line(Tr("USB devices"), std::to_string(a.usbDeviceCount));
    line(Tr("Battery"), !a.batteryPresent ? Tr("none")
                        : a.batteryCharging ? TrF("{0}%, charging", a.batteryPercent)
                                            : TrF("{0}%", a.batteryPercent));
    line(Tr("Keyboard layout"), a.keyboardLayout.empty() ? Tr("unknown") : a.keyboardLayout);
    for (const std::string& warning : a.warnings) std::printf("%s\n", TrF("Note: {0}", warning).c_str());
    return EXIT_SUCCESS;
}

// The way back: no start at sign-in, the taskbar as it was. A running
// session desktop sees the switch is gone and closes within a second.
int RestoreDesktop() {
    std::string error;
    if (!UltraCanvasDesktopShell::RestoreSystemDesktop(&error)) {
        std::printf("%s\n", TrF("The Windows desktop could not be restored: {0}", error).c_str());
        return EXIT_FAILURE;
    }
    std::printf("%s\n", Tr("The Windows desktop is back: UltraDesktop no longer starts at sign-in.").c_str());
    return EXIT_SUCCESS;
}

// From the next sign-in UltraDesktop is the session's shell; this session
// keeps the desktop it has.
int UseInsteadOfExplorer() {
    std::string error;
    if (!UltraCanvasDesktopShell::ReplaceSystemDesktop(UltraCanvasDesktopShell::FindProgram("UltraDesktop"),
                                                       {"--shell"}, &error, DesktopReplacement::Instead)) {
        std::printf("%s\n", TrF("UltraDesktop cannot take the place of Explorer: {0}", error).c_str());
        return EXIT_FAILURE;
    }
    std::printf("%s\n%s\n",
                Tr("From the next sign-in UltraDesktop is the desktop, and Explorer does not start.").c_str(),
                Tr("Should UltraDesktop fail to start, Explorer starts in its place for that session.").c_str());
    return EXIT_SUCCESS;
}

int Screenshot(const std::string& file) {
    const std::string path = file.empty() ? UltraCanvasDesktopShell::DefaultScreenshotPath() : file;
    std::string error;
    if (!UltraCanvasDesktopShell::CaptureScreen(path, &error)) {
        std::printf("%s\n", TrF("Screenshot failed: {0}", error).c_str());
        return EXIT_FAILURE;
    }
    std::printf("%s\n", TrF("Saved {0}", path).c_str());
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char* argv[]) {
    std::string settingsPath = UltraDesktop::DesktopSettings::DefaultPath();
    bool haveEdge = false;
    UltraDesktop::TaskbarEdge edge = UltraDesktop::TaskbarEdge::Left;
    bool sessionDesktop = false;
    bool shellSession = false;

    // The language first: what the arguments below print - the help, a
    // list, a refusal - is read in a terminal before any window exists.
    UltraCanvasApplicationBase::LoadTranslations("UltraDesktop");

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
        } else if (arg == "--restore-desktop") {
            return RestoreDesktop();
        } else if (arg == "--use-as-desktop") {
            std::string error;
            if (!UltraCanvasDesktopShell::ReplaceSystemDesktop(UltraCanvasDesktopShell::FindProgram("UltraDesktop"),
                                                               {"--session"}, &error)) {
                std::printf("%s\n", TrF("UltraDesktop cannot take the place of the desktop: {0}", error).c_str());
                return EXIT_FAILURE;
            }
            sessionDesktop = true;
        } else if (arg == "--use-instead-of-explorer") {
            return UseInsteadOfExplorer();
        } else if (arg == "--session") {
            sessionDesktop = true;
        } else if (arg == "--shell") {
            sessionDesktop = true;
            shellSession = true;
        } else if (arg == "--edge") {
            if (i + 1 >= argc || !UltraDesktop::ParseTaskbarEdge(argv[i + 1], edge)) {
                std::printf("%s\n", Tr("--edge needs a value: left, top or bottom").c_str());
                return EXIT_FAILURE;
            }
            ++i;
            haveEdge = true;
        } else if (arg == "--settings") {
            if (i + 1 >= argc) {
                std::printf("%s\n", Tr("--settings needs a file name").c_str());
                return EXIT_FAILURE;
            }
            settingsPath = argv[++i];
        } else {
            std::printf("%s\n%s\n", TrF("Unknown argument: {0}", arg).c_str(), Tr("Use --help for usage.").c_str());
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
        desktop.SetSessionDesktop(sessionDesktop);
        desktop.SetShellSession(shellSession);
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
