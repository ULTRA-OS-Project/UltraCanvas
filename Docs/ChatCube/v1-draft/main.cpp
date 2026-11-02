// ChatCube/main.cpp
// ChatCube application entry point.
// Initialises the UltraCanvas application, registers provider plugins,
// creates the main window, and starts the event loop.
// Version: 1.0.0
// Last Modified: 2026-04-13
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "ChatCubeWindow.h"
#include "ChatCubeProviderManager.h"
#include "MatrixProviderPlugin.h"
// Future: #include "TelegramProviderPlugin.h"
// Future: #include "XMPPProviderPlugin.h"
// Future: #include "ChatCubeDirectPlugin.h"

#include <iostream>
#include <memory>

using namespace UltraCanvas;
using namespace UltraCanvas::ChatCube;

// ============================================================================
// ENTRY POINT
// ============================================================================

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    // ── Step 1: Create and initialise the UltraCanvas application ─────────
    auto app = UltraCanvasApplication::Create();
    if (!app->Initialize("ChatCube")) {
        std::cerr << "[ChatCube] Failed to initialise UltraCanvas application."
                  << std::endl;
        return 1;
    }

    // ── Step 2: Initialise the ChatCube provider manager ──────────────────
    ChatManagerConfig managerConfig = ChatManagerConfig::Default();
    managerConfig.PluginDirectory   = "Modules/";
    managerConfig.CacheDirectory    = "Resources/Data/cache/";

    auto& mgr = ChatCubeProviderManager::GetInstance();
    if (!mgr.Initialize(managerConfig)) {
        std::cerr << "[ChatCube] Failed to initialise provider manager." << std::endl;
        return 1;
    }

    // ── Step 3: Register built-in / compiled-in plugins ───────────────────
    // Matrix — full open API, shipped as reference plugin
    mgr.RegisterPlugin(std::make_shared<MatrixProviderPlugin>());

    // (Additional plugins registered here as they are implemented)
    // mgr.RegisterPlugin(std::make_shared<TelegramProviderPlugin>());
    // mgr.RegisterPlugin(std::make_shared<XMPPProviderPlugin>());
    // mgr.RegisterPlugin(std::make_shared<ChatCubeDirectPlugin>());

    std::cout << "[ChatCube] Registered "
              << mgr.GetAllPlugins().size()
              << " provider plugin(s)." << std::endl;

    // ── Step 4: Create the main window ────────────────────────────────────
    ChatCubeWindowStyle windowStyle;
    windowStyle.WindowWidth    = 900;
    windowStyle.WindowHeight   = 620;
    windowStyle.LeftPanelWidth = 280;

    auto window = std::make_shared<ChatCubeWindow>();
    if (!window->Initialize(windowStyle)) {
        std::cerr << "[ChatCube] Failed to initialise main window." << std::endl;
        mgr.Shutdown();
        return 1;
    }

    // ── Step 5: Register window with the application and show it ──────────
    app->RegisterWindow(window);
    window->Show();
    window->CenterOnScreen();

    std::cout << "[ChatCube] Starting event loop..." << std::endl;

    // ── Step 6: Run the UltraCanvas event loop ─────────────────────────────
    const int exitCode = app->Run();

    // ── Step 7: Clean shutdown ─────────────────────────────────────────────
    mgr.Shutdown();
    std::cout << "[ChatCube] Shutdown complete. Exit code: " << exitCode << std::endl;

    return exitCode;
}
