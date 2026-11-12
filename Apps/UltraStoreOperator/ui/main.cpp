// Apps/UltraStoreOperator/ui/main.cpp
// ultrastoreop: the window when started plainly (or with --settings <file>
// and --page <id> alone), the commands of cli/Commands.cpp when given one. One copy per
// user (UltraCanvasSingleInstance.h): a second start brings the running
// window forward. The commands never initialise the application, so they
// run on a headless machine and in CI.
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "Commands.h"
#include "Operator.h"
#include "OperatorWindow.h"

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
    std::string settingsPath = UltraStoreOperator::OperatorSettings::DefaultPath();
    std::string page;
    // The window takes --settings <file> and --page <id> and nothing else;
    // anything more is a command.
    bool window = true;
    for (size_t i = 0; i < args.size(); i += 2) {
        if (i + 1 < args.size() && args[i] == "--settings") settingsPath = args[i + 1];
        else if (i + 1 < args.size() && args[i] == "--page") page = args[i + 1];
        else { window = false; break; }
    }
    if (!window) return UltraStoreOperator::Cli::Run(args);

    UltraCanvasSingleInstance instance("org.ultraos.ultrastoreop", "UltraStoreOperator");
    const UltraCanvasSingleInstance::Start start = instance.Claim();
    if (start == UltraCanvasSingleInstance::Start::HandedOff) return EXIT_SUCCESS;

#ifdef __linux__
    if (!XInitThreads()) std::cerr << "warning: XInitThreads failed\n";
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
#endif

    UltraCanvasApplication app;
    std::unique_ptr<UltraStoreOperator::Ui::OperatorWindow> operatorWindow;
    try {
        if (!app.Initialize("UltraStoreOperator")) {
            std::cerr << "Failed to initialize UltraCanvas application\n";
            return EXIT_FAILURE;
        }
        if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
            return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "UltraStoreOperator");
        app.SetDefaultWindowIcon(NormalizePath(GetResourcesDir() + "media/appicon/UltraStoreOperator.png"));
        operatorWindow = std::make_unique<UltraStoreOperator::Ui::OperatorWindow>(app, settingsPath, page);
        if (!operatorWindow->Create()) {
            std::cerr << "Failed to create the operator window\n";
            return EXIT_FAILURE;
        }
        operatorWindow->Show();
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
    operatorWindow.reset();
    return EXIT_SUCCESS;
}
