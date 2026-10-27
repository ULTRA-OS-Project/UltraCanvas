// Apps/UltraStore/ui/main.cpp
// ultrastore: the window when started plainly (or with --catalog, --root,
// --channel), the commands of cli/Commands.cpp when given one. One program,
// as UltraFIBU and UltraPress are, because a case-insensitive file system
// cannot tell `ultrastore` from `UltraStore`; one copy per user
// (UltraCanvasSingleInstance.h): a second start brings the running window
// forward. The commands never initialise the application, so they run on a
// headless machine and in CI as before.
// Version: 0.2.0 - ultrastore:// links, handed to the running copy
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "Commands.h"
#include "StoreWindow.h"

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

// A start with no command, or with the window's options only, opens the window.
bool IsStoreLink(const std::string& arg) { return arg.rfind("ultrastore://", 0) == 0; }

// A start with no command, with the window's options only, or with a link
// (ultrastore://code/<CODE>, ultrastore://app/<id>) opens the window.
bool WantsWindow(const std::vector<std::string>& args) {
    if (args.empty()) return true;
    const std::string& first = args[0];
    return first == "--catalog" || first == "--root" || first == "--channel" || IsStoreLink(first);
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<std::string> args(argv + 1, argv + argc);
    if (!WantsWindow(args)) return UltraStore::Cli::Run(args);

    UltraStore::Ui::WindowOptions options;
    std::vector<std::string> links;   // the links of this start, for the window - or the running copy's
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (IsStoreLink(a)) { links.push_back(a); continue; }
        std::string* target = a == "--catalog" ? &options.catalogFile : a == "--root" ? &options.root
                            : a == "--channel" ? &options.channel : nullptr;
        if (!target || i + 1 >= args.size()) {
            std::fprintf(stderr, "ultrastore: %s\n", target ? (a + " wants a value").c_str() : ("unknown option " + a).c_str());
            return UltraStore::Cli::Usage(stderr);
        }
        *target = args[++i];
    }

    // One copy per user: a second start brings the running one forward and
    // quits. Outlives app.Run(), so it sits outside the try.
    UltraCanvasSingleInstance instance("org.ultraos.ultrastore", "UltraStore");
    const UltraCanvasSingleInstance::Start start = instance.Claim(UltraCanvasSingleInstance::MakeOpenRequest(links));
    if (start == UltraCanvasSingleInstance::Start::HandedOff) return EXIT_SUCCESS;

#ifdef __linux__
    if (!XInitThreads()) std::cerr << "warning: XInitThreads failed\n";
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);
#endif

    UltraCanvasApplication app;
    std::unique_ptr<UltraStore::Ui::StoreWindow> window;
    try {
        if (!app.Initialize("UltraStore")) {
            std::cerr << "Failed to initialize UltraCanvas application\n";
            return EXIT_FAILURE;
        }
        if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
            return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "UltraStore");
        app.SetDefaultWindowIcon(NormalizePath(GetResourcesDir() + "media/appicon/UltraStore.png"));

        window = std::make_unique<UltraStore::Ui::StoreWindow>(app, options);
        if (!window->Create()) {
            std::cerr << "Failed to create the store window\n";
            return EXIT_FAILURE;
        }
        window->Show();
        for (const std::string& link : links) window->OpenLink(link);
        // A later start with a link hands it to this window; without one it
        // brings the window forward.
        UltraStore::Ui::StoreWindow* raw = window.get();
        instance.ListenOpen([raw](const std::vector<std::string>& paths) {
            for (const std::string& p : paths) if (IsStoreLink(p)) raw->OpenLink(p);
            UltraCanvasSingleInstance::BringToFront();
        });
        app.Run();
        instance.Close();
    } catch (const std::exception& e) {
        std::cerr << "Fatal: " << e.what() << "\n";
        return EXIT_FAILURE;
    } catch (...) {
        std::cerr << "Fatal: unknown exception\n";
        return EXIT_FAILURE;
    }
    window.reset();   // joins the worker threads before the application goes
    return EXIT_SUCCESS;
}
