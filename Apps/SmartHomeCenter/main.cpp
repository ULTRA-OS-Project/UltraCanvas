// Apps/SmartHomeCenter/main.cpp
// SmartHome Center - the home control window on the SmartHome module.
//
// Every built-in radio the build carries is offered to the module; when none
// can start (no stick, no gateway - or, today, none compiled in), the window
// opens the simulated demo home instead and says so. --demo asks for the
// demo home outright.
//
// Two ways in. Without --list it opens the window. With --list it starts the
// home, prints it room by room and exits - usable over ssh and checkable in
// CI. One window per user: a second start brings the first one forward.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS

#include "ui/SmartHomeCenterModel.h"
#include "ui/SmartHomeCenterPaths.h"
#include "ui/SmartHomeCenterWindow.h"

#include "UltraCanvasSmartHome.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasDebug.h"
#include "UltraCanvasModalDialog.h"
#include "UltraCanvasSingleInstance.h"
#include "UltraCanvasUtils.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <string>
#include <vector>

#ifdef __linux__
#include <X11/Xlib.h>
#endif

#ifndef SMARTHOMECENTER_VERSION
#error "SMARTHOMECENTER_VERSION is not defined: build through CMake, which reads it from Docs/SmartHomeCenter/CHANGELOG.md"
#endif

using namespace UltraCanvas;
using namespace UltraCanvas::SmartHome;

namespace {

void SignalHandler(int) {
    UltraCanvasApplicationBase::RequestExitFromSignal();
}

void PrintUsage(const char* programName) {
    std::printf(
        "SmartHome Center - control the lights, heating, locks and blinds of your home\n"
        "Powered by the UltraCanvas framework (SmartHome module)\n"
        "\n"
        "Usage: %s [options]\n"
        "\n"
        "  (no options)        Open the SmartHome Center window\n"
        "  --demo              Use the simulated demo home even if a radio is available\n"
        "  --list              Start the home, print it room by room and exit\n"
        "  --page <name>       Open on a page: home, devices, energy or activity\n"
        "  --select <device>   Open with a device selected, by its name\n"
        "  --trigger <device>  Demo home: set a sensor off as the window opens\n"
        "                      (\"Under-sink leak sensor\", \"Hallway motion\", ...)\n"
        "  -v, --version       Show version information\n"
        "  -h, --help          Show this message\n",
        programName);
}

// Starts the module and its radios. Returns true when the home is the
// simulated one.
bool StartHome(bool forceDemo) {
    auto& api = SmartHomeAPI::Instance();
    api.Initialize();
    bool anyRadio = false;
    if (!forceDemo) {
        for (SmartHomeProtocolType type : {SmartHomeProtocolType::Matter, SmartHomeProtocolType::Thread,
                                           SmartHomeProtocolType::Zigbee, SmartHomeProtocolType::ZWave,
                                           SmartHomeProtocolType::KNX}) {
            // Only backends this build carries have a factory; EnableProtocol
            // refuses the rest, and a carried one with no hardware fails too.
            if (api.EnableProtocol(type)) anyRadio = true;
        }
    }
    if (anyRadio) return false;
    api.EnableProtocol(SmartHomeProtocolType::Simulated);
    return true;
}

bool ParsePage(const std::string& text, SmartHomeCenter::Page& page) {
    if (text == "home") page = SmartHomeCenter::Page::Home;
    else if (text == "devices") page = SmartHomeCenter::Page::Devices;
    else if (text == "energy") page = SmartHomeCenter::Page::Energy;
    else if (text == "activity") page = SmartHomeCenter::Page::Activity;
    else return false;
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    bool list = false;
    bool demo = false;
    SmartHomeCenter::Page page = SmartHomeCenter::Page::Home;
    std::string select;
    std::vector<std::string> triggers;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return EXIT_SUCCESS;
        } else if (arg == "--version" || arg == "-v") {
            std::printf("SmartHome Center %s\nUltraCanvas Framework %s\n",
                        SMARTHOMECENTER_VERSION, UltraCanvas::versionString);
            return EXIT_SUCCESS;
        } else if (arg == "--list") {
            list = true;
        } else if (arg == "--demo") {
            demo = true;
        } else if (arg == "--page") {
            if (i + 1 >= argc || !ParsePage(argv[++i], page)) {
                std::printf("--page needs one of: home, devices, energy, activity\n");
                return EXIT_FAILURE;
            }
        } else if (arg == "--select") {
            if (i + 1 >= argc) {
                std::printf("--select needs a device name\n");
                return EXIT_FAILURE;
            }
            select = argv[++i];
        } else if (arg == "--trigger") {
            if (i + 1 >= argc) {
                std::printf("--trigger needs a device name\n");
                return EXIT_FAILURE;
            }
            triggers.push_back(argv[++i]);
        } else {
            std::printf("Unknown argument: %s\nUse --help for usage.\n", arg.c_str());
            return EXIT_FAILURE;
        }
    }

    if (list) {
        const bool simulated = StartHome(demo);
        if (simulated) std::printf("Demo home - simulated devices, no radio\n");
        std::fputs(SmartHomeCenter::FormatHomeText(SmartHomeCenter::DescribeHome()).c_str(), stdout);
        SmartHomeAPI::Instance().Shutdown();
        return EXIT_SUCCESS;
    }

    // Only the window claims the one copy per user: it owns the radios.
    UltraCanvasSingleInstance instance("org.ultraos.smarthomecenter", "SmartHomeCenter");
    const UltraCanvasSingleInstance::Start start = instance.Claim();
    if (start == UltraCanvasSingleInstance::Start::HandedOff) return EXIT_SUCCESS;

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
        if (!app.Initialize("SmartHomeCenter")) {
            debugOutput << "Failed to initialize the UltraCanvas application" << std::endl;
            return EXIT_FAILURE;
        }
        if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
            return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "SmartHome Center");
        app.SetDefaultWindowIcon(NormalizePath(GetResourcesDir() + "media/appicon/SmartHomeCenter.png"));
        UltraCanvasDialogManager::SetUseNativeDialogs(true);

        const bool simulated = StartHome(demo);
        // The home's own database: scenes, automations, names and rooms,
        // the log and the history. Without one the window still works; it
        // just forgets on exit, and says so in the log.
        const std::string storePath = SmartHomeCenter::DefaultStorePath(simulated);
        if (storePath.empty() || !SmartHomeAPI::Instance().OpenStore(storePath)) {
            debugOutput << "SmartHome store not opened: " << SmartHomeAPI::Instance().GetStoreError() << std::endl;
        }
        {
            SmartHomeCenter::SmartHomeCenterWindow window;
            if (!window.Initialize(simulated)) {
                debugOutput << "Failed to create the SmartHome Center window" << std::endl;
                exitCode = EXIT_FAILURE;
            } else {
                window.ShowPage(page);
                if (!select.empty()) {
                    const std::string id = window.FindDeviceByName(select);
                    if (!id.empty()) window.SelectDevice(id);
                }
                // Through the API like any command; only the demo home's
                // devices answer these.
                for (const std::string& name : triggers) {
                    const std::string id = window.FindDeviceByName(name);
                    if (simulated && !id.empty()) SmartHomeAPI::Instance().SendCommand({id, "simulateTrigger", {}});
                }
                window.Show();
                instance.ListenRaise();
                app.Run();
                instance.Close();
            }
        }
    } catch (const std::exception& e) {
        debugOutput << "Unhandled exception: " << e.what() << std::endl;
        exitCode = EXIT_FAILURE;
    }
    // After the window, whose destructor unhooks its callbacks.
    SmartHomeAPI::Instance().Shutdown();
    return exitCode;
}
