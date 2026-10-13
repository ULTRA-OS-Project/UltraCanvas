// Apps/UltraPress/ui/main.cpp
// ultrapress: the one UltraPress program - the commands and the window.
// A command word first (new, check, page, publish, import, help) runs that
// command without a window (cli/UltraPressCli.h); a site folder opens it in
// the editor - the pages and the current page's blocks on the left, the
// page rendered on real elements in the middle, the selected block's
// properties on the right (ui/UltraPressWindow.h). The renderer is the one
// that becomes the published page under Emscripten.
//
//   ultrapress <site-dir>            open a site folder in the editor
//   ultrapress <site-dir> --check    render every page without a window and
//                                    print the element counts and warnings
//   ultrapress <command> ...         new, check, page, publish, import, help
//   ultrapress --version
//
// There were two programs, ultrapress and UltraPress, until the macOS build
// showed that two names differing only in case are one name on a
// case-insensitive filesystem. One program, like ultrafibu.
// Version: 0.4.1 - one program: the commands and the window
// Version: 0.3.0 - editing: blocks added, moved, removed and their properties changed; Save
// Version: 0.2.0
// Last Modified: 2026-10-13
// Author: UltraCanvas Framework / ULTRA OS
#include "UltraPressBlocks.h"
#include "UltraPressCli.h"
#include "UltraPressModel.h"
#include "UltraPressRender.h"
#include "UltraPressWindow.h"

#include "UltraCanvasApplication.h"
#include "UltraCanvasButton.h"
#include "UltraCanvasContainer.h"
#include "UltraCanvasLabel.h"
#include "UltraCanvasUtils.h"
#include "UltraCanvasWindow.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#ifndef ULTRAPRESS_VERSION
#error "ULTRAPRESS_VERSION comes from the first line of Docs/UltraPress/CHANGELOG.md through CMake; never a literal here"
#endif

using namespace UltraCanvas;

namespace {

const Color kPaper(255, 255, 255, 255);

int Usage() {
    std::fprintf(stderr,
                 "UltraPress %s\n"
                 "  ultrapress <site-dir>           open a site folder in the editor\n"
                 "  ultrapress <site-dir> --check   render every page without a window, print what was built\n"
                 "  ultrapress <command> ...        new, check, page, publish, import; `ultrapress help` lists them\n"
                 "  ultrapress --version\n",
                 ULTRAPRESS_VERSION);
    return 2;
}

// Whether the first argument is a command word rather than a site folder.
// A folder that happens to be called `check` is opened as `./check`.
bool IsCommand(const std::string& word) {
    static const char* const kCommands[] = {"new", "check", "page", "publish", "import",
                                            "help", "--help", "-h"};
    for (const char* command : kCommands) {
        if (word == command) return true;
    }
    return false;
}

int RunCheck(const UltraPress::Site& site, const std::string& siteDir) {
    UltraPress::RenderOptions options;
    options.mediaRoot = siteDir;
    options.onLink = [](const std::string&) {};
    UltraPress::PageRenderer renderer(site, options);
    int failures = 0;
    for (const UltraPress::Page& page : site.pages) {
        UltraPress::RenderedPage rendered = renderer.Render(page);
        std::printf("%s: %d elements\n", page.slug.c_str(), rendered.elementCount);
        for (const std::string& warning : rendered.warnings) {
            std::printf("  %s\n", warning.c_str());
            if (warning.rfind("unknown block", 0) == 0) ++failures;
        }
    }
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char* argv[]) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty()) return Usage();
    if (args[0] == "--version" || args[0] == "-v") {
        std::printf("UltraPress %s\n", ULTRAPRESS_VERSION);
        return 0;
    }
    if (IsCommand(args[0])) return UltraPress::CommandLine(argc, argv);
    const std::string siteDir = args[0];
    bool check = false;
    for (size_t i = 1; i < args.size(); ++i) {
        if (args[i] == "--check") check = true;
        else return Usage();
    }

    struct { UltraPress::Site site; } viewer;
    std::vector<std::string> errors;
    if (!UltraPress::LoadSite(siteDir, viewer.site, errors)) {
        for (const std::string& error : errors) std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }
    for (const UltraPress::ValidationIssue& issue : UltraPress::ValidateSite(viewer.site)) {
        std::fprintf(stderr, "%s %s: %s\n", issue.page.c_str(), issue.path.c_str(), issue.message.c_str());
    }
    if (check) return RunCheck(viewer.site, siteDir);

    static UltraCanvasApplication app;
    if (!app.Initialize(argv[0])) return 1;

    WindowConfig cfg;
    cfg.title = viewer.site.name + " - UltraPress " ULTRAPRESS_VERSION;
    cfg.width = 1500;
    cfg.height = 920;
    cfg.backgroundColor = kPaper;
    auto window = CreateWindow(cfg);
    if (!window) return 1;

    // Static lifetime: under Emscripten Run() never returns and unwinds
    // main()'s frame while the loop keeps ticking.
    static UltraPress::EditorWindow editor(std::move(viewer.site), siteDir);
    editor.Build(window);
#if defined(__EMSCRIPTEN__)
    window->Maximize();
#endif
    window->Show();
    app.Run();
    return 0;
}
