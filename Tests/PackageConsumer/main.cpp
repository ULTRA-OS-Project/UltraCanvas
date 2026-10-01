// Tests/PackageConsumer/main.cpp
// Builds against an installed UltraCanvas through find_package(UltraCanvas)
// alone - see CMakeLists.txt beside it.
//
// Two things are checked, and the split between them matters. Compiling and
// linking proves the package: the headers resolve from the install prefix
// (including the ones that reach into Plugins/), the exported targets carry
// their dependencies, and the symbols are there. Running proves only what
// can run without a display, so main() exercises the JSON engine and the
// UTF-8 path helpers and prints the version. The windowed path is compiled
// and linked but never executed: a window needs a display server, and CI
// has none.
//
// Version: 1.0.0
// Last Modified: 2026-10-01
// Author: UltraCanvas Framework

#include "UltraCanvasApplication.h"
#include "UltraCanvasWindow.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasTextInput.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasPathUtf8.h"
#include "DataFormats/UltraCanvasJSON.h"
// A plugin element, from include/Plugins: the catalogue's charts, diagrams and
// gauges are part of the same library and must come with the package.
#include "Plugins/Diagrams/UltraCanvasGaugeDiagramElement.h"

#include <cstdlib>
#include <iostream>
#include <string>

using namespace UltraCanvas;

namespace {

// Compiled and linked, never called: the windowed half of the framework.
// Taking its address below keeps the linker from discarding it.
int WindowedMain() {
    UltraCanvasApplication app;
    if (!app.Initialize("PackageConsumer")) {
        return EXIT_FAILURE;
    }

    WindowConfig config;
    config.title  = "PackageConsumer";
    config.width  = 640;
    config.height = 400;
    auto window = CreateWindow(config);

    auto label  = CreateLabel("greeting", 20, 20, 300, 24, "Hello from an installed UltraCanvas");
    auto input  = CreateTextInput("name", 20, 60, 240, 26);
    auto button = CreateButton("quit", 20, 100, 120, 32, "Quit");
    auto gauge  = std::make_shared<UltraCanvasGaugeDiagramElement>("progress", 20, 150, 300, 24);
    button->onClick = [&app]() { app.RequestExit(); };

    window->AddChild(label);
    window->AddChild(input);
    window->AddChild(button);
    window->AddChild(gauge);
    window->Show();

    app.Run();
    return EXIT_SUCCESS;
}

} // namespace

int main() {
    // Headless: the JSON engine is in the core archive (and, in a static
    // build, in the uc-yyjson archive the package has to export with it).
    JSONParseResult parsed;
    JSONValue doc = JSON::Parse(R"({"framework":"UltraCanvas","elements":[1,2,3]})", &parsed);
    if (!parsed.success) {
        std::cerr << "JSON parse failed: " << parsed.errorMessage << "\n";
        return EXIT_FAILURE;
    }
    const std::string name = doc["framework"].GetString("");
    const std::string roundTrip = JSON::Serialize(doc);
    if (name != "UltraCanvas" || roundTrip.find("\"elements\"") == std::string::npos) {
        std::cerr << "JSON round trip failed: " << roundTrip << "\n";
        return EXIT_FAILURE;
    }

    // Header-only, but the header has to be where the package says it is.
    const std::string folder = PathToUtf8(PathFromUtf8("ไทย/🎨") / "file.txt");
    if (folder.find("file.txt") == std::string::npos) {
        std::cerr << "PathUtf8 round trip failed: " << folder << "\n";
        return EXIT_FAILURE;
    }

    volatile auto keepWindowedCode = &WindowedMain;
    (void)keepWindowedCode;

    std::cout << "UltraCanvas " << versionString << " installed package: OK\n";
    return EXIT_SUCCESS;
}
