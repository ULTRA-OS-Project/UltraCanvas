// Apps/UltraStoreAdmin/ui/main.cpp
// ultrastoreadmin: the window when started plainly (or with --store <dir>
// alone), the commands of cli/Commands.cpp when given one. One copy per user
// (UltraCanvasSingleInstance.h): a second start brings the running window
// forward. The commands never initialise the application, so they run on a
// headless machine and in CI.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "AdminWindow.h"
#include "Commands.h"
#include "PackageStore.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasSingleInstance.h"
#include "UltraCanvasUtils.h"

#ifdef __linux__
#include <X11/Xlib.h>
#include <csignal>
#endif

#include <cstdio>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace UltraCanvas;

namespace {

#ifdef __linux__
void OnSignal(int) {
    UltraCanvasApplicationBase::RequestExitFromSignal();
}
#endif

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    std::string storeDir = UltraStoreAdmin::DefaultAdminStoreDir();
    bool window = args.empty();
    if (args.size() == 2 && args[0] == "--store") {
        storeDir = args[1];
        window = true;
    }
    if (!window) return UltraStoreAdmin::Cli::Run(args);

    UltraCanvasSingleInstance instance("org.ultraos.ultrastoreadmin", "UltraStoreAdmin");
    const UltraCanvasSingleInstance::Start start = instance.Claim();
    if (start == UltraCanvasSingleInstance::Start::HandedOff) return EXIT_SUCCESS;

#ifdef __linux__
    if (!XInitThreads()) std::cerr << "warning: XInitThreads failed\n";
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
#endif

    UltraCanvasApplication app;
    std::unique_ptr<UltraStoreAdmin::Ui::AdminWindow> adminWindow;
    try {
        if (!app.Initialize("UltraStoreAdmin")) {
            std::cerr << "Failed to initialize UltraCanvas application\n";
            return EXIT_FAILURE;
        }
        if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
            return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "UltraStoreAdmin");
        app.SetDefaultWindowIcon(NormalizePath(GetResourcesDir() + "media/appicon/UltraStoreAdmin.png"));
        adminWindow = std::make_unique<UltraStoreAdmin::Ui::AdminWindow>(app, storeDir);
        if (!adminWindow->Create()) {
            std::cerr << "Failed to create the admin window\n";
            return EXIT_FAILURE;
        }
        adminWindow->Show();
        instance.ListenRaise();
        app.Run();
        instance.Close();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Fatal: unknown exception\n";
        return EXIT_FAILURE;
    }
    adminWindow.reset();
    return EXIT_SUCCESS;
}
