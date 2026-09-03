// Apps/UltraMail/main.cpp
// UltraMail application entry point. Creates the UltraCanvas application, opens
// the local store under the user data directory, shows the main window (start
// page, or Toolbox + account info-tile bar once an account exists) and runs the
// main loop.
// Version: 0.2.0
// Last Modified: 2026-09-03
// Author: UltraCanvas Framework / ULTRA OS
#include "ui/UltraMailApp.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasConfig.h"

#include <cstdlib>
#include <string>

namespace {

// Per-platform user data directory for UltraMail.
std::string UserDataDir() {
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
        return std::string(xdg) + "/UltraMail";
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::string(home) + "/.local/share/UltraMail";
    return "./UltraMail";
}

} // namespace

int main() {
    UltraCanvas::UltraCanvasApplication app;
    if (!app.Initialize("UltraMail"))
        return EXIT_FAILURE;
    app.SetDefaultWindowIcon(
        UltraCanvas::NormalizePath(UltraCanvas::GetResourcesDir() + "media/appicon/UltraMail.svg"));

    UltraMail::UltraMailApp mail;
    if (!mail.Initialize(UserDataDir()))
        return EXIT_FAILURE;

    auto window = mail.CreateMainWindow();
    window->Show();

    app.Run();
    return EXIT_SUCCESS;
}
