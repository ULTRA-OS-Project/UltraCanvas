// Apps/UltraAccount/main.cpp
// UltraAccount: the one ULTRA OS account - registration, the email and the
// phone number verified, the subscription, the recovery keys applications
// leave with it (ui/AccountWindow.h). The service is
// Server/UltraStoreAccount; the client every application shares is
// core/UltraAccountClient.h.
//
//   UltraAccount [--service URL]
//
// Version: 0.1.0
// Author: UltraCanvas Framework / ULTRA OS
#include "ui/AccountWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasSingleInstance.h"
#include "UltraCanvasUtils.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

#ifndef ULTRAACCOUNT_VERSION
#error "ULTRAACCOUNT_VERSION is set by Apps/UltraAccount/CMakeLists.txt from Docs/UltraAccount/CHANGELOG.md"
#endif

int main(int argc, char* argv[]) {
    std::string service;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") {
            std::cout << "UltraAccount - the ULTRA OS account: sign up, verify the email and the phone, the subscription.\n"
                      << "Usage: " << argv[0] << " [--service URL]\n";
            return 0;
        }
        if (a == "-v" || a == "--version") {
            std::cout << "UltraAccount " ULTRAACCOUNT_VERSION "\n";
            return 0;
        }
        if (a == "--service" && i + 1 < argc) service = argv[++i];
    }
    if (service.empty()) {
        const UltraAccount::Session saved = UltraAccount::Session::Load();
        service = saved.url.empty() ? UltraAccount::kDefaultServiceUrl : saved.url;
    }

    UltraCanvas::UltraCanvasSingleInstance instance("org.ultraos.ultraaccount", "UltraAccount");
    const auto start = instance.Claim();
    if (start == UltraCanvas::UltraCanvasSingleInstance::Start::HandedOff) return EXIT_SUCCESS;

    UltraCanvas::UltraCanvasApplication app;
    if (!app.Initialize("UltraAccount")) {
        std::cerr << "Failed to initialize UltraCanvas application\n";
        return EXIT_FAILURE;
    }
    if (start == UltraCanvas::UltraCanvasSingleInstance::Start::AlreadyRunning) {
        return UltraCanvas::UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "UltraAccount");
    }
    app.SetDefaultWindowIcon(UltraCanvas::NormalizePath(UltraCanvas::GetResourcesDir() + "media/appicon/UltraAccount.png"));

    auto window = std::make_unique<UltraAccount::AccountWindow>(app, service);
    if (!window->Create()) {
        std::cerr << "Failed to create the main window\n";
        return EXIT_FAILURE;
    }
    window->Show();
    instance.ListenRaise();
    app.Run();
    instance.Close();
    return EXIT_SUCCESS;
}

#ifdef _WIN32
#include <windows.h>
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int) {
    return main(__argc, __argv);
}
#endif
