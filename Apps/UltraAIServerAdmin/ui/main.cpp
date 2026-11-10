// Apps/UltraAIServerAdmin/ui/main.cpp
// ultraai-serveradmin: the window when started plainly, the commands of
// cli/Commands.cpp when given one. One copy per user
// (UltraCanvasSingleInstance.h): a second start brings the running window
// forward. The commands never initialise the application, so they run on a
// headless machine, in a script or in CI.
// Version: 0.1.0
// Author: UltraAI Module
#include "Commands.h"
#include "ServerAdminWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasSingleInstance.h"
#include "UltraCanvasUtils.h"

#ifdef __linux__
#include <X11/Xlib.h>
#include <csignal>
#endif

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
    if (!args.empty()) return UltraAIServerAdmin::Cli::Run(args);

    UltraCanvasSingleInstance instance("org.ultraos.ultraaiserveradmin", "UltraAIServerAdmin");
    const UltraCanvasSingleInstance::Start start = instance.Claim();
    if (start == UltraCanvasSingleInstance::Start::HandedOff) return EXIT_SUCCESS;

#ifdef __linux__
    if (!XInitThreads()) std::cerr << "warning: XInitThreads failed\n";
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
#endif

    UltraCanvasApplication app;
    std::unique_ptr<UltraAIServerAdmin::Ui::ServerAdminWindow> window;
    try {
        if (!app.Initialize("UltraAIServerAdmin")) {
            std::cerr << "Failed to initialize UltraCanvas application\n";
            return EXIT_FAILURE;
        }
        if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
            return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "UltraAI ServerAdmin");
        app.SetDefaultWindowIcon(NormalizePath(GetResourcesDir() + "media/appicon/UltraAI.png"));
        window = std::make_unique<UltraAIServerAdmin::Ui::ServerAdminWindow>(app);
        if (!window->Create()) {
            std::cerr << "Failed to create the window\n";
            return EXIT_FAILURE;
        }
        window->Show();
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
    window.reset();
    return EXIT_SUCCESS;
}
