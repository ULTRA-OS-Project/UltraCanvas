// Apps/UltraSocial/main.cpp
// UltraSocial application entry point. Creates the UltraCanvas application,
// opens the store under the user data directory, shows the compose window
// and runs the main loop.
// Version: 0.1.0 (Phase 1)
// Author: UltraCanvas Framework / ULTRA OS
#include "ui/UltraSocialApp.h"

#include "UltraCanvasApplication.h"

#include <UltraNet/UltraNetCore.h>

#include <cstdlib>
#include <string>

namespace {

// Per-platform user data directory for UltraSocial.
std::string UserDataDir() {
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
        return std::string(xdg) + "/UltraSocial";
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::string(home) + "/.local/share/UltraSocial";
    return "./UltraSocial";
}

} // namespace

int main() {
    UltraCanvas::UltraCanvasApplication app;
    if (!app.Initialize("UltraSocial"))
        return EXIT_FAILURE;

    UltraNet_Initialize();   // connectors speak HTTPS

    UltraSocial::UltraSocialApp social;
    if (!social.Initialize(UserDataDir()))
        return EXIT_FAILURE;

    auto window = social.CreateMainWindow();
    window->Show();

    app.Run();
    UltraNet_Shutdown();
    return EXIT_SUCCESS;
}
