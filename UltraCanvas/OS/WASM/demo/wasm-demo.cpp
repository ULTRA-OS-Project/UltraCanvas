// OS/WASM/demo/wasm-demo.cpp
// Minimal UltraCanvas application used to validate the WASM backend in a
// browser: a window, pango-rendered labels, and a button wired to onClick.
// Version: 1.0.0
// Last Modified: 2026-08-12
// Author: UltraCanvas Framework
#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasButton.h"

#include <memory>
#include <string>

using namespace UltraCanvas;

int main(int, char* argv[]) {
    // Static lifetime is required under Emscripten: Run() never returns —
    // emscripten_set_main_loop() unwinds main()'s frame (running destructors
    // under -fwasm-exceptions), so a stack-local application would be
    // destroyed while its main loop is still ticking.
    static UltraCanvasApplication app;
    if (!app.Initialize(argv[0])) {
        return 1;
    }

    WindowConfig cfg;
    cfg.title = "UltraCanvas WASM Demo";
    cfg.width = 640;
    cfg.height = 400;
    auto window = CreateWindow(cfg);
    if (!window) {
        return 1;
    }

    auto title = std::make_shared<UltraCanvasLabel>("TitleLabel", 20, 24, 600, 32);
    title->SetText("Hello from UltraCanvas in WebAssembly!");
    title->SetFontSize(20);
    window->AddChild(title);

    auto counter = std::make_shared<UltraCanvasLabel>("CounterLabel", 20, 150, 600, 24);
    counter->SetText("Button clicked 0 times");
    counter->SetFontSize(14);
    window->AddChild(counter);

    auto button = CreateButton("DemoButton", 20, 90, 200, 40, "Click me");
    auto clicks = std::make_shared<int>(0);
    button->onClick = [counter, clicks]() {
        ++*clicks;
        counter->SetText("Button clicked " + std::to_string(*clicks) + " times");
    };
    window->AddChild(button);

    window->Show();
    app.Run(); // never returns in the browser; the main loop keeps ticking
    return 0;
}
