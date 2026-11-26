// Apps/UltraOffice/main.cpp
// UltraOffice - documents, spreadsheets and presentations in one window
// (Docs/UltraOffice/README.md). One copy per user: a second start hands its
// files to the running window and quits (UltraCanvasSingleInstance.h).
//
//   UltraOffice                          the Start tab
//   UltraOffice report.docx budget.ods   each file in a tab of its kind
//
// Version: 0.2.0 - the language of the settings (UltraOfficeSettings.h), unless ULTRACANVAS_LANG names one
// Version: 0.1.0
// Last Modified: 2026-11-25
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraOfficeSettings.h"
#include "UltraOfficeWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasAppSettings.h"
#include "UltraCanvasConfig.h"
#include "UltraCanvasPathUtf8.h"   // GetEnvUtf8
#include "UltraCanvasSingleInstance.h"
#include "UltraCanvasUtils.h"
#include "UltraLocalisation/UltraLocalisation.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#ifndef ULTRAOFFICE_VERSION
#error "ULTRAOFFICE_VERSION must come from Docs/UltraOffice/CHANGELOG.md via CMake"
#endif

using namespace UltraCanvas;

namespace {

void PrintUsage(const char* program) {
    std::printf("UltraOffice - documents, spreadsheets and presentations.\n"
                "Usage: %s [options] [files...]\n"
                "  -h, --help      show this message\n"
                "  -v, --version   show the version\n", program);
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> paths;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "-h" || a == "--help") { PrintUsage(argv[0]); return EXIT_SUCCESS; }
        if (a == "-v" || a == "--version") { std::printf("UltraOffice %s\n", ULTRAOFFICE_VERSION); return EXIT_SUCCESS; }
        if (!a.empty() && a[0] == '-') {
            std::fprintf(stderr, "Unknown option: %s\n", a.c_str());
            PrintUsage(argv[0]);
            return EXIT_FAILURE;
        }
        paths.push_back(a);
    }

    // Only a start that opens a window claims the one copy per user; the
    // paths go to the running copy as absolute ones.
    UltraCanvasSingleInstance instance("org.ultraos.ultraoffice", "UltraOffice");
    const UltraCanvasSingleInstance::Start start = instance.Claim(UltraCanvasSingleInstance::MakeOpenRequest(paths));
    if (start == UltraCanvasSingleInstance::Start::HandedOff) return EXIT_SUCCESS;

    // The language the Settings window chose for UltraOffice, before the
    // catalogues are read; a language named for the run (ULTRACANVAS_LANG)
    // still wins.
    {
        UltraCanvasAppSettings stored("UltraOffice");
        stored.Load();
        const std::string language = UltraOffice::OfficeSettings(stored).GetLanguage();
        if (language != "system" && GetEnvUtf8("ULTRACANVAS_LANG").empty()) UltraLocalisation::SetLanguageOverride(language);
    }

    UltraCanvasApplication app;
    if (!app.Initialize("UltraOffice")) {
        std::fprintf(stderr, "UltraOffice: the UltraCanvas application could not be initialised (no display?).\n");
        return EXIT_FAILURE;
    }
    if (start == UltraCanvasSingleInstance::Start::AlreadyRunning)
        return UltraCanvasSingleInstance::RunAlreadyRunningAlert(app, "UltraOffice");
    app.SetDefaultWindowIcon(NormalizePath(GetResourcesDir() + "media/appicon/UltraOffice.png"));

    auto window = std::make_shared<UltraOffice::UltraOfficeWindow>();
    WindowConfig config;
    config.title = "UltraOffice";
    config.width = 1280;
    config.height = 820;
    config.minWidth = 760;
    config.minHeight = 480;
    config.resizable = true;
    if (!window->Create(config)) {
        std::fprintf(stderr, "UltraOffice: the window could not be created.\n");
        return EXIT_FAILURE;
    }
    window->onWindowClosed = [&app]() { app.Exit(); };

    // A later start's files open here, and the window comes forward.
    UltraOffice::UltraOfficeWindow* shown = window.get();
    instance.ListenOpen([shown](const std::vector<std::string>& handed) {
        shown->OpenPaths(handed);
        UltraCanvasSingleInstance::BringToFront();
    });

    window->Initialize(paths);
    window->Show();
    app.Run();
    instance.Close();   // no request reaches the window once it starts to go
    return EXIT_SUCCESS;
}
